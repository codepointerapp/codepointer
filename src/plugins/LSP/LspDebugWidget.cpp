#include <QCheckBox>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include "GlobalCommands.hpp"
#include "LspDebugWidget.hpp"
#include "LspPlugin.hpp"
#include "pluginmanager.h"
#include "widgets/qmdieditor.h"

static auto makeTable(const QStringList &headers) -> QTableWidget * {
    auto table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

LspDebugWidget::LspDebugWidget(LspPlugin *plugin, QWidget *parent)
    : QWidget(parent), plugin(plugin) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- servers -------------------------------------------------------------
    auto serversBox = new QGroupBox(tr("Servers"), this);
    auto serversLayout = new QVBoxLayout(serversBox);
    serversTable = makeTable({tr("Root"), tr("Server"), tr("State"), tr("Docs")});
    serversTable->setMaximumHeight(110);
    serversLayout->addWidget(serversTable);
    layout->addWidget(serversBox);

    // --- documents -----------------------------------------------------------
    auto documentsBox = new QGroupBox(tr("Synced documents"), this);
    auto documentsLayout = new QVBoxLayout(documentsBox);
    documentsTable = makeTable({tr("File"), tr("Version")});
    documentsTable->setMaximumHeight(110);
    documentsLayout->addWidget(documentsTable);
    layout->addWidget(documentsBox);

    // --- query ---------------------------------------------------------------
    auto queryBox = new QGroupBox(tr("Query"), this);
    auto queryLayout = new QVBoxLayout(queryBox);

    fileEdit = new QLineEdit(queryBox);
    fileEdit->setPlaceholderText(tr("absolute path to a source file"));
    currentEditorButton = new QPushButton(tr("Current editor"), queryBox);
    auto fileRow = new QHBoxLayout;
    fileRow->addWidget(fileEdit, 1);
    fileRow->addWidget(currentEditorButton);
    queryLayout->addLayout(fileRow);

    lineSpin = new QSpinBox(queryBox);
    lineSpin->setRange(0, 1000000);
    columnSpin = new QSpinBox(queryBox);
    columnSpin->setRange(0, 100000);
    auto posRow = new QHBoxLayout;
    posRow->addWidget(new QLabel(tr("Line:"), queryBox));
    posRow->addWidget(lineSpin);
    posRow->addWidget(new QLabel(tr("Col:"), queryBox));
    posRow->addWidget(columnSpin);
    posRow->addStretch(1);
    queryLayout->addLayout(posRow);
    // LSP positions are zero based, and so are the values the editor sends.
    queryLayout->addWidget(new QLabel(tr("<i>positions are 0-based</i>"), queryBox));

    syncButton = new QPushButton(tr("Sync"), queryBox);
    completionButton = new QPushButton(tr("Completion"), queryBox);
    hoverButton = new QPushButton(tr("Hover"), queryBox);
    auto buttonRow = new QHBoxLayout;
    buttonRow->addWidget(syncButton);
    buttonRow->addWidget(completionButton);
    buttonRow->addWidget(hoverButton);
    queryLayout->addLayout(buttonRow);
    layout->addWidget(queryBox);

    // --- log -----------------------------------------------------------------
    auto logBox = new QGroupBox(tr("Log"), this);
    auto logLayout = new QVBoxLayout(logBox);
    traceCheck = new QCheckBox(tr("Trace LSP traffic"), logBox);
    traceCheck->setChecked(true);
    auto clearButton = new QPushButton(tr("Clear"), logBox);
    auto logHeader = new QHBoxLayout;
    logHeader->addWidget(traceCheck, 1);
    logHeader->addWidget(clearButton);
    logLayout->addLayout(logHeader);

    logView = new QPlainTextEdit(logBox);
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(2000);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLayout->addWidget(logView);
    layout->addWidget(logBox, 1);

    connect(clearButton, &QPushButton::clicked, logView, &QPlainTextEdit::clear);
    connect(currentEditorButton, &QPushButton::clicked, this, &LspDebugWidget::useCurrentEditor);
    connect(syncButton, &QPushButton::clicked, this, &LspDebugWidget::runSync);
    connect(completionButton, &QPushButton::clicked, this, &LspDebugWidget::runCompletion);
    connect(hoverButton, &QPushButton::clicked, this, &LspDebugWidget::runHover);
    connect(serversTable, &QTableWidget::itemSelectionChanged, this, &LspDebugWidget::refreshState);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    connect(refreshTimer, &QTimer::timeout, this, &LspDebugWidget::refreshState);
    refreshTimer->start();
    refreshState();
}

void LspDebugWidget::log(const QString &message) {
    logView->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + message);
}

void LspDebugWidget::appendTrace(const QString &message) {
    if (traceCheck->isChecked()) {
        log(message);
    }
}

