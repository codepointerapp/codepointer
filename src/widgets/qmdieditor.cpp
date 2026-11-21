/**
 * \file qmdieditor.cpp
 * \brief Implementation of the qmdiEditor
 * \author Diego Iastrubni diegoiast@gmail.com
 * \see Qutepart qmdiClient
 */

// SPDX-License-Identifier: MIT

#include <algorithm>

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFuture>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>

#include <QPromise>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolBar>
#include <QToolTip>
#include <QTreeView>
#include <QtConcurrent>

#include <QToolTip>
#include <pluginmanager.h>
#include <qmdiserver.h>

#include "GlobalCommands.hpp"
#include "widgets/BoldItemDelegate.hpp"
#include "widgets/codeeditorfactory.h"
#include "widgets/qmdieditor.h"
#include "widgets/textoperationswidget.h"
#include "widgets/textpreview.h"
#include "widgets/ui_bannermessage.h"

#if defined(WIN32)
#define PLATFORM_LINE_ENDING "\r\n"
#else
#define PLATFORM_LINE_ENDING "\n"
#endif

auto static const cExtensions = QStringList{"c", "cpp", "cxx", "cc", "c++", "m", "mm"};
auto static const headerExtensions = QStringList{"h", "hpp", "hh"};

auto static getCorrespondingFile(PluginManager *manager, const QString &fileName)
    -> QFuture<QString> {

    // First - choose easy solution, file aside the original one
    auto fileInfo = QFileInfo(fileName);
    auto baseName = fileInfo.baseName();
    auto dirPath = fileInfo.absolutePath();

    if (cExtensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
        for (const auto &headerExt : headerExtensions) {
            auto headerFileName = dirPath + QDir::separator() + baseName + "." + headerExt;
            if (QFileInfo::exists(headerFileName)) {
                return QtFuture::makeReadyValueFuture(headerFileName);
            }
        }
    } else if (headerExtensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
        for (const auto &cExt : cExtensions) {
            auto cFileName = dirPath + QDir::separator() + baseName + "." + cExt;
            if (QFileInfo::exists(cFileName)) {
                return QtFuture::makeReadyValueFuture(cFileName);
            }
        }
    }

    // Did not work, lets ask someone - find me the corresponding file
    // This should be the project manager replying to this, from currently
    // active project. Or maybe from all active projects.
    return manager
        ->handleCommandAsync(GlobalCommands::FindMatchingFile,
                             {
                                 {GlobalArguments::FileName, fileName},
                             })
        .then([](const CommandArgs &args) {
            return args.value(GlobalArguments::FileName).toString();
        });
}

auto static runningUnderGnome() -> bool {
#if defined(WIN32)
    return false;
#else
    std::string desktopEnv =
        std::getenv("XDG_CURRENT_DESKTOP") ? std::getenv("XDG_CURRENT_DESKTOP") : "";

    return desktopEnv.find("GNOME") != std::string::npos;
#endif
}

auto static getLineEnding(QIODevice &stream, const QString &defaultLineEnding) -> QString {
    if (stream.atEnd()) {
        return defaultLineEnding;
    }

    auto ending = QString{};
    auto pos = stream.pos();
    while (!stream.atEnd()) {
        QChar ch = stream.read(1).at(0);
        if (ch == '\r' || ch == '\n') {
            ending += ch;
            if (!stream.atEnd()) {
                ch = stream.read(1).at(0);
                if (ch == '\r' || ch == '\n') {
                    if (ch != ending[0]) {
                        ending += ch;
                    }
                }
            }
            break;
        }
    }
    stream.seek(pos);
    return ending;
}

static auto addressFromSymbolTag(const QVariantHash &tag) -> QString {
    static auto const START_MARKER = QString("/^");
    static auto const END_MARKER = QString("$/;\"");
    static auto const MIN_LENGTH = START_MARKER.length() + END_MARKER.length();

    auto address = tag[GlobalArguments::Raw].toString();
    if (address.startsWith(START_MARKER) && address.endsWith(END_MARKER) &&
        address.length() > MIN_LENGTH) {
        address = address.mid(START_MARKER.length(), address.length() - MIN_LENGTH);
    }
    return address;
}

static auto openSymbolTag(const QVariantHash &tag, PluginManager *manager) -> void {
    if (!manager) {
        return;
    }
    auto const fileName = tag[GlobalArguments::FileName].toString();
    if (fileName.isEmpty()) {
        return;
    }

    auto const nativeFileName = QDir::toNativeSeparators(fileName);
    manager->openFile(nativeFileName);
    auto const editor = dynamic_cast<qmdiEditor *>(manager->clientForFileName(nativeFileName));
    if (!editor) {
        return;
    }

    editor->loadContent(true);
    auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();
    if (lineNumber > 0) {
        auto const columnNumber = tag[GlobalArguments::ColumnNumber].toInt();
        editor->goTo(columnNumber > 0 ? columnNumber - 1 : 0, lineNumber - 1);
    } else {
        auto const address = addressFromSymbolTag(tag);
        if (!address.isEmpty()) {
            editor->findText(address);
        }
    }
    editor->setFocus();
}

static auto createSubFollowSymbolSubmenu(const CommandArgs &data, QMenu *menu,
                                         PluginManager *manager) -> void {
    if (!data.contains(GlobalArguments::Tags)) {
        return;
    }

    auto tags = data[GlobalArguments::Tags].toList();
    auto originalSymbol = data[GlobalArguments::Symbol].toString();
    {
        auto a = new QAction(originalSymbol, menu);
        a->setEnabled(false);

        if (tags.isEmpty()) {
            a->setText(QObject::tr("%1 - not found").arg(originalSymbol));
            menu->addAction(a);
            return;
        }
        menu->addAction(a);
    }

    QSet<QString> addedItems;
    QVariantList definitions;
    QVariantList otherTags;

    auto isCpp = false;
    for (const QVariant &item : std::as_const(tags)) {
        auto const tag = item.toHash();
        auto fileName = tag[GlobalArguments::FileName].toString().toLower();
        if (fileName.endsWith(".cpp") || fileName.endsWith(".c") || fileName.endsWith(".hpp") ||
            fileName.endsWith(".h") || fileName.endsWith(".cc") || fileName.endsWith(".hh") ||
            fileName.endsWith(".cxx") || fileName.endsWith(".hxx")) {
            isCpp = true;
        }

        if (tag[GlobalArguments::IsDefinition].toBool()) {
            definitions.append(item);
        } else {
            otherTags.append(item);
        }
    }

    // Show both definitions and declarations (e.g. .h and .cpp) only for C/C++.
    // For other languages, prioritize definitions if they exist.
    const auto &tagsToShow = isCpp ? tags : (definitions.isEmpty() ? otherTags : definitions);

    for (const QVariant &item : std::as_const(tagsToShow)) {
        auto const tag = item.toHash();
        auto const fileName = tag[GlobalArguments::FileName].toString();
        auto const fieldType = tag[GlobalArguments::Type].toString();
        auto const fieldValue = tag[GlobalArguments::Value].toString();
        auto const address = addressFromSymbolTag(tag);
        auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();

        QString itemKey;
        if (lineNumber > 0) {
            itemKey = QString("%1:%2").arg(fileName).arg(lineNumber);
        } else {
            itemKey = QString("%1:%2").arg(fileName).arg(address);
        }

        if (addedItems.contains(itemKey)) {
            continue;
        }
        addedItems.insert(itemKey);

        auto fi = QFileInfo(fileName);
        auto simpleFileName = fi.fileName();
        auto title = QString();
        if (lineNumber > 0) {
            title = QString("%1:%2 - %3 %4")
                        .arg(simpleFileName)
                        .arg(lineNumber)
                        .arg(fieldType, fieldValue);
        } else {
            title = QString("%1 - %2 %3").arg(simpleFileName, fieldType, fieldValue);
        }

        auto a = new QAction(title, menu);
        QObject::connect(a, &QAction::triggered, a,
                         [tag, manager]() { openSymbolTag(tag, manager); });
        menu->addAction(a);
    }
}

class EditorStateGuard {
  public:
    explicit EditorStateGuard(CodeEditorInterface *editor) : m_editor(editor) {
        if (m_editor) {
            m_state = m_editor->cursorState();
        }
    }

    ~EditorStateGuard() { restore(); }

    void restore() {
        if (!m_editor || m_restored) {
            return;
        }
        m_restored = true;
        m_editor->setCursorState(m_state);
    }

    EditorStateGuard(const EditorStateGuard &) = delete;
    EditorStateGuard &operator=(const EditorStateGuard &) = delete;

  private:
    CodeEditorInterface *m_editor = nullptr;
    CodeEditor::CursorState m_state;
    bool m_restored = false;
};

