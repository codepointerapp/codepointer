/**
 * \file AppPaths.hpp
 * \brief Common helpers for resolving application runtime data paths
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace AppPaths {

/// Returns the canonical path of the executable, dereferencing any symlinks
/// (such as an AppImage AppRun symlink pointing to usr/bin/codepointer).
inline auto canonicalExecutablePath() -> QString {
    auto const exePath = QCoreApplication::applicationFilePath();
    auto const canonical = QFileInfo(exePath).canonicalFilePath();
    return canonical.isEmpty() ? exePath : canonical;
}

/// Returns the directory containing the canonical executable.
inline auto canonicalApplicationDirPath() -> QString {
    return QFileInfo(canonicalExecutablePath()).path();
}

/// Returns candidate directories where shared application data files
/// (e.g. share/codepointer) are installed.
inline auto dataDirectories() -> QStringList {
    auto const appName = QCoreApplication::applicationName();
    auto candidates = QStringList{};

    // 1. If running inside an AppImage ($APPDIR set by the AppImage runtime)
    auto const appDirEnv = qEnvironmentVariable("APPDIR");
    if (!appDirEnv.isEmpty()) {
        candidates << appDirEnv + "/usr/share/" + appName;
        candidates << appDirEnv + "/share/" + appName;
    }

    // 2. Resolved canonical binary directory (follows symlinks like AppRun -> usr/bin/codepointer)
    auto const canonicalBinDir = canonicalApplicationDirPath();
#ifdef Q_OS_WIN
    candidates << canonicalBinDir + "/share/" + appName;
#else
    candidates << canonicalBinDir + "/../share/" + appName;
    candidates << canonicalBinDir + "/share/" + appName;
    candidates << canonicalBinDir + "/usr/share/" + appName;
#endif

    // 3. Application dir path without resolving symlinks (e.g. portable/in-tree runs)
    auto const appDirPath = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    candidates << appDirPath + "/share/" + appName;
#else
    candidates << appDirPath + "/../share/" + appName;
    candidates << appDirPath + "/share/" + appName;
    candidates << appDirPath + "/usr/share/" + appName;
#endif

    // 4. Standard system data locations (e.g. /usr/local/share, /usr/share)
    for (auto const &loc : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        candidates << loc;
    }

    return candidates;
}

/// Returns the directory where system data for the application is located.
/// If fileToCheck is provided (e.g. "lsp-servers.json"), the first candidate
/// containing that file is preferred.
inline auto systemDataDir(const QString &fileToCheck = QString()) -> QString {
    auto const appName = QCoreApplication::applicationName();
    auto const candidates = dataDirectories();

    if (!fileToCheck.isEmpty()) {
        for (auto const &cand : candidates) {
            auto const clean = QDir::cleanPath(cand);
            if (QFileInfo::exists(clean + QDir::separator() + fileToCheck)) {
                return clean;
            }
        }
    }

    for (auto const &cand : candidates) {
        auto const clean = QDir::cleanPath(cand);
        if (QDir(clean).exists()) {
            return clean;
        }
    }

#ifdef Q_OS_WIN
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/share/" + appName);
#else
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../share/" + appName);
#endif
}

/// Finds the full path to a shared data file across candidate data directories.
/// If found, returns the path to the existing file; otherwise returns the expected
/// path in the systemDataDir.
inline auto findDataFile(const QString &fileName) -> QString {
    auto const dir = systemDataDir(fileName);
    return QDir::toNativeSeparators(dir) + QDir::separator() + fileName;
}

/// Returns the directory containing icons (e.g. share/icons).
inline auto iconsDir() -> QString {
    auto candidates = QStringList{};
    auto const appDirEnv = qEnvironmentVariable("APPDIR");
    if (!appDirEnv.isEmpty()) {
        candidates << appDirEnv + "/usr/share/icons" << appDirEnv + "/share/icons";
    }

    auto const canonicalBinDir = canonicalApplicationDirPath();
#ifdef Q_OS_WIN
    candidates << canonicalBinDir + "/share/icons";
#else
    candidates << canonicalBinDir + "/../share/icons" << canonicalBinDir + "/share/icons"
               << canonicalBinDir + "/usr/share/icons";
#endif

    auto const appDirPath = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    candidates << appDirPath + "/share/icons";
#else
    candidates << appDirPath + "/../share/icons" << appDirPath + "/share/icons"
               << appDirPath + "/usr/share/icons";
#endif

    for (auto const &cand : candidates) {
        auto const clean = QDir::cleanPath(cand);
        if (QDir(clean + "/breeze").exists() || QDir(clean).exists()) {
            return clean;
        }
    }

#ifdef Q_OS_WIN
    return QDir::cleanPath(appDirPath + "/share/icons");
#else
    return QDir::cleanPath(appDirPath + "/../share/icons");
#endif
}

} // namespace AppPaths
