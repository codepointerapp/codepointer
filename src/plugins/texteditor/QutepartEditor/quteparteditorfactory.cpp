/**
 * \file quteparteditorfactory.cpp
 * \brief CodeEditorFactory implementation for Qutepart.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QCoreApplication>

#include <qutepart/qutepart.h>
#include <qutepart/theme.h>

#include "quteparteditor.h"
#include "quteparteditorfactory.h"

namespace Qutepart {
extern QMap<QString, QString> languageNameToXmlFileName;
}

static auto fromQutepartLanguage(const Qutepart::LangInfo &langInfo) -> CodeEditor::LanguageInfo {
    auto result = CodeEditor::LanguageInfo();
    result.id = langInfo.id;
    result.names = langInfo.names;
    result.indentAlgorithm = static_cast<CodeEditor::IndentAlgorithm>(langInfo.indentAlg);
    return result;
}

QString QutepartEditorFactory::id() const { return QStringLiteral("qutepart"); }

QString QutepartEditorFactory::displayName() const { return QStringLiteral("Qutepart"); }

std::unique_ptr<CodeEditorInterface> QutepartEditorFactory::createEditor(QWidget *parent) {
    return std::make_unique<QutepartCodeEditor>(parent);
}

QList<CodeEditor::LanguageInfo> QutepartEditorFactory::languages() const {
    auto result = QList<CodeEditor::LanguageInfo>();
    auto plainText = CodeEditor::LanguageInfo();
    plainText.id = QStringLiteral("Plain text");
    plainText.names = {QCoreApplication::translate("QutepartEditorFactory", "Plain text")};
    plainText.indentAlgorithm = CodeEditor::IndentAlgorithm::None;
    result.append(plainText);

    auto const names = Qutepart::languageNameToXmlFileName.keys();
    for (auto const &name : names) {
        auto langInfo = Qutepart::chooseLanguage({}, name, {});
        if (langInfo.isValid()) {
            result.append(fromQutepartLanguage(langInfo));
        }
    }
    return result;
}

CodeEditor::LanguageInfo
QutepartEditorFactory::languageForFile(const QString &fileName) const {
    auto langInfo = Qutepart::chooseLanguage({}, {}, fileName);
    if (!langInfo.isValid()) {
        return {};
    }
    return fromQutepartLanguage(langInfo);
}

CodeEditor::LanguageInfo QutepartEditorFactory::languageByName(const QString &name) const {
    if (name.compare("Plain text", Qt::CaseInsensitive) == 0) {
        auto plainText = CodeEditor::LanguageInfo();
        plainText.id = QStringLiteral("Plain text");
        plainText.names = {QCoreApplication::translate("QutepartEditorFactory", "Plain text")};
        plainText.indentAlgorithm = CodeEditor::IndentAlgorithm::None;
        return plainText;
    }

    auto langInfo = Qutepart::chooseLanguage({}, name, {});
    if (!langInfo.isValid()) {
        return {};
    }
    return fromQutepartLanguage(langInfo);
}

QStringList QutepartEditorFactory::fileExtensions() const { return {}; }
