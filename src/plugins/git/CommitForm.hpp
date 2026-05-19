#pragma once

#include <QWidget>
#include <qmdiclient.h>

namespace Ui {
class CommitForm;
}

class GitPlugin;
class GitStatusTableModel;

enum class GitFileStatus { Modified, Added, Deleted, Renamed, Copied, Untracked, Unknown };

class CommitForm : public QWidget, public qmdiClient {
    Q_OBJECT

  public:
    explicit CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent);
    ~CommitForm();

    virtual QString mdiClientFileName() override;

  protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  public slots:
    void updateGitStatus();
    void newFileSelected(const QString &filename, GitFileStatus status);
    void revertCurrentImpl();
    void revertSelectionImpl();
    void commitImpl();
    void pushImpl();
    void setAmend(bool amend);

  private:
    Ui::CommitForm *ui;
    GitStatusTableModel *model;
    GitPlugin *git;
    QString repoRoot;
};
