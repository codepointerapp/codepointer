#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "iplugin.h"

struct Formatter {
    QString name;
    QString binary;
    QStringList args;
    QStringList extensions;

    bool stdin;
    bool stdout;
    bool requiresFilepath;
    bool tempfile;

    static Formatter *fromJson(const QJsonObject &obj);
};

class FormatterRegistry {
  public:
    bool loadFromFile(const QString &jsonFile);
    const Formatter *getForFile(const QString &filePath) const;
    int count() const;

  private:
    QVector<Formatter *> m_indenters;
    QHash<QString, const Formatter *> m_extIndex;
};

class CodeFormatPlugin : public IPlugin {
    FormatterRegistry builtInRegistry;

  public:
    CodeFormatPlugin();

    virtual void on_client_merged(qmdiHost *host) override;
    virtual void loadConfig(QSettings &settings) override;
    virtual void saveConfig(QSettings &settings) override;

    virtual int canHandleAsyncCommand(const QString &command,
                                      const CommandArgs &args) const override;
    virtual QFuture<CommandArgs> handleCommandAsync(const QString &command,
                                                    const CommandArgs &args) override;

    QFuture<QString> runFormat(const QString &fileName, const QString &input,
                               const Formatter *indenter);
};
