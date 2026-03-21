#include <QCheckBox>
#include <QDebug>
#include <QDockWidget>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFuture>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStringListModel>
#include <QTimer>
#include <QtConcurrent>

#include <iplugin.h>

#include "GlobalCommands.hpp"
#include "ui_GitCommands.h"
#include "ui_GitCommit.h"
#include "widgets/AutoShrinkLabel.hpp"
#include "widgets/BoldItemDelegate.hpp"

#include "plugins/git/CommitDelegate.hpp"
#include "plugins/git/CommitForm.hpp"
#include "plugins/git/CommitModel.hpp"
#include "plugins/git/CreateGitBranch.hpp"
#include "plugins/git/GitPlugin.hpp"

QString shortGitSha1(const QString &fullSha1, int length = 7) {
    if (length <= 0) {
        return QString();
    }

    if (fullSha1.size() <= length) {
        return fullSha1;
    }

    return fullSha1.left(length);
}

class GitCommitDisplay : public QWidget {
  public:
    explicit GitCommitDisplay(QWidget *parent) : QWidget(parent) { ui.setupUi(this); }
    Ui::GitCommit ui;
    QString currentSha1;
};

GitPlugin::GitPlugin() {
    name = tr("git scm support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

// TODO: find git in a more proper way
#if defined(Q_OS_WINDOWS)
    auto label = tr("git exe");
    gitBinary = R"(C:\Program Files\Git\bin\git.exe)";
#else
    gitBinary = "git";
    auto label = tr("git binary");
#endif

    config.pluginName = tr("git");
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(label)
                                     .setDescription(tr("Where is git installed"))
                                     .setKey(Config::GitBinaryKey)
                                     .setType(qmdiConfigItem::Path)
                                     .setDefaultValue(gitBinary)
                                     .setPossibleValue(true) // Must be an existing file
                                     .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("<a href='https://git-scm.com/' >Visit git home page</a>"))
            .setKey(Config::GitHomepageKey)
            .setType(qmdiConfigItem::Label)
            .build());

    // Save and restore the last git command
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::GitLastCommandKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue(QString())
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::GitLastDirKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue(QString())
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::GitLastActiveItemKey)
                                     .setType(qmdiConfigItem::String)
                                     .setDefaultValue(QString())
                                     .setUserEditable(false)
                                     .build());
}

GitPlugin::~GitPlugin() {}

void GitPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);

    diffFile = new QAction(tr("git diff current file"), this);
    logFile = new QAction(tr("git log current file"), this);
    logProject = new QAction(tr("git log project/dir"), this);
    revert = new QAction(tr("git revert"), this);
    commit = new QAction(tr("git commit"), this);
    stash = new QAction(tr("git stash"), this);
    branches = new QAction(tr("git branch"), this);

    diffFile->setToolTip(tr("git: Show changes (current file)"));
    diffFile->setShortcut(QKeySequence("Ctrl+G, D"));
    logFile->setToolTip(tr("Show commits (current file)"));
    logFile->setShortcut(QKeySequence("Ctrl+G, F"));
    logProject->setToolTip(tr("Show commits (current project)"));
    logProject->setShortcut(QKeySequence("Ctrl+G, L"));
    revert->setToolTip(tr("Revert existing commits"));
    revert->setShortcut(QKeySequence("Ctrl+G, U"));
    commit->setToolTip(tr("Record changes to the repository"));
    commit->setShortcut(QKeySequence("Ctrl+G, C"));
    stash->setToolTip(tr("tash away changes to dirty working directory"));
    branches->setToolTip(tr("List, create, or delete branches"));

    connect(logFile, &QAction::triggered, this, &GitPlugin::logFileHandler);
    connect(logProject, &QAction::triggered, this, &GitPlugin::logProjectHandler);
    connect(diffFile, &QAction::triggered, this, &GitPlugin::diffFileHandler);
    connect(revert, &QAction::triggered, this, &GitPlugin::revertFileHandler);
    connect(commit, &QAction::triggered, this, &GitPlugin::commitHandler);

    auto menuName = "&Git";
    host->menus.addActionGroup(menuName, "&Project");
    menus[menuName]->addAction(diffFile);
    menus[menuName]->addAction(logFile);
    menus[menuName]->addAction(logProject);
    menus[menuName]->addAction(revert);
    menus[menuName]->addAction(commit);
    menus[menuName]->addAction(stash);
    menus[menuName]->addAction(branches);

    auto manager = dynamic_cast<PluginManager *>(host);
    auto w = new QWidget;
    form = new Ui::GitCommandsForm();
    form->setupUi(w);
    form->listView->setViewMode(QListView::ListMode);
    form->listView->setAlternatingRowColors(true);
    form->branchListCombo->setItemDelegate(new BoldItemDelegate(form->branchListCombo));
    form->diffBranchButton->setEnabled(false);
    form->newBranchButton->setEnabled(false);
    form->deleteBranchButton->setEnabled(false);

    auto delegate = new CommitDelegate(form->listView);
    form->listView->setItemDelegate(delegate);
    form->listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(form->listView, &QAbstractItemView::clicked, this, &GitPlugin::on_gitCommitClicked);
    connect(form->listView, &QAbstractItemView::doubleClicked, this,
            &GitPlugin::on_gitCommitDoubleClicked);
    connect(form->refreshBranchesButton, &QToolButton::clicked, this,
            &GitPlugin::refreshBranchesHandler);
    connect(form->diffBranchButton, &QPushButton::clicked, this, &GitPlugin::diffBranchHandler);
    connect(form->newBranchButton, &QPushButton::clicked, this, &GitPlugin::newBranchHandler);
    connect(form->deleteBranchButton, &QPushButton::clicked, this, &GitPlugin::deleteBranchHandler);
    form->checkoutBranchButton->setEnabled(false);
    gitDock = manager->createNewPanel(Panels::East, "gitpanel", tr("Git"), w);
}

