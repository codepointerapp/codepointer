#include <atomic>
#include <memory>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPromise>

#include <QDockWidget>

#include "GlobalCommands.hpp"
#include "LspClientImpl.hpp"
#include "LspDebugWidget.hpp"
#include "LspPlugin.hpp"
#include "pluginmanager.h"
#include "widgets/qmdieditor.h"

namespace {

auto const SupportedSuffixes = QStringList{
    "cpp", "hpp", "c", "h", "cc", "hh", "cxx", "hxx",
};

/// Guards against a promise being completed twice - the server callback and any
/// future timeout path must be able to race safely. A QFuture that never finishes
/// leaks the caller's QFutureWatcher, so exactly-once matters in both directions.
struct PendingRequest {
    QPromise<CommandArgs> promise;
    std::atomic_flag done = ATOMIC_FLAG_INIT;

    void complete(const CommandArgs &result) {
        if (done.test_and_set()) {
            return;
        }
        promise.addResult(result);
        promise.finish();
    }
};

/// What actually gets typed into the buffer. clangd's `label` for a function is
/// the full signature ("foo(int a)"), so inserting it verbatim is wrong; the spec
/// order is textEdit, then insertText, then label.
auto insertTextFor(const lsp::CompletionItem &item) -> QString {
    if (item.textEdit.has_value()) {
        auto const &edit = *item.textEdit;
        if (std::holds_alternative<lsp::TextEdit>(edit)) {
            return QString::fromStdString(std::get<lsp::TextEdit>(edit).newText);
        }
        return QString::fromStdString(std::get<lsp::InsertReplaceEdit>(edit).newText);
    }
    if (item.insertText.has_value()) {
        return QString::fromStdString(*item.insertText);
    }
    return QString::fromStdString(item.label);
}

auto isSupportedFile(const QString &fileName) -> bool {
    return SupportedSuffixes.contains(QFileInfo(fileName).suffix(), Qt::CaseInsensitive);
}

} // namespace

LspPlugin::LspPlugin() {
    name = tr("LSP Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    config.pluginName = tr("LSP");
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Enable LSP"))
                                     .setDescription(tr("Use a language server for this project"))
                                     .setKey(Config::EnableLspKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("clangd binary"))
                                     .setDescription(tr("Language server used for C/C++"))
                                     .setKey(Config::ClangdBinaryKey)
                                     .setType(qmdiConfigItem::Path)
                                     .setDefaultValue("clangd")
                                     .build());
}

LspPlugin::~LspPlugin() { cleanup(); }

void LspPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);
    if (debugDock) {
        return;
    }
    auto manager = dynamic_cast<PluginManager *>(host);
    if (!manager) {
        return;
    }
    debugWidget = new LspDebugWidget(this);
    // Queued by construction when the trace originates on a reader thread.
    connect(this, &LspPlugin::traceMessage, debugWidget, &LspDebugWidget::appendTrace);
    debugDock = manager->createNewPanel(Panels::East, "lspdebug", tr("LSP"), debugWidget);
    connect(manager, &PluginManager::newClientAdded, this,
            [this](qmdiClient *) { updateEditorCompletionMode(); });
    connect(this, &LspPlugin::serverReady, this, &LspPlugin::updateEditorCompletionMode);
    qDebug() << "LspPlugin: debug panel created";
}

void LspPlugin::on_client_unmerged(qmdiHost *host) {
    cleanup();
    delete debugDock;
    debugDock = nullptr;
    debugWidget = nullptr;
    IPlugin::on_client_unmerged(host);
}

QList<LspServerInfo> LspPlugin::serverInfos() const {
    auto locker = QMutexLocker(&serversMutex);
    auto out = QList<LspServerInfo>();
    out.reserve(servers.size());
    for (auto it = servers.cbegin(); it != servers.cend(); ++it) {
        auto const &client = it.value();
        out.append(LspServerInfo{
            .root = it.key(),
            .binary = clangdBinary,
            .serverName = QString::fromStdString(client->serverName()),
            .ready = client->isReady(),
            .running = client->isRunning(),
            .documentCount = static_cast<int>(client->documents().size()),
        });
    }
    std::sort(out.begin(), out.end(), [](auto const &a, auto const &b) { return a.root < b.root; });
    return out;
}

