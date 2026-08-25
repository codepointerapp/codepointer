#include <atomic>
#include <memory>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPromise>

#include <QAction>
#include <QDockWidget>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include "GlobalCommands.hpp"
#include "LspClientImpl.hpp"
#include "LspDebugWidget.hpp"
#include "LspPlugin.hpp"
#include "pluginmanager.h"
#include "widgets/qmdieditor.h"

namespace {

// FIXME: this should be a JSON file installed on the app's share dir, and users
// should be able to append to the list, in a user defined json. Just like indenters.
// See src/plugins/CodeFormat/CodeFormat.cpp.

/// Shipped defaults. Everything here is overridable through the ServersJson
/// setting; the shape is documented next to that config item.
auto builtinDefinitions() -> QList<LspServerDefinition> {
    auto clangd = LspServerDefinition{
        .name = "clangd",
        .homepage = "https://clangd.llvm.org/",
        // ${build_directory} is substituted per project, exactly as task strings
        // in ProjectBuildConfig are. Other servers need no arguments at all,
        // which is the point: nothing here is special-cased for clangd.
        .arguments = {"--compile-commands-dir=${build_directory}", "--log=error"},
        .binary = "clangd",
        .suffixes = {{"m", "objective-c"}, {"mm", "objective-cpp"}},
    };
    for (auto const &suffix : {"cc", "cpp", "cxx", "hh", "hpp", "hxx", "inl", "ipp"}) {
        clangd.suffixes.insert(suffix, "cpp");
    }
    // clangd serves C too, but the document has to be announced as "c".
    for (auto const &suffix : {"c", "h"}) {
        clangd.suffixes.insert(suffix, "c");
    }

    return {
        clangd,
        LspServerDefinition{
            .name = "gopls",
            .homepage = "https://go.dev/gopls/",
            .arguments = {},
            .binary = "gopls",
            .suffixes = {{"go", "go"}},
        },
        LspServerDefinition{
            .name = "pylsp",
            .homepage = "https://github.com/python-lsp/python-lsp-server",
            .arguments = {},
            .binary = "pylsp",
            .suffixes = {{"py", "python"}, {"pyi", "python"}},
        },
        LspServerDefinition{
            .name = "rust-analyzer",
            .homepage = "https://rust-analyzer.github.io/",
            .arguments = {},
            .binary = "rust-analyzer",
            .suffixes = {{"rs", "rust"}},
        },
        LspServerDefinition{
            .name = "sourcekit-lsp",
            .homepage = "https://www.swift.org/sourcekit-lsp/",
            .arguments = {},
            .binary = "sourcekit-lsp",
            .suffixes = {{"swift", "swift"}},
        },
        LspServerDefinition{
            .name = "typescript-language-server",
            .homepage = "https://github.com/typescript-language-server/typescript-language-server",
            .arguments = {"--stdio"},
            .binary = "typescript-language-server",
            .suffixes = {{"js", "javascript"},
                         {"jsx", "javascript"},
                         {"mjs", "javascript"},
                         {"cjs", "javascript"},
                         {"ts", "typescript"},
                         {"tsx", "typescript"}},
        },
        LspServerDefinition{
            .name = "bash-language-server",
            .homepage = "https://github.com/bash-lsp/bash-language-server",
            .arguments = {"start"},
            .binary = "bash-language-server",
            .suffixes = {{"sh", "shell"}, {"bash", "shell"}},
        },
        LspServerDefinition{
            .name = "yaml-language-server",
            .homepage = "https://github.com/redhat-developer/yaml-language-server",
            .arguments = {"--stdio"},
            .binary = "yaml-language-server",
            .suffixes = {{"yaml", "yaml"}, {"yml", "yaml"}},
        },
        LspServerDefinition{
            .name = "json-language-server",
            .homepage = "https://github.com/Microsoft/vscode-json-languageservice",
            .arguments = {"--stdio"},
            .binary = "vscode-json-language-server",
            .suffixes = {{"json", "json"}, {"jsonc", "json"}},
        },
    };
}

/// An array of server objects, matching `src/plugins/LSP/servers.json`.
QString definitionsToJson(const QList<LspServerDefinition> &definitions) {
    auto root = QJsonArray();
    for (auto const &definition : definitions) {
        auto suffixes = QJsonObject();
        for (auto it = definition.suffixes.cbegin(); it != definition.suffixes.cend(); ++it) {
            suffixes.insert(it.key(), it.value());
        }
        root.append(QJsonObject{
            {"name", definition.name},
            {"homepage", definition.homepage},
            {"arguments", QJsonArray::fromStringList(definition.arguments)},
            {"binary", definition.binary},
            {"suffixes", suffixes},
        });
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

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

/// The paths to try for a configured executable. On Windows a server may be
/// configured either as "clangd" or "clangd.exe", and an absolute path is not
/// extension-completed by QStandardPaths, so expand it ourselves.
auto executableCandidates(const QString &path) -> QStringList {
    auto candidates = QStringList{path};
#if defined(Q_OS_WIN)
    if (QFileInfo(path).suffix().isEmpty()) {
        auto pathExt = QString::fromLocal8Bit(qgetenv("PATHEXT"));
        auto const extensions = pathExt.isEmpty()
                                    ? QStringList{".com", ".exe", ".bat", ".cmd"}
                                    : pathExt.split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (auto const &extension : extensions) {
            candidates << path + extension.toLower();
        }
    }
#endif
    return candidates;
}

/// Substitutes the same ${...} placeholders ProjectBuildConfig uses in tasks, so
/// server arguments are configured the way build commands already are.
QStringList expandArguments(const QStringList &arguments, const QString &sourceDir,
                            const QString &buildDir) {
    auto out = QStringList();
    out.reserve(arguments.size());
    for (auto const &argument : arguments) {
        out << QString(argument)
                   .replace("${source_directory}", sourceDir)
                   .replace("${build_directory}", buildDir);
    }
    return out;
}

} // namespace

LspPlugin::LspPlugin() {
    name = tr("LSP Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    refactorAction = new QAction(tr("Refactor..."), this);
    refactorAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    refactorAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(refactorAction, &QAction::triggered, this, &LspPlugin::refactorAtCursor);
    menus[tr("&Edit")]->addAction(refactorAction);

    documentSyncTimer.setSingleShot(true);
    documentSyncTimer.setInterval(DocumentSyncDebounceMs);
    connect(&documentSyncTimer, &QTimer::timeout, this, &LspPlugin::flushDirtyDocuments);

    config.pluginName = tr("LSP");
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Language servers"))
            .setDescription(tr("An array of servers: name, arguments, binary and the file "
                               "suffixes each serves. \"suffixes\" maps a file extension to the "
                               "LSP languageId. ${source_directory} and ${build_directory} are "
                               "expanded per project. An entry replaces the built-in of the same "
                               "name."))
            .setKey(Config::ServersJsonKey)
            .setType(qmdiConfigItem::Json)
            .setDefaultValue(definitionsToJson(builtinDefinitions()))
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("More paths for language servers"))
            .setDescription(tr("If a language server is not on the standard PATH, add it here"))
            .setKey(Config::ExtraPathsKey)
            .setType(qmdiConfigItem::PathList)
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
    connect(manager, &PluginManager::newClientAdded, this, [this](qmdiClient *) {
        // Deferred: the editor's content is loaded after the client is added, so
        // syncing right now would hand the server an empty buffer.
        QTimer::singleShot(0, this, [this]() {
            updateEditorCompletionMode();
            syncOpenDocuments();
        });
    });
    connect(this, &LspPlugin::serverReady, this, &LspPlugin::updateEditorCompletionMode);
    connect(this, &LspPlugin::serverReady, this, &LspPlugin::syncOpenDocuments);
    connect(this, &LspPlugin::progressChanged, debugWidget, &LspDebugWidget::showProgress);
    connect(this, &LspPlugin::diagnosticsReady, this, &LspPlugin::applyDiagnostics);
    connect(manager, &PluginManager::newClientAdded, this, [this](qmdiClient *client) {
        if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
            applyDiagnostics(QFileInfo(editor->mdiClientFileName()).absoluteFilePath());
        }
    });
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
    for (auto it = servers.cbegin(); it != servers.cend(); ++it) {
        for (auto sit = it.value().cbegin(); sit != it.value().cend(); ++sit) {
            auto const &client = sit.value();
            out.append(LspServerInfo{
                .root = it.key(),
                .id = sit.key(),
                .binary = QString::fromStdString(client->documentRoot()),
                .serverName = QString::fromStdString(client->serverName()),
                .ready = client->isReady(),
                .running = client->isRunning(),
                .documentCount = static_cast<int>(client->documents().size()),
            });
        }
    }
    std::sort(out.begin(), out.end(), [](auto const &a, auto const &b) {
        return a.root != b.root ? a.root < b.root : a.id < b.id;
    });
    return out;
}

QList<QPair<QString, int>> LspPlugin::documentsFor(const QString &root) const {
    auto out = QList<QPair<QString, int>>();
    auto locker = QMutexLocker(&serversMutex);
    auto it = servers.constFind(root);
    if (it == servers.cend()) {
        return out;
    }
    for (auto sit = it.value().cbegin(); sit != it.value().cend(); ++sit) {
        for (auto const &[file, version] : sit.value()->documents()) {
            out.append(QPair<QString, int>{QString::fromStdString(file), version});
        }
    }
    locker.unlock();
    std::sort(out.begin(), out.end(),
              [](auto const &a, auto const &b) { return a.first < b.first; });
    return out;
}

int LspPlugin::applyTextEdits(const QList<LspTextEdit> &edits) {
    auto manager = getManager();
    if (!manager || edits.isEmpty()) {
        return 0;
    }

    auto byFile = QHash<QString, QList<LspTextEdit>>();
    for (auto const &edit : edits) {
        byFile[edit.file].append(edit);
    }

    auto changed = 0;
    for (auto it = byFile.begin(); it != byFile.end(); ++it) {
        // Edits may land in files that are not open; open them so the change is
        // visible and undoable rather than rewriting them behind the user's back.
        manager->openFile(QDir::toNativeSeparators(it.key()));
        auto editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(it.key()));
        if (!editor) {
            qWarning() << "LspPlugin: cannot apply edits, could not open" << it.key();
            continue;
        }

        // Every range is stated against the original document, so applying top-down
        // would invalidate the ranges below. Sort descending and work backwards.
        auto &fileEdits = it.value();
        std::sort(fileEdits.begin(), fileEdits.end(), [](auto const &a, auto const &b) {
            return a.startLine != b.startLine ? a.startLine > b.startLine
                                              : a.startCharacter > b.startCharacter;
        });

        auto documentEdits = QList<qmdiEditor::TextEdit>();
        for (auto const &edit : fileEdits) {
            documentEdits.append(qmdiEditor::TextEdit{edit.startLine, edit.startCharacter,
                                                      edit.endLine, edit.endCharacter,
                                                      edit.newText});
        }
        if (!editor->applyTextEdits(documentEdits)) {
            continue;
        }
        changed++;
    }