void GitPlugin::on_client_unmerged(qmdiHost *host) {
    IPlugin::on_client_unmerged(host);
    delete gitDock;
}

void GitPlugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);
    restoreGitLog();
}

int GitPlugin::canOpenFile(const QString &fileName) {
    auto url = QUrl(fileName);
    if (url.scheme().isEmpty()) {
        return 0;
    }
    return url.scheme() == "git" ? 5 : 0;
}

qmdiClient *GitPlugin::openFile(const QString &fileName, int, int, int) {
    auto url = QUrl(fileName);
    auto repoDir = url.path();
    auto manager = getManager();
    auto commitForm = new CommitForm(repoDir, this, manager);
    mdiServer->addClient(commitForm);
    return nullptr;
}

void GitPlugin::logFileHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    auto filename = client->mdiClientFileName();
    logHandler(GitLog::File, filename);
}

void GitPlugin::logProjectHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    auto filename = client->mdiClientFileName();
    logHandler(GitLog::Project, filename);
}

void GitPlugin::diffFileHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    if (!client) {
        return;
    }
    auto filename = client->mdiClientFileName();
    auto clientName = client->mdiClientName;
    auto position = manager->getMdiServer()->getClientIndex(client);

    getDiff(filename).then(
        this, [this, filename, clientName, position](const std::tuple<QString, int> &res) {
            auto [diff, exitCode] = res;
            if (exitCode != 0 || diff.isEmpty()) {
                return;
            }

            // Fix compilation under (some?) clang
            auto diffStr = diff;
            detectRepoRoot(filename).then(
                this, [this, clientName, position, diffStr](const std::tuple<QString, int> &res2) {
                    auto [repoRoot, exitCode2] = res2;
                    if (exitCode2 != 0 || repoRoot.isEmpty()) {
                        return;
                    }
                    CommandArgs args = {
                        {GlobalArguments::FileName, QString("%1.diff").arg(clientName)},
                        {GlobalArguments::Content, diffStr},
                        {GlobalArguments::ReadOnly, true},
                        {GlobalArguments::Position, position},
                        {GlobalArguments::SourceDirectory, repoRoot},
                    };
                    getManager()->handleCommandAsync(GlobalCommands::DisplayText, args);
                });
        });
}

