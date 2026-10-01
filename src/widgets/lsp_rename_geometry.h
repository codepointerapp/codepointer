/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef LSP_RENAME_GEOMETRY_H
#define LSP_RENAME_GEOMETRY_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

/// The geometry behind an LSP rename: turning the position the user clicked, and
/// the ranges a language server sends back, into edits on a text document.
///
/// These are free functions over a document rather than members of qmdiEditor on
/// purpose. The rename path is the one place where an off-by-one silently
/// corrupts the user's file rather than failing visibly, and this way it can be
/// covered by tests without standing up a widget, a plugin host and a language
/// server. Nothing here touches global state, so the tests need no fixtures.
namespace lspRename {

/// One replacement, with both ranges 0-based and end-exclusive, as LSP states
/// them, and stated against the document as it was before any edit applied.
struct Edit {
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    QString newText;
};

/// The identifier (letters, digits, underscores) spanning the 0-based
/// (line, column) in `text`, or empty when the column is not over one. Used to
/// pre-fill the inline rename box with the symbol's current name.
inline QString identifierAt(const QString &text, int line, int column) {
    auto lines = text.split(QChar::LineFeed);
    if (line < 0 || line >= lines.size() || column < 0 || column > lines.at(line).size()) {
        return {};
    }
    auto const &s = lines.at(line);
    auto isIdent = [](QChar c) { return c.isLetterOrNumber() || c == QChar('_'); };
    auto start = column;
    while (start > 0 && isIdent(s.at(start - 1))) {
        --start;
    }
    auto end = column;
    while (end < s.size() && isIdent(s.at(end))) {
        ++end;
    }
    return s.mid(start, end - start);
}

/// The document offset of a 0-based LSP (line, character) position, or -1 when
/// the position is not in the document.
///
/// LSP character offsets are end-exclusive and may legally sit one past the last
/// character of a line, so the valid range is [0, line length]. Anything outside
/// that - a negative index, or one past the end of the line - is rejected rather
/// than clamped. Clamping is what turned a stale server reply into a corrupted
/// file: a negative column silently became column 0, so the replacement was
/// pasted at the start of the line and the original symbol was left behind.
inline int positionOf(const QTextDocument *document, int line, int character) {
    if (!document || line < 0 || character < 0) {
        return -1;
    }
    auto block = document->findBlockByNumber(line);
    if (!block.isValid()) {
        return -1;
    }
    auto lineLength = static_cast<int>(block.text().size());
    if (character > lineLength) {
        return -1;
    }
    return block.position() + character;
}

/// Applies `edits` to `document` as one undoable action, and returns how many
/// were applied.
///
/// `edits` must be sorted bottom-up. Every range is stated against the document
/// before any edit applied, so applying top-down would invalidate the ranges
/// still to come; the caller sorts, and this applies in the order given. Edits
/// whose range does not map onto the document are skipped rather than clamped
/// silently, so a stale or bogus server response changes less rather than more.
inline int applyEdits(QTextDocument *document, const QList<Edit> &edits) {
    if (!document || edits.isEmpty()) {
        return 0;
    }

    auto cursor = QTextCursor(document);
    auto applied = 0;
    cursor.beginEditBlock();
    for (auto const &edit : edits) {
        auto from = positionOf(document, edit.startLine, edit.startCharacter);
        auto to = positionOf(document, edit.endLine, edit.endCharacter);
        if (from < 0 || to < 0 || to < from) {
            continue;
        }
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        cursor.insertText(edit.newText);
        applied++;
    }
    cursor.endEditBlock();
    return applied;
}

/// Applies a rename's edits, refusing the whole reply unless every one of its
/// ranges covers `oldName`.
///
/// A rename is the one positional request where the caller knows what text every
/// returned edit is supposed to replace: the symbol being renamed. Checking that
/// turns a server answering about a stale document - whose ranges are well-formed
/// but name something else - into a refusal instead of a corrupted file.
///
/// The check is all or nothing, and that is the point. The server computed every
/// range against one snapshot, so a reply that is stale anywhere is stale
/// throughout. The ranges that still happen to line up with the current buffer are
/// a coincidence, not permission: applying those and skipping the rest renames the
/// symbol in some places and not others, which is a file that no longer compiles -
/// strictly worse than leaving it alone. So every range is validated before
/// anything is written.
///
/// `rejected`, when given, receives one note per edit that failed, for diagnostics.
/// The notes carry the range as the server stated it, which is what identifies the
/// divergence.
inline int applyRenameEdits(QTextDocument *document, const QList<Edit> &edits,
                            const QString &oldName, QStringList *rejected = nullptr) {
    if (!document || edits.isEmpty()) {
        return 0;
    }

    // Pass one: validate every range against the document as it stands. Nothing is
    // written yet, so each range is measured against the same text.
    auto ranges = QList<QPair<int, int>>();
    ranges.reserve(edits.size());
    auto allValid = true;

    for (auto const &edit : edits) {
        auto from = positionOf(document, edit.startLine, edit.startCharacter);
        auto to = positionOf(document, edit.endLine, edit.endCharacter);
        if (from < 0 || to < 0 || to < from) {
            if (rejected) {
                rejected->append(QStringLiteral("%1:%2-%3:%4 is outside the document")
                                     .arg(edit.startLine)
                                     .arg(edit.startCharacter)
                                     .arg(edit.endLine)
                                     .arg(edit.endCharacter));
            }
            allValid = false;
            continue;
        }

        // Read the covered text without disturbing a cursor. selectedText() uses
        // U+2029 for paragraph breaks; an identifier contains none, so a range
        // spanning lines cannot match and is refused.
        QTextCursor reader(document);
        reader.setPosition(from);
        reader.setPosition(to, QTextCursor::KeepAnchor);
        if (reader.selectedText() != oldName) {
            if (rejected) {
                rejected->append(QStringLiteral("%1:%2 covers '%3', not '%4'")
                                     .arg(edit.startLine)
                                     .arg(edit.startCharacter)
                                     .arg(reader.selectedText(), oldName));
            }
            allValid = false;
            continue;
        }

        ranges.append({from, to});
    }

    if (!allValid) {
        return 0;
    }

    // Pass two: every range checked out, so they are consistent with each other and
    // can be written in the bottom-up order the caller sorted them in.
    auto cursor = QTextCursor(document);
    cursor.beginEditBlock();
    for (auto i = 0; i < ranges.size(); ++i) {
        cursor.setPosition(ranges.at(i).first);
        cursor.setPosition(ranges.at(i).second, QTextCursor::KeepAnchor);
        cursor.insertText(edits.at(i).newText);
    }
    cursor.endEditBlock();
    return static_cast<int>(ranges.size());
}

} // namespace lspRename

#endif // LSP_RENAME_GEOMETRY_H
