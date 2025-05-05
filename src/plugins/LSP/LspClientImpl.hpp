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
    using DiagnosticsCallback =
        std::function<void(const std::string &, const std::vector<lsp::Diagnostic> &)>;
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

    /// True once the server answered `initialize` and was sent `initialized`.
    bool isReady() const { return m_ready.load(); }
    bool isRunning() const { return m_running.load(); }
    const std::string &documentRoot() const { return m_documentRoot; }
    std::string serverName() const;
    /// Snapshot of the synced documents and their versions.
    std::vector<std::pair<std::string, int>> documents() const;

    /// Sends didOpen the first time a file is seen, didChange afterwards. Requests
    /// that depend on buffer contents must call this first, otherwise the server
    /// answers against a stale (or unknown) document.
    void syncDocument(const std::string &fileName, const std::string &text);
    void closeDocument(const std::string &fileName);

    void requestCompletion(const std::string &fileName, int line, int column,
                           CompletionCallback callback);
    void requestHover(const std::string &fileName, int line, int column, HoverCallback callback);

  private:
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

    DiagnosticsCallback m_diagnosticsCallback;
    TraceCallback m_traceCallback;
};
