/**
 * \file qmdiSplitTab.h
 * \brief Definition of qmdi enabled split tav
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QPainter>
#include <QPushButton>
#include <QSize>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

#include "SplitTabWidget.h"
#include "qmdiserver.h"

class DefaultButtonsProvider : public ButtonsProvider {
  public:
    virtual QWidget *requestButton(bool first, int tabIndex, SplitTabWidget *split);

    QWidget *getFirstTabButtons(bool first, SplitTabWidget *split);
    QWidget *getNonFirstTabButtons(bool first, SplitTabWidget *split);

  private:
    QAction *appMenuAction = nullptr;
};

class qmdiSplitTab : public SplitTabWidget, public qmdiServer {
    Q_OBJECT

  public:
    // SplitTabWidget
    bool loadingFinished = false;
    qmdiSplitTab(QWidget *parent = nullptr);
    ~qmdiSplitTab() override;
    virtual void onTabFocusChanged(QWidget *widget, bool focused) override;
    virtual bool event(QEvent *ev) override;
    virtual bool eventFilter(QObject *obj, QEvent *event) override;
    virtual void onNewSplitCreated(QTabWidget *tabWidget, int count) override;

    // qmdiServer interface
    virtual void addClient(qmdiClient *client, int position = -1) override;
    virtual void deleteClient(qmdiClient *) override;
    virtual int getClientsCount() const override;
    virtual qmdiClient *getClient(int i) const override;
    virtual qmdiClient *getCurrentClient() const override;
    virtual int getCurrentClientIndex() const override;
    virtual void setCurrentClientIndex(int i) override;
    virtual int getClientIndex(qmdiClient *client) const override;
    virtual void moveClient(int oldPosition, int newPosition) override;
    virtual void updateClientName(const qmdiClient *client) override;

  signals:
    void newClientAdded(qmdiClient *);

  private:
    virtual void mdiSelected(qmdiClient *client, int index) const override;
    void on_middleMouse_pressed(int, QPoint);
    void on_rightMouse_pressed(int, QPoint);
    int computeLeading(QTabWidget *);

    QTabWidget *tabWidgetFromIndex(int globalIndex, int &localIndex) const;
    QWidget *activeWidget = nullptr;
};
