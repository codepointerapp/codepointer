#pragma once

#include <QAbstractItemModel>
#include <QSet>
#include <QVector>
#include <QWidget>

#include "CompilerOutputDecoders.h"

namespace Ui {
class ProjectIssuesWidget;
}

class PluginManager;
class qmdiClient;

class CompileStatusModel : public QAbstractTableModel {
    Q_OBJECT

  public:
    explicit CompileStatusModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void clearAll();
    void addItem(const CompileStatus &status);
    CompileStatus getItem(const QModelIndex &index) const;
    QList<CompileStatus> getItemsFor(const QString &filename) const;

    void setWarningsVisible(bool visible);
    void setErrorsVisible(bool visible);
    void setOthersVisible(bool visible);

    bool areWarningsVisible() const;
    bool areErrorsVisible() const;
    bool areOthersVisible() const;

  private:
    QVector<CompileStatus> statuses;
    QVector<CompileStatus> filteredStatuses;
    QStringList headers;
    bool showWarnings;
    bool showErrors;
    bool showOthers;

    void applyFilter();
    bool shouldShowStatus(const CompileStatus &status) const;
};

class ProjectIssuesWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ProjectIssuesWidget(PluginManager *parent = nullptr);
    ~ProjectIssuesWidget();

    void processLine(const QString &line, int lineNumber, const QString &sourceDir,
                     const QString &buildDir);
    inline void clearAllIssues() { model->clearAll(); }

  protected:
    void changeEvent(QEvent *e);
    void decorateClient(qmdiClient *client);

    /// Drops this widget's marks from every open editor. Scoped by owner, so
    /// clangd's diagnostics on the same lines survive.
    void clearEditorStatuses();

  private:
    PluginManager *manager;
    CompileStatusModel *model;
    Ui::ProjectIssuesWidget *ui;
    GeneralDetector outputDetector;
};
