/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QTest>

#include <lsp/json/json.h>

#include "plugins/LSP/lsp_diagnostic.hpp"

using namespace lspDiagnostic;

/// The conversion has one job: whatever the server sends must come back
/// unchanged. Anything dropped here does not fail loudly - the server simply
/// stops offering fixes for that diagnostic, which reads as "the feature does
/// not work" rather than "we corrupted a field".
class LspDiagnosticTest : public QObject {
    Q_OBJECT

  private slots:
    void roundTripsPositions();
    void roundTripsSeverity();
    void roundTripsStringCode();
    void keepsNumericCodeNumeric();
    void roundTripsSource();
    void roundTripsData();
    void survivesMarkupMessage();
    void defaultsWhenServerOmitsFields();
};

void LspDiagnosticTest::roundTripsPositions() {
    auto item = lsp::Diagnostic{};
    item.range = {.start = {.line = 42, .character = 7}, .end = {.line = 43, .character = 19}};
    item.message = lsp::String{"something"};

    auto back = toItem(fromItem(item));
    QCOMPARE(back.range.start.line, 42u);
    QCOMPARE(back.range.start.character, 7u);
    QCOMPARE(back.range.end.line, 43u);
    QCOMPARE(back.range.end.character, 19u);
}

void LspDiagnosticTest::roundTripsSeverity() {
    for (auto severity : {lsp::DiagnosticSeverity::Error, lsp::DiagnosticSeverity::Warning,
                          lsp::DiagnosticSeverity::Information, lsp::DiagnosticSeverity::Hint}) {
        auto item = lsp::Diagnostic{};
        item.message = lsp::String{"m"};
        item.severity = lsp::DiagnosticSeverityEnum{severity};
        auto back = toItem(fromItem(item));
        QVERIFY(back.severity.has_value());
        QCOMPARE(back.severity->index(), severity);
    }
}

void LspDiagnosticTest::roundTripsStringCode() {
    auto item = lsp::Diagnostic{};
    item.message = lsp::String{"m"};
    item.code = lsp::OneOf<lsp::Int, lsp::String>{lsp::String{"err-one"}};

    auto back = toItem(fromItem(item));
    QVERIFY(back.code.has_value());
    QVERIFY(std::holds_alternative<lsp::String>(*back.code));
    QCOMPARE(QString::fromStdString(std::get<lsp::String>(*back.code)), QString("err-one"));
}

void LspDiagnosticTest::keepsNumericCodeNumeric() {
    // Angular compares these codes strictly and rejects a quoted number, which
    // is how Zed lost fixes until PR #24347.
    auto item = lsp::Diagnostic{};
    item.message = lsp::String{"m"};
    item.code = lsp::OneOf<lsp::Int, lsp::String>{lsp::Int{600}};

    auto back = toItem(fromItem(item));
    QVERIFY(back.code.has_value());
    QVERIFY(std::holds_alternative<lsp::Int>(*back.code));
    QCOMPARE(std::get<lsp::Int>(*back.code), 600);
}

void LspDiagnosticTest::roundTripsSource() {
    auto item = lsp::Diagnostic{};
    item.message = lsp::String{"m"};
    item.source = "clang";

    auto back = toItem(fromItem(item));
    QVERIFY(back.source.has_value());
    QCOMPARE(QString::fromStdString(*back.source), QString("clang"));
}

void LspDiagnosticTest::roundTripsData() {
    // The field the whole feature rests on: it is where clangd keeps the handle
    // it uses to recognise the diagnostic when we ask for a code action.
    auto parsed = lsp::json::parse(R"({"index":3,"fixKind":"add-include"})");
    auto item = lsp::Diagnostic{};
    item.message = lsp::String{"m"};
    item.data = parsed;

    auto back = toItem(fromItem(item));
    QVERIFY(back.data.has_value());
    QCOMPARE(lsp::json::stringify(*back.data), lsp::json::stringify(parsed));
}

void LspDiagnosticTest::survivesMarkupMessage() {
    auto item = lsp::Diagnostic{};
    item.message = lsp::MarkupContent{.kind = lsp::MarkupKind::Markdown, .value = "**bold**"};

    // The variant is not remembered, so it comes back as a plain String. Assert
    // the text survives; the variant loss is documented, not a silent surprise.
    auto back = toItem(fromItem(item));
    QVERIFY(std::holds_alternative<lsp::String>(back.message));
    QCOMPARE(QString::fromStdString(std::get<lsp::String>(back.message)), QString("**bold**"));
}

void LspDiagnosticTest::defaultsWhenServerOmitsFields() {
    auto item = lsp::Diagnostic{};
    item.message = lsp::String{"bare"};

    auto cached = fromItem(item);
    QCOMPARE(cached.severity, Severity::Error); // absent severity is an error
    QVERIFY(cached.code.isEmpty());
    QVERIFY(cached.source.isEmpty());
    QVERIFY(cached.rawData.isEmpty());

    auto back = toItem(cached);
    QVERIFY(!back.code.has_value());
    QVERIFY(!back.source.has_value());
    QVERIFY(!back.data.has_value());
    QVERIFY(back.severity.has_value());
}

QTEST_MAIN(LspDiagnosticTest)
#include "test_lsp_diagnostic.moc"
