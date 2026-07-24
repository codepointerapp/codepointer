#include <QAbstractButton>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QTemporaryFile>
#include <QTextStream>
#include <QToolTip>
#include <QWidget>
#include <QtAlgorithms>
#include <QtConcurrent>

#include "CommitForm.hpp"
#include "GitPlugin.hpp"
#include "GlobalCommands.hpp"
#include "plugins/texteditor/texteditor_plg.h"
#include "ui_CommitForm.h"
#include "widgets/qmdieditor.h"

class PathElideDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideLeft, opt.rect.width() - 10);
        opt.textElideMode = Qt::ElideNone;
        auto *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }
};

struct GitStatusEntry {
    QString filename;
    GitFileStatus status;
    bool staged = false;
    bool unstaged = false;
    bool checked = false;
};

static auto parseGitStatus(QStringView statusOutput) -> QList<GitStatusEntry> {
    auto out = QList<GitStatusEntry>();
    auto lines = statusOutput.split('\n', Qt::SkipEmptyParts);
    for (auto line : std::as_const(lines)) {
        if (line.size() < 3) {
            continue;
        }
        auto x = line[0];
        auto y = line[1];
        auto status = (x == '?' && y == '?')   ? GitFileStatus::Untracked
                      : (x == 'A' || y == 'A') ? GitFileStatus::Added
                      : (x == 'M' || y == 'M') ? GitFileStatus::Modified
                      : (x == 'D' || y == 'D') ? GitFileStatus::Deleted
                      : (x == 'R' || y == 'R') ? GitFileStatus::Renamed
                      : (x == 'C' || y == 'C') ? GitFileStatus::Copied
                                               : GitFileStatus::Unknown;
        // x is the index/staged column, y is the worktree column; anything but ' ' or '?'
        // means a change is present there. A file can be staged and further modified
        // afterwards, in which case both are set (e.g. "MM", "AM").
        auto staged = x != ' ' && x != '?';
        auto unstaged = y != ' ' && y != '?';
        out.append({line.mid(3).trimmed().toString(), status, staged, unstaged});
    }
    return out;
}

static auto createTempFileWithContent(const QString &content) -> QString {
    auto file = QTemporaryFile();
    // keep file after destruction
    file.setAutoRemove(false);
    if (!file.open()) {
        return {};
    }
    auto out = QTextStream(&file);
    out << content;
    file.close();
    return file.fileName();
}

auto statusToText(GitFileStatus status) -> QString {
    switch (status) {
    case GitFileStatus::Modified:
        return QStringLiteral("Modified");
    case GitFileStatus::Added:
        return QStringLiteral("Added");
    case GitFileStatus::Deleted:
        return QStringLiteral("Deleted");
    case GitFileStatus::Renamed:
        return QStringLiteral("Renamed");
    case GitFileStatus::Copied:
        return QStringLiteral("Copied");
    case GitFileStatus::Untracked:
        return QStringLiteral("Untracked");
    default:
        return QStringLiteral("Unknown");
    }
}

class GitStatusTableModel final : public QAbstractTableModel {
    // Q_OBJECT

  public:
    enum Roles { StatusRole = Qt::UserRole + 1 };
    explicit GitStatusTableModel(QObject *parent = nullptr);

    // Re-implementation rom QAbstractTableModel
    auto rowCount(const QModelIndex &parent = {}) const -> int override;
    auto columnCount(const QModelIndex &parent = {}) const -> int override;
    auto data(const QModelIndex &index, int role) const -> QVariant override;
    auto setData(const QModelIndex &index, const QVariant &value, int role) -> bool override;
    auto flags(const QModelIndex &index) const -> Qt::ItemFlags override;
    auto headerData(int section, Qt::Orientation orientation, int role) const -> QVariant override;

    // Public API
    auto setEntries(QList<GitStatusEntry> entries) -> void;
    auto checkedEntries() const -> QList<GitStatusEntry>;
    auto setAllChecked(bool checked) -> void;
    auto hasAnyChecked() const -> bool;

  private:
    QList<GitStatusEntry> m_entries;
};

GitStatusTableModel::GitStatusTableModel(QObject *parent) : QAbstractTableModel(parent) {}

auto GitStatusTableModel::rowCount(const QModelIndex &parent) const -> int {
    return parent.isValid() ? 0 : m_entries.size();
}

