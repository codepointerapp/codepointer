#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QTemporaryFile>
#include <QTextStream>
#include <QtAlgorithms>

#include "CommitForm.hpp"
#include "GitPlugin.hpp"
#include "plugins/texteditor/texteditor_plg.h"
#include "ui_CommitForm.h"
#include "widgets/qmdieditor.h"

enum class GitFileStatus { Modified, Added, Deleted, Renamed, Copied, Untracked, Unknown };

struct GitStatusEntry {
    QString filename;
    GitFileStatus status;
    bool checked = false;
};

static auto parseGitStatus(QStringView statusOutput) -> QList<GitStatusEntry>
{
    auto out = QList<GitStatusEntry>();
    for (auto line : statusOutput.split('\n', Qt::SkipEmptyParts)) {
        if (line.size() < 3) {
            continue;
        }
        auto x = line[0];
        auto y = line[1];
        auto status =
            (x == '?' && y == '?') ? GitFileStatus::Untracked :
            (x == 'A' || y == 'A') ? GitFileStatus::Added :
            (x == 'M' || y == 'M') ? GitFileStatus::Modified :
            (x == 'D' || y == 'D') ? GitFileStatus::Deleted :
            (x == 'R' || y == 'R') ? GitFileStatus::Renamed :
            (x == 'C' || y == 'C') ? GitFileStatus::Copied :
                                     GitFileStatus::Unknown;
        out.append({ line.mid(3).trimmed().toString(), status });
    }
    return out;
}

auto createTempFileWithContent(const QString &content) -> QString {
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

class GitStatusTableModel final : public QAbstractTableModel {
    // Q_OBJECT

  public:
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

    static auto statusToText(GitFileStatus status) -> QString;
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
    const auto &e = m_entries.at(index.row());
    if (index.column() == 0 && role == Qt::CheckStateRole) {
        return e.checked ? Qt::Checked : Qt::Unchecked;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case 1:
        return statusToText(e.status);
    case 2:
        return e.filename;
    default:
        return {};
    }
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

auto GitStatusTableModel::statusToText(GitFileStatus status) -> QString {
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

void GitStatusTableModel::setAllChecked(bool checked) {
    if (m_entries.isEmpty()) {
        return;
    }

    for (auto &e : m_entries) {
        e.checked = checked;
    }

    const QModelIndex topLeft = index(0, 0);
    const QModelIndex bottomRight = index(rowCount() - 1, 0);

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

/////////
CommitForm::CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent)
    : QWidget(parent), ui(new Ui::CommitForm) {
    ui->setupUi(this);
    mdiClientName = tr("Commit");
    repoRoot = dir;
    git = plugin;

    model = new GitStatusTableModel(ui->tableView);
    // We will make it simpler for now, no inline editing.
    // I hope in the future to add a way to edit the file itself here.
    // What prevetns:
    //  1. We don't have a notion of shared document. We cannot open the
    //     same "content" in different tabs.
    //  2. When double clicking a line in a diff, the code directly opens the
    //     modified file. Instead we will need to modify the code, and somehow
    //     catch this event in this class, and navigate to the file.
    //  3. 3 Color layuout would be stretch on small screens. I would like that
    //     on smaller "displays" the editor would be below the diff view, and
    //     on larger screen on the side. Qt provides no such layout.
    //     Solution to this might be having 2 editors with shared document, and
    //     on resize hide/show the revevant one. Other alternative - move it
    //     between layouts.
    {
        ui->modifiedFileNameLabel->hide();
        ui->modifiedFileContents->hide();
    }

    // This code is a back hack, I use instead of changing the UI file to use
    // a qmdiEditor. I am unsure how can I see QtDesigner to allocate the widget
    // in a non-standard way. Note how I request the editor plugin for a widget
    // instead of creating one manually here.
    {
        auto layout = ui->diffPreview->parentWidget()->layout();
        auto manager = git->getManager();
        auto plugin = manager->findPlugin("TextEditorPlugin");
        if (auto p = dynamic_cast<TextEditorPlugin *>(plugin)) {
            auto client = p->fileNewEditor();
            client->mdiServer = git->mdiServer;
            if (auto e = dynamic_cast<qmdiEditor *>(client)) {
                e->setLineNumbersVisible(false);
                e->setReadOnly(true);
                e->setMinimapVisible(false);
                e->setHighlighter("diff.xml");
                e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                layout->replaceWidget(ui->diffPreview, e);
                ui->diffPreview->deleteLater();
                ui->diffPreview = e->getEditor();
            }
        }
    }

    ui->tableView->setModel(model);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->revertSelectedButton->setEnabled(false);
    ui->commitButton->setEnabled(false);
    ui->commitMessage->setFocusPolicy(Qt::StrongFocus);

    auto *header = ui->tableView->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);

    connect(ui->revertCurrentButton, &QAbstractButton::clicked, this,
            &CommitForm::revertCurrentImpl);
    connect(ui->revertSelectedButton, &QAbstractButton::clicked, this,
            &CommitForm::revertSelectionImpl);
    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected) {
                qDebug() << "selection model changed";
                if (selected.indexes().size() == 0) {
                    newFileSelected({});
                    return;
                }
                auto firstIndex = selected.indexes().first();
                auto idx = model->index(firstIndex.row(), 2);
                auto fileName = model->data(idx, Qt::DisplayRole).toString();
                newFileSelected(fileName);
                Q_UNUSED(deselected);
            });
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (!roles.isEmpty() && !roles.contains(Qt::CheckStateRole)) {
                    return;
                }

                auto hasSelection = !model->checkedEntries().isEmpty();
                ui->revertSelectedButton->setEnabled(hasSelection);
                ui->commitButton->setEnabled(hasSelection);
            });
    connect(model, &QAbstractItemModel::modelReset, this,
            [this]() { ui->revertSelectedButton->setEnabled(false); });
    connect(ui->selectAllButton, &QAbstractButton::clicked, this,
            [this]() { model->setAllChecked(true); });
    connect(ui->selectNoneButton, &QAbstractButton::clicked, this, [this]() {
        model->setAllChecked(false);
        ui->revertSelectedButton->setEnabled(false);
    });
    connect(ui->commitButton, &QAbstractButton::clicked, this, &CommitForm::commitImpl);
    updateGitStatus();
}

