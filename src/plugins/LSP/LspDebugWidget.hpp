#pragma once

#include <QWidget>

class LspPlugin;

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

/// Right-hand dock showing what the LSP plugin is doing: which servers are up,
/// which documents they know about, a live message trace, and a form for firing
/// ad-hoc requests through the very same code path the editor uses.
class LspDebugWidget : public QWidget {
    Q_OBJECT

  public:
    explicit LspDebugWidget(LspPlugin *plugin, QWidget *parent = nullptr);

  public slots:
    void appendTrace(const QString &message);
    void refreshState();

  private slots:
    void useCurrentEditor();
    void runCompletion();
    void runHover();
    void runSync();

  private:
    void log(const QString &message);
    /// Fires `command` through the plugin's normal async path, so the panel
    /// exercises exactly what the editor exercises.
    void runQuery(const QString &command);

    LspPlugin *plugin = nullptr;

    QTableWidget *serversTable = nullptr;
    QTableWidget *documentsTable = nullptr;
    QLineEdit *fileEdit = nullptr;
    QSpinBox *lineSpin = nullptr;
    QSpinBox *columnSpin = nullptr;
    QPushButton *currentEditorButton = nullptr;
    QPushButton *completionButton = nullptr;
    QPushButton *hoverButton = nullptr;
    QPushButton *syncButton = nullptr;
    QCheckBox *traceCheck = nullptr;
    QPlainTextEdit *logView = nullptr;
    QTimer *refreshTimer = nullptr;
};
