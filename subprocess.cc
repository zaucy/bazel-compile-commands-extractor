#include "subprocess.h"
#include <stdexcept>
#include <iostream>
#include <array>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <strsafe.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>
#include <cstring>
extern char **environ;
#endif

#include <atomic>

namespace subprocess {

static StatusCallback g_status_callback;
static std::atomic<size_t> g_next_id{1};

void SetStatusCallback(StatusCallback callback) {
    g_status_callback = callback;
}

#ifdef _WIN32

// Windows implementation using CreateProcess
RunResult Run(const std::vector<std::string>& command, 
              const std::map<std::string, std::string>& env, 
              bool check) {
    if (command.empty()) throw std::runtime_error("Empty command");

    // Build command line
    std::string cmd_line;
    for (const auto& arg : command) {
        if (!cmd_line.empty()) cmd_line += " ";
        // Basic escaping for Windows cmd
        if (arg.find(' ') != std::string::npos || arg.empty()) {
            cmd_line += "\"";
            for (char c : arg) {
                if (c == '"') cmd_line += "\\\"";
                else if (c == '\\') cmd_line += "\\\\"; // Double backslashes? Windows quoting is a mess.
                else cmd_line += c;
            }
            cmd_line += "\"";
        } else {
            cmd_line += arg;
        }
    }

    // Environment block
    std::vector<char> env_block;
    if (!env.empty()) {
        for (const auto& kv : env) {
            std::string s = kv.first + "=" + kv.second;
            env_block.insert(env_block.end(), s.begin(), s.end());
            env_block.push_back('\0');
        }
        env_block.push_back('\0');
    }

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;
    HANDLE hChildStd_ERR_Rd = NULL;
    HANDLE hChildStd_ERR_Wr = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) throw std::runtime_error("CreatePipe");
    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("SetHandleInformation");
    
