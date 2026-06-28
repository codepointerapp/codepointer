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
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <atomic>

#if defined(__linux__)
#include <malloc.h>
#elif defined(_WIN32)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif

TreeSitterPlugin::TreeSitterPlugin() {
    name = tr("Tree-sitter Support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    scanDebounceTimer.setSingleShot(true);
    scanDebounceTimer.setInterval(200);
    connect(&scanDebounceTimer, &QTimer::timeout, this, &TreeSitterPlugin::startNextScan);

    connect(&scanWatcher, &QFutureWatcher<CommandArgs>::finished, this, [this]() {
        auto locker = QMutexLocker(&queueMutex);
        qDebug() << "Tree-sitter: Scan finished. Pending queue:" << pendingScanDirs;
        locker.unlock();
        QTimer::singleShot(0, this, [this]() { startNextScan(); });
    });
}

TreeSitterPlugin::~TreeSitterPlugin() { cleanup(); }

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

    {
        auto locker = QMutexLocker(&queueMutex);
        if (!pendingScanDirs.contains(sourceDir)) {
            pendingScanDirs.prepend(sourceDir);
            qDebug() << "TreeSitterPlugin: Queued scan for" << QDir(sourceDir).dirName();
        }
    }

    // If a scan is already running, the finished signal will call startNextScan.
    // If idle, (re)start the debounce timer so rapid ProjectLoaded bursts are
    // collected into one combined scan.
    if (!scanFuture.isValid() || scanFuture.isFinished()) {
        scanDebounceTimer.start();
    }

    return QtFuture::makeReadyValueFuture(CommandArgs{});
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
    engine.resetCancel();

    // Drain the pending queue now — process all known projects in one parallel pass
    // to minimise arena fragmentation (single malloc_trim at the end).
    QStringList dirsToScan;
    dirsToScan.append(QDir::toNativeSeparators(sourceDir));
    {
        auto locker = QMutexLocker(&queueMutex);
        for (const auto &dir : std::as_const(pendingScanDirs)) {
            dirsToScan.append(QDir::toNativeSeparators(dir));
        }
        pendingScanDirs.clear();
    }

    for (const auto &dir : std::as_const(dirsToScan)) {
        engine.addProjectRoot(dir);
    }

    scanFuture = QtConcurrent::run([this, dirsToScan]() -> CommandArgs {
        const QStringList filters = {"*.cpp", "*.hpp", "*.c", "*.h", "*.cc", "*.hh"};
        QStringList fileList;
        QStringList projectNames;
        for (const auto &dir : dirsToScan) {
            projectNames.append(QDir(dir).dirName());
            QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                fileList << it.next();
            }
        }
        const auto projectLabel = projectNames.join(", ");
        const auto totalFiles = static_cast<int>(fileList.size());
        qDebug() << "TreeSitterPlugin: Scanning" << totalFiles << "files across"
                 << projectNames.size() << "projects (" << projectLabel << ")";
        if (totalFiles == 0) {
            return CommandArgs{};
        }

        // Warm up language singletons before parallel work (lazy statics, not thread-safe on first
        // call)
        TreeSitterEngine::getLanguageForFile(QStringLiteral("dummy.cpp"));
        TreeSitterEngine::getLanguageForFile(QStringLiteral("dummy.c"));

        std::atomic<int> processedCount{0};
        std::atomic<int> totalClasses{0};
        std::atomic<int> totalFunctions{0};
        std::atomic<long long> slowestMs{0};
        std::atomic<long long> totalCpuMs{0};
        QMutex statsMutex;
        QString slowestFile;
        int lastReportedBucket = -1;

        QElapsedTimer timer;
        timer.start();

        const int threadCount = qMax(1, QThread::idealThreadCount() - 1);
        scanPool.setMaxThreadCount(threadCount);
        scanPool.setExpiryTimeout(500); // threads exit 500ms after scan completes
#if defined(__linux__)
        mallopt(M_ARENA_MAX, 2); // prevent per-thread glibc arena explosion under parallel parsing
#endif
        qDebug() << "TreeSitterPlugin:" << projectLabel << "- parsing with" << threadCount
                 << "threads";

        std::atomic<int> nextFile{0};

        // Each worker pulls files via atomic index — cancellation is checked before every file,
        // and engine.cancelParsing() aborts any in-progress ts_parser_parse_string call.
        auto worker = [&]() {
            while (true) {
                if (scanIsCancelled.load()) {
                    return;
                }
                const auto i = nextFile.fetch_add(1, std::memory_order_relaxed);
                if (i >= totalFiles) {
                    return;
                }
                const auto &filePath = fileList[i];
                QFile f(filePath);
                if (!f.open(QIODevice::ReadOnly)) {
                    processedCount.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const bool isHeader = TreeSitterEngine::isHeaderFile(filePath);
                const qint64 maxFileSize = isHeader ? 2 * 1024 * 1024 : 512 * 1024;
                if (f.size() > maxFileSize) {
                    qDebug() << "TreeSitterPlugin: skipping large file" << filePath << "("
                             << f.size() / 1024 << "KB)";
                    processedCount.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                auto fileContent = f.readAll();

                QElapsedTimer fileTimer;
                fileTimer.start();
                engine.updateFile(filePath, fileContent);
                for (const auto &sym : engine.getSymbols(filePath, fileContent)) {
                    if (sym.type.contains("class") || sym.type.contains("struct")) {
                        totalClasses.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        totalFunctions.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                const auto fileMs = fileTimer.elapsed();
                totalCpuMs.fetch_add(fileMs, std::memory_order_relaxed);

                const auto done = processedCount.fetch_add(1, std::memory_order_relaxed) + 1;
                const auto pct = done * 100 / totalFiles;
                const auto bucket = (pct / 10) * 10;

                auto locker = QMutexLocker(&statsMutex);
                if (fileMs > slowestMs.load(std::memory_order_relaxed)) {
                    slowestMs.store(fileMs, std::memory_order_relaxed);
                    slowestFile = filePath;
                }
                if (bucket > lastReportedBucket) {
                    lastReportedBucket = bucket;
                    const auto elapsedMs = timer.elapsed();
                    const auto etaMs = (bucket > 10 && done < totalFiles)
                                           ? elapsedMs * (totalFiles - done) / done
                                           : 0LL;
                    locker.unlock();
                    qDebug() << "TreeSitterPlugin:" << projectLabel << "- Parsed" << done << "/"
                             << totalFiles << "files (" << bucket << "%)"
                             << "elapsed:" << elapsedMs / 1000 << "s"
                             << (etaMs > 0 ? QString("ETA: %1s").arg(etaMs / 1000) : QString{});
                }
            }
        };

        QList<QFuture<void>> futures;
        futures.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t) {
            futures.append(QtConcurrent::run(&scanPool, worker));
        }
        for (auto &f : futures) {
            f.waitForFinished();
        }

        // Return freed heap memory (large TSTrees) back to the OS
#if 0
#if defined(__linux__)
        malloc_trim(0);
#elif defined(_WIN32)
        _heapmin();
#elif defined(__APPLE__)
        malloc_zone_pressure_relief(nullptr, 0);
#endif
#endif

        if (scanIsCancelled.load()) {
            qDebug() << "TreeSitterPlugin:" << projectLabel << "- Scan cancelled";
            return CommandArgs{};
        }
        const auto wallMs = timer.elapsed();
        const auto cpuMs = totalCpuMs.load();
        qDebug() << "TreeSitterPlugin:" << projectLabel << "- Found" << totalClasses.load()
                 << "classes/structs and" << totalFunctions.load() << "functions;"
                 << "wall:" << wallMs << "ms cpu:" << cpuMs << "ms"
                 << QString("(%1x vs sequential)")
                        .arg(cpuMs > 0 ? double(cpuMs) / wallMs : 1.0, 0, 'f', 1)
                 << "; slowest file" << slowestMs.load() << "ms:" << slowestFile;
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
        auto symbols = engine.getSymbols(filename, content);
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
            engine.getSymbols(filename, content);
        }

        QVariantList tagList;
        auto symbols = engine.findSymbolsGlobal(symbol, exactMatch, previousWord, separator,
                                                filename, line, column, content);
        for (const auto &sym : symbols) {
            tagList.append(QVariant::fromValue(CommandArgs{
                {GlobalArguments::FileName, engine.resolveFileId(sym.fileId)},
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
        auto content = args[GlobalArguments::Content].toByteArray();
        auto line = args[GlobalArguments::LineNumber].toInt();
        auto col = args[GlobalArguments::ColumnNumber].toInt();
        auto prev = args[GlobalArguments::PreviousWord].toString();
        auto sep = args[GlobalArguments::Separator].toString();

        if (!content.isEmpty() && !filename.isEmpty()) {
            engine.updateFile(filename, content);
        }

        QString tooltip;
        auto symbols =
            engine.findSymbolsGlobal(symbol, true, prev, sep, filename, line, col, content);

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

void TreeSitterPlugin::on_client_merged(qmdiHost *host) { IPlugin::on_client_merged(host); }

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
        engine.cancelParsing(); // abort any in-progress ts_parser_parse_string
        scanFuture.waitForFinished();
        engine.resetCancel();
    }
}