auto GitStatusTableModel::columnCount(const QModelIndex &) const -> int {
    // checkbox | filename | status
    return 3;
}

auto GitStatusTableModel::data(const QModelIndex &index, int role) const -> QVariant {
    if (!index.isValid()) {
        return {};
    }
    auto const &e = m_entries.at(index.row());
    switch (role) {
    case StatusRole:
        return static_cast<int>(e.status);
    case Qt::CheckStateRole:
        if (index.column() == 0) {
            return e.checked ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    case Qt::DisplayRole:
        switch (index.column()) {
        case 1:
            if (e.staged && e.unstaged) {
                return tr("Unstaged");
            }
            if (e.staged) {
                return tr("Staged");
            }
            return statusToText(e.status);
        case 2:
            return e.filename;
        }
        return {};
    case Qt::ToolTipRole:
        return e.filename;
    default:
        break;
    }
    return {};
}

auto GitStatusTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
    -> bool {
    if (!index.isValid()) {
        return false;
    }

    if (index.column() == 0 && role == Qt::CheckStateRole) {
        auto &e = m_entries[index.row()];
        e.checked = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    return false;
}

auto GitStatusTableModel::flags(const QModelIndex &index) const -> Qt::ItemFlags {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    auto f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (index.column() == 0) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

auto GitStatusTableModel::headerData(int section, Qt::Orientation orientation, int role) const
    -> QVariant {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0:
        return tr("Commit");
    case 1:
        return tr("Status");
    case 2:
        return tr("File");
    default:
        return {};
    }
}

auto GitStatusTableModel::setEntries(QList<GitStatusEntry> entries) -> void {
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

auto GitStatusTableModel::checkedEntries() const -> QList<GitStatusEntry> {
    QList<GitStatusEntry> out;
    for (const auto &e : m_entries) {
        if (e.checked) {
            out.append(e);
        }
    }
    return out;
}

void GitStatusTableModel::setAllChecked(bool checked) {
    if (m_entries.isEmpty()) {
        return;
    }
    for (auto &e : m_entries) {
        e.checked = checked;
    }
    auto topLeft = index(0, 0);
    auto bottomRight = index(rowCount() - 1, 0);
    emit dataChanged(topLeft, bottomRight, {Qt::CheckStateRole});
}

bool GitStatusTableModel::hasAnyChecked() const {
    for (const auto &e : m_entries) {
        if (e.checked) {
            return true;
        }
    }
    return false;
}

CommitForm::CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent)
    : QWidget(parent), ui(new Ui::CommitForm) {
    ui->setupUi(this);
    mdiClientName = tr("Commit");
    repoRoot = dir;
    git = plugin;

    model = new GitStatusTableModel(ui->tableView);
    // The reason for injecting the UI with a widget at runtime, is that the code
    // generated by qtcreator (qtdesigner...) does not allow to setup the widget
    // with another "system" - it just `new`s it.
    //
    // I am unhappy about the injection of the editor (`ui->diffPreview = e->getEditor()`)
    // but for now this is good enough.
    {
        auto replaceWidget = [](QWidget *oldWidget, QWidget *newWidget) {
            if (!oldWidget || !newWidget) {
                return;
            }

            if (auto layout = oldWidget->parentWidget()->layout()) {
                if (auto box = qobject_cast<QBoxLayout *>(layout)) {
                    auto idx = box->indexOf(oldWidget);
                    box->removeWidget(oldWidget);
                    box->insertWidget(idx, newWidget);
                } else {
                    layout->replaceWidget(oldWidget, newWidget);
                }
            }

            oldWidget->deleteLater();
        };

        auto manager = git->getManager();
        auto editorPlugin = manager->findPlugin("TextEditorPlugin");
        if (auto p = dynamic_cast<TextEditorPlugin *>(editorPlugin)) {

            auto client = p->fileNewEditor();
            client->mdiServer = git->mdiServer;
            if (auto e = dynamic_cast<qmdiEditor *>(client)) {
                e->setLineNumbersVisible(false);
                e->setReadOnly(true);
                e->setMinimapVisible(false);
                e->setHighlighter("diff.xml");
                e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

                replaceWidget(ui->diffPreview, e);
                ui->diffPreview = e->getEditor();
            }

            auto client2 = p->fileNewEditor();
            client2->mdiServer = git->mdiServer;
            if (auto e = dynamic_cast<qmdiEditor *>(client2)) {
                e->setLineNumbersVisible(false);
                e->setMinimapVisible(false);
                e->setHighlighter("markdown.xml");
                e->setIndentAlgorithm(Qutepart::INDENT_ALG_MARKDOWN);
                e->setDrawSolidEdge(true);
                e->setLineLengthEdge(72);
                e->setSoftLineWrapping(true);
                e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

                // FIXME: qutepart implementation leaking.
                if (auto textEditor = qobject_cast<Qutepart::Qutepart *>(e->getEditor())) {
                    textEditor->setCompletionCallback(
                        [this](const QString &prefix, const QString &,
                               const QString &) -> QFuture<QSet<Qutepart::CompletionItem>> {
                            auto items = QSet<Qutepart::CompletionItem>();
                            if (!prefix.isEmpty()) {
                                for (auto row = 0; row < model->rowCount(); ++row) {
                                    auto idx = model->index(row, 2);
                                    auto name =
                                        QFileInfo(model->data(idx, Qt::DisplayRole).toString())
                                            .fileName();
                                    if (name.startsWith(prefix, Qt::CaseInsensitive)) {
                                        items.insert(
                                            Qutepart::CompletionItem(name, tr("Changed file")));
                                    }
                                }
                            }
                            return QtFuture::makeReadyValueFuture(items);
                        });
                }

                replaceWidget(ui->commitMessage, e);
                ui->commitMessage = e->getEditor();
            }
        }
    }

    ui->tableView->setModel(model);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setItemDelegateForColumn(2, new PathElideDelegate(this));
    connect(ui->tableView, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (ui->showDiff->isChecked() && index.column() == 2) {
            auto text = index.data(Qt::ToolTipRole).toString();
            QApplication::clipboard()->setText(text);
            QTimer::singleShot(150, this, [this, text]() {
                QToolTip::showText(QCursor::pos(), tr("Copied to clipboard: %1").arg(text),
                                   ui->tableView, {}, 1500);
            });
        } else {
            auto filename = index.data(Qt::ToolTipRole).toString();
            auto clientName = index.data(Qt::DisplayRole).toString() + ".diff";
            auto gitFileStatus =
                static_cast<GitFileStatus>(index.data(GitStatusTableModel::StatusRole).toInt());

            switch (gitFileStatus) {
            case GitFileStatus::Modified:
            case GitFileStatus::Added:
                ui->diffLoading->start();
                ui->revertCurrentButton->setText(tr("Revert current"));
                git->runGit({"-C", repoRoot, "diff", "HEAD", "--", filename})
                    .then(this, [this, clientName](const std::tuple<QString, int> &res) {
                        auto [output, exitCode] = res;
                        ui->diffLoading->stop();
                        if (exitCode != 0) {
                            qDebug()
                                << QString("git - code=%1, output=[%2]").arg(exitCode).arg(output);
                            return;
                        }
                        auto position = -1;
                        CommandArgs args = {
                            {GlobalArguments::FileName, clientName},
                            {GlobalArguments::Content, output},
                            {GlobalArguments::ReadOnly, true},
                            {GlobalArguments::Position, position},
                            {GlobalArguments::SourceDirectory, repoRoot},
                        };
                        git->getManager()->handleCommandAsync(GlobalCommands::DisplayText, args);
                    });

                break;
            default:
                auto ff = repoRoot + QDir::separator() + filename;
                git->getManager()->openFile(ff);
            }
        }
    });

    ui->revertSelectedButton->setEnabled(false);
    ui->commitButton->setEnabled(false);
    ui->commitMessage->setFocusPolicy(Qt::StrongFocus);

    auto *header = ui->tableView->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);

    connect(ui->revertCurrentButton, &QAbstractButton::clicked, this,
            &CommitForm::revertCurrentImpl);
    connect(ui->commitButton, &QAbstractButton::clicked, this, &CommitForm::commitImpl);
    connect(ui->pushButton, &QAbstractButton::clicked, this, &CommitForm::pushImpl);
    connect(ui->revertSelectedButton, &QAbstractButton::clicked, this,
            &CommitForm::revertSelectionImpl);
    connect(ui->amendCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            git->runGit({"-C", repoRoot, "log", "-1", "--pretty=%B"})
                .then(this, [this](const std::tuple<QString, int> &res) {
                    auto [msg, exitCode] = res;
                    if (exitCode == 0) {
                        ui->commitMessage->setPlainText(msg.trimmed());
                    }
                });
        }
        ui->commitButton->setEnabled(checked || !model->checkedEntries().isEmpty());
    });
    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &) {
                if (selected.indexes().size() == 0) {
                    newFileSelected({}, GitFileStatus::Unknown);
                    return;
                }
                auto firstIndex = selected.indexes().first();
                auto idx = model->index(firstIndex.row(), 2);
                auto fileName = model->data(idx, Qt::DisplayRole).toString();
                auto status = static_cast<GitFileStatus>(
                    model->data(idx, GitStatusTableModel::StatusRole).toInt());
                newFileSelected(fileName, status);
            });
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(Qt::CheckStateRole)) {
                    return;
                }

                auto hasSelection = !model->checkedEntries().isEmpty();
                ui->revertSelectedButton->setEnabled(hasSelection);
                ui->commitButton->setEnabled(hasSelection || ui->amendCheckbox->isChecked());
            });
    connect(model, &QAbstractItemModel::modelReset, this,
            [this]() { ui->revertSelectedButton->setEnabled(false); });
    connect(ui->selectAllButton, &QAbstractButton::clicked, this,
            [this]() { model->setAllChecked(true); });
    connect(ui->selectNoneButton, &QAbstractButton::clicked, this, [this]() {
        model->setAllChecked(false);
        ui->revertSelectedButton->setEnabled(false);
    });
    connect(ui->showDiff, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState newState) {
        // widget 2 is the diff viewer
        ui->splitter->widget(2)->setVisible(newState);
    });
    updateGitStatus();
}

