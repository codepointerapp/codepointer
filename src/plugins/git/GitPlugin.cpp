#include <QCheckBox>
#include <QDebug>
#include <QDockWidget>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStringListModel>
#include <QTimer>

#include "CommitDelegate.hpp"
#include "CommitModel.hpp"
#include "GitPlugin.hpp"
#include "GlobalCommands.hpp"
#include "iplugin.h"
#include "plugins/git/CreateGitBranch.hpp"
#include "ui_GitCommands.h"
#include "ui_GitCommit.h"
#include "widgets/AutoShrinkLabel.hpp"
#include "widgets/qmdieditor.h"

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
    gitBinary =  R"(C:\Program Files\Git\bin\git.exe)";
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
    stash->setToolTip(tr("tash away changes to dirty working directory"));
    branches->setToolTip(tr("List, create, or delete branches"));

    connect(logFile, &QAction::triggered, this, &GitPlugin::logFileHandler);
    connect(logProject, &QAction::triggered, this, &GitPlugin::logProjectHandler);
    connect(diffFile, &QAction::triggered, this, &GitPlugin::diffFileHandler);
    connect(revert, &QAction::triggered, this, &GitPlugin::revertFileHandler);

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
    auto filename = client->mdiClientFileName();
    auto repoRoot = QFileInfo(filename).absolutePath();
    auto const diff = getDiff(filename);
    if (diff.isEmpty()) {
        return;
    }
    auto position = manager->getMdiServer()->getClientIndex(client);
    repoRoot = detectRepoRoot(repoRoot);
    CommandArgs args = {
        {GlobalArguments::FileName, QString("%1.diff").arg(client->mdiClientName)},
        {GlobalArguments::Content, diff},
        {GlobalArguments::ReadOnly, true},
        {GlobalArguments::Position, position},
        {GlobalArguments::SourceDirectory, repoRoot},
    };
    manager->handleCommandAsync(GlobalCommands::DisplayText, args);
}

void GitPlugin::revertFileHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    auto filename = client->mdiClientFileName();
    auto const diff = getDiff(filename);
    if (diff.isEmpty()) {
        return;
    }

    QMessageBox msgBox(QMessageBox::Warning, client->mdiClientName,
                       tr("Do you want to revert %1?\n").arg(client->mdiClientName),
                       QMessageBox::Yes | QMessageBox::Default, manager);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    auto ret = msgBox.exec();
    if (ret != QMessageBox::Yes) {
        qDebug() << "Not reverting";
        return;
    }
    auto args = QStringList{"restore", client->mdiClientFileName()};
    auto output = runGit(args);
    if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
        editor->loadFile(filename);
        editor->loadContent(false);
    }
}

void GitPlugin::refreshBranchesHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    auto filename = client->mdiClientFileName();
    auto repoRoot = getConfig().getGitLastDir();
    auto output = runGit({"-C", repoRoot, "branch", "-a"});
    auto branches = output.split('\n', Qt::SkipEmptyParts);
    form->branchListCombo->clear();
    auto activeIndex = -1;
    auto delegate = static_cast<BoldItemDelegate *>(form->branchListCombo->itemDelegate());
    for (auto const &line : branches) {
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
}

void GitPlugin::diffBranchHandler() {
    auto manager = getManager();
    auto client = manager->getMdiServer()->getCurrentClient();
    auto filename = client->mdiClientFileName();
    auto repoRoot = QFileInfo(filename).absolutePath();
    auto branch = form->branchListCombo->currentText();
    auto diff = runGit({"diff", branch});
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
    manager->handleCommandAsync(GlobalCommands::DisplayText, args);
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
        auto res = runGit(args);
        form->gitOutput->setText(res);
        form->gitOutput->setToolTip(res);
        refreshBranchesHandler();
    }
}

