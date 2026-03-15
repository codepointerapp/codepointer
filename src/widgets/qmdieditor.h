/**
 * \file qmdieditor
 * \brief Definition of the main editor for the IDE
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QMap>
#include <QStyledItemDelegate>
#include <QToolButton>

// needed only for CommandArgs
#include <pluginmanager.h>
#include <qmdiclient.h>
#include <qutepart/qutepart.h>

#include "endlinestyle.h"

class QFileSystemWatcher;
class QPushButton;
class QComboBox;

class TextPreview;
class TextOperationsWidget;
class SharedHistoryModel;

namespace Ui {
class BannerMessage;
}

namespace Qutepart {
class ThemeManager;
}

/**
A source editor with MDI interface.
This class will be a very rich text editor which will also have a set of toolbars and menus
available for the qmdiHost.

@author Diego Iastrubni <diegoiast@gmail.com>
*/
class qmdiEditor : public QWidget, public qmdiClient {
    Q_OBJECT

  public:
    qmdiEditor(QWidget *p, Qutepart::ThemeManager *theme);
    ~qmdiEditor();

    virtual bool canCloseClient(CloseReason reason = CloseReason::CloseTab) override;
    virtual QString mdiClientFileName() override;
    virtual std::optional<std::tuple<int, int, int>> get_coordinates() const override;
    virtual qmdiClientState getState() const override;
    virtual void setState(const qmdiClientState &state) override;
    virtual void on_client_unmerged(qmdiHost *host) override;

    void setupActions();
    inline bool getModificationsLookupEnabled() const { return fileModifications; }
    void setModificationsLookupEnabled(bool);
    void setEditorFont(const QFont &newFont);
    inline const Qutepart::Theme *getEditorTheme() { return textEditor->getTheme(); }
    inline void setEditorTheme(const Qutepart::Theme *theme) { textEditor->setTheme(theme); }
    void setEditorHighlighter(QString id);
    inline void setEditorMarkWord(bool b) { textEditor->setMarkCurrentWord(b); }

    bool isLocalToolbarVisible() const;
    void setLocalToolbarVisible(bool state) const;

    void setPreviewEnabled(bool enabled);
    void setPreviewVisible(bool enabled);
    bool isPreviewRequested();
    bool isPreviewVisible() const;

    void setHistoryModel(SharedHistoryModel *model);

    inline const QString &getSyntaxID() const { return this->syntaxLangID; }
    inline const QString &getIndentatorID() const { return this->indentationID; }

    bool isMarkDownDocument() const;
    bool isXPMDocument() const;
    bool isSVGDocument() const;
    bool isXMLDocument() const;
    bool isJSONDocument() const;
    inline bool hasPreview() const {
        return isMarkDownDocument() || isXPMDocument() || isSVGDocument() || isXMLDocument() ||
               isJSONDocument();
    }

    QString getSelectedText() const;

  public slots:
    void on_fileChanged(const QString &filename);
    void displayBannerMessage(QString message, int time);
    void hideBannerMessage();

    void newDocument();
    void setPlainText(const QString &plainText);
    void reload();
    bool doSave(bool forceNoFormat = false);
    bool doSaveAs(bool forceNoFormat = false);
    bool loadFile(const QString &fileName);
    bool saveFile(const QString &fileName, bool makeExecutable, bool forceNoFormat = false);
    void doSaveNoFormat();
    void transformBlockToUpper();
    void transformBlockToLower();
    void transformBlockCase();
    void toggleHeaderImpl();
    void loadContent(bool useBackup);
    QFuture<void> reformatContent();

    void chooseHighliter(const QString &newText);
    void chooseIndenter(const QAction *action);
    void findText(const QString &text);
    void updateInternalMappings(const QString &baseDir);

  private slots:
    void updateFileDetails();
    void updateIndenterMenu();
    void updateHighlighterMenu();
    void updatePreview();

    void fileMessage_clicked(const QString &s);
    void hideTimer_timeout();
    void updateClientName();
    void onTextModified();
    void autoSave();

  public:
    QString originalLineEnding = "\n";
    EndLineStyle endLineStyle = EndLineStyle::KeepOriginalEndline;
    FormatCodeOnSave formatOnSave = FormatCodeOnSave::Never;

    bool trimSpacesOnSave = false;
    bool autoPreview = true;

