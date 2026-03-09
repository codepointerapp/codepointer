#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <regex>
#include <string>
#include <vector>

#include <QDirIterator>
#include <QPushButton>
#include <QThreadPool>

#include <pluginmanager.h>
#include <qmdihost.h>

#include "AnsiToHTML.hpp"
#include "ProjectBuildConfig.h"
#include "ProjectManagerPlg.h"
#include "ProjectSearch.h"
#include "ui_ProjectSearchGUI.h"

static auto regexEscape(const std::string &string) -> std::string {
    static const std::string specialChars = "^$\\.*+?()[]{}|";
    std::string result;
    for (char c : string) {
        if (specialChars.find(c) != std::string::npos) {
            result += '\\';
        }
        result += c;
    }
    return result;
}

static auto FilenameMatches(const QString &fileName, const QString &goodList,
                            const QString &badList) -> bool {
    if (!badList.isEmpty()) {
        auto list = badList.split(";");
        for (auto const &rule : std::as_const(list)) {
            if (rule.length() < 3) {
                continue;
            }
            auto clean_rule = rule.trimmed();
            if (clean_rule.isEmpty()) {
                continue;
            }
            auto options = QRegularExpression::UnanchoredWildcardConversion;
            auto pattern = QRegularExpression::wildcardToRegularExpression(rule, options);
            auto regex = QRegularExpression(pattern);
            auto matches = regex.match(fileName).hasMatch();
            if (matches) {
                return false;
            }
        }
    }

    auto filterMatchFound = true;
    if (!goodList.isEmpty()) {
        filterMatchFound = false;
        auto list = goodList.split(";");
        for (const auto &rule : std::as_const(list)) {
            auto clean_rule = rule.trimmed();
            if (clean_rule.isEmpty()) {
                continue;
            }
            auto options = QRegularExpression::UnanchoredWildcardConversion;
            auto pattern = QRegularExpression::wildcardToRegularExpression(rule, options);
            auto regex = QRegularExpression(pattern);
            auto matches = regex.match(fileName).hasMatch();
            if (matches) {
                filterMatchFound = true;
                break;
            }
        }
    }
    return filterMatchFound;
}

struct SearchOptions {
    bool caseSensitive;
    bool wholeWord;
    bool useRegex;
    bool searchInBinaries;
};

auto static searchTextFile(std::ifstream &file, const std::string &searchString,
                           SearchOptions options,
                           std::function<void(const std::string &, size_t)> callback) -> void {
    auto line = std::string();
    auto lineNumber = size_t(0);

    std::regex regex;
    bool useRegexSearch = options.useRegex || options.wholeWord || !options.caseSensitive;

    if (useRegexSearch) {
        auto flags = std::regex_constants::ECMAScript;
        if (!options.caseSensitive) {
            flags |= std::regex_constants::icase;
        }

        auto pattern = std::string();
        if (options.useRegex) {
            pattern = searchString;
        } else {
            pattern = regexEscape(searchString);
            if (options.wholeWord) {
                pattern = "\\b" + pattern + "\\b";
            }
        }

        try {
            regex.assign(pattern, flags);
        } catch (...) {
            return;
        }
    }

    while (std::getline(file, line)) {
        bool match = false;
        if (useRegexSearch) {
            match = std::regex_search(line, regex);
        } else {
            match = (line.find(searchString) != std::string::npos);
        }

        if (match) {
            callback(line, lineNumber);
        }
        lineNumber++;
    }
}

