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
    contentLayout->addWidget(textLabel);
    mainLayout->addWidget(contentWidget);

    auto closeButton = new QPushButton(tr("Close"));
    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();

    connect(closeButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    contentLayout->addLayout(buttonLayout);
    aboutDialog.setLayout(mainLayout);
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
