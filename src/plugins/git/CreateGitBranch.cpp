#include "CreateGitBranch.hpp"
#include "plugins/git/GitPlugin.hpp"
#include "ui_CreateGitBranch.h"

#include <QLineEdit>
#include <QStyle>
#include <qabstractbutton.h>
#include <qapplication.h>

auto static isLightPalette() -> bool {
    QPalette palette = QApplication::palette();
    QColor windowColor = palette.color(QPalette::Window);
    return windowColor.lightness() > 128;
}

CreateGitBranch::CreateGitBranch(QWidget *parent, GitPlugin *p)
    : QDialog(parent), ui(new Ui::CreateGitBranch) {
    ui->setupUi(this);
    this->plugin = p;
    findLocalBranches();
    connect(ui->branchNameEdit, &QLineEdit::textEdited, this, &CreateGitBranch::verifyBranchName);
    connect(ui->createAndCheckoutButton, &QAbstractButton::clicked, this,
            &CreateGitBranch::createBranchAndCheckout);
    connect(ui->createBranchButton, &QAbstractButton::clicked, this,
            &CreateGitBranch::createBranch);
}

CreateGitBranch::~CreateGitBranch() { delete ui; }

void CreateGitBranch::changeEvent(QEvent *e) {
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void CreateGitBranch::verifyBranchName(const QString &newText) {
    QColor searchNotFoundBackgroundColor;
    if (isLightPalette()) {
        searchNotFoundBackgroundColor = QColor(0xFFAAAA);
    } else {
        searchNotFoundBackgroundColor = QColor(0xFF6A6A);
    }

    auto p = palette();
    if (this->availableBranches.contains(newText)) {
        p.setColor(QPalette::Base, searchNotFoundBackgroundColor);
        ui->createAndCheckoutButton->setEnabled(false);
        ui->createBranchButton->setEnabled(false);
    } else {
        p.setColor(QPalette::Base, style()->standardPalette().base().color());
        ui->createAndCheckoutButton->setEnabled(true);
        ui->createBranchButton->setEnabled(true);
    }
    ui->branchNameEdit->setPalette(p);
}

void CreateGitBranch::findLocalBranches() {
    auto [output, exitCode] = plugin->runGit({"branch"});
    if (output.isEmpty()) {
        return;
    }
    this->availableBranches.clear();
    for (auto &line : output.split('\n', Qt::SkipEmptyParts)) {
        auto branchName = line.trimmed();
        if (branchName.startsWith("* ")) {
            branchName.remove(0, 2);
        }
        this->availableBranches.append(branchName.trimmed());
    }
    qDebug() << "Available branches =" << this->availableBranches;
}

void CreateGitBranch::createBranch() {
    ui->gitLogMessage->clear();
    auto res = createBranchImplementation(ui->branchNameEdit->text(), false);
    if (res.isEmpty()) {
        accept();
    }
    ui->gitLogMessage->setText(res);
}

void CreateGitBranch::createBranchAndCheckout() {
    ui->gitLogMessage->clear();
    auto res = createBranchImplementation(ui->branchNameEdit->text(), true);
    if (res.isEmpty()) {
        accept();
    }
    ui->gitLogMessage->setText(res);
}

QString CreateGitBranch::createBranchImplementation(const QString &branchName, bool checkout) {
    QStringList args;
    if (checkout) {
        args = {"checkout", "-b", branchName};
    } else {
        args = {"branch", branchName};
    }
    auto [gitOutput, exitCode] = plugin->runGit(args);
    return gitOutput;
}