qmdiEditor::qmdiEditor(QWidget *p, CodeEditorFactory *factory)
    : QWidget(p), editorFactory(factory) {
    textEditor = editorFactory->createEditor(this);
    operationsWidget = new TextOperationsWidget(this, textEditor.get());
    mdiClientName = tr("NO NAME");
    fileSystemWatcher = new QFileSystemWatcher(this);
    toolbar = new QWidget(this);
    auto layout2 = new QHBoxLayout(toolbar);
    auto layout = new QVBoxLayout(this);

    // Set up completion callback
    textEditor->setCompletionCallback(
        [this](const QString &prefix, const QString &previousWord, const QString &separator) {
            if (prefix.length() < 2 && separator.isEmpty()) {
                return QFuture<QSet<CodeEditor::CompletionItem>>();
            }
            return this->getTagCompletions(prefix, previousWord, separator);
        });

    operationsWidget->hide();
    setupActions();
    layout2->setSpacing(0);
    layout2->setContentsMargins(0, 2, 0, 2);
    toolbar->setLayout(layout2);
    toolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    toolbar->layout()->addWidget(comboChangeHighlighter);
    toolbar->layout()->addWidget(buttonChangeIndenter);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    QWidget *spacer = new QWidget(this);
    QLabel *staticLabel = new QLabel("", this);

    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar->layout()->addWidget(spacer);
    toolbar->layout()->addWidget(staticLabel);
    toolbar->layout()->addWidget(previewButton);

    textPreview = new TextPreview(this);
    textPreview->setVisible(false);

    textEditor->setFrameVisible(false);
    splitter->addWidget(textEditor->widget());
    splitter->addWidget(textPreview);

    // These are set to *F2 on the library, which we want to use for rename
    textEditor->action(CodeEditor::Action::PreviousBookmark)->setShortcut({});
    textEditor->action(CodeEditor::Action::NextBookmark)->setShortcut({});

    textEditor->setCursorPositionChangedCallback([this, staticLabel]() {
        staticLabel->setText(
            QString("%1:%2").arg(textEditor->cursorLine() + 1).arg(textEditor->cursorColumn() + 1));
        maybeScheduleSignatureHelp(false);
        setCodeActionMark(-1, false);
        codeActionTimer->start();
    });

    // LSP signature help: qutepart hides the call tip on any caret move or text
    // edit, so retrigger while the caret stays inside a call's argument list and
    // re-show the tip once the server answers.
    signatureHelpTimer = new QTimer(this);
    signatureHelpTimer->setSingleShot(true);
    signatureHelpTimer->setInterval(400);
    connect(signatureHelpTimer, &QTimer::timeout, this, &qmdiEditor::requestSignatureHelp);
    connect(&signatureHelpWatcher, &QFutureWatcher<CommandArgs>::finished, this, [this]() {
        if (signatureHelpWatcher.isCanceled()) {
            return;
        }
        auto tooltip = signatureHelpWatcher.result()[GlobalArguments::Tooltip].toString();
        // The tip is hidden here and on cursor moves by qutepart; re-check the
        // caret to make sure a late reply does not pop a tip outside a call.
        if (tooltip.isEmpty() || !isInsideCall()) {
            signatureHelpActive = false;
            textEditor->hideCallTip();
            return;
        }
        signatureHelpActive = true;
        if (textEditor->isCallTipVisible()) {
            textEditor->updateCallTip(tooltip);
        } else {
            textEditor->showCallTip(tooltip);
        }
    });
    // The availability probe. The mark is dropped the instant the caret moves
    // rather than waiting out the debounce: a hint that trails the cursor is
    // worse than no hint, and it points at a line the user has already left.
    codeActionTimer = new QTimer(this);
    codeActionTimer->setSingleShot(true);
    codeActionTimer->setInterval(250);
    connect(codeActionTimer, &QTimer::timeout, this, &qmdiEditor::probeCodeActions);
    // Clicking the mark is the same gesture as the shortcut: open the menu. The
    // line is ignored - the caret is already where the mark is, and a right-click
    // style move would fight qutepart's own click handling.
    textEditor->setCodeActionCallback([this](int) { requestCodeActions(); });
    textEditor->setTextChangedCallback([this]() {
        maybeScheduleSignatureHelp(true);
        setCodeActionMark(-1, false);
        codeActionTimer->start();
        onTextModified();
        autoSaveTimer->start();
    });

    // The reply arrives on a reader thread; the watcher brings it back here
    // before any menu is built.
    connect(&codeActionWatcher, &QFutureWatcher<CommandArgs>::finished, this, [this]() {
        if (codeActionWatcher.isCanceled()) {
            return;
        }
        auto const result = codeActionWatcher.result();
        auto const actions = result[GlobalArguments::Actions].toList();
        if (codeActionProbe) {
            // A probe, not a request: only the marker moves, no menu. The caret
            // may have moved while the server thought about it, so the mark goes
            // on the line we asked about rather than wherever the caret ended up.
            // The plugin already drops what it cannot run - command-shaped,
            // resolveable, or empty - so anything left is applicable.
            setCodeActionMark(codeActionProbeLine, !actions.isEmpty());
            return;
        }
        qDebug() << "qmdiEditor: code action reply with" << actions.size() << "action(s)";
        showCodeActionMenu(result);
    });

    connect(fileSystemWatcher, &QFileSystemWatcher::fileChanged, this, &qmdiEditor::on_fileChanged);
    fileModifications = true;

    auto fnt = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fnt.setFixedPitch(true);
    fnt.setPointSize(14);
    setEditorFont(fnt);

    textEditor->setLineWrapMode(CodeEditor::WrapMode::NoWrap);

    auto reformatCodeAction = new QAction(tr("Reformat code"), this);
    connect(reformatCodeAction, &QAction::triggered, this, &qmdiEditor::reformatContent);

    banner = new QWidget(this);
    banner->hide();
    banner->setObjectName("banner");
    ui_banner = new Ui::BannerMessage;
    ui_banner->setupUi(banner);
    connect(ui_banner->label, &QLabel::linkActivated, this, &qmdiEditor::fileMessage_clicked);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(banner);
    layout->addWidget(splitter);
    layout->addWidget(operationsWidget);

    textOperationsMenu = new QMenu(tr("Text actions"), this);
    textOperationsMenu->setObjectName("qmdiEditor::textOperationsMenu");

    codeActionsSubmenu = new QMenu(tr("Code actions"), this);
    codeActionsSubmenu->setObjectName("qmdiEditor::codeActionsSubmenu");
    codeActionsSubmenu->addAction(actionFollowSymbol);
    codeActionsSubmenu->addAction(actionFindReferences);
    codeActionsSubmenu->addAction(actionRenameSymbol);
    codeActionsSubmenu->addSeparator();
    codeActionsSubmenu->addAction(actionCodeActions);
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::DeleteLine));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::CutLine));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::CopyLine));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::PasteLine));
    textOperationsMenu->addSeparator();
    textOperationsMenu->addAction(actionCapitalize);
    textOperationsMenu->addAction(actionLowerCase);
    textOperationsMenu->addAction(actionChangeCase);
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::JoinLines));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::DuplicateSelection));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::DeleteLine));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::MoveLineUp));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::MoveLineDown));
    textOperationsMenu->addAction(textEditor->action(CodeEditor::Action::ToggleComment));
    textOperationsMenu->addSeparator();
    textOperationsMenu->addAction(reformatCodeAction);
    textOperationsMenu->addAction(actionToggleHeader);

    bookmarksMenu = new QMenu(tr("Bookmarks"), this);
    bookmarksMenu->setObjectName("qmdiEditor::bookmarksMenu");
    bookmarksMenu->addAction(textEditor->action(CodeEditor::Action::ToggleBookmark));
    bookmarksMenu->addSeparator();
    bookmarksMenu->addAction(textEditor->action(CodeEditor::Action::NextBookmark));
    bookmarksMenu->addAction(textEditor->action(CodeEditor::Action::PreviousBookmark));

    foldingMenu = new QMenu(tr("Folding"), this);
    foldingMenu->setObjectName("foldingMenu");
    foldingMenu->addAction(textEditor->action(CodeEditor::Action::Fold));
    foldingMenu->addAction(textEditor->action(CodeEditor::Action::Unfold));
    foldingMenu->addAction(textEditor->action(CodeEditor::Action::FoldTopLevel));
    foldingMenu->addAction(textEditor->action(CodeEditor::Action::UnfoldAll));
    foldingMenu->addAction(textEditor->action(CodeEditor::Action::ToggleFold));

    this->menus["&File"]->addAction(actionSave);
    this->menus["&File"]->addAction(actionSaveAs);
    this->menus["&Edit"]->addAction(actionUndo);
    this->menus["&Edit"]->addAction(actionRedo);
    this->menus["&Edit"]->addSeparator();
    this->menus["&Edit"]->addAction(actionCopy);
    this->menus["&Edit"]->addAction(actionCut);
    this->menus["&Edit"]->addAction(actionPaste);
    this->menus["&Edit"]->addSeparator();
    this->menus["&Edit"]->addMenu(textOperationsMenu);
    this->menus["&Edit"]->addMenu(codeActionsSubmenu);
    this->menus["&Edit"]->addMenu(bookmarksMenu);
    this->menus["&Edit"]->addMenu(foldingMenu);
    this->menus["&Edit"]->addAction(actionTogglePreview);
    this->menus["&Edit"]->addAction(textEditor->action(CodeEditor::Action::FindMatchingBracket));
    this->menus["&Edit"]->addAction(reformatCodeAction);

    this->menus["&Search"]->addAction(actionFind);
    this->menus["&Search"]->addAction(actionFindNext);
    this->menus["&Search"]->addAction(actionFindPrev);
    this->menus["&Search"]->addSeparator();
    this->menus["&Search"]->addAction(actionReplace);
    this->menus["&Search"]->addSeparator();
    this->menus["&Search"]->addAction(actionGotoLine);

    this->toolbars[tr("main")]->addSeparator();
    this->toolbars[tr("main")]->addAction(actionSave);
    this->toolbars[tr("main")]->addAction(actionFind);
    this->toolbars[tr("main")]->addAction(actionReplace);
    this->toolbars[tr("main")]->addAction(actionGotoLine);
    this->toolbars[tr("main")]->addAction(actionFindPrev);
    this->toolbars[tr("main")]->addAction(actionFindNext);

    this->contextMenu.addSeparator();
    this->contextMenu.addAction(actionCopyFileName);
    this->contextMenu.addAction(actionCopyFilePath);

    this->installEventFilter(this);
    this->textEditor->widget()->setMouseTracking(true);
    this->textEditor->viewport()->installEventFilter(this);

    this->originalLineEnding = PLATFORM_LINE_ENDING;

    autoSaveTimer = new QTimer(this);
    autoSaveTimer->setSingleShot(true);
    autoSaveTimer->setInterval(5000);
    connect(autoSaveTimer, &QTimer::timeout, this, &qmdiEditor::autoSave);

    // Why a timer for on change?
    // If we issue a "git checkout" - we get notification when git resets the
    // file, we read - and get a "0" sized file. Then later on git would write
    // the correct content - and we get no new notification.
    // Now we read an empty file.
    fileChangedTimer = new QTimer(this);
    fileChangedTimer->setSingleShot(true);
    fileChangedTimer->setInterval(200);
    connect(fileChangedTimer, &QTimer::timeout, this, [this]() {
        if (!fileModifications || fileName.isEmpty()) {
            return;
        }

        auto isModified = textEditor->isModified();
        if (!isModified) {
            reload();
            return;
        }

        auto f = QFileInfo(fileName);
        auto message = QString();
        if (f.exists()) {
            message = QString("%1 <a href=':reload' title='%2'>%3</a>")
                          .arg(tr("File has been modified outside the editor"),
                               tr("Clicking this links will revert all changes to this editor"),
                               tr("Click here to reload"));
        } else {
            message = tr("File has been deleted outside the editor.");
        }
        displayBannerMessage(message, -1);
    });

    // Generate random UID - this is used for namking the backup file.
    QByteArray randomData;
    for (auto i = 0; i < 16; ++i) {
        randomData.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    auto hash = QCryptographicHash::hash(randomData, QCryptographicHash::Sha1).toHex();
    uid = QString::fromLatin1(hash);
}

qmdiEditor::~qmdiEditor() {
    // The editor widget is a child of this QWidget, but the interface that owns
    // it is a member destroyed before QWidget's destructor runs. Drop the event
    // filters first, otherwise the child's destruction can re-enter eventFilter()
    // and call through the already destroyed textEditor.
    if (textEditor) {
        textEditor->viewport()->removeEventFilter(this);
    }
    if (renameEdit) {
        renameEdit->removeEventFilter(this);
    }
    removeEventFilter(this);

    bookmarksMenu->deleteLater();
    textOperationsMenu->deleteLater();
    mdiServer = nullptr;
}

bool qmdiEditor::saveClientContent() {
    if (textEditor->isReadOnly()) {
        return true;
    }
    if (!textEditor->isModified()) {
        return true;
    }
    deleteBackup();
    return doSave();
}

bool qmdiEditor::reloadClientContent() {
    if (textEditor->isReadOnly()) {
        return true;
    }
    if (!textEditor->isModified()) {
        return true;
    }

    auto msgBox = QMessageBox(QMessageBox::Warning, mdiClientName,
                              tr("The document has been modified.\nDo you want to reload it?"),
                              QMessageBox::Yes | QMessageBox::Default, this);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    auto ret = msgBox.exec();
    if (ret == QMessageBox::Yes) {
        reload();
    }
    return false;
}

bool qmdiEditor::canCloseClient(CloseReason reason) {
    if (textEditor->isReadOnly()) {
        saveBackup();
        return true;
    }

    if (!textEditor->isModified()) {
        deleteBackup();
        return true;
    }

    if (reason == CloseReason::ApplicationQuit) {
        if (textEditor->isModified()) {
            saveBackup();
        }
        return true;
    }

    auto msgBox =
        QMessageBox(QMessageBox::Warning, mdiClientName,
                    tr("The document has been modified.\nDo you want to save your changes?"),
                    QMessageBox::Yes | QMessageBox::Default, this);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);

    auto ret = msgBox.exec();
    if (ret == QMessageBox::Yes) {
        return doSave();
    } else if (ret == QMessageBox::Cancel) {
        return false;
    }
    deleteBackup();
    return true;
}

QString qmdiEditor::mdiClientFileName() { return fileName; }

QString qmdiEditor::getShortFileName() {
    if (fileName.isEmpty()) {
        return tr("NO NAME");
    }

    // the name of the object for it's mdi server
    // is the file name alone, without the directory
    auto i = fileName.lastIndexOf('/');
    auto s = QString();
    if (i != -1) {
        s = fileName.mid(i + 1);
    } else {
        i = fileName.lastIndexOf('\\');
        if (i != -1) {
            s = fileName.mid(i + 1);
        } else {
            s = fileName;
        }
    }
    return s;
}

static auto createSubReferencesSubmenu(const CommandArgs &data, QMenu *menu, PluginManager *manager)
    -> void {
    static auto const MAX_PER_FILE = 10;
    static auto const MAX_TOTAL = 50;

    if (!data.contains(GlobalArguments::Tags)) {
        return;
    }

    auto const symbol = data[GlobalArguments::Symbol].toString();
    auto const tags = data[GlobalArguments::Tags].toList();

    if (tags.isEmpty()) {
        auto const none = new QAction(QObject::tr("%1 - no references found").arg(symbol), menu);
        none->setEnabled(false);
        menu->addAction(none);
        return;
    }

    auto order = QStringList();
    auto byFile = QHash<QString, QVariantList>();
    for (auto const &item : tags) {
        auto const fileName = item.toHash()[GlobalArguments::FileName].toString();
        if (!byFile.contains(fileName)) {
            order.append(fileName);
        }
        byFile[fileName].append(item);
    }

    auto groups = std::vector<std::pair<QString, QVariantList>>();
    auto everyUnique = QVariantList();
    for (auto const &fileName : std::as_const(order)) {
        auto seen = QSet<QString>();
        auto unique = QVariantList();
        for (auto const &item : byFile.value(fileName)) {
            auto const tag = item.toHash();
            auto const key = QStringLiteral("%1:%2")
                                 .arg(tag[GlobalArguments::LineNumber].toInt())
                                 .arg(tag[GlobalArguments::ColumnNumber].toInt());
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            unique.append(item);
        }
        groups.emplace_back(fileName, unique);
        everyUnique.append(unique);
    }

    auto number = 0;
    auto shownTotal = 0;
    for (auto const &[fileName, unique] : groups) {
        if (shownTotal >= MAX_TOTAL) {
            continue;
        }

        auto const shown = std::min(static_cast<int>(unique.size()), MAX_PER_FILE);
        for (auto i = 0; i < shown; ++i) {
            number++;
            shownTotal++;

            auto const tag = unique.at(i).toHash();
            auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();
            auto const columnNumber = tag[GlobalArguments::ColumnNumber].toInt();
            auto const simpleFileName = QFileInfo(fileName).fileName();
            auto const title = QStringLiteral("%1. %2:%3 - %4")
                                   .arg(number)
                                   .arg(simpleFileName)
                                   .arg(lineNumber)
                                   .arg(symbol);

            auto const action = new QAction(title, menu);
            QObject::connect(action, &QAction::triggered, action,
                             [fileName, lineNumber, columnNumber, manager]() {
                                 auto const nativeFileName = QDir::toNativeSeparators(fileName);
                                 manager->openFile(nativeFileName);
                                 auto const client = manager->clientForFileName(nativeFileName);
                                 auto const editor = dynamic_cast<qmdiEditor *>(client);
                                 if (!editor) {
                                     return;
                                 }
                                 editor->loadContent(true);
                                 editor->goTo(columnNumber > 0 ? columnNumber - 1 : 0,
                                              lineNumber > 0 ? lineNumber - 1 : 0);
                                 editor->setFocus();
                             });
            menu->addAction(action);
        }
    }

    auto const separator = new QAction(menu);
    separator->setSeparator(true);
    menu->addAction(separator);

    auto const all = new QAction(QObject::tr("Show all results in a panel"), menu);
    QObject::connect(all, &QAction::triggered, menu, [symbol, everyUnique, manager]() {
        if (!manager) {
            return;
        }
        manager->handleCommandAsync(
            GlobalCommands::ShowReferences,
            {{GlobalArguments::Symbol, symbol}, {GlobalArguments::Tags, everyUnique}});
    });
    menu->addAction(all);
}

