#pragma once

#include <QWidget>
#include <qmdiclient.h>

namespace Ui {
class CommitForm;
}

class GitPlugin;

class CommitForm : public QWidget, public qmdiClient
{
    Q_OBJECT

public:
    explicit CommitForm(const QString &dir, GitPlugin *plugin, QWidget *parent);
    ~CommitForm();

private:
    Ui::CommitForm *ui;
    GitPlugin *git;
    QString repoRoot;
};
