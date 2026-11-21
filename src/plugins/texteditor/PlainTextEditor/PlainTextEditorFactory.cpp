/**
 * \file PlainTextEditorFactory.cpp
 * \brief CodeEditorFactory implementation for the minimal QPlainTextEdit editor.
 */

// SPDX-License-Identifier: MIT

#include <QCoreApplication>

#include "PlainTextEditor.h"
#include "PlainTextEditorFactory.h"

namespace {
CodeEditor::LanguageInfo plainTextLanguage() {
    auto info = CodeEditor::LanguageInfo();
    info.id = QStringLiteral("Plain text");
    info.names = {QCoreApplication::translate("PlainTextEditorFactory", "Plain text")};
    info.indentAlgorithm = CodeEditor::IndentAlgorithm::None;
    return info;
}
} // namespace

QString PlainTextEditorFactory::id() const { return QStringLiteral("plain"); }

QString PlainTextEditorFactory::displayName() const {
    return QCoreApplication::translate("PlainTextEditorFactory", "Plain text");
}

std::unique_ptr<CodeEditorInterface> PlainTextEditorFactory::createEditor(QWidget *parent) {
    return std::make_unique<PlainTextCodeEditor>(parent);
}

QList<CodeEditor::LanguageInfo> PlainTextEditorFactory::languages() const {
    return {plainTextLanguage()};
}

CodeEditor::LanguageInfo PlainTextEditorFactory::languageForFile(const QString &fileName) const {
    Q_UNUSED(fileName)
    return plainTextLanguage();
}

CodeEditor::LanguageInfo PlainTextEditorFactory::languageByName(const QString &name) const {
    if (name.isEmpty() || name.compare(QStringLiteral("Plain text"), Qt::CaseInsensitive) == 0) {
        return plainTextLanguage();
    }
    return {};
}

QStringList PlainTextEditorFactory::fileExtensions() const { return {}; }

QStringList PlainTextEditorFactory::themes() const { return {}; }

QString PlainTextEditorFactory::themeDisplayName(const QString &themeFile) const {
    if (themeFile.isEmpty()) {
        return QCoreApplication::translate("PlainTextEditorFactory", "Default");
    }
    return {};
}
