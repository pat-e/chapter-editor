#include "process.hpp"

#include <cerrno>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace {

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), wide.data(), needed);
    return wide;
}

// Follows Windows CommandLineToArgvW quoting rules.
std::wstring quoteWindowsArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    for (auto it = arg.begin(); ; ++it) {
        size_t numBackslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++numBackslashes;
        }

        if (it == arg.end()) {
            quoted.append(numBackslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            quoted.append(numBackslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(numBackslashes, L'\\');
            quoted.push_back(*it);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}
#endif

} // namespace

std::filesystem::path getExecutableDir() {
    std::error_code ec;
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return {};
        }
        if (len < buf.size()) {
            return std::filesystem::path(std::wstring(buf.data(), len)).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return {};
    }
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(buf.data()), ec);
    return ec ? std::filesystem::path(buf.data()).parent_path() : canonical.parent_path();
#else
    std::vector<char> buf(PATH_MAX);
    while (true) {
        ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size());
        if (len < 0) {
            return {};
        }
        if (static_cast<size_t>(len) < buf.size()) {
            return std::filesystem::path(std::string(buf.data(), static_cast<size_t>(len))).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
#endif
}

ProcessResult runProcess(const std::vector<std::string>& args, bool suppressOutput) {
    ProcessResult result;
    if (args.empty() || args[0].empty()) {
        return result;
    }

#ifdef _WIN32
    std::wstring cmdLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdLine.push_back(L' ');
        cmdLine += quoteWindowsArg(utf8ToWide(args[i]));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    HANDLE nulHandle = INVALID_HANDLE_VALUE;

    if (suppressOutput) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        nulHandle = CreateFileW(L"NUL",
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &sa,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
        if (nulHandle != INVALID_HANDLE_VALUE) {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = nulHandle;
            si.hStdError = nulHandle;
        }
    }

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr,
                             mutableCmd.data(),
                             nullptr,
                             nullptr,
                             suppressOutput && (nulHandle != INVALID_HANDLE_VALUE) ? TRUE : FALSE,
                             0,
                             nullptr,
                             nullptr,
                             &si,
                             &pi);

    if (nulHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(nulHandle);
    }

    if (!ok) {
        return result;
    }

    result.launched = true;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
#else
    int errPipe[2] = {-1, -1};
    if (pipe(errPipe) != 0) {
        return result;
    }
    fcntl(errPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(errPipe[0]);
        close(errPipe[1]);
        return result;
    }

    if (pid == 0) {
        close(errPipe[0]);
        if (suppressOutput) {
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDOUT_FILENO);
                dup2(devNull, STDERR_FILENO);
                if (devNull > STDERR_FILENO) {
                    close(devNull);
                }
            }
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        int err = errno;
        ssize_t ignored = write(errPipe[1], &err, sizeof(err));
        (void)ignored;
        _exit(127);
    }

    close(errPipe[1]);
    int childErr = 0;
    ssize_t bytesRead = 0;
    do {
        bytesRead = read(errPipe[0], &childErr, sizeof(childErr));
    } while (bytesRead < 0 && errno == EINTR);
    close(errPipe[0]);

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (bytesRead > 0) {
        // execvp failed in child.
        result.launched = false;
        result.exitCode = -1;
        return result;
    }

    result.launched = true;
    if (waited == pid && WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else {
        result.exitCode = -1;
    }
    return result;
#endif
}

std::optional<std::string> findToolExecutable(const std::string& toolName) {
    std::error_code ec;
    const std::filesystem::path exeDir = getExecutableDir();

#ifdef _WIN32
    const std::string localName = toolName + ".exe";
#else
    const std::string localName = toolName;
#endif

    if (!exeDir.empty()) {
        const std::filesystem::path candidate = exeDir / localName;
        if (std::filesystem::is_regular_file(candidate, ec)) {
#ifndef _WIN32
            if (access(candidate.c_str(), X_OK) == 0)
#endif
            {
                ProcessResult res = runProcess({candidate.string(), "--version"}, true);
                if (res.launched && res.exitCode == 0) {
                    return candidate.string();
                }
            }
        }
    }

    ProcessResult globalRes = runProcess({toolName, "--version"}, true);
    if (globalRes.launched && globalRes.exitCode == 0) {
        return toolName;
    }

    return std::nullopt;
}
