/**
 * \file quteparteditor.h
 * \brief CodeEditorInterface implementation backed by Qutepart.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/codeeditorinterface.h"

class QAction;
class QMenu;
class QScrollBar;

namespace Qutepart {
class Qutepart;
class Theme;
} // namespace Qutepart

class QutepartCodeEditor : public CodeEditorInterface {
  public:
    explicit QutepartCodeEditor(QWidget *parent = nullptr);
    ~QutepartCodeEditor() override;

    QWidget *widget() override;
    QWidget *viewport() override;
    QScrollBar *verticalScrollBar() override;
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

    void setLineBookmark(int line, bool value) override;
    void setLineWarning(int line, bool value) override;
    void setLineError(int line, bool value) override;
    void setLineInfo(int line, bool value) override;
    void setLineBreakpoint(int line, bool value) override;
    void setLineExecuting(int line, bool value) override;

    QString highlighterId() const override;
    void setHighlighter(const QString &id) override;
    void removeHighlighter() override;
    void setIndentAlgorithm(CodeEditor::IndentAlgorithm algorithm) override;
    void setLineWrapMode(CodeEditor::WrapMode mode) override;
    void setThemeFile(const QString &themeFile) override;
    QString themeFile() const override;
    QStringList themes() const override;
    QString themeDisplayName(const QString &themeFile) const override;
    void setEditorFont(const QFont &font) override;

    void setDrawAnyWhitespace(bool value) override;
    void setDrawIndentations(bool value) override;
    void setBracketHighlightingEnabled(bool value) override;
    void setSoftLineWrapping(bool value) override;
    void setLineNumbersVisible(bool value) override;
    void setSmartHomeEnd(bool value) override;
    void setDrawSolidEdge(bool value) override;
    void setLineLengthEdge(int value) override;
    void setMarkAreaVisible(bool value) override;
    void setFoldingAreaVisible(bool value) override;
    void setMinimapVisible(bool value) override;
    void setMarkCurrentWord(bool value) override;
    void setFrameVisible(bool value) override;
    void foldTopLevel() override;

    QAction *action(CodeEditor::Action action) const override;
    QMenu *createStandardContextMenu() override;

  private:
    Qutepart::Qutepart *qutepart = nullptr;
    Qutepart::Theme *theme = nullptr;
    QString highlighterIdValue;
    QString themeFileValue;

    std::function<void()> textChangedCallback;
    std::function<void()> cursorPositionChangedCallback;
    std::function<void(bool)> undoAvailableCallback;
    std::function<void(bool)> redoAvailableCallback;
};
