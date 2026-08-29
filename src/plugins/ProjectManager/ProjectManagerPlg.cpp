/**
 * \file ProjecManagerPlg.cpp
 * \brief Implementation of the project mana
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QByteArray>
#include <QClipboard>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeType>
#include <QScrollBar>
#include <QSettings>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStringList>
#include <QStringListModel>
#include <QTimer>
#include <QWidgetAction>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(WIN32)
#include <io.h>
#define read _read
#endif

#include <CommandPaletteWidget/CommandPalette>
#include <qmdiclient.h>
#include <qmdihost.h>
#include <qmdipluginconfig.h>
#include <qmdiserver.h>
#include <qmditabwidget.h>

#include "AnsiToHTML.hpp"
#include "GlobalCommands.hpp"
#include "ProjectBuildConfig.h"
#include "ProjectIssuesWidget.h"
#include "ProjectManagerPlg.h"
#include "ProjectSearch.h"
#include "kitdefinitionmodel.h"
#include "kitdetector.h"
#include "pluginmanager.h"
#include "plugins/filesystem/filesystemwidget.h"
#include "ui_BuildRunOutput.h"
#include "ui_ProjectManagerGUI.h"
#include "widgets/SearchableMenuButton.hpp"
#include "widgets/qmdieditor.h"

#define USE_TTY_FOR_TASKS

static auto str(QProcess::ExitStatus e) -> QString {
    switch (e) {
    case QProcess::ExitStatus::NormalExit:
        return "Normal exit";
    case QProcess::ExitStatus::CrashExit:
        return "Crashed";
    }
    return "";
}

static auto findExecForPlatform(QHash<QString, QString> files) -> QString {
    if (!files.contains(PLATFORM_CURRENT)) {
        qDebug("Warning - findExecForPlatform: unsupported platform, cannot find executable");
    }
    return files[PLATFORM_CURRENT];
}

static auto regenerateKits(const std::filesystem::path &directoryPath) -> void {
    KitDetector::deleteOldKitFiles(directoryPath);

    auto tools = KitDetector::findCompilerTools(KitDetector::platformUnix);
    auto compilersFound = KitDetector::findCompilers(KitDetector::platformUnix);
    auto qtVersionsFound = std::vector<KitDetector::ExtraPath>();
    KitDetector::findQtVersions(KitDetector::platformUnix, qtVersionsFound, compilersFound);
    KitDetector::generateKitFiles(directoryPath, tools, compilersFound, qtVersionsFound,
                                  KitDetector::platformUnix);
}

static auto getCommandInterpreter(const QString &externalCommand)
    -> std::tuple<QString, QStringList> {
    QString interpreter;
    QStringList command;

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    interpreter = "/bin/sh";
    command << "-c" << externalCommand;
#elif defined(_WIN32)
    interpreter = qgetenv("COMSPEC");
    command << "/k" << externalCommand;
#else
    interpreter = "???"; // Default fallback
#endif

    return {interpreter, command};
}

[[maybe_unused]]
auto static setupPty(QProcess &process, int &masterFd) -> bool {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) {
        return false;
    }

    if (grantpt(masterFd) < 0) {
        close(masterFd);
        return false;
    }

    if (unlockpt(masterFd) < 0) {
        close(masterFd);
        return false;
    }

    char slaveName[512];
    if (ptsname_r(masterFd, slaveName, sizeof(slaveName)) != 0) {
        close(masterFd);
        return false;
    }

    int slaveFd = open(slaveName, O_RDWR | O_NOCTTY);
    if (slaveFd < 0) {
        close(masterFd);
        return false;
    }

    process.setStandardInputFile(QString::fromUtf8(slaveName));
    process.setStandardOutputFile(QString::fromUtf8(slaveName));
    process.setProcessChannelMode(QProcess::MergedChannels);

    close(slaveFd);

    // NOTE: ownership of masterFd passes to the caller, which must close it once
    // the process is done. Registering the cleanup here used to capture `masterFd`
    // by reference - a reference to a local of runCommand() that is long dead by
    // the time QProcess::finished fires - and the connection was never
    // disconnected, so every task run added another lambda closing a garbage fd.
    return true;
#else
    Q_UNUSED(process);
    Q_UNUSED(masterFd);
    return false;
#endif
}

void ProjectBuildModel::addConfig(std::shared_ptr<ProjectBuildConfig> config) {
    auto row = configs.size();
    beginInsertRows(QModelIndex(), row, row);
    configs.push_back(config);
    endInsertRows();
}

void ProjectBuildModel::removeConfig(size_t index) {
    if (index >= configs.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    configs.erase(configs.begin() + index);
    endRemoveRows();
}

std::shared_ptr<ProjectBuildConfig> ProjectBuildModel::getConfig(size_t index) const {
    return this->configs.at(index);
}

int ProjectBuildModel::findConfigDirIndex(const QString &dir) {
    auto i = 0;
    for (const auto &v : configs) {
        if (v->sourceDir == dir) {
            return i;
        }
        i++;
    }
    return -1;
}

std::shared_ptr<ProjectBuildConfig> ProjectBuildModel::findConfigDir(const QString &dir) {
    for (const auto &v : configs) {
        if (v->sourceDir == dir) {
            return v;
        }
    }
    return {};
}

std::shared_ptr<ProjectBuildConfig> ProjectBuildModel::findConfigFile(const QString &fileName) {
    for (const auto &v : configs) {
        if (v->fileName == fileName) {
            return v;
        }
    }
    return {};
}

std::shared_ptr<ProjectBuildConfig> ProjectBuildModel::findProjectForFile(const QString &fileName) {
    for (const auto &c : configs) {
        if (fileName.startsWith(c->sourceDir)) {
            return c;
        }
    }
    return {};
}

int ProjectBuildModel::rowCount(const QModelIndex &) const { return configs.size(); }

QVariant ProjectBuildModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    auto config = configs[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return config->name;
    case Qt::ToolTipRole:
        return QDir::toNativeSeparators(config->sourceDir);
    default:
        break;
    }
    return {};
}

QStringList ProjectBuildModel::getAllOpenDirs() const {
    auto dirs = QStringList();
    for (const auto &v : configs) {
        dirs.append(v->sourceDir);
    }
    return dirs;
}

ProjectManagerPlugin::ProjectManagerPlugin() {
    name = "ProjectManager";
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = true;
    auto monospacedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto values = QStringList() << tr("Never") << tr("Always") << tr("Loaded projects");

    config.pluginName = tr("Project manager");
    config.description = tr("Add support for building using CMake/Cargo/Go");
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Save before running tasks (build, config etc)"))
            .setDescription(tr("If checked, files are saved before running any task"))
            .setKey(Config::SaveBeforeTaskKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(true)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Black console"))
            .setDescription(tr("Should the console background be black, or default"))
            .setKey(Config::BlackConsoleKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(false)
            .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Console font"))
                                     .setKey(Config::ConsoleFontKey)
                                     .setType(qmdiConfigItem::Font)
                                     .setDefaultValue(monospacedFont)
                                     .setValue(monospacedFont)
                                     .build());

    /*
        config.configItems.push_back(qmdiConfigItem::Builder()
                                         .setDisplayName(tr("Extra paths"))
                                         .setDescription(tr("Add new paths for compilers/tools"))
                                         .setKey(Config::ExtraPathKey)
                                         .setType(qmdiConfigItem::StringList)
                                         .setDefaultValue(QStringList())
                                         .build());
    */
    // project GUI
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::OpenDirsKey)
                                     .setType(qmdiConfigItem::StringList)
                                     .setDefaultValue(QStringList())
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SelectedDirectoryKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue(QString{})
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::FilterShowKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::FilterOutKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());

    // search GUI
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchPatternKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchIncludeKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchExcludeKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchPathKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchCollapseFileNamesKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchRegexKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchSensitiveKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SearchWholeWordsKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .setUserEditable(false)
                                     .build());
}

