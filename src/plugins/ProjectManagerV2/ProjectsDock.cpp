/**
 * \file ProjectDock.cpp
 * \brief Project definition
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "ProjectsDock.hpp"
#include "widgets/FilesList.hpp"
#include "widgets/SearchableMenuButton.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QTextLine>
#include <QToolButton>
#include <QVBoxLayout>

ProjectsDock::ProjectsDock(QWidget *parent) : QWidget(parent) {
    // TODO maybe port to a box layout?
    auto vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    setLayout(vl);

    {
        auto hl = new QHBoxLayout(this);
        auto addProject = new QToolButton(this);
        auto delProject = new QToolButton(this);
        projectsCombo = new QComboBox(this);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);
        hl->addWidget(projectsCombo);
        hl->addWidget(addProject);
        hl->addWidget(delProject);
        vl->addWidget(new QLabel("Current project"));
        vl->addLayout(hl);

        connect(addProject, &QAbstractButton::clicked, addProject,
                [this]() { emit newProjectRequested(); });
        connect(delProject, &QAbstractButton::clicked, addProject,
                [this]() { emit newProjectRequested(); });

        addProject->setAutoRaise(true);
        addProject->setObjectName("addProjectToolButton");
        addProject->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ListAdd));
        addProject->setToolTip(tr("Load a new project"));
        delProject->setAutoRaise(true);
        delProject->setObjectName("delProjectToolButton");
        delProject->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ListRemove));
        delProject->setToolTip(tr("Unload the current project"));
    }

    {
        auto hl = new QHBoxLayout(this);
        auto targetsCombo = new QComboBox(this);
        auto configureTasks = new QToolButton(this);
        auto delBuildDir = new QToolButton(this);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        hl->addWidget(targetsCombo);
        hl->addWidget(configureTasks);
        hl->addWidget(delBuildDir);
        vl->addWidget(new QLabel("Current task"));
        vl->addLayout(hl);

        configureTasks->setAutoRaise(true);
        configureTasks->setObjectName("configureTasksToolButton");
        configureTasks->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentOpen));
        configureTasks->setToolTip(tr("Configure tasks for this project"));
        delBuildDir->setAutoRaise(true);
        delBuildDir->setObjectName("delBuildDirToolButton");
        delBuildDir->setIcon(QIcon::fromTheme(QString::fromUtf8("edit-delete")));
        delBuildDir->setToolTip(tr("Delete build dir"));
    }

    {
        auto hl = new QHBoxLayout(this);
        auto binariesCombo = new QComboBox(this);
        auto reload = new QToolButton(this);

        reload->setIcon(QIcon::fromTheme("view-refresh"));
        reload->setAutoRaise(true);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        hl->addWidget(binariesCombo);
        hl->addWidget(reload);
        vl->addWidget(new QLabel("Curreny binary"));
        vl->addLayout(hl);
    }

    {
        auto filesList = new FilesList(this);
        vl->addWidget(filesList);
    }

    {
        auto hl = new QHBoxLayout(this);
        auto kitsCombo = new QComboBox(this);
        auto menu = new QToolButton(this);

        menu->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentProperties));
        hl->addWidget(new QLabel("Kits"));
        hl->addWidget(menu);

        vl->addLayout(hl);
        vl->addWidget(kitsCombo);
    }
}

void ProjectsDock::setProjectsModel(QAbstractItemModel *model) { projectsCombo->setModel(model); }

void ProjectsDock::selectProject(std::shared_ptr<ProjectDefinition> project) {
    // FIXME: we need to find from the model the project index.
    auto i = 0;
    projectsCombo->setCurrentIndex(i);
}