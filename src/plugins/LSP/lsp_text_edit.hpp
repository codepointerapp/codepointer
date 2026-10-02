/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef LSP_TEXT_EDIT_H
#define LSP_TEXT_EDIT_H

#include <string>
#include <vector>

/// Applying LSP text edits to a file that has no editor open.
///
/// The rename path is the one place where an off-by-one silently corrupts a
/// file instead of failing visibly, so this is kept apart from the editor and
/// testable on its own. It is also deliberately free of Qt types: the
/// application takes and returns plain text, so the implementation can be
/// swapped for one that does not need a QTextDocument without the caller
/// changing. There is a Qt-backed implementation today (see
/// lsp_text_edit_qt.cpp) because reusing the tested lspRename geometry is worth
/// more than avoiding the dependency, and a std::string one to follow.
namespace lspTextEdit {

/// One replacement, with both ranges 0-based and end-exclusive exactly as LSP
/// states them, and stated against the text as it was before any edit applied.
struct Edit {
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    std::string newText;
};

/// What happened to the edits handed to apply().
struct Result {
    /// True when the text was rewritten. False means the input is returned
    /// unchanged, which is the safe outcome: a reply we cannot place is worse
    /// than a reply we decline.
    bool applied = false;
    /// The rewritten text. Equal to the input when applied is false.
    std::string text;
    /// One line per edit that was refused, for logging. Empty on success.
    std::vector<std::string> rejected;
};

/// Applies `edits` to `text`, bottom-up, and returns the result.
///
/// When `expectedOldName` is not empty every range must still cover exactly
/// that identifier, and if any does not, none are applied. The server computed
/// every range against one snapshot, so if the file moved on while it worked
/// they are all stale rather than only the unlucky ones; applying the subset
/// that still lines up renames the symbol in some places and not others, which
/// is a file that no longer compiles. This is what makes a reply for a
/// different revision harmless instead of half-applied.
///
/// `edits` must be sorted bottom-up and stated against `text` as it was before
/// any of them applied, the same contract the editor path has. Ranges that do
/// not map onto the text are refused rather than clamped, so a bogus response
/// changes less rather than more.
Result apply(const std::string &text, const std::vector<Edit> &edits,
             const std::string &expectedOldName = {});

} // namespace lspTextEdit

#endif // LSP_TEXT_EDIT_H