void LspDebugWidget::refreshState() {
    auto const infos = plugin->serverInfos();

    // Keep the selected root across refreshes, otherwise the 1s timer would fight
    // the user for the selection.
    auto selectedRoot = QString();
    if (auto *item = serversTable->item(serversTable->currentRow(), 0)) {
        selectedRoot = item->data(Qt::UserRole).toString();
    }

    serversTable->setRowCount(infos.size());
    auto rowToSelect = -1;
    for (auto row = 0; row < infos.size(); ++row) {
        auto const &info = infos[row];
        auto state = !info.running ? tr("dead") : (info.ready ? tr("ready") : tr("starting"));
        auto rootItem = new QTableWidgetItem(QFileInfo(info.root).fileName());
        rootItem->setData(Qt::UserRole, info.root);
        rootItem->setToolTip(info.root);
        serversTable->setItem(row, 0, rootItem);
        serversTable->setItem(
            row, 1,
            new QTableWidgetItem(info.serverName.isEmpty() ? info.binary : info.serverName));
        serversTable->setItem(row, 2, new QTableWidgetItem(state));
        serversTable->setItem(row, 3, new QTableWidgetItem(QString::number(info.documentCount)));
        if (info.root == selectedRoot) {
            rowToSelect = row;
        }
    }
    if (rowToSelect < 0 && !infos.isEmpty()) {
        rowToSelect = 0;
    }
    if (rowToSelect >= 0 && serversTable->currentRow() != rowToSelect) {
        serversTable->selectRow(rowToSelect);
    }

    auto root = QString();
    if (auto *item = serversTable->item(serversTable->currentRow(), 0)) {
        root = item->data(Qt::UserRole).toString();
    }
    auto const documents = plugin->documentsFor(root);
    documentsTable->setRowCount(documents.size());
    for (auto row = 0; row < documents.size(); ++row) {
        auto item = new QTableWidgetItem(QFileInfo(documents[row].first).fileName());
        item->setToolTip(documents[row].first);
        documentsTable->setItem(row, 0, item);
        documentsTable->setItem(row, 1,
                                new QTableWidgetItem(QString::number(documents[row].second)));
    }
}

void LspDebugWidget::useCurrentEditor() {
    auto manager = plugin->getManager();
    if (!manager) {
        return;
    }
    auto editor = dynamic_cast<qmdiEditor *>(manager->currentClient());
    if (!editor) {
        log(tr("No editor is focused"));
        return;
    }
    fileEdit->setText(editor->mdiClientFileName());
    log(tr("Using %1").arg(editor->mdiClientFileName()));
}

void LspDebugWidget::runSync() {
    auto fileName = fileEdit->text();
    if (fileName.isEmpty()) {
        log(tr("No file selected"));
        return;
    }
    auto manager = plugin->getManager();
    auto editor =
        manager ? dynamic_cast<qmdiEditor *>(manager->clientForFileName(fileName)) : nullptr;
    if (!editor) {
        log(tr("%1 is not open in an editor - open it first").arg(fileName));
        return;
    }
    if (!plugin->syncDocument(fileName, editor->getContent())) {
        log(tr("No ready server owns %1").arg(fileName));
    }
}

void LspDebugWidget::runCompletion() { runQuery(GlobalCommands::VariableInfo); }

void LspDebugWidget::runHover() { runQuery(GlobalCommands::KeywordTooltip); }

void LspDebugWidget::runQuery(const QString &command) {
    auto fileName = fileEdit->text();
    if (fileName.isEmpty()) {
        log(tr("No file selected"));
        return;
    }

    auto manager = plugin->getManager();
    auto editor =
        manager ? dynamic_cast<qmdiEditor *>(manager->clientForFileName(fileName)) : nullptr;
    if (!editor) {
        log(tr("%1 is not open in an editor - open it first").arg(fileName));
        return;
    }

    auto args = CommandArgs{
        {GlobalArguments::FileName, fileName},
        {GlobalArguments::Content, editor->getContent()},
        {GlobalArguments::LineNumber, lineSpin->value()},
        {GlobalArguments::ColumnNumber, columnSpin->value()},
        {GlobalArguments::ExactMatch, command == GlobalCommands::KeywordTooltip},
    };

    if (plugin->canHandleAsyncCommand(command, args) == CommandPriority::CannotHandle) {
        log(tr("LSP declines %1 here (no ready server, or unsupported file type)").arg(command));
        return;
    }

    log(QString(">> %1 at %2:%3").arg(command).arg(lineSpin->value()).arg(columnSpin->value()));

    auto watcher = new QFutureWatcher<CommandArgs>(this);
    connect(watcher, &QFutureWatcher<CommandArgs>::finished, this, [this, watcher, command]() {
        if (watcher->isFinished() && !watcher->isCanceled()) {
            auto result = watcher->result();
            if (command == GlobalCommands::VariableInfo) {
                auto const tags = result[GlobalArguments::Tags].toList();
                log(tr("<< %1 completions").arg(tags.size()));
                auto shown = 0;
                for (auto const &entry : tags) {
                    if (shown++ >= 25) {
                        log(QStringLiteral("   ... %1 more").arg(tags.size() - 25));
                        break;
                    }
                    auto const tag = entry.toHash();
                    auto detail = tag[GlobalArguments::Type].toString();
                    log(QStringLiteral("   %1%2").arg(tag[GlobalArguments::Name].toString(),
                                                      detail.isEmpty() ? QString()
                                                                       : "  : " + detail));
                }
            } else {
                auto tooltip = result[GlobalArguments::Tooltip].toString();
                log(tooltip.isEmpty() ? tr("<< no hover result")
                                      : tr("<< hover:\n%1").arg(tooltip));
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(plugin->handleCommandAsync(command, args));
}