    // No longer inheriting Qutepart, instead use "static inheritance"
    void goTo(int x, int y);
    inline void setLineWrapMode(QPlainTextEdit::LineWrapMode mode) {
        textEditor->setLineWrapMode(mode);
    }
    inline void setDrawAnyWhitespace(bool b) { textEditor->setDrawAnyWhitespace(b); }
    inline void setDrawIndentations(bool b) {
        textEditor->setDrawIndentations(b);
        textEditor->setDrawIncorrectIndentation(b);
    }
    inline void setBracketHighlightingEnabled(bool b) {
        textEditor->setBracketHighlightingEnabled(b);
    }
    inline void setSoftLineWrapping(bool value) { textEditor->setSoftLineWrapping(value); }
    inline bool softLineWrapping() const { return textEditor->softLineWrapping(); }
    inline void setLineNumbersVisible(bool b) { textEditor->setLineNumbersVisible(b); }
    inline void setSmartHomeEnd(bool b) { textEditor->setSmartHomeEnd(b); }
    inline void setDrawSolidEdge(bool b) { textEditor->setDrawSolidEdge(b); }
    inline void setLineLengthEdge(int l) { textEditor->setLineLengthEdge(l); }
    inline void removeMetaData() { textEditor->removeMetaData(); }
    inline void setMetaDataMessage(int lineNumber, const QString &message) {
        textEditor->setLineMessage(lineNumber, message);
    }
    inline void setLineBookmark(int lineNumber, bool value) {
        textEditor->setLineBookmark(lineNumber, value);
    }
    inline void setLineWarning(int lineNumber, bool value) {
        textEditor->setLineWarning(lineNumber, value);
    }
    inline void setLineError(int lineNumber, bool value) {
        textEditor->setLineError(lineNumber, value);
    }
    inline void setLineInfo(int lineNumber, bool value) {
        textEditor->setLineInfo(lineNumber, value);
    }
    inline void setLineBreakpoint(int lineNumber, bool value) {
        textEditor->setLineBreakpoint(lineNumber, value);
    }
    inline void setLineExecuting(int lineNumber, bool value) {
        textEditor->setLineExecuting(lineNumber, value);
    }
    inline bool isEmpty() const {
        if (!textEditor->document()) {
            return true;
        }
        return textEditor->document()->isEmpty();
    }
    inline void foldTopLevel() const { textEditor->foldTopLevelBlocks(); }
    inline void setMinimapVisible(bool value) const { textEditor->setMinimapVisible(value); }
    void setReadOnly(bool b);
    inline QPlainTextEdit *getEditor() const { return textEditor; }
    inline void setHighlighter(const QString &languageId) {
        textEditor->setHighlighter(languageId);
    }

  protected:
    virtual void focusInEvent(QFocusEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

    void handleTabSelected();
    void handleTabDeselected();
    void handleWordTooltip(const QPoint &localPosition, const QPoint &globalPosition);
    QFuture<CommandArgs> getCommandForLocation(const QPoint &localPosition, const QString &cmd);
    QFuture<CommandArgs> getSuggestionsForCurrentWord(const QPoint &localPosition);
    QFuture<CommandArgs> getTooltipsForPosition(const QPoint &localPosition);
    QSet<QString> getTagCompletions(const QString &prefix);

  private:
    QString getShortFileName();
    void showContextMenu(const QPoint &localPosition, const QPoint &globalPosition);
    QString getBackupFileName() const;
    void saveBackup();
    void loadBackup();
    void deleteBackup();

    Qutepart::ThemeManager *themeManager = nullptr;
    Qutepart::Qutepart *textEditor = nullptr;
    TextOperationsWidget *operationsWidget = nullptr;
    QString syntaxLangID;
    QString indentationID;

    QFileSystemWatcher *fileSystemWatcher;
    QWidget *banner;
    Ui::BannerMessage *ui_banner;
    int m_timerHideout;
    bool fileModifications = true;
    QTimer *loadingTimer = nullptr;
    QTimer *fileChangedTimer = nullptr;
    bool documentHasBeenLoaded = true;

    qmdiClientState savedState;

    struct StateConstants {
        static constexpr const char *COLUMN = "col";
        static constexpr const char *ROW = "row";
        static constexpr const char *ZOOM = "zoom";
        static constexpr const char *SEL_ANCHOR = "sel-anchor";
        static constexpr const char *SEL_POSITION = "sel-position";
        static constexpr const char *UUID = "uuid";
        static constexpr const char *READ_ONLY = "read-only";
        static constexpr const char *BASE_DIR = "basedir";
    };

    QString fileName;
    QMenu *bookmarksMenu;
    QMenu *textOperationsMenu;
    QMenu *foldingMenu;

    QComboBox *comboChangeHighlighter = nullptr;
    QToolButton *buttonChangeIndenter = nullptr;
    QPushButton *previewButton = nullptr;
    QWidget *toolbar = nullptr;
    TextPreview *textPreview;

    QAction *actionSave = nullptr;
    QAction *actionSaveAs = nullptr;
    QAction *actionSaveNoFormat = nullptr;
    QAction *actionUndo = nullptr;
    QAction *actionRedo = nullptr;
    QAction *actionCopy = nullptr;
    QAction *actionCut = nullptr;
    QAction *actionPaste = nullptr;
    QAction *actionFind = nullptr;
    QAction *actionFindNext = nullptr;
    QAction *actionFindPrev = nullptr;
    QAction *actionReplace = nullptr;
    QAction *actionGotoLine = nullptr;
    QAction *actionCapitalize = nullptr;
    QAction *actionLowerCase = nullptr;
    QAction *actionChangeCase = nullptr;
    QAction *actionCopyFileName = nullptr;
    QAction *actionCopyFilePath = nullptr;
    QAction *actionToggleHeader = nullptr;
    QAction *actionTogglePreview = nullptr;

    struct diffLineNumber {
        QString file;
        int oldLine = -1;
        int newLine = -1;
    };
    // When loading a path/diff file - this maps between lines
    // and the file and offset inside it.
    QMap<int, diffLineNumber> patchMappings;

    struct {
        struct diffLineNumber {
            QString file;
            int oldLine = -1;
            int newLine = -1;
        };
        // When loading a path/diff file - this maps between lines
        // and the file and offset inside it.
        QMap<int, diffLineNumber> mappings;
        QString baseDir;
    } diffMetadata;

    QTimer *autoSaveTimer = nullptr;
    QString uid;
};
