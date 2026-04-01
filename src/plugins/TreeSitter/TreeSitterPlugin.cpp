#include "TreeSitterPlugin.hpp"
#include "GlobalCommands.hpp"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFuture>
#include <QPromise>
#include <QThreadPool>

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

                QDirIterator it(dirToScan, filters, QDir::Files, QDirIterator::Subdirectories);

                int count = 0;
                while (it.hasNext()) {
                    QString file = it.next();
                    QFile f(file);
                    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        engine.updateFile(file, f.readAll());
                        engine.getSymbols(file);
                        count++;
                    }
                }
                qDebug() << "TreeSitterPlugin: Finished scanning" << count << "files in"
                         << dirToScan;
            }
        } else if (command == GlobalCommands::VariableInfo) {
            auto filename = args[GlobalArguments::FileName].toString();
            auto content = args[GlobalArguments::Content].toString();
            auto symbol = args[GlobalArguments::RequestedSymbol].toString();

            if (!content.isEmpty()) {
                engine.updateFile(filename, content);
            }

            auto symbols = engine.getSymbols(filename);
            QVariantList tagList;
            for (const auto &sym : symbols) {
                if (sym.name.contains(symbol, Qt::CaseInsensitive)) {
                    tagList.append(QVariant::fromValue(CommandArgs{
                        {GlobalArguments::FileName, filename},
                        {GlobalArguments::Type, sym.type},
                        {GlobalArguments::Value, sym.name},
                        {GlobalArguments::Name, sym.name},
                        {GlobalArguments::LineNumber, sym.line + 1},
                    }));
                }
            }
            result[GlobalArguments::Tags] = tagList;
        }

        promise->addResult(result);
        promise->finish();
        delete promise;
    });

    return future;
}