CommitForm::~CommitForm() { delete ui; }

QString CommitForm::mdiClientFileName() { return QString("git:%1").arg(repoRoot); }

qmdiClientState CommitForm::getState() const {
    auto c = ui->commitMessage->textCursor();

    auto state = qmdiClientState();
    state["COLUMN"] = c.positionInBlock();
    state["ROW"] = c.blockNumber();
    state["TEXT"] = ui->commitMessage->toPlainText();

    return state;
}

void CommitForm::setState(const qmdiClientState &state) {
    if (state.contains("TEXT")) {
        auto t = state["TEXT"].toString();
        ui->commitMessage->setPlainText(t);
    }

    auto col = 0;
    auto row = 0;
    if (state.contains("COLUMN")) {
        col = state["COLUMN"].toInt();
    }
    if (state.contains("ROW")) {
        row = state["ROW"].toInt();
    }

    auto block = ui->commitMessage->document()->findBlockByNumber(row);
    auto cursor = QTextCursor(block);
    if (col != 0) {
        if (col < 0) {
            col = 0;
        } else if (col > cursor.block().length()) {
            col = cursor.block().length() - 1;
        }
        cursor.setPosition(cursor.position() + col);
    }
    ui->commitMessage->setTextCursor(cursor);
}

bool CommitForm::event(QEvent *e) {
    switch (e->type()) {
    case QEvent::Show:
        updateGitStatus();
        break;
    case QEvent::Resize: {
        auto event = static_cast<QResizeEvent *>(e);
        auto width = event->size().width();
        auto vis = width > 1200;
        ui->showDiff->setChecked(vis);
        break;
    }
    default:
        break;
    }
    return QWidget::event(e);
}

