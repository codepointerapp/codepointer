/*
 * Copyright (C) 2023-... Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStandardPaths>
#include <QTest>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
// Known folder ids, and SHGetKnownFolderPath. Including the whole shell API
// header set is unnecessary and drags in conflicting declarations.
#include <KnownFolders.h>
#include <shlobj.h>
#endif

#include "LspClientImpl.hpp"
#include "lsp_text_edit.hpp"

/// Drives a real clangd over a rename that reaches a file with no editor.
///
/// This is what the unit tests cannot establish: that the server returns edits
/// for a header it was never told about, that those edits pass our own check when
/// applied to a file nobody has open, and that the server can then be brought
/// back in step with the rewritten file.
///
/// Skipped rather than failed when there is no clangd, so the suite stays
/// runnable on a machine without a language server.
class TestLspRenameClangd : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        m_clangd = findClangd();
        if (m_clangd.isEmpty()) {
            QSKIP("clangd not installed");
        }
        m_dir = std::filesystem::temp_directory_path() / "codepointer-lsp-rename-test";
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
    }

    void cleanupTestCase() {
        if (!m_dir.empty()) {
            std::filesystem::remove_all(m_dir);
        }
    }

    void aRenameReachesAFileThatWasNeverOpened() {
        auto const header = m_dir / "widget.h";
        auto const source = m_dir / "widget.cpp";
        writeFile(header, "class Widget {\npublic:\n    void draw();\n};\n");
        writeFile(source, "#include \"widget.h\"\n\nvoid Widget::draw() {}\n");
        // Without a compile database clangd answers a definition request with the
        // declaration it was asked about, and a rename touches only the one file
        // - there is no AST to connect the declaration to the definition. That is
        // a property of the server, not of the code under test, so the database is
        // what makes this a cross-file rename at all.
        writeCompileDatabase(m_dir, {source.string()});

        auto server = startServer();
        QVERIFY2(server != nullptr, "clangd did not become ready");
        QVERIFY(server->isReady());

        // Only the header is announced. widget.cpp is on disk and reachable
        // through the include, but nothing has opened it - the case covered here.
        QVERIFY(server->syncDocument(header.string(), readFile(header), "cpp"));

        auto edits = requestRename(*server, header.string(), 2, 9, "render");
        QVERIFY2(!edits.empty(), "clangd returned no rename edits for the declaration");

        auto files = std::set<std::string>();
        for (auto const &edit : edits) {
            files.insert(edit.file);
        }
        QVERIFY2(files.size() >= 2,
                 qPrintable(QStringLiteral("expected edits in at least 2 files, got %1")
                                .arg(static_cast<int>(files.size()))));

        // Now the closed-file path, exactly as the plugin runs it: take only the
        // edits for the file that has no editor, sort them bottom-up, and apply
        // with the symbol check.
        auto closed = editsFor(edits, source.string());
        QVERIFY2(!closed.empty(), "no edits for the file that was never opened");
        std::sort(closed.begin(), closed.end(), [](auto const &a, auto const &b) {
            return a.startLine != b.startLine ? a.startLine > b.startLine
                                              : a.startCharacter > b.startCharacter;
        });

        auto result = lspTextEdit::apply(readFile(source), toPlain(closed), "draw");
        QVERIFY2(result.applied, "a rename clangd returned was refused by our own check");
        QCOMPARE(QString::fromStdString(result.text),
                 QStringLiteral("#include \"widget.h\"\n\nvoid Widget::render() {}\n"));

        // Syncing afterwards has to leave the server answering from the new text.
        // Without it the server still holds what it read at startup, and the next
        // positional request against this file comes back for text that is gone.
        QVERIFY(server->syncDocument(source.string(), result.text, "cpp"));
        // Column 13 is where `render` starts in "void Widget::render() {}" -
        // column 12 is the second colon, which hovers the class.
        auto const hover = requestHover(*server, source.string(), 2, 13);
        QVERIFY2(!hover.empty(), "no hover after syncing the rewritten file");
        QVERIFY2(hover.find("render") != std::string::npos,
                 qPrintable(QStringLiteral("server answered about the old name: %1")
                                .arg(QString::fromStdString(hover))));
    }

    void aReferencesSearchFindsUsesInFilesThatWereNeverOpened() {
        auto const header = m_dir / "refs.h";
        writeFile(header, "int counter;\nvoid bump();\n");
        auto const a = m_dir / "a.cpp";
        auto const b = m_dir / "b.cpp";
        writeFile(a, "#include \"refs.h\"\n\nvoid bump() {\n    counter++;\n}\n\nint readOne() {\n "
                     "   return counter;\n}\n");
        writeFile(b, "#include \"refs.h\"\n\nvoid other() {\n    counter = 0;\n    bump();\n}\n");
        writeCompileDatabase(m_dir, {a.string(), b.string()});

        auto server = startServer();
        QVERIFY2(server != nullptr, "clangd did not become ready");
        QVERIFY(server->isReady());

        QVERIFY(server->syncDocument(header.string(), readFile(header), "cpp"));

        // counter is declared in a header nobody has open and used in two source
        // files, neither of which is open.
        auto const locations = requestReferences(*server, header.string(), 0, 4, true);
        QVERIFY2(locations.size() >= 4,
                 qPrintable(QStringLiteral("expected the declaration plus 3 uses, got %1")
                                .arg(static_cast<int>(locations.size()))));

        auto files = std::set<std::string>();
        for (auto const &location : locations) {
            files.insert(location.file);
        }
        // Every use must be in a file we never announced, which is the whole point: the
        // server reaches the project through the include graph.
        QVERIFY2(
            files.size() >= 3,
            qPrintable(
                QStringLiteral("expected 3 files, got %1").arg(static_cast<int>(files.size()))));
    }

    void aReferencesSearchExcludesTheDeclarationWhenAsked() {
        auto const header = m_dir / "only.h";
        writeFile(header, "int lone;\n");
        auto const source = m_dir / "only.cpp";
        writeFile(source, "#include \"only.h\"\n\nvoid go() { lone = 1; }\n");
        writeCompileDatabase(m_dir, {source.string()});

        auto server = startServer();
        QVERIFY(server != nullptr && server->isReady());
        QVERIFY(server->syncDocument(header.string(), readFile(header), "cpp"));

        auto const with = requestReferences(*server, header.string(), 0, 4, true);
        auto const without = requestReferences(*server, header.string(), 0, 4, false);
        QVERIFY(!with.empty());
        // The flag has to actually reach the server, or every search silently
        // includes the declaration.
        QVERIFY2(without.size() < with.size(),
                 qPrintable(QStringLiteral("includeDeclaration ignored: %1 vs %2")
                                .arg(static_cast<int>(without.size()))
                                .arg(static_cast<int>(with.size()))));
    }

    void aStaleReplyIsRefusedRatherThanApplied() {
        // The protection that matters for a file nobody can see: the ranges are
        // computed against the text the server read, and if the file changed
        // underneath it we must decline rather than paste the name somewhere
        // arbitrary.
        auto const text = std::string("void somethingElse();\n");
        auto const edits = std::vector<lspTextEdit::Edit>{lspTextEdit::Edit{0, 5, 0, 9, "render"}};
        auto const result = lspTextEdit::apply(text, edits, "draw");
        QVERIFY(!result.applied);
        QCOMPARE(result.text, text);
        QCOMPARE(result.rejected.size(), size_t(1));
    }

  private:
    QString m_clangd;
    std::filesystem::path m_dir;

    /// Locates a clangd to test against.
    ///
    /// `PATH` alone is not enough on Windows: an LLVM installed from the release
    /// zip or the VS installer lands in Program Files and is often absent from the
    /// process PATH, particularly under a service or CI account. A machine with no
    /// clangd is skipped, not failed.
    static auto findClangd() -> QString {
        for (auto const candidate : {qgetenv("CODEPOINTER_TEST_CLANGD"), qgetenv("LLVM_DIR")}) {
            if (!candidate.isEmpty()) {
                auto const path = clangdIn(QString::fromLocal8Bit(candidate));
                if (!path.isEmpty()) {
                    return path;
                }
            }
        }

#ifdef Q_OS_WIN
        for (auto const &root : windowsLlvmRoots()) {
            auto const path = clangdIn(root);
            if (!path.isEmpty()) {
                return path;
            }
        }
        // The Visual Studio component ships its own copy under a versioned path.
        for (auto const &base :
             {windowsFolder(FOLDERID_ProgramFiles), windowsFolder(FOLDERID_ProgramFilesX86)}) {
            for (auto const &year : {QStringLiteral("2022"), QStringLiteral("2019")}) {
                for (auto const &edition :
                     {QStringLiteral("Community"), QStringLiteral("Professional"),
                      QStringLiteral("Enterprise"), QStringLiteral("BuildTools")}) {
                    auto const found =
                        clangdIn(QDir(base).filePath(QStringLiteral("Microsoft VisualStudio/") +
                                                     year + QLatin1Char('/') + edition));
                    if (!found.isEmpty()) {
                        return found;
                    }
                }
            }
        }
#endif

        return QStandardPaths::findExecutable(QStringLiteral("clangd"));
    }

#ifdef Q_OS_WIN
    /// Asks the shell where a well-known folder actually is.
    ///
    /// Not a hardcoded "C:/Program Files": the folder is relocatable, a 32-bit
    /// process is redirected to Program Files (x86) by default, and CI accounts
    /// often have none of these where a developer machine does.
    static auto windowsFolder(GUID const &id) -> QString {
        auto path = PWSTR();
        if (FAILED(SHGetKnownFolderPath(&id, 0, nullptr, &path))) {
            return {};
        }
        auto const folder = QString::fromWCharArray(path);
        CoTaskMemFree(path);
        return folder;
    }

    /// Machine-wide and per-user LLVM prefixes.
    ///
    /// The per-user locations matter: LLVM installed for one account rather than
    /// the machine lands under the user's profile and is never in Program Files,
    /// and in that case the install needs no administrator at all.
    static auto windowsLlvmRoots() -> QStringList {
        auto roots = QStringList{};
        auto const programFiles = windowsFolder(FOLDERID_ProgramFiles);
        auto const programFilesX86 = windowsFolder(FOLDERID_ProgramFilesX86);
        for (auto const &base : {programFiles, programFilesX86}) {
            if (base.isEmpty()) {
                continue;
            }
            for (auto const &name :
                 {QStringLiteral("LLVM"), QStringLiteral("LLVM-20"), QStringLiteral("LLVM-19"),
                  QStringLiteral("LLVM-18"), QStringLiteral("LLVM-17"), QStringLiteral("LLVM-16"),
                  QStringLiteral("LLVM-15")}) {
                roots << QDir(base).filePath(name);
            }
        }

        auto const localAppData = windowsFolder(FOLDERID_LocalAppData);
        if (!localAppData.isEmpty()) {
            // winget, and the installers that default to a per-user scope.
            for (auto const &name :
                 {QStringLiteral("LLVM"), QStringLiteral("LLVM-20"), QStringLiteral("LLVM-19"),
                  QStringLiteral("LLVM-18"), QStringLiteral("LLVM-17"), QStringLiteral("LLVM-16"),
                  QStringLiteral("LLVM-15")}) {
                roots << QDir(localAppData).filePath(QStringLiteral("Programs/") + name);
            }
        }

        auto const profile = windowsFolder(FOLDERID_Profile);
        if (!profile.isEmpty()) {
            roots << QDir(profile).filePath(QStringLiteral("LLVM"));
            for (auto const &name : {QStringLiteral("LLVM-20"), QStringLiteral("LLVM-19"),
                                     QStringLiteral("LLVM-18"), QStringLiteral("LLVM-17"),
                                     QStringLiteral("LLVM-16"), QStringLiteral("LLVM-15")}) {
                roots << QDir(profile).filePath(name);
            }
        }
        return roots;
    }
#endif

    /// clangd ships as <prefix>/bin/clangd, so LLVM_DIR and a Visual Studio root
    /// are both resolved by looking for that executable rather than assuming a
    /// layout.
    static auto clangdIn(QString const &root) -> QString {
        if (root.isEmpty()) {
            return {};
        }
        if (!QFileInfo(root).isDir()) {
            auto const direct = QFileInfo(root);
            if (direct.exists() && direct.isExecutable()) {
                return direct.absoluteFilePath();
            }
            return {};
        }

        // Both spellings are tried: Qt does not apply PATHEXT, so on Windows
        // "clangd" does not find "clangd.exe".
        auto const names = QStringList{QStringLiteral("clangd"), QStringLiteral("clangd.exe")};
        // A standalone LLVM keeps it in <prefix>/bin, the Visual Studio component
        // in <edition>/VC/Tools/Llvm/bin.
        auto const layouts = QStringList{QStringLiteral("bin"), QStringLiteral("."),
                                         QStringLiteral("VC/Tools/Llvm/bin"),
                                         QStringLiteral("VC/Tools/Llvm/x64/bin")};
        for (auto const &layout : layouts) {
            for (auto const &name : names) {
                auto const info = QFileInfo(QDir(root).filePath(layout + QLatin1Char('/') + name));
                if (info.exists() && info.isExecutable()) {
                    return info.absoluteFilePath();
                }
            }
        }
        return {};
    }

    /// Writes a one-entry compile database, naming the compiler next to the clangd
    /// we found rather than a bare "clang++". clangd runs that command to read
    /// the flags, so if LLVM's bin directory is not on PATH - the usual state on
    /// Windows - a bare name silently degrades to single-file renames and the
    /// test fails for a reason that has nothing to do with the code.
    void writeCompileDatabase(std::filesystem::path const &dir,
                              std::vector<std::string> const &sources) {
        auto compiler = QDir::cleanPath(QFileInfo(m_clangd).absolutePath() + QDir::separator() +
                                        QStringLiteral("clang++"));
#ifdef Q_OS_WIN
        compiler += QStringLiteral(".exe");
#endif
        QVERIFY2(QFileInfo(compiler).exists(),
                 qPrintable(QStringLiteral("no clang++ beside %1").arg(m_clangd)));

        // Written as JSON rather than assembled by hand: on Windows these paths
        // contain backslashes, and "\U" or "\P" is not a valid JSON escape.
        // Forward slashes are what clangd wants anyway, so use those.
        auto const toPosix = [](std::string const &path) {
            return QString::fromStdString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        };
        auto entries = QJsonArray{};
        for (auto const &source : sources) {
            auto const absoluteSource = toPosix(source);
            auto entry = QJsonObject{};
            entry.insert(QStringLiteral("directory"), toPosix(dir.string()));
            entry.insert(QStringLiteral("command"),
                         compiler + QStringLiteral(" -std=c++17 -c ") + absoluteSource);
            entry.insert(QStringLiteral("file"), absoluteSource);
            entries.append(entry);
        }
        auto document = QJsonDocument(entries);
        writeFile(dir / "compile_commands.json",
                  document.toJson(QJsonDocument::Compact).toStdString());
    }

    static void writeFile(std::filesystem::path const &path, std::string const &text) {
        auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    static auto readFile(std::filesystem::path const &path) -> std::string {
        auto in = std::ifstream(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// Starts clangd over m_dir and waits for `initialize` to come back. Polled
    /// rather than blocked on, so a server that never starts fails the test
    /// instead of hanging it.
    auto startServer() -> std::unique_ptr<LspClientImpl> {
        auto server = std::make_unique<LspClientImpl>(
            m_clangd.toStdString(), std::vector<std::string>{"--background-index", "--log=error"},
            m_dir.string());
        for (auto i = 0; i < 200 && !server->isReady(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return server;
    }

    static auto requestRename(LspClientImpl &server, std::string const &file, int line, int column,
                              std::string const &newName) -> std::vector<LspClientImpl::TextEdit> {
        auto out = std::vector<LspClientImpl::TextEdit>();
        auto mutex = std::mutex();
        auto answered = std::atomic_bool(false);
        server.requestRename(
            file, static_cast<uint>(line), static_cast<uint>(column), newName,
            [&](std::vector<LspClientImpl::TextEdit> edits, std::string const &error) {
                auto locker = std::lock_guard(mutex);
                out = std::move(edits);
                answered = !error.empty();
            });
        waitFor(answered);
        return out;
    }

    static auto requestReferences(LspClientImpl &server, std::string const &file, int line,
                                  int column, bool includeDeclaration)
        -> std::vector<LspClientImpl::Location> {
        auto out = std::vector<LspClientImpl::Location>();
        auto mutex = std::mutex();
        auto answered = std::atomic_bool(false);
        server.requestReferences(file, static_cast<uint>(line), static_cast<uint>(column),
                                 includeDeclaration,
                                 [&](std::vector<LspClientImpl::Location> locations) {
                                     auto locker = std::lock_guard(mutex);
                                     out = std::move(locations);
                                     answered = true;
                                 });
        waitFor(answered);
        return out;
    }

    static auto requestHover(LspClientImpl &server, std::string const &file, int line, int column)
        -> std::string {
        auto out = std::string();
        auto mutex = std::mutex();
        auto answered = std::atomic_bool(false);
        server.requestHover(file, static_cast<uint>(line), static_cast<uint>(column),
                            [&](std::string text) {
                                auto locker = std::lock_guard(mutex);
                                out = std::move(text);
                                answered = true;
                            });
        waitFor(answered);
        return out;
    }

    /// The server answers on its reader thread, so a request is awaited by
    /// polling rather than by blocking a condition variable the GUI thread could
    /// be expected to service.
    static void waitFor(std::atomic_bool const &answered) {
        for (auto i = 0; i < 400 && !answered.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    static auto editsFor(std::vector<LspClientImpl::TextEdit> const &edits, std::string const &file)
        -> std::vector<LspClientImpl::TextEdit> {
        auto out = std::vector<LspClientImpl::TextEdit>();
        for (auto const &edit : edits) {
            if (edit.file == file) {
                out.push_back(edit);
            }
        }
        return out;
    }

    static auto toPlain(std::vector<LspClientImpl::TextEdit> const &edits)
        -> std::vector<lspTextEdit::Edit> {
        auto out = std::vector<lspTextEdit::Edit>();
        for (auto const &edit : edits) {
            out.push_back(lspTextEdit::Edit{edit.startLine, edit.startCharacter, edit.endLine,
                                            edit.endCharacter, edit.newText});
        }
        return out;
    }
};

QTEST_MAIN(TestLspRenameClangd)
#include "test_lsp_rename_clangd.moc"