auto static searchBinaryFile(std::ifstream &file, const std::string &searchString,

                             std::function<void(const std::string &, size_t)> callback) -> void {
    if (!file.is_open() || searchString.empty()) {
        return;
    }

    auto constexpr bufferSize = 4096;
    auto const searchLen = searchString.size();
    auto const overlap = searchLen > 1 ? searchLen - 1 : 0;

    auto buffer = std::vector<char>(bufferSize + overlap);
    auto fileOffset = size_t{0};
    auto prevTailSize = size_t{0};

    while (file) {
        file.read(buffer.data() + prevTailSize, bufferSize);
        auto bytesRead = file.gcount();

        if (bytesRead == 0) {
            break;
        }

        auto totalBytes = prevTailSize + bytesRead;
        for (auto i = 0; i + searchLen <= totalBytes; ++i) {
            if (std::memcmp(buffer.data() + i, searchString.data(), searchLen) == 0) {
                auto afterMatch = std::min<size_t>(100, totalBytes - (i + searchLen));
                auto result = std::string(buffer.data() + i + searchLen, afterMatch);

                if (afterMatch < 100) {
                    auto extra = std::vector<char>(100 - afterMatch);
                    auto oldPos = file.tellg();
                    file.seekg(fileOffset + i + searchLen + afterMatch, std::ios::beg);
                    file.read(extra.data(), extra.size());

                    auto extraRead = file.gcount();
                    result.append(extra.data(), extraRead);
                    file.seekg(oldPos);
                }
                callback(result, fileOffset + i);
            }
        }

        if (overlap > 0) {
            std::memmove(buffer.data(), buffer.data() + totalBytes - overlap, overlap);
        }
        fileOffset += bytesRead;
        prevTailSize = overlap;
    }
}

auto static searchFile(const std::string &filename, const std::string &searchString,
                       SearchOptions options,
                       std::function<void(const std::string &, size_t)> callback) -> void {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    auto firstLines = std::string();
    auto line = std::string();
    for (auto i = 0; i < 5; i++) {
        std::getline(file, line);
        firstLines += line;
    }

    file.seekg(0);
    if (isPlainText(QString::fromStdString(firstLines))) {
        searchTextFile(file, searchString, options, callback);
    } else {
        if (options.searchInBinaries) {
            searchBinaryFile(file, searchString, callback);
        }
    }
}

ProjectSearch::ProjectSearch(QWidget *parent, ProjectBuildModel *m)
    : QWidget(parent), ui(new Ui::ProjectSearchGUI) {
    ui->setupUi(this);
    this->model = m;

    QStringList headerLabels;
    headerLabels << tr("Filename/Text") << tr("Line");
    ui->treeWidget->setHeaderLabels(headerLabels);
    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    ui->pathEdit->setFileMode(false);
    ui->pathEdit->setPlaceholderText(tr("Search in directory"));
    ui->pathEdit->setPath(QDir::homePath());

    auto host = dynamic_cast<PluginManager *>(parent);
    connect(ui->treeWidget, &QTreeWidget::itemClicked, this, [host](QTreeWidgetItem *item, int) {
        auto parent = item->parent();
        if (!parent) {
            return;
        }
        auto fileName = parent->text(2);
        auto line = item->text(1).toInt() - 1;
        host->openFile(fileName, line);
        auto w = dynamic_cast<QWidget *>(host->currentClient());
        if (w) {
            w->setFocus();
        }

        // TODO - this would be nice. I am unsure how to do this
        // editor->displayBannerMessage("Loaded", 7);
    });

    connect(ui->sourceCombo, &QComboBox::activated, this, [this](int index) {
        if (index <= 0) {
            this->ui->pathEdit->blockSignals(true);
            auto static lastDir = QDir::toNativeSeparators(QDir::homePath());
            auto lastDir2 = this->ui->pathEdit->path();
            this->ui->pathEdit->setPath(lastDir);
            lastDir = lastDir2;
            this->ui->pathEdit->blockSignals(false);
            return;
        }

        // else - use path from the selected project
        auto project = model->getConfig(index - 1);
        this->ui->pathEdit->setPath(QDir::toNativeSeparators(project->sourceDir));
    });

    connect(ui->searchButton, &QPushButton::clicked, this, &ProjectSearch::searchButton_clicked);

    connect(ui->pathEdit, &PathWidget::pathChanged, this, [this](const QString &newPath) {
        auto i = this->model->findConfigDirIndex(newPath);
        if (i == -1) {
            this->ui->sourceCombo->setCurrentIndex(0);
        } else {
            this->ui->sourceCombo->setCurrentIndex(i + 1);
        }
    });

    connect(ui->collapseFileNames, &QAbstractButton::toggled, this, [this](auto toggled) {
        // we just collapse
        if (!toggled) {
            return;
        }
        auto count = ui->treeWidget->topLevelItemCount(); //
        for (auto i = 0; i < count; ++i) {
            auto item = ui->treeWidget->topLevelItem(i); //
            if (item) {
                ui->treeWidget->collapseItem(item);
            }
        }
    });

    auto updateTooltips = [this]() {
        ui->caseSensitiveBtn->setToolTip(ui->caseSensitiveBtn->isChecked()
                                             ? tr("Case Sensitive (On)")
                                             : tr("Case Sensitive (Off)"));
        ui->wholeWordBtn->setToolTip(ui->wholeWordBtn->isChecked() ? tr("Whole Word (On)")
                                                                   : tr("Whole Word (Off)"));
        ui->regexBtn->setToolTip(ui->regexBtn->isChecked() ? tr("Regular Expression (On)")
                                                           : tr("Regular Expression (Off)"));
        ui->searchInBinaryFiles->setToolTip(ui->searchInBinaryFiles->isChecked()
                                                ? tr("Search in binary files as well")
                                                : tr("Search in text files only"));
    };

    connect(ui->caseSensitiveBtn, &QToolButton::toggled, this, updateTooltips);
    connect(ui->wholeWordBtn, &QToolButton::toggled, this, updateTooltips);
    connect(ui->regexBtn, &QToolButton::toggled, this, updateTooltips);
    connect(ui->searchInBinaryFiles, &QCheckBox::toggled, this, updateTooltips);
    updateTooltips();

    auto validateRegex = [this]() {
        if (!ui->regexBtn->isChecked()) {
            ui->searchFor->setStyleSheet("");
            ui->searchFor->setToolTip("");
            return;
        }

        try {
            std::regex(ui->searchFor->text().toStdString());
            ui->searchFor->setStyleSheet("");
            ui->searchFor->setToolTip("");
        } catch (const std::regex_error &e) {
            ui->searchFor->setStyleSheet("background-color: #550000; color: white;");
            ui->searchFor->setToolTip(e.what());
        }
    };

    connect(ui->searchFor, &QLineEdit::textChanged, this, validateRegex);
    connect(ui->regexBtn, &QToolButton::toggled, this, validateRegex);
    validateRegex();
}

