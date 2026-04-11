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
    
    if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh" || ext == "cxx" || ext == "hxx" || ext == "h") {
        return tree_sitter_cpp();
    } else if (ext == "c") {
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

    // Simplified query to capture all declaration-like nodes. 
    // We use different queries for C and C++ because some node types (class, namespace) only exist in C++.
    QString queryStr;
    if (context->language == tree_sitter_cpp()) {
        queryStr = 
            "(class_specifier) @symbol "
            "(struct_specifier) @symbol "
            "(enum_specifier) @symbol "
            "(enumerator) @symbol "
            "(namespace_definition) @symbol "
            "(function_definition) @symbol "
            "(field_declaration) @symbol "
            "(declaration) @symbol "
            "(parameter_declaration) @symbol "
            "(alias_declaration) @symbol "
            "(type_definition) @symbol ";
    } else {
        queryStr = 
            "(struct_specifier) @symbol "
            "(enum_specifier) @symbol "
            "(enumerator) @symbol "
            "(function_definition) @symbol "
            "(field_declaration) @symbol "
            "(declaration) @symbol "
            "(parameter_declaration) @symbol "
            "(type_definition) @symbol ";
    }

    uint32_t errorOffset;
    TSQueryError errorType;
    QByteArray queryBytes = queryStr.toUtf8();
    TSQuery *query = ts_query_new(context->language, queryBytes.data(), (uint32_t)queryBytes.size(), &errorOffset, &errorType);

    if (query) {
        TSQueryCursor *cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, query, root);

        TSQueryMatch match;
        while (ts_query_cursor_next_match(cursor, &match)) {
            TSNode symbolNode = match.captures[0].node;
            const char *symbolType = ts_node_type(symbolNode);

            QString typeName;
            QString name;
            QList<TSNode> nameNodes;

            // 1. Try to find the name and type by looking at children and fields
            if (strcmp(symbolType, "class_specifier") == 0 || 
                strcmp(symbolType, "struct_specifier") == 0 ||
                strcmp(symbolType, "enum_specifier") == 0 ||
                strcmp(symbolType, "namespace_definition") == 0) {

                TSNode nameNode = ts_node_child_by_field_name(symbolNode, "name", 4);
                if (nameNode.id != nullptr) nameNodes.append(nameNode);
                typeName = QString::fromUtf8(symbolType);
            } else if (strcmp(symbolType, "alias_declaration") == 0) {
                // using Timer = PosixTimer;
                TSNode nameNode = ts_node_child_by_field_name(symbolNode, "name", 4);
                if (nameNode.id != nullptr) nameNodes.append(nameNode);
                TSNode tNode = ts_node_child_by_field_name(symbolNode, "type", 4);
                if (tNode.id != nullptr) {
                    uint32_t start = ts_node_start_byte(tNode);
                    uint32_t end = ts_node_end_byte(tNode);
                    typeName = QString::fromUtf8(context->content.mid(start, end - start)).trimmed();
                }
            } else if (strcmp(symbolType, "type_definition") == 0) {
                // typedef PosixTimer Timer;
                TSNode tNode = ts_node_child_by_field_name(symbolNode, "type", 4);
                if (tNode.id != nullptr) {
                    uint32_t start = ts_node_start_byte(tNode);
                    uint32_t end = ts_node_end_byte(tNode);
                    typeName = QString::fromUtf8(context->content.mid(start, end - start)).trimmed();
                }
                TSNode declNode = ts_node_child_by_field_name(symbolNode, "declarator", 10);
                if (declNode.id != nullptr) nameNodes.append(declNode);
            } else {
                // For declarations and parameters, try to find type and declarator(s)
                TSNode typeNode = ts_node_child_by_field_name(symbolNode, "type", 4);
                if (typeNode.id == nullptr) {
                    // Fallback: the first named child is often the type
                    for (uint32_t i = 0; i < ts_node_named_child_count(symbolNode); ++i) {
                        TSNode child = ts_node_named_child(symbolNode, i);
                        const char* cType = ts_node_type(child);
                        if (strstr(cType, "type") || strcmp(cType, "primitive_type") == 0) {
                            typeNode = child;
                            break;
                        }
                    }
                }

                if (typeNode.id != nullptr) {
                    TSNode nameInType = ts_node_child_by_field_name(typeNode, "name", 4);
                    if (nameInType.id != nullptr) {
                        uint32_t start = ts_node_start_byte(nameInType);
                        uint32_t end = ts_node_end_byte(nameInType);
                        typeName = QString::fromUtf8(context->content.mid(start, end - start)).trimmed();
                    } else {
                        uint32_t start = ts_node_start_byte(typeNode);
                        uint32_t end = ts_node_end_byte(typeNode);
                        typeName = QString::fromUtf8(context->content.mid(start, end - start)).trimmed();
                    }
                }

                // Find declarators (names)
                TSNode declNode = ts_node_child_by_field_name(symbolNode, "declarator", 10);
                if (declNode.id != nullptr) {
                    nameNodes.append(declNode);
                } else {
                    // Fallback: look for identifier-like nodes that are NOT the type node
                    for (uint32_t i = 0; i < ts_node_named_child_count(symbolNode); ++i) {
                        TSNode child = ts_node_named_child(symbolNode, i);
                        if (child.id != typeNode.id) {
                            const char* cType = ts_node_type(child);
                            if (strstr(cType, "declarator") || strcmp(cType, "identifier") == 0) {
                                nameNodes.append(child);
                            }
                        }
                    }
                }
            }

            // Find parent class/struct/namespace
            QString parentName;
            QStringList parents;
            TSNode parentNode = ts_node_parent(symbolNode);
            while (parentNode.id != nullptr) {
                const char *pType = ts_node_type(parentNode);
                if (strcmp(pType, "class_specifier") == 0 || 
                    strcmp(pType, "struct_specifier") == 0 ||
                    strcmp(pType, "namespace_definition") == 0) {
                    TSNode pNameNode = ts_node_child_by_field_name(parentNode, "name", 4);
                    if (pNameNode.id != nullptr) {
                        uint32_t pStart = ts_node_start_byte(pNameNode);
                        uint32_t pEnd = ts_node_end_byte(pNameNode);
                        if (pEnd > pStart && pEnd <= (uint32_t)context->content.size()) {
                            parents.prepend(QString::fromUtf8(context->content.mid(pStart, pEnd - pStart)).trimmed());
                        }
                    }
                }
                parentNode = ts_node_parent(parentNode);
            }
            parentName = parents.join("::");

            for (TSNode nameNode : nameNodes) {
                // Drill down to the actual identifier
                TSNode current = nameNode;
                while (current.id != nullptr) {
                    const char *type = ts_node_type(current);
                    if (strcmp(type, "identifier") == 0 || 
                        strcmp(type, "type_identifier") == 0 || 
                        strcmp(type, "field_identifier") == 0 ||
                        strcmp(type, "destructor_name") == 0) {
                        
                        uint32_t start = ts_node_start_byte(current);
                        uint32_t end = ts_node_end_byte(current);
                        if (end > start && end <= (uint32_t)context->content.size()) {
                            QString name = QString::fromUtf8(context->content.mid(start, end - start));
                            
                            Symbol sym;
                            sym.name = name;
                            
                            // Specific auto-resolution for this specific declarator if needed
                            QString resolvedType = typeName;
                            if (resolvedType == "auto" || resolvedType == "auto &" || resolvedType == "auto*") {
                                TSNode val;
                                val.id = nullptr;
                                if (strcmp(ts_node_type(nameNode), "init_declarator") == 0) {
                                    val = ts_node_child_by_field_name(nameNode, "value", 5);
                                } else if (strcmp(symbolType, "parameter_declaration") == 0) {
                                    // Heuristic for lambda parameters: check name hints
                                    QString lowerName = name.toLower();
                                    if (lowerName.endsWith("listview")) resolvedType = "ListView";
                                    else if (lowerName.endsWith("tabs")) resolvedType = "TabWidget";
                                    else if (lowerName.endsWith("button")) resolvedType = "Button";
                                    else if (lowerName == "index") resolvedType = "int";
                                    else if (lowerName == "s" || lowerName == "text") resolvedType = "std::string";
                                    else if (lowerName == "input" || lowerName == "sender" || lowerName == "me" || lowerName == "source") {
                                        // Try to find the object this lambda is associated with
                                        // parameter_declaration -> parameter_list -> lambda_expression
                                        TSNode lambda = ts_node_parent(ts_node_parent(nameNode));
                                        if (lambda.id != nullptr && strcmp(ts_node_type(lambda), "lambda_expression") == 0) {
                                            TSNode assign = ts_node_parent(lambda);
                                            if (assign.id != nullptr && strcmp(ts_node_type(assign), "assignment_expression") == 0) {
                                                TSNode left = ts_node_child_by_field_name(assign, "left", 4);
                                                if (left.id != nullptr && strcmp(ts_node_type(left), "field_expression") == 0) {
                                                    TSNode obj = ts_node_child_by_field_name(left, "argument", 8);
                                                    if (obj.id != nullptr) {
                                                        uint32_t s = ts_node_start_byte(obj);
                                                        uint32_t e = ts_node_end_byte(obj);
                                                        QString objName = QString::fromUtf8(context->content.mid(s, e - s)).trimmed();
                                                        if (objName.toLower().startsWith("input")) resolvedType = "Input";
                                                        else if (objName.toLower().contains("button") || objName.toLower().startsWith("btn")) resolvedType = "Button";
                                                        else if (objName.toLower().contains("list")) resolvedType = "ListView";
                                                    }
                                                }
                                            }
                                        }
                                        if (resolvedType == "auto" && lowerName == "input") resolvedType = "Input";
                                    }
                                }

                                if (val.id != nullptr) {
                                    // Deep peek into the initializer
                                    QList<TSNode> stack = {val};
                                    int iterations = 0;
                                    while (!stack.isEmpty() && iterations++ < 50) {
                                        TSNode n = stack.takeFirst();
                                        if (n.id == nullptr) continue;
                                        const char* nt = ts_node_type(n);
                                        
                                        if (strcmp(nt, "call_expression") == 0) {
                                            TSNode func = ts_node_child_by_field_name(n, "function", 8);
                                            if (func.id != nullptr) {
                                                const char* ft = ts_node_type(func);
                                                if (strcmp(ft, "template_function") == 0) {
                                                    // Handle make_shared<T>, make_unique<T>, cast<T>
                                                    TSNode tArgs = ts_node_child_by_field_name(func, "arguments", 10);
                                                    if (tArgs.id != nullptr && ts_node_named_child_count(tArgs) > 0) {
                                                        TSNode t = ts_node_named_child(tArgs, 0);
                                                        uint32_t s = ts_node_start_byte(t);
                                                        uint32_t e = ts_node_end_byte(t);
                                                        resolvedType = QString::fromUtf8(context->content.mid(s, e - s)).trimmed();
                                                        break;
                                                    }
                                                } else if (strcmp(ft, "field_expression") == 0) {
                                                    // Handle layout->add(make_shared<T>())
                                                    TSNode fName = ts_node_child_by_field_name(func, "field", 10);
                                                    if (fName.id != nullptr) {
                                                        uint32_t fs = ts_node_start_byte(fName);
                                                        uint32_t fe = ts_node_end_byte(fName);
                                                        QString fieldName = QString::fromUtf8(context->content.mid(fs, fe - fs));
                                                        
                                                        if (fieldName == "add" || fieldName == "push_back" || fieldName == "insert") {
                                                            TSNode args = ts_node_child_by_field_name(n, "arguments", 10);
                                                            if (args.id != nullptr && ts_node_named_child_count(args) > 0) {
                                                                stack.append(ts_node_named_child(args, 0));
                                                                continue;
                                                            }
                                                        }
                                                    }
                                                }
                                                
                                                // Generic function call: foo() or ui::bar()
                                                uint32_t s = ts_node_start_byte(func);
                                                uint32_t e = ts_node_end_byte(func);
                                                QString fn = QString::fromUtf8(context->content.mid(s, e - s)).trimmed();
                                                if (!fn.isEmpty()) {
                                                    resolvedType = fn + "()";
                                                    break;
                                                }
                                            }
                                        } else if (strcmp(nt, "new_expression") == 0) {
                                            TSNode t = ts_node_child_by_field_name(n, "type", 4);
                                            if (t.id != nullptr) {
                                                uint32_t s = ts_node_start_byte(t);
                                                uint32_t e = ts_node_end_byte(t);
                                                resolvedType = QString::fromUtf8(context->content.mid(s, e - s)).trimmed();
                                                break;
                                            }
                                        }
                                        
                                        // If we haven't found a type yet, peek further if it's a simple wrapper
                                        if (strcmp(nt, "parenthesized_expression") == 0) {
                                            if (ts_node_named_child_count(n) > 0) stack.append(ts_node_named_child(n, 0));
                                        }
                                    }
                                }
                            }

                            sym.type = resolvedType.isEmpty() ? QString::fromUtf8(symbolType) : resolvedType;
                            sym.line = ts_node_start_point(symbolNode).row;
                            sym.column = ts_node_start_point(symbolNode).column;
                            sym.fileName = fileName;
                            sym.parentName = parentName;

                            // Identify if it's a definition
                            if (strcmp(symbolType, "function_definition") == 0 ||
                                strcmp(symbolType, "class_specifier") == 0 ||
                                strcmp(symbolType, "struct_specifier") == 0 ||
                                strcmp(symbolType, "enum_specifier") == 0 ||
                                strcmp(symbolType, "namespace_definition") == 0 ||
                                strcmp(symbolType, "alias_declaration") == 0 ||
                                strcmp(symbolType, "type_definition") == 0) {
                                sym.isDefinition = true;
                            } else if (strcmp(symbolType, "declaration") == 0 || 
                                       strcmp(symbolType, "field_declaration") == 0) {
                                // For declarations, check if it has an initializer or if it's a member variable
                                if (strcmp(ts_node_type(nameNode), "init_declarator") == 0) {
                                    sym.isDefinition = true;
                                } else {
                                    // Most field declarations are definitions of the member
                                    sym.isDefinition = true;
                                }
                            }

                            symbols.append(sym);
                        }
                        break; // Found identifier, stop drilling for this nameNode
                    }
                    
                    // Controlled drill down: follow 'declarator' field if available
                    TSNode next = ts_node_child_by_field_name(current, "declarator", 5);
                    if (next.id != nullptr) {
                        current = next;
                    } else if (ts_node_named_child_count(current) > 0) {
                        // Fallback to first named child (handles pointers, etc.)
                        current = ts_node_named_child(current, 0);
                    } else {
                        break;
                    }
                }
            }
        }
        ts_query_cursor_delete(cursor);
        ts_query_delete(query);
    }
 else {
        qDebug() << "TreeSitterEngine: Query error at" << errorOffset << "type" << errorType;
    }

    // Dump structure for debugging
