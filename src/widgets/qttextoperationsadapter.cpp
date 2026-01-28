// SPDX-License-Identifier: MIT

#include "qttextoperationsadapter.h"
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextBlock>

QtTextOperationsAdapter::QtTextOperationsAdapter(QWidget *editor, QObject *parent)
    : TextOperationsAdapter(parent), m_editor(editor) {}

QString QtTextOperationsAdapter::selectedText() const {
    return getTextCursor().selectedText();
}

bool QtTextOperationsAdapter::find(const QString &text, FindFlags flags, bool moveCursor) {
    auto c = findInternal(text, getTextCursor(), flags);
    if (!c.isNull()) {
        if (moveCursor) setTextCursor(c);
        return true;
    }
    return false;
}

bool QtTextOperationsAdapter::findIncremental(const QString &text, FindFlags flags, bool moveCursor) {
    auto c = findInternal(text, m_searchCursor, flags);
    if (!c.isNull()) {
        if (moveCursor) setTextCursor(c);
        return true;
    }
    return false;
}

void QtTextOperationsAdapter::saveSearchStartPosition() {
    m_searchCursor = getTextCursor();
}

bool QtTextOperationsAdapter::canReplace() const {
    return getTextDocument() != nullptr;
}

void QtTextOperationsAdapter::replace(const QString &searchText, const QString &replaceText,
                                      FindFlags flags) {
    auto doc = getTextDocument();
    if (!doc) return;

    QTextCursor cursor = getTextCursor();
    
    // If we have a selection and it matches, replace it
    if (cursor.hasSelection()) {
        QString selected = cursor.selectedText();
        bool match = (flags.testFlag(FindCaseSensitively)) 
            ? (selected == searchText) 
            : (selected.compare(searchText, Qt::CaseInsensitive) == 0);
            
        if (match) {
            cursor.beginEditBlock();
            cursor.insertText(replaceText);
            cursor.endEditBlock();
            m_searchCursor = cursor;
        }
    }
}

int QtTextOperationsAdapter::replaceAll(const QString &searchText, const QString &replaceText,
                                         FindFlags flags) {
    auto doc = getTextDocument();
    if (!doc) return 0;

    int count = 0;
    auto qflags = (QTextDocument::FindFlags)(int)flags;
    auto cursor = doc->find(searchText, getTextCursor(), qflags);
    while (!cursor.isNull()) {
        cursor.beginEditBlock();
        cursor.deleteChar();
        cursor.insertText(replaceText);
        cursor.endEditBlock();
        count++;
        cursor = doc->find(searchText, cursor, qflags);
    }
    return count;
}

bool QtTextOperationsAdapter::canGotoLine() const {
    return getTextDocument() != nullptr;
}

void QtTextOperationsAdapter::gotoLine(int line) {
    auto doc = getTextDocument();
    if (!doc) return;

    auto block = doc->findBlockByNumber(line - 1);
    setTextCursor(QTextCursor(block));
}

int QtTextOperationsAdapter::lineCount() const {
    auto doc = getTextDocument();
    return doc ? doc->blockCount() : 0;
}

int QtTextOperationsAdapter::currentLine() const {
    return getTextCursor().blockNumber() + 1;
}

QTextCursor QtTextOperationsAdapter::getTextCursor() const {
    if (auto textEdit = qobject_cast<QTextEdit *>(m_editor)) {
        return textEdit->textCursor();
    } else if (auto plainTextEdit = qobject_cast<QPlainTextEdit *>(m_editor)) {
        return plainTextEdit->textCursor();
    }
    return QTextCursor();
}

void QtTextOperationsAdapter::setTextCursor(const QTextCursor &cursor) {
    if (auto textEdit = qobject_cast<QTextEdit *>(m_editor)) {
        textEdit->setTextCursor(cursor);
    } else if (auto plainTextEdit = qobject_cast<QPlainTextEdit *>(m_editor)) {
        plainTextEdit->setTextCursor(cursor);
    }
}

QTextDocument *QtTextOperationsAdapter::getTextDocument() const {
    if (auto textEdit = qobject_cast<QTextEdit *>(m_editor)) {
        return textEdit->document();
    } else if (auto plainTextEdit = qobject_cast<QPlainTextEdit *>(m_editor)) {
        return plainTextEdit->document();
    }
    return nullptr;
}

QTextCursor QtTextOperationsAdapter::findInternal(const QString &text, QTextCursor startCursor,
                                                  FindFlags flags) {
    auto doc = getTextDocument();
    if (!doc) return QTextCursor();

    QTextDocument::FindFlags qflags;
    if (flags.testFlag(FindBackward)) qflags |= QTextDocument::FindBackward;
    if (flags.testFlag(FindCaseSensitively)) qflags |= QTextDocument::FindCaseSensitively;
    if (flags.testFlag(FindWholeWords)) qflags |= QTextDocument::FindWholeWords;

    auto c = doc->find(text, startCursor, qflags);

    if (c.isNull()) {
        c = QTextCursor(doc);
        c.movePosition(flags & FindBackward ? QTextCursor::End : QTextCursor::Start);
        c = doc->find(text, c, qflags);
    }

    return c;
}