void qmdiEditor::showRenameSymbol() {
    auto cursorRect = textEditor->cursorRect(textEditor->cursorPosition());
    requestRename(cursorRect.center());
}

void qmdiEditor::showReferencesMenu() {
    auto cursorRect = textEditor->cursorRect(textEditor->cursorPosition());
    auto localPosition = cursorRect.bottomLeft();
    auto future = getReferencesForCurrentWord(localPosition);
    auto watcher = new QFutureWatcher<CommandArgs>(this);

    connect(watcher, &QFutureWatcher<CommandArgs>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (watcher->isCanceled()) {
            return;
        }

        auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
        if (!pluginManager) {
            return;
        }

        auto const res = watcher->result();
        auto const symbol = res[GlobalArguments::Symbol].toString();
        auto const tags = res[GlobalArguments::Tags].toList();
        auto order = QStringList();
        auto byFile = QHash<QString, QVariantList>();
        auto everyUnique = QVariantList();
        for (auto const &item : tags) {
            auto const fileName = item.toHash()[GlobalArguments::FileName].toString();
            if (!byFile.contains(fileName)) {
                order.append(fileName);
            }
            byFile[fileName].append(item);
        }

        for (auto const &fileName : std::as_const(order)) {
            auto seen = QSet<QString>();
            for (auto const &item : byFile.value(fileName)) {
                auto const tag = item.toHash();
                auto const key = QStringLiteral("%1:%2")
                                     .arg(tag[GlobalArguments::LineNumber].toInt())
                                     .arg(tag[GlobalArguments::ColumnNumber].toInt());
                if (seen.contains(key)) {
                    continue;
                }
                seen.insert(key);
                everyUnique.append(item);
            }
        }

        pluginManager->handleCommandAsync(
            GlobalCommands::ShowReferences,
            {{GlobalArguments::Symbol, symbol}, {GlobalArguments::Tags, everyUnique}});
    });
    watcher->setFuture(future);
}

void qmdiEditor::showFollowSymbolMenu() {
    auto cursorRect = textEditor->cursorRect(textEditor->cursorPosition());
    auto localPosition = cursorRect.bottomLeft();
    auto globalPosition = textEditor->viewport()->mapToGlobal(localPosition);

    auto future = getSuggestionsForCurrentWord(localPosition);
    auto watcher = new QFutureWatcher<CommandArgs>(this);
    auto safeWatcher = QPointer(watcher);
    connect(watcher, &QFutureWatcher<CommandArgs>::finished, this, [=, this]() {
        watcher->deleteLater();
        if (!safeWatcher || safeWatcher.isNull() || safeWatcher->isCanceled()) {
            return;
        }

        auto res = safeWatcher->result();
        auto pluginManager =
            dynamic_cast<PluginManager *>(mdiServer ? mdiServer->mdiHost : nullptr);
        if (!pluginManager) {
            return;
        }

        // A single answer has nothing to choose between - go straight there.
        auto const tags = res[GlobalArguments::Tags].toList();
        if (tags.size() == 1) {
            auto const tag = tags.first().toHash();
            auto const fileName = tag[GlobalArguments::FileName].toString();
            auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();
            if (!fileName.isEmpty() && (lineNumber > 0 || !addressFromSymbolTag(tag).isEmpty())) {
                openSymbolTag(tag, pluginManager);
                return;
            }
        }

        auto menu = new QMenu(this);
        menu->setObjectName("qmdiEditor::followSymbolMenu");
        createSubFollowSymbolSubmenu(res, menu, pluginManager);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->popup(globalPosition);
    });
    watcher->setFuture(future);
}

void qmdiEditor::showContextMenu(const QPoint &localPosition, const QPoint &globalPosition) {
    auto followSymbolMenu = new QMenu(this);
    followSymbolMenu->setTitle(tr("Follow symbol"));

    auto loadingAction = new QAction(tr("Loading..."), followSymbolMenu);
    loadingAction->setEnabled(false);
    followSymbolMenu->addAction(loadingAction);

    auto referencesMenu = new QMenu(this);
    referencesMenu->setTitle(tr("Find all references"));

    auto loadingReferencesAction = new QAction(tr("Loading..."), referencesMenu);
    loadingReferencesAction->setEnabled(false);
    referencesMenu->addAction(loadingReferencesAction);

    auto menu = textEditor->createStandardContextMenu();
    auto separator = new QAction(this);
    auto actions = menu->actions();
    auto firstAction = actions.isEmpty() ? nullptr : actions.first();

    separator->setSeparator(true);
    menu->insertAction(firstAction, separator);

    auto refactorAction = new QAction(tr("Rename..."), menu);
    menu->insertAction(firstAction, refactorAction);
    // Capture where the menu was opened: a right-click does not move the caret, so
    // the word under the pointer has to be resolved from the click position.
    QObject::connect(refactorAction, &QAction::triggered, this,
                     [this, localPosition]() { requestRename(localPosition); });

    // Same action as Alt+Return, for when the shortcut is not remembered. Unlike
    // the fixes themselves this entry is always present: the plugin declines
    // CodeAction for a file no server claims, and the reply is empty, so picking
    // it does nothing. Hiding it would need a synchronous capability query on the
    // command bus, which does not exist yet.
    auto fixAction = new QAction(tr("Fix available..."), menu);
    menu->insertAction(firstAction, fixAction);
    QObject::connect(fixAction, &QAction::triggered, this, [this, localPosition]() {
        textEditor->setCursorPosition(textEditor->positionAt(localPosition));
        requestCodeActions();
    });

    menu->insertMenu(firstAction, followSymbolMenu);
    menu->insertMenu(firstAction, referencesMenu);

    auto future = getSuggestionsForCurrentWord(localPosition);
    auto referencesFuture = getReferencesForCurrentWord(localPosition);
    auto watcher = new QFutureWatcher<CommandArgs>(this);
    auto referencesWatcher = new QFutureWatcher<CommandArgs>(this);
    auto safeWatcher = QPointer(watcher);
    auto safeFollow = QPointer(followSymbolMenu);
    auto safeLoading = QPointer(loadingAction);
    auto safeReferences = QPointer(referencesMenu);
    auto safeLoadingReferences = QPointer(loadingReferencesAction);
    connect(watcher, &QFutureWatcher<CommandArgs>::finished, menu, [=, this]() {
        if (!safeFollow || !safeLoading /*|| !safeFollow->isVisible()*/) {
            qDebug() << "qmdiEditor: not safe follow";
            return;
        }

        safeFollow->removeAction(safeLoading);
        if (!safeWatcher || safeWatcher.isNull() || safeWatcher->isCanceled()) {
            qDebug() << "qmdiEditor: no safe watcher, null, or cancelled";
            return;
        }

        auto res = safeWatcher->result();
        auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
        if (!pluginManager) {
            qDebug() << "qmdiEditor: no plugin manager";
            return;
        }
        createSubFollowSymbolSubmenu(res, safeFollow, pluginManager);
    });

    connect(referencesWatcher, &QFutureWatcher<CommandArgs>::finished, menu, [=, this]() {
        if (!safeReferences || !safeLoadingReferences) {
            return;
        }

        safeReferences->removeAction(safeLoadingReferences);
        if (!referencesWatcher || referencesWatcher->isCanceled()) {
            return;
        }

        auto res = referencesWatcher->result();
        auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
        if (!pluginManager) {
            return;
        }
        createSubReferencesSubmenu(res, safeReferences, pluginManager);
    });

    watcher->setFuture(future);
    referencesWatcher->setFuture(referencesFuture);
    menu->exec(globalPosition);
    // The inline rename box is opened from the action's triggered handler, which
    // runs inside exec()'s nested event loop. On the way out exec() gives the
    // keyboard back to whichever widget had it before the menu appeared, so the
    // focus requestRename() made has already been undone by the time the user
    // could press a key. Taking it back here, once the loop has unwound, is what
    // leaves the box typed into instead of the document.
    if (renameEdit && renameEdit->isVisible()) {
        renameEdit->setFocus();
        renameEdit->selectAll();
    }
    delete menu;
}

/**
 * @brief Return status of editor
 * @return row, column,  zoom
 */
std::optional<std::tuple<int, int, int>> qmdiEditor::get_coordinates() const {
    auto row = textEditor->cursorLine();
    auto col = textEditor->cursorColumn();
    auto zoom = font().pointSize();
    return std::make_tuple(row, col, zoom);
}

qmdiClientState qmdiEditor::getState() const {
    auto row = textEditor->cursorLine();
    auto col = textEditor->cursorColumn();
    auto zoom = font().pointSize();
    auto state = qmdiClientState();
    state[StateConstants::COLUMN] = col;
    state[StateConstants::ROW] = row;
    state[StateConstants::ZOOM] = zoom;
    state[StateConstants::READ_ONLY] = textEditor->isReadOnly();
    state[StateConstants::BASE_DIR] = diffMetadata.baseDir;

    if (!uid.isEmpty()) {
        state[StateConstants::UUID] = uid;
    }

    if (textEditor->hasSelection()) {
        state[StateConstants::SEL_ANCHOR] = textEditor->selectionStart();
        state[StateConstants::SEL_POSITION] = textEditor->selectionEnd();
    } else {
        state.remove(StateConstants::SEL_ANCHOR);
        state.remove(StateConstants::SEL_POSITION);
    }
    return state;
}

void qmdiEditor::setState(const qmdiClientState &state) {
    savedState = state;
    if (!documentHasBeenLoaded) {
        return;
    }

    if (state.contains(StateConstants::UUID)) {
        uid = state[StateConstants::UUID].toString();
    }

    if (state.contains(StateConstants::ZOOM)) {
        auto zoom = state[StateConstants::ZOOM].toInt();
        auto f = font();
        f.setPointSize(zoom);
        setFont(f);
    }

    {
        auto col = 0;
        auto row = 0;
        if (state.contains(StateConstants::COLUMN)) {
            col = state[StateConstants::COLUMN].toInt();
        }
        if (state.contains(StateConstants::ROW)) {
            row = state[StateConstants::ROW].toInt();
        }
        textEditor->goTo(row, col);
    }

    if (state.contains(StateConstants::SEL_ANCHOR) &&
        state.contains(StateConstants::SEL_POSITION)) {
        auto anchor = state[StateConstants::SEL_ANCHOR].toInt();
        auto position = state[StateConstants::SEL_POSITION].toInt();
        textEditor->setSelection(anchor, position);
    }

    if (state.contains(StateConstants::READ_ONLY)) {
        textEditor->setReadOnly(state[StateConstants::READ_ONLY].toBool());
    }
    diffMetadata.baseDir = state[StateConstants::BASE_DIR].toString();
}

void qmdiEditor::on_client_unmerged(qmdiHost *host) {
    qmdiClient::on_client_unmerged(host);

    auto pluginManager = dynamic_cast<PluginManager *>(host);
    // clang-format off
    auto result = pluginManager->handleCommandAsync(GlobalCommands::ClosedFile, {
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::Client, QVariant::fromValue(this)},
    });
    // clang-format
}