void CommitForm::keyPressEvent(QKeyEvent *event) {
#if 0
    Commenting this out. I am not giving this up, but I think that for now hiding the bottom
    panels is more important.
    if (event->key() == Qt::Key_Escape) {
        if (ui->diffPreview) {
            ui->diffPreview->setFocus(Qt::ShortcutFocusReason);
            event->accept();
            return;
        }
    }
#endif
    QWidget::keyPressEvent(event);
}

void CommitForm::updateGitStatus() {
    git->runGit({"-C", repoRoot, "status", "--porcelain"})
        .then(this, [this](const std::tuple<QString, int> &res) {
            auto [gitOutput, exitCode] = res;
            auto status = parseGitStatus(gitOutput);
            model->setEntries(status);
            if (model->rowCount() > 0) {
                ui->tableView->selectRow(0);
            }
        });
}

void CommitForm::newFileSelected(const QString &filename, GitFileStatus status) {
    if (filename.isEmpty()) {
        ui->diffPreview->setPlainText("");
        return;
    }

    auto highlighter = QString{"diff.xml"};
    auto fullFilePath = repoRoot + "/" + filename;

    auto updateEditor = [this](const QString &output, const QString &highlighter) {
        ui->diffPreview->setPlainText(output);

        // FIXME: this looks way too ugly,
        // Problem - the "editor" is not the correct widget
        // The UI expects a QPlainTextEdit, and we have Widget that includes a
        // QPlainTextEdit.
        if (auto editor = dynamic_cast<qmdiEditor *>(ui->diffPreview->parent()->parent())) {
            // FIXME: Note how we need to hijack the low level APIs of Qutepart
            //        to set the syntax highlighter.
            //        We need better abstractions, some IEditor, which can be an
            //        interface with has implementation as Qutepart of QSCintilla or
            //        something different completely.
            editor->setHighlighter(highlighter);
            editor->updateInternalMappings(repoRoot);
        } else {
            qDebug() << "Double click on diff will not work";
        }
    };

    switch (status) {
    case GitFileStatus::Modified: {
        ui->diffLoading->start();
        ui->diffLabel->setText("git diff HEAD");
        ui->revertCurrentButton->setText(tr("Revert current"));
        git->runGit({"-C", repoRoot, "diff", "HEAD", "--", filename})
            .then(this, [this, updateEditor](const std::tuple<QString, int> &res) {
                auto [output2, exitCode] = res;
                ui->diffLoading->stop();
                if (exitCode != 0) {
                    qDebug() << QString("git - code=%1, output=[%2]").arg(exitCode).arg(output2);
                    ui->commitLogLabel->setText("");
                    updateEditor("", "diff.xml");
                    return;
                }
                updateEditor(output2, "diff.xml");
            });
        return;
    }
    case GitFileStatus::Deleted:
        ui->diffLabel->setText(tr("Deleted"));
        ui->revertCurrentButton->setText(tr("Revert current"));
        updateEditor("", highlighter);
        break;
    case GitFileStatus::Added:
    case GitFileStatus::Renamed:
    case GitFileStatus::Copied:
    case GitFileStatus::Untracked: {
        ui->revertCurrentButton->setText(tr("Delete"));
        ui->diffLabel->setText(tr("Content"));

        auto manager = git->getManager();
        auto plugin = manager->findPlugin("TextEditorPlugin");
        auto p = dynamic_cast<TextEditorPlugin *>(plugin);
        if (!p) {
            qDebug() << "Cannot find the text editor plugin";
            break;
        }
        auto score = p->canOpenFile(fullFilePath);
        if (score == 1) {
            auto langInfo = ::Qutepart::chooseLanguage({}, {}, "filename.txt");
            updateEditor(tr("Not a text file"), highlighter);
            break;
        }

        auto file = QFile(fullFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "Could not open file for reading" << fullFilePath;
            break;
        }
        auto in = QTextStream(&file);
        auto lineCount = 5000;
        auto output = QString();
        while (!in.atEnd() && lineCount >= 0) {
            output += in.readLine();
            output += "\n";
        }
        file.close();

        auto langInfo = ::Qutepart::chooseLanguage({}, {}, filename);
        if (langInfo.isValid()) {
            highlighter = langInfo.id;
        }
        updateEditor(output, highlighter);
        break;
    }
    default:
        break;
    }
}

