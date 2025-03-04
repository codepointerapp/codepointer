#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef _WIN32
#include <ext/stdio_filebuf.h>
#include <signal.h>
#include <sys/wait.h>
#endif

// #include <lsp/io/standardio.h>

#include "LspClientImpl.h"

LspClientImpl::LspClientImpl(const std::string &documentRoot) : m_documentRoot(documentRoot) {
    startClangd();
    initializeLspServer();
}

LspClientImpl::~LspClientImpl() {
    shutdownLspServer();
    stopClangd();
}

void LspClientImpl::setCompletionCallback(CompletionCallback callback) {
    m_completionCallback = std::move(callback);
}

void LspClientImpl::setDiagnosticsCallback(DiagnosticsCallback callback) {
    m_diagnosticsCallback = std::move(callback);
}

void LspClientImpl::setTextChangedCallback(TextChangedCallback callback) {
    m_textChangedCallback = std::move(callback);
}

void LspClientImpl::startClangd() {
#ifdef _WIN32
    startClangdWin32();
#else
    startClangdPosix();
#endif
    m_messageHandler = std::make_unique<lsp::MessageHandler>(*m_connection);
}

class PipeStream : public std::iostream {
  private:
    __gnu_cxx::stdio_filebuf<char> m_filebuf;

  public:
    PipeStream(int fd) : std::iostream(nullptr), m_filebuf(fd, std::ios::in | std::ios::out) {
        rdbuf(&m_filebuf);
    }
};

#ifdef _WIN32
// Helper function to get error messages
static std::string GetLastWin32ErrorAsString() {
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0) {
        return std::string();
    }

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);

    return message;
}

void LspClientImpl::startClangdWin32(const std::wstring &clangdPath) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStdInRead, hChildStdOutWrite;
    if (!CreatePipe(&hChildStdInRead, &m_clangdStdIn, &saAttr, 0) ||
        !CreatePipe(&m_clangdStdOut, &hChildStdOutWrite, &saAttr, 0)) {
        throw std::runtime_error("Failed to create pipes: " + GetLastWin32ErrorAsString());
    }

    SetHandleInformation(m_clangdStdIn, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_clangdStdOut, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW siStartInfo;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOW));
    siStartInfo.cb = sizeof(STARTUPINFOW);
    siStartInfo.hStdError = hChildStdOutWrite;
    siStartInfo.hStdOutput = hChildStdOutWrite;
    siStartInfo.hStdInput = hChildStdInRead;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::wstring cmdLine = L"\"" + clangdPath + L"\"";

    if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo,
                        &piProcInfo)) {
        throw std::runtime_error("Failed to start clangd process: " + GetLastWin32ErrorAsString());
    }

    m_clangdProcess = piProcInfo.hProcess;
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hChildStdInRead);
    CloseHandle(hChildStdOutWrite);

    // Wait for the process to start (with a timeout)
    DWORD waitResult = WaitForSingleObject(m_clangdProcess, 5000); // 5-second timeout
    if (waitResult == WAIT_TIMEOUT) {
        throw std::runtime_error("Timeout waiting for clangd process to start");
    } else if (waitResult != WAIT_OBJECT_0) {
        throw std::runtime_error("Error waiting for clangd process: " +
                                 GetLastWin32ErrorAsString());
    }

    m_connection =
        std::make_unique<lsp::Connection>(std::make_unique<lsp::io::StandardIo>(m_clangdStdIn),
                                          std::make_unique<lsp::io::StandardIo>(m_clangdStdOut));
}

#else
void LspClientImpl::startClangdPosix() {
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) == -1 || pipe(stdoutPipe) == -1) {
        throw std::runtime_error("Failed to create pipes");
    }

    m_clangdPid = fork();
    if (m_clangdPid == -1) {
        throw std::runtime_error("Failed to fork clangd process");
    } else if (m_clangdPid == 0) {
        // Child process
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);

        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);

        execlp("clangd", "clangd", (char *)NULL);
        exit(1);
    }

    // Parent process
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    m_clangdStdIn = stdinPipe[1];
    m_clangdStdOut = stdoutPipe[0];

    auto clangdStdInStream = PipeStream(m_clangdStdIn);
    auto clangdStdOutStream = PipeStream(m_clangdStdOut);
    m_connection = std::make_unique<lsp::Connection>(clangdStdInStream, clangdStdOutStream);
}
#endif

void LspClientImpl::stopClangd() {
#ifdef _WIN32
    TerminateProcess(m_clangdProcess, 0);
    CloseHandle(m_clangdProcess);
    CloseHandle(m_clangdStdIn);
    CloseHandle(m_clangdStdOut);
#else
    kill(m_clangdPid, SIGTERM);
    int status;
    waitpid(m_clangdPid, &status, 0);
    close(m_clangdStdIn);
    close(m_clangdStdOut);
#endif
}

void LspClientImpl::initializeLspServer() {
    auto &dispatcher = m_messageHandler->messageDispatcher();

    auto initializeParams = lsp::requests::Initialize::Params{};
    initializeParams.rootUri = "file://" + m_documentRoot;
    initializeParams.capabilities = {};
    dispatcher.sendRequest<lsp::requests::Initialize>(
        std::move(initializeParams),
        [](const lsp::requests::Initialize::Result &&result) {
            // TODO
        },
        [](const lsp::Error &error) {
            // TODO
        });

    dispatcher.sendRequest<lsp::requests::TextDocument_Diagnostic>(
        lsp::requests::TextDocument_Diagnostic::Params{/* parameters */},
        [](lsp::requests::TextDocument_Diagnostic::Result &&result) {
            //...
        },
        [](const lsp::Error &error) {
            //...
        });
}

void LspClientImpl::shutdownLspServer() {
    /*
      m_messageHandler->sendRequest<lsp::requests::Shutdown>(
          {}, [this](const lsp::Result<lsp::requests::Shutdown::Response> &result) {
              m_messageHandler->sendNotification<lsp::notifications::Exit>({});
          });
          */
}

void LspClientImpl::requestCompletion(int line, int column) {
    /*
        lsp::requests::Completion::Params params;
        params.textDocument.uri = "file://" + m_documentRoot + "/current_file.cpp"; // Adjust as
       needed params.position = {line, column};

        m_messageHandler->sendRequest<lsp::requests::Completion>(
            params, [this](const lsp::Result<lsp::requests::Completion::Response> &result) {
                if (result.has_value() && m_completionCallback) {
                    m_completionCallback(result.value().items);
                }
            });
            */
}

/*
void LspClientImpl::notifyTextChanged() {
    if (m_textChangedCallback) {
        std::string text = m_textChangedCallback();
        std::string uri = "file://" + m_documentRoot + "/current_file.cpp"; // Adjust as needed

        lsp::notifications::DidChangeTextDocument::Params params;
        params.textDocument.uri = uri;
        params.contentChanges = {{text}};

        m_messageHandler->sendNotification<lsp::notifications::DidChangeTextDocument>(params);
    }
}
*/

void LspClientImpl::handleDiagnostics(
    const lsp::notifications::TextDocument_PublishDiagnostics &params) {
    if (m_diagnosticsCallback) {
        // m_diagnosticsCallback(params.diagnostics);
    }
}
