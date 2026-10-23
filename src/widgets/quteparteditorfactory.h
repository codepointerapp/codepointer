/**
 * \file quteparteditorfactory.h
 * \brief CodeEditorFactory implementation for Qutepart.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>

#include "codeeditorfactory.h"

class QutepartEditorFactory : public CodeEditorFactory {
  public:
    QutepartEditorFactory();

    QString id() const override;
    QString displayName() const override;
    std::unique_ptr<CodeEditorInterface> createEditor(QWidget *parent) override;

    QList<CodeEditor::LanguageInfo> languages() const override;
    CodeEditor::LanguageInfo languageForFile(const QString &fileName) const override;
    CodeEditor::LanguageInfo languageByName(const QString &name) const override;
    QStringList fileExtensions() const override;

    QStringList themes() const override;
    QString themeDisplayName(const QString &themeFile) const override;

  private:
    QHash<QString, QString> themeNames;
};
