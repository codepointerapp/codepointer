/**
 * \file SearchableMenuButton.hpp
 * \brief Definition of a menu button with search-as-you-type filtering
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>
#include <QToolButton>

class QLineEdit;

class SearchableMenuButton : public QToolButton {
    Q_OBJECT
  public:
    explicit SearchableMenuButton(QWidget *parent = nullptr);

    void setItems(const QStringList &names, const QStringList &tooltips = QStringList());
    void setCurrentIndex(int index);
    int currentIndex() const { return m_currentIndex; }
    int count() const { return m_names.size(); }

    void setFilterPlaceholder(const QString &placeholder);
    void setPreferLastWhenInvalid(bool on);

  signals:
    void itemSelected(int index);

  private:
    void rebuildMenu();
    QString tooltipFor(int index) const;

    QStringList m_names;
    QStringList m_tooltips;
    QString m_emptyText = "...";
    QString m_filterPlaceholder;
    QLineEdit *m_filterLineEdit = nullptr;
    int m_currentIndex = -1;
    bool m_preferLastWhenInvalid = false;
};