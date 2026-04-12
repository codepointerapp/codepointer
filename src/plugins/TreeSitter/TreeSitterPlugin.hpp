#pragma once

#include "TreeSitterEngine.hpp"
#include "iplugin.h"
#include <QHash>
#include <QObject>

class TreeSitterPlugin : public IPlugin {
    Q_OBJECT

  public:
    TreeSitterPlugin();
    ~TreeSitterPlugin();

    virtual int canHandleAsyncCommand(const QString &command,
                                      const CommandArgs &args) const override;
    virtual QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                                    const CommandArgs &args) override;

  private:
    void generateSymbolReport(const QString &buildDir);
    TreeSitterEngine engine;
};
