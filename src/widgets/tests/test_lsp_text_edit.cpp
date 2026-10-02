/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QObject>
#include <QTest>

#include "plugins/LSP/lsp_text_edit.hpp"

/// Tests for applying edits to a file with no editor open.
///
/// This is the contract a second, Qt-free implementation has to satisfy, so
/// these cases are deliberately about behaviour - refusing a stale reply,
/// refusing a partial one, leaving the text alone - and not about how the text
/// is represented internally.
class TestLspTextEdit : public QObject {
    Q_OBJECT

  private slots:
    void anEmptyEditListLeavesTheTextAlone() {
        auto result = lspTextEdit::apply("one\ntwo", {});
        QVERIFY(!result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("one\ntwo"));
    }

    void aSingleEditIsApplied() {
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{0, 0, 0, 3, "ONE"},
        };
        auto result = lspTextEdit::apply("one\ntwo", edits);
        QVERIFY(result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("ONE\ntwo"));
    }

    void severalEditsApplyBottomUp() {
        // The order the caller sorts them in: descending, so a range stays valid
        // while the ones below it are rewritten.
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{2, 0, 2, 5, "THREE"},
            lspTextEdit::Edit{1, 0, 1, 3, "TWO"},
            lspTextEdit::Edit{0, 0, 0, 3, "ONE"},
        };
        auto result = lspTextEdit::apply("one\ntwo\nthree", edits);
        QVERIFY(result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("ONE\nTWO\nTHREE"));
    }

    void aRenameIsRefusedWholesaleWhenARangeDoesNotCoverTheSymbol() {
        // The reason this check exists at all. A reply computed against an older
        // file has ranges that are well-formed but name something else; applying
        // the ones that still line up renames the symbol in some places and not
        // others, producing a file that no longer compiles.
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{2, 0, 2, 4, "palette"}, // covers "nope", not "pal"
            lspTextEdit::Edit{1, 0, 1, 3, "palette"},
            lspTextEdit::Edit{0, 0, 0, 3, "palette"},
        };
        auto result = lspTextEdit::apply("pal\npal\nnope\n", edits, "pal");
        QVERIFY(!result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("pal\npal\nnope\n"));
        QCOMPARE(result.rejected.size(), size_t(1));
    }

    void aValidRenameReplacesEveryOccurrence() {
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{1, 0, 1, 3, "palette"},
            lspTextEdit::Edit{0, 0, 0, 3, "palette"},
        };
        auto result = lspTextEdit::apply("pal\npal", edits, "pal");
        QVERIFY(result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("palette\npalette"));
        QVERIFY(result.rejected.empty());
    }

    void anOutOfRangeEditIsRefusedRatherThanClamped() {
        // A range past the end of the file means the file is not what the server
        // thought it was. Clamping would paste the new text somewhere arbitrary.
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{42, 0, 42, 3, "palette"},
        };
        auto result = lspTextEdit::apply("pal", edits);
        QVERIFY(!result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("pal"));
    }

    void aRenameThatDoesNotCoverTheSymbolIsRefusedEvenOnTheFirstFile() {
        // Guards the specific case that silently corrupts a file nobody can see:
        // the file on disk moved on while the server worked.
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{0, 0, 0, 3, "palette"},
        };
        auto result = lspTextEdit::apply("nope", edits, "pal");
        QVERIFY(!result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("nope"));
        QCOMPARE(result.rejected.size(), size_t(1));
    }

    void aZeroWidthRangeInserts() {
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{0, 1, 0, 1, "-"},
        };
        auto result = lspTextEdit::apply("ab", edits);
        QVERIFY(result.applied);
        QCOMPARE(QString::fromStdString(result.text), QStringLiteral("a-b"));
    }

    // --- the shape a closed-file rename actually has -------------------------

    void aRenameAcrossAHeaderAndItsSource() {
        // The case that motivated all of this: renaming a public interface reaches
        // a header that was never opened. Both files are rewritten, and neither
        // is opened or left half-renamed.
        auto header = "class Widget {\npublic:\n    void draw();\n};\n";
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{2, 9, 2, 13, "render"},
        };
        auto result = lspTextEdit::apply(header, edits, "draw");
        QVERIFY(result.applied);
        QCOMPARE(QString::fromStdString(result.text),
                 QStringLiteral("class Widget {\npublic:\n    void render();\n};\n"));
    }

    void aRefusedRenameLeavesEveryByteOfTheFileIntact() {
        // What makes the headless path safe to run without a tab: a file the user
        // cannot see is either fully renamed or not touched at all.
        auto original = std::string("void draw();\nvoid draw();\n");
        auto edits = std::vector<lspTextEdit::Edit>{
            lspTextEdit::Edit{1, 0, 1, 12, "void render();"},
            lspTextEdit::Edit{0, 5, 0, 9, "paint"},
        };
        auto result = lspTextEdit::apply(original, edits, "draw");
        QVERIFY(!result.applied);
        QCOMPARE(result.text, original);
    }
};

QTEST_MAIN(TestLspTextEdit)
#include "test_lsp_text_edit.moc"
