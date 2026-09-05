#include <algorithm>
#include <chrono>
#include <iostream>

#include <lsp/json/json.h>
#include <lsp/uri.h>

#include "LspClientImpl.hpp"

namespace {

auto markedStringToText(const lsp::MarkedString &marked) -> std::string {
    if (std::holds_alternative<lsp::String>(marked)) {
        return std::get<lsp::String>(marked);
    }
    return std::get<lsp::MarkedStringWithLanguage>(marked).value;
}

auto hoverToPlainText(const lsp::Hover &hover) -> std::string {
    // Hover::contents is OneOf<...>, i.e. a plain std::variant.
    if (std::holds_alternative<lsp::MarkupContent>(hover.contents)) {
        return std::get<lsp::MarkupContent>(hover.contents).value;
    }
    if (std::holds_alternative<lsp::MarkedString>(hover.contents)) {
        return markedStringToText(std::get<lsp::MarkedString>(hover.contents));
    }
    auto text = std::string();
    for (auto const &marked : std::get<lsp::Array<lsp::MarkedString>>(hover.contents)) {
        if (!text.empty()) {
            text += "\n";
        }
        text += markedStringToText(marked);
    }
    return text;
}

/// One-line rendering of a capability value: booleans as-is, option objects
/// summarised by their interesting keys, everything else as compact JSON.
auto describeCapability(const lsp::json::Value &value) -> std::string {
    if (value.isBoolean()) {
        return value.boolean() ? "true" : "false";
    }
    if (value.isNull()) {
        return "null";
    }
    if (value.isObject()) {
        auto const &object = value.object();
        if (object.isEmpty()) {
            return "yes";
        }
        auto keys = std::string();
        for (const auto &[k, v] : object) {
            (void)v;
            keys += keys.empty() ? "" : ", ";
            keys += k;
        }
        return "yes {" + keys + "}";
    }
    return lsp::json::stringify(value);
}

} // namespace

LspClientImpl::LspClientImpl(const std::string &executable,
                             const std::vector<std::string> &arguments,
                             const std::string &documentRoot)
    : m_documentRoot(documentRoot) {
    startServer(executable, arguments);
    initializeLspServer();
}

LspClientImpl::~LspClientImpl() {
    shutdownLspServer();
    stopServer();
}

void LspClientImpl::setDiagnosticsCallback(DiagnosticsCallback callback) {
    m_diagnosticsCallback = std::move(callback);
}

void LspClientImpl::setTraceCallback(TraceCallback callback) {
    m_traceCallback = std::move(callback);
}

void LspClientImpl::setProgressCallback(ProgressCallback callback) {
    m_progressCallback = std::move(callback);
}

void LspClientImpl::trace(const std::string &message) const {
    if (m_traceCallback) {
        m_traceCallback(message);
    }
}

std::vector<std::pair<std::string, std::string>> LspClientImpl::capabilities() const {
    auto lock = std::lock_guard(m_nameMutex);
    return m_capabilities;
}

bool LspClientImpl::hasCapability(const std::string &name) const {
    auto lock = std::lock_guard(m_nameMutex);
    for (auto const &[key, value] : m_capabilities) {
        if (key == name) {
            return value != "false" && value != "null";
        }
    }
    return false;
}

std::string LspClientImpl::serverName() const {
    auto lock = std::lock_guard(m_nameMutex);
    return m_serverName;
}

std::vector<std::pair<std::string, int>> LspClientImpl::documents() const {
    auto lock = std::lock_guard(m_documentsMutex);
    auto out = std::vector<std::pair<std::string, int>>();
    out.reserve(m_documentVersions.size());
    for (auto const &[file, version] : m_documentVersions) {
        out.emplace_back(file, version);
    }
    return out;
}

