/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef LSP_SYNCED_CONTENT_H
#define LSP_SYNCED_CONTENT_H

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

/// Remembers the text each language server is known to be holding, so an
/// unchanged buffer does not cause a redundant didChange.
///
/// The record of what was sent matters more than it looks. A positional request
/// - hover, completion, signature help, rename - is only meaningful against the
/// document the server actually has, so this is consulted before every one of
/// them. When the record is wrong in the optimistic direction the server never
/// gets the current text, and it answers with ranges computed for text the
/// editor no longer holds: the reply is well-formed and silently wrong, which is
/// how a rename ends up scattering the new name through the file.
///
/// Every path that pushes text to a server must record it here. That is the
/// whole invariant, and it is the one that was previously broken: the
/// unconditional push in syncDocument() did not record, so the cache could
/// claim a revision the server had never seen.
class SyncedContent {
  public:
    /// True when the server does not already hold exactly `text` for `fileName`,
    /// meaning a didChange has to go out before a positional request.
    bool isStale(const QString &fileName, const QString &text) const {
        auto locker = QMutexLocker(&m_mutex);
        return m_contents.value(fileName) != text;
    }

    /// Records that `text` is what the server now holds for `fileName`. Call
    /// this only after the text has actually been sent.
    void record(const QString &fileName, const QString &text) {
        auto locker = QMutexLocker(&m_mutex);
        m_contents[fileName] = text;
    }

    /// Forgets `fileName`, so the next check reports it stale. Used when a
    /// document is closed, and when a server is replaced, since a fresh server
    /// holds nothing even if the old one did.
    void forget(const QString &fileName) {
        auto locker = QMutexLocker(&m_mutex);
        m_contents.remove(fileName);
    }

    void clear() {
        auto locker = QMutexLocker(&m_mutex);
        m_contents.clear();
    }

    /// The text believed to be held for `fileName`, or a null string when
    /// nothing is recorded. For tests and diagnostics.
    QString held(const QString &fileName) const {
        auto locker = QMutexLocker(&m_mutex);
        return m_contents.value(fileName);
    }

    /// Runs `push` and records `text` as held, but only if the server did not
    /// already have it. Returns true when the push ran.
    ///
    /// The decision and the record are taken under one lock, so two callers
    /// racing on the same text cannot both conclude a push is needed and send a
    /// duplicate didChange. Recording before `push` rather than after is
    /// deliberate: it keeps the critical section free of I/O, and the failure it
    /// cannot handle - a send that dies - is already handled by the caller
    /// forgetting the entry.
    template <typename Push> bool syncIfStale(const QString &fileName, const QString &text,
                                             Push &&push) {
        {
            auto locker = QMutexLocker(&m_mutex);
            if (m_contents.value(fileName) == text) {
                return false;
            }
            m_contents[fileName] = text;
        }
        push();
        return true;
    }

  private:
    mutable QMutex m_mutex;
    QHash<QString, QString> m_contents;
};

#endif // LSP_SYNCED_CONTENT_H
