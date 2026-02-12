/**
 * \file BoldItemDelegate.cpp
 * \brief Implementation of the bold item delegate
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QPainter>

#include "widgets/BoldItemDelegate.hpp"

void BoldItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    QString text = index.data(Qt::DisplayRole).toString();
    painter->save();

    bool isSelected = option.state & QStyle::State_Selected;
    if (isSelected) {
        painter->fillRect(option.rect, option.palette.highlight());
        painter->setPen(option.palette.highlightedText().color());
    } else {
        painter->setPen(option.palette.text().color());
    }

    QFont font = painter->font();
    if (text == boldItemStr) {
        font.setBold(true);
    }
    painter->setFont(font);

    // I honestly don't remember why I needed to adjust 4 pixels, but it looks
    // beeter this way.
    QRect textRect = option.rect.adjusted(4, 0, -4, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    painter->restore();
}