    // The servers must be told, or the next request answers against stale text.
    syncOpenDocuments();
    return changed;
}

void LspPlugin::startRename(const QString &path, int line, int character) {
    auto server = serverForFile(path);
    if (!server) {
        return;
    }
    auto ok = false;
    auto newName = QInputDialog::getText(nullptr, tr("Rename symbol"), tr("New name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok || newName.isEmpty()) {
        return;
    }
    server->requestRename(
        path.toStdString(), line, character, newName.toStdString(),
        [this, newName](std::vector<LspClientImpl::TextEdit> edits) {
            auto converted = QList<LspTextEdit>();
            for (auto const &e : edits) {
                converted.append(LspTextEdit{QString::fromStdString(e.file), e.startLine,
                                             e.startCharacter, e.endLine, e.endCharacter,
                                             QString::fromStdString(e.newText)});
            }
            // Editors are GUI-only and this arrives on a reader thread.
            QMetaObject::invokeMethod(
                this,
                [this, converted, newName]() {
                    auto files = applyTextEdits(converted);
                    qDebug() << "LspPlugin: renamed to" << newName << "across" << files
                             << "file(s)," << converted.size() << "edits";
                },
                Qt::QueuedConnection);
        });
}

void LspPlugin::refactorAtCursor() {
    auto manager = getManager();
    if (!manager) {
        return;
    }
    auto editor = dynamic_cast<qmdiEditor *>(manager->currentClient());
    if (!editor) {
        return;
    }
    auto fileName = editor->mdiClientFileName();
    auto server = serverForFile(fileName);
    if (!server) {
        qDebug() << "LspPlugin: no ready server for" << fileName;
        return;
    }

    auto path = QFileInfo(fileName).absoluteFilePath();
    syncDocument(fileName, editor->getContent());

    auto selection = editor->selectionRange();
    auto canRename = server->hasCapability("renameProvider");
    auto canAct = server->hasCapability("codeActionProvider");
    if (!canRename && !canAct) {
        qDebug() << "LspPlugin: server advertises neither rename nor code actions";
        return;
    }

    auto showMenu = [this, path, selection,
                     canRename](std::vector<LspClientImpl::CodeAction> actions) {
        auto menu = QMenu(tr("Refactor"));
        auto renameEntry = canRename ? menu.addAction(tr("Rename symbol...")) : nullptr;
        if (renameEntry && !actions.empty()) {
            menu.addSeparator();
        }
        auto entries = QHash<QAction *, int>();
        for (auto i = 0u; i < actions.size(); ++i) {
            auto entry = menu.addAction(QString::fromStdString(actions[i].title));
            entry->setEnabled(!actions[i].needsCommand);
            if (actions[i].needsCommand) {
                // The server wants workspace/executeCommand and would push the edit
                // back via workspace/applyEdit, which is not handled yet.
                entry->setToolTip(tr("This action needs workspace/executeCommand"));
            }
            entries.insert(entry, static_cast<int>(i));
        }
        if (menu.isEmpty()) {
            return;
        }

        auto chosen = menu.exec(QCursor::pos());
        if (!chosen) {
            return;
        }
        if (chosen == renameEntry) {
            startRename(path, selection.startLine, selection.startCharacter);
            return;
        }
        auto const &action = actions[entries.value(chosen)];
        auto edits = QList<LspTextEdit>();
        for (auto const &e : action.edits) {
            edits.append(LspTextEdit{QString::fromStdString(e.file), e.startLine, e.startCharacter,
                                     e.endLine, e.endCharacter, QString::fromStdString(e.newText)});
        }
        auto files = applyTextEdits(edits);
        qDebug() << "LspPlugin: applied" << QString::fromStdString(action.title) << "to" << files
                 << "file(s)";
    };

    if (!canAct) {
        showMenu({});
        return;
    }
    server->requestCodeActions(path.toStdString(), selection.startLine, selection.startCharacter,
                               selection.endLine, selection.endCharacter, {"refactor", "quickfix"},
                               [this, showMenu](std::vector<LspClientImpl::CodeAction> actions) {
                                   // Hop to the GUI thread: menus and editors are GUI-only.
                                   QMetaObject::invokeMethod(
                                       this, [showMenu, actions]() { showMenu(actions); },
                                       Qt::QueuedConnection);
                               });
}

void LspPlugin::applyDiagnostics(const QString &fileName) {
    auto manager = getManager();
    if (!manager) {
        return;
    }
    auto editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(fileName));
    if (!editor) {
        return;
    }

    auto items = QList<Diagnostic>();
    {
        auto locker = QMutexLocker(&diagnosticsMutex);
        items = diagnostics.value(fileName);
    }

    for (auto line : markedLines.value(fileName)) {
        editor->setLineError(line, false);
        editor->setLineWarning(line, false);
        editor->setLineInfo(line, false);
        editor->setMetaDataMessage(line, {});
    }

    auto marked = QList<int>();
    for (auto const &item : items) {
        switch (item.severity) {
        case 1:
            editor->setLineError(item.line, true);
            break;
        case 2:
            editor->setLineWarning(item.line, true);
            break;
        default:
            editor->setLineInfo(item.line, true);
            break;
        }
        editor->setMetaDataMessage(item.line, item.message);
        marked.append(item.line);
    }
    markedLines[fileName] = marked;
    editor->update();
    qDebug() << "LspPlugin:" << items.size() << "diagnostics applied to"
             << QFileInfo(fileName).fileName();
}