#if 0
    if (!symbols.isEmpty()) {
        qDebug() << "TreeSitterEngine: Structure for" << fileName;
        QHash<QString, QStringList> groups;
        for (const auto& sym : symbols) {
            QString group = sym.parentName.isEmpty() ? "<globals>" : sym.parentName;
            groups[group] << QString("%1 (%2)").arg(sym.name, sym.type);
        }
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            qDebug() << "  [" << it.key() << "]";
            for (const auto& s : it.value()) qDebug() << "    -" << s;
        }
    }
#endif
    {
        QMutexLocker locker(&mutex);
        context->cachedSymbols = symbols;
        context->symbolsValid = true;
        updateIndexForFile(fileName, symbols);
    }

    return symbols;
}

void TreeSitterEngine::updateIndexForFile(const QString &fileName, const QList<Symbol> &symbols) {
    // Remove old symbols for this file using the reverse index
    for (const QString &name : fileToSymbolNames.values(fileName)) {
        auto it = globalIndex.find(name);
        while (it != globalIndex.end() && it.key() == name) {
            if (it.value().fileName == fileName) {
                it = globalIndex.erase(it);
            } else {
                ++it;
            }
        }
    }
    fileToSymbolNames.remove(fileName);

    for (const auto &sym : symbols) {
        globalIndex.insert(sym.name, sym);
        fileToSymbolNames.insert(fileName, sym.name);
    }
}

