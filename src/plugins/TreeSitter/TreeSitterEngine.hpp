#pragma once

#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QMutex>
#include <QString>
#include <atomic>
#include <memory>
#include <tree_sitter/api.h>

namespace TSNodeTypes {
constexpr const char *ClassSpecifier = "class_specifier";
constexpr const char *StructSpecifier = "struct_specifier";
constexpr const char *EnumSpecifier = "enum_specifier";
constexpr const char *Enumerator = "enumerator";
constexpr const char *NamespaceDefinition = "namespace_definition";
constexpr const char *FunctionDefinition = "function_definition";
constexpr const char *FunctionDeclarator = "function_declarator";
constexpr const char *FieldDeclaration = "field_declaration";
constexpr const char *Declaration = "declaration";
constexpr const char *ParameterDeclaration = "parameter_declaration";
constexpr const char *ParameterList = "parameter_list";
constexpr const char *AliasDeclaration = "alias_declaration";
constexpr const char *TypeDefinition = "type_definition";
constexpr const char *Identifier = "identifier";
constexpr const char *TypeIdentifier = "type_identifier";
constexpr const char *FieldIdentifier = "field_identifier";
constexpr const char *DestructorName = "destructor_name";
constexpr const char *InitDeclarator = "init_declarator";
constexpr const char *CallExpression = "call_expression";
constexpr const char *NewExpression = "new_expression";
constexpr const char *TemplateFunction = "template_function";
constexpr const char *FieldExpression = "field_expression";
constexpr const char *ParenthesizedExpression = "parenthesized_expression";
constexpr const char *CompoundStatement = "compound_statement";
constexpr const char *LambdaExpression = "lambda_expression";
constexpr const char *PointerDeclarator = "pointer_declarator";
constexpr const char *ReferenceDeclarator = "reference_declarator";
constexpr const char *ArrayDeclarator = "array_declarator";
constexpr const char *ParenthesizedDeclarator = "parenthesized_declarator";
constexpr const char *Declarator = "declarator";
constexpr const char *Auto = "auto";
} // namespace TSNodeTypes

namespace TSFieldNames {
constexpr const char *Name = "name";
constexpr const char *Type = "type";
constexpr const char *Declarator = "declarator";
constexpr const char *Value = "value";
constexpr const char *Function = "function";
constexpr const char *Arguments = "arguments";
constexpr const char *Field = "field";
} // namespace TSFieldNames

class TreeSitterEngine {
  public:
    TreeSitterEngine();
    ~TreeSitterEngine();

    // Parses or updates the AST for a given file
    void updateFile(const QString &fileName, const QByteArray &content);

    struct Symbol {
        QString name;
        QString type;
        QString signature; // For functions: full signature with parameters
        int line;
        int column;
        quint64 fileId = 0; // key into engine's fileNamePool
        QString parentName; // For members: name of the class/struct
        bool isDefinition = false;

        bool operator==(const Symbol &other) const {
            return name == other.name && fileId == other.fileId && line == other.line &&
                   column == other.column;
        }
    };

    // Returns cached symbols, or parses using provided content
    QList<Symbol> getSymbols(const QString &fileName, const QByteArray &content = {});

    // Fast global lookup; fileContent used for cursor-based type resolution
    QList<Symbol> findSymbolsGlobal(const QString &name, bool exactMatch,
                                    const QString &previousWord = QString(),
                                    const QString &separator = QString(),
                                    const QString &fileName = QString(), int line = -1,
                                    int column = -1, const QByteArray &fileContent = {});

    // Returns the language for a file based on extension
    static const TSLanguage *getLanguageForFile(const QString &fileName);
    static bool isHeaderFile(const QString &fileName);

    // Resolve a fileId back to the full path (call under mutex or from main thread)
    QString resolveFileId(quint64 id) const;

    void cancelParsing() { cancelFlag.store(1, std::memory_order_relaxed); }
    void resetCancel() { cancelFlag.store(0, std::memory_order_relaxed); }

  private:
    struct FileContext {
        TSTree *tree = nullptr;
        const TSLanguage *language = nullptr;
        bool symbolsValid = false;

        ~FileContext() {
            if (tree) {
                ts_tree_delete(tree);
            }
        }
    };

    QHash<QString, std::shared_ptr<FileContext>> fileContexts;

    // Global index: symbol name -> Symbol info
    QMultiHash<QString, Symbol> globalIndex;

    // Interned file paths: hash(path) -> path  (call internFileId only while holding mutex)
    QHash<quint64, QString> fileNamePool;
    quint64 internFileId(const QString &fileName);

    mutable QMutex mutex;
    std::atomic<size_t> cancelFlag{0};

    void updateIndexForFile(const QString &fileName, const QList<Symbol> &symbols);

    // Refactoring helpers

    static QString extractNameFromNode(TSNode node, const QByteArray &content);
    static QString resolveAutoType(TSNode nameNode, const QString &baseType,
                                   const QByteArray &content, std::string_view symbolType);
    static QString resolveParentScope(TSNode symbolNode, const QByteArray &content,
                                      bool &isTopLevel);

    static bool isFunctionOrBlock(std::string_view type);
    static bool isScopeContainer(std::string_view type);
    static bool isTypeAlias(std::string_view type);
    static bool isIdentifier(std::string_view type);
    static bool isDeclarator(std::string_view type);
};
