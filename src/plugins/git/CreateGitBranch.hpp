#pragma once

#include <QDialog>

namespace Ui {
class CreateGitBranch;
}

class GitPlugin;
class QLineEdit;

class CreateGitBranch : public QDialog {
    Q_OBJECT

  public:
    explicit CreateGitBranch(QWidget *parent, GitPlugin *p);
    ~CreateGitBranch();

  protected:
    void changeEvent(QEvent *e);

  private slots:
    void verifyBranchName(const QString &newText);
    void findLocalBranches();
    void createBranchAndCheckout();
    void createBranch();

  private:
    void createBranchImplementation(const QString &branchName, bool checkout);
    Ui::CreateGitBranch *ui;
    GitPlugin *plugin;
    QStringList availableBranches;
};