void GitPlugin::revertFileHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    if (!client) {
        return;
    }
    auto filename = client->mdiClientFileName();
    auto clientName = client->mdiClientName;

    getDiff(filename).then(this, [this, filename, clientName](const std::tuple<QString, int> &res) {
        auto [diff, exitCode] = res;
        if (exitCode != 0 || diff.isEmpty()) {
            return;
        }

        QMessageBox msgBox(QMessageBox::Warning, clientName,
                           tr("Do you want to revert %1?\n").arg(clientName),
                           QMessageBox::Yes | QMessageBox::Default, getManager());
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        auto ret = msgBox.exec();
        if (ret != QMessageBox::Yes) {
            return;
        }
        auto fi = QFileInfo(filename);
        auto args = QStringList{"-C", fi.absolutePath(), "restore", fi.fileName()};
        runGit(args).then(this, [filename](const std::tuple<QString, int> &res2) {
            auto [output, exitCode] = res2;
            if (exitCode != 0) {
                qDebug() << "Failed restoring" << exitCode << output;
            }
        });
    });
}

void GitPlugin::refreshBranchesHandler() {
    auto repoRoot = getConfig().getGitLastDir();
    if (repoRoot.isEmpty()) {
        return;
    }

    runGit({"-C", repoRoot, "branch", "-a"})
        .then(this, [this](const std::tuple<QString, int> &res) {
            auto [output, exitCode] = res;
            auto branches = output.split('\n', Qt::SkipEmptyParts);
            form->branchListCombo->clear();
            auto activeIndex = -1;
            auto delegate = static_cast<BoldItemDelegate *>(form->branchListCombo->itemDelegate());
            for (auto const &line : std::as_const(branches)) {
                auto isActive = line.startsWith('*');
                auto branchName = line.mid(2).trimmed();
                if (branchName.isEmpty()) {
                    continue;
                }

                form->branchListCombo->addItem(branchName);
                if (isActive) {
                    delegate->boldItemStr = branchName;
                    activeIndex = form->branchListCombo->count() - 1;
                }
            }

            if (activeIndex != -1) {
                form->branchListCombo->setCurrentIndex(activeIndex);
            }
            form->diffBranchButton->setEnabled(true);
            form->newBranchButton->setEnabled(true);
            form->deleteBranchButton->setEnabled(true);
            form->checkoutBranchButton->setEnabled(true);
        });
}

void GitPlugin::diffBranchHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    if (!client) {
        return;
    }
    auto filename = client->mdiClientFileName();
    auto repoRoot = QFileInfo(filename).absolutePath();
    auto branch = form->branchListCombo->currentText();
    runGit({"diff", branch})
        .then(this, [this, branch, repoRoot](const std::tuple<QString, int> &res) {
            auto [diff, exitCode] = res;
            if (diff.isEmpty()) {
                return;
            }

            CommandArgs args = {
                {GlobalArguments::FileName, QString("diff-%1.diff").arg(branch)},
                {GlobalArguments::Content, diff},
                {GlobalArguments::ReadOnly, true},
                {GlobalArguments::FoldTopLevel, true},
                {GlobalArguments::SourceDirectory, repoRoot},
            };
            getManager()->handleCommandAsync(GlobalCommands::DisplayText, args);
        });
}

void GitPlugin::newBranchHandler() {
    auto dialog = new CreateGitBranch(getManager(), this);
    dialog->exec();
}

void GitPlugin::deleteBranchHandler() {
    auto branch = form->branchListCombo->currentText();
    if (branch.isEmpty()) {
        return;
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle("Delete git branch");
    msgBox.setText(tr("Are you sure you want to delete branch?\n%1").arg(branch));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIcon(QMessageBox::Icon::Question);

    auto cb = new QCheckBox(tr("Force delete the branch (-D)"));
    msgBox.setCheckBox(cb);
    auto reply = msgBox.exec();
    if (reply == QMessageBox::Yes) {
        auto deleteBranchArg = cb->isChecked() ? "-D" : "-d";
        auto args = QStringList{"branch", deleteBranchArg, branch};
        runGit(args).then(this, [this](const std::tuple<QString, int> &res) {
            auto [output, exitCode] = res;
            if (exitCode != 0) {
                // TODO - display this error
                qDebug() << "Command failed. Error" << exitCode << output;
                return;
            }
            form->gitOutput->setText(output);
            form->gitOutput->setToolTip(output);
            refreshBranchesHandler();
        });
    }
}

void GitPlugin::commitHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    if (!client) {
        return;
    }
    auto filename = client->mdiClientFileName();
    if (filename.isEmpty()) {
        // TODO - query the current project and use it for commits
        qDebug() << "Cannot commit on an empty file" << filename;
        return;
    }

    detectRepoRoot(filename).then(this, [this, manager](const std::tuple<QString, int> &res) {
        auto [repoRoot, exitCode] = res;
        if (exitCode != 0 || repoRoot.isEmpty()) {
            qDebug() << "Filename is not in any git repo";
            return;
        }
        auto commitForm = new CommitForm(repoRoot, this, manager);
        mdiServer->addClient(commitForm);
    });
}

