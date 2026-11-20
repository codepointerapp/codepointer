/**
 * \file LspReferencesWidget.cpp
 * \brief Implementation LspReferencesWidget
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPalette>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QVariant>

#include "GlobalCommands.hpp"
#include "LspReferencesWidget.hpp"

namespace {
/// Keeps a reference visible when the filter matches its "file:line" text or
/// its full path.
class ReferencesProxyModel : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        const auto pattern = filterRegularExpression();
        if (pattern.pattern().isEmpty()) {
            return true;
        }
        const auto index = sourceModel()->index(sourceRow, 0, sourceParent);
        return pattern.match(index.data(Qt::DisplayRole).toString()).hasMatch() ||
               pattern.match(index.data(LspReferencesWidget::FileNameRole).toString()).hasMatch();
    }
};
} // namespace

LspReferencesWidget::LspReferencesWidget(QWidget *parent) : QWidget(parent) {
    symbolLabel = new QLabel(this);
    symbolLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    symbolLabel->setWordWrap(true);
    symbolLabel->setText(tr("References"));

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter references..."));
    m_filter->setClearButtonEnabled(true);

    m_model = new QStandardItemModel(this);
    m_proxy = new ReferencesProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    list = new QListView(this);
    list->setModel(m_proxy);
    list->setAlternatingRowColors(true);
    // Double-click must open the reference, not start an in-place edit.
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);

    emptyLabel = new QLabel(tr("No references found"), this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(list);
    m_stack->addWidget(emptyLabel);
    m_stack->setCurrentIndex(1);

    summaryLabel = new QLabel(this);
    auto dim = palette().brush(QPalette::Disabled, QPalette::WindowText);
    summaryLabel->setStyleSheet(QStringLiteral("color: %1").arg(dim.color().name()));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(symbolLabel);
    layout->addWidget(m_filter);
    layout->addWidget(m_stack, 1);
    layout->addWidget(summaryLabel);

    connect(list, &QListView::activated, this, [this](const QModelIndex &proxyIndex) {
        const auto index = m_proxy->mapToSource(proxyIndex);
        if (!index.isValid()) {
            return;
        }
        emit openLocation(index.data(FileNameRole).toString(), index.data(LineNumberRole).toInt(),
                          index.data(ColumnNumberRole).toInt());
    });

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
        updateEmptyState();
    });
}

void LspReferencesWidget::setResults(const QString &symbol, const QVariantList &rows) {
    symbolLabel->setText(tr("References to %1").arg(symbol));

    auto files = QSet<QString>();
    for (auto const &row : rows) {
        files.insert(row.toHash()[GlobalArguments::FileName].toString());
    }
    m_totalRows = static_cast<int>(rows.size());
    m_totalFiles = files.size();

    m_filter->clear();
    m_model->clear();
    for (auto const &row : rows) {
        auto const tag = row.toHash();
        auto const fileName = tag[GlobalArguments::FileName].toString();
        auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();
        auto const columnNumber = tag[GlobalArguments::ColumnNumber].toInt();

        auto const item = new QStandardItem(
            QStringLiteral("%1:%2").arg(QFileInfo(fileName).fileName()).arg(lineNumber));
        item->setEditable(false);
        item->setToolTip(fileName);
        item->setData(fileName, FileNameRole);
        item->setData(lineNumber, LineNumberRole);
        item->setData(columnNumber, ColumnNumberRole);
        m_model->appendRow(item);
    }

    updateEmptyState();

    if (!m_dock) {
        return;
    }
    if (!rows.isEmpty()) {
        m_dock->show();
        m_dock->raise();
        if (m_proxy->rowCount() > 0) {
            list->setCurrentIndex(m_proxy->index(0, 0));
        }
        list->setFocus();
    }
}

void LspReferencesWidget::updateEmptyState() {
    auto const visible = m_proxy->rowCount();
    emptyLabel->setText(m_totalRows == 0 ? tr("No references found")
                                         : tr("No references match the filter"));
    m_stack->setCurrentIndex(visible == 0 ? 1 : 0);

    if (visible == m_totalRows) {
        summaryLabel->setText(tr("found %1 items in %2 files").arg(m_totalRows).arg(m_totalFiles));
    } else {
        summaryLabel->setText(tr("showing %1 of %2 items").arg(visible).arg(m_totalRows));
    }
}