void LspPlugin::syncOpenDocuments() {
    auto manager = getManager();
    if (!manager) {
        return;
    }
    auto synced = 0;
    for (auto i = 0; i < static_cast<int>(manager->visibleTabs()); ++i) {
        auto editor = dynamic_cast<qmdiEditor *>(manager->getMdiClient(i));
        if (!editor) {
            continue;
        }
        auto fileName = editor->mdiClientFileName();
        ensureServerForFile(fileName);

        auto content = editor->getContent();
        if (content.isEmpty()) {
            continue;
        }
        if (!syncDocument(fileName, content)) {
            continue;
        }
        synced++;

        // ClosedFile is claimed by ProjectManager and the bus only dispatches to one
        // plugin, so we cannot learn about closes that way. destroyed() is the
        // dependable alternative; the path is captured by value because the editor
        // is already being torn down when it fires.
        if (!watchedEditors.contains(editor)) {
            watchedEditors.insert(editor);
            connect(editor, &qmdiEditor::contentChanged, this, [this, editor]() {
                dirtyDocuments.insert(editor->mdiClientFileName());
                documentSyncTimer.start();
            });
            connect(editor, &QObject::destroyed, this, [this](QObject *gone) {
                watchedEditors.remove(gone);
                QTimer::singleShot(0, this, [this]() { reconcileOpenDocuments(); });
            });
        }
    }
    if (synced > 0) {
        qDebug() << "LspPlugin: announced" << synced << "document(s) to their servers";
    }
}

