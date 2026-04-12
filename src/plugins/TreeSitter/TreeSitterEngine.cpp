#include "TreeSitterEngine.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <algorithm>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_c();

TreeSitterEngine::TreeSitterEngine() : parser(nullptr) {}

TreeSitterEngine::~TreeSitterEngine() {}

const TSLanguage *TreeSitterEngine::getLanguageForFile(const QString &fileName) {
    auto info = QFileInfo(fileName);
    auto ext = info.suffix().toLower();

    if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh" || ext == "cxx" ||
        ext == "hxx" || ext == "h") {
        return tree_sitter_cpp();
    } else if (ext == "c") {
        return tree_sitter_c();
    }
    return nullptr;
}

void TreeSitterEngine::updateFile(const QString &fileName, const QString &content) {
    auto const lang = getLanguageForFile(fileName);
    if (!lang) {
        return;
    }

    auto bytes = content.toUtf8();
    auto localParser = ts_parser_new();
    ts_parser_set_language(localParser, lang);
    auto newTree = ts_parser_parse_string(localParser, nullptr, bytes.data(), bytes.size());
    ts_parser_delete(localParser);

    if (newTree) {
        auto locker = QMutexLocker(&mutex);
        auto context = std::make_shared<FileContext>();
        context->tree = newTree;
        context->language = lang;
        context->content = bytes;
        context->symbolsValid = false;
        fileContexts[fileName] = context;
    }
}

TSNode TreeSitterEngine::getRootNode(const QString &fileName) {
    auto locker = QMutexLocker(&mutex);
    if (fileContexts.contains(fileName)) {
        return ts_tree_root_node(fileContexts[fileName]->tree);
    }
    auto nullNode = TSNode{};
    nullNode.id = nullptr;
    return nullNode;
}

