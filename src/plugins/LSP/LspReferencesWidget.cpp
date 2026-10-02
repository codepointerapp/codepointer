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
#include <QListWidget>
#include <QPalette>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVariant>

#include "GlobalCommands.hpp"
#include "LspReferencesWidget.hpp"

LspReferencesWidget::LspReferencesWidget(QWidget *parent) : QWidget(parent) {
    symbolLabel = new QLabel(this);
    symbolLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    symbolLabel->setWordWrap(true);
    symbolLabel->setText(tr("References"));

    list = new QListWidget(this);
    list->setAlternatingRowColors(true);

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
    layout->addWidget(m_stack, 1);
    layout->addWidget(summaryLabel);

    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        emit openLocation(item->data(FileNameRole).toString(), item->data(LineNumberRole).toInt(),
                          item->data(ColumnNumberRole).toInt());
    });
}

void LspReferencesWidget::setResults(const QString &symbol, const QVariantList &rows) {
    symbolLabel->setText(tr("References to %1").arg(symbol));

    auto files = QSet<QString>();
    for (auto const &row : rows) {
        files.insert(row.toHash()[GlobalArguments::FileName].toString());
    }
    summaryLabel->setText(tr("found %1 items in %2 files").arg(rows.size()).arg(files.size()));

    list->clear();
    for (auto const &row : rows) {
        auto const tag = row.toHash();
        auto const fileName = tag[GlobalArguments::FileName].toString();
        auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();
        auto const columnNumber = tag[GlobalArguments::ColumnNumber].toInt();

        auto const item = new QListWidgetItem(
            QStringLiteral("%1:%2").arg(QFileInfo(fileName).fileName()).arg(lineNumber), list);
        item->setToolTip(fileName);
        item->setData(FileNameRole, fileName);
        item->setData(LineNumberRole, lineNumber);
        item->setData(ColumnNumberRole, columnNumber);
    }

    m_stack->setCurrentIndex(rows.isEmpty() ? 1 : 0);

    if (m_dock && !rows.isEmpty()) {
        m_dock->show();
        m_dock->raise();
    }
}
