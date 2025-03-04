#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

class LspClientImpl {
  public:
    using CompletionCallback = std::function<void(const std::vector<lsp::CompletionItem> &)>;
    using DiagnosticsCallback = std::function<void(const std::vector<lsp::Diagnostic> &)>;
    using TextChangedCallback = std::function<std::string()>;

    explicit LspClientImpl(const std::string &documentRoot);
    ~LspClientImpl();

    // Disable copy and move
    LspClientImpl(const LspClientImpl &) = delete;
    LspClientImpl &operator=(const LspClientImpl &) = delete;
    LspClientImpl(LspClientImpl &&) = delete;
    LspClientImpl &operator=(LspClientImpl &&) = delete;

    void setCompletionCallback(CompletionCallback callback);
    void setDiagnosticsCallback(DiagnosticsCallback callback);
    void setTextChangedCallback(TextChangedCallback callback);

    void requestCompletion(int line, int column);

  private:
    void startClangd();
    void stopClangd();
    void initializeLspServer();
    void shutdownLspServer();
    void handleDiagnostics(const lsp::notifications::TextDocument_PublishDiagnostics &params);

#ifdef _WIN32
    void startClangdWin32();
#else
    void startClangdPosix();
#endif

    std::string m_documentRoot;
    std::unique_ptr<lsp::Connection> m_connection;
    std::unique_ptr<lsp::MessageHandler> m_messageHandler;

    CompletionCallback m_completionCallback;
    DiagnosticsCallback m_diagnosticsCallback;
    TextChangedCallback m_textChangedCallback;

#ifdef _WIN32
    HANDLE m_clangdProcess;
    HANDLE m_clangdStdIn;
    HANDLE m_clangdStdOut;
#else
    pid_t m_clangdPid;
    int m_clangdStdIn;
    int m_clangdStdOut;
#endif
};
