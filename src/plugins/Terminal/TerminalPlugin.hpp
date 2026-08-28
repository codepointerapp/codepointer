/**
 * \file TerminalPlugin.hpp
 * \brief Definition of the Terminal plugin
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * License MIT
 */

#pragma once

#include "KodoTerm/KodoTermConfig.hpp"
#include "iplugin.h"

class QDockWidget;
class QAction;
class KodoTerm;

class QLabel;

class TerminalPlugin : public IPlugin {
    struct Config {
        CONFIG_DEFINE(TerminalFont, QString)
        CONFIG_DEFINE(ThemeFile, QString)
        CONFIG_DEFINE(ThemeFileChoose, QString)
        CONFIG_DEFINE(PromptPreview, QString)
        CONFIG_DEFINE(TrippleClickClick, bool)
        CONFIG_DEFINE(CopyOnSelect, bool)
        CONFIG_DEFINE(PasteOnMiddleClick, bool)
        CONFIG_DEFINE(AudioBell, bool)
        CONFIG_DEFINE(VisualBell, bool)
        CONFIG_DEFINE(AntiAlias, bool)
        CONFIG_DEFINE(InitialDir, QString)
        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }

    Q_OBJECT
  public:
    TerminalPlugin();
    ~TerminalPlugin();

    // IPlugin interface
    virtual void on_client_merged(qmdiHost *host) override;
    virtual void on_client_unmerged(qmdiHost *host) override;
    virtual void loadConfig(QSettings &settings) override;
    virtual void configurationHasBeenModified() override;

    void updateTerminalPreview();

  private:
    QDockWidget *terminalDock = nullptr;
    QAction *toggleTerminal;
    KodoTerm *console = nullptr;
    KodoTermConfig consoleConfig;
    QLabel *promptPreviewLabel = nullptr;

    struct {
        TerminalTheme theme;
        QString themeFile;
        QString fontString;
    } tempConfig;
};
