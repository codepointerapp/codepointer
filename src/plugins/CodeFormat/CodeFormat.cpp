#include "CodeFormat.hpp"
#include "GlobalCommands.hpp"
#include "pluginmanager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QtConcurrent>
#include <qfilesystemwatcher.h>

Formatter *Formatter::fromJson(const QJsonObject &obj) {
    const QVariantMap m = obj.toVariantMap();
    Formatter *f = new Formatter;

    f->name = m.value("name").toString();
    f->binary = m.value("binary").toString();
    f->processStdin = m.value("stdin", false).toBool();
    f->processStdout = m.value("stdout", false).toBool();
    f->requiresFilepath = m.value("requires_filepath", false).toBool();
    f->tempfile = m.value("tempfile", false).toBool();
    f->extensions = m.value("exts").toStringList();
    f->args = m.value("args").toStringList();
    return f;
}

bool FormatterRegistry::loadFromFile(const QString &jsonFile) {
    auto f = QFile(jsonFile);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "FormatterRegistry: Could not open" << jsonFile << f.errorString();
        return false;
    }

    auto data = f.readAll();
    auto doc = QJsonDocument::fromJson(data);

    if (doc.isNull()) {
        qDebug() << "FormatterRegistry: JSON document is null for" << jsonFile;
        return false;
    }

    if (!doc.isArray()) {
        qDebug() << "FormatterRegistry: JSON document is not an array for" << jsonFile;
        return false;
    }

    auto arr = doc.array();
    m_indenters.clear();
    m_extIndex.clear();
    for (auto &v : std::as_const(arr)) {
        if (!v.isObject()) {
            continue;
        }

        auto t = Formatter::fromJson(v.toObject());
        m_indenters.append(t);
        for (auto &ext : std::as_const(t->extensions)) {
            m_extIndex.insert(ext.toLower(), t);
        }
    }

    return true;
}

const Formatter *FormatterRegistry::getForFile(const QString &filePath) const {
    auto info = QFileInfo(filePath);
    auto ext = info.suffix().toLower();
    if (!m_extIndex.contains(ext)) {
        qDebug() << "FormatterRegistry: no indenter found for suffix" << ext << "of" << filePath
                 << "m_extIndex keys:" << m_extIndex.keys();
        return nullptr;
    }
    auto lll = m_extIndex[ext];
    return lll;
}

int FormatterRegistry::count() const { return m_indenters.size(); }

void FormatterRegistry::clear() {
    m_extIndex.clear();
    m_indenters.clear();
}

CodeFormatPlugin::CodeFormatPlugin() {
    name = tr("Code format support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    config.pluginName = tr("Code format");
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("More paths for format tools"))
            .setDescription(tr("If a format tool is not on the standard PATH, add it here"))
            .setKey(Config::ExtraPathsKey)
            .setType(qmdiConfigItem::StringList)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Filenames not to format"))
            .setDescription(tr("Comma separated list (for example: *.md, README.txt)"))
            .setKey(Config::IgnoredFilesKey)
            .setType(qmdiConfigItem::String)
            .build());
}

void CodeFormatPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);

    builtInRegistry.loadFromFile(":indenters.json");
    qDebug() << "CodeFormatPlugin: Loaded built in indenters registry, found"
             << builtInRegistry.count();

    auto dataDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    auto fullFileName = dataDir + QDir::separator() + "indenters.json";
    auto w = new QFileSystemWatcher(this);
    w->addPath(fullFileName);
    w->connect(w, &QFileSystemWatcher::fileChanged, this, [this]() {
        auto dataDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        auto fullFileName = dataDir + QDir::separator() + "indenters.json";
        fullFileName = QDir::toNativeSeparators(fullFileName);

        qDebug() << "Reloading" << fullFileName;
        userRegistry.clear();
        userRegistry.loadFromFile(fullFileName);
        if (userRegistry.count() != 0) {
            qDebug() << "CodeFormatPlugin: Loaded user in indenters registry, found "
                     << userRegistry.count() << "from" << fullFileName;
        } else {
            qDebug() << "CodeFormatPlugin: User registry not found " << fullFileName;
        }
    });

    userRegistry.loadFromFile(fullFileName);
    if (userRegistry.count() != 0) {
        qDebug() << "CodeFormatPlugin: Loaded user in indenters registry, found "
                 << userRegistry.count() << "from" << fullFileName;
    } else {
        qDebug() << "CodeFormatPlugin: User registry not found " << fullFileName;
    }
}

void CodeFormatPlugin::loadConfig(QSettings &settings) { IPlugin::loadConfig(settings); }

void CodeFormatPlugin::saveConfig(QSettings &settings) { IPlugin::saveConfig(settings); }

int CodeFormatPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &) const {
    if (command == GlobalCommands::ReformatCode) {
        return CommandPriority::MediumPriority;
    }
    return CommandPriority::CannotHandle;
}

