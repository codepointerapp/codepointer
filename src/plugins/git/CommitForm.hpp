#pragma once

#include <QWidget>
#include <qmdiclient.h>

namespace Ui {
class CommitForm;
}

namespace Qutepart {
class Qutepart;
}

class GitPlugin;
class GitStatusTableModel;

class CommitForm : public QWidget, public qmdiClient {
    Q_OBJECT

  public:
    explicit CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent);
    ~CommitForm();

  protected:
    void keyPressEvent(QKeyEvent *event) override;

  public slots:
    void updateGitStatus();
    void newFileSelected(const QString &filename);
    void revertCurrentImpl();
    void revertSelectionImpl();

  private:
    Ui::CommitForm *ui;
    GitStatusTableModel *model;
    GitPlugin *git;
    Qutepart::Qutepart *editor;
    QString repoRoot;
};
