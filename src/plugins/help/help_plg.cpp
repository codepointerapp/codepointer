/**
 * \file help_plg.cpp
 * \brief Implementation of the help system plugin
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * License LGPL
 * \see HelpPlugin
 */

#include <cstdlib>
#include <iostream>
#include <string>

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSimpleUpdater.h>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <CommandPaletteWidget/commandpalette.h>
#include <iplugin.h>
#include <pluginmanager.h>

#include "GlobalCommands.hpp"
#include "help_plg.h"
#include "iplugin.h"
#include "widgets/bannerwidget.h"
#include "widgets/qmdieditor.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
// Needed for saving the desktop file
#include <filesystem>
#include <fstream>
#endif

#if defined(__linux__)
#define TESTING_CHANNEL "linux-testing"
#elif defined(_WIN32)
#define TESTING_CHANNEL "windows-testing"
#elif defined(__APPLE__)
#define TESTING_CHANNEL "osx-testing"
#else
#error This platform is not supported yet, feel free to help! PRs are welcomed!
#endif

// #define DEBUG_UPDATES
// #define DISABLE_UPDATES

const QString WelcomContent = R"(
# Welcome to CodePointer

CodePointer - an IDE for Rust, Go, C++ and more. The application
will look like a normal text editor, but can

Some hints for starter:

 * Normal keyboard shortcuts you are used
   to should work (`control+f`, `control+o`, `control+s` and more).
 * To select tabs, press `alt+1` etc.
 * To select/hide/show hide sidebar, press `control+1` (this will open the file
   manager).
 * On the top left, you will find the application menu (shortcut is `alt+m`).
 * You can access the command palette which has all the available commands
   using `control+shift+p`.
 * You can press `alt+control+m` to get a conservative menus+toolbars UI.
 * You can split the editor horizontally by pressing
 * Program checks for updates, and will notify when a new version is available.
 * Some files can be re-viewed (JSON, XML, SVG), you can toggle the preview
   panel by pressing `control+.`
 * Embedded terminal is available by pressing `control+t`.

## Project management

You can also load projects, build and execute them:
 * If you edit a `CMakeLists.txt` or `meson.build` or `cargo.toml` you will be
   prompted to open this file as a project. A new sidebar will be opened with
   the project files.
 * You can also add an "existing project", by choosing a directory.
 * You can choose commands to execute for building, or other tasks relevant
   to this project (configure, build), and you can choose which target
   to run (`control+b` and `control-r`).
 * When building, errors are shown at the bottom.
 * You can execute script files (python, Perl, Bash, PowerShell, etc.), by
   pressing `control+shift+r`
)";

auto static createDesktopMenuItem(const std::string &programName, const std::string &version,
                                  const std::string &execPath, const std::string &svgIconContent)
    -> std::string {
#if !defined(_WIN32)
    const char *homeDir = std::getenv("HOME");
    if (!homeDir) {
        std::cerr << "Unable to get HOME directory" << std::endl;
        return {};
    }

    std::filesystem::path homePath(homeDir);
    std::filesystem::path iconFile = homePath / ".local/share/icons" / (programName + ".svg");
    std::filesystem::path desktopFile =
        homePath / ".local/share/applications" / (programName + ".desktop");
    try {
        std::filesystem::create_directories(iconFile.parent_path());
        std::filesystem::create_directories(desktopFile.parent_path());
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error creating directories: " << e.what() << std::endl;
        return {};
    }

    std::ofstream iconStream(iconFile);
    if (!iconStream.is_open()) {
        std::cerr << "Unable to create icon file: " << iconFile << std::endl;
        return {};
    }
    iconStream << svgIconContent;
    iconStream.close();

    std::ofstream file(desktopFile);
    if (!file.is_open()) {
        std::cerr << "Unable to create desktop file: " << desktopFile << std::endl;
        return {};
    }

    file << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Name=" << programName << "\n"
         << "Comment=" << programName << " Text Editor version v" << version << "\n"
         << "Exec=" << execPath << "\n"
         << "Icon=" << iconFile.string() << "\n"
         << "Categories=Utility;TextEditor;\n"
         << "Terminal=false\n";
    file.close();
    return desktopFile.string();
#else
    // all this does not make sense on Windows
    Q_UNUSED(programName)
    Q_UNUSED(version)
    Q_UNUSED(execPath)
    Q_UNUSED(svgIconContent)
    return {};
#endif
}

