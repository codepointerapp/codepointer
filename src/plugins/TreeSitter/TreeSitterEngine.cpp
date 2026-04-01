#include "TreeSitterEngine.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_c();

TreeSitterEngine::TreeSitterEngine() { parser = ts_parser_new(); }

TreeSitterEngine::~TreeSitterEngine() { ts_parser_delete(parser); }

const TSLanguage *TreeSitterEngine::getLanguageForFile(const QString &fileName) {
    QFileInfo info(fileName);
    QString ext = info.suffix().toLower();

    if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh" || ext == "cxx" ||
        ext == "hxx") {
        return tree_sitter_cpp();
    } else if (ext == "c" || ext == "h") {
        return tree_sitter_c();
    }
    return nullptr;
}

void TreeSitterEngine::updateFile(const QString &fileName, const QString &content) {
    const TSLanguage *lang = getLanguageForFile(fileName);
    if (!lang) {
        return;
    }

    QMutexLocker locker(&mutex);
    ts_parser_set_language(parser, lang);

    QByteArray bytes = content.toUtf8();
    TSTree *newTree = ts_parser_parse_string(parser, nullptr, bytes.data(), bytes.size());

    if (newTree) {
        auto context = std::make_shared<FileContext>();
        context->tree = newTree;
        context->language = lang;
        context->content = bytes;
        fileContexts[fileName] = context;
    }
}

TSNode TreeSitterEngine::getRootNode(const QString &fileName) {
    QMutexLocker locker(&mutex);
    if (fileContexts.contains(fileName)) {
        return ts_tree_root_node(fileContexts[fileName]->tree);
    }
    TSNode nullNode;
    nullNode.id = nullptr;
    return nullNode;
}

QList<TreeSitterEngine::Symbol> TreeSitterEngine::getSymbols(const QString &fileName) {
    QList<Symbol> symbols;
    std::shared_ptr<FileContext> context;
    {
        QMutexLocker locker(&mutex);
        if (!fileContexts.contains(fileName)) {
            return symbols;
        }
        context = fileContexts[fileName];
    }

    TSNode root = ts_tree_root_node(context->tree);

    // Simplified query for demo purposes
    const char *queryStr = "(class_specifier name: (type_identifier) @name) @symbol "
                           "(struct_specifier name: (type_identifier) @name) @symbol "
                           "(function_definition declarator: (function_declarator declarator: "
                           "(identifier) @name)) @symbol "
                           "(function_definition declarator: (identifier) @name) @symbol";

    uint32_t errorOffset;
    TSQueryError errorType;
    TSQuery *query = ts_query_new(context->language, queryStr, (uint32_t)strlen(queryStr),
                                  &errorOffset, &errorType);

    if (!query) {
        return symbols;
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        QString name;
        TSNode symbolNode;
        symbolNode.id = nullptr;

        for (uint16_t i = 0; i < match.capture_count; ++i) {
            TSNode node = match.captures[i].node;
            uint32_t capture_id = match.captures[i].index;
            const char *captureName;
            uint32_t captureNameLen;
            captureName = ts_query_capture_name_for_id(query, capture_id, &captureNameLen);
            QString qCaptureName = QString::fromUtf8(captureName, captureNameLen);

            if (qCaptureName == "name") {
                name = QString::fromUtf8(context->content.mid(
                    ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node)));
            } else if (qCaptureName == "symbol") {
                symbolNode = node;
            }
        }

        if (!name.isEmpty() && symbolNode.id != nullptr) {
            Symbol sym;
            sym.name = name;
            sym.type = QString::fromUtf8(ts_node_type(symbolNode));
            sym.line = ts_node_start_point(symbolNode).row;
            sym.column = ts_node_start_point(symbolNode).column;
            symbols.append(sym);
        }
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);

    int classes = 0, functions = 0;
    for (const auto &s : symbols) {
        if (s.type.contains("class") || s.type.contains("struct")) {
            classes++;
        } else {
            functions++;
        }
    }
    qDebug() << "TreeSitterEngine:" << fileName << "found" << classes << "classes/structs and"
             << functions << "functions";

    return symbols;
}
