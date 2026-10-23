/**
 * \file codeeditorinterface.h
 * \brief Abstract interface between the MDI editor and a concrete text editor.
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QFont>
#include <QFuture>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

class QAction;
class QMenu;
class QScrollBar;
class QWidget;
class TextOperationsAdapter;

namespace CodeEditor {

struct CompletionItem {
    QString text;
    QString source;

    bool operator==(const CompletionItem &other) const {
        return text == other.text && source == other.source;
    }
};

inline size_t qHash(const CompletionItem &item, size_t seed = 0) {
    return ::qHash(item.text, seed) ^ ::qHash(item.source, seed);
}

enum class DiagnosticSeverity { Error, Warning, Information, Hint };

struct Diagnostic {
    QString owner;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString message;
};

enum class IndentAlgorithm { None, Normal, CStyle, Lisp, Scheme, Xml, Python, Ruby, Markdown };

enum class WrapMode { NoWrap, WidgetWidth };

struct LanguageInfo {
    QString id;
    QStringList names;
    IndentAlgorithm indentAlgorithm = IndentAlgorithm::None;

    bool isValid() const { return !id.isEmpty(); }
};

struct TextRange {
    int startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
};

struct TextEdit {
    int startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
    QString newText;
};

struct CursorState {
    int line = 0, column = 0, anchorLine = 0, anchorColumn = 0;
};

enum class Action {
    DeleteLine,
    CutLine,
    CopyLine,
    PasteLine,
    JoinLines,
    DuplicateSelection,
    MoveLineUp,
    MoveLineDown,
    ToggleComment,
    ToggleBookmark,
    NextBookmark,
    PreviousBookmark,
    Fold,
    Unfold,
    FoldTopLevel,
    UnfoldAll,
    ToggleFold,
    FindMatchingBracket
};

using CompletionCallback = std::function<QFuture<QSet<CompletionItem>>(
    const QString &prefix, const QString &previousWord, const QString &separator)>;

using CodeActionCallback = std::function<void(int line)>;

} // namespace CodeEditor

/**
 * A text editor used by qmdiEditor.
 *
 * This is deliberately not a QObject: a backend is free to also inherit its
 * native widget (which is a QObject) without Qt's multiple-inheritance rules
 * getting in the way. Events are delivered through the callback setters below.
 *
 * The interface owns widget() and deletes it in its destructor. qmdiEditor owns
 * the interface itself.
 */
class CodeEditorInterface {
  public:
    virtual ~CodeEditorInterface() = default;

    virtual QWidget *widget() = 0;
    virtual QWidget *viewport() = 0;
    virtual QScrollBar *verticalScrollBar() = 0;
    virtual TextOperationsAdapter *createOperationsAdapter(QObject *parent) = 0;

    virtual void setTextChangedCallback(std::function<void()> callback) = 0;
    virtual void setCursorPositionChangedCallback(std::function<void()> callback) = 0;
    virtual void setUndoAvailableCallback(std::function<void(bool)> callback) = 0;
    virtual void setRedoAvailableCallback(std::function<void(bool)> callback) = 0;

    // Document lifecycle
    virtual QString text() const = 0;
    virtual QChar characterAt(int position) const = 0;
    virtual void setText(const QString &text) = 0;
    virtual void clear() = 0;
    virtual bool isEmpty() const = 0;
    virtual bool isModified() const = 0;
    virtual void setModified(bool modified) = 0;
    virtual bool isReadOnly() const = 0;
    virtual void setReadOnly(bool readOnly) = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void copy() = 0;
    virtual void cut() = 0;
    virtual void paste() = 0;
    virtual void setUndoRedoEnabled(bool enabled) = 0;
    virtual void removeModifications() = 0;

    // Loading and editing
    virtual void replaceText(const QString &text, bool undoable) = 0;
    virtual void replaceSelectedText(const QString &text) = 0;
    virtual void trimTrailingSpaces() = 0;
    virtual bool applyTextEdits(const QList<CodeEditor::TextEdit> &edits,
                                const QString &expectedOldName = {}) = 0;
    virtual bool findText(const QString &text) = 0;

