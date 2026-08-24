#pragma once

#include <memory>

#include <QHash>
#include <QMutex>
#include <QString>

#include "iplugin.h"

class LspClientImpl;
class LspDebugWidget;
class QDockWidget;

/// Snapshot of one server, for the debug panel.
struct LspServerInfo {
    QString root;
    QString binary;
    QString serverName;
    bool ready = false;
    bool running = false;
    int documentCount = 0;
};

class LspPlugin : public IPlugin {
    struct Config {
        CONFIG_DEFINE(EnableLsp, bool)
        CONFIG_DEFINE(ClangdBinary, QString)
        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }

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
    /// Pushes `text` into the server owning `fileName`, as the editor would.
    bool syncDocument(const QString &fileName, const QString &text);

  public slots:
    /// Marks every open editor whose file is owned by a ready server as
    /// LSP-exclusive for completions, and clears the flag on the rest.
    void updateEditorCompletionMode();

  signals:
    /// Emitted when a server finishes its handshake. Queued: raised on a reader
    /// thread, consumed on the GUI thread.
    void serverReady();
    /// Emitted for every LSP message. Queued when it originates on a reader
    /// thread, so connected widgets are always touched on the GUI thread.
    void traceMessage(const QString &message);

  public slots:
    void cleanup();

  private:
    /// Returns a ready server owning `fileName`, or nullptr when none applies.
    LspClientImpl *serverForFile(const QString &fileName) const;
    void startServerForProject(const QString &sourceDir, const QString &buildDir);

    // Keyed by project source directory.
    QHash<QString, std::shared_ptr<LspClientImpl>> servers;
    mutable QMutex serversMutex;

    QString clangdBinary = QStringLiteral("clangd");

    LspDebugWidget *debugWidget = nullptr;
    QDockWidget *debugDock = nullptr;
};
