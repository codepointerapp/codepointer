/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QObject>
#include <QString>
#include <QTest>

#include "plugins/LSP/lsp_synced_content.h"

/// Tests for the record of what each language server is known to be holding.
///
/// This is the gate in front of every positional request. When it wrongly
/// reports "already synced", the server is never sent the current buffer and
/// answers with ranges for text the editor no longer has - a well-formed,
/// silently wrong reply, which is what made a rename scatter the new name
/// through the file.
class TestSyncedContent : public QObject {
    Q_OBJECT

  private slots:
    void anUnknownFileIsStale() {
        SyncedContent cache;
        QVERIFY(cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("one")));
    }

    void recordedTextIsNotStale() {
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QStringLiteral("one"));
        QVERIFY(!cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("one")));
    }

    void differentTextIsStale() {
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QStringLiteral("one"));
        QVERIFY(cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("two")));
    }

    void anEmptyBufferIsStillContent() {
        // An empty document is a real revision. Treating "" as "nothing known"
        // would make every request on an empty file re-sync forever.
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QString());
        QVERIFY(!cache.isStale(QStringLiteral("/a.cpp"), QString()));
    }

    void filesAreTrackedSeparately() {
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QStringLiteral("one"));
        QVERIFY(cache.isStale(QStringLiteral("/b.cpp"), QStringLiteral("one")));
        QVERIFY(!cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("one")));
    }

    void forgetMakesAFileStaleAgain() {
        // A closed document, and a replaced server, both invalidate the record:
        // the next server holds nothing.
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QStringLiteral("one"));
        cache.forget(QStringLiteral("/a.cpp"));
        QVERIFY(cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("one")));
    }

    void clearForgetsEverything() {
        SyncedContent cache;
        cache.record(QStringLiteral("/a.cpp"), QStringLiteral("one"));
        cache.record(QStringLiteral("/b.cpp"), QStringLiteral("two"));
        cache.clear();
        QVERIFY(cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("one")));
        QVERIFY(cache.isStale(QStringLiteral("/b.cpp"), QStringLiteral("two")));
    }

    void heldIsEmptyForAnUnknownFile() {
        SyncedContent cache;
        QVERIFY(cache.held(QStringLiteral("/never-seen.cpp")).isNull());
    }
};

QTEST_MAIN(TestSyncedContent)
#include "test_lsp_synced_content.moc"
