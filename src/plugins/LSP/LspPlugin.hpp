#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QTimer>

#include "iplugin.h"

class LspClientImpl;
class LspDebugWidget;
class QAction;

/// One language server as configured: which binary, how to launch it, and which
/// file suffixes it serves. Serialised as an array of these, in the shape of
/// `src/plugins/LSP/servers.json`.
///
/// `suffixes` maps a file extension to the LSP languageId sent in didOpen, so a
/// single clangd can serve both "c" and "cpp".
struct LspServerDefinition {
    QString name = {};
    /// Where to get the server. Shown when the binary cannot be found, which is
    /// the only moment the answer is actually useful.
    QString homepage = {};
    QStringList arguments = {};
    QString binary = {};
    QHash<QString, QString> suffixes = {};
};

class QDockWidget;

/// Snapshot of one server, for the debug panel.
/// A replacement to apply, mirrored from LspClientImpl::TextEdit so the plugin
/// header does not have to include the generated lsp types.
struct LspTextEdit {
    QString file;
    int startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
    QString newText;
};

// FIXME: we could merge this with LspServerDefinition
struct LspServerInfo {
    QString root;
    QString id;
    QString binary;
    QString serverName;
    bool ready = false;
    bool running = false;
    int documentCount = 0;
};

class LspPlugin : public IPlugin {
    struct Config {
        CONFIG_DEFINE(ServersJson, QString);
        CONFIG_DEFINE(ExtraPaths, QStringList);
        qmdiPluginConfig *config = nullptr;
    };
    struct ConstConfig {
        static constexpr auto ServersJsonKey = "ServersJson";
        static constexpr auto ExtraPathsKey = "ExtraPaths";
        QString getServersJson() const { return config->getVariable<QString>(ServersJsonKey); }
        QStringList getExtraPaths() const { return config->getVariable<QStringList>(ExtraPathsKey); }
        const qmdiPluginConfig *config = nullptr;
    };

    Config getConfig() { return Config{&this->config}; }
    ConstConfig getConstConfig() const { return ConstConfig{&this->config}; }

    Q_OBJECT

  public:
    LspPlugin();
    ~LspPlugin();

    int canHandleAsyncCommand(const QString &command, const CommandArgs &args) const override;
    QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                            const CommandArgs &args) override;

    void on_client_merged(qmdiHost *host) override;
    void on_client_unmerged(qmdiHost *host) override;

    // --- used by the debug panel -------------------------------------------
    QList<LspServerInfo> serverInfos() const;
    QList<QPair<QString, int>> documentsFor(const QString &root) const;
    /// Capabilities advertised by one server, as (name, value) pairs.
    QList<QPair<QString, QString>> capabilitiesFor(const QString &root,
                                                   const QString &serverId) const;
    /// Pushes `text` into the server owning `fileName`, as the editor would.
    bool syncDocument(const QString &fileName, const QString &text);

  public slots:
    /// Marks every open editor whose file is owned by a ready server as
    /// LSP-exclusive for completions, and clears the flag on the rest.
    void updateEditorCompletionMode();

    /// Announces every open, LSP-owned document to its server. Diagnostics are
    /// only published for documents the server has been told about, so merely
    /// opening a file has to trigger didOpen - the query path is too late.
    void syncOpenDocuments();

    /// Sends didClose for any document a server still holds that is no longer open
    /// in an editor. Without it clangd keeps every file it ever saw - each with its
    /// own multi-megabyte preamble - and keeps publishing diagnostics for them.
    void reconcileOpenDocuments();

    /// Pushes every document marked dirty by an edit to its server. Called by the
    /// debounce timer, never directly from a keystroke.
    void flushDirtyDocuments();

    /// Paints the cached diagnostics for `fileName` onto its editor, if open.
    void applyDiagnostics(const QString &fileName);

    /// Asks the server what refactorings it offers at the cursor, shows them in a
    /// menu, and applies the chosen one.
    void refactorAtCursor();
  signals:
    /// Emitted when a server finishes its handshake. Queued: raised on a reader
    /// thread, consumed on the GUI thread.
    void serverReady();

    /// Emitted for every LSP message. Queued when it originates on a reader
    /// thread, so connected widgets are always touched on the GUI thread.
    void traceMessage(const QString &message);

    /// Background work reported by a server (clangd indexing). Queued: raised on
    /// a reader thread.
    void progressChanged(const QString &root, const QString &text, int percentage, bool active);

    /// Raised on a reader thread when a server publishes diagnostics for a file.
    void diagnosticsReady(const QString &fileName);

  public slots:
    void cleanup();

  private:
    /// Returns a ready server owning `fileName`, or nullptr when none applies.
    LspClientImpl *serverForFile(const QString &fileName) const;

    /// Starts the server for this file's language, if the file belongs to a known
    /// project and that server is not already up. Servers are started on demand:
    /// launching every installed server for every project means clangd running on
    /// a Rust project and rust-analyzer being handed a C++ root, which it rejects.
    void ensureServerForFile(const QString &fileName);

    /// Built-in definitions merged with the user's JSON overrides.
    QList<LspServerDefinition> serverDefinitions() const;

    /// The LSP languageId for this file, or empty when no server claims it.
    QString languageForFile(const QString &fileName) const;

    void startOneServer(const LspServerDefinition &definition, const QString &executable,
                        const std::vector<std::string> &arguments, const QString &root);

    /// Resolves a server binary against the configured extra paths first, then
    /// PATH. Returns an empty string when it cannot be found.
    QString resolveServerBinary(const QString &binary) const;

    // project source directory -> server id -> client
    QHash<QString, QHash<QString, std::shared_ptr<LspClientImpl>>> servers;

    // project source directory -> build directory, recorded on ProjectLoaded
    QHash<QString, QString> projectRoots;
    mutable QList<LspServerDefinition> cachedDefinitions;
    mutable QMutex serversMutex;

    // FIXME: add a enum for severity.
    struct Diagnostic {
        int line = 0;
        int severity = 1; // lsp::DiagnosticSeverity: 1 error, 2 warning, 3 info, 4 hint
        QString message;
    };

    // Written from reader threads, read on the GUI thread.
    mutable QMutex diagnosticsMutex;
    QHash<QString, QList<Diagnostic>> diagnostics;

    // Lines we marked last time, so ours can be cleared without wiping the build
    // markers that ProjectManager paints onto the same editor.
    QHash<QString, QList<int>> markedLines;

    // Editors we have already hooked destroyed()/contentChanged() on.
    QSet<QObject *> watchedEditors;

    // Typing produces a keystroke-rate stream of changes, and didChange sends the
    // whole document each time. Collect the dirty files and push them once the
    // user pauses. Requests that depend on the buffer (completion, hover,
    // definition, refactor) still sync immediately - a debounced sync there would
    // have the server answering against stale text.
    static constexpr int DocumentSyncDebounceMs = 3000;
    QTimer documentSyncTimer;
    QSet<QString> dirtyDocuments;

    /// Applies a set of server-supplied edits. Returns the number of files changed.
    int applyTextEdits(const QList<struct LspTextEdit> &edits);
    /// Prompts for a new name and applies the server's rename edits.
    void startRename(const QString &path, int line, int character);

    QAction *refactorAction = nullptr;
    LspDebugWidget *debugWidget = nullptr;
    QDockWidget *debugDock = nullptr;
};