auto static getExecutablePath() -> std::string {
    std::string path;

#if defined(__linux__)
    const char *appImagePath = std::getenv("APPIMAGE");
    if (appImagePath) {
        return std::string(appImagePath);
    }

    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        path = std::string(result, (count > 0) ? count : 0);
    }
    if (path.empty()) {
        path = std::filesystem::absolute(std::filesystem::path(program_invocation_name)).string();
    }
#elif defined(_WIN32)
    char result[MAX_PATH];
    GetModuleFileNameA(NULL, result, MAX_PATH);
    path = std::string(result);
#endif

    return path;
}

auto static canInstallDesktopFile() -> bool {
#if defined(__linux__)
    // supported only on linux
    if (std::getenv("FLATPAK_ID") != nullptr) {
        return false;
    }

    auto exePath = getExecutablePath();
    auto path = std::filesystem::path(exePath);
    auto restrictedDirs = {"/usr/bin", "/opt", "/usr/local"};
    for (const auto &dir : restrictedDirs) {
        if (std::filesystem::path(exePath).string().find(dir) == 0) {
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

auto static refreshSystemMenus() -> void {
#if defined(__linux__)
    std::string desktopEnv =
        std::getenv("XDG_CURRENT_DESKTOP") ? std::getenv("XDG_CURRENT_DESKTOP") : "";

    if (desktopEnv.find("GNOME") != std::string::npos) {
        // std::system("killall gnome-panel || true");
        std::system("xdotool key F5");
    } else if (desktopEnv.find("KDE") != std::string::npos) {
        std::system("kbuildsycoca5");
        std::system("kbuildsycoca6");
    } else if (desktopEnv.find("XFCE") != std::string::npos) {
        std::system("xfce4-panel -r");
    } else {
        // Generic approach for other environments
        std::system("update-menus");
    }
#endif
}

auto updatesUrl =
    "https://raw.githubusercontent.com/codepointerapp/codepointer/refs/heads/main/updates.json";

HelpPlugin::HelpPlugin() {
    name = tr("Help system browser");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
}

HelpPlugin::~HelpPlugin() {}

void HelpPlugin::on_client_merged(qmdiHost *host) {
#ifndef DISABLE_UPDATES
    auto updateChannelStrings = QStringList() << tr("Do not check for updates")
                                              << tr("Check for updates every time program starts")
                                              << tr("Check for updates once per day")
                                              << tr("Check for updates once per week");
    auto stableChannelStrings = QStringList() << tr("Stable channel (recommended)")
                                              << tr("Testing channel (pre-releases)");

    config.pluginName = tr("Global config");
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Check for updates"))
            .setDescription(tr("When to check for updates for this program"))
            .setKey(Config::UpdatesChecksKey)
            .setType(qmdiConfigItem::OneOf)
            .setPossibleValue(updateChannelStrings)
            .setDefaultValue(UpdateCheck::Daily)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Update channel"))
            .setDescription(tr("Keep at stable, unless you want to report bugs"))
            .setKey(Config::UpdatesChannelKey)
            .setType(qmdiConfigItem::OneOf)
            .setPossibleValue(stableChannelStrings)
            .setDefaultValue(UpdateChannels::Stable)
            .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::LastUpdateTimeKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("0")
                                     .setUserEditable(false)
                                     .build());
    auto actionCheckForUpdates = new QAction(tr("&Check for updates"), this);
    connect(actionCheckForUpdates, &QAction::triggered, this,
            &HelpPlugin::checkForUpdates_triggered);
#endif

    // We like this shortcut, even on non OSX computers
    getManager()->actionConfig->setShortcut(QKeySequence("Ctrl+,"));

    auto actionAbout = new QAction(tr("&About"), this);
    actionAbout->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::HelpAbout));
    connect(actionAbout, &QAction::triggered, this, &HelpPlugin::actionAbout_triggered);

    auto actionVisitHomePage = new QAction(tr("Visit homepage"), this);
    connect(actionVisitHomePage, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://gitlab.com/codepointer/codepointer/"));
    });
    auto actionAboutQt = new QAction(tr("About Qt"), this);
    connect(actionAboutQt, &QAction::triggered, this, []() { QApplication::aboutQt(); });

    if (canInstallDesktopFile()) {
        auto installDesktopFile = new QAction(tr("Install desktop file"), this);
        connect(installDesktopFile, &QAction::triggered, this, [this]() {
            auto svgResourcePath = CODEPOINTER_SVG_ICON;
            auto exe = getExecutablePath();
            auto svgFile = QFile(QString::fromStdString(svgResourcePath));
            if (!svgFile.open(QIODevice::ReadOnly)) {
                std::cerr << "Unable to open SVG resource: " << svgResourcePath << std::endl;
                return;
            }
            auto svgContent = svgFile.readAll().toStdString();
            svgFile.close();
            auto desktopFile = createDesktopMenuItem(
                QApplication::applicationName().toStdString(),
                QApplication::applicationVersion().toStdString(), exe, svgContent);
            if (!desktopFile.empty()) {
                this->getManager()->openFile(QString::fromStdString(desktopFile));
            }
            refreshSystemMenus();
        });
        menus["&Help"]->addAction(installDesktopFile);
    }
    auto searchAction = new QAction(tr("Search action in UI"), this);
    searchAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_P));
    connect(searchAction, &QAction::triggered, this, [this]() {
        auto model = new ActionListModel(this);
        auto window = getManager();
        model->setActions(collectWidgetActions(window));
        auto commandPalette = new CommandPalette(window);
        commandPalette->setFilterModes(CommandPalette::FilterMode::RemoveAccelerators);
        commandPalette->setDataModel(model);
        commandPalette->setItemDelegate(new ActionDelegate(commandPalette));
        connect(commandPalette, &CommandPalette::didChooseItem, this,
                [commandPalette](const QModelIndex &index, const QAbstractItemModel *model) {
                    auto data = model->data(index, Qt::UserRole);
                    auto action = data.value<QAction *>();
                    if (action) {
                        action->trigger();
                    }
                    commandPalette->deleteLater();
                });
        commandPalette->show();
    });

    auto showWelcomeScreenAction = new QAction(tr("Show welcome screen"), this);
    connect(showWelcomeScreenAction, &QAction::triggered, this, &HelpPlugin::showWelcomeScreen);

