/**
 * \file SplitTabWidget.cpp
 * \brief Implementation of the split tab widget
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "SplitTabWidget.h"
#include <QApplication>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QIODevice>
#include <QMimeData>
#include <QPainter>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

DropIndicatorWidget::DropIndicatorWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void DropIndicatorWidget::showAt(const QRect &rect, bool after) {
    auto lineWidth = 4;
    auto height = rect.height();
    auto x = after ? rect.right() - lineWidth / 2 : rect.left() - lineWidth / 2;
    m_after = after;

    setGeometry(x, rect.top(), lineWidth, height);
    show();
    raise();
    update();
}

void DropIndicatorWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    QColor color = palette().color(QPalette::Highlight);
    color.setAlpha(200);
    p.fillRect(rect(), color);
}

DraggableTabBar::DraggableTabBar(QWidget *parent) : QTabBar(parent) {
    setAcceptDrops(true);
    dropIndicator = new DropIndicatorWidget(this);
    dropIndicator->setFixedHeight(height());
}

void DraggableTabBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragStartPos = event->pos();
    }
    QTabBar::mousePressEvent(event);
}

void DraggableTabBar::mouseDoubleClickEvent(QMouseEvent *event) {
    auto index = tabAt(event->pos());
    if (index < 0) {
        emit emptyAreaDoubleClicked(event->pos());
    }
    QTabBar::mouseDoubleClickEvent(event);
}

void DraggableTabBar::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }

    auto tabWidget = qobject_cast<DraggableTabWidget *>(parentWidget());
    if (!tabWidget) {
        return;
    }

    // If drag and drop is disabled, just do regular tab reordering
    if (!dragAndDropEnabled) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    auto index = tabAt(dragStartPos);
    if (index < 0) {
        return;
    }

    if ((event->pos() - dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    auto page = tabWidget->widget(index);
    if (!page) {
        return;
    }

    auto drag = new QDrag(this);
    auto mimeData = new QMimeData;

    auto style = tabWidget->style();
    auto option = QStyleOptionTab();
    option.initFrom(tabWidget);
    option.state = QStyle::State_Selected;
    option.text = tabText(index);
    option.shape = tabWidget->tabBar()->shape();
    option.position = QStyleOptionTab::OnlyOneTab;
    option.selectedPosition = QStyleOptionTab::NotAdjacent;
    option.cornerWidgets = QStyleOptionTab::NoCornerWidgets;

    auto metrics = QFontMetrics(tabWidget->font());
    auto textWidth = metrics.horizontalAdvance(tabText(index));
    auto tabWidth = textWidth + 10;
    auto tabHeight = metrics.height() + 10;
    auto pixmap = QPixmap(tabWidth, tabHeight);
    auto painter = QPainter(&pixmap);

    pixmap.fill(Qt::transparent);
    painter.setRenderHint(QPainter::Antialiasing);
    option.rect = pixmap.rect();
    style->drawControl(QStyle::CE_TabBarTab, &option, &painter, tabWidget);

    painter.setPen(option.palette.color(QPalette::WindowText));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, tabText(index));
    painter.end();

    auto ptrData = QByteArray();
    auto stream = QDataStream(&ptrData, QIODevice::WriteOnly);
    stream << (quintptr)page << (quintptr)tabWidget;
    mimeData->setData("application/x-qplaintextedit-widget", ptrData);
    drag->setMimeData(mimeData);

    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    drag->exec(Qt::MoveAction);
}

void DraggableTabBar::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-qplaintextedit-widget")) {
        event->acceptProposedAction();
    }
}

void DraggableTabBar::dragMoveEvent(QDragMoveEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-qplaintextedit-widget")) {
        return;
    }

    auto dropPos = event->position().toPoint();
    auto newIndex = tabAt(dropPos);

    if (newIndex >= 0) {
        QRect rect = tabRect(newIndex);
        bool after = dropPos.x() > rect.center().x();
        dropIndicator->showAt(rect, after);
    } else {
        QRect rect = tabRect(count() - 1);
        dropIndicator->showAt(rect, true);
    }

    event->acceptProposedAction();
}

void DraggableTabBar::dragLeaveEvent(QDragLeaveEvent *event) {
    dropIndicator->hide();
    QTabBar::dragLeaveEvent(event);
}

void DraggableTabBar::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-qplaintextedit-widget")) {
        return;
    }

    auto tabWidget = qobject_cast<DraggableTabWidget *>(parentWidget());
    if (!tabWidget) {
        return;
    }

    // Forward the drop event to the tab widget
    QDropEvent newEvent(event->position(), event->dropAction(), event->mimeData(), event->buttons(),
                        event->modifiers());
    tabWidget->dropEvent(&newEvent);

    dropIndicator->hide();
}

DraggableTabWidget::DraggableTabWidget(QWidget *parent) : QTabWidget(parent) {
    setAcceptDrops(true);
    setTabBar(new DraggableTabBar(this));
    tabBar()->setAcceptDrops(true);
}

void DraggableTabWidget::tabRemoved(int index) {
    QTabWidget::tabRemoved(index);
    emit tabWidgetRemoved();
}

void DraggableTabWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-qplaintextedit-widget")) {
        event->acceptProposedAction();
    }
}

void DraggableTabWidget::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-qplaintextedit-widget")) {
        return;
    }

    auto ptrData = event->mimeData()->data("application/x-qplaintextedit-widget");
    auto stream = QDataStream(&ptrData, QIODevice::ReadOnly);
    auto ptrVal = quintptr{0};

    stream >> ptrVal;
    auto widget = reinterpret_cast<QWidget *>(ptrVal);
    if (!widget) {
        qDebug() << "DraggableTabWidget: drop rejected: null widget pointer.";
        return;
    }

    auto oldTabWidget = qobject_cast<DraggableTabWidget *>(widget->parent()->parent());
    if (!oldTabWidget) {
        return;
    }

    if (oldTabWidget == this) {
        auto oldIndex = indexOf(widget);
        auto dropPos = event->position().toPoint();
        auto newIndex = tabBar()->tabAt(dropPos);

        if (newIndex >= 0) {
            auto tabRect = tabBar()->tabRect(newIndex);
            if (dropPos.x() > tabRect.center().x()) {
                newIndex++;
            }
            if (newIndex != oldIndex && newIndex != oldIndex + 1) {
                auto label = tabText(oldIndex);
                auto movedWidget = this->widget(oldIndex);
                removeTab(oldIndex);
                insertTab(newIndex > oldIndex ? newIndex - 1 : newIndex, movedWidget, label);
                setCurrentWidget(movedWidget);
            }
        } else {
            auto label = tabText(oldIndex);
            auto movedWidget = this->widget(oldIndex);
            removeTab(oldIndex);
            addTab(movedWidget, label);
            setCurrentWidget(widget);
        }
        event->acceptProposedAction();
        return;
    }

    auto index = oldTabWidget->indexOf(widget);
    auto label = oldTabWidget->tabText(index);
    auto dropPos = event->position().toPoint();
    auto insertIndex = tabBar()->tabAt(dropPos);
    oldTabWidget->removeTab(index);
    if (insertIndex >= 0) {
        auto tabRect = tabBar()->tabRect(insertIndex);
        if (dropPos.x() > tabRect.center().x()) {
            insertIndex++;
        }
    } else {
        insertIndex = count();
    }

    insertTab(insertIndex, widget, label);
    setCurrentWidget(widget);
    event->acceptProposedAction();
}

SplitterWithWidgetAdded::SplitterWithWidgetAdded(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent) {}

void SplitterWithWidgetAdded::childEvent(QChildEvent *event) {
    QSplitter::childEvent(event);
    if (event->type() == QEvent::ChildAdded) {
        auto addedWidget = qobject_cast<QWidget *>(event->child());
        if (addedWidget) {
            emit widgetAdded(addedWidget);
        }
    }
}

SplitTabWidget::SplitTabWidget(QWidget *parent)
    : QWidget(parent), splitter(new SplitterWithWidgetAdded(Qt::Horizontal, this)) {
    auto layout = new QHBoxLayout(this);
    connect(splitter, &SplitterWithWidgetAdded::widgetAdded, this, [this](QWidget *w) {
        auto tab = qobject_cast<DraggableTabWidget *>(w);
        if (tab) {
            auto count = splitter->count();
            onNewSplitCreated(tab, count);
            QTimer::singleShot(0, this, [this]() { onSplitCountMaybeChanged(); });
        }
    });

    layout->addWidget(splitter);
    layout->setContentsMargins(0, 0, 0, 0);
    splitter->installEventFilter(this);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) mutable {
        if (!now || !this->isAncestorOf(now)) {
            return;
        }

        auto w = now;
        while (w) {
            if (auto *tab = qobject_cast<QTabWidget *>(w)) {
                if (tab != currentTabWidget) {
                    currentTabWidget = tab;
                    onTabFocusChanged(tab->currentWidget(), true);
                }
                break;
            }
            w = w->parentWidget();
        }
    });
}

void SplitTabWidget::addTab(QWidget *widget, const QString &label, const QString &tooltip) {
    addTabToCurrentSplit(widget, label, tooltip);

    // This code deals with pre-loading. You can configure a the pre-state of the split tab
    // and then when adding new tabs, the widget will split as requested.
    if (!savedSplitCount.empty()) {
        auto splitsCount = splitter->count();
        auto currentTabIndex = findSplitIndex(currentTabWidget);

        if (currentTabIndex >= savedSplitCount.size() ||
            currentTabIndex >= savedSplitInternalSizes.size()) {
            savedSplitCount.clear();
            savedSplitInternalSizes.clear();
            return;
        }

        auto widgetsInCurrentTab = currentTabWidget->count();
        auto maxWidgetsInCurrentTab = savedSplitCount[currentTabIndex];

        if (widgetsInCurrentTab == maxWidgetsInCurrentTab) {
            if (splitsCount != savedSplitCount.size()) {
                splitHorizontally();
            } else {
                splitter->setSizes(savedSplitInternalSizes);
                savedSplitCount.clear();
                savedSplitInternalSizes.clear();
            }
        }
    }
}

void SplitTabWidget::splitHorizontally() {
    auto currentIndex = splitter->indexOf(currentTabWidget);
    auto newTabWidget = new DraggableTabWidget(this);
    newTabWidget->setObjectName(QString("tabWidget#%1").arg(currentIndex + 1));
    newTabWidget->setDocumentMode(true);
    newTabWidget->setMovable(true);
    newTabWidget->setObjectName(QString("QTabWidget#%1").arg(splitter->count()));
    connect(newTabWidget, &DraggableTabWidget::tabWidgetRemoved, this, [this, newTabWidget]() {
        if (newTabWidget->count() == 0 && closeSplitWhenEmpty) {
            closeSplitWithTabWidget(newTabWidget);
        }
    });

    splitter->insertWidget(currentIndex + 1, newTabWidget);
    updateCurrentTabWidget(newTabWidget);
    equalizeWidths();
}

void SplitTabWidget::closeCurrentSplit() {
    // We alaways keep at last a single tab. Why? since it may contain a menu for the app.
    if (splitter->count() <= 1) {
        onTabFocusChanged(nullptr, true);
        return;
    }
    closeSplitWithTabWidget(currentTabWidget);
}

void SplitTabWidget::closeSplitWithTabWidget(QTabWidget *tab) {
    auto currentIndex = splitter->indexOf(tab);
    tab->deleteLater();
    if (currentIndex > 0) {
        currentIndex = currentIndex - 1;
    } else {
        currentTabWidget = nullptr;
        emit onTabFocusChanged(nullptr, false);
        QTimer::singleShot(0, this, [this]() { emit onSplitCountMaybeChanged(); });
        return;
    }

    auto newWidget = splitter->widget(currentIndex);
    auto newTab = qobject_cast<QTabWidget *>(newWidget);
    updateCurrentTabWidget(newTab);
    equalizeWidths();

    // Defer the split count check to after the event loop processes the deletion
    QTimer::singleShot(0, this, [this]() { onSplitCountMaybeChanged(); });
}

void SplitTabWidget::addTabToCurrentSplit(QWidget *widget, const QString &label,
                                          const QString &tooltip) {
    if (!currentTabWidget) {
        splitHorizontally();
    }
    auto index = currentTabWidget->addTab(widget, label);
    currentTabWidget->setCurrentIndex(index);
    currentTabWidget->setTabToolTip(index, tooltip);
    widget->setObjectName(label);
    widget->installEventFilter(this);
    widget->setFocus();
    onTabFocusChanged(currentTabWidget->currentWidget(), true);
}

void SplitTabWidget::addTabToSplit(int splitNumber, int position, QWidget *widget,
                                   const QString &label, const QString &tooltip) {
    auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(splitNumber));
    if (!tabWidget) {
        return;
    }

    auto index = tabWidget->insertTab(position, widget, label);
    tabWidget->setCurrentIndex(index);
    tabWidget->setTabToolTip(index, tooltip);
    widget->setObjectName(label);
    widget->installEventFilter(this);
    widget->setFocus();
    onTabFocusChanged(tabWidget->currentWidget(), true);
}

int SplitTabWidget::findSplitIndex(QTabWidget *w) {
    for (auto i = 0; i < splitter->count(); i++) {
        auto tabWidget = qobject_cast<QTabWidget *>(splitter->widget(i));
        if (!tabWidget) {
            continue;
        }
        if (w == tabWidget) {
            return i;
        }
    }
    return -1;
}

void SplitTabWidget::moveTabToNewSplit(QWidget *widget) {
    splitHorizontally();
    auto oldTab = qobject_cast<QTabWidget *>(widget->parentWidget()->parentWidget());
    auto index = oldTab->indexOf(widget);
    auto text = oldTab->tabText(index);
    auto tooltip = oldTab->tabToolTip(index);

    // The widget is automatically removed from oldTab when reparented
    auto newIndex = currentTabWidget->addTab(widget, text);
    currentTabWidget->setTabToolTip(newIndex, tooltip);
    currentTabWidget->setCurrentIndex(newIndex);
    widget->setFocus();
}

void SplitTabWidget::closeCurrentTab() {
    int index = currentTabWidget->currentIndex();
    if (index >= 0) {
        QWidget *widget = currentTabWidget->widget(index);
        widget->removeEventFilter(this);
        currentTabWidget->removeTab(index);
    }

    if (currentTabWidget->count() == 0) {
        if (closeSplitWhenEmpty) {
            closeCurrentSplit();
        }
    }
}

void SplitTabWidget::updateCurrentTabWidget(QTabWidget *newCurrent) {
    if (currentTabWidget == newCurrent) {
        return;
    }
    if (newCurrent->parentWidget() != splitter) {
        qDebug()
            << "updateCurrentTabWidget passing wrong parent, will not update current tab, parent="
            << newCurrent->parentWidget();
        return;
    }
    currentTabWidget = newCurrent;
    if (currentTabWidget && currentTabWidget->currentWidget()) {
        onTabFocusChanged(currentTabWidget->currentWidget(), true);
    }
}

void SplitTabWidget::equalizeWidths() {
    auto count = splitter->count();
    if (count <= 1) {
        return;
    }
    QList<int> sizes;
    auto width = splitter->width() / count;
    for (auto i = 0; i < count; ++i) {
        sizes << width;
    }
    splitter->setSizes(sizes);
}

bool SplitTabWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ShowToParent) {
        auto widget = qobject_cast<QWidget *>(watched);
        if (widget) {
            // Find the tab widget that contains this widget
            auto parent = widget->parentWidget();
            while (parent) {
                if (auto tabWidget = qobject_cast<QTabWidget *>(parent)) {
                    if (tabWidget == currentTabWidget && tabWidget->currentWidget()) {
                        onTabFocusChanged(tabWidget->currentWidget(), true);
                    }
                    break;
                }
                parent = parent->parentWidget();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SplitTabWidget::moveNextTab() {
    auto tabWidget = currentTabWidget;
    auto widgetIndex = tabWidget->currentIndex();

    widgetIndex++;
    if (widgetIndex >= tabWidget->count()) {
        auto currentSplitIndex = splitter->indexOf(currentTabWidget);
        currentSplitIndex++;
        if (currentSplitIndex >= splitter->count()) {
            currentSplitIndex = 0;
        }
        tabWidget = qobject_cast<QTabWidget *>(splitter->widget(currentSplitIndex));
        widgetIndex = 0;
    }

    currentTabWidget = tabWidget;
    currentTabWidget->setFocus(Qt::TabFocusReason);
    currentTabWidget->setCurrentIndex(widgetIndex);
    currentTabWidget->widget(widgetIndex)->setFocus();
}

void SplitTabWidget::movePrevTab() {
    auto tabWidget = currentTabWidget;
    auto widgetIndex = tabWidget->currentIndex();

    widgetIndex--;
    if (widgetIndex < 0) {
        auto currentSplitIndex = splitter->indexOf(currentTabWidget);
        currentSplitIndex--;
        if (currentSplitIndex < 0) {
            currentSplitIndex = splitter->count() - 1;
        }
        tabWidget = qobject_cast<QTabWidget *>(splitter->widget(currentSplitIndex));
        widgetIndex = tabWidget->count() - 1;
    }

    currentTabWidget = tabWidget;
    currentTabWidget->setFocus(Qt::TabFocusReason);
    currentTabWidget->setCurrentIndex(widgetIndex);
    currentTabWidget->widget(widgetIndex)->setFocus();
}

QList<int> SplitTabWidget::getSplitInternalCount() const {
    auto l = QList<int>();
    for (auto i = 0; i < splitter->count(); i++) {
        auto w = splitter->widget(i);
        auto t = qobject_cast<QTabWidget *>(w);
        if (t) {
            l.push_back(t->count());
        } else {
            l.push_back(0);
        }
    }
    return l;
}

void SplitTabWidget::setButtonProvider(ButtonsProvider *newProvider) {
    this->buttonsProvider = newProvider;
    // TODO - how do we modify existing tabs?
}

QWidget *SplitTabWidget::getCurrentWidget() {
    if (!currentTabWidget) {
        return nullptr;
    }
    return currentTabWidget->currentWidget();
}

int SplitTabWidget::getWigetsCountInCurrentSplit() const {
    if (!currentTabWidget) {
        return 0;
    }
    return currentTabWidget->count();
}

void SplitTabWidget::onTabFocusChanged(QWidget *widget, bool focused) {
    auto tab = qobject_cast<QTabWidget *>(widget);
    if (tab) {
        if (focused && tab->currentWidget()) {
            if (!tab->currentWidget()->hasFocus()) {
                tab->currentWidget()->setFocus();
            }
            onTabFocusChanged(tab->currentWidget(), true);
        }
    }
    if (widget && focused && !widget->hasFocus()) {
        widget->setFocus();
    }
}

void SplitTabWidget::onNewSplitCreated(QTabWidget *tabWidget, int count) {
    if (buttonsProvider) {
        auto left = buttonsProvider->requestButton(true, count, this);
        auto right = buttonsProvider->requestButton(false, count, this);
        tabWidget->setCornerWidget(left, Qt::TopLeftCorner);
        tabWidget->setCornerWidget(right, Qt::TopRightCorner);
    }

    if (auto tabBar = qobject_cast<DraggableTabBar *>(tabWidget->tabBar())) {
        connect(
            tabBar, &DraggableTabBar::emptyAreaDoubleClicked, this,
            [this, tabWidget](const QPoint &pos) { emit emptyAreaDoubleClicked(tabWidget, pos); });
    }

    connect(tabWidget, &QTabWidget::currentChanged, this, [this, tabWidget](int index) {
        if (index >= 0 && tabWidget == currentTabWidget) {
            onTabFocusChanged(tabWidget->widget(index), true);
        }
    });
}

void SplitTabWidget::onSplitCountMaybeChanged() {
    auto enableDragAndDrop = splitter->count() > 1;
    for (auto i = 0; i < splitter->count(); ++i) {
        auto tabWidget = qobject_cast<DraggableTabWidget *>(splitter->widget(i));
        if (tabWidget) {
            if (auto tabBar = qobject_cast<DraggableTabBar *>(tabWidget->tabBar())) {
                tabBar->setDragAndDropEnabled(enableDragAndDrop);
            }

            if (buttonsProvider) {
                if (auto oldWidget = tabWidget->cornerWidget(Qt::TopLeftCorner)) {
                    oldWidget->deleteLater();
                }
                if (auto oldWidget = tabWidget->cornerWidget(Qt::TopRightCorner)) {
                    oldWidget->deleteLater();
                }

                auto left = buttonsProvider->requestButton(true, i, this);
                auto right = buttonsProvider->requestButton(false, i, this);
                if (left) {
                    left->setParent(tabWidget);
                    left->show();
                }
                if (right) {
                    right->setParent(tabWidget);
                    right->show();
                }
                tabWidget->setCornerWidget(left, Qt::TopLeftCorner);
                tabWidget->setCornerWidget(right, Qt::TopRightCorner);
#if 0
                qDebug() << QString("Adding to widget=%1@left -> %2")
                                .arg(tabWidget->objectName())
                                .arg(left->objectName());
                qDebug() << QString("Adding to widget=%1@right -> %2")
                                .arg(tabWidget->objectName())
                                .arg(right->objectName());
#endif
            }
        }
    }
}