QFuture<CommandArgs> CodeFormatPlugin::handleCommandAsync(const QString &command,
                                                          const CommandArgs &args) {
    if (command != GlobalCommands::ReformatCode) {
        return {};
    }

    auto ignoreList = getConfig().getIgnoredFiles();
    auto fileName = args[GlobalArguments::FileName].toString();
    auto baseName = QFileInfo(fileName).fileName();
    auto matches = false;

    // Split by ';' or ','
    auto patterns = ignoreList.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
    for (auto &p : patterns) {
        p = p.trimmed();
    }
    for (const auto &pattern : std::as_const(patterns)) {
        auto regex = QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                                        QRegularExpression::CaseInsensitiveOption);
        if (regex.match(baseName).hasMatch()) {
            matches = true;
            break;
        }
    }
    if (matches) {
        qDebug() << "CodeFormatPlugin: filename is flagged, not formatting per request";
        return {};
    }

    auto content = args[GlobalArguments::Content].toString();
    auto indenter = userRegistry.getForFile(fileName);
    if (!indenter) {
        qDebug() << "CodeFormatPlugin: No user indenter - using internal one";
        indenter = builtInRegistry.getForFile(fileName);
    }

    if (!indenter) {
        qDebug() << "CodeFormatPlugin: no formatter for file" << fileName
                 << "suffix:" << QFileInfo(fileName).suffix();
        CommandArgs result;
        result[GlobalArguments::Content] = content;
        result[GlobalArguments::ExitCode] = GlobalResults::NotSupported;
        return QtFuture::makeReadyValueFuture(result);
    }

    return runFormat(fileName, content, indenter);
}

QFuture<CommandArgs> CodeFormatPlugin::runFormat(const QString &fileName, const QString &input,
                                                 const Formatter *indenter) {
    return QtConcurrent::run([this, fileName, input, indenter]() -> CommandArgs {
        QProcess proc;
        QStringList args;
        CommandArgs result;

        result[GlobalArguments::Content] = input;
        result[GlobalArguments::ExitCode] = 0;

        args.reserve(indenter->args.size());
        for (const auto &arg : indenter->args) {
            QString a = arg;
            args.append(a.replace("$filepath", fileName));
        }

        auto paths = getConfig().getExtraPaths();
        paths.append(QString::fromLocal8Bit(qgetenv("PATH")).split(QDir::listSeparator()));
        auto program = QStandardPaths::findExecutable(indenter->binary, paths);

        if (program.isEmpty()) {
            qDebug() << "CodeFormatPlugin: executable not found:" << indenter->binary;
            result[GlobalArguments::ExitCode] = GlobalResults::ExecutableNotFound;
            result[GlobalArguments::ErrorMessage] =
                tr("Executable not found: %1").arg(indenter->binary);
            return result;
        }

        auto fullCommand = program + " " + args.join(" ");
        qDebug() << "CodeFormatPlugin: running" << fullCommand;
        proc.start(program, args);
        if (!proc.waitForStarted()) {
            qDebug() << "CodeFormatPlugin: waitForStarted failed for" << fullCommand;
            result[GlobalArguments::ExitCode] = GlobalResults::ExecutableError;
            result[GlobalArguments::ErrorMessage] = tr("Failed running %1").arg(fullCommand);
            return result;
        }
        if (indenter->processStdin) {
            proc.write(input.toUtf8());
            proc.closeWriteChannel();
        }
        if (!proc.waitForFinished()) {
            qDebug() << "CodeFormatPlugin: waitForFinished failed for" << indenter->binary;
            result[GlobalArguments::ExitCode] = GlobalResults::Crashed;
            result[GlobalArguments::ErrorMessage] = tr("Command crashed %1").arg(fullCommand);
            return result;
        }

        if (proc.exitCode() != 0) {
            auto processStderr = proc.readAllStandardError();
            qDebug() << "CodeFormatPlugin:" << indenter->binary << "code:" << proc.exitCode();
            qDebug() << "CodeFormatPlugin stderr:" << processStderr;
            result[GlobalArguments::ExitCode] = proc.exitCode();
            result[GlobalArguments::ErrorMessage] = processStderr;
            return result;
        }

        if (indenter->processStdout) {
            auto out = proc.readAllStandardOutput();
            if (!out.isEmpty()) {
                result[GlobalArguments::Content] = QString::fromUtf8(out);
                return result;
            } else {
                qDebug() << "CodeFormatPlugin: stdout is empty for" << indenter->binary;
            }
        }
        if (!indenter->processStdout && indenter->tempfile) {
            auto file = QFile(fileName);
            if (file.open(QIODevice::ReadOnly)) {
                result[GlobalArguments::Content] = QString::fromUtf8(file.readAll());
                return result;
            }
        }
        qDebug() << "CodeFormatPlugin: fallthrough for" << indenter->binary
                 << "returning original string";
        result[GlobalArguments::ExitCode] = -3;
        return result;
    });
}