#ifndef DISABLE_UPDATES
    menus["&Help"]->addAction(actionCheckForUpdates);
#endif
#if defined(DEBUG_UPDATES)
    auto debugChecks = new QAction("Debug check for updates", this);
    connect(debugChecks, &QAction::triggered, this, [this]() { doStartupChecksForUpdate(); });
    menus["&Help"]->addAction(debugChecks);
#endif
    menus["&Help"]->addAction(searchAction);
    menus["&Help"]->addSeparator();
    menus["&Help"]->addAction(showWelcomeScreenAction);
    menus["&Help"]->addAction(actionVisitHomePage);
    menus["&Help"]->addAction(actionAboutQt);
    menus["&Help"]->addAction(actionAbout);

    auto w = dynamic_cast<QWidget *>(host);
    w->installEventFilter(this);
}

void HelpPlugin::on_client_unmerged(qmdiHost *host) {
    IPlugin::on_client_unmerged(host);
    auto w = dynamic_cast<QWidget *>(host);
    w->removeEventFilter(this);
}

void HelpPlugin::showAbout() {
    QMessageBox::information(dynamic_cast<QMainWindow *>(mdiServer), "About",
                             "A file system browser plugin");
}

void HelpPlugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);
    doStartupChecksForUpdate(false);
}

