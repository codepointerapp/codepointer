#pragma once

#include "TreeSitterEngine.hpp"
#include "iplugin.h"
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
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

    std::atomic<bool> scanIsCancelled{false};
    QFuture<CommandArgs> scanFuture;
    QFutureWatcher<CommandArgs> scanWatcher;

    TreeSitterEngine engine;

  public slots:
    void cleanup();
};
