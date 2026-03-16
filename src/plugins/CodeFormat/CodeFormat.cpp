#include "CodeFormat.hpp"
#include "GlobalCommands.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QtConcurrent>

Formatter *Formatter::fromJson(const QJsonObject &obj) {
    const QVariantMap m = obj.toVariantMap();
    Formatter *f = new Formatter;

    f->name = m.value("name").toString();
    f->binary = m.value("binary").toString();
    f->stdin = m.value("stdin", false).toBool();
    f->stdout = m.value("stdout", false).toBool();
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
    for (const auto &v : arr) {
        if (!v.isObject()) {
            continue;
        }

        auto t = Formatter::fromJson(v.toObject());
        m_indenters.append(t);
        for (auto const &ext : t->extensions) {
            m_extIndex.insert(ext.toLower(), t);
        }
    }

    return true;
}

const Formatter *FormatterRegistry::getForFile(const QString &filePath) const {
    auto info = QFileInfo(filePath);
    auto ext = info.suffix().toLower();
    if (!m_extIndex.contains(ext)) {
        qDebug() << "FormatterRegistry: no indenter found for suffix" << ext << "of" << filePath << "m_extIndex keys:" << m_extIndex.keys();
        return nullptr;
    }
    auto lll = m_extIndex[ext];
    return lll;
}

int FormatterRegistry::count() const { return m_indenters.size(); }

CodeFormatPlugin::CodeFormatPlugin() {
    name = tr("Code format support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
}

void CodeFormatPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);

    builtInRegistry.loadFromFile(":indenters.json");
    qDebug() << "CodeFormatPlugin: Loaded built in indenters registry, found ";
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

    auto fileName = args[GlobalArguments::FileName].toString();
    auto content = args[GlobalArguments::Content].toString();
    auto indenter = builtInRegistry.getForFile(fileName);

    if (!indenter) {
        qDebug() << "CodeFormatPlugin: no formatter for file" << fileName << "suffix:" << QFileInfo(fileName).suffix();
        return QtFuture::makeReadyValueFuture(args);
    }

    return runFormat(fileName, content, indenter)
        .then(this, [args](const QString &formattedContent) mutable {
            CommandArgs out = args;
            out[GlobalArguments::Content] = formattedContent;
            return out;
        });
}

// FIXME: report errors upstream, both exit code and output.
//        why? I case a user wants to define its own indenter - he would like to know why it fails
//        and what the indenter spit into stderr for example.
QFuture<QString> CodeFormatPlugin::runFormat(const QString &fileName, const QString &input,
                                             const Formatter *indenter) {
    return QtConcurrent::run([fileName, input, indenter]() -> QString {
        QProcess proc;
        QStringList args;

        args.reserve(indenter->args.size());
        for (auto arg : indenter->args) {
            args.append(arg.replace("$filepath", fileName));
        }

        qDebug() << "CodeFormatPlugin: running" << indenter->binary << args.join(" ");
        proc.start(indenter->binary, args);
        if (!proc.waitForStarted()) {
            qDebug() << "CodeFormatPlugin: waitForStarted failed for" << indenter->binary;
            return input;
        }
        if (indenter->stdin) {
            proc.write(input.toUtf8());
            proc.closeWriteChannel();
        }
        if (!proc.waitForFinished()) {
            qDebug() << "CodeFormatPlugin: waitForFinished failed for" << indenter->binary;
            return input;
        }
        
        if (proc.exitCode() != 0) {
            qDebug() << "CodeFormatPlugin:" << indenter->binary << "exited with code" << proc.exitCode();
            qDebug() << "CodeFormatPlugin stderr:" << proc.readAllStandardError();
            // If it failed, we probably want to return the original input
            return input;
        }

        if (indenter->stdout) {
            QByteArray out = proc.readAllStandardOutput();
            if (!out.isEmpty()) {
                return QString::fromUtf8(out);
            } else {
                qDebug() << "CodeFormatPlugin: stdout is empty for" << indenter->binary;
            }
        }
        if (!indenter->stdout && indenter->tempfile) {
            auto file = QFile(fileName);
            if (file.open(QIODevice::ReadOnly)) {
                return QString::fromUtf8(file.readAll());
            }
        }
        qDebug() << "CodeFormatPlugin: fallthrough for" << indenter->binary << "returning original string";
        return input;
    });
}