void LspPlugin::flushDirtyDocuments() {
    auto manager = getManager();
    if (!manager || dirtyDocuments.isEmpty()) {
        return;
    }
    auto pending = dirtyDocuments;
    dirtyDocuments.clear();

    auto sent = 0;
    for (auto const &fileName : pending) {
        auto editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(fileName));
        if (!editor) {
            continue; // closed while we were waiting
        }
        if (syncDocument(fileName, editor->getContent())) {
            sent++;
        }
    }
    if (sent > 0) {
        qDebug() << "LspPlugin: pushed" << sent << "changed document(s) after"
                 << DocumentSyncDebounceMs << "ms idle";
    }
}

void LspPlugin::reconcileOpenDocuments() {
    auto manager = getManager();
    if (!manager) {
        return;
    }

    auto stillOpen = QSet<QString>();
    for (auto i = 0; i < static_cast<int>(manager->visibleTabs()); ++i) {
        if (auto editor = dynamic_cast<qmdiEditor *>(manager->getMdiClient(i))) {
            stillOpen.insert(QFileInfo(editor->mdiClientFileName()).absoluteFilePath());
        }
    }

    auto closed = 0;
    auto locker = QMutexLocker(&serversMutex);
    for (auto it = servers.cbegin(); it != servers.cend(); ++it) {
        for (auto sit = it.value().cbegin(); sit != it.value().cend(); ++sit) {
            for (auto const &[file, version] : sit.value()->documents()) {
                auto fileName = QString::fromStdString(file);
                if (stillOpen.contains(fileName)) {
                    continue;
                }
                sit.value()->closeDocument(file);
                closed++;
                auto diagLocker = QMutexLocker(&diagnosticsMutex);
                diagnostics.remove(fileName);
                markedLines.remove(fileName);
            }
        }
    }
    locker.unlock();
    if (closed > 0) {
        qDebug() << "LspPlugin: closed" << closed << "document(s) on their servers";
    }
}

