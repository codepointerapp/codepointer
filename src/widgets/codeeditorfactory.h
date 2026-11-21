/**
 * \file codeeditorfactory.h
 * \brief Factory for concrete code editor implementations.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include "codeeditorinterface.h"

/**
 * Creates editors for a specific backend and exposes the metadata the rest of
 * the application needs without knowing which backend is in use.
 */
class CodeEditorFactory {
  public:
    virtual ~CodeEditorFactory() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual std::unique_ptr<CodeEditorInterface> createEditor(QWidget *parent) = 0;

    virtual QList<CodeEditor::LanguageInfo> languages() const = 0;
    virtual CodeEditor::LanguageInfo languageForFile(const QString &fileName) const = 0;
    virtual CodeEditor::LanguageInfo languageByName(const QString &name) const = 0;
    virtual QStringList fileExtensions() const = 0;
};
