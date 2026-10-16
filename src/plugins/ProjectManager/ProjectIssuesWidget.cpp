#include <optional>

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QStyledItemDelegate>

#include "ProjectIssuesWidget.h"
#include "pluginmanager.h"
#include "ui_ProjectIssuesWidget.h"

// TODO: do not use qmdiEditor directly, but an interface. This will let
//       us eventually be able to replace the editorplugin.
#include "widgets/qmdieditor.h"

// Owner for everything this widget reports. Scoped so a build's marks clear as a
// group, the way clangd's do, rather than only ever being added.
constexpr auto BuildOwner = "build";

/// The severity of a compiler status line, or nothing when the line names no
/// severity we recognise. Optional rather than a default: compiler output is
/// arbitrary text, and inventing an error for every line we fail to parse would
/// mark whole files wrong.
auto typeToStatus(const QString &name) -> std::optional<Qutepart::DiagnosticSeverity> {
    if (name.contains("error", Qt::CaseInsensitive)) {
        return Qutepart::DiagnosticSeverity::Error;
    }
    if (name.compare("warning", Qt::CaseInsensitive) == 0) {
        return Qutepart::DiagnosticSeverity::Warning;
    }
    if (name.contains("info", Qt::CaseInsensitive) == 0) {
        return Qutepart::DiagnosticSeverity::Information;
    }
    return std::nullopt;
}

CompileStatusModel::CompileStatusModel(QObject *parent)
    : QAbstractTableModel(parent), showWarnings(true), showErrors(true), showOthers(true) {
    headers << tr("Type") << tr("Message") << tr("Location");
}

int CompileStatusModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : filteredStatuses.size();
}

int CompileStatusModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : headers.size();
}

QVariant CompileStatusModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= filteredStatuses.size()) {
        return {};
    }

    auto const &status = filteredStatuses.at(index.row());

    if (index.column() == 0 && role == Qt::DecorationRole) {
        auto const severity = typeToStatus(status.type);
        if (!severity) {
            return {};
        }
        // Keyed by the bit because that is what iconForStatus wants, and the bit
        // is derived rather than stored - the severity is the real thing.
        auto const iconType = Qutepart::diagnosticBit(*severity);
        auto static iconCache = QHash<int, QIcon>();
        if (!iconCache.contains(iconType)) {
            iconCache[iconType] = Qutepart::iconForStatus(iconType);
        }
        return iconCache.value(iconType);
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return {};
        case 1:
            return status.message;
        case 2:
            return QString("%1 (%2:%3)")
                .arg(status.displayName)
                .arg(status.row + 1)
                .arg(status.col + 1);
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case 1:
            return status.message;
        case 2:
            return status.fileName;
        default:
            break;
        }
    }

    return {};
}

QVariant CompileStatusModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section >= headers.size()) {
        return {};
    }

    return headers.at(section);
}

void CompileStatusModel::sort(int column, Qt::SortOrder order) {
    beginResetModel();
    std::sort(filteredStatuses.begin(), filteredStatuses.end(),
              [column, order](const CompileStatus &s1, const CompileStatus &s2) {
                  bool lessThan;
                  switch (column) {
                  case 0: // Type column
                      lessThan = s1.type < s2.type;
                      break;
                  case 1: // Message column
                      lessThan = s1.message < s2.message;
                      break;
                  case 2: // Location column
                      lessThan = s1.fileName < s2.fileName;
                      break;
                  default:
                      return false;
                  }
                  return order == Qt::AscendingOrder ? lessThan : !lessThan;
              });
    endResetModel();
}

void CompileStatusModel::clearAll() {
    beginResetModel();
    statuses.clear();
    filteredStatuses.clear();
    endResetModel();
}

void CompileStatusModel::addItem(const CompileStatus &status) {
    beginResetModel();
    statuses.append(status);
    applyFilter();
    endResetModel();
}

CompileStatus CompileStatusModel::getItem(const QModelIndex &index) const {
    if (!index.isValid()) {
        return {};
    }
    if (index.row() < 0) {
        return {};
    }
    if (index.row() >= filteredStatuses.size()) {
        return {};
    }
    return filteredStatuses.at(index.row());
}

QList<CompileStatus> CompileStatusModel::getItemsFor(const QString &filename) const {
    auto fileStatus = QVector<CompileStatus>();
    for (const auto &status : statuses) {
        if (status.fileName == filename) {
            fileStatus.append(status);
        }
    }
    return fileStatus;
}

void CompileStatusModel::setWarningsVisible(bool visible) {
    if (showWarnings != visible) {
        showWarnings = visible;
        beginResetModel();
        applyFilter();
        endResetModel();
    }
}

