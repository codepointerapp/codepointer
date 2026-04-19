#pragma once

#include "TreeSitterEngine.hpp"
#include "iplugin.h"
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
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

    QStringList pendingScanDirs;
    QMutex queueMutex;

    TreeSitterEngine engine;

  public slots:
    void cleanup();
};
