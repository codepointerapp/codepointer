/**
 * \file ProjectDefinition.hpp
 * \brief Project definition
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include "ProjectDefinition.hpp"

namespace {

using StringHash = QHash<QString, QString>;
using StringPair = QPair<QString, QString>;

// FIXME: this should be moved to a global place,
auto static expandString(const QString &input, QHash<QString, QString> dictionary) -> QString {
    static auto regex = QRegularExpression(R"(\$\{([a-zA-Z0-9_]+)\})");
    auto output = input;
    auto depth = 0;
    auto maxDepth = 10;

    while (depth < maxDepth) {
        auto it = regex.globalMatch(output);
        if (!it.hasNext()) {
            break;
        }
        while (it.hasNext()) {
            auto match = it.next();
            auto key = match.captured(1);
            auto replacement = dictionary.value(key, "");
            output.replace(match.captured(0), replacement);
        }
        depth++;
    }
    return output;
}

auto static parsePlatformCommands(const QJsonObject &commandsObj, TaskInfo &taskInfo) -> void {
    for (auto it = commandsObj.begin(); it != commandsObj.end(); ++it) {
        auto value = it.value();
        if (value.isString()) {
            taskInfo.commands.insert(it.key(), QStringList{value.toString()});
        } else if (value.isArray()) {
            QStringList commands;
            for (const auto &cmd : value.toArray()) {
                commands.append(cmd.toString());
            }
            taskInfo.commands.insert(it.key(), commands);
        }
    }
}

auto static savePlatformCommands(const QHash<QString, QStringList> &commands,
                                 QJsonObject &commandsObj) -> auto {
    for (auto it = commands.constBegin(); it != commands.constEnd(); ++it) {
        auto commandsList = it.value();
        if (commandsList.size() == 1) {
            commandsObj[it.key()] = commandsList.first();
        } else {
            auto commandsArray = QJsonArray();
            for (const auto &cmd : commandsList) {
                commandsArray.append(cmd);
            }
            commandsObj[it.key()] = commandsArray;
        }
    }
}

auto static arePlatformCommandsIdentical(const QHash<QString, QStringList> &commands) -> bool {
    if (commands.isEmpty()) {
        return false;
    }
    auto firstCommand = commands.begin().value();
    for (auto it = std::next(commands.begin()); it != commands.end(); ++it) {
        if (it.value() != firstCommand) {
            return false;
        }
    }
    return true;
}

auto getExecutableFromTargetFile(const QString &filePath) -> std::optional<StringPair> {
    auto file = QFile(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "getExecutableFromTargetFile: Failed to open target file:" << filePath;
        return std::nullopt;
    }

    auto doc = QJsonDocument::fromJson(file.readAll());
    auto obj = doc.object();
    if (obj["type"].toString() != "EXECUTABLE") {
        return std::nullopt;
    }

    auto artifacts = obj["artifacts"].toArray();
    if (artifacts.isEmpty()) {
        return std::nullopt;
    }
    auto name = obj["name"].toString();
    auto path = artifacts[0].toObject()["path"].toString();
    return StringPair{name, path};
}

auto getExecutablesFromCMakeFileAPI(const QString &buildDir) -> StringHash {
    auto executables = StringHash();
    auto replyDir = QDir(buildDir + "/.cmake/api/v1/reply");
    if (!replyDir.exists()) {
        qWarning() << "getExecutablesFromCMakeFileAPI: Reply directory does not exist:"
                   << replyDir.path();
        return executables;
    }

    auto indexFiles = replyDir.entryList({"index-*.json"}, QDir::Files);
    if (indexFiles.isEmpty()) {
        qWarning() << "getExecutablesFromCMakeFileAPI: No index file found";
        return executables;
    }

    auto indexFilePath = replyDir.filePath(indexFiles.first());
    auto indexFile = QFile(indexFilePath);
    if (!indexFile.open(QIODevice::ReadOnly)) {
        qWarning() << "getExecutablesFromCMakeFileAPI: Failed to open index file:" << indexFilePath;
        return executables;
    }

    auto indexDoc = QJsonDocument::fromJson(indexFile.readAll());
    auto indexObj = indexDoc.object();
    auto objects = indexObj["objects"].toArray();

    auto codemodelFile = QString();
    for (const auto &entry : std::as_const(objects)) {
        auto obj = entry.toObject();
        if (obj["kind"].toString() == "codemodel") {
            codemodelFile = obj["jsonFile"].toString();
            break;
        }
    }

    if (codemodelFile.isEmpty()) {
        qWarning() << "getExecutablesFromCMakeFileAPI: No codemodel found in index file";
        return executables;
    }

    auto codemodelPath = replyDir.filePath(codemodelFile);
    auto codemodel = QFile(codemodelPath);
    if (!codemodel.open(QIODevice::ReadOnly)) {
        qWarning() << "getExecutablesFromCMakeFileAPI: Failed to open codemodel file:"
                   << codemodelPath;
        return executables;
    }

    auto codeDoc = QJsonDocument::fromJson(codemodel.readAll());
    auto codeObj = codeDoc.object();
    auto configurations = codeObj["configurations"].toArray();
    if (configurations.isEmpty()) {
        qWarning() << "getExecutablesFromCMakeFileAPI: No configurations in codemodel";
        return executables;
    }

    auto targets = configurations[0].toObject()["targets"].toArray();
    for (auto const &target : std::as_const(targets)) {
        auto obj = target.toObject();
        auto targetFile = obj["jsonFile"].toString();
        if (targetFile.isEmpty()) {
            continue;
        }

        auto targetFilePath = replyDir.filePath(targetFile);
        auto result = getExecutableFromTargetFile(targetFilePath);
        if (result.has_value()) {
            executables.insert(result->first, result->second);
        }
    }

    return executables;
}

auto static cargoListBinUnits(const QString &metaData) -> StringHash {
    auto fileMap = StringHash{};

    auto file = QFile(metaData);
    if (!file.open(QFile::OpenModeFlag::ReadOnly)) {
        return fileMap;
    }

    auto output = file.readAll();
    auto jsonDoc = QJsonDocument::fromJson(output);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        qWarning() << "cargoListBinUnits: Failed to parse cargo metadata JSON";
        return fileMap;
    }

    auto rootObj = jsonDoc.object();
    auto targetDirStr = rootObj.value("target_directory").toString();
    if (targetDirStr.isEmpty()) {
        qWarning() << "cargoListBinUnits: target_directory missing";
        return fileMap;
    }

    auto targetDir = QDir(targetDirStr);
    auto packages = rootObj.value("packages").toArray();
    for (const auto &packageVal : std::as_const(packages)) {
        if (!packageVal.isObject()) {
            continue;
        }

        auto packageObj = packageVal.toObject();
        auto targets = packageObj.value("targets").toArray();

        for (const auto &targetVal : std::as_const(targets)) {
            if (!targetVal.isObject()) {
                continue;
            }

            auto targetObj = targetVal.toObject();
            auto kindArray = targetObj.value("kind").toArray();
            bool isBin = std::any_of(kindArray.constBegin(), kindArray.constEnd(),
                                     [](const auto &val) { return val.toString() == "bin"; });
            if (!isBin) {
                continue;
            }

            auto targetName = targetObj.value("name").toString();
#ifdef Q_OS_WIN
            auto exeName = targetName + ".exe";
#else
            auto exeName = targetName;
#endif
            auto exePath = targetDir.filePath(QDir("debug").filePath(exeName));
            fileMap.insert(targetName, exePath);
        }
    }
    return fileMap;
}

} // namespace

std::shared_ptr<ProjectDefinition> ProjectDefinition::tryGuessFromCMake(const QString &fileName) {
    auto fi = QFileInfo(fileName);
    if (fi.fileName().compare("cmakelists.txt", Qt::CaseSensitivity::CaseInsensitive) != 0) {
        return {};
    }
    auto di = fi.dir();
    if (!fi.isReadable()) {
        return {};
    }

    auto value = std::make_shared<ProjectDefinition>();
    value->projectType = ProjectType::cmake;
    value->autoGenerated = true;
    value->name = fi.dir().dirName() + " (CMake)";
    value->sourceDir = fi.dir().absolutePath();
    value->hideFilter = ".git;.vscode;.vs;cbuild;dist;out;build";
    value->buildDir = QString("${source_directory}%1cbuild").arg(QDir::separator());

    // Debug configuration
    {
        auto t = TaskInfo();
        t.name = "CMake (configure/Debug)";
        t.tooltip = "cmake -S ${source_directory} -B ${build_directory} -DCMAKE_BUILD_TYPE=Debug";

        // clang-format off
        t.commands.insert( PLATFORM_LINUX, {
            "mkdir -p ${build_directory}/.cmake/api/v1/query/",
            "touch ${build_directory}/.cmake/api/v1/query/codemodel-v2",
            "cmake -S ${source_directory} -B ${build_directory} -DCMAKE_BUILD_TYPE=Debug",
        });

        // FIXME:  how about we port to powershell?
        // Why not running the commands directly on this shell instead of spawning a new one?
        // Great question! This is because commands may fail, and I don't want them to kill the
        // build system, and I cannot use "| rem" easily.
        // Note also the clang format thingie, the lines are too long and then are separated
        // which makes reading the command very hard.
        t.commands.insert( PLATFORM_WINDOWS, {
            "cmd /c \"mkdir \"${build_directory}\\.cmake\\api\\v1\\query\" >nul 2>nul || rem\"",
            "cmd /c \"type nul > \"${build_directory}\\.cmake\\api\\v1\\query\\codemodel-v2\" || rem\"",
            "cmake -S \"${source_directory}\" -B \"${build_directory}\" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1",
        });
        // clang-format on
        t.runDirectory = "${source_directory}";
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }

    // Release configuration
    {
        auto t = TaskInfo();
        t.name = "CMake (configure/Release)";
        t.tooltip = "cmake -S ${source_directory} -B ${build_directory} -DCMAKE_BUILD_TYPE=Release";
        // clang-format off
        t.commands.insert( PLATFORM_LINUX, {
            "mkdir -p ${build_directory}/.cmake/api/v1/query/",
            "touch ${build_directory}/.cmake/api/v1/query/codemodel-v2",
            "cmake -S ${source_directory} -B ${build_directory} -DCMAKE_BUILD_TYPE=Release",
        });
        t.commands.insert( PLATFORM_WINDOWS, {
            "cmd /c \"mkdir \"${build_directory}\\.cmake\\api\\v1\\query\" >nul 2>nul || rem\"",
            "cmd /c \"type nul > \"${build_directory}\\.cmake\\api\\v1\\query\\codemodel-v2\" || rem\"",
            "cmake -S \"${source_directory}\" -B \"${build_directory}\" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1"
        });
        // clang-format on
        t.runDirectory = "${source_directory}";
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }

    // Build commands
    auto cmakeBuildParallel = "cmake --build ${build_directory} --parallel ${cpu_count}";
    auto cmakeBuildSingle = "cmake --build ${build_directory}";

    {
        auto t = TaskInfo();
        t.name = "CMake Build (parallel)";
        t.commands.insert(PLATFORM_LINUX, {cmakeBuildParallel});
        t.commands.insert(PLATFORM_WINDOWS, {cmakeBuildParallel});
        t.runDirectory = "${source_directory}";
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "CMake Build (single thread)";
        t.commands.insert(PLATFORM_LINUX, {cmakeBuildSingle});
        t.commands.insert(PLATFORM_WINDOWS, {cmakeBuildSingle});
        t.runDirectory = "${source_directory}";
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }

    value->updateBinariesCMake();
    return value;
}

std::shared_ptr<ProjectDefinition> ProjectDefinition::tryGuessFromCargo(const QString &fileName) {
    auto fi = QFileInfo(fileName);
    if (fi.fileName().compare("cargo.toml", Qt::CaseSensitivity::CaseInsensitive) != 0) {
        return {};
    }
    if (!fi.isReadable()) {
        return {};
    }

    auto value = std::make_shared<ProjectDefinition>();
    value->projectType = ProjectType::cargo;
    value->autoGenerated = true;
    value->name = fi.dir().dirName() + " (Cargo)";
    value->sourceDir = fi.dir().absolutePath();
    value->hideFilter = ".git;.vscode;target";
    value->buildDir = QString("${source_directory}%1target").arg(QDir::separator());

    auto cargoBuild = "cargo build";
    auto cargoBuildRelease = "cargo build --release";
    auto cargoUpdate = "cargo update";
    auto cargoClean = "cargo clean";

    // FIXME: will this work on Windows?
    auto cargoListPackages =
        "(cargo metadata --format-version=1 --no-deps > ${build_directory}/cargo-metadata.json)";

    {
        auto t = TaskInfo();
        t.name = "cargo build";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {cargoBuild, cargoListPackages});
        t.commands.insert(PLATFORM_WINDOWS, {cargoBuild, cargoListPackages});
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "cargo build (release)";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {cargoBuildRelease, cargoListPackages});
        t.commands.insert(PLATFORM_WINDOWS, {cargoBuildRelease, cargoListPackages});
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = cargoClean;
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {cargoClean});
        t.commands.insert(PLATFORM_WINDOWS, {cargoClean});
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "cargo update";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {cargoUpdate});
        t.commands.insert(PLATFORM_WINDOWS, {cargoUpdate});
        value->tasksInfo.push_back(t);
    }

    value->updateBinariesCargo();
    return value;
}

std::shared_ptr<ProjectDefinition> ProjectDefinition::tryGuessFromGo(const QString &fileName) {
    auto fi = QFileInfo(fileName);
    if (fi.fileName().compare("go.mod", Qt::CaseSensitivity::CaseInsensitive) != 0) {
        return {};
    }
    if (!fi.isReadable()) {
        return {};
    }

    auto value = std::make_shared<ProjectDefinition>();
    value->projectType = ProjectType::golang;
    value->autoGenerated = true;
    value->name = fi.dir().dirName() + " (Go)";
    value->sourceDir = fi.dir().absolutePath();
    value->hideFilter = ".git;.vscode;";
    value->buildDir = "";

    auto goBuild = "go build";
    auto goFix = "go fix";

    {
        auto t = TaskInfo();
        t.name = "go build";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {goBuild});
        t.commands.insert(PLATFORM_WINDOWS, {goBuild});
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "go fix";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {goFix});
        t.commands.insert(PLATFORM_WINDOWS, {goFix});
        value->tasksInfo.push_back(t);
    }
    value->updateBinariesGo();
    return value;
}

std::shared_ptr<ProjectDefinition> ProjectDefinition::tryGuessFromMeson(const QString &fileName) {
    auto fi = QFileInfo(fileName);
    if (fi.fileName().compare("meson.build", Qt::CaseSensitivity::CaseInsensitive) != 0) {
        return {};
    }
    if (!fi.isReadable()) {
        return {};
    }

    auto value = std::make_shared<ProjectDefinition>();
    value->projectType = ProjectType::meson;
    value->autoGenerated = true;
    value->name = fi.dir().dirName() + " (Meson)";
    value->sourceDir = fi.dir().absolutePath();
    value->hideFilter = ".git;.vscode;mbuild;build";
    value->buildDir = QString("${source_directory}%1mbuild").arg(QDir::separator());

    auto mesonSetup = "meson setup ${build_directory}";
    auto mesonBuild = "meson compile -C ${build_directory}";
    auto mesonTest = "meson test -C ${build_directory}";

    {
        auto t = TaskInfo();
        t.name = "meson setup";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {mesonSetup});
        t.commands.insert(PLATFORM_WINDOWS, {mesonSetup});
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "meson build";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {mesonBuild});
        t.commands.insert(PLATFORM_WINDOWS, {mesonBuild});
        t.isBuild = true;
        value->tasksInfo.push_back(t);
    }
    {
        auto t = TaskInfo();
        t.name = "meson tests";
        t.runDirectory = "${source_directory}";
        t.commands.insert(PLATFORM_LINUX, {mesonTest});
        t.commands.insert(PLATFORM_WINDOWS, {mesonTest});
        value->tasksInfo.push_back(t);
    }
    value->updateBinariesMeson();
    return value;
}

std::shared_ptr<ProjectDefinition>
ProjectDefinition::tryLoadFromCodePointer(const QString &jsonFileName) {
    auto file = QFile();
    file.setFileName(jsonFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    auto value = std::shared_ptr<ProjectDefinition>();
    auto fi = QFileInfo(jsonFileName);
    auto json = QJsonDocument::fromJson(file.readAll());
    file.close();

    auto toHash = [](QJsonValueRef v) -> QHash<QString, QString> {
        QHash<QString, QString> hash;
        if (v.isObject()) {
            auto jsonObj = v.toObject();
            auto const placeholder = jsonObj.keys();
            for (const auto &vv : placeholder) {
                hash[vv] = jsonObj[vv].toString();
            }
        }
        return hash;
    };
    auto parseExecutables = [&toHash](QJsonValue v) -> QList<ExecutableInfo> {
        QList<ExecutableInfo> info;
        if (v.isArray()) {
            auto const placeholder = v.toArray();
            for (const auto &vv : placeholder) {
                ExecutableInfo execInfo;
                auto obj = vv.toObject();
                execInfo.name = vv.toObject().value("name").toString();
                execInfo.executables = toHash(obj["executables"]);
                execInfo.runDirectory = obj["runDirectory"].toString();
                info.push_back(execInfo);
            };
        }
        return info;
    };
    auto parseTasksInfo = [](QJsonValue v) -> QList<TaskInfo> {
        QList<TaskInfo> info;
        if (v.isArray()) {
            auto const placeholder = v.toArray();
            for (auto const &vv : placeholder) {
                TaskInfo taskInfo;
                auto obj = vv.toObject();
                auto value = obj["commands"];
                taskInfo.name = obj["name"].toString();

                if (obj.contains("tooltip")) {
                    taskInfo.tooltip = obj["tooltip"].toString();
                }

                if (value.isString()) {
                    auto cmd = QStringList(value.toString());
                    taskInfo.commands.insert(PLATFORM_LINUX, cmd);
                    taskInfo.commands.insert(PLATFORM_WINDOWS, cmd);

                } else if (value.isArray()) {
                    auto cmdList = QStringList();
                    auto const pp = value.toArray();
                    for (auto const &cmd : pp) {
                        cmdList << cmd.toString();
                    }
                    taskInfo.commands.insert(PLATFORM_LINUX, cmdList);
                    taskInfo.commands.insert(PLATFORM_WINDOWS, cmdList);

                } else if (value.isObject()) {
                    auto commandsObj = value.toObject();

                    if (commandsObj.size() == 1) {
                        auto innerVal = commandsObj.begin().value();
                        if (innerVal.isString()) {
                            auto cmd = QStringList(innerVal.toString());
                            taskInfo.commands.insert(PLATFORM_LINUX, cmd);
                            taskInfo.commands.insert(PLATFORM_WINDOWS, cmd);
                        } else if (innerVal.isArray()) {
                            QStringList cmdList;
                            auto const pp = innerVal.toArray();
                            for (auto const &cmd : pp) {
                                cmdList << cmd.toString();
                            }
                            taskInfo.commands.insert(PLATFORM_LINUX, cmdList);
                            taskInfo.commands.insert(PLATFORM_WINDOWS, cmdList);
                        } else {
                            parsePlatformCommands(commandsObj, taskInfo);
                        }
                    } else {
                        parsePlatformCommands(commandsObj, taskInfo);
                    }

                } else {
                    qWarning() << "buildFromJsonFile: Invalid 'commands' format.";
                }

                taskInfo.runDirectory = obj["runDirectory"].toString();
                taskInfo.isBuild = obj["isBuild"].toBool(false);
                info.push_back(taskInfo);
            };
        }
        return info;
    };
    if (!json.isNull()) {
        value = std::make_shared<ProjectDefinition>();
        value->autoGenerated = false;
        value->sourceDir = fi.absolutePath();
        value->fileName = fi.absoluteFilePath();
        value->name = json["name"].toString();
        value->buildDir = json["build_directory"].toString();
        value->executables = parseExecutables(json["executables"]);
        value->tasksInfo = parseTasksInfo(json["tasks"]);
        value->hideFilter = json["hideFilter"].toString();

        if (value->name.isEmpty()) {
            value->name = fi.dir().dirName();
        }
    } else {
        qDebug() << "ProjectBuildConfig::buildFromFile: Loading file failed, malformed JSON?"
                 << jsonFileName;
    }
    return value;
}

QList<std::shared_ptr<ProjectDefinition>>
ProjectDefinition::findProjects(const QString &directory) {
    QList<std::shared_ptr<ProjectDefinition>> projects;

    if (auto p = tryLoadFromCodePointer(directory + QDir::separator() + "codepointer.json")) {
        projects.push_back(p);
    }
    if (auto p = tryGuessFromCMake(directory + QDir::separator() + "CMakeLists.txt")) {
        projects.push_back(p);
    }
    if (auto p = tryGuessFromMeson(directory + QDir::separator() + "meson.build")) {
        projects.push_back(p);
    }
    if (auto p = tryGuessFromCargo(directory + QDir::separator() + "Cargo.toml")) {
        projects.push_back(p);
    }
    if (auto p = tryGuessFromGo(directory + QDir::separator() + "go.mod")) {
        projects.push_back(p);
    }
    return projects;
}

void ProjectDefinition::updateBinariesCMake() {
    this->executables.clear();
    auto effectiveBuildDir = expandString(this->buildDir, getConfigDictionary());
    auto binaries = getExecutablesFromCMakeFileAPI(effectiveBuildDir);
    for (const auto &[key, value] : binaries.asKeyValueRange()) {
        auto e = ExecutableInfo();
        e.name = value;
        e.runDirectory = "${build_directory}";
        e.executables[PLATFORM_LINUX] = "${build_directory}/" + value;
        e.executables[PLATFORM_WINDOWS] = "${build_directory}\\" + value;
        this->executables.push_back(e);
    }
}

void ProjectDefinition::updateBinariesCargo() {
    auto e = ExecutableInfo();
    this->executables.clear();

#if 1
    e.name = "cargo run";
    e.runDirectory = "${source_directory}";
    e.executables[PLATFORM_LINUX] = "cargo run";
    e.executables[PLATFORM_WINDOWS] = "cargo run";
    this->executables.push_back(e);
#endif
    auto metaData = QString::fromLatin1("${build_directory}/cargo-metadata.json");
    metaData = expandString(metaData, getConfigDictionary());
    auto binaries = cargoListBinUnits(metaData);
    for (const auto &[key, value] : binaries.asKeyValueRange()) {
        e.name = key;
        e.runDirectory = "${source_directory}";
        e.executables[PLATFORM_LINUX] = value;
        e.executables[PLATFORM_WINDOWS] = value;
        this->executables.push_back(e);
    }
}

void ProjectDefinition::updateBinariesGo() {
    auto e = ExecutableInfo();
    e.name = "go run";
    e.runDirectory = "${source_directory}";
    e.executables[PLATFORM_LINUX] = "go run ${source_directory}";
    e.executables[PLATFORM_WINDOWS] = "go run ${source_directory}";
    this->executables.clear();
    this->executables.push_back(e);
}

void ProjectDefinition::updateBinariesMeson() {
    auto findMesonExecutables = [](const QString &directory,
                                   const QString &buildDir) -> QHash<QString, QString> {
        auto fullBuildPath = buildDir;
        auto process = QProcess();
        process.start("meson", QStringList() << "introspect" << fullBuildPath << "--targets");
        process.waitForFinished();

        auto output = process.readAllStandardOutput();
        if (output.isEmpty()) {
            qCritical() << "updateBinariesMeson: No output from meson introspect.";
            return {};
        }

        auto parseError = QJsonParseError();
        auto doc = QJsonDocument::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCritical() << "updateBinariesMeson: Failed to parse JSON:" << parseError.errorString()
                        << output << " builddir:" << buildDir;
            return {};
        }

        auto const targets = doc.array();
        auto result = QHash<QString, QString>();
        for (auto value : targets) {
            QJsonObject obj = value.toObject();
            if (obj["type"].toString() == "executable") {
                auto name = obj["name"].toString();
                auto filenames = obj["filename"].toArray();
                if (!filenames.isEmpty()) {
                    auto fullPath = filenames.first().toString();
                    auto relativeToDir = QDir(directory).relativeFilePath(fullPath);
                    result.insert(name, relativeToDir);
                }
            }
        }
        return result;
    };

    auto d = getConfigDictionary();
    auto effectiveSourceDir = expandString(this->sourceDir, d);
    auto effectiveBuildDir = expandString(this->buildDir, d);
    auto mesonExecutables = findMesonExecutables(effectiveSourceDir, effectiveBuildDir);
    this->executables.clear();
    for (auto it = mesonExecutables.constBegin(); it != mesonExecutables.constEnd(); ++it) {
        auto n = it.key();
        auto path = it.value();
        auto e = ExecutableInfo();
        e.name = n;
        e.runDirectory = "${source_directory}";
        e.executables[PLATFORM_LINUX] = path;
        e.executables[PLATFORM_WINDOWS] = path + ".exe";
        this->executables.push_back(e);
    }
}

void ProjectDefinition::updateBinaries() {
    // TODO
}

void ProjectDefinition::saveToFile(const QString &jsonFileName) {
    auto file = QFile(jsonFileName);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "saveToFile: Failed to open file for writing:" << file.errorString();
        return;
    }

    auto jsonObj = QJsonObject();
    jsonObj["build_directory"] = buildDir;

    auto execsArray = QJsonArray();
    for (const auto &exec : std::as_const(executables)) {
        auto execObj = QJsonObject();
        execObj["name"] = exec.name;
        execObj["runDirectory"] = exec.runDirectory;

        auto execsDetailsObj = QJsonObject();
        for (auto it = exec.executables.constBegin(); it != exec.executables.constEnd(); ++it) {
            execsDetailsObj[it.key()] = it.value();
        }
        execObj["executables"] = execsDetailsObj;

        execsArray.append(execObj);
    }
    jsonObj["executables"] = execsArray;

    auto tasksArray = QJsonArray();
    for (const auto &task : std::as_const(tasksInfo)) {
        auto taskObj = QJsonObject();
        taskObj["name"] = task.name;
        if (!task.commands.isEmpty()) {
            if (arePlatformCommandsIdentical(task.commands)) {
                // All platforms have the same command
                auto firstCommand = task.commands.begin().value();
                if (firstCommand.size() == 1) {
                    taskObj["commands"] = firstCommand.first();
                } else {
                    auto commandsArray = QJsonArray();
                    for (const auto &cmd : std::as_const(firstCommand)) {
                        commandsArray.append(cmd);
                    }
                    taskObj["commands"] = commandsArray;
                }
            } else {
                // Different commands per platform
                auto commandsObj = QJsonObject();
                savePlatformCommands(task.commands, commandsObj);
                taskObj["commands"] = commandsObj;
            }
        } else {
            // No commands at all
            taskObj["commands"] = QJsonValue();
        }
        taskObj["runDirectory"] = task.runDirectory;
        taskObj["isBuild"] = task.isBuild;

        tasksArray.append(taskObj);
    }
    jsonObj["tasks"] = tasksArray;

    auto jsonDoc = QJsonDocument(jsonObj);
    file.write(jsonDoc.toJson());
    file.close();

    this->fileName = jsonFileName;
}

const QHash<QString, QString> ProjectDefinition::getConfigDictionary() const {
    auto dictionary = QHash<QString, QString>();
    dictionary["source_directory"] = QDir::toNativeSeparators(sourceDir);
    dictionary["build_directory"] = QDir::toNativeSeparators(buildDir);
    dictionary["cpu_count"] = QString::number(qMax(1, QThread::idealThreadCount()));
    return dictionary;
}

bool ProjectDefinition::operator==(const ProjectDefinition &other) const {
    return this->name == other.name && this->buildDir == other.buildDir &&
           this->executables == other.executables && this->tasksInfo == other.tasksInfo;
}
