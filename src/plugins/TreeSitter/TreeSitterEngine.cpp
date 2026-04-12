#include "TreeSitterEngine.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <algorithm>
#include <string_view>

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

    static const char *cppQuery =
        "(class_specifier) @symbol (struct_specifier) @symbol (enum_specifier) @symbol "
        "(enumerator) @symbol (namespace_definition) @symbol (function_definition) @symbol "
        "(field_declaration) @symbol (declaration) @symbol (parameter_declaration) @symbol "
        "(alias_declaration) @symbol (type_definition) @symbol ";

    static const char *cQuery =
        "(struct_specifier) @symbol (enum_specifier) @symbol (enumerator) @symbol "
        "(function_definition) @symbol (field_declaration) @symbol (declaration) @symbol "
        "(parameter_declaration) @symbol (type_definition) @symbol ";

    const char *queryStr = (context->language == tree_sitter_cpp()) ? cppQuery : cQuery;

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
            typeName = QString::fromUtf8(symbolType.data(), symbolType.size());
        } else if (symbolType == TSNodeTypes::Enumerator) {
            auto n = ts_node_child_by_field_name(symbolNode, TSFieldNames::Name, 4);
            if (n.id) {
                nameNodes.append(n);
            }
            auto p = ts_node_parent(ts_node_parent(symbolNode));
            if (p.id && std::string_view(ts_node_type(p)) == TSNodeTypes::EnumSpecifier) {
                typeName = extractNameFromNode(
                    ts_node_child_by_field_name(p, TSFieldNames::Name, 4), context->content);
            }
        } else if (isTypeAlias(symbolType)) {
            auto nameField = (symbolType == TSNodeTypes::AliasDeclaration)
                                 ? TSFieldNames::Name
                                 : TSFieldNames::Declarator;
            nameNodes.append(ts_node_child_by_field_name(symbolNode, nameField, 10));
            typeName = extractNameFromNode(
                ts_node_child_by_field_name(symbolNode, TSFieldNames::Type, 4), context->content);
        } else {
            auto tNode = ts_node_child_by_field_name(symbolNode, TSFieldNames::Type, 4);
            typeName = extractNameFromNode(tNode, context->content);
            nameNodes.append(ts_node_child_by_field_name(symbolNode, TSFieldNames::Declarator, 10));
        }

        auto isTopLevel = true;
        auto parentName = resolveParentScope(symbolNode, context->content, isTopLevel);

        for (auto &node : nameNodes) {
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

    auto locker = QMutexLocker(&mutex);
    context->cachedSymbols = symbols;
    context->symbolsValid = true;
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
            parents.clear();
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
    auto it = globalIndex.begin();
    while (it != globalIndex.end()) {
        if (it.value().fileName == fileName) {
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
                                    const QString &sep, const QString &file, int line, int col) {
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
        if (!file.isEmpty() && sym.fileName.startsWith(projectRoot)) {
            results.append(sym);
        } else {
            otherProjectResults.append(sym);
        }
    };

    if (!sep.isEmpty() && !prev.isEmpty()) {
        auto type = QString{};
        qDebug() << "TreeSitterEngine: findSymbolsGlobal for" << prev << sep << "name=" << name;
        if (sep == "::") {
            type = prev;
        } else if (prev == "this" && fileContexts.contains(file)) {
            auto isTop = true;
            type = resolveParentScope(
                ts_node_descendant_for_point_range(ts_tree_root_node(fileContexts[file]->tree),
                                                   {(uint32_t)line, (uint32_t)col},
                                                   {(uint32_t)line, (uint32_t)col}),
                fileContexts[file]->content, isTop);
            qDebug() << "TreeSitterEngine: resolved 'this' to type" << type;
        } else if (fileContexts.contains(file)) {
            auto const &cached = fileContexts[file]->cachedSymbols;
            auto it = std::find_if(cached.begin(), cached.end(), [&](const Symbol &s) {
                return s.name == prev && s.line <= line;
            });
            if (it != cached.end()) {
                type = it->type;
                qDebug() << "TreeSitterEngine: found local declaration of" << prev << "with type"
                         << type;
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
                    qDebug() << "TreeSitterEngine: found class member" << prev << "with type"
                             << type;
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

                if (!results.isEmpty() || !otherProjectResults.isEmpty()) {
                    auto const &finalRes = results.isEmpty() ? otherProjectResults : results;
                    qDebug() << "TreeSitterEngine: found" << finalRes.size() << "members for" << st
                             << "in" << (results.isEmpty() ? "other" : "current") << "project";
                    return finalRes;
                }

                auto al = globalIndex.values(st);
                cur = "";
                for (const auto &s : std::as_const(al)) {
                    if (s.parentName.isEmpty() && !s.type.isEmpty() && s.type != st) {
                        cur = s.type;
                        qDebug() << "TreeSitterEngine: resolving alias" << st << "->" << cur;
                        break;
                    }
                }
            }
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

QList<QString> TreeSitterEngine::getTrackedFiles() const {
    auto locker = QMutexLocker(&mutex);
    return fileContexts.keys();
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