void LspPlugin::updateEditorCompletionMode() {
    auto manager = getManager();
    if (!manager) {
        return;
    }

    auto exclusive = 0;
    auto editors = 0;
    for (auto i = 0; i < manager->visibleTabs(); ++i) {
        auto editor = dynamic_cast<qmdiEditor *>(manager->getMdiClient(i));
        if (!editor) {
            continue;
        }
        auto owned = serverForFile(editor->mdiClientFileName()) != nullptr;
        editor->setCompletionExclusive(owned);
        editors++;
        exclusive += owned ? 1 : 0;
    }
    qDebug() << "LspPlugin: LSP-only completions on" << exclusive << "of" << editors << "editors";
}

QList<QPair<QString, QString>> LspPlugin::capabilitiesFor(const QString &root,
                                                          const QString &serverId) const {
    auto out = QList<QPair<QString, QString>>();
    auto locker = QMutexLocker(&serversMutex);
    auto project = servers.constFind(root);
    if (project == servers.cend()) {
        return out;
    }
    auto client = project.value().constFind(serverId);
    if (client == project.value().cend()) {
        return out;
    }
    for (auto const &[name, value] : client.value()->capabilities()) {
        out.append({QString::fromStdString(name), QString::fromStdString(value)});
    }
    return out;
}

