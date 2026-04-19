#include "TreeSitterEngine.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <algorithm>
#include <string_view>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_c();

TreeSitterEngine::TreeSitterEngine()  {}

TreeSitterEngine::~TreeSitterEngine() {}

quint64 TreeSitterEngine::internFileId(const QString &fileName) {
    auto id = static_cast<quint64>(qHash(fileName));
    fileNamePool.insert(id, fileName);
    return id;
}

QString TreeSitterEngine::resolveFileId(quint64 id) const {
    auto locker = QMutexLocker(&mutex);
    return fileNamePool.value(id);
}

bool TreeSitterEngine::isHeaderFile(const QString &fileName) {
    const auto ext = QFileInfo(fileName).suffix().toLower();
    return ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx";
}

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

void TreeSitterEngine::updateFile(const QString &fileName, const QByteArray &content) {
    auto const lang = getLanguageForFile(fileName);
    if (!lang) {
        return;
    }

    auto localParser = ts_parser_new();
    ts_parser_set_language(localParser, lang);
    ts_parser_set_cancellation_flag(localParser, reinterpret_cast<const size_t *>(&cancelFlag));
    auto newTree = ts_parser_parse_string(localParser, nullptr, content.data(), content.size());
    ts_parser_delete(localParser);

    if (!newTree) {
        if (cancelFlag.load(std::memory_order_relaxed)) {
            qDebug() << "TreeSitterEngine: cancelled while parsing" << QFileInfo(fileName).fileName();
        }
        return;
    }
    auto locker = QMutexLocker(&mutex);
    auto context = std::make_shared<FileContext>();
    context->tree = newTree;
    context->language = lang;
    context->symbolsValid = false;
    fileContexts[fileName] = context;
}


