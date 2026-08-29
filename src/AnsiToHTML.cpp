/**
 * \file AnsiToHTML.hpp
 * \brief Helper functions to convert ANSI codes to HTML - implementation
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPD-license: MIT

#include "AnsiToHTML.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QTextBlock>
#include <QTextEdit>
#include <QUrl>

auto isPlainText(const QString &str) -> bool {
    if (str.isEmpty()) {
        return true;
    }
    auto textCharCount = 0;
    auto totalCharCount = 0;
    for (const auto &ch : str) {
        totalCharCount++;
        if (ch == '\0') {
            return false;
        }
        if (ch.isLetterOrNumber() || ch.isPunct() || ch.isSpace() || ch.isSymbol() || ch.isMark()) {
            textCharCount++;
        } else if (ch.category() == QChar::Other_Control && ch.unicode() < 32) {
            if (ch == '\n' || ch == '\r' || ch == '\t') {
                textCharCount++;
            }
        }
    }
    auto textRatio = static_cast<double>(textCharCount) / totalCharCount;
    return textRatio > 0.90;
}

const QMap<int, QString> &defaultFgColorMap() {
    static const QMap<int, QString> fg = {
        {30, "#000000"}, {31, "#ff0000"}, {32, "#00ff00"}, {33, "#ffff00"},
        {34, "#0000ff"}, {35, "#ff00ff"}, {36, "#00ffff"}, {37, "#ffffff"},
        {90, "#888888"}, {91, "#ff5555"}, {92, "#55ff55"}, {93, "#ffff55"},
        {94, "#5555ff"}, {95, "#ff55ff"}, {96, "#55ffff"}, {97, "#ffffff"}};
    return fg;
}

const QMap<int, QString> &defaultBgColorMap() {
    static const QMap<int, QString> bg = {
        {40, "#000000"},  {41, "#ff0000"},  {42, "#00ff00"},  {43, "#ffff00"},
        {44, "#0000ff"},  {45, "#ff00ff"},  {46, "#00ffff"},  {47, "#ffffff"},
        {100, "#888888"}, {101, "#ff5555"}, {102, "#55ff55"}, {103, "#ffff55"},
        {104, "#5555ff"}, {105, "#ff55ff"}, {106, "#55ffff"}, {107, "#ffffff"}};
    return bg;
}

auto appendAscii(QTextEdit *edit, const QString &ansiText) -> void {
    edit->moveCursor(QTextCursor::End);
    edit->insertPlainText(ansiText);

    const auto blockCount = edit->document()->blockCount();
    const auto lastBlock = edit->document()->findBlockByNumber(blockCount - 1);
    if (lastBlock.isValid()) {
        QTextCursor cursor(lastBlock);
        cursor.movePosition(QTextCursor::StartOfBlock);
        edit->setTextCursor(cursor);
        // edit->centerCursor();
    }
}

void applyAnsiCodeToFormat(QTextCharFormat &fmt, const QString &codeStr) {
    const int code = codeStr.toInt();

    const auto &fgMap = defaultFgColorMap();
    const auto &bgMap = defaultBgColorMap();

    // Reset
    if (code == 0) {
        fmt = QTextCharFormat();
        return;
    }

    // Text style attributes
    switch (code) {
    case 1:
        fmt.setFontWeight(QFont::Bold);
        break;
    case 2:
        fmt.setFontWeight(QFont::Normal);
        break;
    case 3:
        fmt.setFontItalic(true);
        break;
    case 4:
        fmt.setFontUnderline(true);
        break;
    case 9:
        fmt.setFontStrikeOut(true);
        break;

    case 22:
        fmt.setFontWeight(QFont::Normal);
        break;
    case 23:
        fmt.setFontItalic(false);
        break;
    case 24:
        fmt.setFontUnderline(false);
        break;
    case 29:
        fmt.setFontStrikeOut(false);
        break;
    }

    // Foreground color
    if (fgMap.contains(code)) {
        fmt.setForeground(QColor(fgMap.value(code)));
    }

    // Background color
    if (bgMap.contains(code)) {
        fmt.setBackground(QColor(bgMap.value(code)));
    }
}

auto insertLinkifiedText(QTextCursor &cursor, const QString &text,
                         const QTextCharFormat &baseFormat, bool linkifyFiles,
                         const QString &baseDir) -> void
{
    static QRegularExpression unixPathRegex(
        R"(((?:~?/?[\w\-.+\\/ ]+|\./[\w\-.+/ ]+|\.\./[\w\-.+/ ]+)\.[a-zA-Z0-9+_-]{1,8})(?::(\d+))(?::(\d+))?(:)?)"
    );

    static QRegularExpression windowsPathRegex(
        R"(([A-Za-z]:[\\/][\w\-.+\\/ ]+\.[a-zA-Z0-9+_-]{1,8})(?:\((\d+)(?:,(\d+))?\)|:(\d+)(?::(\d+))?))"
    );

    static QRegularExpression urlRegex(
        R"((https?|file)://[^\s<>()]+)"
    );

    auto lastPos = 0;
    auto view = QStringView{text};

    QVector<QRegularExpression> regexes;

    if (linkifyFiles) {
        regexes = {
            windowsPathRegex,
            unixPathRegex,
            urlRegex
        };
    } else {
        regexes = {
            urlRegex
        };
    }

    struct MatchInfo {
        qsizetype start;
        qsizetype end;
        QString full;
        QString file;
        QString line;
        QString column;
        bool isUrl;
    };

    QVector<MatchInfo> matches;

    for (const auto &regex : regexes) {
        auto matchIter = regex.globalMatch(text);

        while (matchIter.hasNext()) {
            const auto match = matchIter.next();

            MatchInfo info;
            info.start = match.capturedStart();
            info.end = match.capturedEnd();
            info.full = match.captured(0);
            info.isUrl = regex == urlRegex;

            if (info.isUrl) {
                matches.append(std::move(info));
                continue;
            }

            if (regex == windowsPathRegex) {
                info.file = match.captured(1);

                // MSVC: file.cpp(167,41)
                if (!match.captured(2).isEmpty()) {
                    info.line = match.captured(2);
                    info.column = match.captured(3);
                }
                // GCC/Clang on Windows: file.cpp:167:41
                else {
                    info.line = match.captured(4);
                    info.column = match.captured(5);
                }
            } else {
                // Unix / relative GCC/Clang:
                // file.cpp:167:41
                info.file = match.captured(1);
                info.line = match.captured(2);
                info.column = match.captured(3);
            }

            matches.append(std::move(info));
        }
    }

    // The three regexes run independently, so put their matches back
    // into text order.
    std::sort(matches.begin(), matches.end(),
              [](const MatchInfo &a, const MatchInfo &b) {
                  return a.start < b.start;
              });

    for (const auto &match : matches) {
        // Ignore overlapping matches.
        if (match.start < lastPos) {
            continue;
        }

        if (match.start > lastPos) {
            cursor.insertText(
                view.sliced(lastPos, match.start - lastPos).toString(),
                baseFormat);
        }

        auto linkFmt = baseFormat;
        QUrl linkUrl;

        if (match.isUrl) {
            linkUrl = QUrl(match.full);
        } else {
            auto filePath = match.file;
            filePath.replace('\\', '/');

            auto fi = QFileInfo(filePath);

            if (fi.isRelative()) {
                filePath = QDir(baseDir).absoluteFilePath(filePath);
            }

            filePath = QDir::cleanPath(filePath);

            linkUrl = QUrl::fromLocalFile(filePath);

            if (!match.line.isEmpty()) {
                auto fragment = match.line;

                if (!match.column.isEmpty()) {
                    fragment += ',' + match.column;
                }

                linkUrl.setFragment(fragment);
            }
        }

        linkFmt.setAnchor(true);
        linkFmt.setAnchorHref(linkUrl.toString());
        linkFmt.setForeground(QBrush(Qt::blue));
        linkFmt.setFontUnderline(true);

        cursor.insertText(match.full, linkFmt);
        lastPos = match.end;
    }

    if (lastPos < text.length()) {
        cursor.insertText(
            view.sliced(lastPos).toString(),
            baseFormat);
    }
}


void appendAnsiText(QTextEdit *edit, const QString &ansiText, const QString &baseDir) {
    auto cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);

    static QRegularExpression ansiRegex("\x1b\\[([0-9;]*)m");
    static QRegularExpression removeRegex("\x1b\\[K");

    auto cleanedText = ansiText;
    cleanedText.remove(removeRegex);
    auto view = QStringView{cleanedText};

    auto matchIter = ansiRegex.globalMatch(cleanedText);
    auto lastPos = 0;
    auto fmt = QTextCharFormat{};
    auto codes = QStringList{};
    auto start = 0;
    auto linkifyFiles = true;

    while (matchIter.hasNext()) {
        auto match = matchIter.next();
        codes = match.captured(1).split(';');
        start = match.capturedStart();

        if (start > lastPos) {
            auto chunk = view.sliced(lastPos, start - lastPos).toString();
            insertLinkifiedText(cursor, chunk, fmt, linkifyFiles, baseDir);
        }

        if (codes.isEmpty() || codes.contains('0')) {
            fmt = QTextCharFormat(); // Reset
        } else {
            for (const auto &code : std::as_const(codes)) {
                applyAnsiCodeToFormat(fmt, code);
            }
        }

        lastPos = match.capturedEnd();
    }

    if (lastPos < view.length()) {
        insertLinkifiedText(cursor, view.sliced(lastPos).toString(), fmt, linkifyFiles, baseDir);
    }

    edit->setTextCursor(cursor);
    edit->ensureCursorVisible();
}

auto removeAnsiEscapeCodes(const QString &input) -> QString {
    static const QRegularExpression ansiRegex(
        R"((\x1B\[[0-9;?]*[ -/]*[@-~])|(\x1B\]8;[^\x07\x1B]*[\x07\x1B\\]))");
    auto result = input;
    return result.replace(ansiRegex, "");
}
