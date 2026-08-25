#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QThreadPool>

#include "iplugin.h"
#include "pluginmanager.h"

struct Formatter {
    QString name;
    QString binary;
    QStringList args;
    QStringList extensions;

    bool processStdin;
    bool processStdout;
    bool requiresFilepath;
    bool tempfile;

    static Formatter *fromJson(const QJsonObject &obj);
};

class FormatterRegistry {
  public:
    bool loadFromFile(const QString &jsonFile);
    const Formatter *getForFile(const QString &filePath) const;
    int count() const;
    void clear();

  private:
    QVector<Formatter *> m_indenters;
    QHash<QString, const Formatter *> m_extIndex;
};

class CodeFormatPlugin : public IPlugin {
    struct Config {
        CONFIG_DEFINE(ExtraPaths, QStringList)
        CONFIG_DEFINE(IgnoredFiles, QString)
        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }
    FormatterRegistry builtInRegistry;
    FormatterRegistry userRegistry;

    /// Formatting runs an external process and blocks on waitForFinished(). On the
    /// global pool that thread is shared with everything else using QtConcurrent -
    /// notably TreeSitter, which dispatches a query per keystroke and serialises
    /// them on one engine mutex. Saturate the global pool and the format task is
    /// merely *queued*: it never starts, nothing is logged, and the failure looks
    /// transient and unexplainable. Its own pool cannot be starved by others.
    QThreadPool formatPool;

  public:
    CodeFormatPlugin();

    virtual void on_client_merged(qmdiHost *host) override;
    virtual void loadConfig(QSettings &settings) override;
    virtual void saveConfig(QSettings &settings) override;

    virtual int canHandleAsyncCommand(const QString &command,
                                      const CommandArgs &args) const override;
    virtual QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                                    const CommandArgs &args) override;

    QFuture<CommandArgs> runFormat(const QString &fileName, const QString &input,
                                   const Formatter *indenter);
};
