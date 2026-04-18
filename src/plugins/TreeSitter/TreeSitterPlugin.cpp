#include "TreeSitterPlugin.hpp"
#include "GlobalCommands.hpp"
#include "iplugin.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFuture>
#include <QtConcurrent>

TreeSitterPlugin::TreeSitterPlugin() {
    name = tr("Tree-sitter Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    connect(&scanWatcher, &QFutureWatcher<CommandArgs>::finished, this, []() {
        qDebug() << "Tree-sitter: File processing finished.";
    });
}

TreeSitterPlugin::~TreeSitterPlugin() {
    cleanup();
}

int TreeSitterPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &args) const {
    auto const static projectTriggers = QStringList{
        GlobalCommands::ProjectLoaded,
        GlobalCommands::BuildFinished,
    };

    if (projectTriggers.contains(command)) {
        return CommandPriority::HighPriority;
    }

    auto const static filters = QStringList{
        "cpp", "hpp", "c", "h", "cc", "hh", "cxx", "hxx",
    };
    auto const static fileCommands = QStringList{
        GlobalCommands::VariableInfo,
        GlobalCommands::KeywordTooltip,
        GlobalCommands::ListSymbols,
    };

    if (fileCommands.contains(command)) {
        auto filename = args[GlobalArguments::FileName].toString();
        auto fi = QFileInfo(filename);
        if (filters.contains(fi.suffix(), Qt::CaseInsensitive)) {
            return CommandPriority::HighPriority;
        }
    }

    return CommandPriority::CannotHandle;
}

QFuture<CommandArgs> TreeSitterPlugin::scanProjectDir(const QString &sourceDir) {
    if (sourceDir.isEmpty()) {
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }

    const auto dirToScan = QDir::toNativeSeparators(sourceDir);
    const auto projectName = QDir(sourceDir).dirName();
    const QStringList filters = {"*.cpp", "*.hpp", "*.c", "*.h", "*.cc", "*.hh"};

    QStringList fileList;
    QDirIterator it(dirToScan, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        fileList << it.next();
    }
    qDebug() << "TreeSitterPlugin: Scanning" << fileList.size() << "files in" << dirToScan;

    // Single background thread: parse all files then build the symbol index.
    // Serial execution avoids concurrent ts_parser/ts_query calls on the same
    // TSLanguage pointer, which corrupt heap state inside tree-sitter.
    scanIsCancelled.store(false);

    // FIXME: we could serialize it more - with n-2 threads.
    scanFuture = QtConcurrent::run([this, projectName, fileList = std::move(fileList)]() -> CommandArgs {
        const auto totalFiles = static_cast<int>(fileList.size());
        auto lastReportedPct = -1;
        QElapsedTimer timer;
        timer.start();
        for (auto i = 0; i < totalFiles; ++i) {
            if (scanIsCancelled.load()) {
                qDebug() << "Canceled after " << i << "files, currently " << fileList[i];
                return CommandArgs{};
            }
            QFile f(fileList[i]);
            if (f.open(QIODevice::ReadOnly)) {
                engine.updateFile(fileList[i], f.readAll());
            }
            const auto pct = (i + 1) * 100 / totalFiles;
            const auto bucket = (pct / 10) * 10;
            if (bucket > lastReportedPct) {
                lastReportedPct = bucket;
                const auto elapsedMs = timer.elapsed();
                const auto etaMs = (i + 1) < totalFiles
                                       ? elapsedMs * (totalFiles - i - 1) / (i + 1)
                                       : 0LL;
                qDebug() << "TreeSitterPlugin:" << projectName << "- Parsed" << (i + 1)
                         << "/" << totalFiles << "files (" << bucket << "%)"
                         << "elapsed:" << elapsedMs / 1000 << "s ETA:" << etaMs / 1000 << "s";
            }
        }

        auto totalClasses = 0;
        auto totalFunctions = 0;
        QElapsedTimer passTimer;
        passTimer.start();
        const auto trackedFiles = engine.getTrackedFiles();
        qDebug() << "TreeSitterPlugin:" << projectName << "- Symbol pass:"
                 << trackedFiles.size() << "tracked files (this project scanned" << totalFiles << ");"
                 << "getTrackedFiles() took" << passTimer.elapsed() << "ms";
        passTimer.restart();
        auto slowestMs = 0LL;
        QString slowestFile;
        for (const QString &file : trackedFiles) {
            if (scanIsCancelled.load()) {
                qDebug() << "Scan cancelled";
                return CommandArgs{};
            }
            QElapsedTimer fileTimer;
            fileTimer.start();
            for (const auto &sym : engine.getSymbols(file)) {
                if (sym.type.contains("class") || sym.type.contains("struct")) {
                    totalClasses++;
                } else {
                    totalFunctions++;
                }
            }
            const auto fileMs = fileTimer.elapsed();
            if (fileMs > slowestMs) {
                slowestMs = fileMs;
                slowestFile = file;
            }
        }
        qDebug() << "TreeSitterPlugin:" << projectName << "- Found" << totalClasses
                 << "classes/structs and" << totalFunctions << "functions;"
                 << "symbol pass took" << passTimer.elapsed() << "ms;"
                 << "slowest file" << slowestMs << "ms:" << slowestFile;
        return CommandArgs{};
    });
    scanWatcher.setFuture(scanFuture);
    return scanFuture;
}

QFuture<CommandArgs> TreeSitterPlugin::handleCommandAsync(const QString &command,
                                                          const CommandArgs &args) {
    if (command == GlobalCommands::ProjectLoaded || command == GlobalCommands::BuildFinished) {
        auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
        if (sourceDir.isEmpty()) {
            sourceDir = args[GlobalArguments::BuildDirectory].toString();
        }
        return scanProjectDir(sourceDir);
    }

    CommandArgs result;
    if (command == "ListSymbols") {
        auto filename = args[GlobalArguments::FileName].toString();
        auto content = args[GlobalArguments::Content].toString().toUtf8();
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
        auto content = args[GlobalArguments::Content].toString().toUtf8();
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
                {GlobalArguments::Raw, sym.name},
                {GlobalArguments::IsDefinition, sym.isDefinition},
            }));
        }
        result[GlobalArguments::Symbol] = symbol;
        result[GlobalArguments::Tags] = tagList;
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

    return QtFuture::makeReadyValueFuture(result);
}

void TreeSitterPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);
}

void TreeSitterPlugin::on_client_unmerged(qmdiHost *host) {
    cleanup();
    IPlugin::on_client_unmerged(host);
}

void TreeSitterPlugin::cleanup() {
    // Disconnect first so no finished/resultReady signals fire after we return,
    // which could reference members that are being destroyed.
    scanWatcher.disconnect();
    if (scanWatcher.isRunning()) {
        qDebug() << "TreeSitterPlugin: Cancelling file scan...";
        scanIsCancelled.store(true);
        scanWatcher.waitForFinished();
    }
}