void CommitForm::revertCurrentImpl() {
    auto selected = ui->tableView->currentIndex();
    auto idx = model->index(selected.row(), 2);
    auto fileName = model->data(idx, Qt::DisplayRole).toString();
    auto status =
        static_cast<GitFileStatus>(model->data(idx, GitStatusTableModel::StatusRole).toInt());

    auto isDir = QFileInfo(QDir(repoRoot).filePath(fileName)).isDir();

    auto msgBox = QMessageBox();
    msgBox.setWindowTitle("Revert file");
    if (status == GitFileStatus::Modified) {
        msgBox.setText(
            tr("Are you sure you want to revert this file?<br><br><b>%1</b>").arg(fileName));
    } else if (isDir) {
        msgBox.setText(
            tr("Are you sure you want to delete this directory?<br><br><b>%1</b>").arg(fileName));
    } else {
        msgBox.setText(
            tr("Are you sure you want to delete this file?<br><br><b>%1</b>").arg(fileName));
    }
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIcon(QMessageBox::Icon::Question);
    auto reply = msgBox.exec();
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (status == GitFileStatus::Modified) {
        auto args = QStringList{"-C", repoRoot, "checkout", fileName};
        git->runGit(args).then(this, [this](const std::tuple<QString, int> &res) {
            auto [output, exitCode] = res;
            ui->gitOutput->setText(output.trimmed());
        });
    } else {
        auto fullPath = QDir(repoRoot).filePath(fileName);
        auto success = isDir ? QDir(fullPath).removeRecursively() : QFile::remove(fullPath);
        if (success) {
            ui->gitOutput->setText(tr("Deleted %1").arg(fileName));
        } else {
            ui->gitOutput->setText(tr("Failed deleting %1").arg(fileName));
        }
    }
    updateGitStatus();
}

