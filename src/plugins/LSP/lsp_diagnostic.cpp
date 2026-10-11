/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "lsp_diagnostic.hpp"

#include <variant>

#include <lsp/json/json.h>

namespace lspDiagnostic {
namespace {

/// Both conversions below rest on the two enums being declared in the same
/// order. The assert keeps that honest if lspgen ever regenerates them apart.
auto toSeverity(lsp::DiagnosticSeverityEnum const &severity) -> Severity {
    static_assert(
        static_cast<int>(Severity::Error) == static_cast<int>(lsp::DiagnosticSeverity::Error) &&
        static_cast<int>(Severity::Hint) == static_cast<int>(lsp::DiagnosticSeverity::Hint));
    return static_cast<Severity>(severity.index());
}

auto fromSeverity(Severity severity) -> lsp::DiagnosticSeverityEnum {
    return lsp::DiagnosticSeverityEnum{static_cast<lsp::DiagnosticSeverity>(severity)};
}

} // namespace

Diagnostic fromItem(const lsp::Diagnostic &item) {
    auto out = Diagnostic{};
    out.line = item.range.start.line;
    out.severity = item.severity.has_value() ? toSeverity(*item.severity) : Severity::Error;
    out.message = std::holds_alternative<lsp::MarkupContent>(item.message)
                      ? QString::fromStdString(std::get<lsp::MarkupContent>(item.message).value)
                      : QString::fromStdString(std::get<lsp::String>(item.message));
    out.startCharacter = item.range.start.character;
    out.endLine = item.range.end.line;
    out.endCharacter = item.range.end.character;
    if (item.code.has_value()) {
        auto const &code = *item.code;
        if (std::holds_alternative<lsp::String>(code)) {
            out.code = QString::fromStdString(std::get<lsp::String>(code));
        } else {
            out.code = QString::number(std::get<lsp::Int>(code));
            out.codeIsNumber = true;
        }
    }
    if (item.source.has_value()) {
        out.source = QString::fromStdString(*item.source);
    }
    if (item.data.has_value()) {
        out.rawData = QString::fromStdString(lsp::json::stringify(*item.data));
    }
    return out;
}

lsp::Diagnostic toItem(const Diagnostic &cached) {
    auto out = lsp::Diagnostic{};
    out.range = {.start = {.line = cached.line, .character = cached.startCharacter},
                 .end = {.line = cached.endLine, .character = cached.endCharacter}};
    // The variant the server sent is not remembered, so the message goes back as
    // a plain String. clangd only ever sends that one.
    out.message = lsp::String{cached.message.toStdString()};
    out.severity = fromSeverity(cached.severity);
    if (!cached.code.isEmpty()) {
        out.code = cached.codeIsNumber
                       ? lsp::OneOf<lsp::Int, lsp::String>{lsp::Int{cached.code.toInt()}}
                       : lsp::OneOf<lsp::Int, lsp::String>{lsp::String{cached.code.toStdString()}};
    }
    if (!cached.source.isEmpty()) {
        out.source = cached.source.toStdString();
    }
    if (!cached.rawData.isEmpty()) {
        out.data = lsp::json::parse(cached.rawData.toStdString());
    }
    return out;
}

} // namespace lspDiagnostic
