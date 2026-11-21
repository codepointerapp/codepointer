/**
 * \file QutepartEditorPlugin.hpp
 * \brief Editor backend plugin exposing the Qutepart editor factory.
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "iplugin.h"

class CodeEditorFactory;

/**
 * Wraps the original Qutepart backed editor as a plugin.
 *
 * The factory is handed to TextEditorPlugin, which takes ownership of it.
 */
class QutepartEditorPlugin : public IPlugin {
    Q_OBJECT
  public:
    QutepartEditorPlugin();
    ~QutepartEditorPlugin() override;

    CodeEditorFactory *factory() const;

  private:
    CodeEditorFactory *editorFactory;
};
