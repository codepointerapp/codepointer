/**
 * \file quteparteditorfactory.cpp
 * \brief CodeEditorFactory implementation for Qutepart.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>

#include <qutepart/qutepart.h>
#include <qutepart/theme.h>

#include "quteparteditor.h"
#include "quteparteditorfactory.h"

namespace Qutepart {
extern QMap<QString, QString> languageNameToXmlFileName;
} // namespace Qutepart

namespace {
QList<CodeEditor::LanguageInfo> allLanguagesCache;
QHash<QString, CodeEditor::LanguageInfo> languagesByFileCache;
QHash<QString, CodeEditor::LanguageInfo> languagesByNameCache;
} // namespace

static auto fromQutepartLanguage(const Qutepart::LangInfo &langInfo) -> CodeEditor::LanguageInfo {
    auto result = CodeEditor::LanguageInfo();
    result.id = langInfo.id;
    result.names = langInfo.names;
    result.indentAlgorithm = static_cast<CodeEditor::IndentAlgorithm>(langInfo.indentAlg);
    return result;
}

static auto isPlainTextFileName(const QString &fileName) -> bool {
    auto const info = QFileInfo(fileName);
    static const QStringList plainTextNames = {
        QStringLiteral("readme"),   QStringLiteral("license"),  QStringLiteral("copying"),
        QStringLiteral("makefile"), QStringLiteral("doxyfile"), QStringLiteral("cmakelists.txt"),
    };
    if (plainTextNames.contains(info.fileName().toLower())) {
        return true;
    }

    static const QStringList plainTextSuffixes = {QStringLiteral("txt"), QStringLiteral("text")};
    return plainTextSuffixes.contains(info.suffix().toLower());
}

QString QutepartEditorFactory::id() const { return QStringLiteral("qutepart"); }

QString QutepartEditorFactory::displayName() const { return QStringLiteral("Qutepart"); }

int QutepartEditorFactory::score(const QString &fileName) const {
    if (fileName.isEmpty()) {
        return EditorScore::Low;
    }

    auto const language = languageForFile(fileName);
    if (!language.isValid()) {
        return isPlainTextFileName(fileName) ? EditorScore::Medium : 0;
    }
    return language.id == QStringLiteral("Plain text") ? EditorScore::Medium : EditorScore::High;
}

std::unique_ptr<CodeEditorInterface> QutepartEditorFactory::createEditor(QWidget *parent) {
    return std::make_unique<QutepartCodeEditor>(parent);
}

QList<CodeEditor::LanguageInfo> QutepartEditorFactory::languages() const {
    if (!allLanguagesCache.isEmpty()) {
        return allLanguagesCache;
    }

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
    allLanguagesCache = result;
    return result;
}

CodeEditor::LanguageInfo
QutepartEditorFactory::languageForFile(const QString &fileName) const {
    auto cached = languagesByFileCache.constFind(fileName);
    if (cached != languagesByFileCache.cend()) {
        return cached.value();
    }

    auto result = CodeEditor::LanguageInfo();
    auto const langInfo = Qutepart::chooseLanguage({}, {}, fileName);
    if (langInfo.isValid()) {
        result = fromQutepartLanguage(langInfo);
    }
    languagesByFileCache.insert(fileName, result);
    return result;
}

CodeEditor::LanguageInfo QutepartEditorFactory::languageByName(const QString &name) const {
    auto cached = languagesByNameCache.constFind(name);
    if (cached != languagesByNameCache.cend()) {
        return cached.value();
    }

    auto result = CodeEditor::LanguageInfo();
    if (name.compare("Plain text", Qt::CaseInsensitive) == 0) {
        result.id = QStringLiteral("Plain text");
        result.names = {QCoreApplication::translate("QutepartEditorFactory", "Plain text")};
        result.indentAlgorithm = CodeEditor::IndentAlgorithm::None;
    } else {
        auto const langInfo = Qutepart::chooseLanguage({}, name, {});
        if (langInfo.isValid()) {
            result = fromQutepartLanguage(langInfo);
        }
    }
    languagesByNameCache.insert(name, result);
    return result;
}

QStringList QutepartEditorFactory::fileExtensions() const { return {}; }
