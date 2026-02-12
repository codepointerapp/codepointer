/**
 * \file BoldItemDelegate.hpp
 * \brief Definition of the bold item delegate
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QStyledItemDelegate>

/**
 * \class Bold
 * \brief item delegate used for highlighting a specific valud
 *
 * This is a quick and dirty delegate to strap into a QListView or QComboBox
 * (or anything supported) that will display a specific item, at the same font
 * color as the default - but also bold.
 *
 * Can be used to mark the current item.
 */
class BoldItemDelegate : public QStyledItemDelegate {
  public:
    QString boldItemStr = "";
    explicit BoldItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};