ProjectManagerPlugin::~ProjectManagerPlugin() {
    // runProcess is a member, so ~QProcess runs after this body and kills any task
    // still going - which emits finished() while the plugin (and, during
    // PluginManager teardown, other plugins) are already being destroyed. The
    // handler calls back into the command bus, so it must not be reachable any
    // more. Disconnect first, then reap.
    runProcess.disconnect();
    stopRunningTask();
    releaseTaskPty();
    delete searchPanelUI;
}

void ProjectManagerPlugin::showAbout() {
    QMessageBox::information(dynamic_cast<QMainWindow *>(mdiServer), tr("About"),
                             tr("The project manager plugin"));
}

qmdiActionGroup *ProjectManagerPlugin::getContextMenuActions(const QString &menuId,
                                                             const QString &dirPath) {
    if (menuId != FileSystemWidget::POPUPMENU) {
        return nullptr;
    }

    auto fileInfo = QFileInfo(dirPath);
    if (!fileInfo.isDir()) {
        return nullptr;
    }

    auto action = new QAction(tr("Open Directory as Project"));
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::FolderOpen));
    connect(action, &QAction::triggered, this, [this, dirPath]() {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setText(tr("Do you want to load directory as a project?\n\n%2").arg(dirPath));
        msgBox.setWindowTitle(tr("Confirmation"));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);

        auto ret = msgBox.exec();
        switch (ret) {
        case QMessageBox::Yes: {
            if (addProjectFromDir(dirPath)) {
                newProjectSelected(gui->projectButton->currentIndex());
            }
            projectDock->raise();
            break;
        }
        case QMessageBox::No:
            break;
        default:
            break;
        }
    });

    auto actionGroup = new qmdiActionGroup(tr("Project Manager"));
    actionGroup->addAction(action);
    return actionGroup;
}

void ProjectManagerPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);
    auto manager = dynamic_cast<PluginManager *>(host);
    auto *w = new QWidget;

    gui = new Ui::ProjectManagerGUI;
    gui->setupUi(w);
    gui->addDirectory->setIcon(QIcon::fromTheme("list-add"));
    gui->removeDirectory->setIcon(QIcon::fromTheme("list-remove"));

    connect(gui->runButton, &QAbstractButton::clicked, this,
            &ProjectManagerPlugin::runButton_clicked);
    connect(gui->taskButton, &QAbstractButton::clicked, this,
            &ProjectManagerPlugin::runTask_clicked);
    connect(gui->cleanButton, &QAbstractButton::clicked, this,
            &ProjectManagerPlugin::clearProject_clicked);
    connect(gui->addDirectory, &QAbstractButton::clicked, this,
            &ProjectManagerPlugin::addProject_clicked);
    connect(gui->removeDirectory, &QAbstractButton::clicked, this,
            &ProjectManagerPlugin::removeProject_clicked);
    connect(gui->filesList, &FilesList::fileSelected, this, &ProjectManagerPlugin::onItemClicked);

    projectDock = manager->createNewPanel(Panels::West, "projectmamager", tr("Project"), w);

    projectIssues = new ProjectIssuesWidget(manager);
    issuesDock =
        manager->createNewPanel(Panels::South, "projectissues", tr("Issues"), projectIssues);

    auto w2 = new QWidget;
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    outputPanel = new Ui::BuildRunOutput;
    outputPanel->setupUi(w2);
    outputDock = manager->createNewPanel(Panels::South, "buildoutput", tr("Output"), w2);
    outputPanel->commandOuput->setLineWrapMode(QTextEdit::LineWrapMode::NoWrap);
    outputPanel->commandOuput->viewport()->setCursor(Qt::CursorShape::IBeamCursor);
    outputPanel->commandOuput->setOpenExternalLinks(false);
    outputPanel->commandOuput->setOpenLinks(false);
    outputPanel->commandOuput->setFont(font);
    outputPanel->commandOuput->setAcceptRichText(true);
    outputPanel->commandOuput->viewport()->setCursor(Qt::CursorShape::IBeamCursor);
    outputPanel->commandOuput->setLineWrapMode(QTextEdit::NoWrap);
    outputPanel->commandOuput->clear();

    // why in a timer? because at this step, if we set the palette, the widget
    // will get inserted "soon" and its palette will change anyway.
    QTimer::singleShot(0, this, &ProjectManagerPlugin::configurationHasBeenModified);

    connect(outputPanel->commandOuput, &QTextBrowser::anchorClicked, outputPanel->commandOuput,
            [this](const QUrl &link) {
                if (!link.isLocalFile()) {
                    QDesktopServices::openUrl(link);
                    return;
                }

                auto fi = QFileInfo(link.toLocalFile());
                auto fileName = QDir::toNativeSeparators(fi.filePath());
                auto dirName = fi.dir().dirName();

                if (fi.isRelative() || dirName == "./") {
                    qDebug() << "OutputPanel: clicking on relative path failed: "
                             << link.toString();
                    return;
                }
                auto row = -1;
                auto col = -1;
                auto fragment = link.fragment();

                if (!fragment.isEmpty()) {
                    // line/rows are 1 based on compiler output, internally they are 0 based
                    auto parts = fragment.split(',');
                    auto ok = false;
                    if (!parts.isEmpty()) {
                        row = parts[0].toInt(&ok);
                        if (!ok) {
                            row = -1;
                        } else {
                            row--;
                        }
                    }
                    if (parts.size() > 1) {
                        col = parts[1].toInt(&ok);
                        if (!ok) {
                            col = -1;
                        } else {
                            col--;
                        }
                    }
                }
                getManager()->openFile(fileName, col, row);
            });

    connect(outputPanel->clearOutput, &QAbstractButton::clicked, this,
            [this]() { this->outputPanel->commandOuput->clear(); });
    connect(outputPanel->copyOutput, &QAbstractButton::clicked, this, [this]() {
        auto text = this->outputPanel->commandOuput->document()->toPlainText();
        auto clipboard = QGuiApplication::clipboard();
        clipboard->setText(text);
    });
    connect(outputPanel->playButton, &QAbstractButton::clicked, this,
            [this]() { this->gui->runButton->animateClick(); });
    connect(outputPanel->buildButton, &QAbstractButton::clicked, this,
            [this]() { this->gui->taskButton->animateClick(); });
    connect(outputPanel->cancelButton, &QAbstractButton::clicked, this, [this]() {
        if (this->runProcess.processId() != 0) {
            this->runProcess.kill();
        }
    });
    connect(&runProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        auto output = this->runProcess.readAllStandardOutput();
        processBuildOutput(output);
    });
    connect(&runProcess, &QProcess::readyReadStandardError, this, [this]() {
        auto output = this->runProcess.readAllStandardError();
        processBuildOutput(output);
    });
    connect(
        &runProcess, &QProcess::finished, this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            auto output = QString("[code=%1, status=%2]\n").arg(exitCode).arg(str(exitStatus));

            appendAnsiText(outputPanel->commandOuput, output, {});
            getManager()->showPanels(Qt::BottomDockWidgetArea);
            outputDock->raise();
            outputDock->show();

            auto process = sender();
            auto var1 = process->property("runningTask").value<quintptr>();

            // FIXME: if the task gets deleted
            auto *runningTask = reinterpret_cast<TaskInfo *>(var1);

            auto project =
                process->property("runningProject").value<std::shared_ptr<ProjectBuildConfig>>();
            auto buildDirectory = process->property(GlobalArguments::BuildDirectory).toString();
            auto sourceDirectory = process->property(GlobalArguments::SourceDirectory).toString();
            auto projectName = QString{};
            {
                auto d = QDir(sourceDirectory);
                projectName = d.dirName();
            }

            // GlobalCommands::BuildFinished is documented as "build succeeded", and
            // CTags/TreeSitter reindex on it - so a build that exited non-zero must
            // not trigger it. Note exitStatus only says whether the process crashed;
            // the exit code is what says whether the build worked.
            auto const buildSucceeded =
                exitStatus == QProcess::ExitStatus::NormalExit && exitCode == 0;
            if (project && runningTask && runningTask->isBuild && buildSucceeded) {
                qDebug() << "Notifying about a good build" << project->buildDir << buildDirectory
                         << project->sourceDir << sourceDirectory << runningTask->name;

                if (project->autoGenerated) {
                    project->updateBinaries();
                    updateExecutablesUI(project);
                }

                getManager()->handleCommandAsync(
                    GlobalCommands::BuildFinished,
                    {
                        {GlobalArguments::BuildDirectory, buildDirectory},
                        {GlobalArguments::SourceDirectory, sourceDirectory},
                        {GlobalArguments::TaskName, runningTask->name},
                        {GlobalArguments::Name, project->name},
                        {GlobalArguments::ExitCode, exitCode},
                    });
            }

            runProcess.setProperty("runningTask", {});
            runProcess.setProperty("runningProject", {});
        });
    connect(&runProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        auto output = QString("\n[error: code=%1]").arg((int)error);
        auto project = getCurrentConfig();

        appendAnsiText(outputPanel->commandOuput, output, project->sourceDir);
        qWarning() << "Process error occurred:" << error;
        qWarning() << "Error string:" << runProcess.errorString();
    });
    connect(&configWatcher, &QFileSystemWatcher::fileChanged, this,
            &ProjectManagerPlugin::projectFile_modified);
    connect(gui->cleanButton, &QToolButton::clicked, this,
            [this]() { this->outputPanel->commandOuput->clear(); });
    auto menu = new QMenu(getManager());
    auto rescanKits = new QAction(tr("Rescan kits"), menu);
    auto recreateKits = new QAction(tr("Recreate kits"), menu);
    auto openKitsinFM = new QAction(tr("Open kits dir in file manager"), menu);
    auto editCurrentKit = new QAction(tr("Edit this kit"), menu);

    connect(rescanKits, &QAction::triggered, this, [this]() {
        auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        auto kits = findKitDefinitions(QDir::toNativeSeparators(dataPath).toStdString());
        kitsModel->setKitDefinitions(kits);
        updateKitsUI();
    });
    connect(recreateKits, &QAction::triggered, this, [rescanKits]() {
        auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        {
            auto d = QDir(dataPath);
            if (!d.exists()) {
                if (!d.mkpath(dataPath)) {
                    qDebug() << "Failed to create directory:" << dataPath;
                    return;
                }
            }
        }
        regenerateKits(std::filesystem::path(dataPath.toStdString()));
        rescanKits->trigger();
    });
    connect(openKitsinFM, &QAction::triggered, this, []() {
        auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        auto url = QUrl::fromLocalFile(dataPath);
        QDesktopServices::openUrl(url);
    });
    connect(editCurrentKit, &QAction::triggered, this, [this]() {
        auto kit = this->getCurrentKit();
        if (!kit) {
            return;
        }
        auto path = QString::fromStdString(kit->filePath);
        getManager()->openFile(path);
    });

    menu->addAction(rescanKits);
    menu->addAction(recreateKits);
    menu->addAction(openKitsinFM);
    menu->addAction(editCurrentKit);

    gui->toolkitToolButton->setMenu(menu);
    gui->toolkitToolButton->setPopupMode(QToolButton::InstantPopup);
    connect(gui->editBuildConfig, &QAbstractButton::clicked, this, [this]() {
        auto buildConfig = this->getCurrentConfig();
        if (buildConfig->fileName.isEmpty()) {
            auto path = buildConfig->sourceDir + "/" + "codepointer.json";
            buildConfig->saveToFile(path);
            configWatcher.addPath(buildConfig->fileName);
        }
        getManager()->openFile(buildConfig->fileName);
    });

    projectModel = new ProjectBuildModel();
    searchPanelUI = new ProjectSearch(manager, projectModel);
    auto searchPanel = manager->createNewPanel(Panels::West, "search", tr("Search"), searchPanelUI);
    auto projectSearch = new QAction(tr("Search in project"));
    projectSearch->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_F));
    connect(projectSearch, &QAction::triggered, this, [searchPanel, this]() {
        auto currentEditor = dynamic_cast<qmdiEditor *>(getManager()->currentClient());
        if (currentEditor) {
            // FIXME: need api for getting selected text from mdi client
            auto text = currentEditor->getSelectedText();
            if (!text.isEmpty()) {
                searchPanelUI->setSearchPattern(text);
            }
        }
        searchPanel->raise();
        searchPanel->show();
        searchPanel->setFocus();
        searchPanelUI->setFocusOnSearch();
    });
    manager->addAction(projectSearch);
    kitsModel = new KitDefinitionModel();
    auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    auto kits = findKitDefinitions(QDir::toNativeSeparators(dataPath).toStdString());
    kitsModel->setKitDefinitions(kits);
    gui->kitButton->setFilterPlaceholder(tr("Filter kits..."));
    gui->kitButton->setPreferLastWhenInvalid(false);
    updateKitsUI();

    gui->projectButton->setFilterPlaceholder(tr("Filter projects..."));
    gui->projectButton->setPreferLastWhenInvalid(true);
    connect(gui->projectButton, &SearchableMenuButton::itemSelected, this,
            &ProjectManagerPlugin::newProjectSelected);
    updateProjectButtonMenu();

    runAction = new QAction(QIcon::fromTheme("document-save"), tr("&Run"), this);
    buildAction = new QAction(QIcon::fromTheme("document-save-as"), tr("&Run task"), this);
    clearAction = new QAction(QIcon::fromTheme("edit-clear"), tr("&Delete build directory"), this);

    runAction->setEnabled(false);
    buildAction->setEnabled(false);
    clearAction->setEnabled(false);

    connect(runAction, &QAction::triggered, this, &ProjectManagerPlugin::runButton_clicked);
    connect(buildAction, &QAction::triggered, this, &ProjectManagerPlugin::runTask_clicked);
    connect(clearAction, &QAction::triggered, this, &ProjectManagerPlugin::clearProject_clicked);

    runAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    buildAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_B));

    this->menus[tr("&Project")]->addAction(runAction);
    this->menus[tr("&Project")]->addAction(buildAction);
    this->menus[tr("&Project")]->addAction(clearAction);

    this->availableTasksMenu = new QMenu(tr("Available tasks"));
    this->availableExecutablesMenu = new QMenu(tr("Available executables"));
    this->menus[tr("&Project")]->addMenu(availableTasksMenu);
    this->menus[tr("&Project")]->addMenu(availableExecutablesMenu);

    auto addNewProjectIcon = new QAction(tr("Add existing project.."));
    auto removeProjectIcon = new QAction(tr("Close project"));
    connect(addNewProjectIcon, &QAction::triggered, this,
            [this]() { this->gui->addDirectory->animateClick(); });
    connect(removeProjectIcon, &QAction::triggered, this,
            [this]() { this->gui->removeDirectory->animateClick(); });

    this->menus[tr("&Project")]->addSeparator();
    this->menus[tr("&Project")]->addAction(addNewProjectIcon);
    this->menus[tr("&Project")]->addAction(removeProjectIcon);

    auto quickOpen = new QAction(tr("Quick open file..."), this);
    quickOpen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    this->menus[tr("&Project")]->addAction(quickOpen);

    commandPalette = new CommandPalette(manager);
    commandPalette->setFilterModes(CommandPalette::FilterMode::FileMatch);
    connect(quickOpen, &QAction::triggered, this, [this]() {
        if (commandPalette->isVisible()) {
            commandPalette->hide();
        } else {
            auto model = new QStringListModel(commandPalette);
            model->setStringList(gui->filesList->currentFilteredFiles());
            commandPalette->setDataModel(model);
            commandPalette->clearText();
            commandPalette->show();
        }
    });

    connect(commandPalette, &CommandPalette::didChooseItem, this,
            [this](const QModelIndex index, const QAbstractItemModel *model) {
                auto fname = model->data(index, Qt::DisplayRole).toString();
                auto dirName = gui->filesList->getDir();
                this->getManager()->openFile(dirName + fname);
                commandPalette->setDataModel(nullptr);
            });
}