void LspClientImpl::startServer(const std::string &executable,
                                const std::vector<std::string> &arguments) {
    // lsp::Process handles fork/exec and CreateProcess, and hands back a single
    // bidirectional stream - which is what lsp::Connection expects.
    m_process = std::make_unique<lsp::Process>(lsp::Process::start(executable, arguments));
    m_connection = std::make_unique<lsp::Connection>(m_process->stdIO());
    m_messageHandler = std::make_unique<lsp::MessageHandler>(std::move(*m_connection));

    m_messageHandler->on<lsp::notifications::TextDocumentPublishDiagnostics>(
        [this](lsp::notifications::TextDocumentPublishDiagnostics::Params &&params) {
            trace("<-- publishDiagnostics " + std::string(params.uri.path()) + " (" +
                  std::to_string(params.diagnostics.size()) + ")");
            if (m_diagnosticsCallback) {
                m_diagnosticsCallback(std::string(params.uri.path()), params.diagnostics);
            }
        });

    // clangd asks permission before reporting background work. Refusing to answer
    // (or not advertising window.workDoneProgress) means no indexing progress at all.
    m_messageHandler->on<lsp::requests::WindowWorkDoneProgressCreate>(
        [](lsp::requests::WindowWorkDoneProgressCreate::Params &&) {
            return lsp::requests::WindowWorkDoneProgressCreate::Result{};
        });

    m_messageHandler->on<lsp::notifications::Progress>(
        [this](lsp::notifications::Progress::Params &&params) {
            if (!m_progressCallback || !params.value.isObject()) {
                return;
            }
            auto const &object = params.value.object();
            auto field = [&object](const char *key) -> std::string {
                auto *it = object.find(key);
                return (it && it->isString()) ? it->string() : std::string{};
            };
            auto const kind = field("kind");
            auto percentage = -1;
            if (auto *it = object.find("percentage"); it && it->isNumber()) {
                percentage = static_cast<int>(it->number());
            }
            trace("<-- $/progress " + kind + " " + field("title") + " " + field("message") +
                  (percentage >= 0 ? " " + std::to_string(percentage) + "%" : ""));
            m_progressCallback(field("title"), field("message"), percentage, kind != "end");
        });

    // Responses and notifications only arrive while somebody is reading the
    // stream, so the handler needs a thread of its own.
    m_running = true;
    m_readerThread = std::thread([this]() {
        while (m_running.load()) {
            try {
                m_messageHandler->processNextMessage();
            } catch (const lsp::ConnectionError &) {
                break; // server went away - nothing left to read
            } catch (const std::exception &e) {
                // A single malformed message or a stuck callback must not tear
                // down the whole connection: log it and keep reading so later
                // responses (hover, completion, ...) still reach their callbacks.
                std::cerr << "LspClientImpl: error handling message, skipping: " << e.what()
                          << std::endl;
            }
        }
        m_running = false;
    });
}