void qmdiEditor::setupActions() {
    buttonChangeIndenter = new QToolButton(this);
    comboChangeHighlighter = new QComboBox(this);
    previewButton = new QPushButton(this);
    previewButton->setText(tr("Preview"));
    previewButton->setFlat(true);
    previewButton->setCheckable(true);

    connect(previewButton, &QAbstractButton::toggled, this, [this](bool toggled) {
        this->textPreview->setVisible(toggled);
        if (toggled) {
            updatePreview();
        }
    });

    actionSave = new QAction(QIcon::fromTheme("document-save"), tr("&Save"), this);
    actionSaveAs = new QAction(QIcon::fromTheme("document-save-as"), tr("Save &as..."), this);
    actionSaveNoFormat = new QAction(tr("Save without formatting"), this);
    actionUndo = new QAction(QIcon::fromTheme("edit-undo"), tr("&Undo"), this);
    actionRedo = new QAction(QIcon::fromTheme("edit-redo"), tr("&Redo"), this);
    actionCopy = new QAction(QIcon::fromTheme("edit-copy"), tr("&Copy"), this);
    actionCut = new QAction(QIcon::fromTheme("edit-cut"), tr("&Cut"), this);
    actionPaste = new QAction(QIcon::fromTheme("edit-paste"), tr("&Paste"), this);
    actionFind = new QAction(QIcon::fromTheme("edit-find"), tr("&Find"), this);
    actionFindNext = new QAction(QIcon::fromTheme("go-next"), tr("Find &next"), this);
    actionFindPrev = new QAction(QIcon::fromTheme("go-previous"), tr("Find &previous"), this);
    actionReplace = new QAction(QIcon::fromTheme("edit-find-replace"), tr("&Replace"), this);
    actionGotoLine = new QAction(QIcon::fromTheme("go-jump"), tr("&Goto line"), this);
    actionCopyFileName = new QAction(tr("Copy filename to clipboard"), this);
    actionCopyFilePath = new QAction(tr("Copy full path to clipboard"), this);

    actionCapitalize = new QAction(tr("Change to &capital letters"), this);
    actionLowerCase = new QAction(tr("Change to &lower letters"), this);
    actionChangeCase = new QAction(tr("Change ca&se"), this);
    actionToggleHeader = new QAction(tr("Toggle header/implementation"), this);
    actionTogglePreview = new QAction(tr("Toggle preview"), this);
    actionCodeActions = new QAction(tr("Fix available..."), this);
    actionFollowSymbol = new QAction(tr("Follow symbol"), this);
    actionFindReferences = new QAction(tr("Find all references"), this);
    actionRenameSymbol = new QAction(tr("Rename..."), this);

    addAction(actionCapitalize);
    addAction(actionLowerCase );
    addAction(actionChangeCase );
    addAction(actionToggleHeader );
    addAction(actionTogglePreview );
    addAction(actionCodeActions);
    addAction(actionFollowSymbol);
    addAction(actionFindReferences);
    addAction(actionRenameSymbol);
    addAction(actionSaveNoFormat);

    actionSave->setShortcut(QKeySequence::Save);
    actionSaveAs->setShortcut(QKeySequence::SaveAs);
    actionSaveNoFormat->setShortcut(QKeySequence(Qt::ControlModifier | Qt::MetaModifier | Qt::Key_S));
    actionUndo->setShortcut(QKeySequence::Undo);
    actionRedo->setShortcut(QKeySequence::Redo);
    actionCopy->setShortcut(QKeySequence::Copy);
    actionCut->setShortcut(QKeySequence::Cut);
    actionPaste->setShortcut(QKeySequence::Paste);
    actionFind->setShortcut(QKeySequence::Find);

    // Gnome uses control+g for find next, which steals our git shortcuts
    if (!runningUnderGnome()) {
        actionFindNext->setShortcut(QKeySequence::FindNext);
    }
    // this is usually "control+r, which we use for running a target
    // actionReplace->setShortcut(QKeySequence::Replace);
    actionReplace->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_H));

    actionFindPrev->setShortcut(QKeySequence::FindPrevious);
    actionGotoLine->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_L));
    actionCapitalize->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_U));
    actionLowerCase->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_U));
    actionToggleHeader->setShortcut(Qt::Key_F4);
    actionTogglePreview->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Period));
    // Alt+Return is the muscle memory for "fix this line" from every other
    // editor; the menu entry below is the discoverable route to the same thing.
    actionCodeActions->setShortcut(QKeySequence(Qt::AltModifier | Qt::Key_Return));
    actionCodeActions->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFollowSymbol->setShortcut(Qt::Key_F12);
    actionFollowSymbol->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFindReferences->setShortcut(QKeySequence(Qt::ShiftModifier | Qt::Key_F12));
    actionFindReferences->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionRenameSymbol->setShortcut(Qt::Key_F2);
    actionRenameSymbol->setShortcutContext(Qt::WidgetWithChildrenShortcut);

    auto highlighters = QStringList();
    for (auto const &language : editorFactory->languages()) {
        highlighters.append(language.names.isEmpty() ? language.id : language.names.first());
    }
    comboChangeHighlighter->setObjectName("qmdiEditor::comboChangeHighlighter");
    comboChangeHighlighter->addItems(highlighters);
    BoldItemDelegate *delegate = new BoldItemDelegate(comboChangeHighlighter);
    comboChangeHighlighter->setItemDelegate(delegate);

    buttonChangeIndenter->setObjectName("qmdiEditor::changeIndenter");
    buttonChangeIndenter->setText(tr("Change indentation"));
    buttonChangeIndenter->setPopupMode(QToolButton::InstantPopup);

    actionSave->setObjectName("qmdiEditor::actionSave");
    actionSaveAs->setObjectName("qmdiEditor::actionSaveAs");
    actionSaveNoFormat->setObjectName("qmdiEditor::actionSaveNoFormat");
    actionUndo->setObjectName("qmdiEditor::actionUndo");
    actionRedo->setObjectName("qmdiEditor::actionRedo");
    actionCopy->setObjectName("qmdiEditor::actionCopy");
    actionCut->setObjectName("qmdiEditor::actionCut");
    actionPaste->setObjectName("qmdiEditor::actionPaste");
    actionFind->setObjectName("qmdiEditor::actionFind");
    actionFindNext->setObjectName("qmdiEditor::actionFindNext");
    actionFindPrev->setObjectName("qmdiEditor::actionFindPrev");
    actionReplace->setObjectName("qmdiEditor::actionReplace");
    actionGotoLine->setObjectName("qmdiEditor::actionGotoLine");
    actionCapitalize->setObjectName("qmdiEditor::actionCapitalize");
    actionLowerCase->setObjectName("qmdiEditor::actionLowerCase");
    actionChangeCase->setObjectName("qmdiEditor::actionChangeCase");
    actionToggleHeader->setObjectName("qmdiEditor::actiohToggleHeader");
    actionTogglePreview->setObjectName("qmdiEditor::actionTogglePreview");
    actionFollowSymbol->setObjectName("qmdiEditor::actionFollowSymbol");
    actionFindReferences->setObjectName("qmdiEditor::actionFindReferences");
    actionRenameSymbol->setObjectName("qmdiEditor::actionRenameSymbol");

    actionSave->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionSaveAs->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionSaveNoFormat->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionUndo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionRedo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionCopy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionCut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionPaste->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFind->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFindNext->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFindPrev->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionReplace->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionGotoLine->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionCapitalize->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionLowerCase->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionChangeCase->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionToggleHeader->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionTogglePreview->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFollowSymbol->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionFindReferences->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actionRenameSymbol->setShortcutContext(Qt::WidgetWithChildrenShortcut);

    textEditor->setUndoAvailableCallback(
        [this](bool available) { actionUndo->setEnabled(available); });
    textEditor->setRedoAvailableCallback(
        [this](bool available) { actionRedo->setEnabled(available); });

    connect(actionCopyFileName, &QAction::triggered, actionCopyFileName, [this]() {
        auto c = QApplication::clipboard();
        c->setText(getShortFileName());
    });
    connect(actionCopyFilePath, &QAction::triggered, actionCopyFilePath, [this]() {
        auto c = QApplication::clipboard();
        c->setText(fileName);
    });

    QMenu *indentMenu = new QMenu(tr("Indentation"), this);
    QActionGroup *indentGroup = new QActionGroup(this);
    indentGroup->addAction(indentMenu->addAction(tr("None")))->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Normal - using previous line")))
        ->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("C-Style indentation")))->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Lisp based indentatio")))->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Scheme based indentation")))
        ->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("XML based indentation")))->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Python based indentation")))
        ->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Ruby based indentation")))->setCheckable(true);
    indentGroup->addAction(indentMenu->addAction(tr("Markdown based indentation")))->setCheckable(true);
    indentGroup->setExclusive(true);

    buttonChangeIndenter->setMenu(indentMenu);
    connect(indentGroup, &QActionGroup::triggered, this, &qmdiEditor::chooseIndenter);
    connect(indentMenu, &QMenu::aboutToShow, this, &qmdiEditor::updateIndenterMenu);

    connect(comboChangeHighlighter, &QComboBox::currentTextChanged, this,
            &qmdiEditor::chooseHighliter);
    connect(actionSave, &QAction::triggered, this, [this](){ doSave(); });
    connect(actionSaveAs, &QAction::triggered, this, [this](){ doSaveAs(); });
    connect(actionSaveNoFormat, &QAction::triggered, this, &qmdiEditor::doSaveNoFormat);
    connect(actionUndo, &QAction::triggered, this, [this]() { textEditor->undo(); });
    connect(actionRedo, &QAction::triggered, this, [this]() { textEditor->redo(); });
    connect(actionCopy, &QAction::triggered, this, [this]() { textEditor->copy(); });
    connect(actionCut, &QAction::triggered, this, [this]() { textEditor->cut(); });
    connect(actionPaste, &QAction::triggered, this, [this]() { textEditor->paste(); });
    connect(actionFind, &QAction::triggered, operationsWidget, &TextOperationsWidget::showSearch);
    connect(actionFindNext, &QAction::triggered, operationsWidget,
            &TextOperationsWidget::searchNext);
    connect(actionFindPrev, &QAction::triggered, operationsWidget,
            &TextOperationsWidget::searchPrevious);
    connect(actionReplace, &QAction::triggered, operationsWidget,
            &TextOperationsWidget::showReplace);
    connect(actionGotoLine, &QAction::triggered, operationsWidget,
            &TextOperationsWidget::showGotoLine);

    connect(actionCapitalize, &QAction::triggered, this, &qmdiEditor::transformBlockToUpper);
    connect(actionLowerCase, &QAction::triggered, this, &qmdiEditor::transformBlockToLower);
    connect(actionChangeCase, &QAction::triggered, this, &qmdiEditor::transformBlockCase);
    connect(actionToggleHeader, &QAction::triggered, this, &qmdiEditor::toggleHeaderImpl);
    connect(actionTogglePreview, &QAction::triggered, previewButton, &QPushButton::click);
    connect(actionCodeActions, &QAction::triggered, this, &qmdiEditor::requestCodeActions);
    connect(actionFollowSymbol, &QAction::triggered, this, &qmdiEditor::showFollowSymbolMenu);
    connect(actionFindReferences, &QAction::triggered, this, &qmdiEditor::showReferencesMenu);
    connect(actionRenameSymbol, &QAction::triggered, this, &qmdiEditor::showRenameSymbol);

    // default is control+b - which we want to use for build
    textEditor->action(CodeEditor::Action::ToggleBookmark)->setShortcut(QKeySequence());
}

void qmdiEditor::setModificationsLookupEnabled(bool value) {
    fileModifications = value;
    if (!mdiClientFileName().isEmpty()) {
        if (fileModifications) {
            fileSystemWatcher->addPath(fileName);
        } else {
            fileSystemWatcher->removePath(fileName);
        }
    }
}

void qmdiEditor::setEditorFont(const QFont &newFont) {
    textEditor->setEditorFont(newFont);
    operationsWidget->setTextFont(newFont);
}

void qmdiEditor::setEditorThemeFile(const QString &themeFile) {
    textEditor->setThemeFile(themeFile);
}

QString qmdiEditor::editorThemeFile() const { return textEditor->themeFile(); }

void qmdiEditor::setEditorHighlighter(QString id) {
    if (this->syntaxLangID == id) {
        return;
    }
    this->syntaxLangID = id;
    textEditor->setHighlighter(id);
}

bool qmdiEditor::isLocalToolbarVisible() const { return toolbar->isVisible(); }

void qmdiEditor::setLocalToolbarVisible(bool state) const { toolbar->setVisible(state); }

void qmdiEditor::setPreviewEnabled(bool enabled) {
    this->previewButton->setEnabled(enabled);
    this->actionTogglePreview->setEnabled(enabled);
}

void qmdiEditor::setPreviewVisible(bool enabled) { this->previewButton->setChecked(enabled); }

bool qmdiEditor::isPreviewRequested() {
    return this->previewButton->isEnabled() && this->previewButton->isChecked();
}

bool qmdiEditor::isPreviewVisible() const { return this->previewButton->isChecked(); }

void qmdiEditor::setHistoryModel(SharedHistoryModel *model) {
    operationsWidget->setSearchHistory(model);
}

bool qmdiEditor::isMarkDownDocument() const {
    return fileName.endsWith(".md", Qt::CaseInsensitive);
}

