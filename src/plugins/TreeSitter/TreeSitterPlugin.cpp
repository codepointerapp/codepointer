#include "TreeSitterPlugin.hpp"
#include "GlobalCommands.hpp"
#include "iplugin.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFuture>
#include <QMutexLocker>
#include <QTimer>
#include <QtConcurrent>

TreeSitterPlugin::TreeSitterPlugin() {
    name = tr("Tree-sitter Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    connect(&scanWatcher, &QFutureWatcher<CommandArgs>::finished, this, [this]() {
        auto locker = QMutexLocker(&queueMutex);
        qDebug() << "Tree-sitter: Scan finished. Pending queue:" << pendingScanDirs;
        locker.unlock();
        QTimer::singleShot(0, this, [this]() { startNextScan(); });
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

    auto locker = QMutexLocker(&queueMutex);
    if (scanFuture.isValid() && !scanFuture.isFinished()) {
        if (!pendingScanDirs.contains(sourceDir)) {
            pendingScanDirs.append(sourceDir);
            qDebug() << "TreeSitterPlugin: Queued scan for" << QDir(sourceDir).dirName();
        }
        return QtFuture::makeReadyValueFuture(CommandArgs{});
    }
    locker.unlock();

    return doScanProjectDir(sourceDir);
}

void TreeSitterPlugin::startNextScan() {
    auto locker = QMutexLocker(&queueMutex);
    if (pendingScanDirs.isEmpty()) {
        qDebug() << "TreeSitterPlugin: startNextScan - queue empty, done.";
        return;
    }
    auto nextDir = pendingScanDirs.takeFirst();
    locker.unlock();
    qDebug() << "TreeSitterPlugin: Starting next queued scan ->" << QDir(nextDir).dirName();
    doScanProjectDir(nextDir);
}

QFuture<CommandArgs> TreeSitterPlugin::doScanProjectDir(const QString &sourceDir) {
    scanIsCancelled.store(false);

    const auto dirToScan = QDir::toNativeSeparators(sourceDir);
    const auto projectName = QDir(sourceDir).dirName();

    // File collection runs inside the background thread so scanFuture is valid
    // immediately — any subsequent scanProjectDir call will see it and queue.
    scanFuture = QtConcurrent::run([this, dirToScan, projectName]() -> CommandArgs {
        const QStringList filters = {"*.cpp", "*.hpp", "*.c", "*.h", "*.cc", "*.hh"};
        QStringList fileList;
        QDirIterator it(dirToScan, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            fileList << it.next();
        }
        qDebug() << "TreeSitterPlugin: Scanning" << fileList.size() << "files in" << dirToScan;

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
        qDebug() << "TreeSitterPlugin:" << projectName << "- Symbol pass for" << totalFiles << "files";
        auto slowestMs = 0LL;
        QString slowestFile;
        auto lastSymPct = -1;
        for (auto si = 0; si < totalFiles; ++si) {
            if (scanIsCancelled.load()) {
                qDebug() << "Scan cancelled";
                return CommandArgs{};
            }
            QElapsedTimer fileTimer;
            fileTimer.start();
            for (const auto &sym : engine.getSymbols(fileList[si])) {
                if (sym.type.contains("class") || sym.type.contains("struct")) {
                    totalClasses++;
                } else {
                    totalFunctions++;
                }
            }
            const auto fileMs = fileTimer.elapsed();
            if (fileMs > slowestMs) {
                slowestMs = fileMs;
                slowestFile = fileList[si];
            }
            const auto symBucket = ((si + 1) * 100 / totalFiles / 10) * 10;
            if (symBucket > lastSymPct) {
                lastSymPct = symBucket;
                qDebug() << "TreeSitterPlugin:" << projectName << "- Symbol pass"
                         << symBucket << "% elapsed:" << passTimer.elapsed() / 1000 << "s";
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
    {
        auto locker = QMutexLocker(&queueMutex);
        pendingScanDirs.clear();
    }
    // Disconnect first so no finished/resultReady signals fire after we return,
    // which could reference members that are being destroyed.
    scanWatcher.disconnect();
    // Use scanFuture directly — scanWatcher only tracks the most-recently-set
    // future and would miss an orphaned one started by a prior scanProjectDir call.
    if (scanFuture.isValid() && !scanFuture.isFinished()) {
        qDebug() << "TreeSitterPlugin: Cancelling file scan...";
        scanIsCancelled.store(true);
        scanFuture.waitForFinished();
    }
}

