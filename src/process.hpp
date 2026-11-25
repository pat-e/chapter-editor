#ifndef CHAPTER_EDITOR_PROCESS_HPP
#define CHAPTER_EDITOR_PROCESS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ProcessResult {
    bool launched = false;
    int exitCode = -1;
};

// Returns the directory containing the running executable, or empty path on failure.
std::filesystem::path getExecutableDir();

// Executes a command directly without invoking a shell (safe against spaces and metacharacters).
ProcessResult runProcess(const std::vector<std::string>& args, bool suppressOutput = false);

// Searches for a tool executable alongside the current binary first, then on PATH.
std::optional<std::string> findToolExecutable(const std::string& toolName);

#endif // CHAPTER_EDITOR_PROCESS_HPP
