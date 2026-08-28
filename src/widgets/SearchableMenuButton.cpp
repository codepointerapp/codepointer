/**
 * \file SearchableMenuButton.cpp
 * \brief Implementation of a menu button with search-as-you-type filtering
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "SearchableMenuButton.hpp"

#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QWidgetAction>

#include <algorithm>
#include <utility>

SearchableMenuButton::SearchableMenuButton(QWidget *parent) : QToolButton(parent) {
    setPopupMode(QToolButton::MenuButtonPopup);
    setAutoRaise(false);

    connect(this, &QToolButton::clicked, this, [this]() {
        if (menu()) {
            showMenu();
        }
    });
}

void SearchableMenuButton::setItems(const QStringList &names, const QStringList &tooltips) {
    m_names = names;
    m_tooltips = (tooltips.size() == names.size()) ? tooltips : QStringList();
    rebuildMenu();
}

void SearchableMenuButton::setCurrentIndex(int index) {
    if (index < 0 || index >= m_names.size()) {
        m_currentIndex = index;
        rebuildMenu();
        return;
    }
    if (m_currentIndex != index) {
        m_currentIndex = index;
        setText(m_names.at(index));
        setToolTip(tooltipFor(index));
    }
}

void SearchableMenuButton::setFilterPlaceholder(const QString &placeholder) {
    m_filterPlaceholder = placeholder;
}

void SearchableMenuButton::setPreferLastWhenInvalid(bool on) { m_preferLastWhenInvalid = on; }

QString SearchableMenuButton::tooltipFor(int index) const {
    return (index >= 0 && index < m_tooltips.size()) ? m_tooltips.at(index) : m_names.at(index);
}

void SearchableMenuButton::rebuildMenu() {
    if (auto previous = menu()) {
        previous->deleteLater();
        setMenu(nullptr);
        m_filterLineEdit = nullptr;
    }

    setEnabled(!m_names.isEmpty());
    if (m_names.isEmpty()) {
        m_currentIndex = -1;
        setText(m_emptyText);
        setToolTip(QString());
        return;
    }
    if (m_currentIndex < 0 || m_currentIndex >= m_names.size()) {
        m_currentIndex = m_preferLastWhenInvalid ? m_names.size() - 1 : 0;
    }

    setText(m_names.at(m_currentIndex));
    setToolTip(tooltipFor(m_currentIndex));

    if (m_names.size() < 2) {
        return;
    }

    auto searchMenu = new QMenu(this);
    auto filterLineEdit = new QLineEdit(searchMenu);
    filterLineEdit->setPlaceholderText(m_filterPlaceholder);
    filterLineEdit->setClearButtonEnabled(true);
    m_filterLineEdit = filterLineEdit;

    auto filterAction = new QWidgetAction(searchMenu);
    filterAction->setDefaultWidget(filterLineEdit);
    searchMenu->addAction(filterAction);

    QList<QAction *> actions;
    for (const auto &name : std::as_const(m_names)) {
        auto action = new QAction(name, searchMenu);
        searchMenu->addAction(action);
        actions.append(action);
    }

    connect(filterLineEdit, &QLineEdit::textChanged, searchMenu,
            [actions = actions](const QString &text) {
                for (auto action : actions) {
                    action->setVisible(action->text().contains(text, Qt::CaseInsensitive));
                }
            });

    connect(searchMenu, &QMenu::aboutToShow, this, [filterLineEdit]() {
        filterLineEdit->clear();
        // The popup steals focus from the edit box while being shown, so grab it
        // back as soon as the menu is on screen.
        QTimer::singleShot(0, filterLineEdit, [filterLineEdit]() {
            filterLineEdit->setFocus(Qt::PopupFocusReason);
            filterLineEdit->selectAll();
        });
    });

    connect(searchMenu, &QMenu::triggered, this, [this, actions = actions](QAction *action) {
        auto it = std::find(actions.cbegin(), actions.cend(), action);
        if (it == actions.cend()) {
            return;
        }
        auto newIndex = static_cast<int>(std::distance(actions.cbegin(), it));
        m_currentIndex = newIndex;
        setText(m_names.at(newIndex));
        setToolTip(tooltipFor(newIndex));
        emit itemSelected(newIndex);
    });

    setMenu(searchMenu);
}