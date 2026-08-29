#include "CompilerOutputDecoders.h"

#include <QDir>
#include <QFileInfo>

bool GccOutputDetector::processLine(const QString &line, const QString &, const QString &buildDir) {
    auto match = regionPattern.match(line);
    if (match.hasMatch()) {
        if (!currentStatus.message.isEmpty()) {
            m_compileStatuses.append(currentStatus);
        }

        // gcc errors/warnings start at  line 1, we start and line 0
        auto fileName = match.captured(1);
        // gcc clang use full file names, Go, will also match - but uses relative file names
        if (!fileName.startsWith("./") || !fileName.startsWith(".\\") ||
            fileName.startsWith("../") || fileName.startsWith("..\\")) {
            auto lineNmber = match.captured(2).toInt() - 1;
            auto columnNumber = match.captured(3).toInt() - 1;
            auto type = match.captured(4);
            auto message = match.captured(5);

            auto fi = QFileInfo(fileName);
            if (fi.isRelative()) {
                fileName = QDir::cleanPath(buildDir + QDir::separator() + fileName);
            }
            auto displayName = fi.fileName();
            if (!displayName.isEmpty()) {
                currentStatus =
                    CompileStatus{fileName, displayName, lineNmber,          columnNumber,
                                  type,     message,     "GccOutputDetector"};
            }
            return true;
        }
    } else if (!line.isEmpty()) {
        if (!currentStatus.fileName.isEmpty()) {
            currentStatus.message += "\n";
            currentStatus.message += line;
            return true;
        }
    } else {
        // Empty line (can be treated as a boundary or EOF)
        if (!currentStatus.message.isEmpty()) {
            m_compileStatuses.append(currentStatus);
            currentStatus.message.clear();
            currentStatus.fileName.clear();
        }
    }

    return false;
}

QList<CompileStatus> GccOutputDetector::foundStatus() {
    QList<CompileStatus> result = m_compileStatuses;
    m_compileStatuses.clear(); // Reset the internal list
    return result;
}

void GccOutputDetector::endOfOutput() {
    if (!currentStatus.message.isEmpty()) {
        m_compileStatuses.append(currentStatus);

        // Clear the current message buffer
        currentStatus.message.clear();
        currentStatus.fileName.clear();
    }
}

GeneralDetector::~GeneralDetector() { qDeleteAll(detectors); }

bool GeneralDetector::processLine(const QString &line, const QString &sourceDir,
                                  const QString &buildDir) {
    for (auto detector : detectors) {
        if (detector->processLine(line, sourceDir, buildDir)) {
            return true;
        }
    }
    return false;
}

QList<CompileStatus> GeneralDetector::foundStatus() {
    auto list = QList<CompileStatus>();
    for (auto detector : detectors) {
        list += detector->foundStatus();
    }
    return list;
}

void GeneralDetector::endOfOutput() {
    for (auto detector : detectors) {
        detector->endOfOutput();
    }
}

void GeneralDetector::add(OutputDetector *detector) { detectors.append(detector); }

void GeneralDetector::remove(OutputDetector *detector) {
    if (detectors.removeOne(detector)) {
        delete detector;
    }
}

bool ClOutputDetector::processLine(const QString &line, const QString &, const QString &) {
    static QRegularExpression clPattern(
        R"(([a-zA-Z]:\\[^:]+|\S+)\((\d+),(\d+)\):\s+(\w+)\s+(\w+):\s+(.+))");

    auto match = clPattern.match(line);
    if (match.hasMatch()) {
        auto fileName = match.captured(1);
        auto lineNumber = match.captured(2).toInt() - 1;
        auto columnNumber = match.captured(3).toInt() - 1;
        auto type = match.captured(4);
        // auto code = match.captured(5);
        auto message = match.captured(6);

        auto fi = QFileInfo(fileName);
        auto displayName = fi.fileName();
        compileStatuses.append(CompileStatus{fileName, displayName, lineNumber, columnNumber, type,
                                             message, "ClOutputDetector"});

        return true;
    }

    return false;
}

QList<CompileStatus> ClOutputDetector::foundStatus() {
    QList<CompileStatus> result = compileStatuses;
    compileStatuses.clear();
    return result;
}

void ClOutputDetector::endOfOutput() {
    // nothing
}

