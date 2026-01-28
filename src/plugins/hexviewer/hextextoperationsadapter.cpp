// SPDX-License-Identifier: MIT

#include "hextextoperationsadapter.h"
#include <QHexView/qhexview.h>
#include <QHexView/model/qhexcursor.h>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/model/qhexutils.h>

HexTextOperationsAdapter::HexTextOperationsAdapter(QHexView *hexView, QObject *parent)
    : TextOperationsAdapter(parent), m_hexView(hexView) {}

QString HexTextOperationsAdapter::selectedText() const {
    return QString::fromUtf8(m_hexView->hexCursor()->selectedBytes());
}

bool HexTextOperationsAdapter::find(const QString &text, FindFlags flags, bool moveCursor) {
    return findInternal(text, m_hexView->hexCursor()->offset(), flags, moveCursor);
}

bool HexTextOperationsAdapter::findIncremental(const QString &text, FindFlags flags, bool moveCursor) {
    return findInternal(text, m_searchStartOffset, flags, moveCursor);
}

void HexTextOperationsAdapter::saveSearchStartPosition() {
    m_searchStartOffset = m_hexView->hexCursor()->offset();
}

bool HexTextOperationsAdapter::canReplace() const {
    return true;
}

void HexTextOperationsAdapter::replace(const QString &searchText, const QString &replaceText, FindFlags flags) {
    // The TextOperationsWidget expects replace() to replace the current match
    // (which should be selected) and update the incremental search position.
    if (m_hexView->hexCursor()->hasSelection()) {
        QString selected = QString::fromUtf8(m_hexView->hexCursor()->selectedBytes());
        bool match = (flags.testFlag(FindCaseSensitively)) 
            ? (selected == searchText) 
            : (selected.compare(searchText, Qt::CaseInsensitive) == 0);
        
        if (match) {
            qint64 offset = m_hexView->hexCursor()->selectionStartOffset();
            int len = m_hexView->hexCursor()->selectionLength();
            QByteArray ba = replaceText.toUtf8();
            
            if (len == ba.size()) {
                m_hexView->hexDocument()->replace(offset, ba);
            } else {
                m_hexView->hexDocument()->remove(offset, len);
                m_hexView->hexDocument()->insert(offset, ba);
            }
            
            // Move cursor to after the replacement and clear selection
            m_hexView->hexCursor()->move(offset + ba.size());
            m_searchStartOffset = offset + ba.size();
            m_hexView->viewport()->update();
        }
    }
}

int HexTextOperationsAdapter::replaceAll(const QString &searchText, const QString &replaceText, FindFlags flags) {
    int count = 0;
    unsigned int options = QHexFindOptions::None;
    if (flags.testFlag(FindCaseSensitively)) options |= QHexFindOptions::CaseSensitive;

    QByteArray searchBa = searchText.toUtf8();
    QByteArray replaceBa = replaceText.toUtf8();
    if (searchBa.isEmpty()) return 0;

    qint64 offset = 0;
    // We don't use QHexUtils::replace in a loop here because it might be slow 
    // and we want to control the iteration better.
    while (true) {
        auto result = QHexUtils::find(m_hexView, searchText, offset, QHexFindMode::Text, options, QHexFindDirection::Forward);
        if (result.first < 0) break;

        if (result.second == replaceBa.size()) {
            m_hexView->hexDocument()->replace(result.first, replaceBa);
        } else {
            m_hexView->hexDocument()->remove(result.first, result.second);
            m_hexView->hexDocument()->insert(result.first, replaceBa);
        }
        
        count++;
        offset = result.first + replaceBa.size();
        if (offset >= m_hexView->hexDocument()->length()) break;
    }
    m_hexView->viewport()->update();
    return count;
}

bool HexTextOperationsAdapter::canGotoLine() const {
    return true;
}

void HexTextOperationsAdapter::gotoLine(int line) {
    m_hexView->hexCursor()->move(m_hexView->hexCursor()->offsetToPosition((line - 1) * 16));
}

int HexTextOperationsAdapter::lineCount() const {
    return (m_hexView->hexDocument()->length() + 15) / 16;
}

int HexTextOperationsAdapter::currentLine() const {
    return (m_hexView->hexCursor()->offset() / 16) + 1;
}

bool HexTextOperationsAdapter::findInternal(const QString &text, qint64 startOffset, FindFlags flags, bool moveCursor) {
    QHexFindDirection fd = flags.testFlag(FindBackward) ? QHexFindDirection::Backward : QHexFindDirection::Forward;
    unsigned int options = QHexFindOptions::None;
    if (flags.testFlag(FindCaseSensitively)) options |= QHexFindOptions::CaseSensitive;

    auto result = QHexUtils::find(m_hexView, text, startOffset, QHexFindMode::Text, options, fd);
    
    // Wrap around if not found
    if (result.first < 0) {
        qint64 wrapOffset = flags.testFlag(FindBackward) ? m_hexView->hexDocument()->length() : 0;
        result = QHexUtils::find(m_hexView, text, wrapOffset, QHexFindMode::Text, options, fd);
    }

    if (result.first >= 0) {
        if (moveCursor) {
            m_hexView->hexCursor()->move(result.first);
            m_hexView->hexCursor()->selectSize(result.second);
        }
        return true;
    }
    return false;
}