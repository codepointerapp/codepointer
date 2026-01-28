// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

class TextOperationsAdapter : public QObject {
    Q_OBJECT
  public:
    enum FindFlag {
        NoFlags = 0x0,
        FindBackward = 0x1,
        FindCaseSensitively = 0x2,
        FindWholeWords = 0x4
    };
    Q_DECLARE_FLAGS(FindFlags, FindFlag)

    explicit TextOperationsAdapter(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~TextOperationsAdapter() = default;

    virtual QString selectedText() const = 0;

    /**
     * @brief Find text from current cursor position.
     * @return true if found.
     */
    virtual bool find(const QString &text, FindFlags flags, bool moveCursor) = 0;

    /**
     * @brief Find text from a saved start position (for incremental search).
     * @return true if found.
     */
    virtual bool findIncremental(const QString &text, FindFlags flags, bool moveCursor) = 0;

    /**
     * @brief Save the current cursor position as the start position for incremental search.
     */
    virtual void saveSearchStartPosition() = 0;

    virtual bool canReplace() const { return false; }
    virtual void replace(const QString &, const QString &, FindFlags) {}
    virtual int replaceAll(const QString &, const QString &, FindFlags) { return 0; }

    virtual bool canGotoLine() const { return false; }
    virtual void gotoLine(int) {}
    virtual int lineCount() const { return 0; }
    virtual int currentLine() const { return 0; }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TextOperationsAdapter::FindFlags)