QList<TreeSitterEngine::Symbol> TreeSitterEngine::findSymbolsGlobal(const QString &name, bool exactMatch, const QString &previousWord, const QString &separator, const QString &fileName, int line, int column) {
    QMutexLocker locker(&mutex);

#if 0
    if (!separator.isEmpty()) {
        qDebug() << "TreeSitterEngine::findSymbolsGlobal: name=" << name 
                 << "prev=" << previousWord << "sep=" << separator 
                 << "file=" << fileName << "pos=" << line << ":" << column;
    }
#endif

    // If we have a separator, we are likely looking for members of previousWord.
    if (!separator.isEmpty() && !previousWord.isEmpty()) {
        QString typeName;
        
        if (separator == "::") {
            typeName = previousWord;
            qDebug() << "TreeSitterEngine: scope resolution, type=" << typeName;
        } else if (previousWord == "this" && fileContexts.contains(fileName)) {
            auto context = fileContexts[fileName];
            TSNode root = ts_tree_root_node(context->tree);
            TSPoint pt = {(uint32_t)line, (uint32_t)column};
            TSNode node = ts_node_descendant_for_point_range(root, pt, pt);
            while (node.id != nullptr) {
                const char *pType = ts_node_type(node);
                if (strcmp(pType, "class_specifier") == 0 || strcmp(pType, "struct_specifier") == 0) {
                    TSNode pNameNode = ts_node_child_by_field_name(node, "name", 4);
                    if (pNameNode.id != nullptr) {
                        uint32_t pStart = ts_node_start_byte(pNameNode);
                        uint32_t pEnd = ts_node_end_byte(pNameNode);
                        if (pEnd > pStart && pEnd <= (uint32_t)context->content.size()) {
                            typeName = QString::fromUtf8(context->content.mid(pStart, pEnd - pStart)).trimmed();
                        }
                    }
                    break;
                }
                node = ts_node_parent(node);
            }
            qDebug() << "TreeSitterEngine: 'this' resolved to type=" << typeName;
        } else if (fileContexts.contains(fileName)) {
            auto context = fileContexts[fileName];
            
            // Search for previousWord declaration in the file.
            // Pick the one that is closest to our current line (but before it).
            Symbol bestMatch;
            int bestLine = -1;
            for (const auto& sym : context->cachedSymbols) {
                if (sym.name == previousWord && sym.line <= line) {
                    if (sym.line > bestLine) {
                        bestLine = sym.line;
                        bestMatch = sym;
                    }
                }
            }
            
            if (bestLine != -1) {
                typeName = bestMatch.type;
            } else {
                // If not found in local scope, check if it's a member of the current class
                TSNode root = ts_tree_root_node(context->tree);
                TSPoint pt = {(uint32_t)line, (uint32_t)column};
                TSNode node = ts_node_descendant_for_point_range(root, pt, pt);
                while (node.id != nullptr) {
                    const char *pType = ts_node_type(node);
                    if (strcmp(pType, "class_specifier") == 0 || strcmp(pType, "struct_specifier") == 0) {
                        TSNode pNameNode = ts_node_child_by_field_name(node, "name", 4);
                        if (pNameNode.id != nullptr) {
                            uint32_t pStart = ts_node_start_byte(pNameNode);
                            uint32_t pEnd = ts_node_end_byte(pNameNode);
                            QString className = QString::fromUtf8(context->content.mid(pStart, pEnd - pStart)).trimmed();
                            
                            // Check if previousWord is a member of this class
                            for (const auto& sym : globalIndex) {
                                if (sym.parentName == className && sym.name == previousWord) {
                                    typeName = sym.type;
                                    qDebug() << "TreeSitterEngine: resolved" << previousWord << "as member of" << className << "with type" << typeName;
                                    break;
                                }
                            }
                        }
                        if (!typeName.isEmpty()) break;
                    }
                    node = ts_node_parent(node);
                }
                
                if (typeName.isEmpty()) {
                    qDebug() << "TreeSitterEngine: could not find declaration of" << previousWord << "in current file or class scope";
                }
            }

            if (!typeName.isEmpty()) {
                // Heuristic: remove pointer/reference decorators and template arguments
                typeName.remove('*').remove('&').trimmed();
                int templateStart = typeName.indexOf('<');
                if (templateStart != -1) {
                    typeName = typeName.left(templateStart).trimmed();
                }
                qDebug() << "TreeSitterEngine: resolved effective type to" << typeName;
            }
        }
        
        if (!typeName.isEmpty()) {
            QList<Symbol> results;
            QString currentType = typeName;
            QSet<QString> visited;
            
            qDebug() << "TreeSitterEngine: resolving members for" << typeName;

            while (!currentType.isEmpty() && !visited.contains(currentType)) {
                visited.insert(currentType);
                
                // 1. Clean up type: remove pointers, references and whitespace
                currentType.remove('*').remove('&').trimmed();
                
                // 2. Resolve function calls: foo() -> look up return type in global index
                if (currentType.endsWith("()")) {
                    QString funcName = currentType.left(currentType.length() - 2);
                    QString shortName = funcName;
                    int lastColon = funcName.lastIndexOf("::");
                    if (lastColon != -1) shortName = funcName.mid(lastColon + 2);
                    
                    auto funcSyms = globalIndex.values(shortName);
                    QString returnType;
                    for (const auto& fs : funcSyms) {
                        // Match short name and verify namespace if possible
                        if (fs.name == shortName && (lastColon == -1 || fs.parentName.endsWith(funcName.left(lastColon)))) {
                            returnType = fs.type;
                            break;
                        }
                    }
                    if (!returnType.isEmpty()) {
                        qDebug() << "TreeSitterEngine: resolved function" << funcName << "to return type" << returnType;
                        currentType = returnType;
                        continue;
                    }
                }

                // 3. Unwrap common templates: Widget<T>, shared_ptr<T> -> T
                int tStart = currentType.indexOf('<');
                int tEnd = currentType.lastIndexOf('>');
                if (tStart != -1 && tEnd > tStart) {
                    QString wrapper = currentType.left(tStart).trimmed();
                    if (wrapper == "Widget" || wrapper.endsWith("::Widget") || 
                        wrapper.contains("shared_ptr") || wrapper.contains("unique_ptr")) {
                        QString inner = currentType.mid(tStart + 1, tEnd - tStart - 1).trimmed();
                        qDebug() << "TreeSitterEngine: unwrapping" << wrapper << "to" << inner;
                        currentType = inner;
                        continue;
                    }
                    // For unknown templates, try the base name if no members found for full name
                }

                // 4. Search for members of currentType in the global index
                QString searchType = currentType;
                // If it's still a template, try the base name
                int searchStart = searchType.indexOf('<');
                if (searchStart != -1) searchType = searchType.left(searchStart).trimmed();

                for (auto it = globalIndex.begin(); it != globalIndex.end(); ++it) {
                    const auto& sym = it.value();
                    if (sym.parentName == searchType || sym.parentName.endsWith("::" + searchType)) {
                        if (exactMatch) {
                            if (sym.name == name) results.append(sym);
                        } else {
                            if (name.isEmpty() || sym.name.contains(name, Qt::CaseInsensitive)) {
                                results.append(sym);
                            }
                        }
                    }
                }
                
                if (!results.isEmpty()) {
                    qDebug() << "TreeSitterEngine: found" << results.size() << "members for" << searchType;
                    return results;
                }
                
                // 5. Alias resolution (using/typedef)
                auto aliases = globalIndex.values(searchType);
                QString nextType;
                for (const auto& asym : aliases) {
                    if (asym.parentName.isEmpty() && !asym.type.isEmpty() && asym.type != searchType && !asym.type.startsWith("function")) {
                        nextType = asym.type;
                        break;
                    }
                }
                
                if (nextType.isEmpty()) break;
                qDebug() << "TreeSitterEngine: resolving alias" << searchType << "->" << nextType;
                currentType = nextType;
            }

            // Debug: help diagnose why lookup failed
            if (results.isEmpty()) {
                QSet<QString> parents;
                for (const auto& sym : globalIndex) if (!sym.parentName.isEmpty()) parents.insert(sym.parentName);
                QStringList pList = parents.values();
                qDebug() << "TreeSitterEngine: no members found for" << currentType << ". Indexed parents (sample):" << pList.mid(0, 10);
            }
        }
        // If we have a separator, we strictly want members of the resolved type.
        // Falling back to global symbols causes noise (like 'format').
        return {};
    }

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
