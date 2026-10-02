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
class QListWidget;
class QListWidgetItem;
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

  signals:
    void openLocation(const QString &fileName, int lineNumber, int columnNumber);

  private:
    static const int FileNameRole = Qt::UserRole + 1;
    static const int LineNumberRole = Qt::UserRole + 2;
    static const int ColumnNumberRole = Qt::UserRole + 3;

    QLabel *symbolLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QListWidget *list = nullptr;
    QLabel *emptyLabel = nullptr;
    QStackedWidget *m_stack = nullptr;
    QDockWidget *m_dock = nullptr;
};