void GitPlugin::commitDisplayHandler(const QModelIndex &mi) {
    auto widget = static_cast<GitCommitDisplay *>(form->container->widget(0));
    auto manager = getManager();
    auto filename = mi.data().toString();
    auto sha1 = widget->currentSha1;
    auto lastDir = getConfig().getGitLastDir();

    runGit({"-C", lastDir, "show", sha1, "--", filename})
        .then(this, [manager, filename, sha1, lastDir](const std::tuple<QString, int> &res) {
            auto [diff, exitCode] = res;
            if (exitCode != 0) {
                // TODO display this error
                return;
            }
            auto shortSha1 = shortGitSha1(sha1);
            auto displayName = QString("%1-%2.diff").arg(shortSha1, filename);
            CommandArgs args = {
                {GlobalArguments::FileName, displayName},
                {GlobalArguments::Content, diff},
                {GlobalArguments::ReadOnly, true},
                {GlobalArguments::SourceDirectory, lastDir},
            };
            manager->handleCommandAsync(GlobalCommands::DisplayText, args);
        });
}

void GitPlugin::logHandler(GitLog log, const QString &filename) {
    detectRepoRoot(filename).then(this, [this, log, filename](const std::tuple<QString, int> &res) {
        auto [repoRoot, exitCode] = res;
        if (exitCode != 0 || repoRoot.isEmpty()) {
            form->label->setText(tr("No commits or not a git repo"));
            form->diffBranchButton->setEnabled(true);
            form->newBranchButton->setEnabled(true);
            form->deleteBranchButton->setEnabled(true);
            return;
        }

        auto args = QStringList{"-C", repoRoot, "log", "--graph",
                                "--pretty=format:%x01%H%x02%P%x02%an%x02%ai%x02%s"};
        auto labelText = QString();
        switch (log) {
        case GitPlugin::GitLog::File:
            labelText = QString("git log %1").arg(filename);
            if (!filename.isEmpty()) {
                args << "--" << filename;
            }
            break;
        case GitPlugin::GitLog::Project:
            labelText = QString("git log (repo)");
            break;
        }

        getConfig().setGitLastDir(repoRoot);
        form->label->setText(labelText);
        getConfig().setGitLastCommand(args.join(" "));

        runGit(args).then(this, [this](const std::tuple<QString, int> &res) {
            auto [output, exitCode] = res;
            if (exitCode != 0) {
                // ui->commitLogLabel->setText(output);
                return;
            }

            auto model = new CommitModel(this);
            model->setContent(output);
            form->listView->setModel(model);
            gitDock->raise();
            gitDock->show();
        });
    });
}

void GitPlugin::on_gitCommitClicked(const QModelIndex &mi) {
    auto const *model = static_cast<CommitModel *>(form->listView->model());
    auto const sha1 = model->data(mi, CommitModel::Roles::HashRole).toString();
    getConfig().setGitLastActiveItem(sha1);
    getManager()->saveSettings();

    auto const sha1Short = shortGitSha1(sha1);
    getRawCommit(sha1).then(this, [this, sha1, sha1Short](const std::tuple<QString, int> &res) {
        auto [rawCommit, exitCode] = res;
        if (exitCode != 0) {
            return;
        }
        auto const fullCommit = FullCommitInfo::parse(rawCommit);
        auto widget = static_cast<GitCommitDisplay *>(form->container->widget(0));
        if (!widget) {
            widget = new GitCommitDisplay(form->container);
            form->container->addWidget(widget);
            connect(widget->ui.commits, &QAbstractItemView::doubleClicked, this,
                    &GitPlugin::commitDisplayHandler);
        }

        widget->currentSha1 = sha1;
        widget->ui.sha1->setPrimaryText(sha1);
        widget->ui.sha1->setFallbackText(sha1Short);
        widget->ui.commiter->setText(fullCommit.author.toString());
        widget->ui.commitDate->setText(fullCommit.date.toString());
        widget->ui.commit->setText(fullCommit.subject.toString());
        widget->ui.commitMessage->setVisible(!fullCommit.body.trimmed().isEmpty());
        widget->ui.commitMessage->setMarkdown(fullCommit.body);

        auto s = QStringList();
        for (auto ss : fullCommit.files) {
            s.push_back(ss.filename.toString());
        }
        widget->ui.commits->setModel(new QStringListModel(s));
        widget->ui.commits->setEditTriggers(QAbstractItemView::NoEditTriggers);
    });
}

