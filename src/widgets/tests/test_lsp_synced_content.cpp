/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QObject>
#include <QString>
#include <QTest>

#include <atomic>
#include <thread>
#include <vector>

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

    // --- syncIfStale: decision and record taken together --------------------

    void syncIfStalePushesWhenNothingIsRecorded() {
        SyncedContent cache;
        auto pushed = 0;
        auto didPush = cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("one"),
                                        [&pushed] { pushed++; });
        QVERIFY(didPush);
        QCOMPARE(pushed, 1);
    }

    void syncIfStaleSkipsWhenTheServerAlreadyHasTheText() {
        SyncedContent cache;
        auto pushed = 0;
        cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("one"),
                          [&pushed] { pushed++; });
        auto again = cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("one"),
                                       [&pushed] { pushed++; });
        QVERIFY(!again);
        // The whole point of the record: no duplicate didChange, which would make
        // the server re-parse the file for nothing.
        QCOMPARE(pushed, 1);
    }

    void syncIfStalePushesAgainOnceTheTextChanges() {
        SyncedContent cache;
        auto pushed = 0;
        cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("one"),
                          [&pushed] { pushed++; });
        cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("two"),
                          [&pushed] { pushed++; });
        QCOMPARE(pushed, 2);
        QVERIFY(!cache.isStale(QStringLiteral("/a.cpp"), QStringLiteral("two")));
    }

    void syncIfStaleRecordsTheTextItPushed() {
        SyncedContent cache;
        cache.syncIfStale(QStringLiteral("/a.cpp"), QStringLiteral("one"), [] {});
        QCOMPARE(cache.held(QStringLiteral("/a.cpp")), QStringLiteral("one"));
    }

    void syncIfStalePushesOnlyOnceUnderConcurrentCallers() {
        // Two threads can reach the gate for the same file at once - a
        // keystroke's debounced sync and a positional request, say. The decision
        // and the record have to happen under one lock, or both conclude a push
        // is needed and the server is sent the same didChange twice.
        SyncedContent cache;
        auto pushes = std::atomic<int>{0};
        const auto text = QStringLiteral("shared buffer text");

        auto racer = [&cache, &pushes, text] {
            cache.syncIfStale(QStringLiteral("/a.cpp"), text,
                              [&pushes] { pushes.fetch_add(1); });
        };

        std::vector<std::thread> threads;
        for (auto i = 0; i < 8; ++i) {
            threads.emplace_back(racer);
        }
        for (auto &thread : threads) {
            thread.join();
        }

        QCOMPARE(pushes.load(), 1);
        QVERIFY(!cache.isStale(QStringLiteral("/a.cpp"), text));
    }

    void heldIsEmptyForAnUnknownFile() {
        SyncedContent cache;
        QVERIFY(cache.held(QStringLiteral("/never-seen.cpp")).isNull());
    }
};

QTEST_MAIN(TestSyncedContent)
#include "test_lsp_synced_content.moc"