void CommitForm::revertSelectionImpl() {
    ui->commitMessage->clear();
    auto checked = model->checkedEntries();
    if (checked.isEmpty()) {
        return;
    }

    auto msgBox = QMessageBox();
    msgBox.setWindowTitle("Revert multiple files");
    msgBox.setText(tr("Are you sure you want to revert %1 files?").arg(checked.count()));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIcon(QMessageBox::Icon::Question);
    auto reply = msgBox.exec();
    if (reply != QMessageBox::Yes) {
        return;
    }

    auto args = QStringList{"-C", repoRoot, "checkout"};
    for (auto &c : std::as_const(checked)) {
        args.push_back(c.filename);
    }

    ui->gitOutput->clear();
    git->runGit(args).then(this, [this](const std::tuple<QString, int> &res) {
        auto [output, exitCode] = res;
        ui->gitOutput->setText(output.trimmed());
        if (exitCode == 0) {
            updateGitStatus();
        }
    });
}

void CommitForm::commitImpl() {
    auto const &checked = model->checkedEntries();
    if (checked.isEmpty() && !ui->amendCheckbox->isChecked()) {
        return;
    }

    auto doCommit = [this]() {
        auto commitLogFileName = createTempFileWithContent(ui->commitMessage->toPlainText());
        auto commitArgs = QStringList{"-C", repoRoot, "commit"};
        if (ui->amendCheckbox->isChecked()) {
            commitArgs << "--amend";
        }
        commitArgs << "-F" << commitLogFileName;

        git->runGit(commitArgs)
            .then(this, [this, commitLogFileName](const std::tuple<QString, int> &res2) {
                auto [output2, exitCode2] = res2;
                QFile::remove(commitLogFileName);
                ui->gitOutput->setText(output2.trimmed());
                if (exitCode2 != 0) {
                    qDebug() << QString("ExitCode=%1, output=%2").arg(exitCode2).arg(output2);
                } else {
                    ui->commitMessage->clear();
                    ui->amendCheckbox->setChecked(false);
                }
                updateGitStatus();
            });
    };

    if (checked.isEmpty()) {
        doCommit();
    } else {
        auto args = QStringList{"-C", repoRoot, "add"};
        for (auto const &c : checked) {
            args.push_back(c.filename);
        }

        ui->gitOutput->clear();
        git->runGit(args).then(this, [this, doCommit](const std::tuple<QString, int> &res) {
            auto [output, exitCode] = res;
            if (exitCode != 0) {
                ui->gitOutput->setText(output.trimmed());
                qDebug() << QString("ExitCode=%1, output=%2").arg(exitCode).arg(output);
                return;
            }
            doCommit();
        });
    }
}

void CommitForm::pushImpl() {
    auto t = ui->pushButton->text();
    ui->pushButton->setEnabled(false);
    ui->pushButton->setText(tr("Pushing..."));
    ui->gitOutput->clear();

    auto args = QStringList{"-C", repoRoot, "push"};
    if (ui->forcePushCheckbox->isChecked()) {
        args << "--force";
    }

    git->runGit(args).then(this, [this, t](const std::tuple<QString, int> &result) {
        auto [output, exitCode] = result;
        if (exitCode != 0) {
            qDebug()
                << QString("ExitCode=%1, output=%2\ncommand=git push").arg(exitCode).arg(output);
        }

        updateGitStatus();
        ui->gitOutput->setText(output.trimmed());
        ui->pushButton->setText(t);
        ui->pushButton->setEnabled(true);
    });
};

void CommitForm::setAmend(bool amend) { ui->amendCheckbox->setChecked(amend); }
