#pragma once

#include <QString>
#include <QHash>
#include <tree_sitter/api.h>
#include <memory>
#include <QList>
#include <QMutex>

class TreeSitterEngine {
public:
    TreeSitterEngine();
    ~TreeSitterEngine();

    // Parses or updates the AST for a given file
    void updateFile(const QString &fileName, const QString &content);

    struct Symbol {
        QString name;
        QString type;
        int line;
        int column;
    };
    QList<Symbol> getSymbols(const QString &fileName);

    // Returns the root node for a given file
    TSNode getRootNode(const QString &fileName);

    // Returns the language for a file based on extension
    const TSLanguage* getLanguageForFile(const QString &fileName);

private:
    struct FileContext {
        TSTree *tree = nullptr;
        const TSLanguage *language = nullptr;
        QByteArray content;
        ~FileContext() {
            if (tree) ts_tree_delete(tree);
        }
    };

    TSParser *parser;
    QHash<QString, std::shared_ptr<FileContext>> fileContexts;
    QMutex mutex;
};
