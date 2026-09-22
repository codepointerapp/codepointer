#pragma once

/**
 * \file ProjectManagerV2Plugin.cpp
 * \brief Project manager plugin, V2
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * \see PluginManager
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "iplugin.h"

class ProjectManagerV2Plugin : public IPlugin {

    struct Config {
        // TODO - migration code
        CONFIG_DEFINE(BlackConsole, bool);
        CONFIG_DEFINE(ConsoleFont, QString)

        // Actual user config,
        CONFIG_DEFINE(SaveBeforeTask, bool);
        CONFIG_DEFINE(ExtraPath, QStringList);

        // Opened projects
        CONFIG_DEFINE(OpenPrject, QStringList);
        CONFIG_DEFINE(SelectedProject, QString);

        // State of the search panel
        CONFIG_DEFINE(SearchPath, QString);
        CONFIG_DEFINE(SearchPattern, QString);
        CONFIG_DEFINE(SearchInclude, QString);
        CONFIG_DEFINE(SearchExclude, QString);
        CONFIG_DEFINE(SearchWholeWords, bool);
        CONFIG_DEFINE(SearchSensitive, bool);
        CONFIG_DEFINE(SearchRegex, bool);
        CONFIG_DEFINE(SearchCollapseFileNames, bool);

        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }

    Q_OBJECT
  public:
    ProjectManagerV2Plugin();
    ~ProjectManagerV2Plugin();

    virtual void on_client_merged(qmdiHost *host) override;
    virtual void configurationHasBeenModified() override;
    virtual void loadConfig(QSettings &settings) override;
    virtual void saveConfig(QSettings &settings) override;
    virtual int canHandleAsyncCommand(const QString &command,
                                      const CommandArgs &args) const override;
    virtual QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                                    const CommandArgs &args) override;

    virtual qmdiActionGroup *getContextMenuActions(const QString &menuId,
                                                   const QString &filePath) override;
};
