/**
 * \file PlainTextEditorPlugin.h
 * \brief Makes the plain QPlainTextEdit backend available as a plugin.
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "iplugin.h"
#include "plugins/texteditor/PlainTextEditor/PlainTextEditorFactory.h"

class PlainTextEditorPlugin : public IPlugin, public PlainTextEditorFactory {
    Q_OBJECT
  public:
    PlainTextEditorPlugin();
};