bool qmdiEditor::isXPMDocument() const { return fileName.endsWith(".xpm", Qt::CaseInsensitive); }

bool qmdiEditor::isSVGDocument() const { return fileName.endsWith(".svg", Qt::CaseInsensitive); }

bool qmdiEditor::isXMLDocument() const {
    static QRegularExpression regex(R"(.*xml.*\.xml$)", QRegularExpression::CaseInsensitiveOption);
    return regex.match(syntaxLangID).hasMatch();
}

bool qmdiEditor::isJSONDocument() const {
    return syntaxLangID.contains("json", Qt::CaseInsensitive);
}

void qmdiEditor::on_fileChanged(const QString &filename) {
    if (this->fileName != filename || !fileModifications) {
        return;
    }
    fileChangedTimer->start();
}

void qmdiEditor::reload() {
    EditorStateGuard guard(textEditor.get());

    documentHasBeenLoaded = false;
    loadContent(false);
}

void qmdiEditor::hideTimer_timeout() {
    if (m_timerHideout != 0) {
        QString s;
        s.setNum(m_timerHideout);
        m_timerHideout--;
        ui_banner->timer->setText(s);
        QTimer::singleShot(1000, this, SLOT(hideTimer_timeout()));
    } else {
        ui_banner->timer->clear();
        banner->hide();
    }
}

// FIXME: this is becoming a "on modified" function.
void qmdiEditor::updateClientName() {
    static const auto MODIFIED_TEXT = QStringLiteral(" ") + QChar(0x270F); // pencil
    static const auto EXECUTABLE_TEXT = QStringLiteral(" ") + QChar(0x2699); // wheel
    // static const auto EXECUTABLE_TEXT = QStringLiteral(" | >_");

    updatePreview();

    auto action = contextMenu.findActionNamed("runScript");
    auto baseName = getShortFileName();
    auto isModified = textEditor->isModified();
    auto newName = baseName;
    auto isExecutable = (action != nullptr);
    auto canOpenPreview = hasPreview();
    setPreviewEnabled(canOpenPreview);
    setPreviewVisible(canOpenPreview && autoPreview);

    if (isModified) {
        newName += MODIFIED_TEXT;
    }
    if (isExecutable) {
        newName += EXECUTABLE_TEXT;
    }

    if (mdiClientName != newName) {
        mdiClientName = newName;
        if (mdiServer) {
            mdiServer->updateClientName(this);
        }
    }
}

void qmdiEditor::onTextModified() {
    autoPreview = isPreviewVisible();
    updateClientName();
    emit contentChanged();
}

void qmdiEditor::goTo(int x, int y) {
    if (documentHasBeenLoaded) {
        textEditor->goTo(y, x);
    } else {
        savedState[StateConstants::ROW] = y;
        savedState[StateConstants::COLUMN] = x;
    }
}

QString qmdiEditor::getContent() const { return textEditor->text(); }

qmdiEditor::Range qmdiEditor::selectionRange() const { return textEditor->selectionRange(); }

bool qmdiEditor::applyTextEdits(const QList<TextEdit> &edits, const QString &expectedOldName) {
    if (edits.isEmpty()) {
        return false;
    }

    auto applied = textEditor->applyTextEdits(edits, expectedOldName);
    if (!applied && !expectedOldName.isEmpty()) {
        // The buffer moved while the server was working, so its ranges no longer
        // describe this document. Applying them would rename the symbol in some
        // places and not others, so nothing is applied - which means the rename
        // silently did nothing unless it is said out loud.
        displayBannerMessage(
            tr("Cannot rename: the file changed after the request was sent. Try again."), 60);
    }
    return applied;
}

QString qmdiEditor::getSelectedText() const { return textEditor->selectedText(); }

void qmdiEditor::focusInEvent(QFocusEvent *event) {

    QWidget::focusInEvent(event);
    textEditor->widget()->setFocus();
}

