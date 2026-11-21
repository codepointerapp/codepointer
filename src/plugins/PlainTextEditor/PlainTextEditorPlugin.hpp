/**
 * \file PlainTextEditorPlugin.hpp
 * \brief Editor backend plugin exposing the minimal QPlainTextEdit factory.
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "iplugin.h"

class CodeEditorFactory;

/**
 * Provides a small, dependency-free editor backend.
 *
 * The factory is handed to TextEditorPlugin, which takes ownership of it.
 */
class PlainTextEditorPlugin : public IPlugin {
    Q_OBJECT
  public:
    PlainTextEditorPlugin();
    ~PlainTextEditorPlugin() override;

    CodeEditorFactory *factory() const;

  private:
    CodeEditorFactory *editorFactory;
};
