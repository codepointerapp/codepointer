// SPDX-License-Identifier: MIT

#pragma once

#include "../../widgets/textoperationsadapter.h"

class QHexView;

class HexTextOperationsAdapter : public TextOperationsAdapter {
    Q_OBJECT
  public:
    explicit HexTextOperationsAdapter(QHexView *hexView, QObject *parent = nullptr);

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
    bool findInternal(const QString &text, qint64 startOffset, FindFlags flags, bool moveCursor);

    QHexView *m_hexView;
    qint64 m_searchStartOffset = 0;
};