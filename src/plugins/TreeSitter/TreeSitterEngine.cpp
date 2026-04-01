#include "TreeSitterEngine.hpp"
#include <QFileInfo>
#include <QDebug>
#include <QMutexLocker>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_c();

TreeSitterEngine::TreeSitterEngine() : parser(nullptr) {
}

TreeSitterEngine::~TreeSitterEngine() {
}

const TSLanguage* TreeSitterEngine::getLanguageForFile(const QString &fileName) {
    QFileInfo info(fileName);
    QString ext = info.suffix().toLower();
    
    if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh" || ext == "cxx" || ext == "hxx") {
        return tree_sitter_cpp();
    } else if (ext == "c" || ext == "h") {
        return tree_sitter_c();
    }
    return nullptr;
}

void TreeSitterEngine::updateFile(const QString &fileName, const QString &content) {
    const TSLanguage *lang = getLanguageForFile(fileName);
    if (!lang) return;

    QByteArray bytes = content.toUtf8();
    TSParser *localParser = ts_parser_new();
    ts_parser_set_language(localParser, lang);
    TSTree *newTree = ts_parser_parse_string(localParser, nullptr, bytes.data(), bytes.size());
    ts_parser_delete(localParser);

    if (newTree) {
        QMutexLocker locker(&mutex);
        auto context = std::make_shared<FileContext>();
        context->tree = newTree;
        context->language = lang;
        context->content = bytes;
        context->symbolsValid = false;
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
    std::shared_ptr<FileContext> context;
    {
        QMutexLocker locker(&mutex);
        if (!fileContexts.contains(fileName)) return {};
        context = fileContexts[fileName];
        if (context->symbolsValid) return context->cachedSymbols;
    }

    TSNode root = ts_tree_root_node(context->tree);
    QList<Symbol> symbols;

    // Robust and permissive query
    const char *queryStr = 
        "(class_specifier name: (_) @name) @symbol "
        "(struct_specifier name: (_) @name) @symbol "
        "(enum_specifier name: (_) @name) @symbol "
        "(namespace_definition name: (_) @name) @symbol "
        "(function_definition declarator: (_) @name) @symbol "
        "(field_declaration declarator: (_) @name) @symbol "
        "(declaration declarator: (_) @name) @symbol ";

    uint32_t errorOffset;
    TSQueryError errorType;
    TSQuery *query = ts_query_new(context->language, queryStr, (uint32_t)strlen(queryStr), &errorOffset, &errorType);

    if (query) {
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
                    // For declarators, we might need to drill down to find the actual identifier
                    TSNode nameNode = node;
                    while (ts_node_child_count(nameNode) > 0) {
                        bool found = false;
                        for (uint32_t j = 0; j < ts_node_child_count(nameNode); ++j) {
                            TSNode child = ts_node_child(nameNode, j);
                            const char* type = ts_node_type(child);
                            if (strcmp(type, "identifier") == 0 || strcmp(type, "type_identifier") == 0 || strcmp(type, "field_identifier") == 0) {
                                nameNode = child;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // If no specific child found, just take the first named one or break
                            if (ts_node_named_child_count(nameNode) > 0) {
                                nameNode = ts_node_named_child(nameNode, 0);
                            } else {
                                break;
                            }
                        } else {
                            break; // found our identifier
                        }
                    }

                    uint32_t start = ts_node_start_byte(nameNode);
                    uint32_t end = ts_node_end_byte(nameNode);
                    if (end > start && end <= (uint32_t)context->content.size()) {
                        name = QString::fromUtf8(context->content.mid(start, end - start));
                    }
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
                sym.fileName = fileName;
                symbols.append(sym);
            }
        }
        ts_query_cursor_delete(cursor);
        ts_query_delete(query);
    } else {
        qDebug() << "TreeSitterEngine: Query error at" << errorOffset << "type" << errorType;
    }

    {
        QMutexLocker locker(&mutex);
        context->cachedSymbols = symbols;
        context->symbolsValid = true;
        updateIndexForFile(fileName, symbols);
    }

    return symbols;
}

void TreeSitterEngine::updateIndexForFile(const QString &fileName, const QList<Symbol> &symbols) {
    auto it = globalIndex.begin();
    while (it != globalIndex.end()) {
        if (it.value().fileName == fileName) {
            it = globalIndex.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto &sym : symbols) {
        globalIndex.insert(sym.name, sym);
    }
}

QList<TreeSitterEngine::Symbol> TreeSitterEngine::findSymbolsGlobal(const QString &name, bool exactMatch) {
    QMutexLocker locker(&mutex);
    if (exactMatch) {
        return globalIndex.values(name);
    } else {
        QList<Symbol> results;
        for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
            if (it.key().contains(name, Qt::CaseInsensitive)) {
                results.append(it.value());
            }
        }
        return results;
    }
}

QList<QString> TreeSitterEngine::getTrackedFiles() const {
    QMutexLocker locker(&mutex);
    return fileContexts.keys();
}