bool qmdiEditor::eventFilter(QObject *watched, QEvent *event) {
    if (watched == renameEdit) {
        // Escape drops the pending name, Enter accepts it. Both are consumed here
        // so they never reach the editor underneath, and returning true keeps the
        // box from acting on them as text movement.
        //
        // Enter has to be consumed here and not left to returnPressed alone. The
        // box is drawn over the viewport, so without this the same key press also
        // reaches the QPlainTextEdit and inserts a newline at the caret - which
        // shifts every line below it and makes the ranges the server is about to
        // return land on the wrong text.
        if (event->type() == QEvent::KeyPress) {
            auto keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                hideInlineRename();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                auto name = renameEdit->text();
                auto line = renameLine;
                auto column = renameColumn;
                auto content = textEditor->text();
                hideInlineRename();
                applyInlineRename(name, line, column, content);
                return true;
            }
        }
        // Clicking or tabbing away abandons the name, like any other inline edit.
        // Only when it is up: hideInlineRename() itself moves focus back to the
        // editor, and that must not be mistaken for the user leaving.
        if (event->type() == QEvent::FocusOut && renameEdit->isVisible()) {
            hideInlineRename();
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    if (watched == this) {
        switch (event->type()) {
        case QEvent::Show:
            handleTabSelected();
            break;
        case QEvent::Hide:
            handleTabDeselected();
            break;
        default:
            break;
        }
    }

    if (watched == textEditor->viewport()) {
        if (event->type() == QEvent::ToolTip) {
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
            handleWordTooltip(helpEvent->pos(), helpEvent->globalPos());
            return true;
        }

        if (event->type() == QEvent::ContextMenu) {
            auto menuEvent = static_cast<QContextMenuEvent *>(event);
            showContextMenu(menuEvent->pos(), menuEvent->globalPos());
            return true;
        }

        if (event->type() == QEvent::MouseButtonDblClick) {
            auto mouseEvent = static_cast<QMouseEvent *>(event);
            auto pos = mouseEvent->position().toPoint();
            auto position = textEditor->positionAt(pos);
            auto blockNumber = textEditor->lineAt(position);
            if (blockNumber >= 0 && blockNumber < textEditor->lineCount()) {
                if (diffMetadata.mappings.contains(blockNumber)) {
                    auto l = diffMetadata.mappings[blockNumber];
                    auto pluginManager = mdiServer ?
                        dynamic_cast<PluginManager *>(mdiServer->mdiHost) : nullptr;
                    auto filename = l.file;
                    filename = QDir::toNativeSeparators(filename);
                    if (pluginManager) {
                        // Lines start on the editor from 0
                        if (l.newLine >= 0) {
                            pluginManager->openFile(filename, 0, l.newLine - 1);
                        } else {
                            pluginManager->openFile(filename, 0, l.oldLine - 1);
                        }
                    } else {
                        qDebug() << "qmdiEditor::eventFilter - cannot open file from diff/patch";
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void qmdiEditor::handleTabSelected() {
    if (loadingTimer) {
        return;
    }
    loadingTimer = new QTimer(this);
    loadingTimer->setSingleShot(true);
    connect(loadingTimer, &QTimer::timeout, this, [this](){
        this->loadContent(true);
    });
    loadingTimer->start(50);
}

void qmdiEditor::handleTabDeselected() {
    if (!loadingTimer || !loadingTimer->isActive()) {
        return;
    }
    loadingTimer->stop();
    delete loadingTimer;
    loadingTimer = nullptr;
}

QFuture<QSet<CodeEditor::CompletionItem>> qmdiEditor::getTagCompletions(const QString &prefix, const QString &previousWord,
                                             const QString &separator) {
    if (!mdiServer || !mdiServer->mdiHost) {
        return {};
    }

    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return {};
    }

    CommandArgs args;
    args[GlobalArguments::RequestedSymbol] = prefix;
    args[GlobalArguments::FileName] = mdiClientFileName();
    args[GlobalArguments::Content] = textEditor->text();
    args[GlobalArguments::ExactMatch] = false;

    args[GlobalArguments::LineNumber] = textEditor->cursorLine();
    args[GlobalArguments::ColumnNumber] = textEditor->cursorColumn();
    args[GlobalArguments::Position] = textEditor->cursorPosition();

    if (!previousWord.isEmpty()) {
        args[GlobalArguments::PreviousWord] = previousWord;
    }
    if (!separator.isEmpty()) {
        args[GlobalArguments::Separator] = separator;
    }

    auto future = pluginManager->handleCommandAsync(GlobalCommands::VariableInfo, args);

    // In exclusive mode every suggestion comes from the one provider, so the source
    // label is noise on every row. Read the flag here, on the GUI thread: the
    // continuation below has no context object and runs on whichever thread
    // completes the future.
    auto const singleSource = textEditor->completionExclusive();

    return future.then(
        [singleSource](const QFuture<CommandArgs> &f) -> QSet<CodeEditor::CompletionItem> {
        auto completions = QSet<CodeEditor::CompletionItem>();
        if (!f.isFinished() || !f.isValid()) {
            return completions;
        }

        auto result = f.result();
        if (result.contains(GlobalArguments::Tags)) {
            auto tags = result[GlobalArguments::Tags].toList();
            for (const QVariant &item : std::as_const(tags)) {
                auto const tag = item.toHash();
                auto const name = tag[GlobalArguments::Name].toString();
                auto const type = tag[GlobalArguments::Type].toString();
                auto const fileName = tag[GlobalArguments::FileName].toString();
                auto const lineNumber = tag[GlobalArguments::LineNumber].toInt();

                if (!name.isEmpty()) {
                    QString sourceInfo;
                    if (!singleSource) {
                        QString sourceName = tag[GlobalArguments::Source].toString();
                        if (sourceName.isEmpty()) {
                            sourceName = "TreeSitter";
                            if (type == "tag" || type.isEmpty()) sourceName = "CTags";
                        }

                        sourceInfo = sourceName;
                        if (!fileName.isEmpty()) {
                            QFileInfo fi(fileName);
                            sourceInfo += "/" + fi.fileName();
                            if (lineNumber > 0) {
                                sourceInfo += ":" + QString::number(lineNumber);
                            }
                        }
                    }
                    completions.insert(CodeEditor::CompletionItem(name, sourceInfo));
                }
            }
        }
        return completions;
    });
}

void qmdiEditor::handleWordTooltip(const QPoint &localPosition, const QPoint &globalPosition) {
    auto future = getTooltipsForPosition(localPosition);
    auto watcher = new QFutureWatcher<CommandArgs>(this);
    connect(watcher, &QFutureWatcher<CommandArgs>::finished, this,
            [this, watcher, globalPosition]() {
                if (watcher->isFinished() && !watcher->isCanceled()) {
                    auto res = watcher->result();
                    auto tooltip = res[GlobalArguments::Tooltip].toString();
                    if (!tooltip.isEmpty()) {
                        QToolTip::showText(globalPosition, tooltip, textEditor->widget());
                    }
                }
                watcher->deleteLater();
            });
    watcher->setFuture(future);
}

QFuture<CommandArgs> qmdiEditor::getCommandForLocation(const QPoint &localPosition,
                                                       const QString &cmd) {
    if (!mdiServer || !mdiServer->mdiHost) {
        return {};
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return {};
    }

    auto position = textEditor->positionAt(localPosition);
    auto range = textEditor->wordRangeAt(position);
    if (range.startLine != range.endLine) {
        return {};
    }

    auto line = textEditor->lineText(range.startLine);
    auto const length = range.endCharacter - range.startCharacter;
    if (length <= 0) {
        return {};
    }

    auto symbol = line.mid(range.startCharacter, length);
    int lineNumber = range.startLine;
    int columnNumber = range.startCharacter;

    QString previousWord;
    QString separator;

    // Check for "->" or "::" or "."
    if (range.startCharacter >= 2) {
        auto twoChar = line.mid(range.startCharacter - 2, 2);
        if (twoChar == "->" || twoChar == "::") {
            separator = twoChar;
        }
    }
    if (separator.isEmpty() && range.startCharacter >= 1) {
        auto oneChar = line.mid(range.startCharacter - 1, 1);
        if (oneChar == "." || oneChar == ":") {
            separator = oneChar;
        }
    }

    if (!separator.isEmpty()) {
        auto endOfPrevWord = range.startCharacter - separator.length();
        while (endOfPrevWord > 0 && line.at(endOfPrevWord - 1).isSpace()) {
            --endOfPrevWord;
        }
        auto wordStart = endOfPrevWord;
        while (wordStart > 0 && (line.at(wordStart - 1).isLetterOrNumber() ||
                                 line.at(wordStart - 1) == QLatin1Char('_'))) {
            --wordStart;
        }
        if (wordStart < endOfPrevWord) {
            previousWord = line.mid(wordStart, endOfPrevWord - wordStart);
        }
    }

    // clang-format off
    auto res = pluginManager->handleCommandAsync(cmd, {
        {GlobalArguments::RequestedSymbol, symbol },
        {GlobalArguments::FileName, mdiClientFileName() },
        {GlobalArguments::ExactMatch, true },
        {GlobalArguments::LineNumber, lineNumber },
        {GlobalArguments::ColumnNumber, columnNumber },
        {GlobalArguments::Content, textEditor->text() },
        {GlobalArguments::PreviousWord, previousWord },
        {GlobalArguments::Separator, separator },
    });
    // clang-format on
    return res;
}

QFuture<CommandArgs> qmdiEditor::getSuggestionsForCurrentWord(const QPoint &localPosition) {
    return getCommandForLocation(localPosition, GlobalCommands::VariableInfo);
}

QFuture<CommandArgs> qmdiEditor::getReferencesForCurrentWord(const QPoint &localPosition) {
    return getCommandForLocation(localPosition, GlobalCommands::FindReferences);
}

QFuture<CommandArgs> qmdiEditor::getTooltipsForPosition(const QPoint &localPosition) {
    return getCommandForLocation(localPosition, GlobalCommands::KeywordTooltip);
}

void qmdiEditor::maybeScheduleSignatureHelp(bool textEdited) {
    if (!isInsideCall()) {
        signatureHelpTimer->stop();
        signatureHelpActive = false;
        return;
    }
    // A plain caret move is not reason enough to ask the server again for text
    // it has already answered for: only retrigger while a tip is expected. Text
    // edits inside a call always re-ask, because the active parameter moved.
    if (!signatureHelpActive && !textEdited) {
        return;
    }
    signatureHelpActive = true;
    signatureHelpTimer->start();
}

bool qmdiEditor::isInsideCall() const {
    auto caret = textEditor->cursorPosition();

    // Walk backwards, tracking parenthesis depth, until the '(' that encloses
    // the caret or a statement boundary. Stop after a generous amount of text so
    // a pathological one-line statement cannot turn every keystroke into a
    // full-document sweep.
    auto depth = 0;
    auto openParen = -1;
    auto const textStart = qMax(0, caret - 2048);
    for (int i = caret - 1; i >= textStart; --i) {
        auto ch = textEditor->characterAt(i);
        if (ch == QLatin1Char(')')) {
            ++depth;
        } else if (ch == QLatin1Char('(')) {
            if (depth == 0) {
                openParen = i;
                break;
            }
            --depth;
        } else if (ch == QLatin1Char(';') || ch == QLatin1Char('{') || ch == QLatin1Char('}')) {
            break;
        }
    }
    if (openParen < 0) {
        return false;
    }

    // The '(' must belong to a call: preceded by a function name (possibly after
    // whitespace) or by a closing paren of a nested call. Statement keywords are
    // excluded so "if (...)" and friends do not ask the server for a signature.
    auto k = openParen - 1;
    while (k >= 0 && textEditor->characterAt(k).isSpace()) {
        --k;
    }
    if (k < 0) {
        return false;
    }
    // A closing paren (chained call) or a template argument list ("foo<T>(")
    // may stand directly before the '('.
    if (textEditor->characterAt(k) == QLatin1Char(')') ||
        textEditor->characterAt(k) == QLatin1Char('>')) {
        return true;
    }
    if (!textEditor->characterAt(k).isLetter() && textEditor->characterAt(k) != QLatin1Char('_')) {
        return false;
    }
    auto wordStart = k;
    while (wordStart > 0 && (textEditor->characterAt(wordStart - 1).isLetter() ||
                             textEditor->characterAt(wordStart - 1) == QLatin1Char('_'))) {
        --wordStart;
    }
    QString word;
    word.reserve(k - wordStart + 1);
    for (auto i = wordStart; i <= k; ++i) {
        word.append(textEditor->characterAt(i));
    }
    // FIXME: get the list from the highlighter. See
    // https://github.com/diegoiast/qutepart-cpp/issues/78
    static const QSet<QString> controlKeywords = {
        QStringLiteral("if"),     QStringLiteral("for"),     QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("catch"),   QStringLiteral("do"),
        QStringLiteral("return"), QStringLiteral("foreach"),
    };
    return !controlKeywords.contains(word);
}

void qmdiEditor::requestSignatureHelp() {
    if (!mdiServer || !mdiServer->mdiHost) {
        return;
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return;
    }
    auto line = textEditor->cursorLine();
    auto column = textEditor->cursorColumn();
    // clang-format off
    signatureHelpWatcher.setFuture(pluginManager->handleCommandAsync(GlobalCommands::SignatureHelp, {
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::LineNumber, line},
        {GlobalArguments::ColumnNumber, column},
        {GlobalArguments::Content, textEditor->text()},
    }));
    // clang-format on
}

void qmdiEditor::requestCodeActions() {
    if (!mdiServer || !mdiServer->mdiHost) {
        return;
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager || mdiClientFileName().isEmpty()) {
        return;
    }

    auto const range = selectionRange();
    codeActionProbe = false;
    // clang-format off
    codeActionWatcher.setFuture(pluginManager->handleCommandAsync(GlobalCommands::CodeAction, {
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::LineNumber, range.startLine},
        {GlobalArguments::ColumnNumber, range.startCharacter},
        {GlobalArguments::EndLineNumber, range.endLine},
        {GlobalArguments::EndColumnNumber, range.endCharacter},
        {GlobalArguments::Content, textEditor->text()},
    }));
    // clang-format on
}

void qmdiEditor::probeCodeActions() {
    if (!mdiServer || !mdiServer->mdiHost || mdiClientFileName().isEmpty()) {
        return;
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return;
    }

    // The caret, not the selection: the probe answers "is there anything to do
    // where I am", and a selection spanning ten lines is not that question.
    auto const line = textEditor->cursorLine();
    auto const column = textEditor->cursorColumn();
    codeActionProbe = true;
    codeActionProbeLine = line;
    // clang-format off
    codeActionWatcher.setFuture(pluginManager->handleCommandAsync(GlobalCommands::CodeAction, {
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::LineNumber, line},
        {GlobalArguments::ColumnNumber, column},
        {GlobalArguments::EndLineNumber, line},
        {GlobalArguments::EndColumnNumber, column},
        {GlobalArguments::Content, textEditor->text()},
        // Refactorings are offered on most lines; only a fix justifies a
        // permanent mark, so the probe asks for quickfixes alone.
        {GlobalArguments::OnlyCodeActionKinds, QStringList{"quickfix"}},
    }));
    // clang-format on
}

void qmdiEditor::setCodeActionMark(int line, bool available) {
    if (codeActionMarkLine >= 0) {
        textEditor->setLineCodeAction(codeActionMarkLine, false);
    }
    codeActionMarkLine = available ? line : -1;
    if (available) {
        textEditor->setLineCodeAction(line, true);
    }
    textEditor->widget()->update();
}

void qmdiEditor::showCodeActionMenu(const CommandArgs &result) {
    auto const actions = result[GlobalArguments::Actions].toList();
    if (actions.isEmpty()) {
        // Nothing offered. Staying silent is right here: the shortcut is bound
        // unconditionally, and a banner on every Alt+Return over a clean line
        // would be noise.
        return;
    }

    auto *menu = new QMenu(this);
    menu->setObjectName("qmdiEditor::codeActionsMenu");

    // Preferred first, then everything else in server order. Stable, so two
    // equally preferred actions keep the order the server chose.
    auto ranked = QList<QVariant>(actions);
    std::stable_sort(ranked.begin(), ranked.end(), [](auto const &a, auto const &b) {
        return a.toHash()[GlobalArguments::IsDefinition].toBool() &&
               !b.toHash()[GlobalArguments::IsDefinition].toBool();
    });

    for (auto const &entry : ranked) {
        auto const action = entry.toHash();
        auto *item = new QAction(action[GlobalArguments::Name].toString(), menu);
        auto const kind = action[GlobalArguments::Type].toString();
        if (!kind.isEmpty()) {
            // The kind is what a user would call the fix ("quickfix" against a
            // bare "fix"), and it is the only way to tell two same-titled fixes
            // apart before picking one.
            item->setToolTip(kind);
        }
        // A disabled action is shown, not hidden: the spec wants the reason
        // visible. Clicking one must do nothing, which is also what a greyed
        // entry says.
        auto const disabled = action[GlobalArguments::ReadOnly].toBool();
        if (disabled) {
            item->setEnabled(false);
            auto const reason = action[GlobalArguments::ErrorMessage].toString();
            if (!reason.isEmpty()) {
                item->setToolTip(reason);
            }
        } else {
            // The entry travels by value and the menu is the context object, so
            // both it and `this` are safe: the menu is a child of this editor,
            // and a destroyed context drops its connections.
            QObject::connect(item, &QAction::triggered, menu,
                             [this, entry]() { applyChosenCodeAction(entry); });
        }
        menu->addAction(item);
    }

    // The rect is viewport-relative, while QMenu::popup() wants global
    // coordinates - so this has to go through the viewport or the list opens
    // near the screen origin instead of at the caret.
    auto const viewportPos = textEditor->cursorRect(textEditor->cursorPosition()).bottomLeft();
    // popup() returns immediately, so the menu cannot be deleted here: the
    // deferred delete would run on the next event loop pass and close it again
    // before it could be used. aboutToHide is when it is genuinely finished.
    QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);

    // Land on an action rather than on nothing, so Enter applies the top one
    // without a click. Set before the popup so it is highlighted from the first
    // frame. The first *enabled* entry, not the first entry: disabled ones are
    // listed so their reason is visible, and arriving on one would make Enter
    // look like it had done nothing. After the ranking above, that is the server's
    // preferred action, which is the one a blind Enter should get.
    for (auto *item : menu->actions()) {
        if (item->isEnabled()) {
            menu->setActiveAction(item);
            break;
        }
    }

    menu->popup(textEditor->viewport()->mapToGlobal(viewportPos));
}

void qmdiEditor::applyChosenCodeAction(const QVariant &entry) {
    if (!mdiServer || !mdiServer->mdiHost) {
        return;
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return;
    }
    // Fire and forget for the edits: the menu is gone by now, so nothing is
    // waiting on a future that outlives it. Content comes back with the action -
    // it is the text the server computed the ranges against, and the plugin
    // refuses rather than write a fix into a buffer that has since moved.
    pluginManager->handleCommandAsync(
        GlobalCommands::ApplyCodeAction,
        {
            {GlobalArguments::FileName, mdiClientFileName()},
            {GlobalArguments::Name, entry.toHash()[GlobalArguments::Name]},
            {GlobalArguments::Tags, entry.toHash()[GlobalArguments::Tags]},
            {GlobalArguments::Content, entry.toHash()[GlobalArguments::Content]},
        });
}

void qmdiEditor::requestRename(const QPoint &position) {
    auto wordPosition = textEditor->positionAt(position);
    auto range = textEditor->wordRangeAt(wordPosition);
    auto line = range.startLine;
    auto column = range.startCharacter;
    auto currentName = QString();
    if (range.startLine == range.endLine && range.endCharacter > range.startCharacter) {
        currentName = textEditor->lineText(line).mid(range.startCharacter,
                                                     range.endCharacter - range.startCharacter);
    }
    if (currentName.isEmpty()) {
        displayBannerMessage(tr("There is no symbol to rename here."), 60);
        return;
    }

    if (!renameEdit) {
        // Drawn on the viewport, like the call tip and the completion list, so it
        // sits in the text rather than in a dialog on top of it.
        renameEdit = new QLineEdit(textEditor->viewport());
        renameEdit->setFont(textEditor->widget()->font());
        renameEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
        renameEdit->hide();
        renameEdit->installEventFilter(this);

        auto shadow = new QGraphicsDropShadowEffect(renameEdit);
        shadow->setBlurRadius(16);
        shadow->setOffset(1, 2);
        // A shadow is depth, not a theme color, so it stays a dark overlay that
        // works over any background.
        shadow->setColor(QColor(0, 0, 0, 140));
        renameEdit->setGraphicsEffect(shadow);

        // Enter accepts the name, Escape drops it. Both are consumed in
        // eventFilter() rather than connected here: a key press that is only
        // observed by a signal still reaches the editor underneath and inserts a
        // newline at the caret.

        // The name refers to one place in the buffer: if the text changes, or the
        // user does something else, the box is no longer about that symbol. A
        // scroll is not a change, it only moves the box along with the text.
        // Text and cursor callbacks in the constructor handle the first two.
        // Scrolling moves the text under the box, so it has to move with it.
        connect(textEditor->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this]() { repositionInlineRename(); });
    }

    renameLine = line;
    renameColumn = column;
    renameEdit->setText(currentName);
    renameEdit->selectAll();

    // Shown before positioning: the geometry is in viewport coordinates, and a
    // hidden widget has no meaningful size to lay out against.
    renameEdit->show();
    renameEdit->raise();
    repositionInlineRename();
    renameEdit->setFocus();

    textEditor->hideCallTip();
}

void qmdiEditor::repositionInlineRename() {
    if (!renameEdit) {
        return;
    }
    // Anchor over the symbol, so the box lines up with the name being replaced
    // instead of sitting wherever the caret happened to be.
    if (renameLine < 0 || renameLine >= textEditor->lineCount()) {
        hideInlineRename();
        return;
    }
    auto name = textEditor->lineText(renameLine);
    auto column = qBound(0, renameColumn, name.size());
    auto isIdent = [](QChar c) { return c.isLetterOrNumber() || c == QChar('_'); };
    auto start = column;
    while (start > 0 && isIdent(name.at(start - 1))) {
        --start;
    }
    auto end = column;
    while (end < name.size() && isIdent(name.at(end))) {
        ++end;
    }
    if (start == end) {
        hideInlineRename();
        return;
    }
    // The result is in viewport coordinates, which is what the parent uses too.
    auto lineStart = textEditor->positionAt(renameLine, 0);
    auto rect = textEditor->cursorRect(lineStart + start);

    // Size it to the line, with room for the border and padding, and wide enough
    // for the current name plus what gets typed over it.
    auto metrics = renameEdit->fontMetrics();
    auto height = qMax(rect.height(), metrics.height() + 8);
    auto textWidth = metrics.horizontalAdvance(name.mid(start, end - start));
    auto width = qMax(textWidth * 2, rect.width() * 2) + 16;

    // Preferred spot: two lines above the symbol, so the symbol and the line
    // directly above it both stay readable. The previous block's cursorRect is
    // used rather than just moving up by the box height, so it is still correct
    // when the text above is wrapped. A y of -1 means there is no line above at
    // all, and any negative value means it has scrolled off the top.
    auto viewportSize = textEditor->viewport()->size();
    auto y = -1;
    if (renameLine > 0) {
        auto abovePosition = textEditor->positionAt(renameLine - 1, 0);
        if (abovePosition >= 0) {
            y = textEditor->cursorRect(abovePosition).y() - rect.height();
        }
    }
    if (y < 0) {
        // No room above - the symbol is on the first line, or near the top of
        // the viewport - so go below it rather than cover it.
        y = rect.y() + rect.height();
    }
    y = qBound(0, y, qMax(0, viewportSize.height() - height));
    auto x = qBound(0, rect.x(), qMax(0, viewportSize.width() - width));
    renameEdit->setGeometry(x, y, width, height);
}

void qmdiEditor::hideInlineRename() {
    if (!renameEdit) {
        return;
    }
    renameEdit->hide();
    renameLine = -1;
    renameColumn = -1;
    // Give the keyboard back to the text, but only if the box actually had it -
    // renaming can be abandoned by a window switch, where stealing focus here
    // would be wrong.
    if (textEditor && renameEdit->hasFocus()) {
        textEditor->widget()->setFocus();
    }
}

void qmdiEditor::applyInlineRename(const QString &newName, int line, int column,
                                   const QString &content) {
    if (newName.isEmpty()) {
        return;
    }
    auto range = textEditor->wordRangeAt(textEditor->positionAt(line, column));
    auto currentName = QString();
    if (range.startLine == range.endLine && range.endCharacter > range.startCharacter) {
        currentName = textEditor->lineText(line).mid(range.startCharacter,
                                                     range.endCharacter - range.startCharacter);
    }
    if (newName == currentName) {
        // Nothing to ask the server to do.
        return;
    }
    if (!mdiServer || !mdiServer->mdiHost) {
        return;
    }
    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!pluginManager) {
        return;
    }

    // The handling plugin applies the rename through the server and reports
    // failures in the result - the editor only decides how to show them.
    // clang-format off
    auto future = pluginManager->handleCommandAsync(GlobalCommands::RenameSymbol, {
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::LineNumber, line},
        {GlobalArguments::ColumnNumber, column},
        {GlobalArguments::Content, content},
        {GlobalArguments::NewName, newName},
    });
    // clang-format on
    if (!future.isValid()) {
        // No plugin claimed the command, so nothing can rename here.
        displayBannerMessage(tr("Rename is not available for this file."), 60);
        return;
    }

    // The rename is fire-and-forget on the editor side: whatever the plugin could
    // not do comes back as a message, and only the banner is ours. The watcher is
    // parented to this editor and deletes itself once it has delivered, since the
    // server answers on a reader thread well after this function has returned.
    auto watcher = new QFutureWatcher<CommandArgs>(this);
    connect(watcher, &QFutureWatcher<CommandArgs>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (watcher->isCanceled()) {
            return;
        }
        auto error = watcher->result()[GlobalArguments::ErrorMessage].toString();
        if (!error.isEmpty()) {
            displayBannerMessage(error, 60);
        }
    });
    watcher->setFuture(future);
}

