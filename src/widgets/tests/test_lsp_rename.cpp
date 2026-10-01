/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QObject>
#include <QTest>
#include <QTextDocument>

#include "widgets/lsp_rename_geometry.h"

using lspRename::Edit;

/// Tests for the geometry of an LSP rename. These cover the two ways the
/// feature corrupts a document rather than failing: resolving the wrong symbol
/// from a click, and positioning the server's ranges wrongly inside the buffer.
class TestLspRename : public QObject {
    Q_OBJECT

  private slots:
    // --- identifierAt: what the editor asks the server to rename -------------

    void identifierCoversWholeSymbolWhenClickedInTheMiddle() {
        // The click lands between the two l's; the whole name is still wanted.
        QCOMPARE(lspRename::identifierAt(QStringLiteral("int myVariable = 1;"), 0, 6),
                 QStringLiteral("myVariable"));
    }

    void identifierAtTheFirstCharacterOfTheLine() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("myVariable = 1;"), 0, 0),
                 QStringLiteral("myVariable"));
    }

    void identifierAtTheLastCharacterOfTheSymbol() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("int myVariable = 1;"), 0, 13),
                 QStringLiteral("myVariable"));
    }

    void identifierJustPastTheSymbolStillResolvesIt() {
        // Column 14 is the space right after the name. Clicking at the trailing
        // edge of a word is the common case, not a click on nothing, so the word
        // to the left is what gets renamed.
        QCOMPARE(lspRename::identifierAt(QStringLiteral("int myVariable = 1;"), 0, 14),
                 QStringLiteral("myVariable"));
    }

    void identifierHandlesUnderscoresAndDigits() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("_private2 = 1;"), 0, 3),
                 QStringLiteral("_private2"));
    }

    void identifierStopsAtAnOperator() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("a+b"), 0, 1), QStringLiteral("a"));
        QCOMPARE(lspRename::identifierAt(QStringLiteral("a+b"), 0, 2), QStringLiteral("b"));
    }

    void identifierOnTheSecondLine() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("one\ntwoThree\nfour"), 1, 4),
                 QStringLiteral("twoThree"));
    }

    void identifierOnAnEmptyLineIsEmpty() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("one\n\nthree"), 1, 0), QString());
    }

    void identifierOutOfRangeLinesAreEmpty() {
        QCOMPARE(lspRename::identifierAt(QStringLiteral("one\ntwo"), -1, 0), QString());
        QCOMPARE(lspRename::identifierAt(QStringLiteral("one\ntwo"), 9, 0), QString());
    }

    void identifierPastTheEndOfALineIsEmpty() {
        // The column is one past the line's last character, which is legal for a
        // cursor but points at no symbol.
        QCOMPARE(lspRename::identifierAt(QStringLiteral("one\ntwo"), 0, 4), QString());
    }

    // --- positionOf: mapping LSP positions onto the document ----------------

    void positionAtStartOfDocument() {
        QTextDocument document(QStringLiteral("one\ntwo"));
        QCOMPARE(lspRename::positionOf(&document, 0, 0), 0);
    }

    void positionInsideTheFirstLine() {
        QTextDocument document(QStringLiteral("one\ntwo"));
        QCOMPARE(lspRename::positionOf(&document, 0, 2), 2);
    }

    void positionAtStartOfSecondLineSkipsTheNewline() {
        QTextDocument document(QStringLiteral("one\ntwo"));
        // "one\n" is four characters, so line 1 starts at offset 4.
        QCOMPARE(lspRename::positionOf(&document, 1, 0), 4);
    }

    void positionOnePastTheEndOfALineIsTheNewline() {
        QTextDocument document(QStringLiteral("one\ntwo"));
        // End-exclusive LSP ranges regularly address one past the last
        // character. That must resolve to the block separator, not to the first
        // character of the next line, or an edit ending a line eats into the
        // text below it. QTextDocument keeps blocks apart with U+2029 and
        // converts back to '\n' only in toPlainText().
        QCOMPARE(lspRename::positionOf(&document, 0, 3), 3);
        QCOMPARE(document.characterAt(3), QChar::ParagraphSeparator);
    }

    void positionRejectsAColumnPastTheEndOfALine() {
        // An over-long character is not a position in this document. Rejecting
        // it is what stops a stale reply from being clamped onto the line and
        // pasting a name into unrelated code.
        QTextDocument document(QStringLiteral("one\ntwo"));
        QCOMPARE(lspRename::positionOf(&document, 0, 99), -1);
    }

    void positionOnAnInvalidLineIsMinusOne() {
        QTextDocument document(QStringLiteral("one\ntwo"));
        QCOMPARE(lspRename::positionOf(&document, 9, 0), -1);
    }

    void positionOfANullDocumentIsMinusOne() { QCOMPARE(lspRename::positionOf(nullptr, 0, 0), -1); }

    // --- applyEdits: the whole rename response, applied ----------------------

    void applyASingleRename() {
        QTextDocument document(QStringLiteral("int myVariable = 1;"));
        auto edits = QList<Edit>{Edit{0, 4, 0, 14, QStringLiteral("myRenamed")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("int myRenamed = 1;"));
    }

    void applyMultipleEditsBottomUpAsThePluginSortsThem() {
        // Mirrors a real response: several occurrences, sorted bottom-up by the
        // plugin before they reach here. Applying top-down would shift the
        // ranges of the edits still to come, which is the classic way this
        // feature scatters the new name through the file.
        QTextDocument document(QStringLiteral("one\ntwo\nthree"));
        auto edits = QList<Edit>{
            Edit{2, 0, 2, 5, QStringLiteral("C")},
            Edit{1, 0, 1, 3, QStringLiteral("B")},
            Edit{0, 0, 0, 3, QStringLiteral("A")},
        };
        QCOMPARE(lspRename::applyEdits(&document, edits), 3);
        QCOMPARE(document.toPlainText(), QStringLiteral("A\nB\nC"));
    }

    void applyAnEditThatEndsALineDoesNotEatTheNextOne() {
        // The end-exclusive range stops at end-of-line. If the mapping resolved
        // that to the character after the newline, the space at the start of
        // line 2 would be swallowed.
        QTextDocument document(QStringLiteral("abc\n  def"));
        auto edits = QList<Edit>{Edit{0, 0, 0, 3, QStringLiteral("X")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("X\n  def"));
    }

    void applyEditsOnTheLastLine() {
        QTextDocument document(QStringLiteral("one\ntwoThree"));
        auto edits = QList<Edit>{Edit{1, 3, 1, 8, QStringLiteral("X")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("one\ntwoX"));
    }

    void applyEditsReturnsZeroForNoEdits() {
        QTextDocument document(QStringLiteral("one"));
        QCOMPARE(lspRename::applyEdits(&document, QList<Edit>{}), 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("one"));
    }

    void applyEditsSkipsAnOutOfRangeEditRatherThanClamping() {
        // A response computed against a stale document can name a line that no
        // longer exists. Dropping it loses one rename; clamping it would paste
        // the name into unrelated code.
        QTextDocument document(QStringLiteral("one\ntwo"));
        auto edits = QList<Edit>{
            Edit{9, 0, 9, 3, QStringLiteral("BAD")},
            Edit{0, 0, 0, 3, QStringLiteral("A")},
        };
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("A\ntwo"));
    }

    void applyEditsSkipsAnInvertedRange() {
        QTextDocument document(QStringLiteral("abcdef"));
        // end before start is nonsense; it must not delete backwards.
        auto edits = QList<Edit>{Edit{0, 4, 0, 2, QStringLiteral("X")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("abcdef"));
    }

    void applyEditsRejectsANegativeColumnRatherThanClampingToZero() {
        // This is the corruption seen in the field: a negative column clamped to
        // 0 pastes the replacement at the start of the line and leaves the
        // original symbol untouched, so the file gains a stray name.
        QTextDocument document(QStringLiteral("    pal.setColor();"));
        auto edits = QList<Edit>{Edit{0, -5, 0, -2, QStringLiteral("palette")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("    pal.setColor();"));
    }

    void applyEditsRejectsAColumnPastTheLineEnd() {
        QTextDocument document(QStringLiteral("ab"));
        auto edits = QList<Edit>{Edit{0, 0, 0, 99, QStringLiteral("X")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("ab"));
    }

    // --- rename replies are checked against the symbol they rename -----------

    void applyRenameEditsReplacesEveryOccurrenceOfTheOldName() {
        auto text = QStringLiteral("    void polish(QPalette &pal) override {\n"
                                   "        QProxyStyle::polish(pal);\n"
                                   "        pal.setColor(QPalette::Highlight, tint);\n"
                                   "        pal.setColor(QPalette::HighlightedText, Qt::white);\n"
                                   "    }\n");
        QTextDocument document(text);
        // Bottom-up, as the caller sorts them. Columns are where `pal` actually
        // sits on each line: 26 and 28 inside the signatures, 8 at the start
        // of each call.
        auto edits = QList<Edit>{
            Edit{3, 8, 3, 11, QStringLiteral("palette")},
            Edit{2, 8, 2, 11, QStringLiteral("palette")},
            Edit{1, 28, 1, 31, QStringLiteral("palette")},
            Edit{0, 26, 0, 29, QStringLiteral("palette")},
        };
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 4);
        QVERIFY(rejected.isEmpty());
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("    void polish(QPalette &palette) override {\n"
                                "        QProxyStyle::polish(palette);\n"
                                "        palette.setColor(QPalette::Highlight, tint);\n"
                                "        palette.setColor(QPalette::HighlightedText, Qt::white);\n"
                                "    }\n"));
    }

    void applyRenameEditsRefusesAReplyForADifferentRevision() {
        // The server answered about a document where the symbol was already
        // renamed, so its ranges name something else. Applying it would paste
        // the new name next to the old symbol; refusing leaves the file alone.
        QTextDocument document(QStringLiteral("    pal.setColor();\n"));
        auto edits = QList<Edit>{Edit{0, 4, 0, 4, QStringLiteral("palette\n    ")}};
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("    pal.setColor();\n"));
        QCOMPARE(rejected.size(), 1);
    }

    void applyRenameEditsIsAllOrNothing() {
        // A rename is atomic. The server computed every range against the same
        // snapshot, so if the buffer moved on while it worked, *all* the ranges
        // are stale, not just the unlucky ones. Applying the subset that still
        // happens to line up renames the symbol in some places and not others,
        // which is a file that no longer compiles - worse than doing nothing.
        QTextDocument document(QStringLiteral("pal\npal\nnope\n"));
        auto edits = QList<Edit>{
            Edit{2, 0, 2, 4, QStringLiteral("palette")}, // covers "nope", not "pal"
            Edit{1, 0, 1, 3, QStringLiteral("palette")}, // fine
            Edit{0, 0, 0, 3, QStringLiteral("palette")}, // fine
        };
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 0);
        // Nothing applied: not the two valid edits either.
        QCOMPARE(document.toPlainText(), QStringLiteral("pal\npal\nnope\n"));
        QCOMPARE(rejected.size(), 1);
    }

    void applyRenameEditsRefusesAnOutOfRangeRange() {
        QTextDocument document(QStringLiteral("pal"));
        auto edits = QList<Edit>{Edit{42, 0, 42, 3, QStringLiteral("palette")}};
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("pal"));
        QCOMPARE(rejected.size(), 1);
    }

    void applyRenameEditsRefusesARangeSpanningLines() {
        // An identifier is on one line; a range crossing a paragraph break is
        // not a rename of it.
        QTextDocument document(QStringLiteral("pal\npal"));
        auto edits = QList<Edit>{Edit{0, 0, 1, 3, QStringLiteral("palette")}};
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 0);
        QCOMPARE(document.toPlainText(), QStringLiteral("pal\npal"));
        QCOMPARE(rejected.size(), 1);
    }

    void applyRenameEditsIsOneUndoStep() {
        QTextDocument document(QStringLiteral("pal\npal"));
        auto edits = QList<Edit>{
            Edit{1, 0, 1, 3, QStringLiteral("sym")},
            Edit{0, 0, 0, 3, QStringLiteral("sym")},
        };
        auto rejected = QStringList();
        QCOMPARE(lspRename::applyRenameEdits(&document, edits, QStringLiteral("pal"), &rejected),
                 2);
        document.undo();
        QCOMPARE(document.toPlainText(), QStringLiteral("pal\npal"));
    }

    void applyEditsIsOneUndoStep() {
        QTextDocument document(QStringLiteral("one\ntwo\nthree"));
        auto edits = QList<Edit>{
            Edit{2, 0, 2, 5, QStringLiteral("C")},
            Edit{1, 0, 1, 3, QStringLiteral("B")},
            Edit{0, 0, 0, 3, QStringLiteral("A")},
        };
        lspRename::applyEdits(&document, edits);
        document.undo();
        QCOMPARE(document.toPlainText(), QStringLiteral("one\ntwo\nthree"));
    }

    void applyEditsWithAMultiLineReplacement() {
        QTextDocument document(QStringLiteral("keep\ntargetValue\nkeep"));
        auto edits = QList<Edit>{Edit{1, 0, 1, 11, QStringLiteral("first\nsecond")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("keep\nfirst\nsecond\nkeep"));
    }

    // --- ranges that do not describe this document --------------------------
    //
    // A server answers against the revision it was last sent. If the buffer
    // moved on, its ranges are still well-formed - they simply name ranges that
    // mean something else here. Applying them then corrupts the file instead of
    // failing, which is how a rename ends up with its replacement text wedged
    // between indentation and the original symbol.

    void aZeroWidthRangeInsertsRatherThanReplaces() {
        // This is the shape observed in the field: newText carrying a newline
        // plus the indentation, pasted at the column where the symbol started,
        // leaving the symbol itself untouched. Pinned here so the behaviour is
        // a known quantity rather than a surprise - the defence belongs in the
        // caller, which must check the revision before applying.
        QTextDocument document(QStringLiteral("    pal.setColor(Qt::white);\n"));
        auto edits = QList<Edit>{Edit{0, 4, 0, 4, QStringLiteral("palette\n    ")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(),
                 QStringLiteral("    palette\n    pal.setColor(Qt::white);\n"));
    }

    void aRangeNarrowerThanItsReplacementIsPastedNotSwapped() {
        // A range computed against a document where the identifier was shorter
        // cuts the name in half: the replacement lands, the tail stays. This is
        // the corruption to guard against, and the reason the caller compares the
        // buffer against the text the request was issued for.
        QTextDocument document(QStringLiteral("int pal = 1;"));
        // Server thought the identifier was "pa" (stale, longer original).
        auto edits = QList<Edit>{Edit{0, 4, 0, 6, QStringLiteral("palette")}};
        QCOMPARE(lspRename::applyEdits(&document, edits), 1);
        QCOMPARE(document.toPlainText(), QStringLiteral("int palettel = 1;"));
    }
};

QTEST_MAIN(TestLspRename)
#include "test_lsp_rename.moc"
