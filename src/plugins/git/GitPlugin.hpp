#pragma once

#include "iplugin.h"
#include <tuple>

namespace Ui {
class GitCommandsForm;
}

class GitPlugin : public IPlugin {
    Q_OBJECT
    struct Config {
        CONFIG_DEFINE(GitBinary, QString)
        CONFIG_DEFINE(GitHomepage, QString)
        CONFIG_DEFINE(GitLastCommand, QString)
        CONFIG_DEFINE(GitLastDir, QString)
        CONFIG_DEFINE(GitLastActiveItem, QString)
        qmdiPluginConfig *config;
    };
    Config &getConfig() {
        static Config configObject{&this->config};
        return configObject;
    }

    enum class GitLog { File, Project };

  public:
    GitPlugin();
    ~GitPlugin();

    // IPlugin interface
    virtual void on_client_merged(qmdiHost *host) override;
    virtual void on_client_unmerged(qmdiHost *host) override;
    virtual void loadConfig(QSettings &settings) override;
    virtual int canOpenFile(const QString &fileName) override;
    virtual qmdiClient *openFile(const QString &fileName, int x = -1, int y = -1, int z = -1) override;

  public slots:
    void logFileHandler();
    void logProjectHandler();
    void diffFileHandler();
    void revertFileHandler();
    void refreshBranchesHandler();
    void diffBranchHandler();
    void newBranchHandler();
    void deleteBranchHandler();
    void commitHandler();
    void logHandler(GitPlugin::GitLog log, const QString &filename);
    void on_gitCommitClicked(const QModelIndex &mi);
    void on_gitCommitDoubleClicked(const QModelIndex &mi);

  public slots:
    std::tuple<QString, int> runGit(const QStringList &args);
    QString detectRepoRoot(const QString &path);
    QString getDiff(const QString &path);
    QString getRawCommit(const QString &sha1);
    void restoreGitLog();

  private:
    QAction *diffFile = nullptr;
    QAction *logFile = nullptr;
    QAction *logProject = nullptr;
    QAction *revert = nullptr;
    QAction *commit = nullptr;
    QAction *stash = nullptr;
    QAction *branches = nullptr;
    QString gitBinary = "git";
    QDockWidget *gitDock = nullptr;
    Ui::GitCommandsForm *form = nullptr;
};
