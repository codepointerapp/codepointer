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

#include "pluginmanager.h"
#include "plugins/CTags/CTagsPlugin.hpp"
#include "plugins/CodeFormat/CodeFormat.hpp"
#include "plugins/ProjectManager/ProjectManagerPlg.h"
#include "plugins/SplitTabsPlugin/SplitTabsPlugin.hpp"
#include "plugins/Terminal/TerminalPlugin.hpp"
#include "plugins/TreeSitter/TreeSitterPlugin.hpp"
#include "plugins/filesystem/filesystembrowser.h"
#include "plugins/git/GitPlugin.hpp"
#include "plugins/help/help_plg.h"
#include "plugins/hexviewer/hexviewer_plg.h"
#include "plugins/imageviewer/imageviewer_plg.h"
#include "plugins/texteditor/texteditor_plg.h"

class ThemeProxyStyle : public QProxyStyle {
  public:
    using QProxyStyle::QProxyStyle;

    void polish(QPalette &pal) override {
        QProxyStyle::polish(pal);
#if defined(BUILD_DEV)
        auto tintBackgroundColor = QColor::fromRgb(0xFFC107); // Yellow
#elif defined(BUILD_OFFICIAL)
        auto tintBackgroundColor = QColor::fromRgb(0x44aa44); // Green
#else
        auto tintBackgroundColor = QColor::fromRgb(0x6ba8ff); // Blue
#endif
        pal.setColor(QPalette::Highlight, tintBackgroundColor);
        pal.setColor(QPalette::HighlightedText, Qt::white);
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override {
        if (element == CE_DockWidgetTitle) {
            if (const auto *v6 = qstyleoption_cast<const QStyleOptionDockWidget *>(option)) {
                auto rect = v6->rect;
                auto highlight = v6->palette.color(QPalette::Highlight);
                auto lighter = highlight.lighter(150);

                painter->save();
                if (v6->state & State_Active) {
                    auto grad = QLinearGradient(rect.topLeft(), rect.topRight());
                    grad.setColorAt(0, highlight);
                    grad.setColorAt(1, lighter);
                    painter->fillRect(rect, grad);
                } else {
                    painter->fillRect(rect, highlight);
                }

                auto titleRect = rect.adjusted(5, 0, -5, 0);
                painter->setPen(Qt::white);
                auto font = painter->font();
                font.setBold(true);
                painter->setFont(font);
                painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, v6->title);
                painter->restore();
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

int main(int argc, char *argv[]) {
    Q_INIT_RESOURCE(qutepart_syntax_files);
    Q_INIT_RESOURCE(qutepart_theme_data);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(CODEPOINTER_APP_NAME);
    QCoreApplication::setApplicationVersion("0.1.6");

#if defined(WIN32)
    // default style on windows is ugly and unusable.
    // lets fallback to something more usable for us
    app.setStyle(new ThemeProxyStyle("windowsvista"));
    auto needsIcons = true;
    auto iconsPath = "/share/icons";
#else
    app.setStyle(new ThemeProxyStyle(app.style()->objectName()));
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
    pluginManager.addPlugin(new TreeSitterPlugin);
    pluginManager.addPlugin(new CTagsPlugin);
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
