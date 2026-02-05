#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QtAlgorithms>

#include <qutepart/qutepart.h>

#include "CommitForm.hpp"
#include "GitPlugin.hpp"
#include "ui_CommitForm.h"

enum class GitFileStatus { Modified, Added, Deleted, Renamed, Copied, Untracked, Unknown };

struct GitStatusEntry {
    QString filename;
    GitFileStatus status;
    bool checked = false;
};

static auto parseGitStatus(QStringView statusOutput) -> QList<GitStatusEntry> {
    auto out = QList<GitStatusEntry>{};

    for (auto line : statusOutput.split('\n', Qt::SkipEmptyParts) |
                         std::views::filter([](auto l) { return l.size() >= 3; })) {
        const auto x = line[0];
        const auto y = line[1];

        out.append(
            {line.mid(3).trimmed().toString(), (x == '?' && y == '?')   ? GitFileStatus::Untracked
                                               : (x == 'A' || y == 'A') ? GitFileStatus::Added
                                               : (x == 'M' || y == 'M') ? GitFileStatus::Modified
                                               : (x == 'D' || y == 'D') ? GitFileStatus::Deleted
                                               : (x == 'R' || y == 'R') ? GitFileStatus::Renamed
                                               : (x == 'C' || y == 'C') ? GitFileStatus::Copied
                                                                        : GitFileStatus::Unknown});
    }
    return out;
}

class GitStatusTableModel final : public QAbstractTableModel {
    // Q_OBJECT

  public:
    explicit GitStatusTableModel(QObject *parent = nullptr);

    auto rowCount(const QModelIndex &parent = {}) const -> int override;
    auto columnCount(const QModelIndex &parent = {}) const -> int override;
    auto data(const QModelIndex &index, int role) const -> QVariant override;
    auto setData(const QModelIndex &index, const QVariant &value, int role) -> bool override;
    auto flags(const QModelIndex &index) const -> Qt::ItemFlags override;
    auto headerData(int section, Qt::Orientation orientation, int role) const -> QVariant override;

    auto setEntries(QList<GitStatusEntry> entries) -> void;
    auto checkedEntries() const -> QList<GitStatusEntry>;

  private:
    QList<GitStatusEntry> m_entries;

    static auto statusToText(GitFileStatus status) -> QString;
};

GitStatusTableModel::GitStatusTableModel(QObject *parent) : QAbstractTableModel(parent) {}

auto GitStatusTableModel::rowCount(const QModelIndex &parent) const -> int {
    return parent.isValid() ? 0 : m_entries.size();
}

auto GitStatusTableModel::columnCount(const QModelIndex &) const -> int {
    return 3; // checkbox | filename | status
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

/////////
CommitForm::CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent)
    : QWidget(parent), ui(new Ui::CommitForm) {
    ui->setupUi(this);
    mdiClientName = tr("Commit");
    repoRoot = dir;
    git = plugin;

    model = new GitStatusTableModel(ui->tableView);
    // We will make it simpler for now
    ui->modifiedFileNameLabel->hide();
    ui->modifiedFileContents->hide();

    ui->tableView->setModel(model);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);

    {
        editor = new Qutepart::Qutepart(this);

        // TODO - this would be epic
        // editor = textEditorPlugin->fileNewEditor();

        // TODO - I would like to get a highlighter from an extensions
        editor->setHighlighter("diff.xml");

        auto layout = ui->diffPreview->parentWidget()->layout();
        layout->replaceWidget(ui->diffPreview, editor);
        ui->diffPreview->deleteLater();
        ui->diffPreview = editor;
    }

    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected) {
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

    auto *header = ui->tableView->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);

    updateGitStatus();
}

CommitForm::~CommitForm() { delete ui; }

void CommitForm::updateGitStatus() {
    auto gitOutput = git->runGit({"-C", repoRoot, "status", "--porcelain"});
    auto status = parseGitStatus(gitOutput);
    model->setEntries(status);
}

void CommitForm::newFileSelected(const QString &filename) {
    if (filename.isEmpty()) {
        ui->diffPreview->setPlainText("");
        return;
    }

    auto diff = git->runGit({"-C", repoRoot, "diff", filename});
    ui->diffPreview->setPlainText(diff);
}
