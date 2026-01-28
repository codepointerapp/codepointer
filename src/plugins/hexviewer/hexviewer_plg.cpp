// SPDX-License-Identifier: MIT

#include "hexviewer_plg.h"
#include "hextextoperationsadapter.h"
#include "qclipboard.h"
#include "widgets/textoperationswidget.h"
#include <QApplication>
#include <QFileInfo>
#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QHexView/qhexview.h>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

class qmdiHexViewer : public QWidget, public qmdiClient {
    Q_OBJECT
  public:
    QString thisFileName;
    QHexView *hexView;
    TextOperationsWidget *operationsWidget;

    qmdiHexViewer(QWidget *p, const QString &fileName) : QWidget(p), qmdiClient() {
        hexView = new QHexView(this);
        operationsWidget = new TextOperationsWidget(this, hexView);
        operationsWidget->setAdapter(new HexTextOperationsAdapter(hexView, operationsWidget));
        operationsWidget->hide();

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(hexView);
        layout->addWidget(operationsWidget);

        auto actionCopyFileName = new QAction(tr("Copy filename to clipboard"), this);
        auto actionCopyFilePath = new QAction(tr("Copy full path to clipboard"), this);
        connect(actionCopyFileName, &QAction::triggered, this, [this]() {
            auto c = QApplication::clipboard();
            c->setText(this->mdiClientFileName());
        });
        connect(actionCopyFilePath, &QAction::triggered, this, [this]() {
            auto c = QApplication::clipboard();
            c->setText(mdiClientName);
        });

        auto fi = QFileInfo(fileName);
        auto document = QHexDocument::fromMappedFile(fileName, hexView);
        hexView->setDocument(document);
        this->mdiClientName = fi.fileName();
        this->thisFileName = fileName;

        hexView->setContextMenuPolicy(Qt::ActionsContextMenu);
        this->contextMenu.addSeparator();
        this->contextMenu.addAction(actionCopyFileName);
        this->contextMenu.addAction(actionCopyFilePath);
        this->contextMenu.addActionsToWidget(hexView);

        setupActions();
    }

    ~qmdiHexViewer() { mdiServer = nullptr; }

    void setupActions() {
        auto actionFind =
            new QAction(QIcon::fromTheme(QIcon::ThemeIcon::EditFind), tr("&Find"), this);
        auto actionReplace =
            new QAction(QIcon::fromTheme("edit-find-replace"), tr("&Replace"), this);
        auto actionCopyBinary =
            new QAction(QIcon::fromTheme("edit-copy"), tr("Copy (binary)"), this);
        auto actionCopyAsHex = new QAction(QIcon::fromTheme("edit-copy"), tr("Copy (HEX)"), this);
        auto actionPastBinary =
            new QAction(QIcon::fromTheme("edit-copy"), tr("Paste (binary)"), this);
        auto actionPastAsHex = new QAction(QIcon::fromTheme("edit-copy"), tr("Paste (hex)"), this);

        actionFind->setShortcut(QKeySequence::Find);
        actionReplace->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
        actionCopyBinary->setShortcut(QKeySequence::Copy);
        actionCopyAsHex->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
        actionPastBinary->setShortcut(QKeySequence::Paste);
        actionPastAsHex->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));

        connect(actionFind, &QAction::triggered, operationsWidget,
                &TextOperationsWidget::showSearch);
        connect(actionReplace, &QAction::triggered, operationsWidget,
                &TextOperationsWidget::showReplace);

        toolbars[tr("main")]->addAction(actionFind);
        toolbars[tr("main")]->addAction(actionReplace);
        this->menus["&Search"]->addAction(actionFind);
        this->menus["&Search"]->addAction(actionReplace);
        this->menus["&Search"]->addAction(actionCopyBinary);
        this->menus["&Search"]->addAction(actionCopyAsHex);
        this->menus["&Search"]->addAction(actionPastBinary);
        this->menus["&Search"]->addAction(actionPastAsHex);
    }

    virtual bool canCloseClient(CloseReason) override {
        if (!hexView->hexDocument()->isModified()) {
            return true;
        }

        QMessageBox msgBox(
            QMessageBox::Warning, mdiClientName,
            tr("The (binary) document has been modified.\nDo you want to save your changes?"),
            QMessageBox::Yes | QMessageBox::Default, this);

        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Yes);

        int ret = msgBox.exec();

        if (ret == QMessageBox::Yes) {
            return doSave();
        } else if (ret == QMessageBox::Cancel) {
            return false;
        }

        hexView->hexDocument()->clearModified();
        return true;
    }

    virtual QString mdiClientFileName() override { return thisFileName; }

    virtual std::optional<std::tuple<int, int, int>> get_coordinates() const override { return {}; }

    bool doSave() {
        QFile file(thisFileName);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox msgBox(QMessageBox::Warning, mdiClientName, tr("Could not save file"),
                               QMessageBox::Ok, this);
            msgBox.exec();
            return false;
        }

        bool success = hexView->hexDocument()->saveTo(&file);
        file.close();
        return success;
    }
};

HexViewrPlugin::HexViewrPlugin() {
    name = tr("Hex viewer plugin");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
}

HexViewrPlugin::~HexViewrPlugin() {}

QStringList HexViewrPlugin::myExtensions() {
    QStringList s;
    return s;
}

int HexViewrPlugin::canOpenFile(const QString &fileName) {
    static const QStringList extensions = {".bin", ".img", "blob", ".so",   ".AppImage",
                                           ".a",   ".exe", ".dll", ".dlib", ".pdf"};

    auto uri = QUrl(fileName);
    auto scheme = uri.scheme();
    if (!scheme.isEmpty() && scheme != "file" && scheme.size() > 1) {
        return -1;
    }
    for (const QString &ext : extensions) {
        if (fileName.endsWith(ext, Qt::CaseInsensitive)) {
            return 6;
        }
    }

    if (!fileName.contains(".")) {
        return 5;
    }
    return 2;
}

qmdiClient *HexViewrPlugin::openFile(const QString &fileName, int, int, int) {
    auto parentWidget = dynamic_cast<QWidget *>(mdiServer);
    auto viewer = new qmdiHexViewer(parentWidget, fileName);
    mdiServer->addClient(viewer);
    return viewer;
}

#include "hexviewer_plg.moc"