void HelpPlugin::doStartupChecksForUpdate(bool notifyUserNoUpdates) {
#ifndef DISABLE_UPDATES
    QString lastCheckStr = getConfig().getLastUpdateTime();
    auto updatesCheck = getConfig().getUpdatesChecks();
    auto lastCheck = lastCheckStr.toULongLong();
    auto currentTime = QDateTime::currentSecsSinceEpoch();
    auto timeDiff = 0UL;

    switch (updatesCheck) {
    case UpdateCheck::NoChecks:
#if defined(DEBUG_UPDATES)
        qDebug() << ">No check at all";
#endif
        timeDiff = INT32_MAX;
        break;
    case UpdateCheck::EveryTime:
#if defined(DEBUG_UPDATES)
        qDebug() << ">Checks when app starts";
#endif
        break;
    case UpdateCheck::Daily:
#if defined(DEBUG_UPDATES)
        qDebug() << ">Checks Daily";
#endif
        timeDiff = 60 * 60 * 24;
        break;
    case UpdateCheck::Weekly:
#if defined(DEBUG_UPDATES)
        qDebug() << ">Checks wheekly";
#endif
        timeDiff = 60 * 60 * 24 * 7;
        break;
    }

#if defined(DEBUG_UPDATES)
    qDebug() << ">Current time = " << currentTime;
    qDebug() << ">Last check = " << lastCheck;
    qDebug() << ">Delta = " << currentTime - lastCheck << " < " << timeDiff;
#endif
    if (currentTime - lastCheck > timeDiff) {
        doChecksForUpdate(notifyUserNoUpdates);
        getConfig().setLastUpdateTime(QString::number(currentTime));
#if defined(DEBUG_UPDATES)
    } else {
        qDebug() << " - No need to check for updates";
#endif
    }
#else
    Q_UNUSED(notifyUserNoUpdates)
#endif
}

void HelpPlugin::doChecksForUpdate(bool notifyUserNoUpdates) {
#ifndef DISABLE_UPDATES
    switch (getConfig().getUpdatesChannel()) {
    case UpdateChannels::Stable:
#if defined(DEBUG_UPDATES)
        qDebug() << "Checking updates from stable channel";
#endif
        break;
    case UpdateChannels::Testing:
#if defined(DEBUG_UPDATES)
        qDebug() << "Checking updates from testing channel";
#endif
        QSimpleUpdater::getInstance()->setPlatformKey(updatesUrl, TESTING_CHANNEL);
        break;
    }
    QSimpleUpdater::getInstance()->setNotifyOnUpdate(updatesUrl, true);
    QSimpleUpdater::getInstance()->setNotifyOnFinish(updatesUrl, notifyUserNoUpdates);
    QSimpleUpdater::getInstance()->setDownloaderEnabled(updatesUrl, true);
    QSimpleUpdater::getInstance()->checkForUpdates(updatesUrl);

    auto currentTime = QDateTime::currentSecsSinceEpoch();
    getConfig().setLastUpdateTime(QString::number(currentTime));
#else
    Q_UNUSED(notifyUserNoUpdates)
#endif
}

void HelpPlugin::uiCleanUp() {
    /*
    Eventually - kill of the running task
    if (isTaskRunnning()) {
        return;
    }
    */

    auto manager = getManager();
    auto window = dynamic_cast<QMainWindow *>(mdiServer->mdiHost);
    if (isBottomPanelsVisible()) {
        for (auto dock : window->findChildren<QDockWidget *>()) {
            if (window->dockWidgetArea(dock) == Qt::BottomDockWidgetArea && !dock->isFloating()) {
                dock->hide();
            }
        }
        return;
    }

    auto e = dynamic_cast<qmdiEditor *>(manager->currentClient());
    if (e) {
        if (e->isPreviewVisible()) {
            e->setPreviewVisible(false);
            return;
        }
    }

    auto w = dynamic_cast<QWidget *>(manager->currentClient());
    if (w) {
        w->setFocus();
    }
}

