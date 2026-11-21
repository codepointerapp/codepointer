/**
 * \file codeeditorfactory.h
 * \brief Factory for concrete code editor implementations.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include "codeeditorinterface.h"

/// Score magnitudes used when choosing an editor backend. Higher wins.
namespace EditorScore {
constexpr int CannotHandle = -1;
constexpr int Low = 5;
constexpr int Medium = 50;
constexpr int High = 100;
} // namespace EditorScore

/**
 * Creates editors for a specific backend and exposes the metadata the rest of
 * the application needs without knowing which backend is in use.
 */
class CodeEditorFactory {
  public:
    virtual ~CodeEditorFactory() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    /// How well this backend suits the given file. An empty file name means a
    /// new unnamed editor. Values <= 0 mean the backend cannot handle it.
    virtual int score(const QString &fileName) const = 0;
    virtual std::unique_ptr<CodeEditorInterface> createEditor(QWidget *parent) = 0;

    virtual QList<CodeEditor::LanguageInfo> languages() const = 0;
    virtual CodeEditor::LanguageInfo languageForFile(const QString &fileName) const = 0;
    virtual CodeEditor::LanguageInfo languageByName(const QString &name) const = 0;
    virtual QStringList fileExtensions() const = 0;
};
