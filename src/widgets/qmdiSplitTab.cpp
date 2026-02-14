/**
 * \file qmdiSplitTab.cpp
 * \brief Definition of qmdi enabled split tav
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSize>
#include <QSplitter>
#include <QString>
#include <QStyle>
#include <QStyleOptionMenuItem>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <pluginmanager.h>
#include <qmdiclient.h>
#include <qmdihost.h>

#include "widgets/qmdiSplitTab.h"

// Should we have a close button on each tab? or at the sides of the widget? On the widget
// This is not default, as close button on each tab takes space. Mid button click exists, right
// button click brings a menu to close.
#define CLOSABLE_TABS 0

class DecoratedMenu : public QMenu {
  public:
    explicit DecoratedMenu(const QString &sidebarText, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QString m_sidebarText;
    int m_sidebarWidth;
};

DecoratedMenu::DecoratedMenu(const QString &sidebarText, QWidget *parent)
    : QMenu(parent), m_sidebarText(sidebarText) {
    m_sidebarWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    setObjectName("fancyShmancyMenu");
}

void DecoratedMenu::paintEvent(QPaintEvent *event) {
    QMenu::paintEvent(event);

    auto p = QPainter(this);
    auto sidebarRect = QRect(0, 0, m_sidebarWidth, height());
    auto grad = QLinearGradient(sidebarRect.topLeft(), sidebarRect.bottomLeft());
    auto sidebarColor = palette().color(QPalette::Highlight);
    grad.setColorAt(0, sidebarColor);
    grad.setColorAt(1, sidebarColor.lighter(150));

    p.fillRect(sidebarRect, grad);
    p.save();
    p.translate(sidebarRect.center());
    p.rotate(-90);
    auto rotatedRect = QRect(-sidebarRect.height() / 2, -sidebarRect.width() / 2,
                             sidebarRect.height(), sidebarRect.width());

    p.setPen(Qt::darkGray);
    p.translate(-1, +1);
    p.drawText(rotatedRect, Qt::AlignCenter, m_sidebarText);
    p.translate(+2, -2);
    p.setPen(Qt::white);
    p.drawText(rotatedRect, Qt::AlignCenter, m_sidebarText);
    p.restore();
}

QWidget *DefaultButtonsProvider::requestButton(bool first, int tabIndex, SplitTabWidget *split) {
    auto manager = dynamic_cast<PluginManager *>(split->parent());
    auto isMinimizedMode = manager->isInMinimizedMode();
    if (isMinimizedMode && tabIndex == 0) {
        return getFirstTabButtons(first, split);
    }
    return getNonFirstTabButtons(first, split);
}

QWidget *DefaultButtonsProvider::getFirstTabButtons(bool first, SplitTabWidget *split) {
    auto manager = dynamic_cast<PluginManager *>(split->parent());

    if (first) {
        auto appMenuButton = new QToolButton(split);
        appMenuButton->setObjectName("fancyShmancyMenu");
        appMenuButton->setIcon(split->windowIcon());
        appMenuButton->setText(QApplication::applicationName());
        appMenuButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        appMenuButton->setPopupMode(QToolButton::InstantPopup);
        appMenuButton->setAutoRaise(true);
        appMenuButton->setProperty("windowActive", true);

        auto appHighlightColor = qApp->palette().color(QPalette::Highlight);
        auto b = appHighlightColor.lighter();
        b.setAlpha(180);

        auto highlightedStyle =
            QString::fromUtf8(R"CSS(
QToolButton#fancyShmancyMenu[windowActive="true"] {
    background-color: %1;
    border: 1px solid transparent;
}

QToolButton#fancyShmancyMenu:hover {
    background-color: %2;
}

QToolButton#fancyShmancyMenu:pressed {
    background-color: %2;
}

QToolButton#fancyShmancyMenu[windowActive="false"] {
    background-color: transparent;
    // border: 1px solid transparent;
}
)CSS")
                .arg(b.name(QColor::HexArgb), appHighlightColor.name(QColor::HexArgb));

        manager->setStyleSheet(highlightedStyle);
        QObject::connect(qApp, &QGuiApplication::focusWindowChanged, appMenuButton,
                         [appMenuButton](QWindow *win) {
                             QWidget *windowWidget = appMenuButton->window();
                             bool active = windowWidget && windowWidget->windowHandle() &&
                                           windowWidget->windowHandle() == win;

                             appMenuButton->setProperty("windowActive", active);
                             appMenuButton->style()->unpolish(appMenuButton);
                             appMenuButton->style()->polish(appMenuButton);
                             appMenuButton->update();
                         });

        auto popup = new DecoratedMenu(QApplication::applicationName(), manager);
        auto menu = manager->menus.updatePopMenu(popup);
        appMenuButton->setMenu(menu);

        auto action = new QAction(appMenuButton);
        action->setShortcuts({Qt::ALT | Qt::Key_M, Qt::ALT | Qt::Key_Atilde});
        action->setShortcutContext(Qt::ApplicationShortcut);
        QObject::connect(action, &QAction::triggered, appMenuButton, [appMenuButton] {
            if (appMenuButton->menu() && appMenuButton->menu()->isVisible()) {
                appMenuButton->menu()->close();
            } else if (appMenuButton->menu()) {
                appMenuButton->showMenu();
            }
        });

        if (appMenuAction) {
            manager->removeAction(appMenuAction);
        }
        manager->addAction(action);
        appMenuAction = action;

        appMenuButton->setToolTip(
            manager->tr("Application menu %1").arg(appMenuAction->shortcut().toString()));
        QObject::connect(appMenuButton, &QObject::destroyed, split, [this, action]() {
            if (appMenuAction == action) {
                appMenuAction = nullptr;
            }
        });

        return appMenuButton;
    }

#if not CLOSABLE_TABS
    auto tabCloseBtn = new QToolButton(split);
    tabCloseBtn->setObjectName("simple-close-button");
    tabCloseBtn->setAutoRaise(true);
    tabCloseBtn->setIcon(QIcon::fromTheme("document-close"));
    QObject::connect(tabCloseBtn, &QAbstractButton::clicked, manager, &PluginManager::closeClient);
    return tabCloseBtn;
#else
    return nullptr;
#endif
}

QWidget *DefaultButtonsProvider::getNonFirstTabButtons(bool first, SplitTabWidget *split) {
    auto manager = dynamic_cast<PluginManager *>(split->parent());

    if (first) {
        auto tabNewBtn = new QToolButton(split);
        tabNewBtn->setObjectName("newDocumentSplitButton");
        tabNewBtn->setAutoRaise(true);
        tabNewBtn->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentNew));
        QObject::connect(tabNewBtn, &QAbstractButton::clicked, tabNewBtn,
                         [manager, tabNewBtn, split]() {
                             auto tab = qobject_cast<QTabWidget *>(tabNewBtn->parentWidget());
                             if (tab) {
                                 split->updateCurrentTabWidget(tab);
                             }
                             if (manager) {
                                 emit manager->newFileRequested(tabNewBtn);
                             }
                         });
        return tabNewBtn;
    }

#if not CLOSABLE_TABS
    auto tabCloseBtn = new QToolButton(split);
    tabCloseBtn->setAutoRaise(true);
    tabCloseBtn->setIcon(QIcon::fromTheme("document-close"));
    QObject::connect(tabCloseBtn, &QAbstractButton::clicked, manager, &PluginManager::closeClient);
    return tabCloseBtn;
#else
    return nullptr;
#endif
}

qmdiSplitTab::qmdiSplitTab(QWidget *parent) : SplitTabWidget(parent) {
    connect(this, &SplitTabWidget::emptyAreaDoubleClicked, this,
            [this](QTabWidget *tabWidget, const QPoint &) {
                auto manager = dynamic_cast<PluginManager *>(this->parent());
                if (tabWidget) {
                    updateCurrentTabWidget(tabWidget);
                }
                if (tabWidget && manager) {
                    emit manager->newFileRequested(tabWidget->tabBar());
                }
            });
}

qmdiSplitTab::~qmdiSplitTab() {
    auto count = getClientsCount();
    for (int i = 0; i < count; ++i) {
        if (auto client = getClient(i)) {
            client->mdiServer = nullptr;
        }
    }
}

void qmdiSplitTab::onTabFocusChanged(QWidget *widget, bool focused) {
    SplitTabWidget::onTabFocusChanged(widget, focused);

    // nothing to do, if the same tab has been selected twice
    if (widget == activeWidget) {
        return;
    }
    if (!focused) {
        if (widget == nullptr) {
            auto client = dynamic_cast<qmdiClient *>(activeWidget);
            if (client && mdiHost) {
                mdiHost->unmergeClient(client);
                auto m = dynamic_cast<QMainWindow *>(mdiHost);
                mdiHost->updateGUI(m);
            }
            activeWidget = nullptr;
        }
        return;
    }

    auto client = dynamic_cast<qmdiClient *>(activeWidget);
    if (client) {
        mdiHost->unmergeClient(client);
    }

    activeWidget = widget;

    client = dynamic_cast<qmdiClient *>(activeWidget);
    if (activeWidget) {
        mdiHost->mergeClient(client);
    }

    auto m = dynamic_cast<QMainWindow *>(mdiHost);
    auto index = getCurrentClientIndex();
    mdiHost->updateGUI(m);
    mdiSelected(client, index);

    if (auto firstTab = qobject_cast<QTabWidget *>(splitter->widget(0))) {
        if (auto button = qobject_cast<QToolButton *>(firstTab->cornerWidget(Qt::TopLeftCorner))) {
            auto menu = new DecoratedMenu(QApplication::applicationName(), this);
            mdiHost->menus.updatePopMenu(menu);
            button->setMenu(menu);
        };
    }
}

bool qmdiSplitTab::event(QEvent *ev) {
    if (ev->type() == QEvent::ParentChange) {
        // when minimized mode changes, modify buttons for each tab
        auto manager = dynamic_cast<PluginManager *>(parentWidget());
        if (manager) {
            connect(manager, &PluginManager::minimizedModeChanged, this, [this](bool status) {
                auto manager = dynamic_cast<PluginManager *>(parentWidget());
                SplitTabWidget::onSplitCountMaybeChanged();
                keepSingleClient = status;
                if (status && loadingFinished) {
                    if (getClientsCount() < 1) {
                        emit manager->newFileRequested(this);
                    }
                }
            });
        }
    }
    return QWidget::event(ev);
}

bool qmdiSplitTab::eventFilter(QObject *obj, QEvent *event) {
    if (!obj->inherits("QTabBar")) {
        return SplitTabWidget::eventFilter(obj, event);
    }

    if (event->type() != QEvent::MouseButtonPress) {
        return SplitTabWidget::eventFilter(obj, event);
    }

    auto tabBar = qobject_cast<QTabBar *>(obj);
    auto mouseEvent = static_cast<QMouseEvent *>(event);
    auto position = mouseEvent->pos();
    auto clickedItem = tabBar->tabAt(position);
    if (clickedItem == -1) {
        return SplitTabWidget::eventFilter(obj, event);
    }

    auto tabWidget = qobject_cast<QTabWidget *>(tabBar->parent());
    auto globalPosition = tabBar->mapToGlobal(position);
    auto parentPosition = this->mapFromGlobal(globalPosition);
    clickedItem += computeLeading(tabWidget);

    switch (mouseEvent->button()) {
    case Qt::RightButton:
        // Use a QTimer::singleShot to display the context menu. Calling menu->exec()
        // directly from within the eventFilter creates a nested event loop, which
        // causes the mouse press event to be re-processed and incorrectly propagated
        // to the client widget, creating a second menu. The timer decouples the menu
        // creation from the event, ensuring the mouse event is fully consumed first.
        QTimer::singleShot(0, this, [this, clickedItem, parentPosition]() {
            on_rightMouse_pressed(clickedItem, parentPosition);
        });
        break;
    case Qt::MiddleButton:
        on_middleMouse_pressed(clickedItem, parentPosition);
        break;
    case Qt::LeftButton:
    default:
        return SplitTabWidget::eventFilter(obj, event);
    }

    return true;
}

void qmdiSplitTab::onNewSplitCreated(QTabWidget *tabWidget, int count) {
    SplitTabWidget::onNewSplitCreated(tabWidget, count);
    tabWidget->tabBar()->installEventFilter(this);

#if CLOSABLE_TABS
    tabWidget->setTabsClosable(true);
    connect(tabWidget, &QTabWidget::tabCloseRequested, tabWidget, [this, tabWidget](int index) {
        auto widget = tabWidget->widget(index);
        auto client = dynamic_cast<qmdiClient *>(widget);
        if (!client) {
            delete widget;
            return;
        }
        client->closeClient();
    });
#endif
}

void qmdiSplitTab::addClient(qmdiClient *client, int position) {
    auto w = dynamic_cast<QWidget *>(client);
    if (w == nullptr) {
        qDebug("%s %s %d: warning trying to add a qmdiClient which does not derive QWidget",
               __FILE__, __FUNCTION__, __LINE__);
        return;
    }

    auto label = client->mdiClientName;
    auto tooltip = client->mdiClientFileName();
    if (position < 0) {
        addTab(w, label, tooltip);
    } else {
        // We don't handle splits and restore if a specific position has been requested
        auto localIndex = -1;
        auto tabWidget = tabWidgetFromIndex(position, localIndex);
        if (tabWidget) {
            // TODO: This is very similar to addTabToSplit, merge functions somehow
            auto index = tabWidget->insertTab(localIndex, w, label);
            tabWidget->setCurrentIndex(index);
            tabWidget->setTabToolTip(index, tooltip);
            w->setObjectName(label);
            w->installEventFilter(this);
            w->setFocus();
            onTabFocusChanged(tabWidget->currentWidget(), true);
        } else {
            addTab(w, label, tooltip);
        }
    }
    client->mdiServer = this;
    w->setFocus();
    emit newClientAdded(client);
}

void qmdiSplitTab::deleteClient(qmdiClient *client) {
    if (client == nullptr) {
        return;
    }
    if (mdiHost == nullptr) {
        return;
    }
    if (dynamic_cast<qmdiClient *>(activeWidget) != client) {
        return;
    }

    mdiHost->unmergeClient(client);
    mdiHost->updateGUI(dynamic_cast<QMainWindow *>(mdiHost));
    activeWidget = nullptr;
}

int qmdiSplitTab::getClientsCount() const {
    auto total = 0;
    for (auto i = 0; i < splitter->count(); i++) {
        auto w = splitter->widget(i);
        auto t = qobject_cast<QTabWidget *>(w);
        if (!w) {
            continue;
        }
        total += t->count();
    }
    return total;
}

qmdiClient *qmdiSplitTab::getClient(int i) const {
    auto localIndex = -1;
    auto tabWidget = tabWidgetFromIndex(i, localIndex);
    if (tabWidget && localIndex >= 0) {
        return dynamic_cast<qmdiClient *>(tabWidget->widget(localIndex));
    }
    return nullptr;
}

qmdiClient *qmdiSplitTab::getCurrentClient() const {
    if (!currentTabWidget) {
        return nullptr;
    }
    auto currentIndex = currentTabWidget->currentIndex();
    if (currentIndex < 0) {
        return nullptr;
    }
    return dynamic_cast<qmdiClient *>(currentTabWidget->widget(currentIndex));
}

int qmdiSplitTab::getCurrentClientIndex() const {
    if (!currentTabWidget) {
        return -1;
    }
    auto currentWidgetIndex = currentTabWidget->currentIndex();
    if (currentWidgetIndex < 0) {
        return -1;
    }
    auto globalIndex = 0;
    for (auto i = 0; i < splitter->count(); i++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(i));
        if (!tabWidget) {
            continue;
        }
        if (tabWidget == currentTabWidget) {
            return globalIndex + currentWidgetIndex;
        }
        globalIndex += tabWidget->count();
    }

    return -1;
}

void qmdiSplitTab::setCurrentClientIndex(int i) {
    if (i < 0) {
        return;
    }

    auto currentCount = 0;
    for (auto tabIndex = 0; tabIndex < splitter->count(); tabIndex++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(tabIndex));
        if (!tabWidget) {
            continue;
        }

        auto tabCount = tabWidget->count();
        if (i < currentCount + tabCount) {
            // The requested index is in this tab widget
            tabWidget->setCurrentIndex(i - currentCount);
            updateCurrentTabWidget(tabWidget);
            return;
        }

        currentCount += tabCount;
    }
}

int qmdiSplitTab::getClientIndex(qmdiClient *client) const {
    if (!client) {
        return -1;
    }
    auto currentCount = 0;
    for (auto tabIndex = 0; tabIndex < splitter->count(); tabIndex++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(tabIndex));
        if (!tabWidget) {
            continue;
        }
        auto tabCount = tabWidget->count();
        for (auto localIndex = 0; localIndex < tabCount; localIndex++) {
            auto currentClient = dynamic_cast<qmdiClient *>(tabWidget->widget(localIndex));
            if (client == currentClient) {
                return currentCount;
            }
            currentCount++;
        }
    }
    return -1;
}

void qmdiSplitTab::moveClient(int oldPosition, int newPosition) {
    if (oldPosition == newPosition || oldPosition < 0 || newPosition < 0) {
        qDebug() << "qmdiSplitTab::moveClient Invalid old/new position";
        return;
    }

    auto oldLocalIndex = -1;
    auto oldTabWidget = tabWidgetFromIndex(oldPosition, oldLocalIndex);
    if (!oldTabWidget || oldLocalIndex < 0) {
        qDebug() << "qmdiSplitTab::moveClient Old tab/position not found";
        return;
    }

    auto newLocalIndex = -1;
    auto newTabWidget = tabWidgetFromIndex(newPosition, newLocalIndex);
    if (!newTabWidget || newLocalIndex < 0) {
        qDebug() << "qmdiSplitTab::moveClient New tab/position not found";
        return;
    }

    if (oldTabWidget == newTabWidget) {
        newTabWidget->tabBar()->moveTab(oldLocalIndex, newLocalIndex);
        newTabWidget->setCurrentIndex(newLocalIndex);
        return;
    }

    if (oldPosition > newPosition) {
        ++newLocalIndex;
    }

    auto widget = oldTabWidget->widget(oldLocalIndex);
    if (!widget) {
        qDebug() << "qmdiSplitTab::moveClient Widget not found at oldLocalIndex";
        return;
    }

    auto text = oldTabWidget->tabText(oldLocalIndex);
    auto tooltip = oldTabWidget->tabToolTip(oldLocalIndex);
    oldTabWidget->removeTab(oldLocalIndex);
    newTabWidget->insertTab(newLocalIndex, widget, text);
    newTabWidget->setTabToolTip(newLocalIndex, tooltip);
    newTabWidget->setCurrentIndex(newLocalIndex);
    updateCurrentTabWidget(newTabWidget);
}

void qmdiSplitTab::updateClientName(const qmdiClient *client) {
    for (auto tabIndex = 0; tabIndex < splitter->count(); tabIndex++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(tabIndex));
        if (!tabWidget) {
            continue;
        }
        for (auto innerIndex = 0; innerIndex < tabWidget->count(); innerIndex++) {
            auto w = tabWidget->widget(innerIndex);
            auto c = dynamic_cast<qmdiClient *>(w);
            if (!c) {
                return;
            }
            if (c == client) {
                tabWidget->setTabText(innerIndex, c->mdiClientName);
                tabWidget->setTabToolTip(innerIndex, c->mdiClientFileName());
                return;
            }
        }
    }
}

void qmdiSplitTab::mdiSelected(qmdiClient *client, int index) const {
    if (onMdiSelected) {
        onMdiSelected(client, index);
    }
}

void qmdiSplitTab::on_middleMouse_pressed(int i, QPoint) { tryCloseClient(i); }

void qmdiSplitTab::on_rightMouse_pressed(int i, QPoint p) { showClientMenu(i, p); }

int qmdiSplitTab::computeLeading(QTabWidget *w) {
    if (!w) {
        return 0;
    }
    auto leadingIndex = 0;
    for (auto tabIndex = 0; tabIndex < splitter->count(); tabIndex++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(tabIndex));
        if (!tabWidget) {
            continue;
        }
        if (tabWidget == w) {
            return leadingIndex;
        }
        leadingIndex += tabWidget->count();
    }
    return leadingIndex;
}

QTabWidget *qmdiSplitTab::tabWidgetFromIndex(int globalIndex, int &localIndex) const {
    auto currentIndex = 0;
    for (auto i = 0; i < splitter->count(); ++i) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(i));
        if (!tabWidget) {
            continue;
        }

        auto count = tabWidget->count();
        if (globalIndex < currentIndex + count) {
            localIndex = globalIndex - currentIndex;
            return tabWidget;
        }
        currentIndex += count;
    }

    localIndex = -1;
    return nullptr;
}
