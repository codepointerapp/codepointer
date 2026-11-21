/**
 * \file PlainTextEditorPlugin.cpp
 * \brief Makes the plain QPlainTextEdit backend available as a plugin.
 */

// SPDX-License-Identifier: MIT

#include "PlainTextEditorPlugin.h"

PlainTextEditorPlugin::PlainTextEditorPlugin() {
    name = QStringLiteral("PlainTextEditor");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = QStringLiteral("0.0.1");
    autoEnabled = true;
    alwaysEnabled = true;

    config.pluginName = tr("Plain text editor");
    config.description = tr("Minimal editor backend without syntax highlighting");
}
