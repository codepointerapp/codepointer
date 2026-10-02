/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "lsp_text_edit.hpp"

#include <QString>
#include <QTextDocument>

#include "widgets/lsp_rename_geometry.h"

/// The Qt-backed implementation of lspTextEdit::apply.
///
/// It exists because the geometry - turning a line and column into an offset,
/// refusing a range that no longer covers the symbol, applying bottom-up so
/// earlier ranges stay valid - is already written and tested in lspRename, and
/// reimplementing it here to avoid QTextDocument would mean two copies of the
/// part of this that must not be wrong. The dependency is confined to this one
/// file: the header the caller sees speaks only in std::string, so replacing
/// this with a std::string implementation later touches nothing else.
namespace lspTextEdit {

auto apply(const std::string &text, const std::vector<Edit> &edits,
           const std::string &expectedOldName) -> Result {
    auto result = Result{};
    result.text = text;
    if (edits.empty()) {
        return result;
    }

    // A plain document, with no widget attached: this runs for files that were
    // never opened, so there is no editor and no cursor to preserve.
    auto document = QTextDocument();
    document.setPlainText(QString::fromStdString(text));

    auto converted = QList<lspRename::Edit>();
    converted.reserve(static_cast<int>(edits.size()));
    for (auto const &edit : edits) {
        converted.append(lspRename::Edit{edit.startLine, edit.startCharacter, edit.endLine,
                                         edit.endCharacter, QString::fromStdString(edit.newText)});
    }

    auto rejected = QStringList();
    auto applied =
        expectedOldName.empty()
            ? lspRename::applyEdits(&document, converted)
            : lspRename::applyRenameEdits(&document, converted,
                                          QString::fromStdString(expectedOldName), &rejected);

    // Carried back whether or not anything applied. A refusal is the case where
    // the caller most needs to be able to say why, and it is the one case where
    // dropping the detail would leave it guessing.
    for (auto const &reason : rejected) {
        result.rejected.push_back(reason.toStdString());
    }
    if (applied <= 0) {
        return result;
    }

    result.applied = true;
    result.text = document.toPlainText().toStdString();
    return result;
}

} // namespace lspTextEdit