    // Cursor and selection. All line and column values are 0-based.
    virtual int cursorPosition() const = 0;
    virtual int cursorLine() const = 0;
    virtual int cursorColumn() const = 0;
    virtual bool hasSelection() const = 0;
    virtual int selectionStart() const = 0;
    virtual int selectionEnd() const = 0;
    virtual QString selectedText() const = 0;
    virtual void setCursorPosition(int position, bool keepAnchor = false) = 0;
    virtual void setSelection(int anchor, int position) = 0;
    virtual void goTo(int line, int column) = 0;
    virtual CodeEditor::CursorState cursorState() const = 0;
    virtual void setCursorState(const CodeEditor::CursorState &state) = 0;
    virtual int positionAt(const QPoint &viewportPosition) const = 0;
    virtual int positionAt(int line, int column) const = 0;
    virtual int lineAt(int position) const = 0;
    virtual int columnAt(int position) const = 0;
    virtual int lineCount() const = 0;
    virtual QString lineText(int line) const = 0;
    virtual CodeEditor::TextRange wordRangeAt(int position) const = 0;
    virtual CodeEditor::TextRange selectionRange() const = 0;
    /// Rectangle of the caret at `position`, in viewport coordinates.
    virtual QRect cursorRect(int position) const = 0;

    // Completion, call tips, code actions
    virtual void setCompletionCallback(CodeEditor::CompletionCallback callback) = 0;
    virtual void setCompletionExclusive(bool exclusive) = 0;
    virtual bool completionExclusive() const = 0;
    virtual void setCodeActionCallback(CodeEditor::CodeActionCallback callback) = 0;
    virtual void setLineCodeAction(int line, bool available) = 0;
    virtual void showCallTip(const QString &text) = 0;
    virtual void updateCallTip(const QString &text) = 0;
    virtual void hideCallTip() = 0;
    virtual bool isCallTipVisible() const = 0;

    // Diagnostics and line marks
    virtual void setDiagnostic(int line, const CodeEditor::Diagnostic &diagnostic) = 0;
    virtual void clearDiagnostics(const QString &owner) = 0;
    virtual void clearAllDiagnostics() = 0;
    virtual QList<CodeEditor::Diagnostic> diagnosticsOn(int line) const = 0;
    virtual void setLineMessage(int line, const QString &message) = 0;
    virtual void removeMetaData() = 0;

    virtual void setLineBookmark(int, bool) {}
    virtual void setLineWarning(int, bool) {}
    virtual void setLineError(int, bool) {}
    virtual void setLineInfo(int, bool) {}
    virtual void setLineBreakpoint(int, bool) {}
    virtual void setLineExecuting(int, bool) {}

    // Language, theme and visual options
    virtual QString highlighterId() const = 0;
    virtual void setHighlighter(const QString &id) = 0;
    virtual void removeHighlighter() = 0;
    virtual void setIndentAlgorithm(CodeEditor::IndentAlgorithm algorithm) = 0;
    virtual void setLineWrapMode(CodeEditor::WrapMode mode) = 0;
    virtual void setThemeFile(const QString &themeFile) { Q_UNUSED(themeFile); }
    virtual QString themeFile() const { return {}; }
    virtual void setEditorFont(const QFont &font) = 0;

    virtual void setDrawAnyWhitespace(bool) {}
    virtual void setDrawIndentations(bool) {}
    virtual void setBracketHighlightingEnabled(bool) {}
    virtual void setSoftLineWrapping(bool) {}
    virtual void setLineNumbersVisible(bool) {}
    virtual void setSmartHomeEnd(bool) {}
    virtual void setDrawSolidEdge(bool) {}
    virtual void setLineLengthEdge(int) {}
    virtual void setMarkAreaVisible(bool) {}
    virtual void setFoldingAreaVisible(bool) {}
    virtual void setMinimapVisible(bool) {}
    virtual void setMarkCurrentWord(bool) {}
    virtual void setFrameVisible(bool) {}
    virtual void foldTopLevel() {}

    // Actions and menus
    virtual QAction *action(CodeEditor::Action action) const = 0;
    virtual QMenu *createStandardContextMenu() = 0;
};
