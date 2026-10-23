/**
 * \file quteparteditor.cpp
 * \brief CodeEditorInterface implementation backed by Qutepart.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QAction>
#include <QMenu>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <qutepart/qutepart.h>
#include <qutepart/theme.h>

#include "lsp_rename_geometry.h"
#include "quteparteditor.h"
#include "qttextoperationsadapter.h"

static auto toQutepartSeverity(CodeEditor::DiagnosticSeverity severity)
    -> Qutepart::DiagnosticSeverity {
    switch (severity) {
    case CodeEditor::DiagnosticSeverity::Error:
        return Qutepart::DiagnosticSeverity::Error;
    case CodeEditor::DiagnosticSeverity::Warning:
        return Qutepart::DiagnosticSeverity::Warning;
    case CodeEditor::DiagnosticSeverity::Information:
        return Qutepart::DiagnosticSeverity::Information;
    case CodeEditor::DiagnosticSeverity::Hint:
        return Qutepart::DiagnosticSeverity::Hint;
    }
    return Qutepart::DiagnosticSeverity::Information;
}

static auto fromQutepartSeverity(Qutepart::DiagnosticSeverity severity)
    -> CodeEditor::DiagnosticSeverity {
    switch (severity) {
    case Qutepart::DiagnosticSeverity::Error:
        return CodeEditor::DiagnosticSeverity::Error;
    case Qutepart::DiagnosticSeverity::Warning:
        return CodeEditor::DiagnosticSeverity::Warning;
    case Qutepart::DiagnosticSeverity::Information:
        return CodeEditor::DiagnosticSeverity::Information;
    case Qutepart::DiagnosticSeverity::Hint:
        return CodeEditor::DiagnosticSeverity::Hint;
    }
    return CodeEditor::DiagnosticSeverity::Information;
}

QutepartCodeEditor::QutepartCodeEditor(QWidget *parent)
    : qutepart(new Qutepart::Qutepart(parent)) {
    QObject::connect(qutepart, &QPlainTextEdit::textChanged, qutepart, [this]() {
        if (textChangedCallback) {
            textChangedCallback();
        }
    });
    QObject::connect(qutepart, &QPlainTextEdit::cursorPositionChanged, qutepart, [this]() {
        if (cursorPositionChangedCallback) {
            cursorPositionChangedCallback();
        }
    });
    QObject::connect(qutepart, &QPlainTextEdit::undoAvailable, qutepart, [this](bool available) {
        if (undoAvailableCallback) {
            undoAvailableCallback(available);
        }
    });
    QObject::connect(qutepart, &QPlainTextEdit::redoAvailable, qutepart, [this](bool available) {
        if (redoAvailableCallback) {
            redoAvailableCallback(available);
        }
    });
}

QutepartCodeEditor::~QutepartCodeEditor() {
    delete theme;
    delete qutepart;
}

QWidget *QutepartCodeEditor::widget() { return qutepart; }
QWidget *QutepartCodeEditor::viewport() { return qutepart->viewport(); }
QScrollBar *QutepartCodeEditor::verticalScrollBar() { return qutepart->verticalScrollBar(); }

TextOperationsAdapter *QutepartCodeEditor::createOperationsAdapter(QObject *parent) {
    return new QtTextOperationsAdapter(qutepart, parent);
}

void QutepartCodeEditor::setTextChangedCallback(std::function<void()> callback) {
    textChangedCallback = std::move(callback);
}

void QutepartCodeEditor::setCursorPositionChangedCallback(std::function<void()> callback) {
    cursorPositionChangedCallback = std::move(callback);
}

void QutepartCodeEditor::setUndoAvailableCallback(std::function<void(bool)> callback) {
    undoAvailableCallback = std::move(callback);
}

void QutepartCodeEditor::setRedoAvailableCallback(std::function<void(bool)> callback) {
    redoAvailableCallback = std::move(callback);
}

QString QutepartCodeEditor::text() const { return qutepart->toPlainText(); }

QChar QutepartCodeEditor::characterAt(int position) const {
    return qutepart->document()->characterAt(position);
}

void QutepartCodeEditor::setText(const QString &text) { qutepart->setPlainText(text); }

void QutepartCodeEditor::clear() { qutepart->clear(); }

bool QutepartCodeEditor::isEmpty() const {
    return !qutepart->document() || qutepart->document()->isEmpty();
}

bool QutepartCodeEditor::isModified() const { return qutepart->document()->isModified(); }

void QutepartCodeEditor::setModified(bool modified) {
    qutepart->document()->setModified(modified);
}

bool QutepartCodeEditor::isReadOnly() const { return qutepart->isReadOnly(); }

void QutepartCodeEditor::setReadOnly(bool readOnly) { qutepart->setReadOnly(readOnly); }

bool QutepartCodeEditor::canUndo() const {
    return qutepart->document() && qutepart->document()->isUndoAvailable();
}

bool QutepartCodeEditor::canRedo() const {
    return qutepart->document() && qutepart->document()->isRedoAvailable();
}

void QutepartCodeEditor::undo() { qutepart->undo(); }
void QutepartCodeEditor::redo() { qutepart->redo(); }
void QutepartCodeEditor::copy() { qutepart->multipleCursorCopy(); }
void QutepartCodeEditor::cut() { qutepart->multipleCursorCut(); }
void QutepartCodeEditor::paste() { qutepart->multipleCursorPaste(); }

void QutepartCodeEditor::setUndoRedoEnabled(bool enabled) {
    if (qutepart->document()) {
        qutepart->document()->setUndoRedoEnabled(enabled);
    }
}

void QutepartCodeEditor::removeModifications() { qutepart->removeModifications(); }

void QutepartCodeEditor::replaceText(const QString &text, bool undoable) {
    auto document = qutepart->document();
    if (!document) {
        return;
    }

    auto undoWasEnabled = document->isUndoRedoEnabled();
    if (!undoable) {
        document->setUndoRedoEnabled(false);
    }

    auto cursor = QTextCursor(document);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();

    if (!undoable) {
        document->setUndoRedoEnabled(undoWasEnabled);
    }
}

void QutepartCodeEditor::replaceSelectedText(const QString &text) {
    auto cursor = qutepart->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }
    cursor.beginEditBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
    qutepart->setTextCursor(cursor);
}

void QutepartCodeEditor::trimTrailingSpaces() {
    auto document = qutepart->document();
    if (!document) {
        return;
    }

    static const auto trailingSpaces = QRegularExpression("\\s+$");
    auto cursor = QTextCursor(document);
    cursor.beginEditBlock();
    for (auto block = document->begin(); block != document->end();) {
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

bool QutepartCodeEditor::applyTextEdits(const QList<CodeEditor::TextEdit> &edits,
                                        const QString &expectedOldName) {
    if (edits.isEmpty() || !qutepart->document()) {
        return false;
    }

    auto converted = QList<lspRename::Edit>();
    converted.reserve(edits.size());
    for (auto const &edit : edits) {
        converted.append(lspRename::Edit{edit.startLine, edit.startCharacter, edit.endLine,
                                         edit.endCharacter, edit.newText});
    }

    if (expectedOldName.isEmpty()) {
        return lspRename::applyEdits(qutepart->document(), converted) > 0;
    }

    auto rejected = QStringList();
    return lspRename::applyRenameEdits(qutepart->document(), converted, expectedOldName,
                                       &rejected) > 0;
}

bool QutepartCodeEditor::findText(const QString &text) {
    auto cursor = qutepart->document()->find(text, qutepart->textCursor());
    if (cursor.isNull()) {
        return false;
    }
    qutepart->setTextCursor(cursor);
    return true;
}

int QutepartCodeEditor::cursorPosition() const { return qutepart->textCursor().position(); }
int QutepartCodeEditor::cursorLine() const { return qutepart->textCursor().blockNumber(); }
int QutepartCodeEditor::cursorColumn() const { return qutepart->textCursor().columnNumber(); }
bool QutepartCodeEditor::hasSelection() const { return qutepart->textCursor().hasSelection(); }
int QutepartCodeEditor::selectionStart() const { return qutepart->textCursor().selectionStart(); }
int QutepartCodeEditor::selectionEnd() const { return qutepart->textCursor().selectionEnd(); }

QString QutepartCodeEditor::selectedText() const {
    auto text = qutepart->textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

void QutepartCodeEditor::setCursorPosition(int position, bool keepAnchor) {
    auto cursor = qutepart->textCursor();
    cursor.setPosition(position, keepAnchor ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    qutepart->setTextCursor(cursor);
}

void QutepartCodeEditor::setSelection(int anchor, int position) {
    auto cursor = qutepart->textCursor();
    cursor.setPosition(anchor, QTextCursor::MoveAnchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    qutepart->setTextCursor(cursor);
}

void QutepartCodeEditor::goTo(int line, int column) { qutepart->goTo(line, column); }

CodeEditor::CursorState QutepartCodeEditor::cursorState() const {
    auto cursor = qutepart->textCursor();
    auto state = CodeEditor::CursorState();
    state.line = cursor.blockNumber();
    state.column = cursor.positionInBlock();
    if (cursor.anchor() >= cursor.position()) {
        state.anchorLine = cursor.blockNumber();
        state.anchorColumn = cursor.positionInBlock();
    } else {
        auto block = qutepart->document()->findBlock(cursor.anchor());
        state.anchorLine = block.blockNumber();
        state.anchorColumn = cursor.anchor() - block.position();
    }
    return state;
}

void QutepartCodeEditor::setCursorState(const CodeEditor::CursorState &state) {
    auto document = qutepart->document();
    auto newCursor = QTextCursor(document);
    auto maxLine = document->blockCount() - 1;

    auto caretLine = std::min(state.line, maxLine);
    auto caretBlock = document->findBlockByNumber(caretLine);
    newCursor.setPosition(caretBlock.position());
    auto caretMaxColumn = caretBlock.length() - 1;
    newCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                           std::min(state.column, caretMaxColumn));

    auto anchorLine = std::min(state.anchorLine, maxLine);
    auto anchorBlock = document->findBlockByNumber(anchorLine);
    auto anchorMaxColumn = anchorBlock.length() - 1;
    auto anchorPos = anchorBlock.position() + std::min(state.anchorColumn, anchorMaxColumn);
    newCursor.setPosition(anchorPos, QTextCursor::KeepAnchor);
    qutepart->setTextCursor(newCursor);
}

int QutepartCodeEditor::positionAt(const QPoint &viewportPosition) const {
    return qutepart->cursorForPosition(viewportPosition).position();
}

int QutepartCodeEditor::positionAt(int line, int column) const {
    auto block = qutepart->document()->findBlockByNumber(line);
    if (!block.isValid()) {
        return -1;
    }
    return block.position() + std::min(column, block.length() - 1);
}

int QutepartCodeEditor::lineAt(int position) const {
    auto block = qutepart->document()->findBlock(position);
    return block.isValid() ? block.blockNumber() : -1;
}

int QutepartCodeEditor::columnAt(int position) const {
    auto block = qutepart->document()->findBlock(position);
    return block.isValid() ? position - block.position() : -1;
}

int QutepartCodeEditor::lineCount() const { return qutepart->document()->blockCount(); }

QString QutepartCodeEditor::lineText(int line) const {
    auto block = qutepart->document()->findBlockByNumber(line);
    return block.isValid() ? block.text() : QString();
}

CodeEditor::TextRange QutepartCodeEditor::wordRangeAt(int position) const {
    auto cursor = QTextCursor(qutepart->document());
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);

    auto range = CodeEditor::TextRange();
    auto document = qutepart->document();
    auto startBlock = document->findBlock(cursor.selectionStart());
    auto endBlock = document->findBlock(cursor.selectionEnd());
    range.startLine = startBlock.blockNumber();
    range.startCharacter = cursor.selectionStart() - startBlock.position();
    range.endLine = endBlock.blockNumber();
    range.endCharacter = cursor.selectionEnd() - endBlock.position();
    return range;
}

CodeEditor::TextRange QutepartCodeEditor::selectionRange() const {
    auto cursor = qutepart->textCursor();
    auto document = qutepart->document();
    auto range = CodeEditor::TextRange();
    auto startBlock = document->findBlock(cursor.selectionStart());
    auto endBlock = document->findBlock(cursor.selectionEnd());
    range.startLine = startBlock.blockNumber();
    range.startCharacter = cursor.selectionStart() - startBlock.position();
    range.endLine = endBlock.blockNumber();
    range.endCharacter = cursor.selectionEnd() - endBlock.position();
    return range;
}

QRect QutepartCodeEditor::cursorRect(int position) const {
    auto cursor = QTextCursor(qutepart->document());
    cursor.setPosition(position);
    return qutepart->QPlainTextEdit::cursorRect(cursor);
}

void QutepartCodeEditor::setCompletionCallback(CodeEditor::CompletionCallback callback) {
    qutepart->setCompletionCallback(
        [callback = std::move(callback)](const QString &prefix, const QString &previousWord,
                                         const QString &separator)
            -> QFuture<QSet<Qutepart::CompletionItem>> {
            if (!callback) {
                return {};
            }
            auto future = callback(prefix, previousWord, separator);
            if (!future.isValid()) {
                return {};
            }
            return future.then([](const QFuture<QSet<CodeEditor::CompletionItem>> &f) {
                auto items = QSet<Qutepart::CompletionItem>();
                if (!f.isFinished() || !f.isValid()) {
                    return items;
                }
                for (auto const &item : f.result()) {
                    items.insert(Qutepart::CompletionItem(item.text, item.source));
                }
                return items;
            });
        });
}

void QutepartCodeEditor::setCompletionExclusive(bool exclusive) {
    qutepart->setCustomCompletionsExclusive(exclusive);
}

bool QutepartCodeEditor::completionExclusive() const {
    return qutepart->customCompletionsExclusive();
}

void QutepartCodeEditor::setCodeActionCallback(CodeEditor::CodeActionCallback callback) {
    qutepart->setCodeActionCallback(std::move(callback));
}

void QutepartCodeEditor::setLineCodeAction(int line, bool available) {
    qutepart->setLineCodeAction(line, available);
}

void QutepartCodeEditor::showCallTip(const QString &text) { qutepart->showCallTip(text); }
void QutepartCodeEditor::updateCallTip(const QString &text) { qutepart->updateCallTip(text); }
void QutepartCodeEditor::hideCallTip() { qutepart->hideCallTip(); }
bool QutepartCodeEditor::isCallTipVisible() const { return qutepart->isCallTipVisible(); }

void QutepartCodeEditor::setDiagnostic(int line, const CodeEditor::Diagnostic &diagnostic) {
    qutepart->setDiagnostic(line, Qutepart::Diagnostic{diagnostic.owner,
                                                       toQutepartSeverity(diagnostic.severity),
                                                       diagnostic.message});
}

void QutepartCodeEditor::clearDiagnostics(const QString &owner) {
    qutepart->clearDiagnostics(owner);
}

void QutepartCodeEditor::clearAllDiagnostics() { qutepart->clearAllDiagnostics(); }

QList<CodeEditor::Diagnostic> QutepartCodeEditor::diagnosticsOn(int line) const {
    auto result = QList<CodeEditor::Diagnostic>();
    for (auto const &diagnostic : qutepart->diagnosticsOn(line)) {
        result.append(CodeEditor::Diagnostic{diagnostic.owner,
                                             fromQutepartSeverity(diagnostic.severity),
                                             diagnostic.message});
    }
    return result;
}

void QutepartCodeEditor::setLineMessage(int line, const QString &message) {
    qutepart->setLineMessage(line, message);
}

void QutepartCodeEditor::removeMetaData() { qutepart->removeMetaData(); }

void QutepartCodeEditor::setLineBookmark(int line, bool value) {
    qutepart->setLineBookmark(line, value);
}

void QutepartCodeEditor::setLineWarning(int line, bool value) {
    qutepart->setLineWarning(line, value);
}

void QutepartCodeEditor::setLineError(int line, bool value) {
    qutepart->setLineError(line, value);
}

void QutepartCodeEditor::setLineInfo(int line, bool value) {
    qutepart->setLineInfo(line, value);
}

void QutepartCodeEditor::setLineBreakpoint(int line, bool value) {
    qutepart->setLineBreakpoint(line, value);
}

void QutepartCodeEditor::setLineExecuting(int line, bool value) {
    qutepart->setLineExecuting(line, value);
}

QString QutepartCodeEditor::highlighterId() const { return highlighterIdValue; }

void QutepartCodeEditor::setHighlighter(const QString &id) {
    highlighterIdValue = id;
    qutepart->setHighlighter(id);
}

void QutepartCodeEditor::removeHighlighter() {
    highlighterIdValue.clear();
    qutepart->removeHighlighter();
}

void QutepartCodeEditor::setIndentAlgorithm(CodeEditor::IndentAlgorithm algorithm) {
    qutepart->setIndentAlgorithm(static_cast<Qutepart::IndentAlg>(algorithm));
}

void QutepartCodeEditor::setLineWrapMode(CodeEditor::WrapMode mode) {
    qutepart->QPlainTextEdit::setLineWrapMode(mode == CodeEditor::WrapMode::WidgetWidth
                                                  ? QPlainTextEdit::WidgetWidth
                                                  : QPlainTextEdit::NoWrap);
}

void QutepartCodeEditor::setThemeFile(const QString &themeFile) {
    Qutepart::Theme *newTheme = nullptr;
    if (!themeFile.isEmpty()) {
        newTheme = new Qutepart::Theme();
        if (!newTheme->loadTheme(themeFile)) {
            delete newTheme;
            newTheme = nullptr;
        }
    }

    qutepart->setTheme(newTheme);
    delete theme;
    theme = newTheme;
    themeFileValue = theme ? themeFile : QString();
}

QString QutepartCodeEditor::themeFile() const { return themeFileValue; }

void QutepartCodeEditor::setEditorFont(const QFont &font) { qutepart->setFont(font); }

void QutepartCodeEditor::setDrawAnyWhitespace(bool value) {
    qutepart->setDrawAnyWhitespace(value);
}

void QutepartCodeEditor::setDrawIndentations(bool value) {
    qutepart->setDrawIndentations(value);
    qutepart->setDrawIncorrectIndentation(value);
}

void QutepartCodeEditor::setBracketHighlightingEnabled(bool value) {
    qutepart->setBracketHighlightingEnabled(value);
}

void QutepartCodeEditor::setSoftLineWrapping(bool value) {
    qutepart->setSoftLineWrapping(value);
}

void QutepartCodeEditor::setLineNumbersVisible(bool value) {
    qutepart->setLineNumbersVisible(value);
}

void QutepartCodeEditor::setSmartHomeEnd(bool value) { qutepart->setSmartHomeEnd(value); }

void QutepartCodeEditor::setDrawSolidEdge(bool value) { qutepart->setDrawSolidEdge(value); }

void QutepartCodeEditor::setLineLengthEdge(int value) { qutepart->setLineLengthEdge(value); }

void QutepartCodeEditor::setMarkAreaVisible(bool value) { qutepart->setMarkAreaVisible(value); }

void QutepartCodeEditor::setFoldingAreaVisible(bool value) {
    qutepart->setFoldingAreaVisible(value);
}

void QutepartCodeEditor::setMinimapVisible(bool value) { qutepart->setMinimapVisible(value); }

void QutepartCodeEditor::setMarkCurrentWord(bool value) {
    qutepart->setMarkCurrentWord(value);
}

void QutepartCodeEditor::setFrameVisible(bool value) {
    qutepart->setFrameStyle(value ? QFrame::StyledPanel : QFrame::NoFrame);
}

void QutepartCodeEditor::foldTopLevel() { qutepart->foldTopLevelBlocks(); }

QAction *QutepartCodeEditor::action(CodeEditor::Action action) const {
    switch (action) {
    case CodeEditor::Action::DeleteLine:
        return qutepart->deleteLineAction();
    case CodeEditor::Action::CutLine:
        return qutepart->cutLineAction();
    case CodeEditor::Action::CopyLine:
        return qutepart->copyLineAction();
    case CodeEditor::Action::PasteLine:
        return qutepart->pasteLineAction();
    case CodeEditor::Action::JoinLines:
        return qutepart->joinLinesAction();
    case CodeEditor::Action::DuplicateSelection:
        return qutepart->duplicateSelectionAction();
    case CodeEditor::Action::MoveLineUp:
        return qutepart->moveLineUpAction();
    case CodeEditor::Action::MoveLineDown:
        return qutepart->moveLineDownAction();
    case CodeEditor::Action::ToggleComment:
        return qutepart->toggleCommentAction();
    case CodeEditor::Action::ToggleBookmark:
        return qutepart->toggleBookmarkAction();
    case CodeEditor::Action::NextBookmark:
        return qutepart->nextBookmarkAction();
    case CodeEditor::Action::PreviousBookmark:
        return qutepart->prevBookmarkAction();
    case CodeEditor::Action::Fold:
        return qutepart->foldAction();
    case CodeEditor::Action::Unfold:
        return qutepart->unfoldAction();
    case CodeEditor::Action::FoldTopLevel:
        return qutepart->foldTopLevelAction();
    case CodeEditor::Action::UnfoldAll:
        return qutepart->unfoldAllAction();
    case CodeEditor::Action::ToggleFold:
        return qutepart->toggleFoldAction();
    case CodeEditor::Action::FindMatchingBracket:
        return qutepart->findMatchingBracketAction();
    }
    return nullptr;
}

QMenu *QutepartCodeEditor::createStandardContextMenu() {
    return qutepart->createStandardContextMenu();
}