void ProjectManagerPlugin::configurationHasBeenModified() {
    IPlugin::configurationHasBeenModified();

    auto p = outputDock->palette();
    if (getConfig().getBlackConsole()) {
        p.setColor(QPalette::Base, Qt::black);
        p.setColor(QPalette::Text, QColor(192, 192, 192));
    }
    outputPanel->commandOuput->setPalette(p);
    outputPanel->commandOuput->viewport()->setPalette(p);

    auto newFont = QFont();
    newFont.fromString(getConfig().getConsoleFont());
    outputPanel->commandOuput->setFont(newFont);
}

void ProjectManagerPlugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);

    searchPanelUI->setSearchPath(getConfig().getSearchPath());
    searchPanelUI->setSearchPattern(getConfig().getSearchPattern());
    searchPanelUI->setSearchInclude(getConfig().getSearchInclude());
    searchPanelUI->setSearchExclude(getConfig().getSearchExclude());
    searchPanelUI->setCollapseFiles(getConfig().getSearchCollapseFileNames());
    searchPanelUI->setSearchWholeWords(getConfig().getSearchWholeWords());
    searchPanelUI->setSearchRegex(getConfig().getSearchRegex());
    searchPanelUI->setSearchCaseSensitive(getConfig().getSearchSensitive());

    auto dirsToLoad = getConfig().getOpenDirs();

    for (auto const &d : std::as_const(dirsToLoad)) {
        addProjectFromDir(d);
    }

    auto selectedDirectory = getConfig().getSelectedDirectory();
    auto i = projectModel->findConfigDirIndex(selectedDirectory);
    if (i >= 0) {
        gui->projectButton->setCurrentIndex(i);
        newProjectSelected(i);
    }
}

void ProjectManagerPlugin::saveConfig(QSettings &settings) {
    auto selectedProject = getCurrentConfig();
    auto selectProjetName = selectedProject ? selectedProject->sourceDir : "";
    getConfig().setOpenDirs(projectModel->getAllOpenDirs());
    getConfig().setSelectedDirectory(selectProjetName);
    getConfig().setSearchPattern(searchPanelUI->getSearchPattern());
    getConfig().setSearchInclude(searchPanelUI->getSearchInclude());
    getConfig().setSearchExclude(searchPanelUI->getSearchExclude());
    getConfig().setSearchPath(searchPanelUI->getSearchPath());
    getConfig().setSearchCollapseFileNames(searchPanelUI->getCollapseFiles());
    getConfig().setSearchWholeWords(searchPanelUI->getSearchWholeWords());
    getConfig().setSearchRegex(searchPanelUI->getSearchRegex());
    getConfig().setSearchSensitive(searchPanelUI->getSearchCaseSensitive());
    IPlugin::saveConfig(settings);
}

int ProjectManagerPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &) const {
    if (command == GlobalCommands::LoadedFile) {
        return true;
    }
    if (command == GlobalCommands::SavedFile) {
        return true;
    }
    if (command == GlobalCommands::ClosedFile) {
        return true;
    }
    if (command == GlobalCommands::FindMatchingFile) {
        return true;
    }
    if (command == GlobalCommands::GetProjectForFile) {
        return true;
    }
    return false;
}

#ifdef Q_OS_WIN
#include <windows.h>
#endif

auto verifyRunnable(const QString &fileName) -> bool {
    auto info = QFileInfo(fileName);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

#ifdef Q_OS_WIN
    static const auto runnableExts =
        QStringList{".exe", ".bat", ".cmd", ".ps1", ".com", "py", "js"};
    if (runnableExts.contains(info.suffix(), Qt::CaseInsensitive)) {
        return true;
    }

#if 0
    BUG: this will return true for basically everything.

    // Ask Windows: is there a registered handler for this file type?
    auto wFile = reinterpret_cast<LPCWSTR>(fileName.utf16());
    wchar_t exePath[MAX_PATH];
    auto result = FindExecutableW(wFile, nullptr, exePath);

    // According to docs: >32 means success (a valid handler was found)
    // https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-findexecutablew
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        return true;
    }
#endif

#else
    // Linux / macOS
    if (info.isExecutable()) {
        return true;
    }
#endif
    return false;
}

static const QStringList cExtensions = {"c", "cpp", "cxx", "cc", "c++"};
static const QStringList headerExtensions = {"h", "hpp", "hh"};

QFuture<CommandArgs> ProjectManagerPlugin::handleCommandAsync(const QString &command,
                                                              const CommandArgs &args) {
    if (command == GlobalCommands::LoadedFile) {
        auto client = args.value(GlobalArguments::Client).value<qmdiClient *>();
        auto filename = args[GlobalArguments::FileName].toString();
        fixClientsMenu(client, filename);
        return {};
    }
    if (command == GlobalCommands::SavedFile) {
        auto client = args.value(GlobalArguments::Client).value<qmdiClient *>();
        auto filename = args[GlobalArguments::FileName].toString();
        fixClientsMenu(client, filename);
        return {};
    }
    if (command == GlobalCommands::ClosedFile) {
        auto filename = args[GlobalArguments::FileName];
        auto client = args.value(GlobalArguments::Client).value<qmdiClient *>();
        if (client) {
            auto action = client->contextMenu.findActionNamed("runScript");
            client->contextMenu.removeAction(action);
            if (client == mdiServer->getCurrentClient()) {
                this->mdiServer->mdiHost->unmergeClient(client);
                this->mdiServer->mdiHost->mergeClient(client);
                this->mdiServer->mdiHost->updateGUI();
            }
        }
        return {};
    }
    if (command == GlobalCommands::FindMatchingFile) {
        auto filename = args[GlobalArguments::FileName].toString();
        auto allFiles = gui->filesList->getAllFiles();
        auto fileInfo = QFileInfo(filename);
        auto baseName = fileInfo.baseName();
        auto suffix = fileInfo.suffix().toLower();
        auto targetExtensions = QStringList();

        if (cExtensions.contains(suffix)) {
            targetExtensions = headerExtensions;
        } else if (headerExtensions.contains(suffix)) {
            targetExtensions = cExtensions;
        }

        if (!targetExtensions.isEmpty()) {
            for (const auto &file : allFiles) {
                auto fi = QFileInfo(file);
                if (fi.baseName() == baseName && targetExtensions.contains(fi.suffix().toLower())) {
                    auto fullFileName =
                        gui->filesList->getDir() + QDir::separator() + fi.filePath();
                    fullFileName = QDir::cleanPath(fullFileName);
                    return QtFuture::makeReadyValueFuture(
                        CommandArgs{{GlobalArguments::FileName, fullFileName}});
                }
            }
        }
    }
    if (command == GlobalCommands::GetProjectForFile) {
        auto filename = args[GlobalArguments::FileName].toString();
        auto project = projectModel->findProjectForFile(filename);
        auto out = CommandArgs();

        out[GlobalArguments::Value] = project != nullptr;
        if (project) {
            out[GlobalArguments::ProjectName] = project->name;
            out[GlobalArguments::SourceDirectory] = project->sourceDir;
        }
        return QtFuture::makeReadyValueFuture(out);
    }
    return {};
}

int ProjectManagerPlugin::canOpenFile(const QString &fileName) {
    auto uri = QUrl(fileName);
    if (uri.scheme() == "loaded") {
        return 5;
    }
    if (uri.scheme() == "projectmanager") {
        return 5;
    }
    return 0;
}

qmdiClient *ProjectManagerPlugin::openFile(const QString &requestedUri, int x, int, int) {
    auto uri = QUrl(requestedUri);
    auto filename = uri.path();
    auto fi = QFileInfo(filename);
    auto dir = fi.dir().absolutePath();

    if (uri.scheme() == "loaded") {
        tryOpenProject(filename, dir);
        return nullptr;
    }

    if (uri.scheme() == "projectmanager") {
        tryScrollOutput(x);
        return nullptr;
    }

    // just edit the file
    return nullptr;
}