bool LspPlugin::syncDocument(const QString &fileName, const QString &text) {
    auto server = serverForFile(fileName);
    if (!server) {
        return false;
    }
    server->syncDocument(QFileInfo(fileName).absoluteFilePath().toStdString(), text.toStdString(),
                         languageForFile(fileName).toStdString());
    dirtyDocuments.remove(fileName);
    return true;
}

void LspPlugin::cleanup() {
    auto locker = QMutexLocker(&serversMutex);
    // Each destructor sends shutdown/exit and joins its reader thread.
    servers.clear();
}

LspClientImpl *LspPlugin::serverForFile(const QString &fileName) const {
    // Owning the directory is not enough - the server also has to speak the
    // language. Without this a README.md inside a C++ project would be treated as
    // LSP-managed, and would be marked completion-exclusive against a server that
    // will never answer for it.
    if (fileName.isEmpty()) {
        return nullptr;
    }
    auto suffix = QFileInfo(fileName).suffix().toLower();
    auto serverId = QString();
    for (auto const &definition : serverDefinitions()) {
        if (definition.suffixes.contains(suffix)) {
            serverId = definition.name;
            break;
        }
    }
    if (serverId.isEmpty()) {
        return nullptr;
    }

    auto path = QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());
    auto locker = QMutexLocker(&serversMutex);
    LspClientImpl *best = nullptr;
    auto bestLength = 0;
    for (auto it = servers.cbegin(); it != servers.cend(); ++it) {
        auto const &root = it.key();
        if (!path.startsWith(root + "/") || root.length() <= bestLength) {
            continue;
        }
        auto server = it.value().constFind(serverId);
        if (server != it.value().cend()) {
            best = server.value().get();
            bestLength = root.length();
        }
    }
    return (best && best->isReady()) ? best : nullptr;
}

namespace {

/// One entry of the servers array. Returns false and explains itself when the
/// entry cannot be used.
bool definitionFromJson(const QJsonObject &entry, LspServerDefinition &out) {
    out.name = entry.value("name").toString().trimmed();
    if (out.name.isEmpty()) {
        qWarning() << "LspPlugin: server entry has no \"name\"; ignored";
        return false;
    }
    out.homepage = entry.value("homepage").toString();
    // A definition that only renames the binary is the common case, so the
    // binary defaults to the server name rather than being required.
    out.binary = entry.value("binary").toString(out.name);
    for (auto const &argument : entry.value("arguments").toArray()) {
        out.arguments << argument.toString();
    }
    // Extension -> LSP languageId, because one server can serve several
    // languages and didOpen has to name the right one.
    auto const suffixes = entry.value("suffixes").toObject();
    for (auto it = suffixes.constBegin(); it != suffixes.constEnd(); ++it) {
        out.suffixes.insert(it.key().toLower(), it.value().toString());
    }
    if (out.suffixes.isEmpty()) {
        qWarning() << "LspPlugin: server" << out.name << "has no \"suffixes\"; ignored";
        return false;
    }
    return true;
}

} // namespace

QList<LspServerDefinition> LspPlugin::serverDefinitions() const {
    if (!cachedDefinitions.isEmpty()) {
        return cachedDefinitions;
    }
    cachedDefinitions = builtinDefinitions();

    auto raw = getConstConfig().getServersJson().toUtf8();
    if (raw.isEmpty()) {
        return cachedDefinitions;
    }
    auto parseError = QJsonParseError{};
    auto document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "LspPlugin: ServersJson is not valid JSON:" << parseError.errorString()
                   << "- using built-in definitions";
        return cachedDefinitions;
    }
    if (!document.isArray()) {
        qWarning() << "LspPlugin: ServersJson is not an array of servers"
                   << "- using built-in definitions";
        return cachedDefinitions;
    }

    // An entry replaces the built-in of the same name outright, so a user can
    // change just the binary without inheriting arguments they did not ask for.
    auto const array = document.array();
    for (auto const &value : array) {
        if (!value.isObject()) {
            qWarning() << "LspPlugin: server entry is not an object; ignored";
            continue;
        }
        auto definition = LspServerDefinition{};
        if (!definitionFromJson(value.toObject(), definition)) {
            continue;
        }
        auto replaced = false;
        for (auto &existing : cachedDefinitions) {
            if (existing.name == definition.name) {
                existing = definition;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            cachedDefinitions.append(definition);
        }
    }
    return cachedDefinitions;
}