QList<TreeSitterEngine::Symbol> TreeSitterEngine::getSymbols(const QString &fileName) {
    auto context = std::shared_ptr<FileContext>{};
    {
        auto locker = QMutexLocker(&mutex);
        if (!fileContexts.contains(fileName)) {
            return {};
        }
        context = fileContexts[fileName];
        if (context->symbolsValid) {
            return context->cachedSymbols;
        }
    }

    auto root = ts_tree_root_node(context->tree);
    auto symbols = QList<Symbol>{};

    auto queryStr = QString{};
    if (context->language == tree_sitter_cpp()) {
        queryStr =
            "(class_specifier) @symbol (struct_specifier) @symbol (enum_specifier) @symbol "
            "(enumerator) @symbol (namespace_definition) @symbol (function_definition) @symbol "
            "(field_declaration) @symbol (declaration) @symbol (parameter_declaration) @symbol "
            "(alias_declaration) @symbol (type_definition) @symbol ";
    } else {
        queryStr =
            "(struct_specifier) @symbol (enum_specifier) @symbol (enumerator) @symbol "
            "(function_definition) @symbol (field_declaration) @symbol (declaration) @symbol "
            "(parameter_declaration) @symbol (type_definition) @symbol ";
    }

    auto errorOffset = uint32_t{};
    auto errorType = TSQueryError{};
    auto queryBytes = queryStr.toUtf8();
    auto query = ts_query_new(context->language, queryBytes.data(), (uint32_t)queryBytes.size(),
                              &errorOffset, &errorType);

    if (query) {
        auto cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, query, root);

        auto match = TSQueryMatch{};
        while (ts_query_cursor_next_match(cursor, &match)) {
            auto symbolNode = match.captures[0].node;
            auto const symbolType = ts_node_type(symbolNode);
            auto typeName = QString{};
            auto nameNodes = QList<TSNode>{};

            // Extract Type and Declarator Nodes
            if (strcmp(symbolType, "class_specifier") == 0 ||
                strcmp(symbolType, "struct_specifier") == 0 ||
                strcmp(symbolType, "enum_specifier") == 0 ||
                strcmp(symbolType, "namespace_definition") == 0) {
                auto n = ts_node_child_by_field_name(symbolNode, "name", 4);
                if (n.id) {
                    nameNodes.append(n);
                }
                typeName = symbolType;
            } else if (strcmp(symbolType, "enumerator") == 0) {
                auto n = ts_node_child_by_field_name(symbolNode, "name", 4);
                if (n.id) {
                    nameNodes.append(n);
                }
                auto p = ts_node_parent(ts_node_parent(symbolNode));
                if (p.id && strcmp(ts_node_type(p), "enum_specifier") == 0) {
                    typeName = extractNameFromNode(ts_node_child_by_field_name(p, "name", 4),
                                                   context->content);
                }
            } else if (strcmp(symbolType, "alias_declaration") == 0) {
                nameNodes.append(ts_node_child_by_field_name(symbolNode, "name", 4));
                typeName = extractNameFromNode(ts_node_child_by_field_name(symbolNode, "type", 4),
                                               context->content);
            } else {
                auto tNode = ts_node_child_by_field_name(symbolNode, "type", 4);
                typeName = extractNameFromNode(tNode, context->content);
                nameNodes.append(ts_node_child_by_field_name(symbolNode, "declarator", 10));
            }

            // Resolve Parent Scope
            auto isTopLevel = true;
            auto parentName = resolveParentScope(symbolNode, context->content, isTopLevel);

            for (auto node : nameNodes) {
                auto name = extractNameFromNode(node, context->content);
                if (name.isEmpty()) {
                    continue;
                }

                auto sym = Symbol{};
                sym.name = name;
                sym.fileName = fileName;
                sym.parentName = parentName;
                sym.line = ts_node_start_point(symbolNode).row;
                sym.column = ts_node_start_point(symbolNode).column;
                sym.type = resolveAutoType(node, typeName, context->content, symbolType);

                // Flag Definition
                if (strcmp(symbolType, "field_declaration") == 0) {
                    sym.isDefinition = true;
                } else if (isTopLevel) {
                    if (strcmp(symbolType, "declaration") == 0) {
                        sym.isDefinition = (strcmp(ts_node_type(node), "init_declarator") == 0);
                    } else {
                        sym.isDefinition = true; // Classes, functions, aliases
                    }
                }

                // Extract Signature for functions/methods
                if (strstr(symbolType, "function") || strstr(ts_node_type(node), "function") ||
                    !sym.type.contains(" ")) {
                    auto params = TSNode{};
                    params.id = nullptr;
                    auto stack = QList<TSNode>{symbolNode, node};
                    while (!stack.isEmpty()) {
                        auto n = stack.takeFirst();
                        if (strcmp(ts_node_type(n), "parameter_list") == 0) {
                            params = n;
                            break;
                        }
                        for (auto j = 0u; j < ts_node_child_count(n); ++j) {
                            stack.append(ts_node_child(n, j));
                        }
                    }
                    if (params.id) {
                        auto pStr = QString::fromUtf8(context->content.mid(
                            ts_node_start_byte(params),
                            ts_node_end_byte(params) - ts_node_start_byte(params)));
                        sym.signature = QString("%1 %2%3").arg(sym.type, sym.name, pStr);
                    }
                }
                symbols.append(sym);
            }
        }
        ts_query_cursor_delete(cursor);
        ts_query_delete(query);
    }

    {
        auto locker = QMutexLocker(&mutex);
        context->cachedSymbols = symbols;
        context->symbolsValid = true;
        updateIndexForFile(fileName, symbols);
    }
    return symbols;
}

