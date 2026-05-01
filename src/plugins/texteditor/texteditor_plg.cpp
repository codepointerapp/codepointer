
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QChar>
#include <QCoreApplication>
#include <QFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QStringListModel>
#include <QUrl>

#include <CommandPaletteWidget/commandpalette.h>
#include <qmdiactiongroup.h>
#include <qmdihost.h>
#include <qmdiserver.h>

#include "AnsiToHTML.hpp"
#include "GlobalCommands.hpp"
#include "texteditor_plg.h"
#include "thememanager.h"
#include "widgets/HistoryLineEdit.h"
#include "widgets/qmdieditor.h"

TextEditorPlugin::TextEditorPlugin() {
    name = "TextEditorPlugin";
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;
    themeManager = new Qutepart::ThemeManager();
    historyModel = new SharedHistoryModel(this);

    /*
    #if defined(WIN32)
        auto installPrefix = QCoreApplication::applicationDirPath();
    #else
        auto installPrefix = QCoreApplication::applicationDirPath() + "/..";
    #endif
    */

    config.pluginName = tr("Text editor");
    config.description = tr("Default text editor, based on QutePart");
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Show toolbar"))
                                     .setDescription(tr("Show a toolbar above the text editor"))
                                     .setKey(Config::ShowToolbarKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Trim spaces"))
                                     .setDescription(tr("Remove spaces from end of lines, on save"))
                                     .setKey(Config::TrimSpacesKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Wrap lines"))
                                     .setDescription(tr("Wrap lines at window size"))
                                     .setKey(Config::WrapLinesKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Auto reload"))
                                     .setDescription(tr("Reload text when file changes on disk"))
                                     .setKey(Config::AutoReloadKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Smart home/end"))
                                     .setDescription(tr("Move cursor to logical/physical home/end"))
                                     .setKey(Config::SmartHomeKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Show white space"))
                                     .setDescription(tr("Display marks above tabs, and spaces"))
                                     .setKey(Config::ShowWhiteKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Draw indentations"))
            .setDescription(tr("Display marks on first indentations of lines"))
            .setKey(Config::ShowIndentationsKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(true)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Highlight brackets"))
            .setDescription(tr("Draws a background around matching brakcet"))
            .setKey(Config::HighlightBracketsKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(true)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Auto preview"))
            .setDescription(tr("Opens a preview panel for supported files"))
            .setKey(Config::AutoPreviewKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(true)
            .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Mark current word"))
            .setDescription(tr("Select the word under cursor in the whole document"))
            .setKey(Config::MarkCurrentWordKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(true)
            .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Show line numbers"))
                                     .setDescription("Show or hide the line numbers panel")
                                     .setKey(Config::ShowLineKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());

    auto values = QStringList() << tr("Unix end of line") << tr("Windows end of line")
                                << tr("Keep original end of line");
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("End of line style"))
                                     .setDescription(tr("Which line ends to use when saving"))
                                     .setKey(Config::LineEndingSaveKey)
                                     .setType(qmdiConfigItem::OneOf)
                                     .setPossibleValue(values)
                                     .setDefaultValue(EndLineStyle::KeepOriginalEndline)
                                     .build());
    config.configItems.push_back(
        qmdiConfigItem::Builder()
            .setDisplayName(tr("Enable soft wrapping"))
            .setDescription(
                tr("When reaching the margin position, word continues on the next line"))
            .setKey(Config::SoftWrappingKey)
            .setType(qmdiConfigItem::Bool)
            .setDefaultValue(false)
            .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Show right margin"))
                                     .setDescription("Shows a a margin at the end of line")
                                     .setKey(Config::MarginKey)
                                     .setType(qmdiConfigItem::Bool)
                                     .setDefaultValue(true)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Margin position"))
                                     .setDescription(tr("Character at which the margin is drawn"))
                                     .setKey(Config::MarginOffsetKey)
                                     .setType(qmdiConfigItem::UInt16)
                                     .setDefaultValue(80)
                                     .build());

    auto monospacedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospacedFont.setFixedPitch(true);

    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Display font"))
                                     .setKey(Config::FontKey)
                                     .setType(qmdiConfigItem::Font)
                                     .setDefaultValue(monospacedFont.toString())
                                     .setValue(monospacedFont.toString())
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::ThemeKey)
                                     .setType(qmdiConfigItem::Font)
                                     .setDefaultValue("")
                                     .setUserEditable(false)
                                     .build());
    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setKey(Config::SeachHistoryKey)
                                     .setType(qmdiConfigItem::StringList)
                                     .setDefaultValue(QStringList())
                                     .setUserEditable(false)
                                     .build());

    auto values2 = QStringList() << tr("Never format") << tr("Always format")
                                 << tr("When file is in a project");

    config.configItems.push_back(qmdiConfigItem::Builder()
                                     .setDisplayName(tr("Format on save"))
                                     .setKey(Config::FormatBehaviourKey)
                                     .setType(qmdiConfigItem::OneOf)
                                     .setPossibleValue(values2)
                                     .setDefaultValue(FormatCodeOnSave::InProjects)
                                     .build());
}

TextEditorPlugin::~TextEditorPlugin() {}

void TextEditorPlugin::on_client_merged(qmdiHost *) {
    auto manager = getManager();
    chooseTheme = new QAction(tr("Choose theme"), manager);
    menus[tr("Se&ttings")]->addAction(chooseTheme);

    manager->connect(manager, &PluginManager::newFileRequested, [this]() { fileNew(); });

    connect(chooseTheme, &QAction::triggered, this, [this]() {
        auto current = getManager()->currentClient();
        auto editor = dynamic_cast<qmdiEditor *>(current);
        if (!editor) {
            return;
        }

        newThemeSelected = false;
        chooseTheme->setDisabled(true);
        auto list = QStringList(tr("System colors"));
        for (auto &t : themeManager->getLoadedFiles()) {
            auto m = themeManager->getThemeMetaData(t);
            list.append(m.name);
        }
        list.sort(Qt::CaseSensitive);

        auto model = new QStringListModel(list, getManager());
        auto p = new CommandPalette(getManager());
        p->setDataModel(model);
        p->show();

        connect(p, &CommandPalette::didHide, this, [this, p, editor]() {
            if (!newThemeSelected) {
                editor->setEditorTheme(this->theme);
                editor->setEditorHighlighter(editor->getSyntaxID());
            } else {
                // apply it globally
                auto newTheme = const_cast<Qutepart::Theme *>(editor->getEditorTheme());
                delete this->theme;
                this->theme = newTheme;
                for (auto i = 0; i < mdiServer->getClientsCount(); i++) {
                    auto client = mdiServer->getClient(i);
                    auto e = dynamic_cast<qmdiEditor *>(client);
                    if (!e) {
                        // current editor already has this enabled
                        continue;
                    }

                    e->setEditorTheme(this->theme);
                }

                if (newTheme) {
                    auto themeFileName =
                        themeManager->getNameFromDesc(newTheme->getMetaData().name);
                    getConfig().setTheme(themeFileName);
                } else {
                    getConfig().setTheme("");
                }
            }
            chooseTheme->setEnabled(true);
            p->deleteLater();
            editor->setFocus();
        });
        connect(p, &CommandPalette::didSelectItem, this,
                [editor, this](const QModelIndex index, const QAbstractItemModel *) {
                    auto newTheme = const_cast<Qutepart::Theme *>(editor->getEditorTheme());
                    if (newTheme != this->theme) {
                        delete newTheme;
                    }
                    newTheme = nullptr;
                    auto themeDescription = index.data(Qt::DisplayRole).toString();
                    auto themeFileName = themeManager->getNameFromDesc(themeDescription);
                    auto themeMetaData = themeManager->getThemeMetaData(themeFileName);
                    if (!themeMetaData.name.isEmpty()) {
                        newTheme = new Qutepart::Theme();
                        const_cast<Qutepart::Theme *>(newTheme)->loadTheme(themeFileName);
                    }
                    editor->setEditorTheme(newTheme);
                });
        connect(p, &CommandPalette::didChooseItem, this, [this]() {
            // don't restore theme on closing - user choose his new theme
            newThemeSelected = true;
        });
    });
}

void TextEditorPlugin::showAbout() {
    QMessageBox::information(dynamic_cast<QMainWindow *>(mdiServer), "About",
                             "This plugin gives a QtSourceView based text editor");
}

void TextEditorPlugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);
    themeManager->loadFromDir(":/qutepart/themes/");

    auto themeFileName = getConfig().getTheme();
    delete this->theme;
    if (!themeFileName.isEmpty()) {
        this->theme = new Qutepart::Theme();
        if (!this->theme->loadTheme(themeFileName)) {
            qDebug() << "Failed loading theme " << themeFileName;
            delete this->theme;
            this->theme = nullptr;
        }
    }
    auto history = getConfig().getSeachHistory();
    historyModel->setHistory(history);
}

void TextEditorPlugin::saveConfig(QSettings &settings) {
    auto history = historyModel->getHistory();
    getConfig().setSeachHistory(history);
    IPlugin::saveConfig(settings);
}

int TextEditorPlugin::canHandleAsyncCommand(const QString &command, const CommandArgs &) const {
    if (command == GlobalCommands::DisplayText) {
        return true;
    }
    return false;
}

QFuture<CommandArgs> TextEditorPlugin::handleCommandAsync(const QString &command,
                                                          const CommandArgs &args) {
    if (command != GlobalCommands::DisplayText) {
        return {};
    }

    auto fileName = args[GlobalArguments::FileName].toString();
    auto content = args[GlobalArguments::Content].toString();
    auto isReadOnly = args[GlobalArguments::ReadOnly].toBool();
    auto shouldFold = args[GlobalArguments::FoldTopLevel].toBool();
    auto sourceDir = args[GlobalArguments::SourceDirectory].toString();
    auto ok = false;
    auto clientPosition = args[GlobalArguments::Position].toInt(&ok);
    if (!ok) {
        clientPosition = -1;
    }

    auto editor = new qmdiEditor(dynamic_cast<QMainWindow *>(mdiServer), themeManager);
    auto langInfo = ::Qutepart::chooseLanguage({}, {}, fileName);
    if (langInfo.isValid()) {
        editor->setEditorHighlighter(langInfo.id);
    }
    // auto shouldAutoPreview = editor->autoPreview;
    // auto canOpenPreview = editor->hasPreview();
    editor->loadFile(fileName);
    editor->setPreviewEnabled(true);
    editor->setPreviewVisible(true);
    editor->setHistoryModel(historyModel);
    editor->setPlainText(content);
    editor->setReadOnly(isReadOnly);
    editor->updateInternalMappings(sourceDir);
    applySettings(editor);
    mdiServer->addClient(editor, clientPosition);
    if (shouldFold) {
        editor->foldTopLevel();
    }
    auto promise = new QPromise<CommandArgs>();
    auto future = promise->future();
    promise->finish();
    return promise->future();
}

QStringList TextEditorPlugin::myExtensions() {
    auto s = QStringList();
    s << tr("Sources C++", "EditorPlugin::myExtensions") +
             " (*.c *.cpp *.cxx *.h *.hpp *.hxx *.inc CMakeLists.txt *.toml *.moc conanfile.txt)";
    s << tr("Headers", "EditorPlugin::myExtensions") + " (*.h *.hpp *.hxx *.inc)";
    s << tr("Sources, other", "EditorPlugin::myExtensions") +
             " (*.py *.java *.js *.rb *.inc *.rust *.rb *.swift *.kot *.sh *.pas *.bas)";
    s << tr("Text files", "EditorPlugin::myExtensions") + " (*.txt)";
    s << tr("Projects", "EditorPlugin::myExtensions") +
             " (CMakeLists.txt cargo.toml go.mod build.meson *.desktop *.iss *.xml *.yml *.json "
             "Makefile*)";
    s << tr("Text images", "EditorPlugin::myExtensions") + " (*.svg *.xpm)";
    s << tr("All files", "EditorPlugin::myExtensions") + " (*.*)";
    return s;
}

int TextEditorPlugin::canOpenFile(const QString &fileName) {
    if (fileName.isEmpty()) {
        return 5;
    }
    auto u = QUrl(fileName);

    // if the scheme is a single line, lets assume this is a windows drive
    if (u.scheme().length() != 1) {
        if ((u.scheme().toLower() != "file") && (!u.scheme().isEmpty())) {
            return -2;
        }
    }

    // TODO - list should be synced with myExtensions() somehow
    static const QStringList extensions = {
        ".c",
        ".cpp",
        ".cxx",
        ".cc",
        ".h",
        ".hpp",
        ".hxx",
        ".inc",
        ".pro",
        ".pri",
        ".txt",
        ".inc",
        ".java",
        ".js",
        ".py",
        ".rb",
        ".go",
        ".pas",
        ".bas",
        ".swift",
        ".mk",
        ".bat",
        ".sh",
        ".md",
        ".user",
        ".txt",
        ".text",
        ".dox",
        ".desktop",
        ".old",
        ".bak",
        ".mk",
        ".svg",
        ".xpm"
        ".sh",
        ".desktop",
        ".qrc",
        ".rc",
        ".json",
        ".iss",
        ".lic",
        ".xml",
        ".json",
        "license",
        "readme",
        "copying",
        "Makefile",
        "Doxyfile"
        "CMakeLists.txt",
        "*.moc",
        "conanfile.txt",
    };
    for (const auto &ext : extensions) {
        if (fileName.endsWith(ext, Qt::CaseInsensitive)) {
            return 5;
        }
    }

    if (fileName.startsWith(".")) {
        return 5;
    }

    auto file = QFile(fileName);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        auto in = QTextStream(&file);
        QString firstLines = in.readLine();
        firstLines.append(in.readLine());
        firstLines.append(in.readLine());
        firstLines.append(in.readLine());
        firstLines.append(in.readLine());
        file.close();
        if (isPlainText(firstLines)) {
            return 5;
        }
    }
    if (file.size() == 0) {
        return 5;
    }

    return 1;
}

qmdiClient *TextEditorPlugin::openFile(const QString &fileName, int x, int y, int zoom) {
    auto editor = new qmdiEditor(dynamic_cast<QMainWindow *>(mdiServer), themeManager);

    // In the future - the zoom, will be used to set state to the lines, if the value is really
    // large. I will assume that font size bigger than 500 is not really existent.
    if (0 < zoom && zoom < 1000000) {
        auto f = editor->font();
        f.setPointSize(zoom);
        editor->setFont(f);
    }

    auto langInfo = ::Qutepart::chooseLanguage({}, {}, fileName);
    if (langInfo.isValid()) {
        editor->setEditorHighlighter(langInfo.id);
    }

    applySettings(editor);
    auto shouldAutoPreview = editor->autoPreview;
    auto canOpenPreview = editor->hasPreview();
    editor->loadFile(fileName);
    editor->setPreviewEnabled(canOpenPreview);
    editor->setPreviewVisible(canOpenPreview && shouldAutoPreview);
    editor->setHistoryModel(historyModel);
    mdiServer->addClient(editor);
    if (x >= 0 || y >= 0) {
        editor->loadContent(true);
        editor->goTo(x, y);
    }
    return editor;
}

qmdiClient *TextEditorPlugin::navigateFile(qmdiClient *client, int x, int y, int z) {
    Q_UNUSED(z);
    auto *editor = dynamic_cast<qmdiEditor *>(client);
    if (editor) {
        editor->goTo(y, x);
    }
    return editor;
}

void TextEditorPlugin::applySettings(qmdiEditor *editor) {
    if (getConfig().getWrapLines()) {
        editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    } else {
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    }

    auto newFont = QFont();
    newFont.fromString(getConfig().getFont());

    editor->setLocalToolbarVisible(getConfig().getShowToolbar());
    editor->setDrawAnyWhitespace(getConfig().getShowWhite());
    editor->setDrawIndentations(getConfig().getShowIndentations());
    editor->setBracketHighlightingEnabled(getConfig().getHighlightBrackets());
    editor->setEditorMarkWord(getConfig().getMarkCurrentWord());
    editor->setSoftLineWrapping(getConfig().getSoftWrapping());
    editor->setLineNumbersVisible(getConfig().getShowLine());
    editor->setSmartHomeEnd(getConfig().getSmartHome());
    editor->setDrawSolidEdge(getConfig().getMargin());
    editor->setLineLengthEdge(getConfig().getMarginOffset());
    editor->endLineStyle = getConfig().getLineEndingSave();
    editor->trimSpacesOnSave = getConfig().getTrimSpaces();
    editor->autoPreview = getConfig().getAutoPreview();
    editor->formatOnSave = getConfig().getFormatBehaviour();
    editor->setEditorFont(newFont);
    editor->repaint();

    if (this->theme != editor->getEditorTheme()) {
        editor->setEditorTheme(this->theme);
    }
}

void TextEditorPlugin::configurationHasBeenModified() {
    for (auto i = 0; i < mdiServer->getClientsCount(); i++) {
        auto client = mdiServer->getClient(i);
        auto editor = dynamic_cast<qmdiEditor *>(client);
        if (!editor) {
            continue;
        }
        applySettings(editor);
    }
}

void TextEditorPlugin::fileNew() {
    auto editor = fileNewEditor();
    mdiServer->addClient(editor);
}

qmdiClient *TextEditorPlugin::fileNewEditor() {
    auto editor = new qmdiEditor(dynamic_cast<QMainWindow *>(mdiServer), themeManager);
    editor->setHistoryModel(historyModel);
    applySettings(editor);
    return editor;
}