void LspClientImpl::stopServer() {
    // shutdownLspServer() has asked the server to exit; give it a moment to do so
    // on its own, otherwise it dies on a signal and logs a transport error.
    if (m_ready.load()) {
        for (auto i = 0; i < 50 && m_process && m_process->isRunning(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (m_process && m_process->isRunning()) {
        m_process->terminate();
    }
    m_running = false;
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    m_messageHandler.reset();
    m_connection.reset();
    m_process.reset();
    m_ready = false;
}

void LspClientImpl::initializeLspServer() {
    auto initializeParams = lsp::requests::Initialize::Params{};
    initializeParams.rootUri = lsp::Uri::fileUriFromPath(m_documentRoot);
    initializeParams.capabilities = lsp::ClientCapabilities{
        .textDocument =
            lsp::TextDocumentClientCapabilities{
                .synchronization = lsp::TextDocumentSyncClientCapabilities{},
                .completion = lsp::CompletionClientCapabilities{},
                .hover =
                    lsp::HoverClientCapabilities{
                        .contentFormat = {{lsp::MarkupKind::PlainText}},
                    },
            },
        // Without this clangd never reports indexing progress at all.
        .window =
            lsp::WindowClientCapabilities{
                .workDoneProgress = true,
            },
    };

    m_messageHandler->sendRequest<lsp::requests::Initialize>(
        std::move(initializeParams),
        [this](lsp::requests::Initialize::Result &&result) {
            // The server only accepts real requests after it has been told the
            // client is ready.
            m_messageHandler->sendNotification<lsp::notifications::Initialized>(
                lsp::notifications::Initialized::Params{});
            m_ready = true;
            {
                auto lock = std::lock_guard(m_nameMutex);
                m_serverName = result.serverInfo.has_value() ? result.serverInfo->name : "unknown";

                // Serialising the whole struct beats reading 36 heterogeneous
                // Opt<OneOf<...>> fields by hand, and keeps working when the
                // protocol gains capabilities this code has never heard of.
                std::string serialized;
                {
                    lsp::json::Writer writer(serialized);
                    auto objectWriter = writer.beginObject();
                    lsp::writeJson(result.capabilities, objectWriter);
                }
                auto asJson = lsp::json::parse(serialized);
                if (asJson.isObject()) {
                    for (auto const &[key, value] : asJson.object()) {
                        m_capabilities.emplace_back(key, describeCapability(value));
                    }
                    std::sort(m_capabilities.begin(), m_capabilities.end());
                }
            }
            trace("<-- initialize result; --> initialized");
            if (result.serverInfo.has_value()) {
                std::cerr << "LspClientImpl: connected to " << result.serverInfo->name << std::endl;
            }
        },
        [this](const lsp::ResponseError &error) {
            trace(std::string("<-- initialize FAILED: ") + error.what());
            std::cerr << "LspClientImpl: initialize failed: " << error.what() << std::endl;
        });
}

void LspClientImpl::shutdownLspServer() {
    if (!m_messageHandler || !m_ready.load()) {
        return;
    }
    m_messageHandler->sendRequest<lsp::requests::Shutdown>(
        [this](lsp::requests::Shutdown::Result &&) {
            m_messageHandler->sendNotification<lsp::notifications::Exit>();
            m_running = false;
        },
        [this](const lsp::ResponseError &) { m_running = false; });
}

void LspClientImpl::syncDocument(const std::string &fileName, const std::string &text,
                                 const std::string &languageId) {
    if (!m_ready.load()) {
        return;
    }

    auto isNew = false;
    auto version = 0;
    {
        auto lock = std::lock_guard(m_documentsMutex);
        auto it = m_documentVersions.find(fileName);
        isNew = it == m_documentVersions.end();
        version = isNew ? 1 : ++it->second;
        if (isNew) {
            m_documentVersions[fileName] = version;
        }
    }

    if (isNew) {
        auto params = lsp::notifications::TextDocumentDidOpen::Params{};
        params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
        params.textDocument.languageId = std::string(languageId);
        params.textDocument.version = version;
        params.textDocument.text = text;
        m_messageHandler->sendNotification<lsp::notifications::TextDocumentDidOpen>(
            std::move(params));
        trace("--> didOpen " + fileName + " v" + std::to_string(version));
        return;
    }

    // Full-document sync. Incremental sync would need range tracking the editor
    // does not expose yet.
    auto params = lsp::notifications::TextDocumentDidChange::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.textDocument.version = version;
    params.contentChanges = {lsp::TextDocumentContentChangeWholeDocument{.text = text}};
    m_messageHandler->sendNotification<lsp::notifications::TextDocumentDidChange>(
        std::move(params));
    trace("--> didChange " + fileName + " v" + std::to_string(version));
}

void LspClientImpl::closeDocument(const std::string &fileName) {
    {
        auto lock = std::lock_guard(m_documentsMutex);
        if (m_documentVersions.erase(fileName) == 0) {
            return;
        }
    }
    if (!m_ready.load()) {
        return;
    }
    auto params = lsp::notifications::TextDocumentDidClose::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    m_messageHandler->sendNotification<lsp::notifications::TextDocumentDidClose>(
        std::move(params));
    trace("--> didClose " + fileName);
}

void LspClientImpl::requestCompletion(const std::string &fileName, uint line, uint column,
                                      CompletionCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocumentCompletion::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.position = {.line = line, .character = column};

    trace("--> completion " + fileName + ":" + std::to_string(line) + ":" + std::to_string(column));
    m_messageHandler->sendRequest<lsp::requests::TextDocumentCompletion>(
        std::move(params),
        [this, callback](lsp::requests::TextDocumentCompletion::Result &&result) {
            auto items = std::vector<lsp::CompletionItem>();
            if (result.holdsAlternative<lsp::CompletionList>()) {
                items = std::move(result.get<lsp::CompletionList>().items);
            } else if (!result.isNull()) {
                items = std::move(result.get<lsp::Array<lsp::CompletionItem>>());
            }
            trace("<-- completion result (" + std::to_string(items.size()) + " items)");
            callback(std::move(items));
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: completion failed: " << error.what() << std::endl;
            callback({});
        });
}

void LspClientImpl::requestDefinition(const std::string &fileName, uint line, uint column,
                                      DefinitionCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocumentDefinition::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.position = {.line = line, .character = column};

    trace("--> definition " + fileName + ":" + std::to_string(line) + ":" + std::to_string(column));
    m_messageHandler->sendRequest<lsp::requests::TextDocumentDefinition>(
        std::move(params),
        [this, callback](lsp::requests::TextDocumentDefinition::Result &&result) {
            auto out = std::vector<Location>();
            auto addLocation = [&out](const lsp::Location &location) {
                out.push_back(Location{std::string(location.uri.path()),
                                       static_cast<int>(location.range.start.line),
                                       static_cast<int>(location.range.start.character)});
            };

            if (!result.isNull()) {
                if (result.holdsAlternative<lsp::Definition>()) {
                    auto const &definition = result.get<lsp::Definition>();
                    if (std::holds_alternative<lsp::Location>(definition)) {
                        addLocation(std::get<lsp::Location>(definition));
                    } else {
                        for (auto const &location :
                             std::get<lsp::Array<lsp::Location>>(definition)) {
                            addLocation(location);
                        }
                    }
                } else {
                    // LocationLink form: the target range is what we want to jump to.
                    for (auto const &link : result.get<lsp::Array<lsp::DefinitionLink>>()) {
                        out.push_back(
                            Location{std::string(link.targetUri.path()),
                                     static_cast<int>(link.targetSelectionRange.start.line),
                                     static_cast<int>(link.targetSelectionRange.start.character)});
                    }
                }
            }
            trace("<-- definition result (" + std::to_string(out.size()) + " locations)");
            callback(std::move(out));
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: definition failed: " << error.what() << std::endl;
            callback({});
        });
}

/// WorkspaceEdit has two forms; `changes` is the simple one and the only one
/// handled here. `documentChanges` additionally creates/renames/deletes files,
/// which needs filesystem work the caller does not do yet.
std::vector<LspClientImpl::TextEdit> LspClientImpl::flatten(const lsp::WorkspaceEdit &edit) {
    auto out = std::vector<TextEdit>();

    auto append = [&out](const std::string &path, const lsp::TextEdit &textEdit) {
        out.push_back(TextEdit{path, static_cast<int>(textEdit.range.start.line),
                               static_cast<int>(textEdit.range.start.character),
                               static_cast<int>(textEdit.range.end.line),
                               static_cast<int>(textEdit.range.end.character), textEdit.newText});
    };

    if (edit.changes.has_value()) {
        for (auto const &[uri, edits] : *edit.changes) {
            auto path = std::string(uri.path());
            for (auto const &textEdit : edits) {
                append(path, textEdit);
            }
        }
    }

    if (edit.documentChanges.has_value()) {
        for (auto const &change : *edit.documentChanges) {
            if (!std::holds_alternative<lsp::TextDocumentEdit>(change)) {
                continue; // create/rename/delete file - not supported yet
            }
            auto const &documentEdit = std::get<lsp::TextDocumentEdit>(change);
            auto path = std::string(documentEdit.textDocument.uri.path());
            for (auto const &one : documentEdit.edits) {
                if (std::holds_alternative<lsp::TextEdit>(one)) {
                    append(path, std::get<lsp::TextEdit>(one));
                } else {
                    auto const &annotated = std::get<lsp::AnnotatedTextEdit>(one);
                    out.push_back(TextEdit{path, static_cast<int>(annotated.range.start.line),
                                           static_cast<int>(annotated.range.start.character),
                                           static_cast<int>(annotated.range.end.line),
                                           static_cast<int>(annotated.range.end.character),
                                           annotated.newText});
                }
            }
        }
    }
    return out;
}

void LspClientImpl::requestCodeActions(const std::string &fileName, uint startLine,
                                       uint startCharacter, uint endLine, uint endCharacter,
                                       const std::vector<std::string> &kinds,
                                       CodeActionCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocumentCodeAction::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.range = {.start = {.line = startLine, .character = startCharacter},
                    .end = {.line = endLine, .character = endCharacter}};
    if (!kinds.empty()) {
        auto only = lsp::Array<lsp::CodeActionKindEnum>();
        for (auto const &kind : kinds) {
            auto value = lsp::CodeActionKindEnum();
            value = std::string(kind);
            only.push_back(value);
        }
        params.context.only = only;
    }

    trace("--> codeAction " + fileName + ":" + std::to_string(startLine));
    m_messageHandler->sendRequest<lsp::requests::TextDocumentCodeAction>(
        std::move(params),
        [this, callback](lsp::requests::TextDocumentCodeAction::Result &&result) {
            auto actions = std::vector<CodeAction>();
            if (!result.isNull()) {
                for (auto const &entry : *result) {
                    if (std::holds_alternative<lsp::CodeAction>(entry)) {
                        auto const &action = std::get<lsp::CodeAction>(entry);
                        auto item = CodeAction{action.title, "", {}, false};
                        if (action.kind.has_value()) {
                            item.kind = std::string(action.kind->value());
                        }
                        if (action.edit.has_value()) {
                            item.edits = flatten(*action.edit);
                        }
                        // No edit means the server wants executeCommand and will
                        // push the result back via workspace/applyEdit.
                        item.needsCommand = item.edits.empty();
                        actions.push_back(std::move(item));
                    } else {
                        auto const &command = std::get<lsp::Command>(entry);
                        actions.push_back(CodeAction{command.title, "command", {}, true});
                    }
                }
            }
            trace("<-- codeAction result (" + std::to_string(actions.size()) + " actions)");
            callback(std::move(actions));
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: codeAction failed: " << error.what() << std::endl;
            callback({});
        });
}

void LspClientImpl::requestRename(const std::string &fileName, uint line, uint column,
                                  const std::string &newName, RenameCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocumentRename::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.position = {.line = line, .character = column};
    params.newName = newName;

    trace("--> rename " + fileName + ":" + std::to_string(line) + " -> " + newName);
    m_messageHandler->sendRequest<lsp::requests::TextDocumentRename>(
        std::move(params),
        [this, callback](lsp::requests::TextDocumentRename::Result &&result) {
            auto edits = result.isNull() ? std::vector<TextEdit>() : flatten(*result);
            trace("<-- rename result (" + std::to_string(edits.size()) + " edits)");
            callback(std::move(edits));
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: rename failed: " << error.what() << std::endl;
            callback({});
        });
}

void LspClientImpl::requestHover(const std::string &fileName, uint line, uint column,
                                 HoverCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocumentHover::Params{};
    params.textDocument.uri = lsp::Uri::fileUriFromPath(fileName);
    params.position = {.line = line, .character = column};

    trace("--> hover " + fileName + ":" + std::to_string(line) + ":" + std::to_string(column));
    m_messageHandler->sendRequest<lsp::requests::TextDocumentHover>(
        std::move(params),
        [this, callback](lsp::requests::TextDocumentHover::Result &&result) {
            auto text = result.isNull() ? std::string{} : hoverToPlainText(*result);
            trace("<-- hover result (" + std::to_string(text.size()) + " chars)");
            callback(text);
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: hover failed: " << error.what() << std::endl;
            callback({});
        });
}