bool HelpPlugin::isTaskRunnning() const {
    // TODO: how do we find the project manager from this context?
    return false;
}

bool HelpPlugin::isBottomPanelsVisible() const {
    auto window = dynamic_cast<QMainWindow *>(mdiServer->mdiHost);
    for (auto dock : window->findChildren<QDockWidget *>()) {
        if (window->dockWidgetArea(dock) == Qt::BottomDockWidgetArea && !dock->isFloating() &&
            dock->isVisible()) {
            return true;
        }
    }
    return false;
}

void HelpPlugin::showWelcomeScreen() {
    auto manager = getManager();
    CommandArgs args = {
        {GlobalArguments::FileName, "welcome.md"},
        {GlobalArguments::Content, WelcomContent},
    };
    manager->handleCommandAsync(GlobalCommands::DisplayText, args);
}

static const char flatpackSVG[] = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="1.18 0 21.65 24">
    <path fill="#4a90d9" d="M12 0c-.556 0-1.111.144-1.61.432l-7.603 4.39a3.22 3.22 0 0 0-1.61 2.788v8.78c0 1.151.612 2.212 1.61 2.788l7.603 4.39a3.22 3.22 0 0 0 3.22 0l7.603-4.39a3.22 3.22 0 0 0 1.61-2.788V7.61a3.22 3.22 0 0 0-1.61-2.788L13.61.432A3.2 3.2 0 0 0 12 0m0 2.358c.15 0 .299.039.431.115l7.604 4.39c.132.077.24.187.315.316L12 12v9.642a.86.86 0 0 1-.431-.116l-7.604-4.39a.87.87 0 0 1-.431-.746V7.61c0-.153.041-.302.116-.43L12 12Z"/>
</svg>
)svg";