std::shared_ptr<ProjectBuildConfig> ProjectManagerPlugin::getCurrentConfig() const {
    auto currentIndex = gui->projectButton->currentIndex();
    if (currentIndex < 0) {
        return {};
    }
    return projectModel->getConfig(currentIndex);
}

const KitDefinition *ProjectManagerPlugin::getCurrentKit() const {
    auto currentIndex = gui->kitButton->currentIndex();
    if (currentIndex < 0) {
        return nullptr;
    }
    return &kitsModel->getKit(currentIndex);
}

void ProjectManagerPlugin::onItemClicked(const QString &fileName) {
    getManager()->openFile(fileName);
}

void ProjectManagerPlugin::addProject_clicked() {
    QString dirName = QFileDialog::getExistingDirectory(gui->filesList, tr("Add directory"));
    if (addProjectFromDir(dirName)) {
        newProjectSelected(gui->projectButton->currentIndex());
    }
    getManager()->saveSettings();
}

void ProjectManagerPlugin::removeProject_clicked() {
    auto index = gui->projectButton->currentIndex();
    if (index < 0) {
        return;
    }
    auto projectConfig = projectModel->getConfig(index);
    auto path = projectConfig->fileName;
    projectModel->removeConfig(index);
    searchPanelUI->updateProjectList();
    configWatcher.removePath(path);
    getManager()->saveSettings();

    // clang-format off
    getManager()->handleCommandAsync(GlobalCommands::ProjectRemoved, {
        {GlobalArguments::ProjectName, projectConfig->name },
        {GlobalArguments::SourceDirectory, projectConfig->sourceDir },
        {GlobalArguments::BuildDirectory, projectConfig->buildDir },
        }
    );
    // clang-format on
    searchPanelUI->updateProjectList();

    updateProjectButtonMenu();
    newProjectSelected(gui->projectButton->currentIndex());
}

void ProjectManagerPlugin::newProjectSelected(int index) {
    auto buildConfig = std::shared_ptr<ProjectBuildConfig>();
    if (index >= 0) {
        buildConfig = projectModel->getConfig(index);
    }

    if (!buildConfig) {
        this->gui->filesList->clear();
        this->gui->filesList->setShowList({});
        this->gui->filesList->setExcludeList({});
        this->gui->filesList->setShowListEnabled(false);
        this->gui->filesList->setExcludeListEnabled(false);
    } else {
        auto s1 = buildConfig->expand(buildConfig->displayFilter);
        auto s2 = buildConfig->expand(buildConfig->hideFilter);
        this->gui->filesList->setShowList(s1);
        this->gui->filesList->setExcludeList(s2);
        this->gui->filesList->setShowListEnabled(true);
        this->gui->filesList->setExcludeListEnabled(true);
        this->gui->filesList->setDir(buildConfig->sourceDir);
    }

    updateTasksUI(buildConfig);
    updateExecutablesUI(buildConfig);
}

auto ProjectManagerPlugin::updateProjectButtonMenu() -> void {
    auto names = QStringList();
    auto tooltips = QStringList();
    auto count = projectModel->rowCount();
    names.reserve(count);
    tooltips.reserve(count);
    for (auto i = 0; i < count; i++) {
        auto buildConfig = projectModel->getConfig(i);
        names << buildConfig->name;
        tooltips << QDir::toNativeSeparators(buildConfig->sourceDir);
    }
    gui->projectButton->setItems(names, tooltips);
}

auto ProjectManagerPlugin::updateKitsUI() -> void {
    auto names = QStringList();
    auto tooltips = QStringList();
    auto count = kitsModel->rowCount();
    names.reserve(count);
    tooltips.reserve(count);
    for (auto i = 0; i < count; i++) {
        auto kit = kitsModel->getKit(i);
        names << QString::fromStdString(kit.name);
        tooltips << QString("%1\n%2").arg(QString::fromStdString(kit.name),
                                          QString::fromStdString(kit.filePath));
    }
    gui->kitButton->setItems(names, tooltips);
}

auto ProjectManagerPlugin::releaseTaskPty() -> void {
#if defined(USE_TTY_FOR_TASKS)
    // Stop watching before closing, so the notifier never sees a stale fd.
    if (taskPtyNotifier) {
        taskPtyNotifier->setEnabled(false);
        delete taskPtyNotifier;
        taskPtyNotifier = nullptr;
    }
    if (taskPtyMasterFd >= 0) {
        close(taskPtyMasterFd);
        taskPtyMasterFd = -1;
    }
#endif
}

auto ProjectManagerPlugin::stopRunningTask() -> bool {
    // processId() is 0 while the process is still in the Starting state, so it is
    // not a usable liveness test - use state() instead.
    if (runProcess.state() == QProcess::NotRunning) {
        return true;
    }
    runProcess.kill();
    // kill() is asynchronous: without waiting for the process to be reaped, the
    // following start() would hit "QProcess::start: Process is already running"
    // and silently do nothing.
    if (!runProcess.waitForFinished(3000)) {
        return false;
    }
    return runProcess.state() == QProcess::NotRunning;
}

bool ProjectManagerPlugin::runCommand(const QString &workingDirectory, const QString &program,
                                      const QStringList &arguments,
                                      const QProcessEnvironment &customEnv, bool capture) {
    if (!stopRunningTask()) {
        processBuildOutput("Could not stop the currently running task\n");
        qWarning() << "Could not stop the currently running task";
        return false;
    }
    // The previous run's pty is dead now - reclaim it before allocating a new one.
    releaseTaskPty();

    // Drop any stale claim; do_runTask() re-establishes it once the task starts.
    runProcess.setProperty("runningTask", {});
    runProcess.setProperty("runningProject", {});

    auto env = customEnv;

#if defined(USE_TTY_FOR_TASKS)
    auto usingPty = false;
    auto masterFd = -1;
#endif

    // Reset redirections
    runProcess.setStandardInputFile(QString());
    runProcess.setStandardOutputFile(QString());
    runProcess.setStandardErrorFile(QString());

#if defined(USE_TTY_FOR_TASKS)
    if (capture) {
        usingPty = setupPty(runProcess, masterFd);
        if (usingPty && masterFd >= 0) {
            runProcess.setProcessChannelMode(QProcess::MergedChannels);
            auto notifier = new QSocketNotifier(masterFd, QSocketNotifier::Read, &runProcess);
            taskPtyMasterFd = masterFd;
            taskPtyNotifier = notifier;
            // SingleShotConnection: this is per-run state, so the connection must not
            // outlive the run that created it.
            connect(
                &runProcess, &QProcess::finished, this, [this]() { releaseTaskPty(); },
                Qt::SingleShotConnection);
            connect(notifier, &QSocketNotifier::activated, notifier, [this, masterFd]() {
                char buffer[4096];
                auto bytesRead = read(masterFd, buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    QString lines = QString::fromUtf8(buffer, bytesRead);
                    processBuildOutput(lines);
                }
            });
        }
    }
    env.insert("FORCE_COLOR", "1");
    env.insert("CLICOLOR_FORCE", "1");
    env.insert("TERM", "xterm-256color");
#endif

    runProcess.setWorkingDirectory(workingDirectory);
    runProcess.setProcessEnvironment(env);
    runProcess.setProgram(program);
    runProcess.setArguments(arguments);

    if (capture) {
#if defined(Q_OS_WIN)
        runProcess.setCreateProcessArgumentsModifier({});
#endif
#if defined(USE_TTY_FOR_TASKS)
        if (!usingPty)
#endif
        {
            runProcess.setProcessChannelMode(QProcess::SeparateChannels);
        }
    } else {
#if defined(Q_OS_WIN)
        runProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NEW_CONSOLE;
            args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        });
#endif
        runProcess.setProcessChannelMode(QProcess::ForwardedChannels);
    }

    runProcess.start();
    if (!runProcess.waitForStarted()) {
        processBuildOutput("Process failed to start\n");
        qWarning() << "Process failed to start:" << program << runProcess.errorString();
        // finished() never fires for a process that never started, so the pty
        // would leak if we did not reclaim it here.
        releaseTaskPty();
        return false;
    }
    return true;
}

