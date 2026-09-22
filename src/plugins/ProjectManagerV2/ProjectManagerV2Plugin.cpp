/**
 * \file ProjectManagerV2Plugin.cpp
 * \brief Project manager plugin, V2
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * \see PluginManager
 */

// SPDX-License-Identifier: MIT

#include "ProjectManagerV2Plugin.hpp"

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
    // auto manager = dynamic_cast<PluginManager *>(host);
}

void ProjectManagerV2Plugin::configurationHasBeenModified() {
    // TODO
}

void ProjectManagerV2Plugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);

    /*
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
    */
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
