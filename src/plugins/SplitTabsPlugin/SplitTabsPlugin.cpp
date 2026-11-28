#include <QSettings>
#include <iplugin.h>
#include <pluginmanager.h>

#include "SplitTabsPlugin.hpp"
#include "plugins/texteditor/texteditor_plg.h"
#include "widgets/qmdiSplitTab.h"

static void SaveList(QSettings &settings, const QList<int> &list, const QString &name) {
    auto strList = QStringList();
    for (auto val : list) {
        strList.append(QString::number(val));
    }
    settings.setValue(name, strList);
}

static QList<int> LoadList(QSettings &settings, const QString &name) {
    auto result = QList<int>();
    auto strList = settings.value(name).toStringList();
    for (auto &str : strList) {
        auto ok = false;
        auto number = str.toInt(&ok);
        result.append(ok ? number : 0);
    }
    return result;
}

SplitTabsPlugin::SplitTabsPlugin(TextEditorPlugin *p) : IPlugin() {
    name = tr("Split tabs support");
    author = tr("Diego Iastrubni <diegoiast@gmail.com>");
    iVersion = 0;
    sVersion = "0.0.1";
    autoEnabled = true;
    alwaysEnabled = false;

    config.pluginName = "SplitTabsPlugin";
    textEditorPlugin = p;
}

SplitTabsPlugin::~SplitTabsPlugin() {}

void SplitTabsPlugin::loadConfig(QSettings &settings) {
    IPlugin::loadConfig(settings);

    settings.beginGroup(config.pluginName);
    auto savedSplitInternalSizes = LoadList(settings, Config::SplitSizesKey);
    auto savedSplitCount = LoadList(settings, Config::SplitCountKey);

    if (savedSplitInternalSizes.size() != savedSplitCount.size()) {
        qDebug() << "SplitTabsPlugin: sizes does not match split count, not splits will be used";
        savedSplitInternalSizes.clear();
        savedSplitCount.clear();
    } else if (savedSplitCount.contains(0)) {
        qDebug() << "SplitTabsPlugin: split count contains 0, not splits will be used";
        savedSplitInternalSizes.clear();
        savedSplitCount.clear();
    } else if (savedSplitInternalSizes.contains(0)) {
        qDebug() << "SplitTabsPlugin: split sizes contains 0, not splits will be used";
        savedSplitInternalSizes.clear();
        savedSplitCount.clear();
    }

    split->savedSplitInternalSizes = savedSplitInternalSizes;
    split->savedSplitCount = savedSplitCount;
    settings.endGroup();
}

void SplitTabsPlugin::saveConfig(QSettings &settings) {
    IPlugin::saveConfig(settings);

    settings.beginGroup(config.pluginName);
    auto a = this->split->getSplitSizes();
    auto b = this->split->getSplitInternalCount();
    SaveList(settings, a, Config::SplitSizesKey);
    SaveList(settings, b, Config::SplitCountKey);
    settings.endGroup();
}

void SplitTabsPlugin::on_client_merged(qmdiHost *host) {
    IPlugin::on_client_merged(host);
    split = new qmdiSplitTab();
    split->setButtonProvider(new DefaultButtonsProvider);

    auto manager = getManager();
    auto splitAction = new QAction("Split tabs", split);
    splitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash));
    splitAction->setIcon(QIcon::fromTheme("view-split-left-right"));
    connect(splitAction, &QAction::triggered, splitAction, [this]() {
        split->splitHorizontally();
        textEditorPlugin->fileNew();
    });
    auto moveSplitAction = new QAction("Move editor to new split", split);
    moveSplitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backslash));
    connect(moveSplitAction, &QAction::triggered, moveSplitAction, [this]() {
        // TODO - this action should be disabled. I need expose the event of tabs modifies
        //        to 3rd parties, and not keep it an internal event.
        if (split->getWigetsCountInCurrentSplit() < 2) {
            return;
        }
        auto w = split->getCurrentWidget();
        split->moveTabToNewSplit(w);
    });
    connect(split, &qmdiSplitTab::newClientAdded, manager, &PluginManager::newClientAdded);

    manager->removeBuiltinActions();
    manager->menus["Se&ttings"]->addAction(splitAction);
    manager->menus["Se&ttings"]->addAction(moveSplitAction);
    manager->replaceMdiServer(split);
    manager->addBuiltinActions();
    manager->updateGUI();
}

void SplitTabsPlugin::on_client_unmerged(qmdiHost *) {}

void SplitTabsPlugin::setLoadingFinished(bool b) { this->split->loadingFinished = b; }
