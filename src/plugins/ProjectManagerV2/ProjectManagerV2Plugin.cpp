/**
 * \file ProjectManagerV2Plugin.cpp
 * \brief Project manager plugin, V2
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * \see PluginManager
 */

// SPDX-License-Identifier: MIT

#include <QAbstractListModel>
#include <QDir>
#include <QFileDialog>

#include "ProjectDefinition.hpp"
#include "ProjectManagerV2Plugin.hpp"
#include "ProjectsDock.hpp"

class ProjectDefinitionModel : public QAbstractListModel {
    QList<std::shared_ptr<ProjectDefinition>> projects;

  public:
    void addProject(std::shared_ptr<ProjectDefinition> project) { projects.append(project); }
    void removeConfig(std::shared_ptr<ProjectDefinition> project) {
        auto i = projects.indexOf(project);
        if (i >= 0) {
            projects.remove(i);
        }
    }

    virtual int rowCount(const QModelIndex &) const override { return projects.count(); }

    virtual QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) {
            return {};
        }
        auto project = projects[index.row()];
        switch (role) {
        case Qt::DisplayRole:
            return project->name;
        case Qt::ToolTipRole:
            return QDir::toNativeSeparators(project->sourceDir);
        default:
            break;
        }
        return {};
    }

    // QStringList getAllOpenDirs() const;
};

ProjectManagerV2Plugin::ProjectManagerV2Plugin() {
    name = "ProjectManagerV2";
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 2;
    sVersion = "2.0.1";
    autoEnabled = true;
    alwaysEnabled = true;

    auto values = QStringList() << tr("Never") << tr("Always") << tr("Loaded projects");
    config.pluginName = tr("Project manager V2");
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
}

ProjectManagerV2Plugin::~ProjectManagerV2Plugin() {
    // TODO
}

void ProjectManagerV2Plugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);
    auto manager = dynamic_cast<PluginManager *>(host);

    dockGUI = new ProjectsDock(manager);
    model = new ProjectDefinitionModel();
    dockGUI->setProjectsModel(model);
    // auto projectDock =
    manager->createNewPanel(Panels::East, "ProjectManagerV2", tr("Project v2"), dockGUI);

    connect(dockGUI, &ProjectsDock::newProjectRequested, dockGUI, [this]() {
        auto manager = getManager();
        auto dirName = QFileDialog::getExistingDirectory(manager, tr("Add directory"));
        auto projects = ProjectDefinition::findProjects(dirName);

        for (auto p : projects) {
            model->addProject(p);
        }
        if (!projects.empty()) {
            dockGUI->selectProject(projects.last());
        }

        manager->saveSettings();
    });
    connect(dockGUI, &ProjectsDock::projectRemovalRequested, dockGUI, [](auto project, auto index) {
        // TODO
    });
    connect(dockGUI, &ProjectsDock::newProjectSelected, dockGUI, [](auto project, auto index) {
        // TODO
    });
}

void ProjectManagerV2Plugin::configurationHasBeenModified() {
    // TODO
}

void ProjectManagerV2Plugin::loadConfig(QSettings &settings) {
    name = "ProjectManager";
    IPlugin::loadConfig(settings);
    name = "ProjectManagerV2";

    /*
        searchPanelUI->setSearchPath(getConfig().getSearchPath());
        searchPanelUI->setSearchPattern(getConfig().getSearchPattern());
        searchPanelUI->setSearchInclude(getConfig().getSearchInclude());
        searchPanelUI->setSearchExclude(getConfig().getSearchExclude());
        searchPanelUI->setCollapseFiles(getConfig().getSearchCollapseFileNames());
        searchPanelUI->setSearchWholeWords(getConfig().getSearchWholeWords());
        searchPanelUI->setSearchRegex(getConfig().getSearchRegex());
        searchPanelUI->setSearchCaseSensitive(getConfig().getSearchSensitive());
    */

    auto dirsToLoad = getConfig().getOpenProjects();
    for (auto const &d : std::as_const(dirsToLoad)) {
        addProjectFromDir(d);
    }
    auto selectedDirectory = getConfig().getSelectedProject();
    // TODO - select current project in the GUI
}

void ProjectManagerV2Plugin::saveConfig(QSettings &settings) {
    IPlugin::saveConfig(settings);
    // TODO
}

int ProjectManagerV2Plugin::canHandleAsyncCommand(const QString &command,
                                                  const CommandArgs &args) const {
    // TODO
    return CommandPriority::CannotHandle;
}

QFuture<CommandArgs> ProjectManagerV2Plugin::handleCommandAsync(const QString &command,
                                                                const CommandArgs &args) {
    // TODO
    return {};
}

qmdiActionGroup *ProjectManagerV2Plugin::getContextMenuActions(const QString &menuId,
                                                               const QString &filePath) {
    // TODO
    return nullptr;
}

void ProjectManagerV2Plugin::addProjectFromDir(const QString &projectDir) {
    // TODO maintain list of projects
    // TODO notifiy GUI about loaded project
}