static const char *default_wayland_icon_xpm[] = {
    "32 32 69 2", "   c None", ".. c #ffff40", ".: c #fff820", ".- c #ffe030", ".= c #ffe038",
    ".+ c #ffff38", ".* c #ffff50", ".# c #ffff68", ".% c #ffd038", ".@ c #ffc808", ".o c #ffd018",
    ".O c #ffd020", ".X c #ffc008", ".0 c #ffc000", ".1 c #ffc800", ".2 c #ffc010", ".3 c #ffff60",
    ".4 c #ffc838", ".5 c #ffc828", ".6 c #ffd040", ".7 c #ffc020", ".8 c #ffb800", ".9 c #ffc810",
    ".a c #fff838", ".b c #ffc018", ".c c #ffc818", ".d c #ffc028", ".e c #ffff88", ".f c #ffc820",
    ".g c #ffd010", ".h c #ffff20", ".i c #ffe048", ".j c #ffc848", ".k c #ffffc8", ".l c #ffe830",
    ".m c #fff018", ".n c #ffd838",

    // Removed duplicate:
    // ".o c #ffe820",

    ".p c #ffd028", ".q c #ffd048", ".r c #ffe828", ".s c #fff030", ".t c #ffff90", ".u c #fff828",
    ".v c #ffd008", ".w c #ffd818", ".x c #ffff48", ".y c #fff840", ".z c #ffd840", ".A c #ffe018",
    ".B c #ffff58", ".C c #ffd828", ".D c #fff038", ".E c #ffffe8", ".F c #ffff30", ".G c #ffd830",
    ".H c #ffffd8", ".I c #ffe028", ".J c #ffffe0", ".K c #ffff80", ".L c #fff830", ".M c #ffff70",
    ".N c #ffd000", ".O c #fff040", ".P c #ffd030", ".Q c #fff028", ".R c #ffff98", ".S c #ffffa0",
    ".T c #fff020", ".U c #ffe020",

    "                                                                ",
    "                          ...:.-.=.+.*                          ",
    "                    .#.%.@.o.O.X.0.1.1.@.2.3                    ",
    "                ...%.%.4.0.5.6.7.0.0.0.0.8.@.9.a                ",
    "                .@.0.0.8.0.8.8.8.8.b.7.8.0.0.0.0.c              ",
    "                .3.1.0.0.0.8.8.2.d.2.2.0.0.0.0.0.1              ",
    "        .3.e      .f.0.8.X.4.2.b.7.8.8.0.0.0.0.0.@              ",
    "        .g.h      .i.0.8.d.j.X.8.8.0.7.0.0.X.X.0.o    .k        ",
    "      .l.1.m        .n.0.8.8.0.0.0.0.2.0.0.X.X.1..    .c.o      ",
    "      .@.8.c        .p.0.0.8.0.2.X.0.8.X.q.8.0.@      .X.@      ",
    "    .r.0.0.2        .f.0.0.0.s  .e.@.0.0.0.0.2.o    .t.1.0.u    ",
    "    .9.0.8.2        .-.0.0.v      .w.f.b.8.0.1.x    ...1.0.9    ",
    "    .1.0.0.9        .y.0.0.9      .3.g.X.b.z.y      .A.0.0.1    ",
    "  ...1.0.0.o        .x.1.0.B        .@.0.0.0.C      .9.0.0.1..  ",
    "  .D.0.0.8.g.E        .1.X          .-.0.0.1.B    .e.1.0.0.1.F  ",
    "  .G.0.X.4.@.H        .v.I    .a.x    .1.0.X      .h.1.0.0.0.l  ",
    "  .G.0.b.%.1.J        .5.K    .g.+    .C.0.f      .L.0.0.0.0.l  ",
    "  .l.0.0.8.1.K              .M.1.o    .K.N.+      .O.0.0.0.1.F  ",
    "  ...1.0.0.1.F              .O.0.1      .C        .v.0.0.0.1.x  ",
    "    .1.0.0.0.I              .c.X.@.*              .I.0.8.0.1    ",
    "    .9.0.0.0.2              .1.4.c.F              .9.0.0.8.2    ",
    "    .o.0.0.0.p            .J.P.0.0.p            .K.@.0.0.1.Q    ",
    "      .v.0.0.O.3          .-.b.2.2.@            .*.0.0.0.@      ",
    "      .I.0.0.0.C          .X.8.X.X.1.3        .R.N.0.0.0.l      ",
    "        .@.0.0.@          .9.0.0.0.0.g        .g.0.0.0.9        ",
    "          .v.0.0.u      .S.1.0.0.0.0.@      .x.1.0.0.@          ",
    "          .M.v.0.0.L    .F.1.0.0.0.0.1.t    .@.0.0.g.R          ",
    "              .9.0.0.9.x.X.0.0.0.0.0.0.T  .U.1.0.9              ",
    "                .u.@.0.0.0.0.0.8.8.0.0.0.2.1.@.a                ",
    "                    .s.9.1.1.0.0.0.0.1.1.9.L                    ",
    "                          .x.a.=.=.a.x                          ",
    "                                                                "};

