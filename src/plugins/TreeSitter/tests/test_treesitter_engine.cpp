#include "TreeSitterEngine.hpp"
#include <QTest>

// Collect member names returned by findSymbolsGlobal for "prev sep name".
static QStringList memberNames(TreeSitterEngine &engine, const QString &prev, const QString &sep,
                               const QString &file, int line, int col, const QByteArray &content) {
    auto results = engine.findSymbolsGlobal("", false, prev, sep, file, line, col, content);
    auto names = QStringList{};
    for (const auto &s : results) {
        names << s.name;
    }
    return names;
}

class TestTreeSitterEngine : public QObject {
    Q_OBJECT

  private:
    static void indexHeader(TreeSitterEngine &engine, const QString &name,
                            const QByteArray &content) {
        engine.updateFile(name, content);
        engine.getSymbols(name, content);
    }

  private slots:
    // -----------------------------------------------------------------------
    // Local variable via type-alias of a smart pointer (struct in header)
    // -----------------------------------------------------------------------

    // Icon i;  i->  should resolve Icon -> std::shared_ptr<ImageData> -> ImageData members.
    void localVarViaTypeAlias_arrow() {
        const auto header = QByteArrayLiteral("struct ImageData {\n"
                                              "    int x;\n"
                                              "    char data[256];\n"
                                              "};\n"
                                              "using Icon = std::shared_ptr<ImageData>;\n");

        // source file: local variable inside a function — NOT indexed by cppLightQuery
        const auto source = QByteArrayLiteral("void foo() {\n" // line 0
                                              "    Icon i;\n"  // line 1
                                              "    i->x;\n"    // line 2  <- cursor
                                              "}\n");

        TreeSitterEngine engine;
        indexHeader(engine, "test.hpp", header);
        engine.updateFile("test.cpp", source);

        auto names = memberNames(engine, "i", "->", "test.cpp", 2, 7, source);
        QVERIFY2(names.contains("x"), "Should find member 'x' via Icon -> shared_ptr<ImageData>");
        QVERIFY2(names.contains("data"),
                 "Should find member 'data' via Icon -> shared_ptr<ImageData>");
    }

    // Same but via dot accessor — dot on a shared_ptr alias should also yield inner members.
    void localVarViaTypeAlias_dot() {
        const auto header = QByteArrayLiteral("struct ImageData {\n"
                                              "    int x;\n"
                                              "    char data[256];\n"
                                              "};\n"
                                              "using Icon = std::shared_ptr<ImageData>;\n");

        const auto source = QByteArrayLiteral("void foo() {\n"
                                              "    Icon i;\n"
                                              "    i.get();\n" // line 2 <- cursor
                                              "}\n");

        TreeSitterEngine engine;
        indexHeader(engine, "test.hpp", header);
        engine.updateFile("test.cpp", source);

        auto names = memberNames(engine, "i", ".", "test.cpp", 2, 6, source);
        QVERIFY2(names.contains("x") || names.contains("data"),
                 "Dot on Icon variable should yield ImageData members");
    }

    // -----------------------------------------------------------------------
    // Local variable shadows class member (bug: class member was returned first)
    // -----------------------------------------------------------------------
    void localVarShadowsClassMember() {
        const auto header = QByteArrayLiteral(
            "struct ImageData {\n"
            "    int x;\n"
            "};\n"
            "using Icon = std::shared_ptr<ImageData>;\n"
            "struct SomeClass {\n"
            "    std::string i;\n" // member named 'i', type std::string — must be shadowed
            "    void foo();\n"
            "};\n");

        const auto source = QByteArrayLiteral("void SomeClass::foo() {\n" // line 0
                                              "    Icon i;\n" // line 1  local shadows class member
                                              "    i->x;\n"   // line 2  <- cursor
                                              "}\n");

        TreeSitterEngine engine;
        indexHeader(engine, "shadow.hpp", header);
        engine.updateFile("shadow.cpp", source);

        auto names = memberNames(engine, "i", "->", "shadow.cpp", 2, 7, source);

        QVERIFY2(names.contains("x"),
                 "Local 'Icon i' should shadow class member 'std::string i' and find x");
        QVERIFY2(!names.contains("size") && !names.contains("length") && !names.contains("empty"),
                 "std::string members must not appear — class member must be shadowed");
    }