void qmdiEditor::displayBannerMessage(QString message, int time) {
    banner->show();
    ui_banner->label->setText(message);
    m_timerHideout = time;
    if (time >= 0) {
        QTimer::singleShot(1000, this, SLOT(hideTimer_timeout()));
    }
}

void qmdiEditor::hideBannerMessage() {
    m_timerHideout = 0;
    ui_banner->label->clear();
    banner->hide();
}

void qmdiEditor::newDocument() { loadFile(""); }

void qmdiEditor::setPlainText(const QString &plainText) {
    textEditor->setText(plainText);
    documentHasBeenLoaded = true;
}

bool qmdiEditor::doSave(bool forceNoFormat) {
    if (fileName.isEmpty()) {
        return doSaveAs(forceNoFormat);
    } else {
        if (!documentHasBeenLoaded) {
            return true;
        }
        return saveFile(fileName, false, forceNoFormat);
    }
}

bool qmdiEditor::doSaveAs(bool forceNoFormat) {
    static QString lastDirectory;
    auto s = QFileDialog::getSaveFileName(this, tr("Save file"), lastDirectory);
    if (s.isEmpty()) {
        return false;
    }

#if defined(WIN32)
    auto makeExecutable = false;
#else
    auto makeExecutable = true;
#endif

    auto f = QFileInfo(s);
    lastDirectory = f.dir().absolutePath();
    return saveFile(s, makeExecutable, forceNoFormat);
}

void qmdiEditor::doSaveNoFormat() { doSave(true); }

bool qmdiEditor::loadFile(const QString &newFileName) {
    fileName = QDir::toNativeSeparators(newFileName);
    mdiClientName = getShortFileName();

    documentHasBeenLoaded = false;
    if (this->fileName.isEmpty()) {
        this->fileName.clear();
        textEditor->clear();
        return true;
    }

    updateClientName();
    return true;
}

bool qmdiEditor::saveFile(const QString &newFileName, bool makeExecutable, bool forceNoFormat) {
    auto sl = fileSystemWatcher->directories();
    if (!sl.isEmpty()) {
        fileSystemWatcher->removePaths(sl);
    }

    auto modificationsEnabledState = getModificationsLookupEnabled();
    setModificationsLookupEnabled(false);
    hideBannerMessage();

    auto needToReformat = false;
    if (!forceNoFormat) {
        switch (formatOnSave) {
        case FormatCodeOnSave::Never:
            needToReformat = false;
            break;
        case FormatCodeOnSave::Always:
            needToReformat = true;
            break;
        case FormatCodeOnSave::InProjects:
            needToReformat = isFileInProject().result();
            break;
        }
    }

    if (needToReformat) {
        auto future = reformatContent();
        if (!future.isFinished()) {
            QEventLoop loop;
            future.then([&loop] { loop.quit(); });
            loop.exec();
        }
    } else {
        if (forceNoFormat) {
            displayBannerMessage(tr("Force saving without formatting"), 0);
        }
    }

    auto file = QFile(newFileName);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Could not open file for saving" << newFileName;
        displayBannerMessage(tr("Could not save file"), 60);
        return false;
    }

    QPointer<qmdiEditor> safeThis(this);
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QApplication::processEvents();

    if (!safeThis) {
        QApplication::restoreOverrideCursor();
        return false;
    }

    if (trimSpacesOnSave) {
        textEditor->trimTrailingSpaces();
    }

    auto textStream = QTextStream(&file);
    auto const lineCount = textEditor->lineCount();
    for (auto line = 0; line < lineCount; ++line) {
        textStream << textEditor->lineText(line);
        if (line + 1 < lineCount) {
            switch (this->endLineStyle) {
            case EndLineStyle::UnixEndLine:
                textStream << "\n";
                break;
            case EndLineStyle::WindowsEndLine:
                textStream << "\r\n";
                break;
            case EndLineStyle::KeepOriginalEndline:
                textStream << originalLineEnding;
                break;
            }
        }
    }

    auto filenameChanged = this->fileName != newFileName;
    if (filenameChanged && makeExecutable) {
        auto line = textEditor->lineText(0);
        if (line.length() >= 3) {
            auto firstChar = line[0];
            auto secondChar = line[1];
            if (firstChar == '#' && secondChar == '!') {
                auto currentPermissions = file.permissions();
                auto newPermissions = currentPermissions | QFileDevice::ExeUser;
                if (!file.setPermissions(newPermissions)) {
                    qWarning() << "Failed to add executable permission.";
                    displayBannerMessage(tr("Failed to add executable permission."), 60);
                }
            }
        }
    }
    file.close();

    fileSystemWatcher->removePath(fileName);
    fileName = newFileName;
    textEditor->removeModifications();
    setModificationsLookupEnabled(modificationsEnabledState);
    updateFileDetails();
    saveBackup();
    fileSystemWatcher->addPath(newFileName);

    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    pluginManager->handleCommandAsync(
        GlobalCommands::SavedFile,
        {
            {GlobalArguments::FileName, mdiClientFileName()},
            {GlobalArguments::Client, QVariant::fromValue(static_cast<qmdiClient *>(this))},
        });

    textEditor->setModified(false);
    updateClientName();
    QApplication::restoreOverrideCursor();
    return true;
}

void qmdiEditor::setReadOnly(bool b) {
    textEditor->setReadOnly(b);
    actionCut->setEnabled(!b);
    actionPaste->setEnabled(!b);
    actionReplace->setEnabled(!b);
    actionCapitalize->setEnabled(!b);
    actionLowerCase->setEnabled(!b);
    actionChangeCase->setEnabled(!b);
    textOperationsMenu->setEnabled(!b);

    if (b) {
        actionUndo->setEnabled(false);
        actionRedo->setEnabled(false);
    } else {
        actionUndo->setEnabled(textEditor->canUndo());
        actionRedo->setEnabled(textEditor->canRedo());
    }
}

void qmdiEditor::transformBlockToUpper() {
    auto const before = textEditor->selectedText();
    auto const after = before.toUpper();
    if (before != after) {
        textEditor->replaceSelectedText(after);
    }
}

void qmdiEditor::transformBlockToLower() {
    auto const before = textEditor->selectedText();
    auto const after = before.toLower();
    if (before != after) {
        textEditor->replaceSelectedText(after);
    }
}

void qmdiEditor::transformBlockCase() {
    auto const before = textEditor->selectedText();
    auto after = before;
    for (auto &character : after) {
        if (character.isLower()) {
            character = character.toUpper();
        } else if (character.isUpper()) {
            character = character.toLower();
        }
    }
    if (before != after) {
        textEditor->replaceSelectedText(after);
    }
}

void qmdiEditor::toggleHeaderImpl() {
    auto manager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    getCorrespondingFile(manager, fileName).then(this, [this](const QString &otherFile) {
        auto nativeFile = QDir::toNativeSeparators(otherFile);
        if (!nativeFile.isEmpty()) {
            auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
            if (pluginManager) {
                pluginManager->openFile(nativeFile);
            }
        }
    });
}

