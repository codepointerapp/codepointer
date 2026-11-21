/**
 * \file QutepartEditorPlugin.cpp
 * \brief Makes the Qutepart editor backend available as a plugin.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "QutepartEditorPlugin.h"

QutepartEditorPlugin::QutepartEditorPlugin() {
    name = QStringLiteral("QutepartEditor");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = QStringLiteral("0.0.1");
    autoEnabled = true;
    alwaysEnabled = true;

    config.pluginName = tr("Qutepart editor");
    config.description = tr("Full syntax-highlighting editor backend");
}