CommitForm::~CommitForm() { delete ui; }

QString CommitForm::mdiClientFileName() { return QString("git:%1").arg(repoRoot); }

void CommitForm::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (ui->diffPreview) {
            ui->diffPreview->setFocus(Qt::ShortcutFocusReason);
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

void CommitForm::updateGitStatus() {
    auto [gitOutput, exitCode] = git->runGit({"-C", repoRoot, "status", "--porcelain"});
    auto status = parseGitStatus(gitOutput);
    model->setEntries(status);
    if (model->rowCount() > 0) {
        ui->tableView->selectRow(0);
    }
}

void CommitForm::newFileSelected(const QString &filename) {
    if (filename.isEmpty()) {
        ui->diffPreview->setPlainText("");
        return;
    }

    auto [output, exitCode] = git->runGit({"-C", repoRoot, "diff", filename});
    if (exitCode != 0) {
        qDebug() << QString("git - code=%1, output=[%2]").arg(exitCode).arg(output);
        ui->commitLogLabel->setText("");
        ui->diffPreview->setPlainText(output);
        return;
    }

    ui->diffPreview->setPlainText(output);
    // FIXME: this looks way too ugly,
    // Problem - the "editor" is not the correct widge
    // The UI expects a QPlainTextEdit, and we have Widget that includes a
    // QPlainTextEdit.
    if (auto editor = dynamic_cast<qmdiEditor *>(ui->diffPreview->parent()->parent())) {
        editor->updateInternalMappings(repoRoot);
    } else {
        qDebug() << "Double click on diff will not work";
    }
}

void CommitForm::revertCurrentImpl() {
    auto selected = ui->tableView->currentIndex();
    auto idx = model->index(selected.row(), 2);
    auto fileName = model->data(idx, Qt::DisplayRole).toString();

    auto msgBox = QMessageBox();
    msgBox.setWindowTitle("Revert file");
    msgBox.setText(tr("Are you sure you want to revert this file?\n%1").arg(fileName));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIcon(QMessageBox::Icon::Question);
    auto reply = msgBox.exec();
    if (reply != QMessageBox::Yes) {
        return;
    }

    auto args = QStringList{"-C", repoRoot, "checkout", fileName};
    auto [output, exitCode] = git->runGit(args);
    ui->gitOutput->setText(output);
    if (exitCode == 0) {
        updateGitStatus();
    }
}

void CommitForm::revertSelectionImpl() {
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

    auto [output, exitCode] = git->runGit(args);
    ui->gitOutput->setText(output);
    if (exitCode == 0) {
        updateGitStatus();
    }
}

void CommitForm::commitImpl() {
    auto const &checked = model->checkedEntries();
    if (checked.isEmpty()) {
        return;
    }

    auto args = QStringList{"-C", repoRoot, "add"};
    for (auto const &c : checked) {
        args.push_back(c.filename);
    }
    auto [output, exitCode] = git->runGit(args);
    if (exitCode != 0) {
        qDebug() << QString("ExitCode=%1, output=%2\ncommand=%3")
                        .arg(exitCode)
                        .arg(output)
                        .arg(QString("git ") + args.join(' '));
        return;
    }

    auto commitLogFileName = createTempFileWithContent(ui->commitMessage->toPlainText());
    auto cleanup = qScopeGuard([&] { QFile::remove(commitLogFileName); });
    args = QStringList{"-C", repoRoot, "commit", "-F", commitLogFileName};
    std::tie(output, exitCode) = git->runGit(args);
    if (exitCode != 0) {
        qDebug() << QString("ExitCode=%1, output=%2\ncommand=%3")
                        .arg(exitCode)
                        .arg(output)
                        .arg(QString("git ") + args.join(' '));
        return;
    }
    this->deleteLater();
}
