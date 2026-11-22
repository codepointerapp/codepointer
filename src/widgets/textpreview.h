/**
 * \file textpreview.h
 * \brief Definition of a preview widget
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractItemModel>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QIcon>
#include <QStackedWidget>

class QLiteHtmlWidget;
class QLabel;
class QTreeView;

namespace pal {
class ImageViewer;
}

class TextPreview : public QStackedWidget {
  public:
    explicit TextPreview(QWidget *p);
    enum PreviewType {
        Markdown,
        SVG,
        XPM,
        JSON,
        XML,
        // YML
        // TOML
    };

    auto previewText(const QString &filename, const QString &str, PreviewType type) -> void;

  private:
    QLiteHtmlWidget *markdownPreview = nullptr;
    pal::ImageViewer *imagePreview = nullptr;
    QTreeView *treeView = nullptr;
};
