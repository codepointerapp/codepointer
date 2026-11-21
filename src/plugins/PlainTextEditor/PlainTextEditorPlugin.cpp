/**
 * \file PlainTextEditorPlugin.cpp
 * \brief Editor backend plugin exposing the minimal QPlainTextEdit factory.
 */

// SPDX-License-Identifier: MIT

#include "PlainTextEditorPlugin.hpp"

#include "PlainTextEditorFactory.h"

PlainTextEditorPlugin::PlainTextEditorPlugin() : editorFactory(new PlainTextEditorFactory) {
    name = tr("Plain text editor backend");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = true;

    config.pluginName = tr("Plain text editor backend");
    config.description = tr("Provides a basic editor backed by QPlainTextEdit.");
}

PlainTextEditorPlugin::~PlainTextEditorPlugin() = default;

CodeEditorFactory *PlainTextEditorPlugin::factory() const { return editorFactory; }