void ProjectManagerPlugin::do_runExecutable(const ExecutableInfo *info) {
    if (!info) {
        qDebug() << "ProjectManagerPlugin::do_runExecutable() - info is null";
        return;
    }
    getManager()->saveSettings();
    auto project = getCurrentConfig();
    auto executablePath = QDir::toNativeSeparators(findExecForPlatform(info->executables));
    auto workingDirectory = info->runDirectory.isEmpty() ? project->buildDir : info->runDirectory;
    executablePath = project->expand(executablePath);

    workingDirectory = project->expand(workingDirectory);
    outputPanel->commandOuput->clear();
    appendAnsiText(outputPanel->commandOuput, "cd " + QDir::toNativeSeparators(workingDirectory),
                   {});
    appendAnsiText(outputPanel->commandOuput, QString("\n%1\n").arg(executablePath), {});
    outputDock->raise();
    outputDock->show();

    auto env = QProcessEnvironment::systemEnvironment();
    auto arguments = QStringList();
    auto useKitForRunning = true;

    if (useKitForRunning) {
        auto kit = getCurrentKit();
        if (kit) {
            auto buildDirectory = project->expand(project->buildDir);
            auto sourceDirectory = project->expand(project->sourceDir);
            auto taskCommand = project->expand(executablePath);
            env.insert("run_directory", workingDirectory);
            env.insert("build_directory", buildDirectory);
            env.insert("source_directory", sourceDirectory);
            env.insert("task", taskCommand);
            executablePath = QString::fromStdString(kit->filePath);
        }
    }
    getManager()->saveSettings();
    auto capture = outputPanel ? outputPanel->captureAppOutput->isChecked() : true;
    runCommand(workingDirectory, executablePath, arguments, env, capture);
}

void ProjectManagerPlugin::do_runTask(const TaskInfo *task) {
    auto platform = PLATFORM_CURRENT;
    getManager()->saveSettings();

    if (!task->commands.contains(platform) || task->commands.value(platform).isEmpty()) {
        auto msg = QString("do_runTask: No valid commands for platform ") + platform;
        qWarning() << msg;
        appendAnsiText(outputPanel->commandOuput, msg + "\n", {});
        return;
    }

    auto kit = getCurrentKit();
    auto commands = task->commands.value(platform);
    auto project = getCurrentConfig();
    auto taskCommand = project->expand(commands.join(" && "));
    auto workingDirectory = project->expand(task->runDirectory);
    auto buildDirectory = project->expand(project->buildDir);
    auto sourceDirectory = project->expand(project->sourceDir);

    if (workingDirectory.isEmpty()) {
        workingDirectory = buildDirectory;
    }
    workingDirectory = QDir::toNativeSeparators(workingDirectory);
    sourceDirectory = QDir::toNativeSeparators(sourceDirectory);

    outputDock->raise();
    outputDock->show();
    outputPanel->commandOuput->clear();
    appendAnsiText(outputPanel->commandOuput, "cd " + workingDirectory + "\n", {});

    auto env = QProcessEnvironment::systemEnvironment();
    auto program = QString();
    auto arguments = QStringList();

    if (!kit) {
        auto [interpreter, command] = getCommandInterpreter(taskCommand);
        appendAnsiText(outputPanel->commandOuput, interpreter + " " + command.join(" ") + "\n", {});
        appendAnsiText(outputPanel->commandOuput, "Commands: " + taskCommand + "\n", {});
        program = interpreter;
        arguments = command;
    } else {
        // NOTE: these are environment variables se
        env.insert("run_directory", workingDirectory);
        env.insert("build_directory", buildDirectory);
        env.insert("source_directory", sourceDirectory);
        env.insert("task", taskCommand);
        program = QString::fromStdString(kit->filePath);
        arguments = {};
    }

    // These must be assigned *after* the process has started. runCommand() may kill
    // and reap a previous task, which fires its finished() handler synchronously -
    // and that handler reads these very properties. Setting them up front made the
    // outgoing task report the incoming task's name, project and directories.
    auto capture = outputPanel ? outputPanel->captureTasksOutput->isChecked() : true;
    if (!runCommand(workingDirectory, program, arguments, env, capture)) {
        return;
    }

    runProcess.setProperty("runningTask", QVariant::fromValue(reinterpret_cast<quintptr>(task)));
    runProcess.setProperty("runningProject", QVariant::fromValue(project));
    runProcess.setProperty("workingDirectory", QVariant::fromValue(workingDirectory));
    runProcess.setProperty(GlobalArguments::BuildDirectory, QVariant::fromValue(buildDirectory));
    runProcess.setProperty(GlobalArguments::SourceDirectory, QVariant::fromValue(sourceDirectory));
}

void ProjectManagerPlugin::runButton_clicked() {
    assert(this->selectedTarget);
    do_runExecutable(this->selectedTarget);
}

void ProjectManagerPlugin::runTask_clicked() {
    auto buildConfig = getCurrentConfig();
    if (selectedTaskIndex < 0) {
        selectedTaskIndex = 0;
    }
    if (!buildConfig || selectedTaskIndex >= buildConfig->tasksInfo.size()) {
        return;
    }

    if (getConfig().getSaveBeforeTask()) {
        if (!saveAllDocuments()) {
            return;
        }
    }
    auto manager = getManager();
    auto count = manager->visibleTabs();
    for (auto i = 0; i < count; i++) {
        auto client = manager->getMdiClient(i);
        if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
            editor->removeMetaData();
        }
    }
    getManager()->saveSettings();
    this->projectIssues->clearAllIssues();
    do_runTask(&buildConfig->tasksInfo[selectedTaskIndex]);
}

