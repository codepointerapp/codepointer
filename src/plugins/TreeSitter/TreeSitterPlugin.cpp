#include "TreeSitterPlugin.hpp"
#include "GlobalCommands.hpp"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFuture>
#include <QPromise>
#include <QTextStream>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>

TreeSitterPlugin::TreeSitterPlugin() {
    name = tr("Tree-sitter Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
}

TreeSitterPlugin::~TreeSitterPlugin() {}

int TreeSitterPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &) const {
    if (command == GlobalCommands::VariableInfo) {
        return 101;
    } else if (command == GlobalCommands::KeywordTooltip) {
        return 101;
    } else if (command == GlobalCommands::ProjectLoaded ||
               command == GlobalCommands::BuildFinished) {
        return 101;
    } else if (command == "ListSymbols") {
        return 101;
    }
    return CommandPriority::CannotHandle;
}

QFuture<CommandArgs> TreeSitterPlugin::handleCommandAsync(const QString &command,
                                                          const CommandArgs &args) {
    auto promise = new QPromise<CommandArgs>();
    auto future = promise->future();

    QThreadPool::globalInstance()->start([this, command, args, promise]() {
        promise->start();
        CommandArgs result;

        if (command == GlobalCommands::ProjectLoaded || command == GlobalCommands::BuildFinished) {
            auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
            if (sourceDir.isEmpty()) {
                sourceDir = args[GlobalArguments::BuildDirectory].toString();
            }
            if (!sourceDir.isEmpty()) {
                QString dirToScan = QDir::toNativeSeparators(sourceDir);
                QStringList filters;
                filters << "*.cpp" << "*.hpp" << "*.c" << "*.h" << "*.cc" << "*.hh";

                QStringList fileList;
                QDirIterator it(dirToScan, filters, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    fileList << it.next();
                }

                qDebug() << "TreeSitterPlugin: Scanning" << fileList.size() << "files...";
                QElapsedTimer timer;
                timer.start();

                int threadCount = qMax(1, QThread::idealThreadCount() - 2);
                QThreadPool pool;
                pool.setMaxThreadCount(threadCount);

                QtConcurrent::blockingMap(&pool, fileList, [this](const QString &file) {
                    QFile f(file);
                    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        engine.updateFile(file, f.readAll());
                        engine.getSymbols(file);
                    }
                });

                int totalClasses = 0;
                int totalFunctions = 0;
                for (const QString &file : engine.getTrackedFiles()) {
                    auto symbols = engine.getSymbols(file);
                    for (const auto &sym : symbols) {
                        if (sym.type.contains("class") || sym.type.contains("struct")) {
                            totalClasses++;
                        } else {
                            totalFunctions++;
                        }
                    }
                }

                qDebug() << "TreeSitterPlugin: Finished scanning in" << timer.elapsed() << "ms.";
                qDebug() << "TreeSitterPlugin: Found" << totalClasses << "classes/structs and"
                         << totalFunctions << "functions.";
            }
        } else if (command == "ListSymbols") {
            auto filename = args[GlobalArguments::FileName].toString();
            auto content = args[GlobalArguments::Content].toString();
            if (!content.isEmpty()) {
                engine.updateFile(filename, content);
            }
            auto symbols = engine.getSymbols(filename);
            QVariantList tagList;
            for (const auto &sym : symbols) {
                tagList.append(QVariant::fromValue(CommandArgs{
                    {GlobalArguments::FileName, filename},
                    {GlobalArguments::Type, sym.type},
                    {GlobalArguments::Value, sym.name},
                    {GlobalArguments::Name, sym.name},
                    {GlobalArguments::LineNumber, sym.line + 1},
                    {GlobalArguments::ColumnNumber, sym.column + 1},
                }));
            }
            result[GlobalArguments::Tags] = tagList;
        } else if (command == GlobalCommands::VariableInfo) {
            auto filename = args[GlobalArguments::FileName].toString();
            auto content = args[GlobalArguments::Content].toString();
            auto symbol = args[GlobalArguments::RequestedSymbol].toString();
            auto exactMatch = args[GlobalArguments::ExactMatch].toBool();
            auto previousWord = args[GlobalArguments::PreviousWord].toString();
            auto separator = args[GlobalArguments::Separator].toString();
            auto line = args[GlobalArguments::LineNumber].toInt();
            auto column = args[GlobalArguments::ColumnNumber].toInt();

            if (!content.isEmpty()) {
                engine.updateFile(filename, content);
                engine.getSymbols(filename);
            }

            QVariantList tagList;
            auto symbols = engine.findSymbolsGlobal(symbol, exactMatch, previousWord, separator,
                                                    filename, line, column);

            for (const auto &sym : symbols) {
                tagList.append(QVariant::fromValue(CommandArgs{
                    {GlobalArguments::FileName, sym.fileName},
                    {GlobalArguments::Type, sym.type},
                    {GlobalArguments::Value, sym.name},
                    {GlobalArguments::Name, sym.name},
                    {GlobalArguments::LineNumber, sym.line + 1},
                    {GlobalArguments::ColumnNumber, sym.column + 1},
                    {GlobalArguments::Raw, sym.name}, // Used for search in editor
                    {GlobalArguments::IsDefinition, sym.isDefinition},
                }));
            }
            result[GlobalArguments::Tags] = tagList;
            result[GlobalArguments::Symbol] = symbol; // Fix empty first item
        } else if (command == GlobalCommands::KeywordTooltip) {
            auto filename = args[GlobalArguments::FileName].toString();
            auto symbol = args[GlobalArguments::RequestedSymbol].toString();

            QString tooltip;
            auto symbols = engine.findSymbolsGlobal(symbol, true);

            QList<TreeSitterEngine::Symbol> definitions;
            for (const auto &sym : symbols) {
                if (sym.isDefinition) {
                    definitions.append(sym);
                }
            }

            const auto &toShow = definitions.isEmpty() ? symbols : definitions;
            for (const auto &sym : toShow) {
                if (!tooltip.isEmpty()) {
                    tooltip += "\n---\n";
                }
                if (!sym.signature.isEmpty()) {
                    tooltip += sym.signature;
                } else {
                    tooltip += QString("%1 %2").arg(sym.type, sym.name);
                }
            }

            if (!tooltip.isEmpty()) {
                result[GlobalArguments::Tooltip] = tooltip;
            }
        }

        promise->addResult(result);
        promise->finish();
        delete promise;
    });

    return future;
}
