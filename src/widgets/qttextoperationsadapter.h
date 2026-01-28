// SPDX-License-Identifier: MIT

#pragma once

#include "textoperationsadapter.h"
#include <QTextCursor>

class QWidget;
class QTextDocument;

class QtTextOperationsAdapter : public TextOperationsAdapter {
    Q_OBJECT
  public:
    explicit QtTextOperationsAdapter(QWidget *editor, QObject *parent = nullptr);

    QString selectedText() const override;
    bool find(const QString &text, FindFlags flags, bool moveCursor) override;
    bool findIncremental(const QString &text, FindFlags flags, bool moveCursor) override;
    void saveSearchStartPosition() override;

    bool canReplace() const override;
    void replace(const QString &searchText, const QString &replaceText, FindFlags flags) override;
    int replaceAll(const QString &searchText, const QString &replaceText, FindFlags flags) override;

    bool canGotoLine() const override;
    void gotoLine(int line) override;
    int lineCount() const override;
    int currentLine() const override;

  private:
    QTextCursor getTextCursor() const;
    void setTextCursor(const QTextCursor &cursor);
    QTextDocument *getTextDocument() const;
    QTextCursor findInternal(const QString &text, QTextCursor startCursor, FindFlags flags);

    QWidget *m_editor;
    QTextCursor m_searchCursor;
};
