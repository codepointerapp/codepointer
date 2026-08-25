#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <lsp/process.h>

/// Thin wrapper around one language server process. All callbacks are invoked on
/// the reader thread, never on the GUI thread - marshal before touching widgets.
class LspClientImpl {
  public:
    using CompletionCallback = std::function<void(std::vector<lsp::CompletionItem>)>;
    using HoverCallback = std::function<void(std::string)>;

    // FIXME: line and column are int, but I see uint is used in some places.
    /// One resolved definition site: absolute path, 0-based line and column.
    struct Location {
        std::string file;
        int line = 0;
        int column = 0;
    };
    using DefinitionCallback = std::function<void(std::vector<Location>)>;

    // FIXME: line and column are int, but I see uint is used in some places.
    /// One replacement, already resolved to an absolute path and 0-based
    /// positions. Flattened from WorkspaceEdit so callers never see lsp types.
    struct TextEdit {
        std::string file;
        int startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
        std::string newText;
    };
    /// A refactoring the server offers. `edits` is empty when the server wants a
    /// workspace/executeCommand round trip instead of handing us the edit.
    struct CodeAction {
        std::string title;
        std::string kind;
        std::vector<TextEdit> edits;
        bool needsCommand = false;
    };

    using CodeActionCallback = std::function<void(std::vector<CodeAction>)>;
    using RenameCallback = std::function<void(std::vector<TextEdit>)>;
    using DiagnosticsCallback =
        std::function<void(const std::string &, const std::vector<lsp::Diagnostic> &)>;

    /// Reports server-side background work (clangd's indexing): a human readable
    /// message, a percentage (-1 when the server does not supply one), and whether
    /// the task is still running.
    using ProgressCallback = std::function<void(
        const std::string &title, const std::string &message, int percentage, bool active)>;

    /// Called for every message sent or received. Fires on both the caller's
    /// thread and the reader thread, so implementations must be thread safe.
    using TraceCallback = std::function<void(const std::string &)>;

    LspClientImpl(const std::string &executable, const std::vector<std::string> &arguments,
                  const std::string &documentRoot);
    ~LspClientImpl();

    LspClientImpl(const LspClientImpl &) = delete;
    LspClientImpl &operator=(const LspClientImpl &) = delete;
    LspClientImpl(LspClientImpl &&) = delete;
    LspClientImpl &operator=(LspClientImpl &&) = delete;

    void setDiagnosticsCallback(DiagnosticsCallback callback);
    void setTraceCallback(TraceCallback callback);
    void setProgressCallback(ProgressCallback callback);

    /// True once the server answered `initialize` and was sent `initialized`.
    bool isReady() const { return m_ready.load(); }

    /// Whether the server process is alive. Deliberately asks the process rather
    /// than the reader-loop flag: a server can die without the reader noticing
    /// straight away, and callers care about the process.

    bool isRunning() const { return m_process && m_process->isRunning(); }
    const std::string &documentRoot() const { return m_documentRoot; }
    std::string serverName() const;

    /// What the server advertised in its initialize reply, flattened to
    /// (name, value) pairs for display. Derived by serialising ServerCapabilities
    /// rather than reading known fields, so capabilities we have never heard of
    /// still show up.
    std::vector<std::pair<std::string, std::string>> capabilities() const;

    /// True when the server advertised this capability as anything other than
    /// false/null. Use before sending a request the server may not implement.
    bool hasCapability(const std::string &name) const;

    /// Snapshot of the synced documents and their versions.
    std::vector<std::pair<std::string, int>> documents() const;

    /// Sends didOpen the first time a file is seen, didChange afterwards. Requests
    /// that depend on buffer contents must call this first, otherwise the server
    /// answers against a stale (or unknown) document.
    void syncDocument(const std::string &fileName, const std::string &text,
                      const std::string &languageId);

    void closeDocument(const std::string &fileName);

    void requestCompletion(const std::string &fileName, uint line, uint column,
                           CompletionCallback callback);
    void requestHover(const std::string &fileName, uint line, uint column, HoverCallback callback);
    void requestDefinition(const std::string &fileName, uint line, uint column,
                           DefinitionCallback callback);

    /// Refactorings and fixes offered for a range. `kinds` filters by CodeActionKind
    /// prefix ("refactor", "quickfix"); empty asks for everything.
    void requestCodeActions(const std::string &fileName, uint startLine, uint startCharacter,
                            uint endLine, uint endCharacter, const std::vector<std::string> &kinds,
                            CodeActionCallback callback);

    void requestRename(const std::string &fileName, uint line, uint column,
                       const std::string &newName, RenameCallback callback);

  private:
    static std::vector<TextEdit> flatten(const lsp::WorkspaceEdit &edit);

    void startServer(const std::string &executable, const std::vector<std::string> &arguments);
    void stopServer();
    void initializeLspServer();
    void shutdownLspServer();

    std::string m_documentRoot;
    // Declaration order matters for teardown: the reader thread is joined first,
    // then the handler/connection go away, and only then is the process reaped.
    std::unique_ptr<lsp::Process> m_process;
    std::unique_ptr<lsp::Connection> m_connection;
    std::unique_ptr<lsp::MessageHandler> m_messageHandler;

    std::thread m_readerThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ready{false};

    mutable std::mutex m_documentsMutex;
    std::unordered_map<std::string, int> m_documentVersions;

    void trace(const std::string &message) const;

    mutable std::mutex m_nameMutex;
    std::string m_serverName;
    std::vector<std::pair<std::string, std::string>> m_capabilities;

    DiagnosticsCallback m_diagnosticsCallback;
    TraceCallback m_traceCallback;
    ProgressCallback m_progressCallback;
};