QString LspPlugin::languageForFile(const QString &fileName) const {
    auto suffix = QFileInfo(fileName).suffix().toLower();
    for (auto const &definition : serverDefinitions()) {
        auto it = definition.suffixes.constFind(suffix);
        if (it != definition.suffixes.cend()) {
            return it.value();
        }
    }
    return {};
}

QString LspPlugin::resolveServerBinary(const QString &binary) const {
    if (binary.isEmpty()) {
        return {};
    }
    // An explicit path is taken as given - searching PATH would only mask a typo.
    // Only the file extension is filled in, for Windows.
    if (QFileInfo(binary).isAbsolute()) {
        for (auto const &candidate : executableCandidates(binary)) {
            auto info = QFileInfo(candidate);
            if (info.exists() && info.isFile() && info.isExecutable()) {
                return candidate;
            }
        }
        return {};
    }
    // Configured paths are searched before PATH, so a locally built server can be
    // preferred over whatever the distribution installed.
    auto paths = getConstConfig().getExtraPaths();
    paths.append(QString::fromLocal8Bit(qgetenv("PATH")).split(QDir::listSeparator()));
    return QStandardPaths::findExecutable(binary, paths);
}

void LspPlugin::ensureServerForFile(const QString &fileName) {
    auto suffix = QFileInfo(fileName).suffix().toLower();
    auto definition = LspServerDefinition{};
    for (auto const &candidate : serverDefinitions()) {
        if (candidate.suffixes.contains(suffix)) {
            definition = candidate;
            break;
        }
    }
    if (definition.name.isEmpty()) {
        return; // no server speaks this file type
    }

    // Longest matching project root wins, so a nested project beats its parent.
    auto path = QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());
    auto root = QString();
    auto buildDir = QString();
    for (auto it = projectRoots.cbegin(); it != projectRoots.cend(); ++it) {
        if (path.startsWith(it.key() + "/") && it.key().length() > root.length()) {
            root = it.key();
            buildDir = it.value();
        }
    }
    if (root.isEmpty()) {
        return; // file is not inside a loaded project
    }

    {
        auto locker = QMutexLocker(&serversMutex);
        if (servers.value(root).contains(definition.name)) {
            return;
        }
    }

    auto executable = resolveServerBinary(definition.binary);
    if (executable.isEmpty()) {
        qDebug() << "LspPlugin:" << definition.binary << "not found, no" << definition.name << "for"
                 << root
                 << (definition.homepage.isEmpty() ? QString() : "- see " + definition.homepage);
        return;
    }

    auto arguments = std::vector<std::string>{};
    for (auto const &argument : expandArguments(definition.arguments, root, buildDir)) {
        // A placeholder that resolved to nothing means the project has no such
        // directory - dropping the argument beats passing "--flag=".
        if (argument.contains("${") || argument.endsWith("=")) {
            qWarning() << "LspPlugin: dropping unresolved argument" << argument << "for"
                       << definition.name;
            continue;
        }
        arguments.push_back(argument.toStdString());
    }
    startOneServer(definition, executable, arguments, root);
}