QString TreeSitterEngine::extractNameFromNode(TSNode node, const QByteArray &content) {
    if (node.id == nullptr) {
        return "";
    }
    auto current = node;
    auto lastIdentifier = TSNode{};
    lastIdentifier.id = nullptr;

    while (current.id != nullptr) {
        auto const type = ts_node_type(current);
        if (strcmp(type, "identifier") == 0 || strcmp(type, "type_identifier") == 0 ||
            strcmp(type, "field_identifier") == 0 || strcmp(type, "destructor_name") == 0) {
            lastIdentifier = current;
        }

        auto next = ts_node_child_by_field_name(current, "declarator", 5);
        if (next.id) {
            current = next;
        } else if (strstr(type, "declarator")) {
            if (strcmp(type, "function_declarator") == 0) {
                auto first = ts_node_named_child(current, 0);
                if (first.id && strcmp(ts_node_type(first), "parameter_list") == 0) {
                    break;
                }
                current = first;
            } else if (ts_node_named_child_count(current) > 0) {
                current = ts_node_named_child(current, 0);
            } else {
                break;
            }
        } else {
            break;
        }
    }

    if (lastIdentifier.id) {
        return QString::fromUtf8(
            content.mid(ts_node_start_byte(lastIdentifier),
                        ts_node_end_byte(lastIdentifier) - ts_node_start_byte(lastIdentifier)));
    }
    return QString::fromUtf8(content.mid(ts_node_start_byte(node),
                                         ts_node_end_byte(node) - ts_node_start_byte(node)))
        .trimmed();
}

QString TreeSitterEngine::resolveAutoType(TSNode nameNode, const QString &baseType,
                                          const QByteArray &content, const char *symbolType) {
    if (!baseType.startsWith("auto")) {
        return baseType;
    }

    auto val = TSNode{};
    val.id = nullptr;
    if (strcmp(ts_node_type(nameNode), "init_declarator") == 0) {
        val = ts_node_child_by_field_name(nameNode, "value", 5);
    }

    if (val.id) {
        auto stack = QList<TSNode>{val};
        auto i = 0;
        while (!stack.isEmpty() && i++ < 50) {
            auto n = stack.takeFirst();
            if (!n.id) {
                continue;
            }
            auto const nt = ts_node_type(n);
            if (strcmp(nt, "call_expression") == 0) {
                auto func = ts_node_child_by_field_name(n, "function", 8);
                if (func.id) {
                    if (strcmp(ts_node_type(func), "template_function") == 0) {
                        auto args = ts_node_child_by_field_name(func, "arguments", 10);
                        if (args.id && ts_node_named_child_count(args) > 0) {
                            return extractNameFromNode(ts_node_named_child(args, 0), content);
                        }
                    }
                    auto fn = extractNameFromNode(func, content);
                    if (fn == "add" || fn == "push_back") {
                        auto args = ts_node_child_by_field_name(n, "arguments", 10);
                        if (args.id && ts_node_named_child_count(args) > 0) {
                            stack.append(ts_node_named_child(args, 0));
                            continue;
                        }
                    }
                    return fn + "()";
                }
            } else if (strcmp(nt, "new_expression") == 0) {
                return extractNameFromNode(ts_node_child_by_field_name(n, "type", 4), content);
            } else if (strcmp(nt, "parenthesized_expression") == 0 &&
                       ts_node_named_child_count(n) > 0) {
                stack.append(ts_node_named_child(n, 0));
            }
        }
    }
    return baseType;
}