QList<QPair<QString, int>> LspPlugin::documentsFor(const QString &root) const {
    auto out = QList<QPair<QString, int>>();
    auto locker = QMutexLocker(&serversMutex);
    auto it = servers.constFind(root);
    if (it == servers.cend()) {
        return out;
    }
    for (auto const &[file, version] : it.value()->documents()) {
        out.append({QString::fromStdString(file), version});
    }
    locker.unlock();
    std::sort(out.begin(), out.end(),
              [](auto const &a, auto const &b) { return a.first < b.first; });
    return out;
}

void LspPlugin::updateEditorCompletionMode() {
    auto manager = getManager();
    if (!manager) {
        return;
    }
    // Exclusive only where a ready server actually owns the document; every other
    // editor keeps qutepart's keyword and buffer-word completions.
    for (auto i = 0; i < static_cast<int>(manager->visibleTabs()); ++i) {
        auto editor = dynamic_cast<qmdiEditor *>(manager->getMdiClient(i));
        if (!editor) {
            continue;
        }
        editor->setCompletionExclusive(serverForFile(editor->mdiClientFileName()) != nullptr);
    }
}

bool LspPlugin::syncDocument(const QString &fileName, const QString &text) {
    auto server = serverForFile(fileName);
    if (!server) {
        return false;
    }
    server->syncDocument(QFileInfo(fileName).absoluteFilePath().toStdString(), text.toStdString());
    return true;
}

void LspPlugin::cleanup() {
    auto locker = QMutexLocker(&serversMutex);
    // Each destructor sends shutdown/exit and joins its reader thread.
    servers.clear();
}

LspClientImpl *LspPlugin::serverForFile(const QString &fileName) const {
    if (fileName.isEmpty()) {
        return nullptr;
    }
    auto path = QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());

    // Longest matching root wins, so a nested project beats its parent.
    auto locker = QMutexLocker(&serversMutex);
    LspClientImpl *best = nullptr;
    auto bestLength = 0;
    for (auto it = servers.cbegin(); it != servers.cend(); ++it) {
        auto const &root = it.key();
        if (path.startsWith(root + "/") && root.length() > bestLength) {
            best = it.value().get();
            bestLength = root.length();
        }
    }
    return (best && best->isReady()) ? best : nullptr;
}

void LspPlugin::startServerForProject(const QString &sourceDir, const QString &buildDir) {
    if (sourceDir.isEmpty() || !getConfig().getEnableLsp()) {
        return;
    }
    auto root = QDir::cleanPath(QDir(sourceDir).absolutePath());
    {
        auto locker = QMutexLocker(&serversMutex);
        if (servers.contains(root)) {
            return;
        }
    }

    auto binary = getConfig().getClangdBinary();
    clangdBinary = binary;
    // Without this clangd only looks next to the source file and in its ancestors,
    // never finds compile_commands.json in the build dir, and silently degrades to
    // guessed flags - which yields lexical rather than semantic completions.
    auto arguments = std::vector<std::string>{};
    if (!buildDir.isEmpty()) {
        auto expanded = QDir::cleanPath(QDir(buildDir).absolutePath());
        if (QFileInfo::exists(expanded + "/compile_commands.json")) {
            arguments.push_back("--compile-commands-dir=" + expanded.toStdString());
        } else {
            qWarning() << "LspPlugin: no compile_commands.json in" << expanded
                       << "- completions will be poor";
        }
    }

    try {
        auto client =
            std::make_shared<LspClientImpl>(binary.toStdString(), arguments, root.toStdString());
        client->setTraceCallback([this, root](const std::string &message) {
            emit traceMessage(QStringLiteral("[%1] %2").arg(QFileInfo(root).fileName(),
                                                            QString::fromStdString(message)));
            // This trace is emitted right after m_ready is set, so it is the
            // earliest point at which editors can be switched over. The signal is
            // queued: we are on the reader thread here.
            if (message.starts_with("<-- initialize result")) {
                emit serverReady();
            }
        });
        client->setDiagnosticsCallback(
            [](const std::string &file, const std::vector<lsp::Diagnostic> &diagnostics) {
                // TODO: apply to the editor via qmdiEditor::setLineError()/setLineWarning().
                // That has to be marshalled onto the GUI thread first.
                qDebug() << "LspPlugin: " << diagnostics.size() << "diagnostics for"
                         << QString::fromStdString(file);
                (void)diagnostics;
            });
        auto locker = QMutexLocker(&serversMutex);
        servers.insert(root, std::move(client));
        auto argLog = QStringList();
        for (auto const &a : arguments) {
            argLog << QString::fromStdString(a);
        }
        qDebug() << "LspPlugin: started" << binary << argLog << "for" << root
                 << "buildDir=" << buildDir;
    } catch (const std::exception &e) {
        qWarning() << "LspPlugin: could not start" << binary << "-" << e.what();
    }
}

int LspPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &args) const {
    // No EnableLsp check here: startServerForProject() honours it, so when LSP is
    // off there is simply no server and serverForFile() returns nullptr.
    if (command == GlobalCommands::ProjectLoaded || command == GlobalCommands::ProjectRemoved) {
        return CommandPriority::HighPriority;
    }

    if (command == GlobalCommands::VariableInfo || command == GlobalCommands::KeywordTooltip) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (!isSupportedFile(fileName)) {
            return CommandPriority::CannotHandle;
        }
        // Only outrank CTags/TreeSitter once a server is actually up: while clangd
        // is still starting, a lower-priority provider is better than no answer.
        return serverForFile(fileName) ? CommandPriority::HighestPriority
                                       : CommandPriority::CannotHandle;
    }

    return CommandPriority::CannotHandle;
}

QFuture<CommandArgs> LspPlugin::handleCommandAsync(const QString &command,
                                                   const CommandArgs &args) {
    if (command == GlobalCommands::ProjectLoaded) {
        startServerForProject(args[GlobalArguments::SourceDirectory].toString(),
                              args[GlobalArguments::BuildDirectory].toString());
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    if (command == GlobalCommands::ProjectRemoved) {
        auto root =
            QDir::cleanPath(QDir(args[GlobalArguments::SourceDirectory].toString()).absolutePath());
        auto locker = QMutexLocker(&serversMutex);
        servers.remove(root);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    auto fileName = args[GlobalArguments::FileName].toString();
    auto server = serverForFile(fileName);
    if (!server) {
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    auto line = args[GlobalArguments::LineNumber].toInt();
    auto column = args[GlobalArguments::ColumnNumber].toInt();
    auto content = args[GlobalArguments::Content].toString();
    auto path = QFileInfo(fileName).absoluteFilePath().toStdString();

    // The server must see the current buffer before it answers a positional
    // question, otherwise the reply refers to a stale document.
    server->syncDocument(path, content.toStdString());

    auto pending = std::make_shared<PendingRequest>();
    auto future = pending->promise.future();
    pending->promise.start();

    if (command == GlobalCommands::VariableInfo) {
        server->requestCompletion(
            path, line, column, [pending, fileName](std::vector<lsp::CompletionItem> items) {
                auto tags = QVariantList();
                tags.reserve(static_cast<int>(items.size()));
                for (auto const &item : items) {
                    auto detail =
                        item.detail.has_value() ? QString::fromStdString(*item.detail) : QString();
                    tags.append(QVariant::fromValue(CommandArgs{
                        {GlobalArguments::Name, insertTextFor(item)},
                        {GlobalArguments::Value, QString::fromStdString(item.label)},
                        {GlobalArguments::Type, detail},
                        {GlobalArguments::Source, QStringLiteral("LSP")},
                    }));
                }
                pending->complete(CommandArgs{{GlobalArguments::Tags, tags}});
            });
    } else {
        server->requestHover(path, line, column, [pending](std::string text) {
            auto result = CommandArgs{};
            if (!text.empty()) {
                result[GlobalArguments::Tooltip] = QString::fromStdString(text);
            }
            pending->complete(result);
        });
    }

    return future;
}
