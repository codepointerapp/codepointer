/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QTest>

#include <lsp/json/json.h>

#include "plugins/LSP/LspClientImpl.hpp"

/// The capabilities we advertise decide what the server is allowed to send back,
/// and nothing about that is visible from the editor: if the object fails to
/// serialise, or a field is named wrong, the server simply answers as if we had
/// never asked. So assert the JSON rather than the struct.
class LspCapabilitiesTest : public QObject {
    Q_OBJECT

  private slots:
    void advertisesCodeActionSupport();
  private:
    /// Serialise the way initializeLspServer does, by hand rather than by
    /// reaching into the client, so this test needs no running server.
    auto serialized() const -> lsp::json::Value;
};

auto LspCapabilitiesTest::serialized() const -> lsp::json::Value {
    auto text = std::string{};
    {
        lsp::json::Writer writer;
        {
            auto objectWriter = writer.beginObject();
            lsp::writeJson(clientCapabilities(), objectWriter);
        }
        text = writer.text();
    }
    return lsp::json::parse(text);
}

void LspCapabilitiesTest::advertisesCodeActionSupport() {
    auto const caps = serialized();
    QVERIFY(caps.isObject());
    auto const &root = caps.object();

    // clangd reports no indexing progress without this one.
    auto window = root.find("window");
    QVERIFY(window != nullptr);
    QVERIFY(window->isObject());
    auto workDone = window->object().find("workDoneProgress");
    QVERIFY(workDone != nullptr);
    QCOMPARE(workDone->boolean(), true);

    auto textDocument = root.find("textDocument");
    QVERIFY(textDocument != nullptr);
    QVERIFY(textDocument->isObject());
    auto const &td = textDocument->object();

    auto codeAction = td.find("codeAction");
    QVERIFY(codeAction != nullptr);
    QVERIFY(codeAction->isObject());
    auto const &ca = codeAction->object();

    // Without this a server may only answer with Command literals, which carry
    // no edit - so every fix would come back unrunnable.
    auto literal = ca.find("codeActionLiteralSupport");
    QVERIFY(literal != nullptr);
    QVERIFY(literal->isObject());
    auto kinds = literal->object().find("codeActionKind");
    QVERIFY(kinds != nullptr);
    QVERIFY(kinds->isObject());
    auto valueSet = kinds->object().find("valueSet");
    QVERIFY(valueSet != nullptr);
    QVERIFY(valueSet->isArray());

    auto seen = QStringList{};
    for (auto const &entry : valueSet->array()) {
        seen << QString::fromStdString(entry.string());
    }
    QVERIFY(seen.contains(""));
    QVERIFY(seen.contains("quickfix"));
    QVERIFY(seen.contains("refactor"));

    // Licenses echoing a diagnostic's `data` back, which is how clangd matches
    // the diagnostic it handed us against the code action request.
    auto dataSupport = ca.find("dataSupport");
    QVERIFY(dataSupport != nullptr);
    QCOMPARE(dataSupport->boolean(), true);

    // We drop these in the parser today, so they must not be advertised.
    QVERIFY(!ca.contains("isPreferredSupport"));
    QVERIFY(!ca.contains("disabledSupport"));
}

QTEST_MAIN(LspCapabilitiesTest)
#include "test_lsp_capabilities.moc"