void CompileStatusModel::setErrorsVisible(bool visible) {
    if (showErrors != visible) {
        showErrors = visible;
        beginResetModel();
        applyFilter();
        endResetModel();
    }
}

void CompileStatusModel::setOthersVisible(bool visible) {
    if (showOthers != visible) {
        showOthers = visible;
        beginResetModel();
        applyFilter();
        endResetModel();
    }
}

bool CompileStatusModel::areWarningsVisible() const { return showWarnings; }

bool CompileStatusModel::areErrorsVisible() const { return showErrors; }

bool CompileStatusModel::areOthersVisible() const { return showOthers; }

void CompileStatusModel::applyFilter() {
    filteredStatuses.clear();
    for (const auto &status : std::as_const(statuses)) {
        if (shouldShowStatus(status)) {
            filteredStatuses.append(status);
        }
    }
}

bool CompileStatusModel::shouldShowStatus(const CompileStatus &status) const {
    QString lowerType = status.type.toLower();
    if (lowerType == "warning") {
        return showWarnings;
    } else if (lowerType == "error") {
        return showErrors;
    } else {
        return showOthers;
    }
}

ProjectIssuesWidget::ProjectIssuesWidget(PluginManager *parent)
    : QWidget(parent), ui(new Ui::ProjectIssuesWidget) {
    model = new CompileStatusModel(this);
    ui->setupUi(this);
    ui->issuesList->setModel(model);
    ui->issuesList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->issuesList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->issuesList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    this->manager = parent;
    connect(manager, &PluginManager::newClientAdded, this, &ProjectIssuesWidget::decorateClient);
    connect(ui->errorsButton, &QPushButton::clicked, this,
            [this]() { this->model->setErrorsVisible(this->ui->errorsButton->isChecked()); });
    connect(ui->warningsButton, &QPushButton::clicked, this,
            [this]() { this->model->setWarningsVisible(this->ui->warningsButton->isChecked()); });
    connect(ui->othersButton, &QPushButton::clicked, this,
            [this]() { this->model->setOthersVisible(this->ui->othersButton->isChecked()); });
    // Both halves: clearing the list without clearing the editors would leave marks
    // on the page that the list no longer knows about, and the next decorateClient
    // would not bring them back either.
    connect(ui->clearButton, &QPushButton::clicked, this, [this]() {
        this->model->clearAll();
        this->clearEditorStatuses();
    });
    connect(ui->issuesList, &QTableView::clicked, this, [this](const QModelIndex &index) {
        auto item = this->model->getItem(index);
        if (!item.fileName.isEmpty()) {
            this->manager->openFile(QDir::toNativeSeparators(item.fileName), item.col, item.row);
            this->manager->openFile("projectmanager:scrolloutput", 0, item.lineNumber);
        }
    });

    outputDetector.add(new ClOutputDetector);
    outputDetector.add(new GccOutputDetector);
    outputDetector.add(new CargoOutputDetector);
    outputDetector.add(new GoLangOutputDetector);
}

ProjectIssuesWidget::~ProjectIssuesWidget() { delete ui; }

auto static setEditorStatus(qmdiEditor *editor, const CompileStatus &status) {
    if (editor->isEmpty()) {
        return;
    }
    auto const severity = typeToStatus(status.type);
    if (!severity) {
        return;
    }
    editor->setDiagnostic(status.row, Qutepart::Diagnostic{BuildOwner, *severity, status.message});
}

void ProjectIssuesWidget::processLine(const QString &rawLines, int lineNumber,
                                      const QString &sourceDir, const QString &buildDir) {
    auto lines = rawLines.split("\n");
    for (auto const &line : std::as_const(lines)) {
        lineNumber += 1;
        outputDetector.processLine(line, sourceDir, buildDir);
        auto items = outputDetector.foundStatus();
        for (auto &item : items) {
            item.lineNumber = lineNumber;
            model->addItem(item);
            auto client = manager->clientForFileName(item.fileName);
            if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
                setEditorStatus(editor, item);
                editor->update();
            }
        }
    }
}

void ProjectIssuesWidget::clearEditorStatuses() {
    for (auto i = 0; i < static_cast<int>(manager->visibleTabs()); ++i) {
        if (auto editor = dynamic_cast<qmdiEditor *>(manager->getMdiClient(i))) {
            editor->clearDiagnostics(BuildOwner);
        }
    }
}

void ProjectIssuesWidget::decorateClient(qmdiClient *client) {
    if (auto editor = dynamic_cast<qmdiEditor *>(client)) {
        auto items = model->getItemsFor(editor->mdiClientFileName());
        for (const auto &item : std::as_const(items)) {
            setEditorStatus(editor, item);
        }
        editor->update();
    } else {
        qDebug() << "Could not decorate client for " << client->mdiClientFileName();
    }
}

void ProjectIssuesWidget::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}