void GitPlugin::on_gitCommitDoubleClicked(const QModelIndex &mi) {
    auto const *model = static_cast<CommitModel *>(form->listView->model());
    auto const sha1 = model->data(mi, CommitModel::Roles::HashRole).toString();
    auto lastDir = getConfig().getGitLastDir();
    getRawCommit(sha1).then(this, [this, sha1, lastDir](const std::tuple<QString, int> &res) {
        auto [rawCommit, exitCode] = res;
        if (exitCode != 0) {
            return;
        }
        auto const fullCommit = FullCommitInfo::parse(rawCommit);
        auto manager = getManager();
        CommandArgs args = {
            {GlobalArguments::FileName, QString("%1.diff").arg(shortGitSha1(sha1))},
            {GlobalArguments::Content, *fullCommit.raw},
            {GlobalArguments::ReadOnly, true},
            {GlobalArguments::FoldTopLevel, true},
            {GlobalArguments::SourceDirectory, lastDir},
        };
        manager->handleCommandAsync(GlobalCommands::DisplayText, args);
    });
}

QFuture<std::tuple<QString, int>> GitPlugin::runGit(const QStringList &args) {
    return QtConcurrent::run([this, args] {
        QProcess p;
        p.setProcessChannelMode(QProcess::ProcessChannelMode::MergedChannels);
        p.start(gitBinary, args);
        p.waitForFinished();
        return std::make_tuple(QString::fromUtf8(p.readAllStandardOutput()), p.exitCode());
    });
}

QFuture<std::tuple<QString, int>> GitPlugin::detectRepoRoot(const QString &filePath) {
    auto dir = QFileInfo(filePath).absolutePath();
    auto args = QStringList{"-C", dir, "rev-parse", "--show-toplevel"};
    return runGit(args).then([](const std::tuple<QString, int> &res) {
        auto [output, exitCode] = res;
        return std::make_tuple((exitCode == 0) ? output.trimmed() : QString{}, exitCode);
    });
}

QFuture<std::tuple<QString, int>> GitPlugin::getDiff(const QString &path) {
    auto fi = QFileInfo(path);
    return runGit({"-C", fi.absolutePath(), "diff", fi.fileName()});
}

QFuture<std::tuple<QString, int>> GitPlugin::getRawCommit(const QString &sha1) {
    return runGit({"-C", getConfig().getGitLastDir(), "show", sha1});
}

void GitPlugin::restoreGitLog() {
    if (!form) {
        return;
    }

    auto cmd = getConfig().getGitLastCommand();
    if (cmd.isEmpty()) {
        return;
    }

    auto args = cmd.split(" ");
    form->label->setText(cmd);
    runGit(args).then(this, [this](const std::tuple<QString, int> &res) {
        auto [output, exitCode] = res;
        auto model = new CommitModel(this);
        model->setContent(output);
        form->listView->setModel(model);

        auto lastActive = getConfig().getGitLastActiveItem();
        if (!lastActive.isEmpty()) {
            for (int i = 0; i < model->rowCount(); ++i) {
                auto index = model->index(i, 0);
                if (model->data(index, CommitModel::Roles::HashRole).toString() == lastActive) {
                    form->listView->setCurrentIndex(index);
                    QTimer::singleShot(0, this, [this, index] { on_gitCommitClicked(index); });
                    break;
                }
            }
        }
    });
    QTimer::singleShot(0, this, &GitPlugin::refreshBranchesHandler);
}