void GitPlugin::logHandler(GitLog log, const QString &filename) {
    auto repoRoot = QFileInfo(filename).absolutePath();
    repoRoot = detectRepoRoot(repoRoot);
    if (repoRoot.isEmpty()) {
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
    auto model = new CommitModel(this);
    form->label->setText(labelText);

    getConfig().setGitLastDir(repoRoot);
    getConfig().setGitLastCommand(args.join(" "));

    auto output = runGit(args);
    model->setContent(output);
    form->listView->setModel(model);
    gitDock->raise();
    gitDock->show();
}

void GitPlugin::on_gitCommitClicked(const QModelIndex &mi) {
    auto const *model = static_cast<CommitModel *>(form->listView->model());
    auto const sha1 = model->data(mi, CommitModel::Roles::HashRole).toString();
    getConfig().setGitLastActiveItem(sha1);
    getManager()->saveSettings();

    auto const sha1Short = shortGitSha1(sha1);
    auto rawCommit = getRawCommit(sha1);
    auto const fullCommit = FullCommitInfo::parse(rawCommit);
    auto widget = static_cast<GitCommitDisplay *>(form->container->widget(0));

    if (!widget) {
        widget = new GitCommitDisplay(form->container);
        form->container->addWidget(widget);
        connect(widget->ui.commits, &QAbstractItemView::doubleClicked, this,
                [this, widget](const QModelIndex &i) {
                    auto manager = getManager();
                    auto filename = i.data().toString();
                    auto diff = runGit({"-C", getConfig().getGitLastDir(), "show",
                                        widget->currentSha1, "--", filename});
                    auto shortSha1 = shortGitSha1(widget->currentSha1);
                    auto displayName = QString("%1-%2.diff").arg(shortSha1).arg(filename);
                    CommandArgs args = {
                        {GlobalArguments::FileName, displayName},
                        {GlobalArguments::Content, diff},
                        {GlobalArguments::ReadOnly, true},
                        {GlobalArguments::SourceDirectory, getConfig().getGitLastDir()},
                    };
                    manager->handleCommandAsync(GlobalCommands::DisplayText, args);
                });
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
}

void GitPlugin::on_gitCommitDoubleClicked(const QModelIndex &mi) {
    auto const *model = static_cast<CommitModel *>(form->listView->model());
    auto const sha1 = model->data(mi, CommitModel::Roles::HashRole).toString();
    auto const rawCommit = getRawCommit(sha1);
    auto const fullCommit = FullCommitInfo::parse(rawCommit);
    auto manager = getManager();
    CommandArgs args = {
        {GlobalArguments::FileName, QString("%1.diff").arg(shortGitSha1(sha1))},
        {GlobalArguments::Content, *fullCommit.raw},
        {GlobalArguments::ReadOnly, true},
        {GlobalArguments::FoldTopLevel, true},
        {GlobalArguments::SourceDirectory, getConfig().getGitLastDir()},
    };
    manager->handleCommandAsync(GlobalCommands::DisplayText, args);
}

QString GitPlugin::runGit(const QStringList &args) {
    // qDebug() << "git " << args.join(" ");
    QProcess p;
    p.setProcessChannelMode(QProcess::ProcessChannelMode::MergedChannels);
    p.start(gitBinary, args);
    p.waitForFinished();
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString GitPlugin::detectRepoRoot(const QString &filePath) {
    QProcess p;
    p.setWorkingDirectory(QFileInfo(filePath).absolutePath());
    p.start(gitBinary, {"rev-parse", "--show-toplevel"});
    p.waitForFinished();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString GitPlugin::getDiff(const QString &path) {
    auto fi = QFileInfo(path);
    return runGit({"-C", fi.absolutePath(), "diff"});
}

QString GitPlugin::getRawCommit(const QString &sha1) {
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
    auto model = new CommitModel(this);
    form->label->setText(cmd);
    auto output = runGit(args);
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
    QTimer::singleShot(0, this, &GitPlugin::refreshBranchesHandler);
}
