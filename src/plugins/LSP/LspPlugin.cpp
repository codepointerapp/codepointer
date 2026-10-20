/**
 * \file LspPlugin.cpp
 * \brief LSP plugin for the IDE
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QPromise>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include <lsp/json/json.h>
#include <lsp/messages.h>

#include "GlobalCommands.hpp"
#include "LspClientImpl.hpp"
#include "LspDebugWidget.hpp"
#include "LspPlugin.hpp"
#include "LspReferencesWidget.hpp"
#include "lsp_diagnostic.hpp"
#include "lsp_text_edit.hpp"
#include "pluginmanager.h"
#include "widgets/lsp_rename_geometry.h"
#include "widgets/qmdieditor.h"

#ifdef Q_OS_WIN
// clang-format off
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
// clang-format on
#endif

/// Guards against a promise being completed twice - the server callback and any
/// future timeout path must be able to race safely. A QFuture that never finishes
/// leaks the caller's QFutureWatcher, so exactly-once matters in both directions.
/// Declared at global scope because LspPlugin's header forward-declares it.
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

namespace {

/// \@brief function to load a file, and reload it on change
///
/// This function should be used to load a file. You pass to it the function to
/// be called to `fopen()` the file - and do your loading/parsing from there.
/// That same callback will be called when the file gets modified.
///
/// A save is not one event. qmdiEditor opens the target WriteOnly - truncating
/// it - and pumps the event loop before it writes anything, so a single Ctrl-S
/// raises fileChanged twice: once for the truncate, where the file reads back
/// empty, and once for the write. Reloading on the first parses nothing, and for
/// a definitions file that reads as "no servers configured" rather than as a
/// transient state. The reload is therefore debounced, so one save is one load
/// and the load happens once the new contents are on disk.
///
/// FIXME: move to a shared place
auto autoReloadFile(QObject *parent, const QString &path, std::function<bool(QString)> loadFile)
    -> QFileSystemWatcher * {
    auto const ReloadDebounceMs = 300;
    auto w = new QFileSystemWatcher(parent);
    auto debounce = new QTimer(parent);
    debounce->setSingleShot(true);
    debounce->setInterval(ReloadDebounceMs);

    auto scheduleReload = [debounce](const QString &) { debounce->start(); };
    auto reloadNow = [w, path, loadFile] {
        if (!w->files().contains(path)) {
            w->addPath(path);
        }
        if (!loadFile(path)) {
            qDebug() << "Reloading failed" << path;
        }
        if (!w->files().contains(path)) {
            w->addPath(path);
        }
    };

    debounce->connect(debounce, &QTimer::timeout, parent, reloadNow);
    w->addPath(path);
    w->connect(w, &QFileSystemWatcher::fileChanged, parent, scheduleReload);
    reloadNow();
    return w;
}

/// One entry of the servers array. Returns false and explains itself when the
/// entry cannot be used.
auto definitionFromJson(const QJsonObject &entry, LspServerDefinition &out) -> bool {
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

// Try to parse the servers definition form "bytearray" - the raw data read from
// QIODevice. Code must be valid utf8.
auto loadDefinitionsFromRawJson(QByteArray raw) -> QList<LspServerDefinition> {
    QList<LspServerDefinition> definitions;

    auto parseError = QJsonParseError{};
    auto document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "LspPlugin: ServersJson is not valid JSON:" << parseError.errorString()
                   << "- using built-in definitions";
        return definitions;
    }
    if (!document.isArray()) {
        qWarning() << "LspPlugin: invalid input, cannot loading lsp servers definitions";
        return definitions;
    }

    auto const array = document.array();
    for (auto const &value : array) {
        if (!value.isObject()) {
            qWarning() << "LspPlugin: invalid input, cannot loading lsp servers definitions";
            break;
        }
        auto definition = LspServerDefinition{};
        if (!definitionFromJson(value.toObject(), definition)) {
            qWarning() << "LspPlugin: invalid input, cannot loading lsp servers definitions";
            break;
        }
        definitions.append(definition);
    }
    return definitions;
}

auto loadDefinitionsFromFile(QString jsonFile) -> QList<LspServerDefinition> {
    auto f = QFile(jsonFile);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "LSP: Could not open" << jsonFile << f.errorString();
        return {};
    }
    auto json = f.readAll();
    return loadDefinitionsFromRawJson(json);
}

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
auto expandArguments(const QStringList &arguments, const QString &sourceDir,
                     const QString &buildDir) -> QStringList {
    auto out = QStringList();
    out.reserve(arguments.size());
    for (auto const &argument : arguments) {
        out << QString(argument)
                   .replace("${source_directory}", sourceDir)
                   .replace("${build_directory}", buildDir);
    }
    return out;
}

/// Renders LSP signature help for the call tip widget: the active signature with
/// its active parameter highlighted. Plain text is HTML-escaped; when the server
/// reports parameter label offsets (UTF-16) within the signature label, those get
/// <b> markers instead of a substring search. Returns an empty string when there
/// is nothing to show.
QString renderSignatureHelp(const lsp::SignatureHelp &help) {
    if (help.signatures.empty()) {
        return {};
    }

    auto active = help.activeSignature.has_value() ? static_cast<std::size_t>(*help.activeSignature)
                                                   : std::size_t{0};
    if (active >= help.signatures.size()) {
        active = 0;
    }
    auto const &signature = help.signatures[active];
    auto signatureLabel = QString::fromStdString(signature.label);

    // The server names the active parameter per signature in 3.16+, falling back
    // to the help-wide activeParameter. Both are null when nothing is active.
    auto activeParameter = -1;
    if (signature.activeParameter.has_value() && !signature.activeParameter->isNull()) {
        activeParameter = static_cast<int>(**signature.activeParameter);
    } else if (help.activeParameter.has_value() && !help.activeParameter->isNull()) {
        activeParameter = static_cast<int>(**help.activeParameter);
    }

    // Resolve the active parameter to a [start, end) span of signatureLabel in
    // UTF-16 code units, which is what QString indices are.
    auto spanStart = -1;
    auto spanEnd = -1;
    if (signature.parameters.has_value() && activeParameter >= 0 &&
        activeParameter < static_cast<int>(signature.parameters->size())) {
        auto const &label = (*signature.parameters)[activeParameter].label;
        if (std::holds_alternative<lsp::Tuple<lsp::Uint, lsp::Uint>>(label)) {
            auto const &span = std::get<lsp::Tuple<lsp::Uint, lsp::Uint>>(label);
            spanStart = static_cast<int>(std::get<0>(span));
            spanEnd = static_cast<int>(std::get<1>(span));
        } else {
            auto const &text = std::get<lsp::String>(label);
            spanStart = signatureLabel.indexOf(QString::fromStdString(text));
            spanEnd = spanStart < 0 ? -1 : spanStart + int(text.size());
        }
    }

    if (spanStart < 0 || spanStart >= spanEnd || spanEnd > signatureLabel.size()) {
        // No usable highlight - the whole signature is fine as-is.
        return signatureLabel.toHtmlEscaped();
    }

    return signatureLabel.left(spanStart).toHtmlEscaped() + QStringLiteral("<b>") +
           signatureLabel.mid(spanStart, spanEnd - spanStart).toHtmlEscaped() +
           QStringLiteral("</b>") + signatureLabel.mid(spanEnd).toHtmlEscaped();
}

} // namespace

LspPlugin::LspPlugin() {
    name = tr("LSP Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    documentSyncTimer.setSingleShot(true);
    documentSyncTimer.setInterval(DocumentSyncDebounceMs);
    connect(&documentSyncTimer, &QTimer::timeout, this, &LspPlugin::flushDirtyDocuments);

    // Default to the LLVM bin dir the Windows installer puts clangd in; empty
    // elsewhere, where servers live on the PATH.
    auto programFilesLLVM = QString();
#ifdef Q_OS_WIN
    PWSTR programFilesPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr,
                                       &programFilesPath))) {
        programFilesLLVM =
            QDir::toNativeSeparators(QString::fromWCharArray(programFilesPath) + "/LLVM/bin/");
        CoTaskMemFree(programFilesPath);
    }
#endif

    config.pluginName = tr("LSP");
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("More paths for language servers"))
            .setDescription(tr("If a language server is not on the standard PATH, add it here"))
            .setKey(Config::ExtraPathsKey)
            .setType(qmdiConfigItem::PathList)
            .setDefaultValue(programFilesLLVM)
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
    debugDock = manager->createNewPanel(Panels::East, "lspdebug", tr("LSP"), debugWidget);

    referencesWidget = new LspReferencesWidget;
    referencesDock =
        manager->createNewPanel(Panels::West, "lspreferences", tr("References"), referencesWidget);
    referencesWidget->setDock(referencesDock);
    // Only worth the screen space once a search has actually produced something.
    referencesDock->hide();
    connect(referencesWidget, &LspReferencesWidget::openLocation, this,
            [this](const QString &fileName, int lineNumber, int columnNumber) {
                auto mgr = getManager();
                if (!mgr) {
                    return;
                }
                auto const nativeFileName = QDir::toNativeSeparators(fileName);
                mgr->openFile(nativeFileName);
                auto const client = mgr->clientForFileName(nativeFileName);
                auto const editor = dynamic_cast<qmdiEditor *>(client);
                if (!editor) {
                    return;
                }
                editor->loadContent(true);
                editor->goTo(columnNumber > 0 ? columnNumber - 1 : 0,
                             lineNumber > 0 ? lineNumber - 1 : 0);
                editor->setFocus();
            });

    // Queued by construction when the trace originates on a reader thread.
    connect(this, &LspPlugin::traceMessage, debugWidget, &LspDebugWidget::appendTrace);
    connect(this, &LspPlugin::serverReady, this, &LspPlugin::updateEditorCompletionMode);
    connect(this, &LspPlugin::serverReady, this, &LspPlugin::syncOpenDocuments);
    connect(this, &LspPlugin::progressChanged, debugWidget, &LspDebugWidget::showProgress);
    connect(this, &LspPlugin::diagnosticsReady, this, &LspPlugin::applyDiagnostics);
    connect(manager, &PluginManager::newClientAdded, this, [this](qmdiClient *client) {
        if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
            applyDiagnostics(QDir::toNativeSeparators(
                QFileInfo(editor->mdiClientFileName()).absoluteFilePath()));
        }

        // Deferred: the editor's content is loaded after the client is added, so
        // syncing right now would hand the server an empty buffer.
        QTimer::singleShot(0, this, [this]() {
            updateEditorCompletionMode();
            syncOpenDocuments();
        });
    });

#ifdef Q_OS_WIN
    auto systemDataDir = QDir(QCoreApplication::applicationDirPath() + "/share/" +
                              QCoreApplication::applicationName())
                             .absolutePath();
#else
    auto systemDataDir = QDir(QCoreApplication::applicationDirPath() + "/../share/" +
                              QCoreApplication::applicationName())
                             .absolutePath();
#endif

    auto userDataDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    auto userDataFile = userDataDir + QDir::separator() + "lsp-servers.json";
    autoReloadFile(this, userDataFile, [this](const QString &s) {
        this->userDefinitions = loadDefinitionsFromFile(s);
        this->stopAllServers();
        this->onServersDefinitionsLoaded(false);
        return this->userDefinitions.size() != 0;
    });

    auto systemFile =
        QDir::toNativeSeparators(systemDataDir) + QDir::separator() + "lsp-servers.json";
    autoReloadFile(this, systemFile, [this](const QString &s) {
        this->systemDefinitions = loadDefinitionsFromFile(s);
        this->stopAllServers();
        this->onServersDefinitionsLoaded(true);
        return this->systemDefinitions.size() != 0;
    });
}

/// Records that one of the two lsp-servers.json files has been loaded, and offers
/// to download the shipped copy once both have reported in with nothing defined.
///
/// The wait for both matters: on a first run the user file is often absent, and
/// judging the setup from it alone would offer a download while the system file
/// that already names every server has not been read yet.
void LspPlugin::onServersDefinitionsLoaded(bool isSystem) {
    if (isSystem) {
        systemDefinitionsLoaded = true;
    } else {
        userDefinitionsLoaded = true;
    }
    if (!userDefinitionsLoaded || !systemDefinitionsLoaded) {
        return;
    }

    qDebug() << "LspPlugin:" << userDefinitions.size() << "user and" << systemDefinitions.size()
             << "system definitions";
    if (!userDefinitions.isEmpty() || !systemDefinitions.isEmpty()) {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        auto messageBox = QMessageBox(getManager());
        messageBox.setText(tr("You are missing <code>lsp-servers.json</code>."));
        messageBox.setInformativeText(
            tr("This file is needed to configure which LSP servers can be used. Without it the IDE "
               "will not be fully functional. We can download and install the file automatically "
               "for you."));
        messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        if (messageBox.exec() == QMessageBox::Yes) {
            downloadServersDefinitions();
        }
    });
}

/// Fetches the lsp-servers.json the project ships and installs it into the user
/// config directory, then opens it so the result can be reviewed.
void LspPlugin::downloadServersDefinitions() {
    auto const url = QUrl("https://raw.githubusercontent.com/codepointerapp/codepointer/refs/heads/"
                          "main/src/plugins/LSP/lsp-servers.json");
    auto const userDataFile = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                              QDir::separator() + "lsp-servers.json";
    auto *networkManager = new QNetworkAccessManager(this);
    auto *reply = networkManager->get(QNetworkRequest(url));

    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, userDataFile]() {
        // Safe to defer the delete and keep using both objects below: deleteLater()
        // only schedules destruction once control returns to the event loop.
        reply->deleteLater();
        networkManager->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(
                nullptr, tr("Download failed"),
                tr("Could not download lsp-servers.json: %1").arg(reply->errorString()));
            return;
        }

        // QSaveFile writes a temporary next to the target and renames it
        // into place. This file is watched and reloaded, so a truncated
        // one would be read back as "no servers configured".
        auto file = QSaveFile(userDataFile);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(nullptr, tr("Save failed"),
                                 tr("Could not save lsp-servers.json: %1").arg(file.errorString()));
            return;
        }

        auto const data = reply->readAll();
        if (file.write(data) != data.size()) {
            QMessageBox::warning(
                nullptr, tr("Save failed"),
                tr("Could not write lsp-servers.json: %1").arg(file.errorString()));
            file.cancelWriting();
            return;
        }

        if (!file.commit()) {
            QMessageBox::warning(
                nullptr, tr("Save failed"),
                tr("Could not commit lsp-servers.json: %1").arg(file.errorString()));
            return;
        }

        // The watcher reloads the file on its own; opening it is only so
        // the user can see what was installed.
        if (auto manager = getManager()) {
            manager->openFile(userDataFile);
        }
    });
}

void LspPlugin::on_client_unmerged(qmdiHost *host) {
    cleanup();
    delete debugDock;
    debugDock = nullptr;
    debugWidget = nullptr;
    delete referencesDock;
    referencesDock = nullptr;
    referencesWidget = nullptr;
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

QString LspPlugin::editorTextFor(const QString &fileName) {
    auto manager = getManager();
    if (!manager) {
        return {};
    }
    // Same normalisation applyTextEdits() uses to reach the editor, so this
    // reports on the editor the edits will actually land in.
    auto native = QDir::toNativeSeparators(QFileInfo(fileName).absoluteFilePath());
    auto editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(native));
    if (!editor) {
        return {};
    }
    return editor->getContent();
}

/// Applies `edits` to a file that has no editor open, by rewriting it on disk.
///
/// Returns true when the file was changed. The write is refused rather than
/// attempted when the text on disk no longer matches the ranges - the file was
/// never in an editor, so it cannot have unsaved changes, which means a mismatch
/// is the file having been modified underneath the server while it worked.
///
/// No formatting is run over the result. The replacement text is the server's,
/// and reformatting a file the user cannot see is a second, unrequested change
/// on top of the first.
auto LspPlugin::applyEditsToClosedFile(const QString &fileName, const QList<LspTextEdit> &edits,
                                       const QString &expectedOldName) -> bool {
    auto file = QFile(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "LspPlugin: cannot apply edits, cannot read" << fileName
                   << file.errorString();
        return false;
    }
    auto const original = file.readAll();
    file.close();

    auto plainEdits = std::vector<lspTextEdit::Edit>();
    plainEdits.reserve(edits.size());
    for (auto const &edit : edits) {
        plainEdits.push_back(lspTextEdit::Edit{edit.startLine, edit.startCharacter, edit.endLine,
                                               edit.endCharacter, edit.newText.toStdString()});
    }

    auto result =
        lspTextEdit::apply(original.toStdString(), plainEdits, expectedOldName.toStdString());
    if (!result.applied) {
        // Nothing was rewritten, so the file is as it was. Say why: a refusal here
        // is usually the file on disk moving on while the server worked, which is
        // worth the user knowing about rather than a rename that half happened.
        qWarning() << "LspPlugin: refused" << result.rejected.size() << "edit(s) for" << fileName
                   << (result.rejected.empty() ? QStringLiteral("no edit could be placed")
                                               : result.rejected.front().c_str());
        return false;
    }

    // Write to a temporary next to the target and move it into place, so an
    // interrupted write cannot leave a source file truncated.
    QSaveFile out(fileName);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning() << "LspPlugin: cannot apply edits, cannot write" << fileName
                   << out.errorString();
        return false;
    }
    if (out.write(result.text.data(), static_cast<qint64>(result.text.size())) < 0 ||
        !out.commit()) {
        qWarning() << "LspPlugin: cannot apply edits, write failed for" << fileName
                   << out.errorString();
        return false;
    }

    // The server has to be told, and syncOpenDocuments() cannot do it: it walks
    // visible tabs, and by definition this file is not one. Without this the
    // server keeps answering positional requests against the text it read before
    // we rewrote the file, so the next rename or hover in that file comes back
    // with ranges for text that no longer exists on disk.
    if (!syncDocument(fileName, QString::fromStdString(result.text))) {
        qWarning() << "LspPlugin: rewrote" << fileName
                   << "on disk but could not sync it to its server; it will answer"
                      "against the previous contents until the server reloads it";
    }
    return true;
}

int LspPlugin::applyTextEdits(const QList<LspTextEdit> &edits, const QString &expectedOldName,
                              const QString &renameOriginFile) {
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
        auto fileName = QDir::toNativeSeparators(QFileInfo(it.key()).absoluteFilePath());

        // Every range is stated against the original document, so applying top-down
        // would invalidate the ranges below. Sort descending and work backwards.
        auto &fileEdits = it.value();
        std::sort(fileEdits.begin(), fileEdits.end(), [](auto const &a, auto const &b) {
            return a.startLine != b.startLine ? a.startLine > b.startLine
                                              : a.startCharacter > b.startCharacter;
        });

        auto editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(fileName));
        if (!editor) {
            // Not open, so there is nothing to edit in memory and no tab worth
            // opening for it: a rename that reaches a public interface would
            // otherwise open every header it touches. The file is rewritten on
            // disk instead, and picked up when it is opened later.
            //
            // The symbol check is applied to every file, not only the one the
            // rename started in. Out here nothing is undoable and nothing is
            // visible, so a range that no longer covers the symbol would corrupt
            // a file the user cannot see; refusing leaves it untouched.
            if (applyEditsToClosedFile(it.key(), fileEdits, expectedOldName)) {
                changed++;
            }
            continue;
        }

        auto documentEdits = QList<qmdiEditor::TextEdit>();
        for (auto const &edit : fileEdits) {
            documentEdits.append(qmdiEditor::TextEdit{edit.startLine, edit.startCharacter,
                                                      edit.endLine, edit.endCharacter,
                                                      edit.newText});
        }
        // The symbol check only holds for the file the rename was started in: its
        // ranges are the ones the server resolved against a known symbol. Other
        // files in a cross-file rename are edited on the plain path.
        auto nativePath = QDir::toNativeSeparators(QFileInfo(it.key()).absoluteFilePath());
        auto isRenameOrigin = expectedOldName.isEmpty() || nativePath == renameOriginFile;
        if (!editor->applyTextEdits(documentEdits, isRenameOrigin ? expectedOldName : QString{})) {
            continue;
        }
        changed++;
    }

    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        dirtyDocuments.insert(QFileInfo(it.key()).absoluteFilePath());
    }
    documentSyncTimer.start();
    return changed;
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

    auto byOwner = QHash<QString, QList<lspDiagnostic::Diagnostic>>();
    {
        auto locker = QMutexLocker(&diagnosticsMutex);
        for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
            auto const perFile = it.value().constFind(fileName);
            if (perFile != it.value().cend() && !perFile->isEmpty()) {
                byOwner.insert(it.key(), *perFile);
            }
        }
    }

    editor->clearAllDiagnostics();
    auto shown = 0;
    for (auto it = byOwner.cbegin(); it != byOwner.cend(); ++it) {
        for (auto const &item : it.value()) {
            auto severity = Qutepart::DiagnosticSeverity::Information;
            switch (item.severity) {
            case lspDiagnostic::Severity::Error:
                severity = Qutepart::DiagnosticSeverity::Error;
                break;
            case lspDiagnostic::Severity::Warning:
                severity = Qutepart::DiagnosticSeverity::Warning;
                break;
            case lspDiagnostic::Severity::Information:
            case lspDiagnostic::Severity::Hint:
                break;
            }
            // The one place the protocol type meets the editor's: every line
            // based API here takes an int block number.
            editor->setDiagnostic(static_cast<int>(item.line),
                                  Qutepart::Diagnostic{it.key(), severity, item.message});
            shown++;
        }
    }
    editor->update();
    qDebug() << "LspPlugin:" << shown << "diagnostic(s) applied to"
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
                // The file goes from every server's set, not from the map itself.
                for (auto owner = diagnostics.begin(); owner != diagnostics.end(); ++owner) {
                    owner.value().remove(fileName);
                }
                staleDiagnostics.remove(fileName);
                syncedContent.forget(fileName);
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
    for (auto const &[cname, value] : client.value()->capabilities()) {
        out.append({QString::fromStdString(cname), QString::fromStdString(value)});
    }
    return out;
}

bool LspPlugin::syncDocument(const QString &fileName, const QString &text) {
    auto server = serverForFile(fileName);
    if (!server) {
        return false;
    }
    if (!server->syncDocument(QFileInfo(fileName).absoluteFilePath().toStdString(),
                              text.toStdString(), languageForFile(fileName).toStdString())) {
        return false;
    }
    dirtyDocuments.remove(fileName);
    syncedContent.record(fileName, text);
    {
        auto diagLocker = QMutexLocker(&diagnosticsMutex);
        staleDiagnostics.insert(fileName);
    }
    return true;
}

void LspPlugin::cleanup() {
    // Teardown: no restart of the servers, and the dock goes away right after.
    if (debugWidget && debugWidget->refreshTimer) {
        debugWidget->refreshTimer->stop();
    }
    auto locker = QMutexLocker(&serversMutex);
    servers.clear();
    syncedContent.clear();
}

void LspPlugin::stopAllServers() {
    // Held for the duration of the clear: destroying a client tears down its reader
    // thread, which re-enters the dock through queued trace/progress signals.
    if (debugWidget && debugWidget->refreshTimer) {
        debugWidget->refreshTimer->stop();
    }
    {
        auto locker = QMutexLocker(&serversMutex);
        servers.clear();
        syncedContent.clear();
    }
    syncOpenDocuments();

    if (debugWidget && debugWidget->refreshTimer) {
        // Deferred, so the servers registered by syncOpenDocuments() - and the
        // ready notifications still in flight - are visible on the first tick.
        QTimer::singleShot(0, debugWidget, [this]() {
            if (debugWidget && debugWidget->refreshTimer) {
                debugWidget->refreshTimer->start();
            }
            if (debugWidget) {
                debugWidget->refreshState();
            }
        });
    }
}

void LspPlugin::configurationHasBeenModified() { stopAllServers(); }

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

QList<LspServerDefinition> LspPlugin::serverDefinitions() const {
    auto d = userDefinitions + systemDefinitions;
    return d;
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
            [this, owner = definition.name](const std::string &file,
                                            const std::vector<lsp::Diagnostic> &items) {
                auto fileName = QDir::toNativeSeparators(
                    QFileInfo(QString::fromStdString(file)).absoluteFilePath());
                auto converted = QList<lspDiagnostic::Diagnostic>();
                converted.reserve(static_cast<int>(items.size()));
                for (auto const &item : items) {
                    converted.append(lspDiagnostic::fromItem(item));
                }
                {
                    auto locker = QMutexLocker(&diagnosticsMutex);
                    diagnostics[owner][fileName] = converted;
                    staleDiagnostics.remove(fileName);
                }
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
    if (command == GlobalCommands::ProjectLoaded || command == GlobalCommands::ProjectRemoved ||
        command == GlobalCommands::ProjectConfigChanged) {
        return CommandPriority::HighPriority;
    }

    if (command == GlobalCommands::VariableInfo || command == GlobalCommands::KeywordTooltip) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
            return CommandPriority::CannotHandle;
        }
        return serverForFile(fileName) ? CommandPriority::HighestPriority
                                       : CommandPriority::CannotHandle;
    }

    if (command == GlobalCommands::SignatureHelp) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
            return CommandPriority::CannotHandle;
        }
        auto server = serverForFile(fileName);
        if (!server || !server->hasCapability("signatureHelpProvider")) {
            return CommandPriority::CannotHandle;
        }
        return CommandPriority::HighestPriority;
    }

    if (command == GlobalCommands::RenameSymbol) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
            return CommandPriority::CannotHandle;
        }
        auto server = serverForFile(fileName);
        if (!server || !server->hasCapability("renameProvider")) {
            return CommandPriority::CannotHandle;
        }
        return CommandPriority::HighestPriority;
    }

    if (command == GlobalCommands::ShowReferences) {
        if (!referencesWidget || !referencesDock) {
            return CommandPriority::CannotHandle;
        }
        return CommandPriority::HighestPriority;
    }

    // The editor owns the menu, so this only has to reach the plugin that owns
    // the edits.
    if (command == GlobalCommands::ApplyCodeAction) {
        return CommandPriority::HighestPriority;
    }

    if (command == GlobalCommands::CodeAction) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
            return CommandPriority::CannotHandle;
        }
        auto server = serverForFile(fileName);
        if (!server || !server->hasCapability("codeActionProvider")) {
            return CommandPriority::CannotHandle;
        }
        return CommandPriority::HighestPriority;
    }

    if (command == GlobalCommands::FindReferences) {
        auto fileName = args[GlobalArguments::FileName].toString();
        if (languageForFile(fileName).isEmpty()) {
            return CommandPriority::CannotHandle;
        }
        auto server = serverForFile(fileName);
        if (!server || !server->hasCapability("referencesProvider")) {
            return CommandPriority::CannotHandle;
        }
        return CommandPriority::HighestPriority;
    }

    return CommandPriority::CannotHandle;
}

// Commands that answer on the GUI thread without a server in the picture. They
// get a ready future, so handleCommandAsync can return before the rest.

void LspPlugin::handleProjectLoaded(const CommandArgs &args) {
    auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
    if (sourceDir.isEmpty()) {
        return;
    }
    auto root = QDir::cleanPath(QDir(sourceDir).absolutePath());
    auto buildDir = args[GlobalArguments::BuildDirectory].toString();
    projectRoots.insert(root, buildDir.isEmpty() ? QString()
                                                 : QDir::cleanPath(QDir(buildDir).absolutePath()));
    // Servers start when a file of their language is actually opened.
    syncOpenDocuments();
}

void LspPlugin::handleProjectConfigChanged(const CommandArgs &args) {
    auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
    if (!sourceDir.isEmpty()) {
        // Refresh root -> build dir before restarting, or ensureServerForFile()
        // rebuilds the servers against the directory the old config named.
        auto root = QDir::cleanPath(QDir(sourceDir).absolutePath());
        auto buildDir = args[GlobalArguments::BuildDirectory].toString();
        projectRoots.insert(
            root, buildDir.isEmpty() ? QString() : QDir::cleanPath(QDir(buildDir).absolutePath()));
    }
    stopAllServers();
}

void LspPlugin::handleProjectRemoved(const CommandArgs &args) {
    auto root =
        QDir::cleanPath(QDir(args[GlobalArguments::SourceDirectory].toString()).absolutePath());
    projectRoots.remove(root);
    auto locker = QMutexLocker(&serversMutex);
    servers.remove(root);
}

void LspPlugin::handleShowReferences(const CommandArgs &args) {
    // Always on the GUI thread here: the editor dispatches this from a menu
    // action, and the widget owns widgets.
    referencesWidget->setResults(args[GlobalArguments::Symbol].toString(),
                                 args[GlobalArguments::Tags].toList());
}

void LspPlugin::handleApplyCodeAction(const CommandArgs &args) {
    auto edits = QList<LspTextEdit>();
    for (auto const &row : args[GlobalArguments::Tags].toList()) {
        auto const edit = row.toHash();
        edits.append(LspTextEdit{
            .file = edit[GlobalArguments::FileName].toString(),
            .newText = edit[GlobalArguments::NewName].toString(),
            .startLine = edit[GlobalArguments::LineNumber].toInt(),
            .startCharacter = edit[GlobalArguments::ColumnNumber].toInt(),
            .endLine = edit[GlobalArguments::EndLineNumber].toInt(),
            .endCharacter = edit[GlobalArguments::EndColumnNumber].toInt(),
        });
    }
    emit traceMessage(tr("LspPlugin: applying code action \"%1\" with %2 edit(s)")
                          .arg(args[GlobalArguments::Name].toString())
                          .arg(edits.size()));

    // The ranges were computed against the buffer as it was when the action was
    // offered. Applying them to a buffer that has moved writes the fix into the
    // wrong place: an inserted #include lands at a stale offset, the error
    // stays, and the marker never clears. Refusing is the same trade rename
    // makes, for the same reason.
    auto const requested = args[GlobalArguments::Content].toString();
    if (!requested.isEmpty()) {
        auto const current = editorTextFor(args[GlobalArguments::FileName].toString());
        if (current != requested) {
            emit traceMessage(tr("LspPlugin: refusing code action - the file changed after it was "
                                 "offered"));
            return;
        }
    }

    // applyTextEdits() sorts bottom-up per file and rewrites files that have no
    // editor open, which a fix touching a header needs.
    auto const changed = applyTextEdits(edits);
    emit traceMessage(tr("LspPlugin: code action changed %1 file(s)").arg(changed));
}

// Handlers that talk to a server.

void LspPlugin::handleDefinition(const LspRequest &ctx, const CommandArgs &args,
                                 std::shared_ptr<PendingRequest> pending) {
    auto const symbol = args[GlobalArguments::RequestedSymbol].toString();
    ctx.server->requestDefinition(
        ctx.path, ctx.line, ctx.column,
        [pending, symbol](std::vector<LspClientImpl::Location> locations) {
            auto tags = QVariantList();
            for (auto const &location : locations) {
                tags.append(QVariant::fromValue(CommandArgs{
                    {GlobalArguments::FileName, QString::fromStdString(location.file)},
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
}

void LspPlugin::handleCompletion(const LspRequest &ctx, std::shared_ptr<PendingRequest> pending) {
    ctx.server->requestCompletion(
        ctx.path, ctx.line, ctx.column,
        [pending, fileName = ctx.fileName](std::vector<lsp::CompletionItem> items) {
            auto tags = QVariantList();
            tags.reserve(static_cast<int>(items.size()));
            for (auto const &item : items) {
                auto const detail =
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
}

void LspPlugin::handleSignatureHelp(const LspRequest &ctx,
                                    std::shared_ptr<PendingRequest> pending) {
    ctx.server->requestSignatureHelp(
        ctx.path, ctx.line, ctx.column, [pending](lsp::SignatureHelp help) {
            pending->complete(CommandArgs{{GlobalArguments::Tooltip, renderSignatureHelp(help)}});
        });
}

void LspPlugin::handleFindReferences(const LspRequest &ctx, const CommandArgs &args,
                                     std::shared_ptr<PendingRequest> pending) {
    auto const symbol = args[GlobalArguments::RequestedSymbol].toString();
    ctx.server->requestReferences(
        ctx.path, ctx.line, ctx.column, true,
        [pending, symbol](std::vector<LspClientImpl::Location> locations) {
            auto tags = QVariantList();
            tags.reserve(static_cast<int>(locations.size()));
            for (auto const &location : locations) {
                tags.append(QVariant::fromValue(CommandArgs{
                    {GlobalArguments::FileName, QString::fromStdString(location.file)},
                    {GlobalArguments::LineNumber, location.line + 1},
                    {GlobalArguments::ColumnNumber, location.column + 1},
                    {GlobalArguments::Name, symbol},
                    {GlobalArguments::Value, symbol},
                    {GlobalArguments::Raw, symbol},
                    {GlobalArguments::Type, QStringLiteral("reference")},
                    {GlobalArguments::Source, QStringLiteral("LSP")},
                    {GlobalArguments::IsDefinition, false},
                }));
            }
            pending->complete(CommandArgs{
                {GlobalArguments::Symbol, symbol},
                {GlobalArguments::Tags, tags},
            });
        });
}

void LspPlugin::handleHover(const LspRequest &ctx, std::shared_ptr<PendingRequest> pending) {
    ctx.server->requestHover(ctx.path, ctx.line, ctx.column, [pending](std::string text) {
        auto result = CommandArgs{};
        if (!text.empty()) {
            result[GlobalArguments::Tooltip] = QString::fromStdString(text);
        }
        pending->complete(result);
    });
}

void LspPlugin::handleRename(const LspRequest &ctx, const CommandArgs &args,
                             std::shared_ptr<PendingRequest> pending) {
    // The editor owns the prompt and supplies the name; all that is left is the
    // round trip to the server and applying the edits it returns.
    auto const newName = args[GlobalArguments::NewName].toString();
    // Resolved from the text the editor sent, so the reply can be checked against
    // the symbol this request was actually about.
    auto const oldName = lspRename::identifierAt(ctx.content, ctx.line, ctx.column);
    auto const fileName = ctx.fileName;
    auto const content = ctx.content;

    ctx.server->requestRename(
        ctx.path, ctx.line, ctx.column, newName.toStdString(),
        [this, newName, oldName, pending, fileName,
         content](std::vector<LspClientImpl::TextEdit> edits, const std::string &serverError) {
            if (!serverError.empty()) {
                pending->complete(
                    CommandArgs{{GlobalArguments::ErrorMessage,
                                 tr("The language server could not rename the symbol: %1")
                                     .arg(QString::fromStdString(serverError))}});
                return;
            }
            auto converted = QList<LspTextEdit>();
            for (auto const &e : edits) {
                converted.append(LspTextEdit{QString::fromStdString(e.file),
                                             QString::fromStdString(e.newText), e.startLine,
                                             e.startCharacter, e.endLine, e.endCharacter});
            }
            // Editors are GUI-only and this arrives on a reader thread.
            QMetaObject::invokeMethod(
                this,
                [this, converted, newName, oldName, pending, fileName, content]() {
                    // The ranges are positional and were computed against the text
                    // the server held when the request went out. A whole-document
                    // comparison here would refuse every rename where an unrelated
                    // part of the file moved in flight, which is the common case
                    // rather than the exception. applyRenameEdits() is the real
                    // guard: it refuses any edit whose range does not cover the
                    // symbol being renamed, so a reply for a different revision
                    // cannot corrupt the file. This only reports, to show whether
                    // the buffer is being touched at all.
                    if (editorTextFor(fileName) != content) {
                        qWarning() << "LspPlugin: buffer differs from request revision"
                                   << "file:" << fileName << "requestedBytes:" << content.size();
                    }
                    auto files = applyTextEdits(converted, oldName, fileName);
                    qDebug() << "LspPlugin: renamed to" << newName << "across" << files
                             << "file(s)," << converted.size() << "edits";
                    pending->complete(CommandArgs{});
                },
                Qt::QueuedConnection);
        });
}

auto LspPlugin::overlappingDiagnostics(const QString &fileName, uint startLine, uint endLine) const
    -> QList<lspDiagnostic::Diagnostic> {
    auto out = QList<lspDiagnostic::Diagnostic>();
    auto locker = QMutexLocker(&diagnosticsMutex);
    // Every server's findings, since the server answers on what it was told
    // regardless of which of them this request concerns.
    for (auto owner = diagnostics.cbegin(); owner != diagnostics.cend(); ++owner) {
        auto const perFile = owner.value().constFind(fileName);
        if (perFile == owner.value().cend()) {
            continue;
        }
        for (auto const &item : *perFile) {
            auto const lastLine = std::max(item.endLine, item.line);
            if (item.line <= endLine && lastLine >= startLine) {
                out.append(item);
            }
        }
    }
    return out;
}

/// Turns one server reply into the {Name, Type, Tags} entries the menu and the
/// margin mark consume. Free-standing because the reply type is a nested struct
/// of LspClientImpl, which the plugin header cannot name; `trace` is passed in
/// so the drops can still be reported.
static auto offerableActions(std::vector<LspClientImpl::CodeAction> actions, const QString &content,
                             const std::function<void(QString)> &trace) -> QVariantList {
    auto out = QVariantList();
    out.reserve(static_cast<int>(actions.size()));

    // The request carries every diagnostic on the line, and a server answers with
    // one action per diagnostic - so "add the missing include" comes back once
    // per problem it would fix. Same title means the same edit here, so only the
    // first is kept.
    auto seenTitles = QSet<QString>();

    for (auto const &action : actions) {
        // Each of these is a real gap, not a reason to hide the entry quietly, so
        // each says what it would take. A menu entry that does nothing when
        // clicked is worse than no entry at all.
        if (action.needsResolve) {
            trace(QObject::tr("LspPlugin: dropping \"%1\" [%2] - needs codeAction/resolve")
                      .arg(QString::fromStdString(action.title),
                           QString::fromStdString(action.kind)));
            continue;
        }
        if (action.needsCommand) {
            trace(QObject::tr("LspPlugin: dropping \"%1\" [%2] - needs executeCommand \"%3\"")
                      .arg(QString::fromStdString(action.title),
                           QString::fromStdString(action.kind),
                           QString::fromStdString(action.commandName)));
            continue;
        }
        if (action.edits.empty()) {
            trace(QObject::tr("LspPlugin: dropping \"%1\" [%2] - no edits and no follow-up")
                      .arg(QString::fromStdString(action.title),
                           QString::fromStdString(action.kind)));
            continue;
        }

        auto const title = QString::fromStdString(action.title);
        if (seenTitles.contains(title)) {
            continue;
        }
        seenTitles.insert(title);

        auto edits = QVariantList();
        edits.reserve(static_cast<int>(action.edits.size()));
        for (auto const &edit : action.edits) {
            edits.append(QVariant::fromValue(CommandArgs{
                {GlobalArguments::FileName, QString::fromStdString(edit.file)},
                {GlobalArguments::LineNumber, edit.startLine},
                {GlobalArguments::ColumnNumber, edit.startCharacter},
                {GlobalArguments::EndLineNumber, edit.endLine},
                {GlobalArguments::EndColumnNumber, edit.endCharacter},
                {GlobalArguments::NewName, QString::fromStdString(edit.newText)},
            }));
        }
        out.append(QVariant::fromValue(CommandArgs{
            {GlobalArguments::Name, title},
            {GlobalArguments::Type, QString::fromStdString(action.kind)},
            {GlobalArguments::Tags, edits},
            {GlobalArguments::IsDefinition, action.isPreferred},
            {GlobalArguments::ReadOnly, action.disabled},
            {GlobalArguments::ErrorMessage, QString::fromStdString(action.disabledReason)},
            // The text these ranges were computed against, so ApplyCodeAction can
            // refuse rather than write a fix into a buffer that has since moved.
            {GlobalArguments::Content, content},
        }));
    }
    return out;
}

void LspPlugin::handleCodeActionRequest(const LspRequest &ctx, const CommandArgs &args,
                                        std::shared_ptr<PendingRequest> pending) {
    // Echoed back verbatim, `data` included: this is how the server recognises
    // which of its own diagnostics it already handed us.
    //
    // When they are stale we send none. Sending them would have the server match
    // its stored fix-its against ranges describing text it has already been told
    // changed, which is how a fix gets offered for a line that no longer has the
    // problem. An empty array is the honest answer: clangd still answers from the
    // range, it just cannot attribute a specific diagnostic.
    auto const stale = [&] {
        auto locker = QMutexLocker(&diagnosticsMutex);
        return staleDiagnostics.contains(ctx.fileName);
    }();
    auto overlapping = stale ? QList<lspDiagnostic::Diagnostic>()
                             : overlappingDiagnostics(ctx.fileName, ctx.startLine, ctx.endLine);

    auto sent = std::vector<lsp::Diagnostic>();
    sent.reserve(static_cast<size_t>(overlapping.size()));
    for (auto const &item : std::as_const(overlapping)) {
        sent.push_back(lspDiagnostic::toItem(item));
    }

    if (stale) {
        emit traceMessage(tr("LspPlugin: diagnostics for %1 are stale - sent none")
                              .arg(QFileInfo(ctx.fileName).fileName()));
    } else {
        emit traceMessage(tr("LspPlugin: codeAction over %1:%2-%3:%4 with %5 diagnostic(s) sent")
                              .arg(ctx.fileName)
                              .arg(ctx.startLine)
                              .arg(ctx.startCharacter)
                              .arg(ctx.endLine)
                              .arg(ctx.endCharacter)
                              .arg(sent.size()));
    }

    // Empty asks for everything, which is what the menu wants. The margin mark
    // sends "quickfix" instead: a refactoring is offered on most lines and would
    // light every one of them.
    auto kinds = std::vector<std::string>();
    for (auto const &kind : args[GlobalArguments::OnlyCodeActionKinds].toStringList()) {
        kinds.push_back(kind.toStdString());
    }

    auto const content = ctx.content;
    ctx.server->requestCodeActions(
        ctx.path, ctx.startLine, ctx.startCharacter, ctx.endLine, ctx.endCharacter, sent, kinds,
        [this, pending, content](std::vector<LspClientImpl::CodeAction> actions) {
            auto const out = offerableActions(std::move(actions), content, [this](QString message) {
                emit traceMessage(message);
            });
            emit traceMessage(tr("LspPlugin: offering %1 action(s)").arg(out.size()));
            pending->complete(CommandArgs{{GlobalArguments::Actions, out}});
        });
}

QFuture<CommandArgs> LspPlugin::handleCommandAsync(const QString &command,
                                                   const CommandArgs &args) {
    // Handled entirely on this thread, so they answer before a server is even
    // looked up - and each one returns a ready future.
    if (command == GlobalCommands::ProjectLoaded) {
        handleProjectLoaded(args);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }
    if (command == GlobalCommands::ProjectConfigChanged) {
        handleProjectConfigChanged(args);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }
    if (command == GlobalCommands::ProjectRemoved) {
        handleProjectRemoved(args);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }
    if (command == GlobalCommands::ShowReferences) {
        handleShowReferences(args);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }
    if (command == GlobalCommands::ApplyCodeAction) {
        // A refusal has to reach the caller, so unlike the others this one is not
        // a ready future; the reason travels in the result either way.
        handleApplyCodeAction(args);
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    auto ctx = LspRequest{};
    ctx.fileName = args[GlobalArguments::FileName].toString();
    ctx.server = serverForFile(ctx.fileName);
    if (!ctx.server) {
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    ctx.line = args[GlobalArguments::LineNumber].toInt();
    ctx.column = args[GlobalArguments::ColumnNumber].toInt();
    // A range command falls back to the caret, so callers without a selection do
    // not have to send a zero-width range.
    ctx.startLine = static_cast<uint>(ctx.line);
    ctx.startCharacter = static_cast<uint>(ctx.column);
    ctx.endLine = args.contains(GlobalArguments::EndLineNumber)
                      ? static_cast<uint>(args[GlobalArguments::EndLineNumber].toInt())
                      : ctx.startLine;
    ctx.endCharacter = args.contains(GlobalArguments::EndColumnNumber)
                           ? static_cast<uint>(args[GlobalArguments::EndColumnNumber].toInt())
                           : ctx.startCharacter;
    ctx.content = args[GlobalArguments::Content].toString();
    ctx.path = QFileInfo(ctx.fileName).absoluteFilePath().toStdString();

    // The server must see the current buffer before it answers a positional
    // question, otherwise the reply refers to a stale document. Full-text sync is
    // expensive (the server re-parses on each didChange), so only push when the
    // text actually differs from what the server last received.
    if (syncedContent.isStale(ctx.fileName, ctx.content) &&
        !syncDocument(ctx.fileName, ctx.content)) {
        // The push did not happen, so the server would answer about a document it
        // is not holding. Its ranges would be well-formed and wrong, and for a
        // rename that means writing the new name into the wrong places. Say so
        // instead of corrupting the buffer.
        qWarning() << "LspPlugin: cannot reach the server for" << ctx.fileName
                   << "- request dropped rather than answered against a stale document";
        return QtFuture::makeReadyValueFuture(
            CommandArgs{{GlobalArguments::ErrorMessage,
                         tr("The language server is not ready yet. Try again in a moment.")}});
    }

    auto pending = std::make_shared<PendingRequest>();
    auto future = pending->promise.future();
    pending->promise.start();

    // Two very different callers share VariableInfo: the completion popup sends
    // ExactMatch=false with a prefix, while "Follow symbol" sends ExactMatch=true
    // for the word under the cursor. The latter wants locations, not candidates.
    if (command == GlobalCommands::VariableInfo && args[GlobalArguments::ExactMatch].toBool()) {
        handleDefinition(ctx, args, pending);
    } else if (command == GlobalCommands::VariableInfo) {
        handleCompletion(ctx, pending);
    } else if (command == GlobalCommands::CodeAction) {
        handleCodeActionRequest(ctx, args, pending);
    } else if (command == GlobalCommands::SignatureHelp) {
        handleSignatureHelp(ctx, pending);
    } else if (command == GlobalCommands::FindReferences) {
        handleFindReferences(ctx, args, pending);
    } else if (command == GlobalCommands::RenameSymbol) {
        handleRename(ctx, args, pending);
    } else {
        handleHover(ctx, pending);
    }

    return future;
}