void qmdiEditor::loadContent(bool useBackup) {
    if (documentHasBeenLoaded) {
        return;
    }
    // clear older watches, and add a new one
    auto sl = fileSystemWatcher->directories();
    if (!sl.isEmpty()) {
        fileSystemWatcher->removePaths(sl);
    }

    auto modificationsEnabledState = getModificationsLookupEnabled();
    setModificationsLookupEnabled(false);
    hideBannerMessage();
    setReadOnly(false);
    QPointer<qmdiEditor> safeThis(this);
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QApplication::processEvents();

    if (!safeThis) {
        QApplication::restoreOverrideCursor();
        return;
    }

    QFile file;
    auto loadedFromBackup = false;
    if (useBackup && savedState.contains(StateConstants::UUID)) {
        uid = savedState[StateConstants::UUID].toString();
        file.setFileName(getBackupFileName());
        if (file.open(QIODevice::ReadOnly)) {
            loadedFromBackup = true;
        } else if (file.setFileName(fileName), !file.open(QIODevice::ReadOnly)) {
            QApplication::restoreOverrideCursor();
            return;
        }
    } else {
        file.setFileName(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            QApplication::restoreOverrideCursor();
            return;
        }
    }

    QElapsedTimer timer;
    timer.start();
    this->originalLineEnding = getLineEnding(file, originalLineEnding);
    auto textStream = QTextStream(&file);
    textStream.seek(0);

    // Why blocking signals?
    // When loading, (setPlainText()) a signal is emitted,  which triggers the system to believe
    // that the content has been modified. Just don't do this. It will also save some time
    // on loading, since really, signals emitted a this stage are not meaningful.
    {
        auto firstLoad = useBackup;
        textEditor->widget()->blockSignals(true);
        auto selStart = textEditor->selectionStart();
        auto selEnd = textEditor->selectionEnd();
        textEditor->replaceText(textStream.readAll(), !firstLoad);
        auto docLength = textEditor->text().size();
        selStart = qMin(selStart, docLength);
        selEnd = qMin(selEnd, docLength);
        textEditor->setSelection(selStart, selEnd);
    }

    QFileInfo fileInfo(fileName);
    auto elapsed = timer.elapsed();

    if (elapsed > 0) {
        // don't spam logs with very fast loads, we don't care about it
        qDebug() << "qmdiEditor::loadContent " << fileName << "loaded in" << elapsed
                 << "mSec, size=" << file.size();
        ;
    }

    fileName = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    if (fileInfo.exists() && !fileInfo.isWritable()) {
        setReadOnly(true);
        displayBannerMessage(
            tr("The file is readonly. Click <a href=':forcerw' title='Click here to try and "
               "change the file attributes for write access'>here to force write access.</a>"),
            60);
    }

    textEditor->setModified(loadedFromBackup);
    updateFileDetails();
    setModificationsLookupEnabled(modificationsEnabledState);
    textEditor->removeModifications();
    textEditor->widget()->blockSignals(false);
    QApplication::restoreOverrideCursor();
    documentHasBeenLoaded = true;

    auto pluginManager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    pluginManager->openFile("loaded:" + fileName);
    auto result = pluginManager->handleCommandAsync(
        GlobalCommands::LoadedFile,
        {
            {GlobalArguments::FileName, mdiClientFileName()},
            {GlobalArguments::Client, QVariant::fromValue(static_cast<qmdiClient *>(this))},
        });

    updateClientName();
    setState(savedState);
    updateInternalMappings(savedState[StateConstants::BASE_DIR].toString());
}

QFuture<void> qmdiEditor::reformatContent() {
    auto manager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    // The text the formatter is about to work on. Kept so the result can be
    // discarded if the buffer moves on while clang-format runs.
    auto const input = textEditor->text();
    auto args = CommandArgs{
        {GlobalArguments::FileName, mdiClientFileName()},
        {GlobalArguments::Content, input},
    };

    return manager->handleCommandAsync(GlobalCommands::ReformatCode, args)
        .then(this, [this, input](CommandArgs args) {
            auto exitCode = args[GlobalArguments::ExitCode].toInt();
            if (exitCode != 0) {
                auto processStderr = args[GlobalArguments::ErrorMessage].toString();
                if (processStderr.isEmpty()) {
                    // A formatter that fails with nothing on stderr would otherwise
                    // produce a blank banner, which reads as a glitch rather than an
                    // error.
                    processStderr = tr("Formatting failed (code %1)").arg(exitCode);
                }

                switch (exitCode) {
                case GlobalResults::ExecutableNotFound:
                    // this->displayBannerMessage(processStderr, 15);
                    break;
                case GlobalResults::ExecutableError:
                    this->displayBannerMessage(processStderr, 25);
                    break;
                case GlobalResults::Crashed:
                    this->displayBannerMessage(processStderr, 35);
                    break;
                case GlobalResults::NotSupported:
                    break;
                }
                return;
            }

            auto c = args[GlobalArguments::Content].toString();
            if (c == textEditor->text()) {
                return;
            }

            // The formatter ran on a worker thread against the text captured above.
            // If the buffer moved on in the meantime - a keystroke, or an LSP edit
            // such as a rename landing mid-format - those changes are not in the
            // formatter's output, and replacing the whole document would silently
            // discard them. Only apply when the buffer is still exactly what was
            // sent, and otherwise let the user re-run it on the current text.
            if (textEditor->text() != input) {
                displayBannerMessage(
                    tr("The file changed while it was being formatted. Format it again to "
                       "apply."),
                    40);
                return;
            }

            EditorStateGuard guard(textEditor.get());
            textEditor->replaceText(c, true);
            textEditor->widget()->setFocus();
        });
}

QFuture<bool> qmdiEditor::isFileInProject() {
    return getProjectDetails().then(
        [](const CommandArgs &args) { return args.value(GlobalArguments::Value).toBool(); });
}

QFuture<CommandArgs> qmdiEditor::getProjectDetails() {
    auto manager = dynamic_cast<PluginManager *>(mdiServer->mdiHost);
    if (!manager) {
        return QtFuture::makeReadyValueFuture(CommandArgs());
    }

    return manager->handleCommandAsync(GlobalCommands::GetProjectForFile,
                                       {
                                           {GlobalArguments::FileName, mdiClientFileName()},
                                       });
}

void qmdiEditor::chooseHighliter(const QString &newText) {
    auto langInfo = editorFactory->languageByName(newText);
    if (langInfo.isValid()) {
        textEditor->setHighlighter(langInfo.id);
    } else {
        textEditor->removeHighlighter();
    }
}

void qmdiEditor::chooseIndenter(const QAction *action) {
    buttonChangeIndenter->setText(action->text());
    auto act = buttonChangeIndenter->menu()->actions();
    auto j = act.indexOf(action);
    if (j >= 0) {
        textEditor->setIndentAlgorithm(static_cast<CodeEditor::IndentAlgorithm>(j));
    }
}

void qmdiEditor::findText(const QString &text) { textEditor->findText(text); }

void qmdiEditor::updateInternalMappings(const QString &baseDir) {
    diffMetadata.mappings.clear();
    diffMetadata.baseDir = baseDir;

    if (!mdiClientName.endsWith(".diff", Qt::CaseInsensitive) &&
        mdiClientName.endsWith(".patch", Qt::CaseInsensitive)) {
        return;
    }

    auto extractFileName = [](QString &line) -> QString {
        auto pos = line.indexOf(' ');
        if (pos < 0) {
            return {};
        }
        // /dev/null = file add/delete
        auto path = line.mid(pos + 1);
        if (path == "/dev/null") {
            return {};
        }
        // Strip leading a/ or b/
        if (path.size() >= 2 && (path[0] == 'a' || path[0] == 'b') && path[1] == '/') {
            path.remove(0, 2);
        }
        return path;
    };

    auto parse_hunk_header = [](const QString &line) -> std::pair<int, int> {
        // Example: "@@ -12,7 +34,9 @@"
        auto old_start = 0;
        auto new_start = 0;
        auto i = line.indexOf('-');
        if (i < 0) {
            return {0, 0};
        }
        // parse old_start, skip '-'
        ++i;
        while (i < line.size() && line[i].isDigit()) {
            old_start = old_start * 10 + (line[i].digitValue());
            ++i;
        }

        i = line.indexOf('+', i);
        if (i < 0) {
            return {0, 0};
        }
        // skip '+'
        ++i;
        while (i < line.size() && line[i].isDigit()) {
            new_start = new_start * 10 + line[i].digitValue();
            ++i;
        }
        return {old_start, new_start};
    };

    auto current_file = QString();
    auto old_line = 0;
    auto new_line = 0;
    auto in_hunk = false;
    auto const lineCount = textEditor->lineCount();
    for (auto i = 0; i < lineCount; ++i) {
        auto text = textEditor->lineText(i);

        if (text.startsWith("+++ ")) {
            current_file = extractFileName(text);
            continue;
        }

        if (text.startsWith("@@")) {
            auto [old_start, new_start] = parse_hunk_header(text);
            old_line = old_start;
            new_line = new_start;
            in_hunk = true;
            continue;
        }

        if (!in_hunk) {
            continue;
        }

        if (text.startsWith(' ')) {
            diffMetadata.mappings[i] = {baseDir + "/" + current_file, old_line, new_line};
            old_line += 1;
            new_line += 1;
        } else if (text.startsWith('-')) {
            diffMetadata.mappings[i] = {baseDir + "/" + current_file, old_line, -1};
            old_line += 1;
        } else if (text.startsWith('+')) {
            diffMetadata.mappings[i] = {baseDir + "/" + current_file, -1, new_line};
            new_line += 1;
        }
    }
}

void qmdiEditor::mapAllLinesToFile(const QString &diffFileName) {
    diffMetadata.mappings.clear();
    diffMetadata.baseDir = QFileInfo(diffFileName).absolutePath();

    auto const lineCount = textEditor->lineCount();
    for (auto i = 0; i < lineCount; ++i) {
        diffMetadata.mappings[i] = {diffFileName, -1, i + 1};
    }
}

/**
 * @brief qmdiEditor::updateFileDetails
 *
 * Called when a file is opened, or saved. Then, the "best" indentator, and
 * syntax is chosen, and fix the UI to reflect it (the bottom combo box).
 *
 * Called on save and load.
 */
void qmdiEditor::updateFileDetails() {
    actionSave->setText(tr("&Save (%1)").arg(getShortFileName()));
    auto langInfo = editorFactory->languageForFile(fileName);

    if (langInfo.isValid()) {
        textEditor->setHighlighter(langInfo.id);
        textEditor->setIndentAlgorithm(langInfo.indentAlgorithm);
        auto indentIndex = static_cast<int>(langInfo.indentAlgorithm);
        buttonChangeIndenter->menu()->actions().at(indentIndex)->setChecked(true);

        auto delegate = static_cast<BoldItemDelegate *>(comboChangeHighlighter->itemDelegate());
        delegate->boldItemStr = langInfo.names.isEmpty() ? langInfo.id : langInfo.names.first();

        auto i = comboChangeHighlighter->findText(delegate->boldItemStr);
        if (i > 0) {
            comboChangeHighlighter->setCurrentIndex(i);
        }

        auto s = buttonChangeIndenter->menu()->actions()[indentIndex]->text();
        buttonChangeIndenter->setText(s);
    } else {
        textEditor->removeHighlighter();
        textEditor->setIndentAlgorithm(CodeEditor::IndentAlgorithm::None);

        auto a = buttonChangeIndenter->menu()->actions().at(
            static_cast<int>(CodeEditor::IndentAlgorithm::None));

        a->setChecked(true);
        buttonChangeIndenter->setText(a->text());

        auto delegate = static_cast<BoldItemDelegate *>(comboChangeHighlighter->itemDelegate());
        delegate->boldItemStr = QStringLiteral("Plain text");

        auto i = comboChangeHighlighter->findText(delegate->boldItemStr);
        if (i > 0) {
            comboChangeHighlighter->setCurrentIndex(i);
        }
    }

    auto suffix = QFileInfo(fileName).suffix();
    auto isCplusplusSource = cExtensions.contains(suffix, Qt::CaseInsensitive) ||
                             headerExtensions.contains(suffix, Qt::CaseInsensitive);
    actionToggleHeader->setEnabled(isCplusplusSource);
}

void qmdiEditor::updateIndenterMenu() {
    auto langInfo = editorFactory->languageForFile(this->fileName);
    auto k = 0;
    for (auto i : buttonChangeIndenter->menu()->actions()) {
        QFont font = i->font();
        if (langInfo.isValid() && k == static_cast<int>(langInfo.indentAlgorithm)) {
            font.setBold(true);
        } else {
            font.setBold(false);
        }
        i->setFont(font);
        k++;
    }
}

void qmdiEditor::updateHighlighterMenu() {
    // FIXME: implement this or remove
}

void qmdiEditor::updatePreview() {
    if (!this->previewButton->isChecked()) {
        return;
    }

    if (isMarkDownDocument()) {
        textPreview->previewText(mdiClientFileName(), textEditor->text(), TextPreview::Markdown);
    } else if (isSVGDocument()) {
        textPreview->previewText(mdiClientFileName(), textEditor->text(), TextPreview::SVG);
    } else if (isXPMDocument()) {
        textPreview->previewText(mdiClientFileName(), textEditor->text(), TextPreview::XPM);
    } else if (isJSONDocument()) {
        textPreview->previewText(mdiClientFileName(), textEditor->text(), TextPreview::JSON);
    } else if (isXMLDocument()) {
        textPreview->previewText(mdiClientFileName(), textEditor->text(), TextPreview::XML);
    }
}

void qmdiEditor::autoSave() {
    if (textEditor->isModified()) {
        saveBackup();
    }
}

QString qmdiEditor::getBackupFileName() const {
    if (uid.isEmpty()) {
        return {};
    }

    auto backupPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backups/";
    QDir().mkpath(backupPath);
    return backupPath + uid + ".bak";
}

void qmdiEditor::saveBackup() {
    auto file = QFile(getBackupFileName());
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream out(&file);
        out << textEditor->text();
    }
}

void qmdiEditor::loadBackup() {
    auto file = QFile(getBackupFileName());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        textEditor->setText(in.readAll());
    }
}

void qmdiEditor::deleteBackup() {
    auto file = QFile(getBackupFileName());
    if (file.exists()) {
        file.remove();
    }
}

void qmdiEditor::fileMessage_clicked(const QString &s) {
    if (s == ":reload") {
        reload();
    }
}
