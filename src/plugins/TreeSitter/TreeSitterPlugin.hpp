#pragma once

#include "iplugin.h"
#include <QObject>
#include <QHash>
#include "TreeSitterEngine.hpp"

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
    TreeSitterEngine engine;
};
