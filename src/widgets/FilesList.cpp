/**
 * \file FileList.cpp
 * \brief Imeplementation of a flat file lister
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include "FilesList.hpp"
#include "LoadingWidget.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QListView>
#include <QQueue>
#include <QRegularExpression>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

// Normalize path to use `/`
static inline QString normalizeFilePath(const QString &path) {
    return QDir::fromNativeSeparators(path);
}

static inline QString normalizeDirPath(const QString &path) {
    auto p = QDir::fromNativeSeparators(path);
    if (!p.endsWith('/')) {
        p = p + '/';
    }
    return p;
}

// Convert glob to regex list
static QList<QRegularExpression> toRegexList(const QStringList &patterns) {
    auto list = QList<QRegularExpression>();
    for (auto &pat : patterns) {
        auto rx = QString();
        for (auto ch : pat.trimmed()) {
            if (ch == '*') {
                rx += ".*";
            } else if (ch == '?') {
                rx += '.';
            } else if (QString("[](){}.+^$|\\").contains(ch)) {
                rx += '\\';
                rx += ch;
            } else {
                rx += ch;
            }
        }
        if (!rx.isEmpty()) {
            list << QRegularExpression("^" + rx + "$", QRegularExpression::CaseInsensitiveOption);
        }
    }
    return list;
}

FileScannerWorker::FileScannerWorker(QObject *parent) : QObject(parent) {}

void FileScannerWorker::setRootDir(const QString &dir) { rootDir = dir; }

void FileScannerWorker::start() {
    QElapsedTimer timer;
    timer.start();
    shouldStop = false;
    scanDir(rootDir);
    emit finished(timer.elapsed());
}

void FileScannerWorker::scanDir(const QString &rootPath) {
    auto chunk = QStringList();
    auto const chunkSize = 1000;
    auto queue = QQueue<QString>();
    queue.enqueue(rootPath);

    while (!queue.isEmpty()) {
        auto dirPath = queue.dequeue();
        auto dir = QDir(dirPath);
        auto entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        for (auto const &fi : std::as_const(entries)) {
            if (shouldStop) {
                qDebug() << "Requested to abort" << rootPath;
                return;
            }
            if (fi.isDir()) {
                queue.enqueue(normalizeFilePath(fi.absoluteFilePath()));
            } else {
                auto rel = QDir(rootDir).relativeFilePath(fi.absoluteFilePath());
                chunk << normalizeFilePath(rel);
                if (chunk.size() >= chunkSize) {
                    emit filesChunkFound(chunk);
                    chunk.clear();
                    QThread::msleep(20);
                }
            }
        }
    }
    if (!chunk.isEmpty()) {
        emit filesChunkFound(chunk);
    }
}

FilesList::FilesList(QWidget *parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    excludeEdit = new QLineEdit(this);
    showEdit = new QLineEdit(this);
    loadingWidget = new LoadingWidget(this);
    filesModel = new FilesListModel();
    displayList = new QListView(this);
    displayList->setAlternatingRowColors(true);
    displayList->setModel(filesModel);

    showEdit->setClearButtonEnabled(true);
    showEdit->setPlaceholderText(tr("Files to show (e.g. main;*.cpp;*.h)"));
    showEdit->setToolTip(showEdit->placeholderText());

    excludeEdit->setClearButtonEnabled(true);
    excludeEdit->setPlaceholderText(tr("Files to hide (e.g. build;*.o*;.git)"));
    excludeEdit->setToolTip(excludeEdit->placeholderText());

    layout->addWidget(loadingWidget);
    layout->addWidget(displayList);
    layout->addWidget(showEdit);
    layout->addWidget(excludeEdit);

    connect(displayList, &QListView::clicked, this, [this](const QModelIndex &modelIndex) {
        auto fileName = this->directory + QDir::separator() + modelIndex.data().toString();
        auto fileInfo = QFileInfo(fileName);
        fileName = fileInfo.absoluteFilePath();
        fileName = QDir::toNativeSeparators(fileName);
        emit fileSelected(fileName);
    });
    connect(excludeEdit, &QLineEdit::textChanged, this, &FilesList::scheduleUpdateList);
    connect(showEdit, &QLineEdit::textChanged, this, &FilesList::scheduleUpdateList);

    updateTimer = new QTimer(this);
    updateTimer->setSingleShot(true);
    updateTimer->setInterval(300);
    connect(updateTimer, &QTimer::timeout, this, [this]() { updateList(allFilesList, true); });
    updateList(allFilesList, true);
}

void FilesList::setExcludeListEnabled(bool state) { this->excludeEdit->setEnabled(state); }

void FilesList::setExcludeList(const QString &filter) {
    this->excludeEdit->setText(filter);
    scheduleUpdateList();
}

void FilesList::setShowListEnabled(bool state) { this->showEdit->setEnabled(state); }

void FilesList::setShowList(const QString &filter) {
    this->showEdit->setText(filter);
    scheduleUpdateList();
}

void FilesList::setDir(const QString &dir) {
    auto dd = normalizeDirPath(dir);
    if (dd == directory) {
        return;
    }
    clear();
    directory = normalizeDirPath(dir);
    filesModel->setBaseDir(dir);
    filesModel->clear();
    if (worker) {
        worker->requestStop();
    }
    scanThread = new QThread;
    worker = new FileScannerWorker;
    worker->moveToThread(scanThread);
    worker->setRootDir(directory);
    auto currentGen = scanGeneration;
    auto *w = worker;
    connect(w, &FileScannerWorker::filesChunkFound, this,
            [this, w, currentGen](const QStringList &chunk) {
                if (currentGen != scanGeneration || w != this->worker) {
                    return;
                }
                allFilesList.append(chunk);
                updateList(chunk, false);
            });
    connect(w, &FileScannerWorker::finished, this, [this, w](qint64 ms) {
        auto msg = w->requestedStop() ? "Scan aborted after" : "Scan finished in";
        qDebug() << "FilesList::setDir" << msg << ms << "ms, " << w->getRootDir();
        if (w == this->worker) {
            this->worker = nullptr;
            this->scanThread->quit();
            this->scanThread = nullptr;
            this->loadingWidget->stop();
        }
        w->deleteLater();
    });
    connect(scanThread, &QThread::started, worker, &FileScannerWorker::start);
    connect(scanThread, &QThread::finished, scanThread, &QObject::deleteLater);
    scanThread->start();
    loadingWidget->start();
    loadingWidget->setToolTip({});
}

void FilesList::setFiles(const QStringList &files) {
    allFilesList = files;
    scheduleUpdateList();
}

void FilesList::clear() {
    scanGeneration++;

    if (worker) {
        disconnect(worker, nullptr, this, nullptr);
        worker->requestStop();
        worker = nullptr;
    }

    if (scanThread) {
        disconnect(scanThread, nullptr, this, nullptr);
        scanThread->quit();
        scanThread->wait();
        scanThread = nullptr;
    }

    allFilesList.clear();
    filesModel->clear();
    loadingWidget->stop();
    directory.clear();
}

const QStringList &FilesList::currentFilteredFiles() const { return filesModel->getFiles(); }

void FilesList::scheduleUpdateList() {
    if (updateTimer->isActive()) {
        updateTimer->stop();
    }
    updateTimer->start();
}

bool FilesList::matchesFilters(const QString &filename,
                               const QList<QRegularExpression> &excludeRegexes,
                               const QList<QRegularExpression> &showRegexes,
                               const QStringList &showTokens) const {
    auto normPath = normalizeFilePath(filename);
    auto segments = normPath.split('/', Qt::SkipEmptyParts);

    for (auto const &rx : excludeRegexes) {
        for (auto const &segment : segments) {
            if (rx.match(segment).hasMatch()) {
                return false;
            }
        }
    }

    if (!showRegexes.isEmpty() || !showTokens.isEmpty()) {
        auto matched = false;
        for (auto const &rx : showRegexes) {
            for (auto const &segment : segments) {
                if (rx.match(segment).hasMatch()) {
                    matched = true;
                    break;
                }
            }
            if (matched) {
                break;
            }
        }
        if (!matched && !showTokens.isEmpty()) {
            for (auto const &segment : segments) {
                const auto lowerSegment = segment.toLower();
                for (auto const &token : showTokens) {
                    if (lowerSegment.contains(token)) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    break;
                }
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

void FilesList::updateList(const QStringList &chunk, bool clearList) {
    auto excludesText = excludeEdit->text();
    auto showsText = showEdit->text();
    QThreadPool::globalInstance()->start([this, chunk, clearList, excludesText, showsText]() {
        auto excludes = toRegexList(excludesText.split(';', Qt::SkipEmptyParts));
        auto shows = toRegexList(showsText.split(';', Qt::SkipEmptyParts));
        auto showTokens = showsText.toLower().split(';', Qt::SkipEmptyParts);
        auto filtered = QStringList();

        for (auto const &rel : chunk) {
            if (matchesFilters(rel, excludes, shows, showTokens)) {
                filtered << rel;
            }
        }

        filtered.sort(Qt::CaseInsensitive);
        QTimer::singleShot(0, this, [this, clearList, filtered]() {
            if (clearList) {
                this->filesModel->clear();
            }

            filesModel->addFiles(filtered);
        });
    });
}

void FilesListModel::clear() {
    beginResetModel();
    displayFiles.clear();
    endResetModel();
}

void FilesListModel::setBaseDir(const QString &newBaseDir) { baseDir = newBaseDir; }

void FilesListModel::addFiles(const QStringList &l) {
    beginResetModel();
    displayFiles.append(l);
    endResetModel();
}

int FilesListModel::rowCount(const QModelIndex &) const { return displayFiles.size(); }

QVariant FilesListModel::data(const QModelIndex &index, int role) const {
    switch (role) {
    case Qt::DisplayRole:
        return displayFiles[index.row()];
        break;
    case Qt::ToolTipRole:
        return baseDir + displayFiles[index.row()];
        break;
    default:
        return {};
    }
}