void ProjectManagerPlugin::clearProject_clicked() {
    auto project = getCurrentConfig();
    if (project == nullptr || project->buildDir.isEmpty()) {
        return;
    }

    auto projectBuildDir = QDir::toNativeSeparators(project->expand(project->buildDir));
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(tr("This will delete <b>%1</b> (%2). Do you want to proceed?")
                       .arg(project->buildDir, projectBuildDir));
    msgBox.setWindowTitle(tr("Confirmation"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    getManager()->saveSettings();
    auto ret = msgBox.exec();
    switch (ret) {
    case QMessageBox::Yes: {
        // TODO - run this in a thread?
        auto outputDir = QDir(projectBuildDir);
        outputDir.removeRecursively();
        break;
    }
    case QMessageBox::No:
        break;
    default:
        break;
    }
}

void ProjectManagerPlugin::projectFile_modified(const QString &path) {
    auto onDiskConfig = ProjectBuildConfig::buildFromJsonFile(path);
    auto inMemoryConfig = projectModel->findConfigFile(path);

    if (onDiskConfig) {
        if (*onDiskConfig == *inMemoryConfig) {
            qDebug("Config file modified, content similar ignoring - %s",
                   path.toStdString().data());
            return;
        }
        *inMemoryConfig = *onDiskConfig;
    } else if (!QFile::exists(path)) {
        if (inMemoryConfig) {
            const auto sourceDir = inMemoryConfig->sourceDir;
            configWatcher.removePath(path);
            auto reScannedConfig = ProjectBuildConfig::buildFromDirectory(sourceDir);
            *inMemoryConfig = *reScannedConfig;
        } else {
            qDebug() << "Config file" << path << "modified/deleted, but was not tracked in memory.";
        }
    } else {
        qDebug("Config file %s modified but not parsable (size %lld). Ignoring.",
               path.toStdString().data(), QFileInfo(path).size());
        return;
    }
    newProjectSelected(gui->projectButton->currentIndex());

    // TODO  - new file created is not working yet.
    qDebug("Config file modified - %s", path.toStdString().data());
}

auto ProjectManagerPlugin::addProjectFromDir(const QString &dirName) -> bool {
    if (dirName.isEmpty()) {
        return false;
    }
    auto buildConfig = projectModel->findConfigDir(dirName);
    if (buildConfig) {
        return false;
    }
    buildConfig = ProjectBuildConfig::buildFromDirectory(dirName);
    if (!buildConfig->fileName.isEmpty()) {
        configWatcher.addPath(buildConfig->fileName);
    }

    auto buildDirectory = buildConfig->expand(buildConfig->buildDir);
    auto sourceDirectory = buildConfig->expand(buildConfig->sourceDir);

    // clang-format off
    getManager()->handleCommandAsync(GlobalCommands::ProjectLoaded, {
        {GlobalArguments::ProjectName, buildConfig->name },
        {GlobalArguments::SourceDirectory, sourceDirectory },
        {GlobalArguments::BuildDirectory, buildDirectory },
    });
    // clang-format on

    projectModel->addConfig(buildConfig);
    searchPanelUI->updateProjectList();
    updateProjectButtonMenu();
    gui->projectButton->setCurrentIndex(projectModel->rowCount() - 1);
    searchPanelUI->updateProjectList();
    return true;
}

auto ProjectManagerPlugin::saveAllDocuments() -> bool {
    for (auto i = 0; i < mdiServer->getClientsCount(); i++) {
        auto c = mdiServer->getClient(i);
        if (!c->saveClientContent()) {
            return false;
        }
    }
    return true;
}

auto ProjectManagerPlugin::processBuildOutput(const QString &line) -> void {
    auto cursor = this->outputPanel->commandOuput->textCursor();
    auto lineNumber = cursor.blockNumber();

    // see https://github.com/codepointerapp/codepointer/issues/88
    // Ninja likes printing "\r" to clear line. Lets not deal with that
    auto fixedAnsi = line;
#if 1
    if (fixedAnsi.size() > 0 && fixedAnsi[0] == QChar('\r')) {
        fixedAnsi[0] = QChar('\n');
    }
#endif

    auto plaintext = removeAnsiEscapeCodes(fixedAnsi);
    auto project = this->getCurrentConfig();
    if (project) {
        auto sourceDir = project->sourceDir;
        auto buildDir = project->expand(project->buildDir);
        appendAnsiText(this->outputPanel->commandOuput, fixedAnsi, sourceDir);
        this->projectIssues->processLine(plaintext, lineNumber, sourceDir, buildDir);
    } else {
        appendAnsiText(this->outputPanel->commandOuput, fixedAnsi, "");
    }
}

auto ProjectManagerPlugin::updateTasksUI(std::shared_ptr<ProjectBuildConfig> buildConfig) -> void {
    if (!buildConfig || buildConfig->tasksInfo.size() == 0) {
        this->gui->taskButton->setText("...");
        this->gui->taskButton->setToolTip("...");
        this->gui->taskButton->setEnabled(false);
        this->availableTasksMenu->clear();

        this->buildAction->setEnabled(false);
        this->clearAction->setEnabled(false);
        this->availableTasksMenu->clear();
    } else {
        auto taskIndex = 0;
        if (!buildConfig->activeTaskName.isEmpty()) {
            taskIndex = buildConfig->findIndexOfTask(buildConfig->activeTaskName);
            if (taskIndex < 0) {
                taskIndex = 0;
            }
        }
        auto taskName = buildConfig->tasksInfo[taskIndex].name;
        auto taskCommand = buildConfig->tasksInfo[taskIndex].getTooltip();
        selectedTaskIndex = taskIndex;
        this->gui->taskButton->setEnabled(true);
        this->gui->taskButton->setText(taskName);
        this->gui->taskButton->setToolTip(taskCommand);

        this->buildAction->setEnabled(true);
        this->buildAction->setText(QString(tr("Action: %1")).arg(taskName));
        this->buildAction->setToolTip(taskCommand);

        this->clearAction->setEnabled(true);
        this->availableTasksMenu->hide();
        this->availableTasksMenu->clear();
        this->mdiServer->mdiHost->updateGUI();
        if (buildConfig->tasksInfo.size() == 1) {
            gui->taskButton->setMenu(nullptr);
        } else {
            auto menu = new QMenu(gui->taskButton);
            gui->taskButton->setMenu(menu);

            auto filterLineEdit = new QLineEdit(menu);
            filterLineEdit->setPlaceholderText(tr("Filter tasks..."));
            filterLineEdit->setClearButtonEnabled(true);

            auto filterAction = new QWidgetAction(menu);
            filterAction->setDefaultWidget(filterLineEdit);
            menu->addAction(filterAction);

            QList<QAction *> actions;
            for (const auto &taskInfo : std::as_const(buildConfig->tasksInfo)) {
                auto action = new QAction(taskInfo.name, menu);
                menu->addAction(action);
                actions.append(action);

                auto mainMenuAction = new QAction(taskInfo.name, this);
                connect(mainMenuAction, &QAction::triggered, this,
                        [this, taskInfo]() { this->do_runTask(&taskInfo); });
                this->availableTasksMenu->addAction(mainMenuAction);
            }

            connect(filterLineEdit, &QLineEdit::textChanged, menu,
                    [actions = actions](const QString &text) {
                        for (auto action : actions) {
                            action->setVisible(action->text().contains(text, Qt::CaseInsensitive));
                        }
                    });

            connect(menu, &QMenu::triggered, this, [this, actions, buildConfig](QAction *action) {
                auto it = std::find(actions.begin(), actions.end(), action);
                if (it == actions.end()) {
                    return;
                }
                auto index = std::distance(actions.begin(), it);
                auto taskName = buildConfig->tasksInfo[index].name;
                auto platform = PLATFORM_CURRENT;
                auto commands = buildConfig->tasksInfo[index].commands.value(platform);
                auto taskCommand = commands.isEmpty() ? "" : commands.first();

                buildConfig->activeTaskName = taskName;
                this->gui->taskButton->setEnabled(true);
                this->gui->taskButton->setText(taskName);
                this->gui->taskButton->setToolTip(taskCommand);

                this->selectedTaskIndex = index;
                this->buildAction->setEnabled(true);
                this->buildAction->setText(QString(tr("Action: %1")).arg(taskName));
                this->buildAction->setToolTip(taskCommand);
            });
        }
    }
}

auto ProjectManagerPlugin::updateExecutablesUI(std::shared_ptr<ProjectBuildConfig> buildConfig)
    -> void {
    if (!buildConfig || buildConfig->executables.size() == 0) {
        this->selectedTarget = nullptr;
        this->gui->runButton->setText("...");
        this->gui->runButton->setToolTip("...");
        this->gui->runButton->setEnabled(false);

        this->runAction->setEnabled(false);
        this->clearAction->setEnabled(false);
        this->availableExecutablesMenu->clear();
    } else {
        auto executableIndex = 0;
        if (!buildConfig->activeExecutableName.isEmpty()) {
            executableIndex = buildConfig->findIndexOfExecutable(buildConfig->activeExecutableName);
            if (executableIndex < 0) {
                executableIndex = 0;
            }
        }

        auto executableName = buildConfig->executables[executableIndex].name;
        auto executablePath =
            findExecForPlatform(buildConfig->executables[executableIndex].executables);

        this->gui->runButton->setEnabled(true);
        this->gui->runButton->setText(executableName);
        this->gui->runButton->setToolTip(QDir::toNativeSeparators(executablePath));
        this->selectedTarget = &buildConfig->executables[executableIndex];

        this->runAction->setEnabled(true);
        this->runAction->setText(
            QString(tr("Run: %1")).arg(QDir::toNativeSeparators(executableName)));
        this->runAction->setToolTip(QDir::toNativeSeparators(executablePath));

        this->availableExecutablesMenu->hide();
        this->availableExecutablesMenu->clear();
        this->mdiServer->mdiHost->updateGUI();
        if (buildConfig->executables.size() == 1) {
            this->gui->runButton->setMenu(nullptr);
        } else {
            auto menu = new QMenu(gui->runButton);
            this->gui->runButton->setMenu(menu);

            auto filterLineEdit = new QLineEdit(menu);
            filterLineEdit->setPlaceholderText(tr("Filter executables..."));
            filterLineEdit->setClearButtonEnabled(true);

            auto filterAction = new QWidgetAction(menu);
            filterAction->setDefaultWidget(filterLineEdit);
            menu->addAction(filterAction);

            QList<QAction *> actions;
            for (const auto &target : std::as_const(buildConfig->executables)) {
                auto action = new QAction(target.name, menu);
                menu->addAction(action);
                actions.append(action);

                auto mainMenuAction = new QAction(target.name, this);
                connect(mainMenuAction, &QAction::triggered, this,
                        [this, target]() { this->do_runExecutable(&target); });
                this->availableExecutablesMenu->addAction(mainMenuAction);
            }
            this->mdiServer->mdiHost->updateGUI();

            connect(filterLineEdit, &QLineEdit::textChanged, menu,
                    [actions = actions](const QString &text) {
                        for (auto action : actions) {
                            action->setVisible(action->text().contains(text, Qt::CaseInsensitive));
                        }
                    });

            connect(menu, &QMenu::triggered, this, [this, actions, buildConfig](QAction *action) {
                auto it = std::find(actions.begin(), actions.end(), action);
                if (it == actions.end()) {
                    return;
                }
                auto index = std::distance(actions.begin(), it);
                auto executableName = buildConfig->executables[index].name;
                auto executablePath =
                    findExecForPlatform(buildConfig->executables[index].executables);
                this->gui->runButton->setEnabled(true);
                this->gui->runButton->setText(executableName);
                this->gui->runButton->setToolTip(executablePath);
                this->selectedTarget = &buildConfig->executables[index];
                buildConfig->activeExecutableName = this->selectedTarget->name;

                this->runAction->setEnabled(true);
                this->runAction->setText(QString(tr("Run: %1")).arg(executableName));
                this->runAction->setToolTip(executablePath);
            });
        }
    }
}

auto ProjectManagerPlugin::tryOpenProject(const QString &filename, const QString &dir) -> bool {
    auto manager = getManager();
    auto client = manager->clientForFileName(filename);
    if (client) {
        emit manager->newClientAdded(client);
    }

    auto project = this->projectModel->findConfigFile(filename);
    if (project) {
        // TODO - should we select this project?
        return true;
    }

    project = this->projectModel->findConfigDir(dir);
    if (project) {
        // TODO - should we select this project?
        return true;
    }

    if (!ProjectBuildConfig::canLoadFile(filename)) {
        return true;
    }

    project = ProjectBuildConfig::buildFromFile(filename);
    if (!project) {
        // project file is not parsable. just bail out.
        return false;
    }

    auto text = tr("This is a project file, do you want to load the project, or just edit the file?"
                   "<br/><br/><b>%1</b>")
                    .arg(filename);
    QMessageBox box;
    box.setWindowTitle(tr("Load project"));
    box.setIcon(QMessageBox::Question);
    box.setText(text);
    box.setTextFormat(Qt::RichText);
    box.addButton("&Load the project (enter)", QMessageBox::AcceptRole);
    box.addButton("&Edit the file (escape)", QMessageBox::RejectRole);
    auto result = box.exec();

    if (result != 2) {
        // User did not choose "Load the project"
        project.reset();
        return false;
    }

    projectModel->addConfig(project);
    searchPanelUI->updateProjectList();
    auto buildDirectory = project->expand(project->buildDir);
    auto sourceDirectory = project->expand(project->sourceDir);

    projectDock->show();
    projectDock->raise();
    getManager()->saveSettings();

    // clang-format off
    getManager()->handleCommandAsync(GlobalCommands::ProjectLoaded, {
        {GlobalArguments::ProjectName, project->name },
        {GlobalArguments::SourceDirectory, sourceDirectory },
        {GlobalArguments::BuildDirectory, buildDirectory },
    });
    // clang-format on
    return true;
}

auto ProjectManagerPlugin::tryScrollOutput(int line) -> bool {

    auto browser = this->outputPanel->commandOuput;
    if (!browser) {
        return false;
    }

    const auto doc = browser->document();
    if (!doc) {
        return false;
    }

    if (line < 0 || line >= doc->blockCount()) {
        return false;
    }

    auto block = doc->findBlockByLineNumber(line);
    if (!block.isValid()) {
        return false;
    }

    auto cursor = QTextCursor(block);
    auto rect = browser->cursorRect(cursor);
    auto blockY = rect.top();
    auto viewportHeight = browser->viewport()->height();
    auto centerScrollValue = blockY - viewportHeight / 2;
    auto vScrollBar = browser->verticalScrollBar();
    centerScrollValue = qMax(0, qMin(centerScrollValue, vScrollBar->maximum()));
    vScrollBar->setValue(centerScrollValue);
    return true;
}

auto ProjectManagerPlugin::fixClientsMenu(qmdiClient *client, const QString &fileName) -> void {
    if (!client) {
        return;
    }

    // TODO - do we even need this argument?
    if (fileName.isEmpty()) {
        return;
    }

    auto action = client->contextMenu.findActionNamed("runScript");
    auto isScript = verifyRunnable(client->mdiClientFileName());
    if (isScript) {
        if (!action) {
            auto shortcut = QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_R);
            action = new QAction(tr("Run script"));
            action->setObjectName("runScript");
            action->setShortcut(shortcut);
            action->setData(QVariant::fromValue(client));
            client->contextMenu.addAction(action);
            client->menus[tr("&Project")]->addAction(action);

            connect(action, &QAction::triggered, action, [action, this]() {
                auto client = action->data().value<qmdiClient *>();
                if (!client->canCloseClient(CloseReason::CloseTab)) {
                    return;
                }
                auto fi = QFileInfo(client->mdiClientFileName());
                auto workingDir = fi.dir().absolutePath();
                auto scriptName = fi.absoluteFilePath();
                auto arguments = QStringList();
                auto env = QProcessEnvironment();
                auto capture = outputPanel ? outputPanel->captureTasksOutput->isChecked() : true;
                this->runCommand(workingDir, scriptName, arguments, env, capture);
            });

            if (this->mdiServer->getCurrentClient() == client) {
                this->mdiServer->mdiHost->unmergeClient(client);
                this->mdiServer->mdiHost->mergeClient(client);
                this->mdiServer->mdiHost->updateGUI();
            }
        }
    } else {
        client->contextMenu.removeAction(action);
        client->menus[tr("&Project")]->removeAction(action);
        if (this->mdiServer->getCurrentClient() == client) {
            this->mdiServer->mdiHost->unmergeClient(client);
            this->mdiServer->mdiHost->mergeClient(client);
            this->mdiServer->mdiHost->updateGUI();
        }
    }

    // This runs again on every save, not just on load - without the name lookup
    // each pass would add another copy of the action to the context menu.
    // qmdiActionGroup::addAction() de-duplicates by pointer, which a freshly
    // allocated QAction can never trip.
    if (client->contextMenu.findActionNamed("copyRelativePath")) {
        return;
    }
    auto widget = dynamic_cast<QObject *>(client);
    auto actionCopyFilePath = new QAction(tr("Copy path relative to project"), widget);
    actionCopyFilePath->setObjectName("copyRelativePath");
    connect(actionCopyFilePath, &QAction::triggered, actionCopyFilePath, [client, this]() {
        auto c = QApplication::clipboard();
        auto fileName = client->mdiClientFileName();
        auto project = projectModel->findProjectForFile(fileName);
        if (!project || !fileName.startsWith(project->sourceDir)) {
            c->setText(fileName);
            return;
        }
        auto fixed = fileName;
        auto l = project->sourceDir.length();
        if (!project->sourceDir.endsWith('\\') && !project->sourceDir.endsWith('/')) {
            l++;
        }
        fixed.remove(0, l);
        c->setText(fixed);
    });
    client->contextMenu.addAction(actionCopyFilePath);
}
