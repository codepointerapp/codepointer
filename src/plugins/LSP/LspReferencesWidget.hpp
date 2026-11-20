/**
 * \file LspReferencesWidget.cpp
 * \brief Implementation LspReferencesWidget
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QWidget>

class QDockWidget;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStandardItemModel;
class QStackedWidget;

/// Left-hand dock listing every use of a symbol.
///
/// The editor's context menu can only show a bounded number of results, so this
/// holds the whole list instead. Kept hidden until there is something to show.
class LspReferencesWidget : public QWidget {
    Q_OBJECT

  public:
    explicit LspReferencesWidget(QWidget *parent = nullptr);
    void setResults(const QString &symbol, const QVariantList &rows);
    void setDock(QDockWidget *dock) { m_dock = dock; }

    static const int FileNameRole = Qt::UserRole + 1;
    static const int LineNumberRole = Qt::UserRole + 2;
    static const int ColumnNumberRole = Qt::UserRole + 3;

  signals:
    void openLocation(const QString &fileName, int lineNumber, int columnNumber);

  private:
    void updateEmptyState();

    QLabel *symbolLabel = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *summaryLabel = nullptr;
    QListView *list = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QLabel *emptyLabel = nullptr;
    QStackedWidget *m_stack = nullptr;
    QDockWidget *m_dock = nullptr;
    int m_totalRows = 0;
    int m_totalFiles = 0;
};