QString TreeSitterEngine::resolveParentScope(TSNode symbolNode, const QByteArray &content,
                                             bool &isTopLevel) {
    auto parents = QStringList{};
    isTopLevel = true;
    auto current = ts_node_parent(symbolNode);
    while (current.id) {
        auto const t = ts_node_type(current);
        if (strcmp(t, "function_definition") == 0 || strcmp(t, "lambda_expression") == 0 ||
            strcmp(t, "compound_statement") == 0) {
            isTopLevel = false;
            parents.clear();
            break;
        }
        if (strcmp(t, "class_specifier") == 0 || strcmp(t, "struct_specifier") == 0 ||
            strcmp(t, "namespace_definition") == 0) {
            parents.prepend(
                extractNameFromNode(ts_node_child_by_field_name(current, "name", 4), content));
        }
        current = ts_node_parent(current);
    }
    return parents.join("::");
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

QList<TreeSitterEngine::Symbol>
TreeSitterEngine::findSymbolsGlobal(const QString &name, bool exactMatch, const QString &prev,
                                    const QString &sep, const QString &file, int line, int col) {
    auto locker = QMutexLocker(&mutex);
    if (!sep.isEmpty() && !prev.isEmpty()) {
        auto type = QString{};
        if (sep == "::") {
            type = prev;
        } else if (prev == "this" && fileContexts.contains(file)) {
            auto isTop = true;
            type = resolveParentScope(
                ts_node_descendant_for_point_range(ts_tree_root_node(fileContexts[file]->tree),
                                                   {(uint32_t)line, (uint32_t)col},
                                                   {(uint32_t)line, (uint32_t)col}),
                fileContexts[file]->content, isTop);
        } else if (fileContexts.contains(file)) {
            auto it = std::find_if(
                fileContexts[file]->cachedSymbols.begin(), fileContexts[file]->cachedSymbols.end(),
                [&](const Symbol &s) { return s.name == prev && s.line <= line; });
            if (it != fileContexts[file]->cachedSymbols.end()) {
                type = it->type;
            }

            if (type.isEmpty()) {
                auto isTop = true;
                auto cls = resolveParentScope(
                    ts_node_descendant_for_point_range(ts_tree_root_node(fileContexts[file]->tree),
                                                       {(uint32_t)line, (uint32_t)col},
                                                       {(uint32_t)line, (uint32_t)col}),
                    fileContexts[file]->content, isTop);
                auto itGlobal =
                    std::find_if(globalIndex.begin(), globalIndex.end(), [&](const Symbol &s) {
                        return s.parentName == cls && s.name == prev;
                    });
                if (itGlobal != globalIndex.end()) {
                    type = itGlobal->type;
                }
            }
        }

        if (!type.isEmpty()) {
            auto visited = QSet<QString>{};
            auto cur = type;
            while (!cur.isEmpty() && !visited.contains(cur)) {
                visited.insert(cur);
                cur = cur.remove('*').remove('&').trimmed();
                if (cur.endsWith("()")) {
                    auto fn = cur.left(cur.length() - 2);
                    auto c = fn.lastIndexOf("::");
                    auto sn = (c == -1) ? fn : fn.mid(c + 2);
                    for (const auto &s : globalIndex.values(sn)) {
                        if (c == -1 || s.parentName.endsWith(fn.left(c))) {
                            cur = s.type;
                            break;
                        }
                    }
                    continue;
                }
                auto ts = cur.indexOf('<');
                if (ts != -1) {
                    auto w = cur.left(ts).trimmed();
                    if (w.contains("shared_ptr") || w.contains("unique_ptr")) {
                        cur = cur.mid(ts + 1, cur.lastIndexOf('>') - ts - 1).trimmed();
                        continue;
                    }
                }
                auto st = cur;
                auto sStart = st.indexOf('<');
                if (sStart != -1) {
                    st = st.left(sStart).trimmed();
                }
                auto res = QList<Symbol>{};
                for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
                    if (it.value().parentName == st || it.value().parentName.endsWith("::" + st)) {
                        if (exactMatch ? it.key() == name
                                       : it.key().startsWith(name, Qt::CaseInsensitive)) {
                            res.append(it.value());
                        }
                    }
                }
                if (!res.isEmpty()) {
                    return res;
                }
                auto al = globalIndex.values(st);
                cur = "";
                for (const auto &s : al) {
                    if (s.parentName.isEmpty() && !s.type.isEmpty() && s.type != st) {
                        cur = s.type;
                        break;
                    }
                }
            }
        }
        return {};
    }
    if (exactMatch) {
        return globalIndex.values(name);
    }
    auto res = QList<Symbol>{};
    for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
        if (it.key().startsWith(name, Qt::CaseInsensitive)) {
            res.append(it.value());
        }
    }
    return res;
}

QList<QString> TreeSitterEngine::getTrackedFiles() const {
    auto locker = QMutexLocker(&mutex);
    return fileContexts.keys();
}