QList<TreeSitterEngine::Symbol> TreeSitterEngine::getSymbols(const QString &fileName,
                                                              const QByteArray &content) {
    auto context = std::shared_ptr<FileContext>{};
    auto fileId = quint64{};
    {
        auto locker = QMutexLocker(&mutex);
        if (!fileContexts.contains(fileName)) {
            return {};
        }
        context = fileContexts[fileName];
        fileId = internFileId(fileName);
        if (context->symbolsValid) {
            auto result = QList<Symbol>{};
            for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
                if (it.value().fileId == fileId) {
                    result.append(it.value());
                }
            }
            return result;
        }
    }
    if (content.isEmpty()) {
        return {};
    }

    auto root = ts_tree_root_node(context->tree);
    auto symbols = QList<Symbol>{};

    // Headers: full symbol set for completion/navigation.
    // Source files: function definitions only — skip parameters, locals, declarations.
    static const char *cppFullQuery =
        "(class_specifier) @symbol (struct_specifier) @symbol (enum_specifier) @symbol "
        "(enumerator) @symbol (namespace_definition) @symbol (function_definition) @symbol "
        "(field_declaration) @symbol (declaration) @symbol (parameter_declaration) @symbol "
        "(alias_declaration) @symbol (type_definition) @symbol ";
    static const char *cppLightQuery = "(function_definition) @symbol ";

    static const char *cFullQuery =
        "(struct_specifier) @symbol (enum_specifier) @symbol (enumerator) @symbol "
        "(function_definition) @symbol (field_declaration) @symbol (declaration) @symbol "
        "(parameter_declaration) @symbol (type_definition) @symbol ";
    static const char *cLightQuery = "(function_definition) @symbol ";

    const bool isHeader = isHeaderFile(fileName);
    const char *queryStr;
    if (context->language == tree_sitter_cpp()) {
        queryStr = isHeader ? cppFullQuery : cppLightQuery;
    } else {
        queryStr = isHeader ? cFullQuery : cLightQuery;
    }

    auto errorOffset = uint32_t{};
    auto errorType = TSQueryError{};
    auto query = ts_query_new(context->language, queryStr, (uint32_t)strlen(queryStr), &errorOffset,
                              &errorType);
    if (!query) {
        qCritical() << "TreeSitterEngine: Query error at" << errorOffset << "type" << errorType
                    << "Query:" << queryStr;
        return {};
    }

    auto cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    auto match = TSQueryMatch{};
    while (ts_query_cursor_next_match(cursor, &match)) {
        auto symbolNode = match.captures[0].node;
        auto const symbolType = std::string_view(ts_node_type(symbolNode));
        auto typeName = QString{};
        auto nameNodes = QList<TSNode>{};

        if (isScopeContainer(symbolType)) {
            auto n = ts_node_child_by_field_name(symbolNode, TSFieldNames::Name, 4);
            if (n.id) {
                nameNodes.append(n);
            }
            // Store the first base class name so the alias resolver can follow
            // inheritance chains (e.g. "struct BAR : FOO" → type = "FOO").
            // The tree is: base_class_clause → base_specifier → type_identifier,
            // so use a small BFS to find the first type_identifier at any depth.
            typeName = QString{};
            for (auto ci = 0u; ci < ts_node_child_count(symbolNode); ++ci) {
                auto child = ts_node_child(symbolNode, ci);
                auto ct = std::string_view(ts_node_type(child));
                if (ct == "base_class_clause" || ct == "base_clause") {
                    auto nodeQueue = QList<TSNode>{};
                    for (auto bi = 0u; bi < ts_node_named_child_count(child); ++bi) {
                        nodeQueue.append(ts_node_named_child(child, bi));
                    }
                    while (!nodeQueue.isEmpty() && typeName.isEmpty()) {
                        auto n = nodeQueue.takeFirst();
                        auto nt = std::string_view(ts_node_type(n));
                        if (nt == "type_identifier" || nt == "qualified_identifier") {
                            typeName = extractNameFromNode(n, content);
                        } else {
                            for (auto k = 0u; k < ts_node_named_child_count(n); ++k) {
                                nodeQueue.append(ts_node_named_child(n, k));
                            }
                        }
                    }
                    break;
                }
            }
        } else if (symbolType == TSNodeTypes::Enumerator) {
            auto n = ts_node_child_by_field_name(symbolNode, TSFieldNames::Name, 4);
            if (n.id) {
                nameNodes.append(n);
            }
            auto p = ts_node_parent(ts_node_parent(symbolNode));
            if (p.id && std::string_view(ts_node_type(p)) == TSNodeTypes::EnumSpecifier) {
                typeName = extractNameFromNode(
                    ts_node_child_by_field_name(p, TSFieldNames::Name, 4), content);
            }
        } else if (isTypeAlias(symbolType)) {
            auto nameField = (symbolType == TSNodeTypes::AliasDeclaration)
                                 ? TSFieldNames::Name
                                 : TSFieldNames::Declarator;
            nameNodes.append(ts_node_child_by_field_name(symbolNode, nameField, 10));
            typeName = extractNameFromNode(
                ts_node_child_by_field_name(symbolNode, TSFieldNames::Type, 4), content);
        } else {
            auto tNode = ts_node_child_by_field_name(symbolNode, TSFieldNames::Type, 4);
            typeName = extractNameFromNode(tNode, content);
            nameNodes.append(ts_node_child_by_field_name(symbolNode, TSFieldNames::Declarator, 10));
        }

        auto isTopLevel = true;
        auto parentName = resolveParentScope(symbolNode, content, isTopLevel);

        for (auto &node : nameNodes) {
            auto name = extractNameFromNode(node, content);
            if (name.isEmpty()) {
                continue;
            }

            auto sym = Symbol{};
            sym.name = name;
            sym.fileId = fileId;
            sym.parentName = parentName;
            sym.line = ts_node_start_point(symbolNode).row;
            sym.column = ts_node_start_point(symbolNode).column;
            sym.type = resolveAutoType(node, typeName, content, symbolType);

            if (symbolType == TSNodeTypes::FieldDeclaration) {
                sym.isDefinition = true;
            } else if (isTopLevel) {
                if (symbolType == TSNodeTypes::Declaration) {
                    sym.isDefinition =
                        (std::string_view(ts_node_type(node)) == TSNodeTypes::InitDeclarator);
                } else {
                    sym.isDefinition = true;
                }
            }

            if (symbolType.find("function") != std::string_view::npos ||
                std::string_view(ts_node_type(node)).find("function") != std::string_view::npos ||
                !sym.type.contains(" ")) {
                auto params = TSNode{};
                params.id = nullptr;
                auto stack = QList<TSNode>{symbolNode, node};
                while (!stack.isEmpty()) {
                    auto n = stack.takeFirst();
                    if (std::string_view(ts_node_type(n)) == TSNodeTypes::ParameterList) {
                        params = n;
                        break;
                    }
                    for (auto j = 0u; j < ts_node_child_count(n); ++j) {
                        stack.append(ts_node_child(n, j));
                    }
                }
                if (params.id) {
                    auto pStr = QString::fromUtf8(content.mid(
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

    auto locker = QMutexLocker(&mutex);
    context->symbolsValid = true;
    if (context->tree) {
        ts_tree_delete(context->tree);
        context->tree = nullptr;
    }
    updateIndexForFile(fileName, symbols);
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
        auto const type = std::string_view(ts_node_type(current));
        if (isIdentifier(type)) {
            lastIdentifier = current;
        }

        auto next = ts_node_child_by_field_name(current, TSFieldNames::Declarator, 5);
        if (next.id) {
            current = next;
        } else if (isDeclarator(type)) {
            if (type == TSNodeTypes::FunctionDeclarator) {
                auto first = ts_node_named_child(current, 0);
                if (first.id &&
                    std::string_view(ts_node_type(first)) == TSNodeTypes::ParameterList) {
                    break;
                }
                current = first;
            } else if (ts_node_named_child_count(current) > 0) {
                current = ts_node_named_child(current, 0);
            } else {
                break;
            }
        } else {
            // struct/class/enum specifiers have a "name" field, not a "declarator".
            // Follow it so we return "FOO" instead of the raw "struct FOO" text.
            if (isScopeContainer(type)) {
                auto nameChild = ts_node_child_by_field_name(current, TSFieldNames::Name, 4);
                if (nameChild.id) {
                    lastIdentifier = nameChild;
                }
            }
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
                                          const QByteArray &content, std::string_view symbolType) {
    Q_UNUSED(symbolType);
    if (!baseType.startsWith(TSNodeTypes::Auto)) {
        return baseType;
    }

    auto val = TSNode{};
    val.id = nullptr;
    if (std::string_view(ts_node_type(nameNode)) == TSNodeTypes::InitDeclarator) {
        val = ts_node_child_by_field_name(nameNode, TSFieldNames::Value, 5);
    }

    if (val.id) {
        auto stack = QList<TSNode>{val};
        auto i = 0;
        while (!stack.isEmpty() && i++ < 50) {
            auto n = stack.takeFirst();
            if (!n.id) {
                continue;
            }
            auto const nt = std::string_view(ts_node_type(n));
            if (nt == TSNodeTypes::CallExpression) {
                auto func = ts_node_child_by_field_name(n, TSFieldNames::Function, 8);
                if (func.id) {
                    if (std::string_view(ts_node_type(func)) == TSNodeTypes::TemplateFunction) {
                        auto args = ts_node_child_by_field_name(func, TSFieldNames::Arguments, 10);
                        if (args.id && ts_node_named_child_count(args) > 0) {
                            return extractNameFromNode(ts_node_named_child(args, 0), content);
                        }
                    }
                    auto fn = extractNameFromNode(func, content);
                    return fn + "()";
                }
            } else if (nt == TSNodeTypes::NewExpression) {
                return extractNameFromNode(ts_node_child_by_field_name(n, TSFieldNames::Type, 4),
                                           content);
            } else if (nt == TSNodeTypes::ParenthesizedExpression) {
                if (ts_node_named_child_count(n) > 0) {
                    stack.append(ts_node_named_child(n, 0));
                }
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
        auto const t = std::string_view(ts_node_type(current));
        if (isFunctionOrBlock(t)) {
            isTopLevel = false;
            break;
        }
        if (isScopeContainer(t)) {
            parents.prepend(extractNameFromNode(
                ts_node_child_by_field_name(current, TSFieldNames::Name, 4), content));
        }
        current = ts_node_parent(current);
    }
    return parents.join("::");
}

void TreeSitterEngine::updateIndexForFile(const QString &fileName, const QList<Symbol> &symbols) {
    auto fileId = internFileId(fileName);
    auto it = globalIndex.begin();
    while (it != globalIndex.end()) {
        if (it.value().fileId == fileId) {
            it = globalIndex.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &sym : std::as_const(symbols)) {
        globalIndex.insert(sym.name, sym);
    }
}

QList<TreeSitterEngine::Symbol>
TreeSitterEngine::findSymbolsGlobal(const QString &name, bool exactMatch, const QString &prev,
                                    const QString &sep, const QString &file, int line, int col,
                                    const QByteArray &fileContent) {
    auto locker = QMutexLocker(&mutex);
    auto results = QList<Symbol>{};
    auto otherProjectResults = QList<Symbol>{};

    // Identify current project branch to prioritize local symbols
    QString currentFileDir = QFileInfo(file).absolutePath();
    // Go up one level from 'src' or 'include' if possible to get the project root
    QString projectRoot = currentFileDir;
    if (projectRoot.endsWith("/src") || projectRoot.endsWith("/include")) {
        projectRoot = projectRoot.left(projectRoot.lastIndexOf('/'));
    }

    auto prioritize = [&](const Symbol &sym) {
        if (!file.isEmpty() && fileNamePool.value(sym.fileId).startsWith(projectRoot)) {
            results.append(sym);
        } else {
            otherProjectResults.append(sym);
        }
    };

    // Re-parse fileContent on demand for cursor-based queries (tree is dropped after indexing).
    auto parseTempTree = [&]() -> TSTree * {
        if (fileContent.isEmpty() || !fileContexts.contains(file)) {
            return nullptr;
        }
        auto lang = fileContexts[file]->language;
        auto localParser = ts_parser_new();
        ts_parser_set_language(localParser, lang);
        auto *t = ts_parser_parse_string(localParser, nullptr, fileContent.data(), fileContent.size());
        ts_parser_delete(localParser);
        return t;
    };

    if (!sep.isEmpty() && !prev.isEmpty()) {
        auto type = QString{};
        qDebug() << "TreeSitterEngine: findSymbolsGlobal for" << prev << sep << "name=" << name;
        if (sep == "::") {
            type = prev;
        } else if (prev == "this" && fileContexts.contains(file)) {
            if (auto *tempTree = parseTempTree()) {
                auto isTop = true;
                type = resolveParentScope(
                    ts_node_descendant_for_point_range(ts_tree_root_node(tempTree),
                                                       {(uint32_t)line, (uint32_t)col},
                                                       {(uint32_t)line, (uint32_t)col}),
                    fileContent, isTop);
                ts_tree_delete(tempTree);
            }
            qDebug() << "TreeSitterEngine: resolved 'this' to type" << type;
        } else if (fileContexts.contains(file)) {
            auto fileId = internFileId(file);
            auto candidates = globalIndex.values(prev);
            auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Symbol &s) {
                return s.fileId == fileId && s.line <= line;
            });
            if (it != candidates.end()) {
                type = it->type;
                qDebug() << "TreeSitterEngine: found local declaration of" << prev << "with type"
                         << type;
            }
            if (type.isEmpty()) {
                if (auto *tempTree = parseTempTree()) {
                    auto isTop = true;
                    auto cls = resolveParentScope(
                        ts_node_descendant_for_point_range(ts_tree_root_node(tempTree),
                                                           {(uint32_t)line, (uint32_t)col},
                                                           {(uint32_t)line, (uint32_t)col}),
                        fileContent, isTop);
                    ts_tree_delete(tempTree);
                    auto itGlobal =
                        std::find_if(globalIndex.begin(), globalIndex.end(), [&](const Symbol &s) {
                            return s.parentName == cls && s.name == prev;
                        });
                    if (itGlobal != globalIndex.end()) {
                        type = itGlobal->type;
                        qDebug() << "TreeSitterEngine: found class member" << prev << "with type"
                                 << type;
                    }
                }
            }
        }

        if (!type.isEmpty()) {
            auto visited = QSet<QString>{};
            auto cur = type;
            while (!cur.isEmpty() && !visited.contains(cur)) {
                visited.insert(cur);
                cur = cur.remove('*').remove('&').trimmed();
                qDebug() << "TreeSitterEngine: resolving effective type..." << cur;
                if (cur.endsWith("()")) {
                    auto fn = cur.left(cur.length() - 2);
                    auto c = fn.lastIndexOf("::");
                    auto sn = (c == -1) ? fn : fn.mid(c + 2);
                    auto values = globalIndex.values(sn);
                    for (const auto &s : std::as_const(values)) {
                        if (c == -1 || s.parentName.endsWith(fn.left(c))) {
                            cur = s.type;
                            qDebug() << "TreeSitterEngine: resolved function" << fn
                                     << "to return type" << cur;
                            break;
                        }
                    }
                    continue;
                }

                auto st = cur;
                auto sStart = st.indexOf('<');
                if (sStart != -1) {
                    st = st.left(sStart).trimmed();
                }

                for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
                    auto const &val = it.value();
                    if (val.parentName == st || val.parentName.endsWith("::" + st) ||
                        (st.contains("::") && val.parentName == st.mid(st.lastIndexOf("::") + 2))) {

                        bool match = exactMatch ? it.key() == name
                                                : it.key().startsWith(name, Qt::CaseInsensitive);
                        if (match) {
                            prioritize(val);
                        }
                    }
                }

                // Don't return early — continue the loop to collect inherited members.

                auto al = globalIndex.values(st);
                cur = "";
                for (const auto &s : std::as_const(al)) {
                    // Skip struct/class/enum definitions — their type is the node-type
                    // name ("struct_specifier" etc.), not a real type alias target.
                    if (isScopeContainer(s.type.toStdString())) {
                        continue;
                    }
                    if (s.parentName.isEmpty() && !s.type.isEmpty() && s.type != st) {
                        cur = s.type;
                        qDebug() << "TreeSitterEngine: resolving alias" << st << "->" << cur;
                        break;
                    }
                }
            }
        }
        if (!results.isEmpty() || !otherProjectResults.isEmpty()) {
            auto const &finalRes = results.isEmpty() ? otherProjectResults : results;
            qDebug() << "TreeSitterEngine: found" << finalRes.size() << "members for" << prev;
            return finalRes;
        }
        qDebug() << "TreeSitterEngine: no semantic results found for" << prev;
        return {};
    }

    // Top-level symbol lookup (used by Follow Symbol)
    for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
        bool match = exactMatch ? it.key() == name : it.key().startsWith(name, Qt::CaseInsensitive);
        if (match) {
            prioritize(it.value());
        }
    }

    return results.isEmpty() ? otherProjectResults : results;
}


bool TreeSitterEngine::isFunctionOrBlock(std::string_view type) {
    return type == TSNodeTypes::FunctionDefinition || type == TSNodeTypes::LambdaExpression ||
           type == TSNodeTypes::CompoundStatement;
}

bool TreeSitterEngine::isScopeContainer(std::string_view type) {
    return type == TSNodeTypes::ClassSpecifier || type == TSNodeTypes::StructSpecifier ||
           type == TSNodeTypes::EnumSpecifier || type == TSNodeTypes::NamespaceDefinition;
}

bool TreeSitterEngine::isTypeAlias(std::string_view type) {
    return type == TSNodeTypes::AliasDeclaration || type == TSNodeTypes::TypeDefinition;
}

bool TreeSitterEngine::isIdentifier(std::string_view type) {
    return type == TSNodeTypes::Identifier || type == TSNodeTypes::TypeIdentifier ||
           type == TSNodeTypes::FieldIdentifier || type == TSNodeTypes::DestructorName;
}

bool TreeSitterEngine::isDeclarator(std::string_view type) {
    return type.find(TSNodeTypes::Declarator) != std::string_view::npos;
}