void LspPlugin::startOneServer(const LspServerDefinition &definition, const QString &executable,
                               const std::vector<std::string> &arguments, const QString &root) {
    try {
        auto client = std::make_shared<LspClientImpl>(executable.toStdString(), arguments,
                                                      root.toStdString());
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
        client->setProgressCallback([this, root](const std::string &title,
                                                 const std::string &message, int percentage,
                                                 bool active) {
            auto text = QString::fromStdString(message.empty() ? title : message);
            emit progressChanged(root, text, percentage, active);
        });
        client->setDiagnosticsCallback(
            [this](const std::string &file, const std::vector<lsp::Diagnostic> &items) {
                auto fileName = QFileInfo(QString::fromStdString(file)).absoluteFilePath();
                auto converted = QList<Diagnostic>();
                converted.reserve(static_cast<int>(items.size()));
                for (auto const &item : items) {
                    converted.append(Diagnostic{
                        static_cast<int>(item.range.start.line),
                        item.severity.has_value() ? static_cast<int>(*item.severity) : 1,
                        QString::fromStdString(item.message),
                    });
                }
                {
                    auto locker = QMutexLocker(&diagnosticsMutex);
                    diagnostics[fileName] = converted;
                }
                // Queued: we are on a reader thread and the editor is GUI-only.
                emit diagnosticsReady(fileName);
            });
        auto locker = QMutexLocker(&serversMutex);
        servers[root].insert(definition.name, std::move(client));
        auto argLog = QStringList();
        for (auto const &a : arguments) {
            argLog << QString::fromStdString(a);
        }
        qDebug() << "LspPlugin: started" << definition.name << executable << argLog << "for"
                 << root;

        // A binary can exist, launch, and still be useless - a rustup proxy shim for
        // an uninstalled component exits immediately, for instance. Nothing else
        // notices: initialize is never answered, isReady() stays false forever, and
        // the language silently has no server. Check back and say so.
        auto watched = std::weak_ptr<LspClientImpl>(servers[root].value(definition.name));
        auto id = definition.name;
        auto binary = definition.binary;
        QTimer::singleShot(5000, this, [this, watched, id, binary, root]() {
            auto client = watched.lock();
            if (!client || client->isReady()) {
                return;
            }
            if (!client->isRunning()) {
                qWarning() << "LspPlugin:" << id << "exited without completing the handshake -"
                           << binary << "is present but not working (an uninstalled rustup"
                           << "component leaves a shim behind, for example)";
                emit traceMessage(
                    tr("%1 exited during startup - %2 is present but not usable").arg(id, binary));
            } else {
                qDebug() << "LspPlugin:" << id << "still starting after 5s";
            }
        });
    } catch (const std::exception &e) {
        qWarning() << "LspPlugin: could not start" << definition.binary << "-" << e.what();
    }
}

int LspPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &args) const {
    if (command == GlobalCommands::ProjectLoaded || command == GlobalCommands::ProjectRemoved) {
        return CommandPriority::HighPriority;
    }

    if (command == GlobalCommands::VariableInfo || command == GlobalCommands::KeywordTooltip) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
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
        auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
        if (!sourceDir.isEmpty()) {
            auto root = QDir::cleanPath(QDir(sourceDir).absolutePath());
            auto buildDir = args[GlobalArguments::BuildDirectory].toString();
            projectRoots.insert(root, buildDir.isEmpty()
                                          ? QString()
                                          : QDir::cleanPath(QDir(buildDir).absolutePath()));
            // Servers start when a file of their language is actually opened.
            syncOpenDocuments();
        }
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    if (command == GlobalCommands::ProjectRemoved) {
        auto root =
            QDir::cleanPath(QDir(args[GlobalArguments::SourceDirectory].toString()).absolutePath());
        projectRoots.remove(root);
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
    server->syncDocument(path, content.toStdString(), languageForFile(fileName).toStdString());

    auto pending = std::make_shared<PendingRequest>();
    auto future = pending->promise.future();
    pending->promise.start();

    // Two very different callers share VariableInfo: the completion popup sends
    // ExactMatch=false with a prefix, while "Follow symbol" sends ExactMatch=true
    // for the word under the cursor. The latter wants locations, not candidates.
    if (command == GlobalCommands::VariableInfo && args[GlobalArguments::ExactMatch].toBool()) {
        auto symbol = args[GlobalArguments::RequestedSymbol].toString();
        server->requestDefinition(
            path, line, column, [pending, symbol](std::vector<LspClientImpl::Location> locations) {
                auto tags = QVariantList();
                for (auto const &location : locations) {
                    auto file = QString::fromStdString(location.file);
                    tags.append(QVariant::fromValue(CommandArgs{
                        {GlobalArguments::FileName, file},
                        {GlobalArguments::LineNumber, location.line + 1},
                        {GlobalArguments::ColumnNumber, location.column + 1},
                        {GlobalArguments::Name, symbol},
                        {GlobalArguments::Value, symbol},
                        {GlobalArguments::Raw, symbol},
                        {GlobalArguments::Type, QStringLiteral("definition")},
                        {GlobalArguments::Source, QStringLiteral("LSP")},
                        {GlobalArguments::IsDefinition, true},
                    }));
                }
                pending->complete(CommandArgs{
                    {GlobalArguments::Symbol, symbol},
                    {GlobalArguments::Tags, tags},
                });
            });
    } else if (command == GlobalCommands::VariableInfo) {
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