static const char *default_x11_icon_xpm[] = {
    "32 32 79 2", "   c None", ".. c #303030", ".: c #404040", ".- c #606060", ".= c #707070",
    ".+ c #202020", ".* c #808080", ".# c #b0b0b0", ".% c #404030", ".@ c #505050", ".o c #202030",
    ".O c #506060", ".X c #f0b090", ".0 c #ffa070", ".1 c #ffe0b0", ".2 c #fff0c0", ".3 c #ffffff",
    ".4 c #ffe0c0", ".5 c #605050", ".6 c #706060", ".7 c #ffd0a0", ".8 c #908080", ".9 c #ffffe0",
    ".a c #fff0d0", ".b c #ff8040", ".c c #ff7030", ".d c #a09080", ".e c #ffffd0", ".f c #ffa080",
    ".g c #ff6020", ".h c #f06020", ".i c #e0c0b0", ".j c #ffe0a0", ".k c #f05010", ".l c #f06030",
    ".m c #101010", ".n c #ffe090",

    // Removed duplicate:
    // ".o c #f05020",

    ".p c #304040", ".q c #f06040", ".r c #000000", ".s c #ffd080", ".t c #ff9040", ".u c #fff080",
    ".v c #ffff90", ".w c #ff9050", ".x c #e05010", ".y c #f07040", ".z c #ffd070", ".A c #ff6030",
    ".B c #909090", ".C c #ffffc0", ".D c #ffd060", ".E c #ff7040", ".F c #ffe070", ".G c #f08040",
    ".H c #102020", ".I c #904020", ".J c #e07030", ".K c #100000", ".L c #403010", ".M c #a07030",
    ".N c #ffb040", ".O c #e0a040", ".P c #c06030", ".Q c #f07030", ".R c #ff8030", ".S c #ffa060",
    ".T c #ffb070", ".U c #ffb080", ".V c #ffc090", ".W c #ffd090", ".Y c #ffc080", ".Z c #f0a040",
    ":. c #ffa040", ":: c #d09030", ":- c #604020", ":= c #707080", ":+ c #f09040", ":* c #302020",

    "                                                                ",
    "                                                                ",
    "                                                                ",
    "                                                                ",
    "                                                                ",
    "            .....:.:.-                          .=..            ",
    "              .+...:.:.*                      .#..              ",
    "              .......:.:.#                    .:                ",
    "                .....%.%.@                  .@..                ",
    "                  .o...%...O.X.0.1  .2.2.3.-.:                  ",
    "                .4.5.+.......6.7        .8.+  .9                ",
    "            .a.b.c  .+.+.....o.d        ..        .e            ",
    "          .f.g.h      .+.+.+.+...i    .:..          .j          ",
    "        .0.k.l          .+.+.+.m.:  .:.+              .n        ",
    "        .o.h            .+.m.m.m.p.-.m                  .e      ",
    "      .q.k.l              .m.r.m.*.m.@                  .s      ",
    "      .h.o.t              .@.r.*.+.r.+                  .u      ",
    "      .o.o                .+.@...r.r.r.:                .v      ",
    "      .o.o.w            ...m  .m.r.r.r.r.=              .u      ",
    "      .h.x.y          .@.m      .m.r.r.r.m              .z      ",
    "      .A.o.h.1      .B.m          .r.r.r.r..          .C.D      ",
    "        .g.k.E      .+            .+.r.r.r.r.@        .F        ",
    "          .g.k.G.a.H.r              .m.r.r.r.m      .F          ",
    "            .l.h.h.I                  .r.r.r.r.+.j.D            ",
    "              .J.h.c.w.7              .O.K.L.M.N.O              ",
    "              .m.P.t.Q.R.t.S.T.U.V.W.Y.T.Z:.:::-.r:=            ",
    "            ...m          .t.t:+:+:..N    :*.r.r.r.+            ",
    "                                                                ",
    "                                                                ",
    "                                                                ",
    "                                                                "};

