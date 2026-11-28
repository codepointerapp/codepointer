/**
 * \file textpreview.cpp
 * \brief Definition of a a preview widget
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "textpreview.h"
#include "xmltreemodel.h"

#include <QAbstractItemModel>
#include <QHeaderView>
#include <QJsonModel.hpp>
#include <QPainter>
#include <QScrollBar>
#include <QSvgRenderer>
#include <QTextBrowser>
#include <QTreeView>
#include <QVector>
#include <cmark.h>
#include <pal/image-viewer.h>
#include <qlitehtmlwidget.h>

auto css_style = R"CSS(
/* Primary heading */
h1 {
  font-size: 2.5em;
  font-weight: 700;
  color: #2c3e50;
  margin-top: 0;
  margin-bottom: 0.5em;
  padding-bottom: 0.3em;
  border-bottom: 2px solid #eaecef;
}

/* Secondary heading */
h2 {
  font-size: 2em;
  font-weight: 600;
  color: #34495e;
  margin-top: 1.5em;
  margin-bottom: 0.5em;
  padding-bottom: 0.3em;
  border-bottom: 1px solid #eaecef;
}

/* Tertiary heading */
h3 {
  font-size: 1.5em;
  font-weight: 600;
  color: #34495e;
  margin-top: 1.5em;
  margin-bottom: 0.5em;
}
p {
  line-height: 1.6;
  color: #34495e;
}
)CSS";

// This is a C++11 function definition, with trailing return value.
// lets open this in qtcreator and debug. Not cool.
auto static NullHandler(QUrl) -> QByteArray { return {}; }

TextPreview::TextPreview(QWidget *p) : QStackedWidget(p) {
    markdownPreview = new QLiteHtmlWidget(this);
    markdownPreview->setResourceHandler(NullHandler);
    // markdownPreview->setOpenExternalLinks(true);

    imagePreview = new pal::ImageViewer(this);
    treeView = new QTreeView(this);
    imagePreview->zoomOriginal();
    treeView->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

    addWidget(markdownPreview);
    addWidget(imagePreview);
    addWidget(treeView);
}

auto TextPreview::previewText(const QString &filename, const QString &str, PreviewType type)
    -> void {
    switch (type) {
    case Markdown: {
        auto scrollBar = markdownPreview->verticalScrollBar();
        auto scrollPosition = scrollBar->value();
        setCurrentIndex(0);

        // FIXME: we need to set the source of the document, to find images
        // markdownPreview->setSource(filename, QTextDocument::MarkdownResource);

        auto s = str.toStdString();
        auto markHtml =
            cmark_markdown_to_html(s.data(), s.length(), CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE);
        auto document = QString("<html><head><style>%1</style></head><body>%2</body></html>")
                            .arg(css_style, markHtml);
        markdownPreview->setHtml(document);
        free(markHtml);

        scrollBar->setValue(scrollPosition);
        break;
    }
    case SVG: {
        setCurrentIndex(1);
        QSvgRenderer renderer(str.toUtf8());
        QPixmap pixmap(renderer.defaultSize());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        imagePreview->setImage(pixmap.toImage());
        break;
    }
    case XPM: {
        setCurrentIndex(1);
        QPixmap pixmap;
        pixmap.loadFromData(str.toUtf8());
        imagePreview->setImage(pixmap.toImage());
        break;
    }
    case JSON: {
        setCurrentIndex(2);
        auto model = new QJsonModel(this);
        if (!str.isEmpty()) {
            model->loadJson(str.toUtf8());
        }
        treeView->setModel(model);
        treeView->expandAll();
        treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        break;
    }
    case XML:
        setCurrentIndex(2);
        auto model = new XmlTreeModel(str, this);
        treeView->setModel(model);
        treeView->expandAll();
        treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        break;
    }
}