ProjectSearch::~ProjectSearch() { delete ui; }

void ProjectSearch::setFocusOnSearch() {
    ui->searchFor->setFocus();
    ui->searchFor->selectAll();
}

const QString ProjectSearch::getSearchPath() const {
    auto sss = ui->pathEdit->path();
    return sss;
}

void ProjectSearch::setSearchPath(const QString &s) { ui->pathEdit->setText(s); }

const QString ProjectSearch::getSearchPattern() const { return ui->searchFor->text(); }

void ProjectSearch::setSearchPattern(const QString &s) {
    ui->searchFor->setText(s);
    ui->searchFor->selectAll();
}

const QString ProjectSearch::getSearchInclude() const { return ui->includeFiles->text(); }

void ProjectSearch::setSearchInclude(const QString &s) { ui->includeFiles->setText(s); }

const QString ProjectSearch::getSearchExclude() const { return ui->excludeFiles->text(); }

void ProjectSearch::setSearchExclude(const QString &s) { ui->excludeFiles->setText(s); }

auto ProjectSearch::getCollapseFiles() const -> bool { return ui->collapseFileNames->isChecked(); }

void ProjectSearch::setCollapseFiles(bool status) { ui->collapseFileNames->setChecked(status); }

auto ProjectSearch::getSearchCaseSensitive() const -> bool {
    return ui->caseSensitiveBtn->isChecked();
}

auto ProjectSearch::setSearchCaseSensitive(bool status) -> void {
    ui->caseSensitiveBtn->setChecked(status);
}

auto ProjectSearch::getSearchWholeWords() const -> bool { return ui->wholeWordBtn->isChecked(); }

auto ProjectSearch::setSearchWholeWords(bool status) -> void {
    ui->wholeWordBtn->setChecked(status);
}

auto ProjectSearch::getSearchRegex() const -> bool { return ui->regexBtn->isChecked(); }

auto ProjectSearch::setSearchRegex(bool status) -> void { ui->regexBtn->setChecked(status); }

