#pragma once

#include "TreeSitterEngine.hpp"
#include "iplugin.h"
#include <QFuture>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <atomic>

class TreeSitterPlugin : public IPlugin {
    Q_OBJECT

  public:
    TreeSitterPlugin();
    ~TreeSitterPlugin();

    virtual int canHandleAsyncCommand(const QString &command,
                                      const CommandArgs &args) const override;
    virtual QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                                    const CommandArgs &args) override;

    virtual void on_client_merged(qmdiHost *host) override;
    virtual void on_client_unmerged(qmdiHost *host) override;

  private:
    QFuture<CommandArgs> scanProjectDir(const QString &sourceDir);
    QFuture<CommandArgs> doScanProjectDir(const QString &sourceDir);
    void startNextScan();

    std::atomic<bool> scanIsCancelled{false};
    QFuture<CommandArgs> scanFuture;
    QFutureWatcher<CommandArgs> scanWatcher;
    QThreadPool scanPool;
    QTimer scanDebounceTimer;

    QStringList pendingScanDirs;
    QMutex queueMutex;

    // Tracks in-flight ListSymbols/VariableInfo/KeywordTooltip queries, which run on
    // QThreadPool::globalInstance() and touch `engine`. cleanup() must wait for these
    // before `engine` is destroyed, or a query still running on a worker thread will
    // use-after-free it.
    QFutureSynchronizer<CommandArgs> completionSynchronizer;

    TreeSitterEngine engine;

  public slots:
    void cleanup();
};
