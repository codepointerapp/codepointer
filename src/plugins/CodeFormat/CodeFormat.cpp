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
        return false;
    }

    auto data = f.readAll();
    auto doc = QJsonDocument::fromJson(data);

    if (!doc.isArray()) {
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
    qDebug() << "CodeFormatPlugin: Loaded built in indenters registry, found "
             << builtInRegistry.count();

    // runFormat(fname, cpp, ind).then(this, [](QString s) { qDebug() << "Aftger cleanup" << s; });
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

    return runFormat(fileName, content, indenter)
        .then(this, [args](const QString &formattedContent) mutable {
            CommandArgs out = args;
            out[GlobalArguments::Content] = formattedContent;
            return out;
        });
}

QFuture<QString> CodeFormatPlugin::runFormat(const QString &fileName, const QString &input,
                                             const Formatter *indenter) {
    return QtConcurrent::run([fileName, input, indenter]() -> QString {
        QProcess proc;
        QStringList args;

        args.reserve(indenter->args.size());
        for (auto arg : indenter->args) {
            args.append(arg.replace("$filepath", fileName));
        }
        proc.start(indenter->binary, args);
        if (!proc.waitForStarted()) {
            qDebug() << "--------" << "waitForStarted failed";
            return input;
        }

        if (indenter->stdin) {
            proc.write(input.toUtf8());
            proc.closeWriteChannel();
        }
        if (!proc.waitForFinished()) {
            qDebug() << "--------" << "waitForFinished failed";
            return input;
        }
        if (indenter->stdout) {
            QByteArray out = proc.readAllStandardOutput();
            if (!out.isEmpty()) {
                qDebug() << "--------" << "READING FROM STDOUT";
                return QString::fromUtf8(out);
            }
        }
        if (!indenter->stdout && indenter->tempfile) {
            auto file = QFile(fileName);
            if (file.open(QIODevice::ReadOnly)) {
                qDebug() << "--------" << "READING FROM FILE";
                return QString::fromUtf8(file.readAll());
            }
        }

        qDebug() << "--------" << "DAFUC";
        return input;
    });
}
