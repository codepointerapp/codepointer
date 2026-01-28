/**
 * \file qsvtextoperationswidget.cpp
 * \brief definition of widget for search, replace, gotoline in a QTextEdit
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QObject>
#include <QStackedWidget>
#include <QString>
#include <QTimer>

#include "textoperationsadapter.h"

class QWidget;
class QLineEdit;

namespace Ui {
class searchForm;
class replaceForm;
class gotoLineForm;
} // namespace Ui

class SharedHistoryModel;

class TextOperationsWidget : public QStackedWidget {
    Q_OBJECT

  public:
    TextOperationsWidget(QWidget *parent, QWidget *editor);
    void initSearchWidget();
    void initReplaceWidget();
    void initGotoLineWidget();
    void setSearchHistory(SharedHistoryModel *model);

    void setAdapter(TextOperationsAdapter *adapter);
    TextOperationsAdapter *adapter() const { return m_adapter; }

    TextOperationsAdapter::FindFlags getSearchFlags();
    TextOperationsAdapter::FindFlags getReplaceFlags();

    void setTextFont(const QFont &newFont);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  public slots:
    void showSearch();
    void showReplace();
    void showGotoLine();

    void searchText_modified(QString s);
    void replaceText_modified(QString s);
    void replaceOldText_returnPressed();
    void replaceAll_clicked();
    void searchNext();
    void searchPrevious();

    void updateSearchInput();
    void updateReplaceInput();

  protected:
    virtual bool eventFilter(QObject *obj, QEvent *event) override;
    bool issueSearch(const QString &text, bool incremental,
                     TextOperationsAdapter::FindFlags findOptions, QLineEdit *lineEdit,
                     bool moveCursor);

    QWidget *editor;
    TextOperationsAdapter *m_adapter = nullptr;
    QTimer replaceTimer;
    QTimer searchTimer;
    QColor searchFoundBackgroundColor;
    QColor searchNotFoundBackgroundColor;

  public:
    SharedHistoryModel *searchHistory;
    QWidget *searchWidget;
    QWidget *replaceWidget;
    QWidget *gotoLineWidget;

    Ui::searchForm *searchFormUi;
    Ui::replaceForm *replaceFormUi;
    Ui::gotoLineForm *gotoLineFormUi;
};
