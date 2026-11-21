/**
 * \file CodePointerPluginManager.hpp
 * \brief PluginManager with read-only access to the registered plugins.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QString>

#include "iplugin.h"
#include "pluginmanager.h"

/**
 * The library keeps its plugin list protected and only scores plugins while
 * opening a file. Editor backends need the same information when creating or
 * reopening a buffer, so expose it here instead of patching the library.
 */
class CodePointerPluginManager : public PluginManager {
  public:
    using PluginManager::PluginManager;

    QList<IPlugin *> enabledPlugins() const {
        auto result = QList<IPlugin *>();
        for (auto *plugin : plugins) {
            if (plugin && plugin->isEnabled()) {
                result.append(plugin);
            }
        }
        return result;
    }

    IPlugin *bestPluginForFile(const QString &fileName) const {
        IPlugin *best = nullptr;
        auto bestScore = -1;
        for (auto *plugin : enabledPlugins()) {
            auto score = plugin->canOpenFile(fileName);
            if (score > bestScore) {
                best = plugin;
                bestScore = score;
            }
        }
        return best;
    }
};
