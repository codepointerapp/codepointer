/**
 * \file PlainTextEditor.h
 * \brief Minimal CodeEditorInterface implementation backed by QPlainTextEdit.
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QHash>
#include <QSet>

#include <QPlainTextEdit>

#include "widgets/codeeditorinterface.h"

class QAction;
class QMenu;

/**
 * A deliberately simple editor backend.
 *
 * Everything that maps naturally onto QPlainTextEdit is implemented directly.
 * Features the plain widget cannot provide (syntax highlighting, folding,
 * completion, call tips) are safe no-ops, so the rest of the application can
 * run against a tiny editor while a richer backend is being developed.
 */
class PlainTextCodeEditor : public QPlainTextEdit, public CodeEditorInterface {
    Q_OBJECT
  public:
    explicit PlainTextCodeEditor(QWidget *parent = nullptr);
    ~PlainTextCodeEditor() override = default;

    QWidget *widget() override { return this; }
    QWidget *viewport() override { return QPlainTextEdit::viewport(); }
    QScrollBar *verticalScrollBar() override { return QPlainTextEdit::verticalScrollBar(); }
    TextOperationsAdapter *createOperationsAdapter(QObject *parent) override;

    void setTextChangedCallback(std::function<void()> callback) override;
    void setCursorPositionChangedCallback(std::function<void()> callback) override;
    void setUndoAvailableCallback(std::function<void(bool)> callback) override;
    void setRedoAvailableCallback(std::function<void(bool)> callback) override;

    QString text() const override;
    QChar characterAt(int position) const override;
    void setText(const QString &text) override;
    void clear() override;
    bool isEmpty() const override;
    bool isModified() const override;
    void setModified(bool modified) override;
    bool isReadOnly() const override;
    void setReadOnly(bool readOnly) override;
    bool canUndo() const override;
    bool canRedo() const override;
    void undo() override;
    void redo() override;
    void copy() override;
    void cut() override;
    void paste() override;
    void setUndoRedoEnabled(bool enabled) override;
    void removeModifications() override;

    void replaceText(const QString &text, bool undoable) override;
    void replaceSelectedText(const QString &text) override;
    void trimTrailingSpaces() override;
    bool applyTextEdits(const QList<CodeEditor::TextEdit> &edits,
                        const QString &expectedOldName = {}) override;
    bool findText(const QString &text) override;

    int cursorPosition() const override;
    int cursorLine() const override;
    int cursorColumn() const override;
    bool hasSelection() const override;
    int selectionStart() const override;
    int selectionEnd() const override;
    QString selectedText() const override;
    void setCursorPosition(int position, bool keepAnchor = false) override;
    void setSelection(int anchor, int position) override;
    void goTo(int line, int column) override;
    CodeEditor::CursorState cursorState() const override;
    void setCursorState(const CodeEditor::CursorState &state) override;
    int positionAt(const QPoint &viewportPosition) const override;
    int positionAt(int line, int column) const override;
    int lineAt(int position) const override;
    int columnAt(int position) const override;
    int lineCount() const override;
    QString lineText(int line) const override;
    CodeEditor::TextRange wordRangeAt(int position) const override;
    CodeEditor::TextRange selectionRange() const override;
    QRect cursorRect(int position) const override;

    void setCompletionCallback(CodeEditor::CompletionCallback callback) override;
    void setCompletionExclusive(bool exclusive) override;
    bool completionExclusive() const override;
    void setCodeActionCallback(CodeEditor::CodeActionCallback callback) override;
    void setLineCodeAction(int line, bool available) override;
    void showCallTip(const QString &text) override;
    void updateCallTip(const QString &text) override;
    void hideCallTip() override;
    bool isCallTipVisible() const override;

    void setDiagnostic(int line, const CodeEditor::Diagnostic &diagnostic) override;
    void clearDiagnostics(const QString &owner) override;
    void clearAllDiagnostics() override;
    QList<CodeEditor::Diagnostic> diagnosticsOn(int line) const override;
    void setLineMessage(int line, const QString &message) override;
    void removeMetaData() override;

    QString highlighterId() const override;
    void setHighlighter(const QString &id) override;
    void removeHighlighter() override;
    void setIndentAlgorithm(CodeEditor::IndentAlgorithm algorithm) override;
    void setLineWrapMode(CodeEditor::WrapMode mode) override;
    void setEditorFont(const QFont &font) override;
    void setFrameVisible(bool visible) override;

    QAction *action(CodeEditor::Action action) const override;
    QMenu *createStandardContextMenu() override;

  private:
    void createActions();
    QAction *makeAction(CodeEditor::Action id, const QString &text);
    QString currentLineText() const;
    void replaceLineText(int line, const QString &text);
    void deleteCurrentLine();
    void copyCurrentLine();
    void cutCurrentLine();
    void joinLines();
    void duplicateSelection();
    void moveLine(int direction);
    void toggleComment();
    void toggleBookmark();
    void gotoBookmark(int direction);
    void refreshDiagnostics();

    std::function<void()> textChangedCallback;
    std::function<void()> cursorPositionChangedCallback;
    std::function<void(bool)> undoAvailableCallback;
    std::function<void(bool)> redoAvailableCallback;

    QHash<CodeEditor::Action, QAction *> actions;
    QSet<int> bookmarks;
    QHash<int, QList<CodeEditor::Diagnostic>> diagnostics;
    bool completionExclusiveValue = false;
    bool callTipVisible = false;
};
