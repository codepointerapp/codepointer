/**
 * \file PlainTextEditor.cpp
 * \brief Minimal CodeEditorInterface implementation backed by QPlainTextEdit.
 */

// SPDX-License-Identifier: MIT

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolTip>

#include "PlainTextEditor.h"
#include "widgets/lsp_rename_geometry.h"
#include "widgets/qttextoperationsadapter.h"

PlainTextCodeEditor::PlainTextCodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        refreshDiagnostics();
        if (textChangedCallback) {
            textChangedCallback();
        }
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (cursorPositionChangedCallback) {
            cursorPositionChangedCallback();
        }
    });
    connect(this, &QPlainTextEdit::undoAvailable, this, [this](bool available) {
        if (undoAvailableCallback) {
            undoAvailableCallback(available);
        }
    });
    connect(this, &QPlainTextEdit::redoAvailable, this, [this](bool available) {
        if (redoAvailableCallback) {
            redoAvailableCallback(available);
        }
    });

    setFrameVisible(false);
    createActions();
}

TextOperationsAdapter *PlainTextCodeEditor::createOperationsAdapter(QObject *parent) {
    return new QtTextOperationsAdapter(this, parent);
}

void PlainTextCodeEditor::setTextChangedCallback(std::function<void()> callback) {
    textChangedCallback = std::move(callback);
}

void PlainTextCodeEditor::setCursorPositionChangedCallback(std::function<void()> callback) {
    cursorPositionChangedCallback = std::move(callback);
}

void PlainTextCodeEditor::setUndoAvailableCallback(std::function<void(bool)> callback) {
    undoAvailableCallback = std::move(callback);
}

void PlainTextCodeEditor::setRedoAvailableCallback(std::function<void(bool)> callback) {
    redoAvailableCallback = std::move(callback);
}

QString PlainTextCodeEditor::text() const { return toPlainText(); }

QChar PlainTextCodeEditor::characterAt(int position) const {
    return document() ? document()->characterAt(position) : QChar();
}

void PlainTextCodeEditor::setText(const QString &text) { setPlainText(text); }

void PlainTextCodeEditor::clear() { QPlainTextEdit::clear(); }

bool PlainTextCodeEditor::isEmpty() const { return !document() || document()->isEmpty(); }

bool PlainTextCodeEditor::isModified() const { return document() && document()->isModified(); }

void PlainTextCodeEditor::setModified(bool modified) {
    if (document()) {
        document()->setModified(modified);
    }
}

bool PlainTextCodeEditor::isReadOnly() const { return QPlainTextEdit::isReadOnly(); }

void PlainTextCodeEditor::setReadOnly(bool readOnly) { QPlainTextEdit::setReadOnly(readOnly); }

bool PlainTextCodeEditor::canUndo() const { return document() && document()->isUndoAvailable(); }

bool PlainTextCodeEditor::canRedo() const { return document() && document()->isRedoAvailable(); }

void PlainTextCodeEditor::undo() { QPlainTextEdit::undo(); }

void PlainTextCodeEditor::redo() { QPlainTextEdit::redo(); }

void PlainTextCodeEditor::copy() { QPlainTextEdit::copy(); }

void PlainTextCodeEditor::cut() { QPlainTextEdit::cut(); }

void PlainTextCodeEditor::paste() { QPlainTextEdit::paste(); }

void PlainTextCodeEditor::setUndoRedoEnabled(bool enabled) {
    QPlainTextEdit::setUndoRedoEnabled(enabled);
}

void PlainTextCodeEditor::removeModifications() { setModified(false); }

void PlainTextCodeEditor::replaceText(const QString &text, bool undoable) {
    if (!document()) {
        return;
    }

    auto undoWasEnabled = document()->isUndoRedoEnabled();
    if (!undoable) {
        document()->setUndoRedoEnabled(false);
    }

    auto cursor = QTextCursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();

    if (!undoable) {
        document()->setUndoRedoEnabled(undoWasEnabled);
    }
}