bool CargoOutputDetector::processLine(const QString &line, const QString &sourceDir,
                                      const QString &) {
    static QRegularExpression errorPattern(R"(^error: (.+))");
    static QRegularExpression locationPattern(R"(^\s*-->\s*([^:]+):(\d+):(\d+))");
    static QRegularExpression contextPattern(R"(^\s*\|\s*\d+\s*\|\s*(.*))");

    auto contextMatch = contextPattern.match(line);
    if (contextMatch.hasMatch()) {
        QString codeLine = contextMatch.captured(1).trimmed();
        if (!codeLine.isEmpty() && !currentStatus.message.isEmpty()) {
            currentStatus.message += "\nCode: " + codeLine;
        }
        return true;
    }

    auto errorMatch = errorPattern.match(line);
    if (errorMatch.hasMatch()) {
        if (!currentStatus.message.isEmpty()) {
            m_compileStatuses.append(currentStatus);
        }
        currentStatus =
            CompileStatus{"", "", -1, -1, "error", errorMatch.captured(1), "CargoOutputDetector"};
        return true;
    } else {
        auto locationMatch = locationPattern.match(line);
        if (locationMatch.hasMatch()) {
            auto fileName = locationMatch.captured(1);
            if (!fileName.startsWith('\\') && !fileName.startsWith('/') && fileName[1] != ':') {
                fileName = sourceDir + "/" + fileName;
            }
            fileName = QDir::toNativeSeparators(fileName);
            currentStatus.fileName = fileName;
            currentStatus.displayName = QFileInfo(currentStatus.fileName).fileName();
            currentStatus.row = locationMatch.captured(2).toInt() - 1;
            currentStatus.col = locationMatch.captured(3).toInt() - 1;
            return true;
        } else if (!currentStatus.message.isEmpty() && !currentStatus.fileName.isEmpty()) {
            m_compileStatuses.append(currentStatus);
            currentStatus = {};
        }
    }

    return false;
}

QList<CompileStatus> CargoOutputDetector::foundStatus() {
    QList<CompileStatus> result = m_compileStatuses;
    m_compileStatuses.clear();
    return result;
}

void CargoOutputDetector::endOfOutput() {
    if (!currentStatus.message.isEmpty()) {
        m_compileStatuses.append(currentStatus);
        currentStatus = {};
    }
    accumulatedMessage.clear();
}

bool GoLangOutputDetector::processLine(const QString &line, const QString &sourceDir,
                                       const QString &) {
    auto static errorPattern = QRegularExpression(R"(^(.+):(\d+):(\d+):\s*(.*))");
    auto static warningPattern = QRegularExpression(R"(^(.+):(\d+):(\d+):\s*warning:\s*(.*))");
    auto errorMatch = errorPattern.match(line);
    auto warningMatch = warningPattern.match(line);

    if (errorMatch.hasMatch() || warningMatch.hasMatch()) {
        if (!currentStatus.message.isEmpty()) {
            m_compileStatuses.append(currentStatus);
        }

        auto severity = errorMatch.hasMatch() ? "error" : "warning";
        auto match = errorMatch.hasMatch() ? errorMatch : warningMatch;

        auto fileName = match.captured(1);
        if (!fileName.startsWith('\\') && !fileName.startsWith('/') && fileName[1] != ':') {
            fileName = sourceDir + fileName;
            return false;
        }

        currentStatus = CompileStatus{fileName,
                                      QFileInfo(match.captured(1)).fileName(),
                                      match.captured(2).toInt() - 1,
                                      match.captured(3).toInt() - 1,
                                      severity,
                                      match.captured(4),
                                      {}};

        return true; // Line processed as an error or warning
    } else if (!line.trimmed().isEmpty()) {
        if (!currentStatus.message.isEmpty()) {
            currentStatus.message += "\n";
        }
        currentStatus.message += line.trimmed();
        return true;
    } else if (!currentStatus.message.isEmpty() && !currentStatus.fileName.isEmpty()) {
        m_compileStatuses.append(currentStatus);
        currentStatus = {};
        return true;
    }

    return false;
}

QList<CompileStatus> GoLangOutputDetector::foundStatus() {
    QList<CompileStatus> result = m_compileStatuses;
    m_compileStatuses.clear();
    return result;
}

void GoLangOutputDetector::endOfOutput() {
    if (!currentStatus.message.isEmpty()) {
        m_compileStatuses.append(currentStatus);
        currentStatus = {};
    }
    accumulatedMessage.clear();
}