void ProjectSearch::updateProjectList() {
    ui->sourceCombo->clear();
    ui->sourceCombo->addItem(tr("Custom"));
    for (auto i = 0; i < model->rowCount(); i++) {
        auto p = model->getConfig(i);
        ui->sourceCombo->addItem(p->name);
    }
    emit ui->pathEdit->pathChanged(ui->pathEdit->path());
}

void ProjectSearch::searchButton_clicked() {
    static bool running = false;

    if (running) {
        running = false;
        return;
    }

    auto allowList = ui->includeFiles->text();
    auto denyList = ui->excludeFiles->text();
    auto originalText = ui->searchButton->text();
    auto searchText = ui->searchFor->text().toStdString();
    auto startSearchPath = QDir::toNativeSeparators(ui->pathEdit->path());

    this->ui->treeWidget->clear();
    SearchOptions options;
    options.caseSensitive = ui->caseSensitiveBtn->isChecked();
    options.wholeWord = ui->wholeWordBtn->isChecked();
    options.useRegex = ui->regexBtn->isChecked();
    options.searchInBinaries = ui->searchInBinaryFiles->isChecked();
    ui->searchButton->setText("(click to &stop)");
    ui->progressIndicator->start();
    running = true;

    if (allowList.isEmpty()) {
        allowList = "*";
    }
    ui->message->clear();
    QThreadPool::globalInstance()->start([this, originalText, allowList, denyList, searchText,
                                          startSearchPath, options]() {
        auto totalFound = 0;
        auto it = QDirIterator(startSearchPath, allowList.split(";"), QDir::Files,
                               QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto fullFileName = QDir::toNativeSeparators(it.next());
            auto *foundData = new QList<FoundData>;
            if (!fullFileName.startsWith(startSearchPath)) {
                continue;
            }

            auto baseDir = QDir(ui->pathEdit->path());
            auto shortFileName = baseDir.relativeFilePath(fullFileName);
            if (!FilenameMatches(fullFileName, allowList, denyList)) {
                continue;
            };
            searchFile(fullFileName.toStdString(), searchText, options,
                       [foundData, &totalFound](auto line, auto line_number) {
                           foundData->push_back({line, line_number});
                           totalFound++;
                       });

            if (!foundData->empty()) {
                // clang-format off
                QMetaObject::invokeMethod(this, "file_searched", Qt::QueuedConnection,
                    Q_ARG(QString, fullFileName),
                    Q_ARG(QString, shortFileName),
                    Q_ARG(QList<FoundData>*, foundData)
                );
                // clang-format on

                if (totalFound != 0) {
                    // Remember - this is a worker thread, we cannot directly modify the UI
                    QMetaObject::invokeMethod(this, [this, totalFound]() {
                        ui->message->setText(QString("Found %1 times").arg(totalFound));
                    }, Qt::QueuedConnection);
                }
            } else {
                delete foundData;
            }

            if (!running) {
                break;
            }
        }

        // this is done, since the progress indicator needs to be stopped from the main thread
        QTimer::singleShot(0, this, [this, totalFound, originalText]() {
            ui->searchButton->setText(originalText);
            ui->progressIndicator->stop();
            running = false;
            if (totalFound != 0) {
                auto m = QString("Found %1 times").arg(totalFound);
                ui->message->setText(m);
            } else {
                ui->message->setText(QString("Not found"));
            }
        });
    });
}

void ProjectSearch::file_searched(QString fullFileName, QString shortFileName,
                                  QList<FoundData> *foundData) {
    auto dirItem = new QTreeWidgetItem(ui->treeWidget);
    dirItem->setText(0, shortFileName);
    dirItem->setText(2, fullFileName);
    dirItem->setToolTip(0, fullFileName);

    for (const auto &s : *foundData) {
        auto lineItem = new QTreeWidgetItem(dirItem);
        auto trimmedText =
            QString::fromUtf8(s.line.data(), std::min(s.line.size(), size_t{256})).trimmed();

        lineItem->setText(0, trimmedText);
        lineItem->setText(1, QString::number(s.lineNumber + 1));
        lineItem->setToolTip(0, trimmedText);
        lineItem->setToolTip(1, shortFileName);
    }
    delete foundData;
    if (!ui->collapseFileNames->isChecked()) {
        ui->treeWidget->expandItem(dirItem);
    }
    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
}