void HelpPlugin::actionAbout_triggered() {
    auto appName = QCoreApplication::applicationName();
    auto version = QCoreApplication::applicationVersion();
    auto aboutText = tr(R"(
<h2>%1 %2</h2>
<p>A versatile text editor</p>
<p>Home page: <a href="%3">%3</a></p>
<p>Mirror: <a href="%4">%4</a></p>
<p>Licensed under the GNU General Public License v2 (GPLv2) or later</p>
<p>This project uses <a href="https://www.qt.io/">Qt6</a>, and the following libraries:</p>
<ul>
    <li><a href="https://github.com/diegoiast/qmdilib">qmdilib</a></li>
    <li><a href="https://github.com/diegoiast/qutepart-cpp">qutepart-cpp</a></li>
    <li><a href="https://github.com/diegoiast/command-palette-widget">command-palette-widget</a></li>
    <li><a href="https://github.com/diegoiast/KodoTerm">KodoTerm</a></li>
    <li><a href="https://github.com/palacaze/image-viewer">image-viewer</a></li>
    <li><a href="https://github.com/Dax89/QHexView">QHexView</a></li>
    <li><a href="https://github.com/alex-spataru/QSimpleUpdater">QSimpleUpdater</a></li>
</ul>

<p>Copyright © 2024-2026 <a href="mailto:diegoiast@gmail.com">Diego Iastrubni</a></p>
)");

    QDialog aboutDialog(getManager());
    aboutDialog.setWindowTitle(tr("About %1").arg(appName));
    aboutDialog.setMinimumSize(400, 300);

    auto mainLayout = new QVBoxLayout(&aboutDialog);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto banner = new BannerWidget(QString("%1 %2").arg(appName, version));
    banner->setFixedHeight(100);
    mainLayout->addWidget(banner);

    auto contentWidget = new QWidget;
    auto contentLayout = new QVBoxLayout(contentWidget);
    auto textLabel =
        new QLabel(aboutText.arg(appName, version, "https://github.com/codepointerapp/codepointer",
                                 "https://gitlab.com/codepointer/codepointer"));
    textLabel->setWordWrap(true);
    textLabel->setOpenExternalLinks(true);
    textLabel->setTextFormat(Qt::RichText);

    auto textLayout = new QHBoxLayout;
    textLayout->addWidget(textLabel);

    // Flatpak is still SVG, so handle it separately.
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        auto flatpakIcon = new QLabel;
        auto renderer =
            new QSvgRenderer(QByteArray(flatpackSVG, sizeof(flatpackSVG) - 1), flatpakIcon);
        auto pixmap = QPixmap(32, 32);
        auto painter = QPainter(&pixmap);

        flatpakIcon->setFixedSize(32, 32);
        flatpakIcon->setToolTip(tr("Running as a Flatpak"));
        flatpakIcon->setAlignment(Qt::AlignCenter);
        pixmap.fill(Qt::transparent);
        renderer->render(&painter);
        flatpakIcon->setPixmap(pixmap);
        textLayout->addWidget(flatpakIcon, 0, Qt::AlignTop);
    }

    auto addPlatformIcon = [&](const char *xpm[], const QString &tooltip) {
        auto icon = new QLabel;
        icon->setFixedSize(32, 32);
        icon->setToolTip(tooltip);
        icon->setAlignment(Qt::AlignCenter);

        auto pixmap = QPixmap(xpm);
        icon->setPixmap(pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        textLayout->addWidget(icon, 0, Qt::AlignTop);
    };

    auto platform = QGuiApplication::platformName();
    if (platform == "wayland") {
        qDebug() << "Platform is wayland!";
        addPlatformIcon(default_wayland_icon_xpm, tr("Running on Wayland"));
    }
    if (platform == "xcb") {
        qDebug() << "Platform is xcb/x11!";
        addPlatformIcon(default_x11_icon_xpm, tr("Running on X11"));
    }

    contentLayout->addLayout(textLayout);
    mainLayout->addWidget(contentWidget);

    auto closeButton = new QPushButton(tr("Close"));
    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();
    connect(closeButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    contentLayout->addLayout(buttonLayout);
    aboutDialog.setLayout(mainLayout);
    aboutDialog.adjustSize();
    aboutDialog.exec();
}

void HelpPlugin::checkForUpdates_triggered() { doChecksForUpdate(true); }

bool HelpPlugin::eventFilter(QObject *obj, QEvent *event) {
    auto handled = QObject::eventFilter(obj, event);

    if (!handled && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            uiCleanUp();
            return true;
        }
    }
    return handled;
}