    // -----------------------------------------------------------------------
    // Direct struct variable in the SAME source file (struct not in any header)
    // struct members are never indexed from .cpp files by the light query, so
    // the engine must fall back to scanning the temp tree.
    // -----------------------------------------------------------------------
    void localStructInSameSourceFile_dot() {
        // Everything in one .cpp file — struct members are NOT in the global index.
        const auto source =
            QByteArrayLiteral("struct ImageData {\n"  // line 0
                              "    int x;\n"          // line 1
                              "    char data[256];\n" // line 2
                              "};\n"                  // line 3
                              "void foo() {\n"        // line 4
                              "    ImageData ii;\n"   // line 5  local var, not indexed
                              "    ii.x;\n"           // line 6  <- cursor
                              "}\n");

        TreeSitterEngine engine;
        engine.updateFile("local.cpp", source);
        // Do NOT call getSymbols — simulates a .cpp file where only
        // function_definition nodes are indexed (light query).

        auto names = memberNames(engine, "ii", ".", "local.cpp", 6, 7, source);
        QVERIFY2(names.contains("x"),
                 "Should find 'x' via temp-tree struct lookup when struct is in same .cpp file");
        QVERIFY2(names.contains("data"),
                 "Should find 'data' via temp-tree struct lookup when struct is in same .cpp file");
    }

    // Arrow accessor variant of the same scenario.
    void localStructInSameSourceFile_arrow() {
        const auto source = QByteArrayLiteral("struct ImageData {\n"
                                              "    int x;\n"
                                              "    char data[256];\n"
                                              "};\n"
                                              "void foo() {\n"
                                              "    ImageData *ii;\n" // line 5
                                              "    ii->x;\n"         // line 6  <- cursor
                                              "}\n");

        TreeSitterEngine engine;
        engine.updateFile("local.cpp", source);

        auto names = memberNames(engine, "ii", "->", "local.cpp", 6, 8, source);
        QVERIFY2(names.contains("x"), "Pointer to local struct should find 'x' via temp tree");
        QVERIFY2(names.contains("data"),
                 "Pointer to local struct should find 'data' via temp tree");
    }

    // Exact reproduction of the reported failure: struct, alias, and variable all
    // inside the same function body, with an extra variable declaration in between.
    void localStructAndAliasInsideFunction() {
        const auto source =
            QByteArrayLiteral("void testFn() {\n"                              // line 0
                              "    struct ImageData {\n"                       // line 1
                              "        int x;\n"                               // line 2
                              "        char data[256];\n"                      // line 3
                              "    };\n"                                       // line 4
                              "    ImageData ii;\n"                            // line 5
                              "    using Icon = std::shared_ptr<ImageData>;\n" // line 6
                              "    Icon i;\n"                                  // line 7
                              "    i->x;\n"                                    // line 8  <- cursor
                              "}\n");

        TreeSitterEngine engine;
        engine.updateFile("funclocal.cpp", source);

        auto names = memberNames(engine, "i", "->", "funclocal.cpp", 8, 7, source);
        QVERIFY2(names.contains("x"),
                 "Should find 'x' of function-local struct via function-local alias");
        QVERIFY2(names.contains("data"),
                 "Should find 'data' of function-local struct via function-local alias");
    }

    // shared_ptr<T> treated as a transparent pointer: both . and -> yield T's members.
    void sharedPtrTreatedAsPointer_sameFile() {
        const auto source = QByteArrayLiteral("struct ImageData {\n"
                                              "    int x;\n"
                                              "    char data[256];\n"
                                              "};\n"
                                              "using Icon = std::shared_ptr<ImageData>;\n" // line 4
                                              "void foo() {\n"                             // line 5
                                              "    Icon i;\n"                              // line 6
                                              "    i->x;\n" // line 7  <- cursor
                                              "}\n");

        TreeSitterEngine engine;
        engine.updateFile("sameFile.cpp", source);

        auto names = memberNames(engine, "i", "->", "sameFile.cpp", 7, 7, source);
        QVERIFY2(names.contains("x"),
                 "shared_ptr<T> alias and T both in same .cpp should still find members");
        QVERIFY2(names.contains("data"),
                 "shared_ptr<T> alias and T both in same .cpp should still find data");
    }
};

QTEST_MAIN(TestTreeSitterEngine)
#include "test_treesitter_engine.moc"
