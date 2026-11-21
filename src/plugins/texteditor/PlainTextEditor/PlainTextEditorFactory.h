/**
 * \file PlainTextEditorFactory.h
 * \brief CodeEditorFactory implementation for the minimal QPlainTextEdit editor.
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/codeeditorfactory.h"

class PlainTextEditorFactory : public CodeEditorFactory {
  public:
    QString id() const override;
    QString displayName() const override;
    std::unique_ptr<CodeEditorInterface> createEditor(QWidget *parent) override;

    QList<CodeEditor::LanguageInfo> languages() const override;
    CodeEditor::LanguageInfo languageForFile(const QString &fileName) const override;
    CodeEditor::LanguageInfo languageByName(const QString &name) const override;
    QStringList fileExtensions() const override;
};
