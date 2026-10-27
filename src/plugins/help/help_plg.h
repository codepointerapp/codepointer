/**
 * \file help_plg.h
 * \brief Definition of the help system  plugin
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * License LGPL
 * \see HelpPlugin
 */

#pragma once

#include "iplugin.h"

#include <QKeyEvent>

class QAction;

enum UpdateCheck {
    NoChecks,
    EveryTime,
    Daily,
    Weekly,
};

enum UpdateChannels {
    Stable,
    Testing,
};

class HelpPlugin : public IPlugin {
    Q_OBJECT

    struct Config {
        CONFIG_DEFINE(UpdatesChecks, UpdateCheck)
        CONFIG_DEFINE(UpdatesChannel, UpdateChannels)
        // FIXME: use uint64_y when it is possible
        // why isn't this a uint64_t? since Variant for these types in Qt6 will save garbage to the
        // ini file. and it just does not work. As ugly as parsing strings manually instead of the
        // framework - this does work.
        CONFIG_DEFINE(LastUpdateTime, QString)
        CONFIG_DEFINE(UseWinUI3Theme, bool)
        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }

  public:
    HelpPlugin();
    ~HelpPlugin();

    virtual void on_client_merged(qmdiHost *host) override;
    virtual void on_client_unmerged(qmdiHost *host) override;

    void showAbout() override;
    virtual void loadConfig(QSettings &settings) override;
    void doStartupChecksForUpdate(bool notifyUserNoUpdates);
    void doChecksForUpdate(bool notifyUserNoUpdates);
    void uiCleanUp();
    bool isTaskRunnning() const;
    bool isBottomPanelsVisible() const;

    virtual void configurationHasBeenModified() override;
  public slots:
    void showWelcomeScreen();
    void actionAbout_triggered();
    void checkForUpdates_triggered();
    void updateTheme();

  protected:
    virtual bool eventFilter(QObject *obj, QEvent *event) override;
};
