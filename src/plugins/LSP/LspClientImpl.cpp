#include <chrono>
#include <filesystem>
#include <iostream>

#include <lsp/io/stream.h>

#include "LspClientImpl.hpp"

namespace {

auto languageIdForFile(const std::string &fileName) -> std::string {
    auto ext = std::filesystem::path(fileName).extension().string();
    if (ext == ".c") {
        return "c";
    }
    if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
        return "cpp";
    }
    return "cpp";
}

auto markedStringToText(const lsp::MarkedString &marked) -> std::string {
    if (std::holds_alternative<lsp::String>(marked)) {
        return std::get<lsp::String>(marked);
    }
    return std::get<lsp::MarkedString_Language_Value>(marked).value;
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

void LspClientImpl::trace(const std::string &message) const {
    if (m_traceCallback) {
        m_traceCallback(message);
    }
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
    m_messageHandler = std::make_unique<lsp::MessageHandler>(*m_connection);

    m_messageHandler->add<lsp::notifications::TextDocument_PublishDiagnostics>(
        [this](lsp::notifications::TextDocument_PublishDiagnostics::Params &&params) {
            trace("<-- publishDiagnostics " + std::string(params.uri.path()) + " (" +
                  std::to_string(params.diagnostics.size()) + ")");
            if (m_diagnosticsCallback) {
                m_diagnosticsCallback(std::string(params.uri.path()), params.diagnostics);
            }
        });

    // Responses and notifications only arrive while somebody is reading the
    // stream, so the handler needs a thread of its own.
    m_running = true;
    m_readerThread = std::thread([this]() {
        while (m_running.load()) {
            try {
                m_messageHandler->processIncomingMessages();
            } catch (const lsp::ConnectionError &) {
                break; // server went away
            } catch (const std::exception &e) {
                std::cerr << "LspClientImpl: " << e.what() << std::endl;
                break;
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
    initializeParams.rootUri = lsp::FileUri::fromPath(m_documentRoot);
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

void LspClientImpl::syncDocument(const std::string &fileName, const std::string &text) {
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
        auto params = lsp::notifications::TextDocument_DidOpen::Params{};
        params.textDocument.uri = lsp::FileUri::fromPath(fileName);
        params.textDocument.languageId = languageIdForFile(fileName);
        params.textDocument.version = version;
        params.textDocument.text = text;
        m_messageHandler->sendNotification<lsp::notifications::TextDocument_DidOpen>(
            std::move(params));
        trace("--> didOpen " + fileName + " v" + std::to_string(version));
        return;
    }

    // Full-document sync. Incremental sync would need range tracking the editor
    // does not expose yet.
    auto params = lsp::notifications::TextDocument_DidChange::Params{};
    params.textDocument.uri = lsp::FileUri::fromPath(fileName);
    params.textDocument.version = version;
    params.contentChanges = {lsp::TextDocumentContentChangeEvent_Text{.text = text}};
    m_messageHandler->sendNotification<lsp::notifications::TextDocument_DidChange>(
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
    auto params = lsp::notifications::TextDocument_DidClose::Params{};
    params.textDocument.uri = lsp::FileUri::fromPath(fileName);
    m_messageHandler->sendNotification<lsp::notifications::TextDocument_DidClose>(
        std::move(params));
    trace("--> didClose " + fileName);
}

void LspClientImpl::requestCompletion(const std::string &fileName, int line, int column,
                                      CompletionCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocument_Completion::Params{};
    params.textDocument.uri = lsp::FileUri::fromPath(fileName);
    params.position = {.line = static_cast<lsp::uint>(line),
                       .character = static_cast<lsp::uint>(column)};

    trace("--> completion " + fileName + ":" + std::to_string(line) + ":" + std::to_string(column));
    m_messageHandler->sendRequest<lsp::requests::TextDocument_Completion>(
        std::move(params),
        [this, callback](lsp::requests::TextDocument_Completion::Result &&result) {
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

void LspClientImpl::requestHover(const std::string &fileName, int line, int column,
                                 HoverCallback callback) {
    if (!m_ready.load()) {
        callback({});
        return;
    }

    auto params = lsp::requests::TextDocument_Hover::Params{};
    params.textDocument.uri = lsp::FileUri::fromPath(fileName);
    params.position = {.line = static_cast<lsp::uint>(line),
                       .character = static_cast<lsp::uint>(column)};

    trace("--> hover " + fileName + ":" + std::to_string(line) + ":" + std::to_string(column));
    m_messageHandler->sendRequest<lsp::requests::TextDocument_Hover>(
        std::move(params),
        [this, callback](lsp::requests::TextDocument_Hover::Result &&result) {
            auto text = result.isNull() ? std::string{} : hoverToPlainText(*result);
            trace("<-- hover result (" + std::to_string(text.size()) + " chars)");
            callback(text);
        },
        [callback](const lsp::ResponseError &error) {
            std::cerr << "LspClientImpl: hover failed: " << error.what() << std::endl;
            callback({});
        });
}
