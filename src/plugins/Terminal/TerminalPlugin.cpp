/**
 * \file TerminalPlugin.cpp
 * \brief Implementation of the Terminal plugin
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * License MIT
 */

#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QFontDatabase>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

#include <KodoTerm/KodoTerm.hpp>
#include <fontwidget.hpp>
#include <qmdidialogevents.hpp>

#include "plugins/Terminal/TerminalPlugin.hpp"

static QString systemCurrentShell() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#if defined(Q_OS_WIN)
    // Windows shell (usually cmd.exe or powershell)
    if (env.contains("ComSpec")) {
        return env.value("ComSpec");
    }
    return QString();
#else
    // Unix / Linux / macOS shell (bash, zsh, fish, etc.)
    if (env.contains("SHELL")) {
        return env.value("SHELL");
    }
    return QString();
#endif
}

TerminalPlugin::TerminalPlugin() {
    name = tr("Terminal support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
    consoleConfig.theme = TerminalTheme::defaultTheme();

    toggleTerminal = new QAction(tr("Toggle terminal"), this);
    toggleTerminal->setShortcut(QKeySequence("Ctrl+T"));
    connect(toggleTerminal, &QAction::triggered, this, [this]() {
        if (terminalDock->isVisible()) {
            terminalDock->hide();
        } else {
            terminalDock->show();
            terminalDock->raise();
            terminalDock->widget()->setFocus();
            console->setFocus();
        }
    });

    menus[tr("&File")]->addAction(toggleTerminal);

    auto monospacedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospacedFont.setFixedPitch(true);

    tempConfig.theme = consoleConfig.theme;

    config.pluginName = tr("Terminal");
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::PromptPreviewKey)
                                     .setType(qmdiConfigItem::Label)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Display font"))
                                     .setKey(Config::TerminalFontKey)
                                     .setType(qmdiConfigItem::Font)
                                     .setDefaultValue(monospacedFont)
                                     .setValue(monospacedFont)
                                     .build());

    // -> start hack
    // Instead of registering a full normal widget, we create it here in this plugin,
    // by using a list of configs:
    // 1. A button that will open a popup - to choose the theme. This is the main
    //    button seen on screen.
    // 2. A user defined config, to store the actual file chosen. No visible.
    // 3. As buttons don't have label - a special label is used.
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Theme"))
                                     .setDescription(tr("Which theme to use for the terminal"))
                                     .setType(qmdiConfigItem::Label)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::ThemeFileKey)
                                     .setType(qmdiConfigItem::String)
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Choose theme"))
            .setDescription(tr("Click to choose form internal themes available"))
            .setKey(Config::ThemeFileChooseKey)
            .setType(qmdiConfigItem::Button)
            .build());
    // -> end hack

    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Enable anti aliased painting"))
                                     .setKey(Config::AntiAliasKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Triple click selects whole line"))
                                     .setKey(Config::TrippleClickClickKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Copy text on selection"))
                                     .setKey(Config::CopyOnSelectKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Paste on middle mouse click"))
                                     .setKey(Config::PasteOnMiddleClickKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Make sound on terminal bells"))
                                     .setKey(Config::AudioBellKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Visual bell"))
                                     .setKey(Config::VisualBellKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Initial dir"))
                                     .setKey(Config::InitialDirKey)
                                     .setType(qmdiConfigItem::Path)
                                     // This sets it to "dir" mode, instead of file
                                     .setPossibleValue(false)
                                     .setDefaultValue(QDir::toNativeSeparators(QDir::homePath()))
                                     .build());
    connect(&qmdiDialogEvents::instance(), &qmdiDialogEvents::widgetCreated, this,
            [this, monospacedFont](auto dialog, auto const &item, auto label, auto widget) {
                if (item.key == Config::PromptPreviewKey) {
                    // hack?
                    // We should in theory have a referenced optional. Which is not available in
                    // C++. In theory, we could use this use this widget and de-reference it.
                    // However - the places where we use it, are callbacks from the dialog.
                    // Meaning, that we first must pass through this place.
                    promptPreviewLabel = label;
                    promptPreviewLabel->setAutoFillBackground(true);
                    promptPreviewLabel->setFrameStyle(QFrame::Panel);
                    {
                        QFont f;
                        f.fromString(getConfig().getTerminalFont());
                        f.setStyleStrategy(getConfig().getAntiAlias() ? QFont::PreferAntialias
                                                                      : QFont::NoAntialias);
                        promptPreviewLabel->setFont(f);
                    }
                    updateTerminalPreview();
                }

                if (item.key == Config::TerminalFontKey) {
                    auto f = qobject_cast<FontWidget *>(widget);
                    connect(f, &FontWidget::fontUpdated, f, [this, f]() {
                        QFont font = f->font();
                        font.setStyleStrategy(getConfig().getAntiAlias() ? QFont::PreferAntialias
                                                                         : QFont::NoAntialias);
                        promptPreviewLabel->setFont(font);
                        updateTerminalPreview();
                        tempConfig.fontString = font.toString();
                    });
                }

                if (item.key == Config::ThemeFileChooseKey) {
                    auto themeMenu = new QMenu(widget);
                    auto button = qobject_cast<QPushButton *>(widget);
                    auto themeCallback = [this](const TerminalTheme::ThemeInfo &info) {
                        qDebug() << "ThemeFileChooseKey - Theme is " << info.path;
                        this->tempConfig.theme = TerminalTheme::loadTheme(info.path);
                        this->tempConfig.themeFile = info.path;
                        updateTerminalPreview();
                    };
                    KodoTerm::populateThemeMenu(themeMenu, tr("Konsole"),
                                                TerminalTheme::ThemeFormat::Konsole, themeCallback);
                    KodoTerm::populateThemeMenu(themeMenu, tr("Windows Terminal"),
                                                TerminalTheme::ThemeFormat::WindowsTerminal,
                                                themeCallback);
                    KodoTerm::populateThemeMenu(themeMenu, tr("iTerm"),
                                                TerminalTheme::ThemeFormat::ITerm, themeCallback);
                    button->setMenu(themeMenu);
                }

                if (item.key == Config::AntiAliasKey) {
                    auto checkbox = qobject_cast<QCheckBox *>(widget);
                    if (checkbox) {
                        connect(checkbox, &QCheckBox::toggled, this, [this](bool checked) {
                            if (promptPreviewLabel) {
                                auto f = promptPreviewLabel->font();
                                f.setStyleStrategy(checked ? QFont::PreferAntialias
                                                           : QFont::NoAntialias);
                                promptPreviewLabel->setFont(f);
                                promptPreviewLabel->update();
                            }
                        });
                    }
                }
                Q_UNUSED(dialog);
            });
}

TerminalPlugin::~TerminalPlugin() {
    // TODO
}

// IPlugin interface
void TerminalPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);

    auto manager = dynamic_cast<PluginManager *>(host);
    console = new KodoTerm(manager);
    console->setProgram(systemCurrentShell());

    auto dir = getConfig().getInitialDir();
    console->setWorkingDirectory(dir);
    console->start();
    terminalDock = manager->createNewPanel(Panels::South, "terminalPanel", tr("Terminal"), console);
}

void TerminalPlugin::on_client_unmerged(qmdiHost *) { delete terminalDock; }

void TerminalPlugin::loadConfig(QSettings &settings) {

    IPlugin::loadConfig(settings);
    configurationHasBeenModified();
}

void TerminalPlugin::configurationHasBeenModified() {
    // TODO: register a normal widget for editing theme files, instead
    //       of this ugly workaround.
    // Why are we modifying the config here? it should have been done by the config
    // dialog?
    // Not on this case. We set the button for the config, instead of registering
    // a widget. This means that this data is handled by the dialog itself.
    getConfig().setThemeFile(tempConfig.themeFile);
    if (!tempConfig.fontString.isEmpty()) {
        getConfig().setTerminalFont(tempConfig.fontString);
    }

    consoleConfig.setDefaults();
    consoleConfig.font.fromString(getConfig().getTerminalFont());
    consoleConfig.tripleClickSelectsLine = getConfig().getTrippleClickClick();
    consoleConfig.copyOnSelect = getConfig().getCopyOnSelect();
    consoleConfig.pasteOnMiddleClick = getConfig().getPasteOnMiddleClick();
    consoleConfig.textAntialiasing = getConfig().getAntiAlias();
    consoleConfig.visualBell = getConfig().getVisualBell();
    consoleConfig.theme = TerminalTheme::loadTheme(getConfig().getThemeFile());
    consoleConfig.customBoxDrawing = true;
    console->setConfig(consoleConfig);
}

void TerminalPlugin::updateTerminalPreview() {
#ifdef Q_OS_WIN
    auto static pseudoPrompt = QString("C:\\> ver<br>Microsoft Windows [Version 10.0.19045.4170]");
#else
    auto static pseudoPrompt =
        QString("user@localhost:~$ uptime<br>12:34:56 up 10 days,  1:23,  2<br>users, "
                " load average: 0.05, 0.01, 0.00");
#endif

    auto pal = promptPreviewLabel->palette();
    pal.setColor(QPalette::Window, this->tempConfig.theme.background);
    pal.setColor(QPalette::WindowText, this->tempConfig.theme.foreground);

    auto colorsSpan = QString("<br><br>");
    for (auto i = 0; i < 16; i++) {
        auto c1 = this->tempConfig.theme.palette[i].name();
        auto c3 = QString("<span style='background-color: %1; color: %1'>-x-</span>").arg(c1);
        colorsSpan += c3;
    }

    promptPreviewLabel->setPalette(pal);
    promptPreviewLabel->setText(pseudoPrompt + colorsSpan);
}