    if (!CreatePipe(&hChildStd_ERR_Rd, &hChildStd_ERR_Wr, &saAttr, 0)) throw std::runtime_error("CreatePipe");
    if (!SetHandleInformation(hChildStd_ERR_Rd, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("SetHandleInformation");

    STARTUPINFOA siStartInfo;
    memset(&siStartInfo, 0, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStd_ERR_Wr;
    siStartInfo.hStdOutput = hChildStd_OUT_Wr;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // CreateProcessA might modify cmd_line
    std::vector<char> mutable_cmd_line(cmd_line.begin(), cmd_line.end());
    mutable_cmd_line.push_back(0);

    BOOL bSuccess = CreateProcessA(NULL, 
        mutable_cmd_line.data(),
        NULL, 
        NULL, 
        TRUE, 
        0, 
        env.empty() ? NULL : env_block.data(), 
        NULL, 
        &siStartInfo, 
        &piProcInfo);

    if (!bSuccess) throw std::runtime_error("CreateProcess failed");

    // Close write ends of pipe in parent
    CloseHandle(hChildStd_OUT_Wr);
    CloseHandle(hChildStd_ERR_Wr);

    // Read output
    std::string stdout_str, stderr_str;
    DWORD dwRead;
    CHAR chBuf[4096];
    
    // Simple blocking read. For true interleaved, we'd need threads or overlapped I/O.
    // But for aquery/simple commands, reading one then the other or alternating might stick.
    // Let's just read until EOF.
    
    // Actually, to prevent deadlock if the child fills one pipe and waits for it to drain while we read the other,
    // we should use PeekNamedPipe or threads. For simplicity in this "portable" version,
    // I'll use a polling loop or just assume the buffer is big enough (it isn't).
    
    // Better strategy for this simple implementation: Use a separate thread for reading stderr?
    // Or just accept that for small output it works.
    // Given "maximally portable" without libraries, complex async I/O is hard.
    // I'll do a simple loop checking both. 
    
    auto start_time = std::chrono::steady_clock::now();
    bool long_running_logged = false;
    size_t process_id = g_next_id++;

    while (true) {
        bool any_read = false;
        DWORD bytesAvail = 0;
        
        // STDOUT
        if (PeekNamedPipe(hChildStd_OUT_Rd, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) {
                stdout_str.append(chBuf, dwRead);
                any_read = true;
            }
        }
        
        // STDERR
        if (PeekNamedPipe(hChildStd_ERR_Rd, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(hChildStd_ERR_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) {
                stderr_str.append(chBuf, dwRead);
                any_read = true;
            }
        }

        if (!any_read) {
            auto now = std::chrono::steady_clock::now();
            if (!long_running_logged && std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= 5) {
                 long_running_logged = true;
                 std::string executable_name = command[0]; 
                 size_t last_slash = executable_name.find_last_of("/\\");
                 if (last_slash != std::string::npos) {
                     executable_name = executable_name.substr(last_slash + 1);
                 }
                 if (executable_name.length() > 1 && executable_name[0] == '"' && executable_name.back() == '"') {
                     executable_name = executable_name.substr(1, executable_name.length() - 2);
                 }
                 if (g_status_callback) g_status_callback(process_id, "External tool running: " + executable_name);
            }

            if (WaitForSingleObject(piProcInfo.hProcess, 10) != WAIT_TIMEOUT) {
                // Process finished. Read remaining.
                while (ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) stdout_str.append(chBuf, dwRead);
                while (ReadFile(hChildStd_ERR_Rd, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) stderr_str.append(chBuf, dwRead);
                break;
            }
        }
    }

    if (long_running_logged && g_status_callback) g_status_callback(process_id, "");

    DWORD exitCode;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hChildStd_OUT_Rd);
    CloseHandle(hChildStd_ERR_Rd);

    RunResult result{ (int)exitCode, stdout_str, stderr_str };
    if (check && result.return_code != 0) {
        throw std::runtime_error("Command failed with return code " + std::to_string(result.return_code) + "\nStderr: " + result.stderr_output);
    }
    return result;
}

#else

// POSIX implementation
RunResult Run(const std::vector<std::string>& command, 
              const std::map<std::string, std::string>& env, 
              bool check) {
    if (command.empty()) throw std::runtime_error("Empty command");

    int pipe_out[2];
    int pipe_err[2];
    if (pipe(pipe_out) == -1) throw std::runtime_error("pipe");
    if (pipe(pipe_err) == -1) throw std::runtime_error("pipe");

    pid_t pid = fork();
    if (pid == -1) throw std::runtime_error("fork");

    if (pid == 0) {
        // Child
        close(pipe_out[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);

        close(pipe_err[0]);
        dup2(pipe_err[1], STDERR_FILENO);
        close(pipe_err[1]);

        std::vector<char*> args;
        for (const auto& arg : command) args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);

        std::vector<std::string> env_strings;
        std::vector<char*> env_ptrs;
        if (!env.empty()) {
            for (const auto& kv : env) {
                env_strings.push_back(kv.first + "=" + kv.second);
            }
            for (const auto& s : env_strings) env_ptrs.push_back(const_cast<char*>(s.c_str()));
            env_ptrs.push_back(nullptr);
            execvpe(args[0], args.data(), env_ptrs.data());
        } else {
            execvp(args[0], args.data());
        }
        
        // If exec fails
        perror("exec");
        exit(1);
    } else {
        // Parent
        close(pipe_out[1]);
        close(pipe_err[1]);

        std::string stdout_str, stderr_str;
        char buffer[4096];
        ssize_t count;

        // Simple sequential read (can deadlock if buffers fill, strictly speaking need select/poll)
        // For robustness, let's use select
        while (true) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(pipe_out[0], &readfds);
            FD_SET(pipe_err[0], &readfds);
            int maxfd = std::max(pipe_out[0], pipe_err[0]);
            
            if (select(maxfd + 1, &readfds, NULL, NULL, NULL) == -1) break;

            bool active = false;
            if (FD_ISSET(pipe_out[0], &readfds)) {
                count = read(pipe_out[0], buffer, sizeof(buffer));
                if (count > 0) {
                    stdout_str.append(buffer, count);
                    active = true;
                } else if (count == 0) {
                    // Closed?
                    FD_CLR(pipe_out[0], &readfds); // Actually need to stop checking this fd
                    close(pipe_out[0]);
                    pipe_out[0] = -1;
                }
            }
            if (FD_ISSET(pipe_err[0], &readfds)) {
                count = read(pipe_err[0], buffer, sizeof(buffer));
                if (count > 0) {
                    stderr_str.append(buffer, count);
                    active = true;
                } else if (count == 0) {
                     FD_CLR(pipe_err[0], &readfds);
                     close(pipe_err[0]);
                     pipe_err[0] = -1;
                }
            }

            if (pipe_out[0] == -1 && pipe_err[0] == -1) break;
        }

        int status;
        waitpid(pid, &status, 0);
        int return_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        RunResult result{ return_code, stdout_str, stderr_str };
        if (check && result.return_code != 0) {
            throw std::runtime_error("Command failed with return code " + std::to_string(result.return_code) + "\nStderr: " + result.stderr_output);
        }
        return result;
    }
}

#endif

} // namespace subprocess
