#pragma once

#include <QString>
#include <QHash>
#include <QMultiHash>
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
        QString fileName; // Store filename for global index lookup
        QString parentName; // For members: name of the class/struct
    };
    
    // Returns cached symbols or parses if needed
    QList<Symbol> getSymbols(const QString &fileName);

    // Fast global lookup
    QList<Symbol> findSymbolsGlobal(const QString &name, bool exactMatch,
                                    const QString &previousWord = QString(),
                                    const QString &separator = QString(),
                                    const QString &fileName = QString(),
                                    int line = -1, int column = -1);

    // Returns the root node for a given file
    TSNode getRootNode(const QString &fileName);

    // Returns the language for a file based on extension
    const TSLanguage* getLanguageForFile(const QString &fileName);

    QList<QString> getTrackedFiles() const;

private:
    struct FileContext {
        TSTree *tree = nullptr;
        const TSLanguage *language = nullptr;
        QByteArray content;
        QList<Symbol> cachedSymbols;
        bool symbolsValid = false;
        
        ~FileContext() {
            if (tree) ts_tree_delete(tree);
        }
    };

    TSParser *parser;
    QHash<QString, std::shared_ptr<FileContext>> fileContexts;
    
    // Global index: symbol name -> Symbol info
    QMultiHash<QString, Symbol> globalIndex;
    
    mutable QMutex mutex;
    
    void updateIndexForFile(const QString &fileName, const QList<Symbol> &symbols);
};
