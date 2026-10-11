/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef LSP_DIAGNOSTIC_H
#define LSP_DIAGNOSTIC_H

#include <cstdint>
#include <string>

#include <QString>

#include <lsp/messages.h>

/// A published diagnostic, kept in the shape the protocol sent it so it can be
/// echoed back verbatim in a textDocument/codeAction request.
///
/// Kept apart from the plugin so the round trip can be tested on its own: a
/// lossy conversion is not a visible failure. The server just stops recognising
/// the diagnostic and returns no fixes, which is the same silence that made this
/// feature look unwired in the first place.
namespace lspDiagnostic {

/// Mirrors lsp::DiagnosticSeverity, which the framework enumerates in this same
/// order. Converting between the two is a single cast; see diagnostic.cpp.
enum class Severity { Error, Warning, Information, Hint };

/// Cached form of one publishDiagnostics entry.
///
/// Positions stay uint rather than int: narrowing to a Qt block number is the
/// consumer's job, not this cache's.
struct Diagnostic {
    uint line = 0;
    Severity severity = Severity::Error;
    QString message;

    // The spec defines these as surviving the trip back to the server in
    // textDocument/codeAction, which matches fixes against them. clangd keeps
    // its fix-it handle in `data`; send a diagnostic without it and the server
    // answers with no actions at all rather than fewer ones.
    uint startCharacter = 0;
    uint endLine = 0;
    uint endCharacter = 0;

    // `code` is a OneOf<Int, String> and must not be coerced: servers that
    // compare it strictly reject a number that arrives quoted.
    QString code;
    bool codeIsNumber = false;
    QString source;

    // `data` kept as JSON text. Round-tripping through the string form is what
    // keeps the fix-it handle intact without this header owning a
    // lsp::json::Value it would have to expose.
    QString rawData;
};

Diagnostic fromItem(const lsp::Diagnostic &item);

/// Rebuild the protocol diagnostic from the cache. Faithfulness matters more
/// than tidiness here: `data` is the server's own fix-it handle, and `code` is
/// compared strictly by some servers (Angular rejects a quoted number).
lsp::Diagnostic toItem(const Diagnostic &cached);

} // namespace lspDiagnostic

#endif // LSP_DIAGNOSTIC_H
