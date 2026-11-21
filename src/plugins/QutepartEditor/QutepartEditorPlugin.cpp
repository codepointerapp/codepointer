/**
 * \file QutepartEditorPlugin.cpp
 * \brief Editor backend plugin exposing the Qutepart editor factory.
 */

// SPDX-License-Identifier: MIT

#include "QutepartEditorPlugin.hpp"

#include "quteparteditorfactory.h"

QutepartEditorPlugin::QutepartEditorPlugin() : editorFactory(new QutepartEditorFactory) {
    name = tr("Qutepart editor backend");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = true;

    config.pluginName = tr("Qutepart editor backend");
    config.description = tr("Provides the full editor backed by Qutepart.");
}

QutepartEditorPlugin::~QutepartEditorPlugin() = default;

CodeEditorFactory *QutepartEditorPlugin::factory() const { return editorFactory; }
