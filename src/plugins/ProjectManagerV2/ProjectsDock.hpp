/**
 * \file ProjectsDock.hpp
 * \brief Projects dock
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QWidget>
#include <memory>

class QAbstractItemModel;
class QComboBox;

class ProjectDefinition;
class SearchableMenuButton;

class ProjectsDock : public QWidget {
    Q_OBJECT
  public:
    ProjectsDock(QWidget *parent);
    void setProjectsModel(QAbstractItemModel *model);
    void selectProject(std::shared_ptr<ProjectDefinition> project);

  signals:
    void newProjectRequested();
    void projectRemovalRequested(std::shared_ptr<ProjectDefinition> project, int index);
    void newProjectSelected(std::shared_ptr<ProjectDefinition> project, int index);

  private:
    QComboBox *projectsCombo;
};
