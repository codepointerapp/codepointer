/**
 * \file main.cpp
 * \brief Entry point of application - CodePointer
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QPainter>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleOptionDockWidget>
#include <QToolButton>
#include <QtGlobal>

#include <winui3style/winui3style.h>

#include "pluginmanager.h"
#include "plugins/CTags/CTagsPlugin.hpp"
#include "plugins/CodeFormat/CodeFormat.hpp"
#include "plugins/LSP/LspPlugin.hpp"
#include "plugins/ProjectManager/ProjectManagerPlg.h"
#include "plugins/SplitTabsPlugin/SplitTabsPlugin.hpp"
#include "plugins/Terminal/TerminalPlugin.hpp"
#include "plugins/filesystem/filesystembrowser.h"
#include "plugins/git/GitPlugin.hpp"
#include "plugins/help/help_plg.h"
#include "plugins/hexviewer/hexviewer_plg.h"
#include "plugins/imageviewer/imageviewer_plg.h"
#include "plugins/texteditor/texteditor_plg.h"

int main(int argc, char *argv[]) {
    Q_INIT_RESOURCE(qutepart_syntax_files);
    Q_INIT_RESOURCE(qutepart_theme_data);

    // File dialogs come up blank on some macOS setups - a virtualised machine, where
    // the AppKit open and save panels are never presented at all. Nothing here can fix
    // that, so the way round it is to stop asking AppKit and let Qt draw its own
    // dialog, which lives inside the window and so cannot go missing.
    //
    // Opt-in through the environment rather than unconditional: the native panel is
    // better where it works (it integrates with Quick Look, Tags and the system
    // places sidebar), so it stays the default and only this opt out.
    //
    //   CODEPOINTER_QT_FILE_DIALOGS=1 ./codepointer
    //
    // Must be set before QApplication exists, since that is when Qt reads it.
    if (!qEnvironmentVariableIsEmpty("CODEPOINTER_QT_FILE_DIALOGS")) {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(CODEPOINTER_APP_NAME);
    QCoreApplication::setApplicationVersion("0.1.9");


#if defined(WIN32)
    auto needsIcons = true;
    auto iconsPath = "/share/icons";
#else
    auto needsIcons = QIcon::fromTheme(QIcon::ThemeIcon::GoNext).isNull();
    auto iconsPath = "/../share/icons";
#endif

    // On bare-bones Linux installs, Windows or OSX, we might not have a freedesktop
    // icons thus - we use our bundled icons.
    if (needsIcons) {
        auto base = QDir(QCoreApplication::applicationDirPath() + iconsPath).absolutePath();
        // clang-format off
        auto paths = QIcon::fallbackSearchPaths()
                     << base + "/breeze/actions/16"
                     << base + "/breeze/actions/22"
                     << base + "/breeze/actions/32";
        // clang-format on
        QIcon::setFallbackSearchPaths(paths);
        QIcon::setFallbackThemeName("Breeze");
        qDebug() << "No icons found, using our own. Icons search path" << paths;
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(app.tr("files"), app.tr("Files to open."), "[files...]");
    parser.process(app);

    PluginManager pluginManager;
    auto filePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    auto iniFilePath = filePath + QString("/%1.ini").arg(CODEPOINTER_APP_NAME);
    auto windowIcon = QIcon(CODEPOINTER_ICON);
    auto helpPlugin = new HelpPlugin;
    auto textEditorPlugin = new TextEditorPlugin;
    auto split = new SplitTabsPlugin(textEditorPlugin);

    pluginManager.setWindowTitle(QCoreApplication::applicationName());
    pluginManager.setWindowIcon(windowIcon);
    pluginManager.setFileSettingsManager(iniFilePath);

    pluginManager.addPlugin(split);
    pluginManager.addPlugin(textEditorPlugin);
    pluginManager.addPlugin(helpPlugin);
    pluginManager.addPlugin(new CodeFormatPlugin);
    pluginManager.addPlugin(new CTagsPlugin);
    pluginManager.addPlugin(new LspPlugin);
    pluginManager.addPlugin(new FileSystemBrowserPlugin);
    pluginManager.addPlugin(new ProjectManagerPlugin);
    pluginManager.addPlugin(new TerminalPlugin);
    pluginManager.addPlugin(new ImageViewrPlugin);
    pluginManager.addPlugin(new HexViewrPlugin);
    pluginManager.addPlugin(new GitPlugin);
    split->setLoadingFinished(false);

    // Those are defaults, restore will override them
    pluginManager.hidePanels(Qt::BottomDockWidgetArea);
    pluginManager.hidePanels(Qt::LeftDockWidgetArea);
    pluginManager.hidePanels(Qt::RightDockWidgetArea);
    pluginManager.actionHideGUI->setChecked(true);
    pluginManager.restoreSettings();
    pluginManager.openFiles(parser.positionalArguments());
    split->setLoadingFinished(true);
    pluginManager.updateGUI();

    if (pluginManager.visibleTabs() == 0) {
        helpPlugin->showWelcomeScreen();
        pluginManager.saveSettings();
    }

    pluginManager.show();
    return app.exec();
}
