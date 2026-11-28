/**
 * \file QutepartEditorPlugin.h
 * \brief Makes the Qutepart editor backend available as a plugin.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "iplugin.h"
#include "plugins/texteditor/QutepartEditor/quteparteditorfactory.h"

class QutepartEditorPlugin : public IPlugin, public QutepartEditorFactory {
    Q_OBJECT
  public:
    QutepartEditorPlugin();
};