void PlainTextCodeEditor::replaceSelectedText(const QString &text) {
    auto cursor = textCursor();
    if (!cursor.hasSelection()) {
        return;
    }
    cursor.beginEditBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void PlainTextCodeEditor::trimTrailingSpaces() {
    if (!document()) {
        return;
    }

    static const auto trailingSpaces = QRegularExpression("\\s+$");
    auto cursor = QTextCursor(document());
    cursor.beginEditBlock();
    for (auto block = document()->begin(); block != document()->end();) {
        auto currentText = block.text();
        currentText.remove(trailingSpaces);
        if (currentText != block.text()) {
            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.insertText(currentText);
            block = cursor.block();
        } else {
            block = block.next();
        }
    }
    cursor.endEditBlock();
}

bool PlainTextCodeEditor::applyTextEdits(const QList<CodeEditor::TextEdit> &edits,
                                         const QString &expectedOldName) {
    if (edits.isEmpty() || !document()) {
        return false;
    }

    auto converted = QList<lspRename::Edit>();
    converted.reserve(edits.size());
    for (auto const &edit : edits) {
        converted.append(lspRename::Edit{edit.startLine, edit.startCharacter, edit.endLine,
                                         edit.endCharacter, edit.newText});
    }

    if (expectedOldName.isEmpty()) {
        return lspRename::applyEdits(document(), converted) > 0;
    }

    auto rejected = QStringList();
    return lspRename::applyRenameEdits(document(), converted, expectedOldName, &rejected) > 0;
}

bool PlainTextCodeEditor::findText(const QString &text) {
    if (!document()) {
        return false;
    }
    auto cursor = document()->find(text, textCursor());
    if (cursor.isNull()) {
        return false;
    }
    setTextCursor(cursor);
    return true;
}

int PlainTextCodeEditor::cursorPosition() const { return textCursor().position(); }

int PlainTextCodeEditor::cursorLine() const { return textCursor().blockNumber(); }

int PlainTextCodeEditor::cursorColumn() const { return textCursor().columnNumber(); }

bool PlainTextCodeEditor::hasSelection() const { return textCursor().hasSelection(); }

int PlainTextCodeEditor::selectionStart() const { return textCursor().selectionStart(); }

int PlainTextCodeEditor::selectionEnd() const { return textCursor().selectionEnd(); }

QString PlainTextCodeEditor::selectedText() const {
    auto text = textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

void PlainTextCodeEditor::setCursorPosition(int position, bool keepAnchor) {
    auto cursor = textCursor();
    cursor.setPosition(position, keepAnchor ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    setTextCursor(cursor);
}

void PlainTextCodeEditor::setSelection(int anchor, int position) {
    auto cursor = textCursor();
    cursor.setPosition(anchor, QTextCursor::MoveAnchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
}

void PlainTextCodeEditor::goTo(int line, int column) {
    if (!document()) {
        return;
    }
    auto block = document()->findBlockByNumber(std::max(0, line));
    if (!block.isValid()) {
        return;
    }
    auto position = block.position() + std::min(std::max(0, column), block.length() - 1);
    auto cursor = QTextCursor(document());
    cursor.setPosition(position);
    setTextCursor(cursor);
}

CodeEditor::CursorState PlainTextCodeEditor::cursorState() const {
    auto cursor = textCursor();
    auto state = CodeEditor::CursorState();
    state.line = cursor.blockNumber();
    state.column = cursor.positionInBlock();
    if (cursor.anchor() >= cursor.position()) {
        state.anchorLine = cursor.blockNumber();
        state.anchorColumn = cursor.positionInBlock();
    } else if (document()) {
        auto anchorBlock = document()->findBlock(cursor.anchor());
        state.anchorLine = anchorBlock.blockNumber();
        state.anchorColumn = cursor.anchor() - anchorBlock.position();
    }
    return state;
}

void PlainTextCodeEditor::setCursorState(const CodeEditor::CursorState &state) {
    if (!document()) {
        return;
    }
    auto cursor = QTextCursor(document());
    auto maxLine = std::max(0, document()->blockCount() - 1);

    auto caretLine = std::min(std::max(0, state.line), maxLine);
    auto caretBlock = document()->findBlockByNumber(caretLine);
    cursor.setPosition(caretBlock.position());
    auto caretMaxColumn = std::max(0, caretBlock.length() - 1);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                        std::min(std::max(0, state.column), caretMaxColumn));

    auto anchorLine = std::min(std::max(0, state.anchorLine), maxLine);
    auto anchorBlock = document()->findBlockByNumber(anchorLine);
    auto anchorMaxColumn = std::max(0, anchorBlock.length() - 1);
    auto anchorPos =
        anchorBlock.position() + std::min(std::max(0, state.anchorColumn), anchorMaxColumn);
    cursor.setPosition(anchorPos, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
}

int PlainTextCodeEditor::positionAt(const QPoint &viewportPosition) const {
    return cursorForPosition(viewportPosition).position();
}

int PlainTextCodeEditor::positionAt(int line, int column) const {
    if (!document()) {
        return -1;
    }
    auto block = document()->findBlockByNumber(line);
    if (!block.isValid()) {
        return -1;
    }
    return block.position() + std::min(column, block.length() - 1);
}

int PlainTextCodeEditor::lineAt(int position) const {
    if (!document()) {
        return -1;
    }
    auto block = document()->findBlock(position);
    return block.isValid() ? block.blockNumber() : -1;
}

int PlainTextCodeEditor::columnAt(int position) const {
    if (!document()) {
        return -1;
    }
    auto block = document()->findBlock(position);
    return block.isValid() ? position - block.position() : -1;
}

int PlainTextCodeEditor::lineCount() const { return document() ? document()->blockCount() : 0; }

QString PlainTextCodeEditor::lineText(int line) const {
    if (!document()) {
        return {};
    }
    auto block = document()->findBlockByNumber(line);
    return block.isValid() ? block.text() : QString();
}

CodeEditor::TextRange PlainTextCodeEditor::wordRangeAt(int position) const {
    auto range = CodeEditor::TextRange();
    if (!document()) {
        return range;
    }
    auto cursor = QTextCursor(document());
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);

    auto startBlock = document()->findBlock(cursor.selectionStart());
    auto endBlock = document()->findBlock(cursor.selectionEnd());
    if (!startBlock.isValid() || !endBlock.isValid()) {
        return range;
    }
    range.startLine = startBlock.blockNumber();
    range.startCharacter = cursor.selectionStart() - startBlock.position();
    range.endLine = endBlock.blockNumber();
    range.endCharacter = cursor.selectionEnd() - endBlock.position();
    return range;
}

CodeEditor::TextRange PlainTextCodeEditor::selectionRange() const {
    auto range = CodeEditor::TextRange();
    if (!document()) {
        return range;
    }
    auto cursor = textCursor();
    auto startBlock = document()->findBlock(cursor.selectionStart());
    auto endBlock = document()->findBlock(cursor.selectionEnd());
    if (!startBlock.isValid() || !endBlock.isValid()) {
        return range;
    }
    range.startLine = startBlock.blockNumber();
    range.startCharacter = cursor.selectionStart() - startBlock.position();
    range.endLine = endBlock.blockNumber();
    range.endCharacter = cursor.selectionEnd() - endBlock.position();
    return range;
}

QRect PlainTextCodeEditor::cursorRect(int position) const {
    if (!document()) {
        return {};
    }
    auto cursor = QTextCursor(document());
    cursor.setPosition(position);
    return QPlainTextEdit::cursorRect(cursor);
}

void PlainTextCodeEditor::setCompletionCallback(CodeEditor::CompletionCallback callback) {
    Q_UNUSED(callback)
}

void PlainTextCodeEditor::setCompletionExclusive(bool exclusive) {
    completionExclusiveValue = exclusive;
}

bool PlainTextCodeEditor::completionExclusive() const { return completionExclusiveValue; }

void PlainTextCodeEditor::setCodeActionCallback(CodeEditor::CodeActionCallback callback) {
    Q_UNUSED(callback)
}

void PlainTextCodeEditor::setLineCodeAction(int line, bool available) {
    Q_UNUSED(line)
    Q_UNUSED(available)
}

void PlainTextCodeEditor::showCallTip(const QString &text) {
    callTipVisible = !text.isEmpty();
    if (callTipVisible) {
        auto position = viewport()->mapToGlobal(cursorRect(textCursor().position()).bottomLeft());
        QToolTip::showText(position, text, this);
    }
}

void PlainTextCodeEditor::updateCallTip(const QString &text) { showCallTip(text); }

void PlainTextCodeEditor::hideCallTip() {
    QToolTip::hideText();
    callTipVisible = false;
}

bool PlainTextCodeEditor::isCallTipVisible() const { return callTipVisible; }

void PlainTextCodeEditor::setDiagnostic(int line, const CodeEditor::Diagnostic &diagnostic) {
    diagnostics[line].append(diagnostic);
    refreshDiagnostics();
}

void PlainTextCodeEditor::clearDiagnostics(const QString &owner) {
    auto emptyLines = QList<int>();
    for (auto it = diagnostics.begin(); it != diagnostics.end(); ++it) {
        auto &lineDiagnostics = it.value();
        lineDiagnostics.erase(std::remove_if(lineDiagnostics.begin(), lineDiagnostics.end(),
                                             [&owner](const CodeEditor::Diagnostic &diagnostic) {
                                                 return diagnostic.owner == owner;
                                             }),
                              lineDiagnostics.end());
        if (lineDiagnostics.isEmpty()) {
            emptyLines.append(it.key());
        }
    }
    for (auto line : emptyLines) {
        diagnostics.remove(line);
    }
    refreshDiagnostics();
}

void PlainTextCodeEditor::clearAllDiagnostics() {
    diagnostics.clear();
    refreshDiagnostics();
}

QList<CodeEditor::Diagnostic> PlainTextCodeEditor::diagnosticsOn(int line) const {
    return diagnostics.value(line);
}

void PlainTextCodeEditor::setLineMessage(int line, const QString &message) {
    Q_UNUSED(line)
    Q_UNUSED(message)
}

void PlainTextCodeEditor::removeMetaData() {}

QString PlainTextCodeEditor::highlighterId() const { return QStringLiteral("Plain text"); }

void PlainTextCodeEditor::setHighlighter(const QString &id) { Q_UNUSED(id) }

void PlainTextCodeEditor::removeHighlighter() {}

void PlainTextCodeEditor::setIndentAlgorithm(CodeEditor::IndentAlgorithm algorithm) {
    Q_UNUSED(algorithm)
}

void PlainTextCodeEditor::setLineWrapMode(CodeEditor::WrapMode mode) {
    QPlainTextEdit::setLineWrapMode(mode == CodeEditor::WrapMode::WidgetWidth
                                        ? QPlainTextEdit::WidgetWidth
                                        : QPlainTextEdit::NoWrap);
}

void PlainTextCodeEditor::setEditorFont(const QFont &font) {
    QPlainTextEdit::setFont(font);
    if (document()) {
        document()->setDefaultFont(font);
    }
}

void PlainTextCodeEditor::setFrameVisible(bool visible) {
    setFrameStyle(visible ? (QFrame::StyledPanel | QFrame::Sunken) : QFrame::NoFrame);
}

QAction *PlainTextCodeEditor::action(CodeEditor::Action action) const {
    return actions.value(action, nullptr);
}

QMenu *PlainTextCodeEditor::createStandardContextMenu() {
    return QPlainTextEdit::createStandardContextMenu();
}

void PlainTextCodeEditor::createActions() {
    auto add = [this](CodeEditor::Action id, const QString &text) {
        auto *action = new QAction(text, this);
        actions.insert(id, action);
        return action;
    };

    connect(add(CodeEditor::Action::DeleteLine, tr("Delete line")), &QAction::triggered, this,
            &PlainTextCodeEditor::deleteCurrentLine);
    connect(add(CodeEditor::Action::CutLine, tr("Cut line")), &QAction::triggered, this,
            &PlainTextCodeEditor::cutCurrentLine);
    connect(add(CodeEditor::Action::CopyLine, tr("Copy line")), &QAction::triggered, this,
            &PlainTextCodeEditor::copyCurrentLine);
    connect(add(CodeEditor::Action::PasteLine, tr("Paste line")), &QAction::triggered, this,
            &PlainTextCodeEditor::paste);
    connect(add(CodeEditor::Action::JoinLines, tr("Join lines")), &QAction::triggered, this,
            &PlainTextCodeEditor::joinLines);
    connect(add(CodeEditor::Action::DuplicateSelection, tr("Duplicate selection")),
            &QAction::triggered, this, &PlainTextCodeEditor::duplicateSelection);
    connect(add(CodeEditor::Action::MoveLineUp, tr("Move line up")), &QAction::triggered, this,
            [this]() { moveLine(-1); });
    connect(add(CodeEditor::Action::MoveLineDown, tr("Move line down")), &QAction::triggered, this,
            [this]() { moveLine(1); });
    connect(add(CodeEditor::Action::ToggleComment, tr("Toggle comment")), &QAction::triggered, this,
            &PlainTextCodeEditor::toggleComment);
    connect(add(CodeEditor::Action::ToggleBookmark, tr("Toggle bookmark")), &QAction::triggered,
            this, &PlainTextCodeEditor::toggleBookmark);
    connect(add(CodeEditor::Action::NextBookmark, tr("Next bookmark")), &QAction::triggered, this,
            [this]() { gotoBookmark(1); });
    connect(add(CodeEditor::Action::PreviousBookmark, tr("Previous bookmark")), &QAction::triggered,
            this, [this]() { gotoBookmark(-1); });

    add(CodeEditor::Action::Fold, tr("Fold"));
    add(CodeEditor::Action::Unfold, tr("Unfold"));
    add(CodeEditor::Action::FoldTopLevel, tr("Fold top level"));
    add(CodeEditor::Action::UnfoldAll, tr("Unfold all"));
    add(CodeEditor::Action::ToggleFold, tr("Toggle fold"));
    add(CodeEditor::Action::FindMatchingBracket, tr("Find matching bracket"));

    for (auto id : {CodeEditor::Action::Fold, CodeEditor::Action::Unfold,
                    CodeEditor::Action::FoldTopLevel, CodeEditor::Action::UnfoldAll,
                    CodeEditor::Action::ToggleFold, CodeEditor::Action::FindMatchingBracket}) {
        actions.value(id)->setEnabled(false);
    }
}

QString PlainTextCodeEditor::currentLineText() const { return lineText(cursorLine()); }

void PlainTextCodeEditor::replaceLineText(int line, const QString &text) {
    if (!document()) {
        return;
    }
    auto block = document()->findBlockByNumber(line);
    if (!block.isValid()) {
        return;
    }
    auto cursor = QTextCursor(document());
    cursor.setPosition(block.position());
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

void PlainTextCodeEditor::deleteCurrentLine() {
    auto cursor = textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::StartOfBlock);
    if (!cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor)) {
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void PlainTextCodeEditor::copyCurrentLine() {
    QApplication::clipboard()->setText(currentLineText());
}

void PlainTextCodeEditor::cutCurrentLine() {
    copyCurrentLine();
    deleteCurrentLine();
}

void PlainTextCodeEditor::joinLines() {
    auto cursor = textCursor();
    auto firstBlock = document() ? document()->findBlock(cursor.selectionStart()) : QTextBlock();
    if (!firstBlock.isValid()) {
        return;
    }

    auto lastBlock = firstBlock;
    if (cursor.hasSelection()) {
        auto end = std::max(0, cursor.selectionEnd() - 1);
        lastBlock = document()->findBlock(end);
    } else {
        lastBlock = firstBlock.next();
    }
    if (!lastBlock.isValid()) {
        return;
    }

    auto endPosition = lastBlock.position() + std::max(0, lastBlock.length() - 1);
    cursor.beginEditBlock();
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    auto joined = cursor.selectedText();
    joined.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
    cursor.insertText(joined);
    cursor.endEditBlock();
    setCursorPosition(firstBlock.position() + joined.size());
}

void PlainTextCodeEditor::duplicateSelection() {
    auto cursor = textCursor();
    auto text = cursor.hasSelection() ? cursor.selectedText() : currentLineText();
    if (!text.isEmpty()) {
        cursor.insertText(text);
    }
}

void PlainTextCodeEditor::moveLine(int direction) {
    auto line = cursorLine();
    auto other = line + direction;
    if (other < 0 || other >= lineCount()) {
        return;
    }
    auto column = cursorColumn();
    auto upper = std::min(line, other);
    auto lower = std::max(line, other);
    auto upperText = lineText(upper);
    auto lowerText = lineText(lower);

    auto cursor = QTextCursor(document());
    cursor.beginEditBlock();
    replaceLineText(upper, lowerText);
    replaceLineText(lower, upperText);
    cursor.endEditBlock();
    goTo(other, column);
}

void PlainTextCodeEditor::toggleComment() {
    auto cursor = textCursor();
    auto first = lineAt(cursor.selectionStart());
    auto end = std::max(cursor.selectionStart(), cursor.selectionEnd() - 1);
    auto last = lineAt(end);
    if (first < 0 || last < first) {
        return;
    }

    auto allCommented = true;
    for (auto line = first; line <= last; ++line) {
        if (!lineText(line).trimmed().startsWith(QLatin1String("//"))) {
            allCommented = false;
            break;
        }
    }

    cursor.beginEditBlock();
    for (auto line = first; line <= last; ++line) {
        auto text = lineText(line);
        auto offset = 0;
        while (offset < text.size() && text.at(offset).isSpace()) {
            ++offset;
        }
        if (allCommented) {
            if (text.mid(offset, 3) == QLatin1String("// ")) {
                text.remove(offset, 3);
            } else if (text.mid(offset, 2) == QLatin1String("//")) {
                text.remove(offset, 2);
            }
        } else {
            text.insert(offset, QLatin1String("// "));
        }
        replaceLineText(line, text);
    }
    cursor.endEditBlock();
}

void PlainTextCodeEditor::toggleBookmark() {
    auto line = cursorLine();
    if (!bookmarks.remove(line)) {
        bookmarks.insert(line);
    }
}

void PlainTextCodeEditor::gotoBookmark(int direction) {
    if (bookmarks.isEmpty()) {
        return;
    }

    auto current = cursorLine();
    auto target = -1;
    if (direction > 0) {
        for (auto line : bookmarks) {
            if (line > current && (target < 0 || line < target)) {
                target = line;
            }
        }
        if (target < 0) {
            target = *std::min_element(bookmarks.begin(), bookmarks.end());
        }
    } else {
        for (auto line : bookmarks) {
            if (line < current && (target < 0 || line > target)) {
                target = line;
            }
        }
        if (target < 0) {
            target = *std::max_element(bookmarks.begin(), bookmarks.end());
        }
    }
    goTo(target, 0);
}

void PlainTextCodeEditor::refreshDiagnostics() {
    auto selections = QList<QTextEdit::ExtraSelection>();
    if (document()) {
        for (auto it = diagnostics.constBegin(); it != diagnostics.constEnd(); ++it) {
            auto block = document()->findBlockByNumber(it.key());
            if (!block.isValid() || it.value().isEmpty()) {
                continue;
            }

            auto selection = QTextEdit::ExtraSelection();
            selection.cursor = QTextCursor(document());
            selection.cursor.setPosition(block.position());
            selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            switch (it.value().first().severity) {
            case CodeEditor::DiagnosticSeverity::Error:
                selection.format.setUnderlineColor(Qt::red);
                break;
            case CodeEditor::DiagnosticSeverity::Warning:
                selection.format.setUnderlineColor(QColor("#d78b00"));
                break;
            case CodeEditor::DiagnosticSeverity::Information:
                selection.format.setUnderlineColor(Qt::blue);
                break;
            case CodeEditor::DiagnosticSeverity::Hint:
                selection.format.setUnderlineColor(Qt::gray);
                break;
            }
            selections.append(selection);
        }
    }
    setExtraSelections(selections);
}
