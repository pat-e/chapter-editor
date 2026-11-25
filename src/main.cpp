#include "chapters.hpp"
#include "process.hpp"

#include <mpv/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr const char* kAppVersion = "2.0";
constexpr double kRemoveToleranceSec = 0.5;

std::atomic<bool> g_interrupted{false};

extern "C" void handleSignal(int /*sig*/) {
    g_interrupted.store(true);
}

bool isStdinInteractive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

int currentPid() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

struct CliOptions {
    std::string inputFile;
    std::string outputMkv;
    std::string chaptersXml;
    std::string loadXml;
    std::string defaultLang = "eng";
    bool explicitChaptersXml = false;
    bool overwrite = false;
    bool showHelp = false;
    bool showVersion = false;
};

void printUsage(const char* argv0) {
    std::cout << "MKV Chapter Editor v" << kAppVersion << "\n\n"
              << "Usage:\n"
              << "  " << argv0 << " [options] <video_file>\n\n"
              << "Options:\n"
              << "  -o, --output <file>     Output MKV path (default: <video>_chaptered.mkv)\n"
              << "  -c, --chapters <file>   Chapters XML path (default: <video>_chapters.xml)\n"
              << "      --load-xml <file>   Auto-load & merge chapters from XML on startup\n"
              << "      --lang <code>       Default ISO 639-2 language code (default: eng)\n"
              << "      --overwrite         Overwrite existing XML / output MKV without asking\n"
              << "  -h, --help              Show this help message and exit\n"
              << "  -v, --version           Show version and exit\n\n"
              << "Controls (in mpv window):\n"
              << "  SPACE                   Play / Pause\n"
              << "  LEFT / RIGHT            Step 1 frame (+Alt: 100, +Shift: 1000)\n"
              << "  UP / DOWN               Jump to next / previous keyframe (+Alt/Shift: ~100/1000)\n"
              << "  PGUP / PGDOWN           Jump to next / previous chapter\n"
              << "  C / INS                 Create chapter at current frame\n"
              << "  D / DEL                 Remove nearest chapter (within 0.5s)\n"
              << "  U                       Move nearest/active chapter to current frame\n"
              << "  R                       Rename nearest/active chapter title\n"
              << "  Z / Ctrl+Z              Undo last chapter edit\n"
              << "  Y / Ctrl+Y / Ctrl+Sh+Z  Redo chapter edit\n"
              << "  T / TAB                 Toggle chapter list overlay\n"
              << "  H / ?                   Toggle on-screen help\n"
              << "  F                       Toggle fullscreen\n"
              << "  M / 9 / 0               Mute / Volume down / Volume up\n"
              << "  [ / ] / Backspace       Playback speed down / up / reset (1.0x)\n"
              << "  S                       Save chapters XML\n"
              << "  L                       Load & merge chapters XML\n"
              << "  X                       Save XML & remux to output MKV (mkvmerge)\n"
              << "  P                       Patch chapters in-place in source MKV (mkvpropedit)\n"
              << "  Q                       Quit (prompts if unsaved edits exist)\n";
}

bool parseArgs(int argc, char* argv[], CliOptions& opts, std::string& errorMsg) {
    bool stopFlags = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!stopFlags && arg == "--") {
            stopFlags = true;
            continue;
        }
        if (!stopFlags && (arg == "-h" || arg == "--help")) {
            opts.showHelp = true;
            return true;
        }
        if (!stopFlags && (arg == "-v" || arg == "--version")) {
            opts.showVersion = true;
            return true;
        }
        if (!stopFlags && (arg == "-o" || arg == "--output")) {
            if (i + 1 >= argc) {
                errorMsg = "Missing argument after " + arg;
                return false;
            }
            opts.outputMkv = argv[++i];
            continue;
        }
        if (!stopFlags && (arg == "-c" || arg == "--chapters")) {
            if (i + 1 >= argc) {
                errorMsg = "Missing argument after " + arg;
                return false;
            }
            opts.chaptersXml = argv[++i];
            opts.explicitChaptersXml = true;
            continue;
        }
        if (!stopFlags && arg == "--load-xml") {
            if (i + 1 >= argc) {
                errorMsg = "Missing argument after --load-xml";
                return false;
            }
            opts.loadXml = argv[++i];
            continue;
        }
        if (!stopFlags && arg == "--lang") {
            if (i + 1 >= argc) {
                errorMsg = "Missing argument after --lang";
                return false;
            }
            opts.defaultLang = argv[++i];
            if (opts.defaultLang.empty()) {
                errorMsg = "Language code cannot be empty.";
                return false;
            }
            continue;
        }
        if (!stopFlags && arg == "--overwrite") {
            opts.overwrite = true;
            continue;
        }
        if (!stopFlags && !arg.empty() && arg[0] == '-') {
            errorMsg = "Unknown option: " + arg;
            return false;
        }
        if (!opts.inputFile.empty()) {
            errorMsg = "Unexpected extra positional argument: " + arg;
            return false;
        }
        opts.inputFile = arg;
    }

    if (opts.inputFile.empty()) {
        errorMsg = "Please specify a video file.";
        return false;
    }

    std::filesystem::path inPath(opts.inputFile);
    std::filesystem::path parent = inPath.parent_path();
    std::string stem = inPath.stem().string();
    if (stem.empty()) stem = "video";

    if (opts.outputMkv.empty()) {
        std::filesystem::path defOut = stem + "_chaptered.mkv";
        opts.outputMkv = (parent.empty() ? defOut : (parent / defOut)).string();
    }
    if (opts.chaptersXml.empty()) {
        std::filesystem::path defXml = stem + "_chapters.xml";
        opts.chaptersXml = (parent.empty() ? defXml : (parent / defXml)).string();
    }
    return true;
}

class TempFileGuard {
public:
    TempFileGuard() = default;
    explicit TempFileGuard(std::filesystem::path p) : path_(std::move(p)) {}
    ~TempFileGuard() { cleanup(); }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

    void reset(std::filesystem::path p) {
        cleanup();
        path_ = std::move(p);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    void cleanup() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
            path_.clear();
        }
    }

    std::filesystem::path path_;
};

struct KeyBinding {
    const char* key;
    const char* cmd;
};

constexpr std::array<KeyBinding, 53> kNormalBindings = {{
    {"RIGHT", "frame-step"},
    {"LEFT", "frame-back-step"},
    {"UP", "script-message jump-keyframe 1"},
    {"DOWN", "script-message jump-keyframe -1"},
    {"Alt+RIGHT", "script-message jump-frames 100 exact"},
    {"Shift+RIGHT", "script-message jump-frames 1000 exact"},
    {"Alt+LEFT", "script-message jump-frames -100 exact"},
    {"Shift+LEFT", "script-message jump-frames -1000 exact"},
    {"Alt+UP", "script-message jump-frames 100 keyframes"},
    {"Shift+UP", "script-message jump-frames 1000 keyframes"},
    {"Alt+DOWN", "script-message jump-frames -100 keyframes"},
    {"Shift+DOWN", "script-message jump-frames -1000 keyframes"},
    {"SPACE", "cycle pause"},
    {"f", "cycle fullscreen"},
    {"F", "cycle fullscreen"},
    {"m", "cycle mute"},
    {"M", "cycle mute"},
    {"9", "add volume -5"},
    {"0", "add volume 5"},
    {"[", "multiply speed 0.9090909"},
    {"]", "multiply speed 1.1"},
    {"BS", "set speed 1.0"},
    {"c", "script-message add-chapter"},
    {"C", "script-message add-chapter"},
    {"INS", "script-message add-chapter"},
    {"d", "script-message remove-chapter"},
    {"D", "script-message remove-chapter"},
    {"DEL", "script-message remove-chapter"},
    {"u", "script-message move-chapter"},
    {"U", "script-message move-chapter"},
    {"r", "script-message rename-chapter"},
    {"R", "script-message rename-chapter"},
    {"z", "script-message undo"},
    {"Z", "script-message undo"},
    {"Ctrl+z", "script-message undo"},
    {"Ctrl+Z", "script-message undo"},
    {"y", "script-message redo"},
    {"Y", "script-message redo"},
    {"Ctrl+y", "script-message redo"},
    {"Ctrl+Y", "script-message redo"},
    {"Ctrl+Shift+z", "script-message redo"},
    {"PGUP", "script-message next-chapter"},
    {"PGDWN", "script-message prev-chapter"},
    {"s", "script-message save-chapters"},
    {"S", "script-message save-chapters"},
    {"l", "script-message load-chapters"},
    {"L", "script-message load-chapters"},
    {"x", "script-message export-and-remux"},
    {"X", "script-message export-and-remux"},
    {"p", "script-message patch-inplace"},
    {"P", "script-message patch-inplace"},
    {"t", "script-message toggle-list"},
    {"T", "script-message toggle-list"},
}};

constexpr std::array<KeyBinding, 7> kExtraNormalBindings = {{
    {"TAB", "script-message toggle-list"},
    {"h", "script-message toggle-help"},
    {"H", "script-message toggle-help"},
    {"?", "script-message toggle-help"},
    {"q", "script-message quit-app"},
    {"Q", "script-message quit-app"},
    {"ESC", "ignore"},
}};

struct RenameKeyEntry {
    const char* mpvKey;
    uint32_t codepoint;
};

std::vector<RenameKeyEntry> buildRenameKeyEntries() {
    std::vector<RenameKeyEntry> entries;
    entries.reserve(128);

    // Lowercase & uppercase ASCII letters.
    static constexpr const char* kLower[26] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
    };
    static constexpr const char* kUpper[26] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
    };
    for (uint32_t i = 0; i < 26; ++i) {
        entries.push_back({kLower[i], 'a' + i});
        entries.push_back({kUpper[i], 'A' + i});
    }

    // Digits and keypad digits.
    static constexpr const char* kDigits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static constexpr const char* kKpDigits[10] = {
        "KP0", "KP1", "KP2", "KP3", "KP4", "KP5", "KP6", "KP7", "KP8", "KP9"
    };
    for (uint32_t i = 0; i < 10; ++i) {
        entries.push_back({kDigits[i], '0' + i});
        entries.push_back({kKpDigits[i], '0' + i});
    }

    static constexpr RenameKeyEntry kSymbols[] = {
        {"SPACE", ' '},
        {"SHARP", '#'},
        {"!", '!'},
        {"\"", '"'},
        {"$", '$'},
        {"%", '%'},
        {"&", '&'},
        {"'", '\''},
        {"(", '('},
        {")", ')'},
        {"*", '*'},
        {"+", '+'},
        {",", ','},
        {"-", '-'},
        {".", '.'},
        {"/", '/'},
        {":", ':'},
        {";", ';'},
        {"<", '<'},
        {"=", '='},
        {">", '>'},
        {"?", '?'},
        {"@", '@'},
        {"[", '['},
        {"\\", '\\'},
        {"]", ']'},
        {"^", '^'},
        {"_", '_'},
        {"`", '`'},
        {"{", '{'},
        {"|", '|'},
        {"}", '}'},
        {"~", '~'},
        {"KP_ADD", '+'},
        {"KP_SUBTRACT", '-'},
        {"KP_MULTIPLY", '*'},
        {"KP_DIVIDE", '/'},
        {"KP_DEC", '.'},
        // Common accented Latin characters.
        {"\xC3\xA4", 0x00E4}, // ä
        {"\xC3\xB6", 0x00F6}, // ö
        {"\xC3\xBC", 0x00FC}, // ü
        {"\xC3\x84", 0x00C4}, // Ä
        {"\xC3\x96", 0x00D6}, // Ö
        {"\xC3\x9C", 0x00DC}, // Ü
        {"\xC3\x9F", 0x00DF}, // ß
        {"\xC3\xA9", 0x00E9}, // é
        {"\xC3\xA8", 0x00E8}, // è
        {"\xC3\xAA", 0x00EA}, // ê
        {"\xC3\xA1", 0x00E1}, // á
        {"\xC3\xA0", 0x00E0}, // à
        {"\xC3\xB1", 0x00F1}, // ñ
        {"\xC3\xA7", 0x00E7}, // ç
    };
    for (const auto& entry : kSymbols) {
        entries.push_back(entry);
    }
    return entries;
}

void setKeybind(mpv_handle* ctx, const char* key, const char* cmd) {
    const char* args[] = {"keybind", key, cmd, nullptr};
    mpv_command(ctx, args);
}

void applyNormalKeybinds(mpv_handle* ctx, const std::vector<RenameKeyEntry>& renameKeys) {
    // Clear rename-only bindings so typing keys don't linger in normal mode.
    for (const auto& rk : renameKeys) {
        setKeybind(ctx, rk.mpvKey, "ignore");
    }
    setKeybind(ctx, "Ctrl+u", "ignore");
    setKeybind(ctx, "ENTER", "ignore");
    setKeybind(ctx, "KP_ENTER", "ignore");

    for (const auto& kb : kNormalBindings) {
        setKeybind(ctx, kb.key, kb.cmd);
    }
    for (const auto& kb : kExtraNormalBindings) {
        setKeybind(ctx, kb.key, kb.cmd);
    }
}

void applyRenameKeybinds(mpv_handle* ctx, const std::vector<RenameKeyEntry>& renameKeys) {
    // Silence navigation/action keys during text input.
    for (const auto& kb : kNormalBindings) {
        setKeybind(ctx, kb.key, "ignore");
    }
    for (const auto& kb : kExtraNormalBindings) {
        setKeybind(ctx, kb.key, "ignore");
    }

    for (const auto& rk : renameKeys) {
        std::string cmd = "script-message rename-code " + std::to_string(rk.codepoint);
        setKeybind(ctx, rk.mpvKey, cmd.c_str());
    }
    setKeybind(ctx, "BS", "script-message rename-backspace");
    setKeybind(ctx, "Ctrl+u", "script-message rename-clear");
    setKeybind(ctx, "ENTER", "script-message rename-commit");
    setKeybind(ctx, "KP_ENTER", "script-message rename-commit");
    setKeybind(ctx, "ESC", "script-message rename-cancel");
}

bool writeFallbackInputConf(const std::filesystem::path& path) {
    std::ofstream conf(path, std::ios::binary | std::ios::trunc);
    if (!conf) return false;
    for (const auto& kb : kNormalBindings) {
        conf << kb.key << " " << kb.cmd << "\n";
    }
    for (const auto& kb : kExtraNormalBindings) {
        conf << kb.key << " " << kb.cmd << "\n";
    }
    conf.flush();
    return conf.good();
}

void syncChaptersToMpv(mpv_handle* ctx, const ChapterManager& manager) {
    const auto& chapters = manager.chapters();
    mpv_node_list array{};
    array.num = static_cast<int>(chapters.size());
    array.keys = nullptr;

    std::vector<mpv_node> mapNodes(chapters.size());
    std::vector<mpv_node_list> mapLists(chapters.size());
    std::vector<mpv_node> titleNodes(chapters.size());
    std::vector<mpv_node> timeNodes(chapters.size());
    std::vector<std::string> titleStrs(chapters.size());
    std::vector<char*> mapKeys(chapters.size() * 2);
    std::vector<mpv_node> mapValues(chapters.size() * 2);

    for (size_t i = 0; i < chapters.size(); ++i) {
        titleStrs[i] = manager.displayTitle(i);

        titleNodes[i].format = MPV_FORMAT_STRING;
        titleNodes[i].u.string = const_cast<char*>(titleStrs[i].c_str());

        timeNodes[i].format = MPV_FORMAT_DOUBLE;
        timeNodes[i].u.double_ = chapters[i].time;

        size_t idx = i * 2;
        mapKeys[idx] = const_cast<char*>("title");
        mapKeys[idx + 1] = const_cast<char*>("time");

        mapValues[idx] = titleNodes[i];
        mapValues[idx + 1] = timeNodes[i];

        mapLists[i].num = 2;
        mapLists[i].keys = &mapKeys[idx];
        mapLists[i].values = &mapValues[idx];

        mapNodes[i].format = MPV_FORMAT_NODE_MAP;
        mapNodes[i].u.list = &mapLists[i];
    }

    array.values = mapNodes.empty() ? nullptr : mapNodes.data();

    mpv_node root{};
    root.format = MPV_FORMAT_NODE_ARRAY;
    root.u.list = &array;
    mpv_set_property(ctx, "chapter-list", MPV_FORMAT_NODE, &root);
}

enum class NotifyLevel {
    Info,
    Success,
    Warning,
    Error
};

struct Notification {
    bool active = false;
    NotifyLevel level = NotifyLevel::Info;
    std::string text;
    std::chrono::steady_clock::time_point expiresAt;
};

enum class ConfirmAction {
    None,
    Quit,
    OverwriteXml,
    OverwriteMkv,
    PatchInPlace
};

struct KeyframeSearch {
    bool active = false;
    int direction = 1;
    double originPos = 0.0;
    double duration = 0.0;
    double lastTarget = -1.0;
    size_t stepIndex = 0;
    static constexpr std::array<double, 6> kSteps = {0.5, 1.5, 3.5, 7.5, 15.5, 30.0};
};

struct RenameState {
    bool active = false;
    int targetIndex = -1;
    std::string buffer;
};

enum class JobKind {
    Remux,
    Patch
};

struct JobCompletion {
    JobKind kind = JobKind::Remux;
    ProcessResult proc;
    std::string outputTarget;
    bool zeroChapters = false;
    std::filesystem::path tempXmlToClean;
};

class AsyncJob {
public:
    ~AsyncJob() { joinIfDoneOrWait(); }

    bool isRunning() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    JobKind activeKind() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeKind_;
    }

    bool start(mpv_handle* ctx,
               JobKind kind,
               std::vector<std::string> cmdArgs,
               std::string outputTarget,
               bool zeroChapters,
               std::filesystem::path tempXmlToClean = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return false;
        if (worker_.joinable()) {
            worker_.join();
        }
        running_ = true;
        finishedReady_ = false;
        activeKind_ = kind;

        worker_ = std::thread([this, ctx, kind, args = std::move(cmdArgs),
                               target = std::move(outputTarget), zeroChapters,
                               tmpXml = std::move(tempXmlToClean)]() {
            ProcessResult res = runProcess(args, false);
            {
                std::lock_guard<std::mutex> doneLock(mutex_);
                completion_.kind = kind;
                completion_.proc = res;
                completion_.outputTarget = target;
                completion_.zeroChapters = zeroChapters;
                completion_.tempXmlToClean = tmpXml;
                running_ = false;
                finishedReady_ = true;
            }
            if (ctx) {
                mpv_wakeup(ctx);
            }
        });
        return true;
    }

    bool checkFinished(JobCompletion& out) {
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finishedReady_) {
                out = std::move(completion_);
                finishedReady_ = false;
                ready = true;
            }
        }
        if (ready && worker_.joinable()) {
            worker_.join();
        }
        return ready;
    }

    void joinIfDoneOrWait() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    mutable std::mutex mutex_;
    std::thread worker_;
    bool running_ = false;
    bool finishedReady_ = false;
    JobKind activeKind_ = JobKind::Remux;
    JobCompletion completion_;
};

std::string assColorWrap(const char* bgrHex, const std::string& safeEscapedText) {
    return std::string("${osd-ass-cc/0}{\\\\c&H") + bgrHex + "&}${osd-ass-cc/1}" +
           safeEscapedText + "${osd-ass-cc/0}{\\\\r}${osd-ass-cc/1}";
}

const char* notifyColorHex(NotifyLevel lvl) {
    switch (lvl) {
        case NotifyLevel::Success: return "66EE66";
        case NotifyLevel::Warning: return "44CCFF";
        case NotifyLevel::Error:   return "5555FF";
        case NotifyLevel::Info:
        default:                   return "FFDD66";
    }
}

bool hasMatroskaExtension(const std::string& pathStr) {
    std::string ext = std::filesystem::path(pathStr).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".mkv" || ext == ".mka" || ext == ".mks" || ext == ".mk3d";
}

bool getCurrentTimePos(mpv_handle* ctx, double& outPos) {
    double pos = 0.0;
    if (mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0 && std::isfinite(pos)) {
        outPos = std::max(0.0, pos);
        return true;
    }
    return false;
}

std::string buildOsdMessage(const ChapterManager& manager,
                            int activeMpvChapter,
                            bool unsavedChanges,
                            bool isMuted,
                            double speed,
                            const AsyncJob& job,
                            const Notification& notify,
                            ConfirmAction confirmAction,
                            const CliOptions& opts,
                            const RenameState& renameState,
                            bool showHelp,
                            bool showList) {
    std::ostringstream osd;
    osd << "Frame: ${estimated-frame-number} / ${estimated-frame-count}\\n"
        << "Time:  ${playback-time} / ${duration}\\n";

    // Chapter summary + status badges.
    const size_t totalChapters = manager.chapters().size();
    osd << "Chapters: " << totalChapters << (unsavedChanges ? " (unsaved*)" : "");
    if (activeMpvChapter >= 0 && static_cast<size_t>(activeMpvChapter) < totalChapters) {
        osd << "  |  Current: #" << (activeMpvChapter + 1) << " \""
            << escapeMpvOsdText(manager.displayTitle(static_cast<size_t>(activeMpvChapter))) << "\"";
    }
    if (isMuted) {
        osd << "  |  " << assColorWrap("44CCFF", "[MUTED]");
    }
    if (std::abs(speed - 1.0) > 0.01) {
        char spdBuf[32];
        std::snprintf(spdBuf, sizeof(spdBuf), "[%.2fx]", speed);
        osd << "  |  " << assColorWrap("FFDD66", spdBuf);
    }
    if (job.isRunning()) {
        std::string badge = (job.activeKind() == JobKind::Patch) ? "[Patching...]" : "[Remuxing...]";
        osd << "  |  " << assColorWrap("44CCFF", badge);
    }

    if (notify.active) {
        osd << "\\n" << assColorWrap(notifyColorHex(notify.level), escapeMpvOsdText(notify.text));
    }

    if (renameState.active) {
        osd << "\\n\\n"
            << assColorWrap("FFDD66", "Rename Chapter #" + std::to_string(renameState.targetIndex + 1) + ":")
            << "\\n> " << escapeMpvOsdText(renameState.buffer) << "_"
            << "\\n[ENTER] Save  |  [ESC] Cancel  |  [Ctrl+U] Clear (auto-name)";
        return osd.str();
    }

    if (confirmAction != ConfirmAction::None) {
        std::string warning;
        switch (confirmAction) {
            case ConfirmAction::Quit:
                warning = "WARNING: Unsaved edits! Press Q again to quit, or S/X/P to save.";
                break;
            case ConfirmAction::OverwriteXml:
                warning = "WARNING: '" + std::filesystem::path(opts.chaptersXml).filename().string() +
                          "' already exists! Press S again to overwrite.";
                break;
            case ConfirmAction::OverwriteMkv:
                warning = "WARNING: '" + std::filesystem::path(opts.outputMkv).filename().string() +
                          "' already exists! Press X again to overwrite.";
                break;
            case ConfirmAction::PatchInPlace:
                warning = "WARNING: Modify source file IN-PLACE via mkvpropedit? Press P again to confirm.";
                break;
            case ConfirmAction::None:
                break;
        }
        osd << "\\n\\n" << assColorWrap("5555FF", escapeMpvOsdText(warning));
        return osd.str();
    }

    if (showHelp) {
        osd << "\\n\\nMKV Chapter Editor Help\\n"
            << "[H]/[?]     Toggle Help    | [T]/[TAB] Chapter List\\n"
            << "[SPACE]     Play / Pause   | [F]       Fullscreen\\n"
            << "[LEFT/RIGHT] Step 1 Frame  (+Alt: 100 | +Shift: 1000)\\n"
            << "[UP/DOWN]   Next/Prev Keyframe (+Alt/Shift: ~100/1000)\\n"
            << "[PGUP/PGDN] Next / Prev Chapter\\n"
            << "[C]/[INS]   Create Chapter | [D]/[DEL] Remove (0.5s)\\n"
            << "[U]         Move Chapter   | [R]       Rename Chapter\\n"
            << "[Z]/[Y]     Undo / Redo    | [M]/[9/0] Mute / Volume\\n"
            << "[[ / ] / BS] Speed -/+/1x  | [L]       Load XML\\n"
            << "[S]         Save XML       | [X]       Save & Remux MKV\\n"
            << "[P]         Patch In-Place | [Q]       Quit";
        return osd.str();
    }

    if (showList) {
        osd << "\\n\\nChapter List (" << totalChapters << "):\\n";
        if (totalChapters == 0) {
            osd << "  (no chapters yet - press C or INS to add)";
        } else {
            constexpr size_t kMaxVisible = 15;
            size_t startIdx = 0;
            if (totalChapters > kMaxVisible && activeMpvChapter > 0) {
                size_t half = kMaxVisible / 2;
                size_t active = static_cast<size_t>(activeMpvChapter);
                if (active > half) {
                    startIdx = std::min(active - half, totalChapters - kMaxVisible);
                }
            }
            size_t endIdx = std::min(totalChapters, startIdx + kMaxVisible);
            if (startIdx > 0) {
                osd << "  ... (" << startIdx << " earlier)\\n";
            }
            for (size_t i = startIdx; i < endIdx; ++i) {
                bool isCurrent = (static_cast<int>(i) == activeMpvChapter);
                std::ostringstream line;
                line << (isCurrent ? "> " : "  ")
                     << "#" << (i + 1) << "  "
                     << formatTimeDisplay(manager.chapters()[i].time) << "  "
                     << manager.displayTitle(i);
                std::string escapedLine = escapeMpvOsdText(line.str());
                if (isCurrent) {
                    osd << assColorWrap("FFDD66", escapedLine);
                } else {
                    osd << escapedLine;
                }
                if (i + 1 < endIdx) osd << "\\n";
            }
            if (endIdx < totalChapters) {
                osd << "\\n  ... (" << (totalChapters - endIdx) << " more)";
            }
        }
        return osd.str();
    }

    osd << "\\n\\nPress 'h' for help  |  't' for chapter list";
    return osd.str();
}

} // namespace

int main(int argc, char* argv[]) {
    CliOptions opts;
    std::string argError;
    if (!parseArgs(argc, argv, opts, argError)) {
        std::cerr << "Error: " << argError << "\n\n";
        printUsage(argv[0]);
        return 1;
    }
    if (opts.showHelp) {
        printUsage(argv[0]);
        return 0;
    }
    if (opts.showVersion) {
        std::cout << "MKV Chapter Editor " << kAppVersion << "\n";
        return 0;
    }

    std::error_code fsEc;
    std::filesystem::path inPath(opts.inputFile);
    if (!std::filesystem::exists(inPath, fsEc)) {
        std::cerr << "Error: File does not exist: " << opts.inputFile << "\n";
        return 1;
    }
    if (std::filesystem::is_directory(inPath, fsEc)) {
        std::cerr << "Error: Input path is a directory, not a video file: " << opts.inputFile << "\n";
        return 1;
    }
    if (!std::filesystem::is_regular_file(inPath, fsEc)) {
        std::cerr << "Error: Input path is not a regular file: " << opts.inputFile << "\n";
        return 1;
    }
    {
        std::ifstream fileCheck(opts.inputFile, std::ios::binary);
        if (!fileCheck.good()) {
            std::cerr << "Error: Could not read file: " << opts.inputFile << "\n";
            return 1;
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::optional<std::string> mkvmergeExec = findToolExecutable("mkvmerge");
    const std::optional<std::string> mkvpropeditExec = findToolExecutable("mkvpropedit");
    if (!mkvmergeExec) {
        std::cerr << "Warning: 'mkvmerge' not found in executable directory or PATH.\n"
                  << "  Editing and XML export (S) will work, but remuxing (X) is unavailable.\n";
    }
    if (!mkvpropeditExec) {
        std::cerr << "Info: 'mkvpropedit' not found; in-place chapter patching (P) is unavailable.\n";
    }

    const unsigned long apiVer = mpv_client_api_version();
    const bool useKeybindApi = (apiVer >= MPV_MAKE_VERSION(2, 0));
    const std::vector<RenameKeyEntry> renameKeys = buildRenameKeyEntries();
    TempFileGuard fallbackConfGuard;

    mpv_handle* ctx = mpv_create();
    if (!ctx) {
        std::cerr << "Fatal: Failed to create mpv context.\n";
        return 1;
    }

    if (!useKeybindApi) {
        std::filesystem::path tmpConf = std::filesystem::temp_directory_path(fsEc) /
            ("chapter_editor_bindings_" + std::to_string(currentPid()) + ".conf");
        if (writeFallbackInputConf(tmpConf)) {
            fallbackConfGuard.reset(tmpConf);
            mpv_set_option_string(ctx, "input-conf", tmpConf.string().c_str());
        } else {
            std::cerr << "Warning: Could not write temporary keybindings file.\n";
        }
    }

    mpv_set_option_string(ctx, "input-default-bindings", "no");
    mpv_set_option_string(ctx, "input-vo-keyboard", "yes");
    mpv_set_option_string(ctx, "pause", "yes");
    mpv_set_option_string(ctx, "sid", "no");
    mpv_set_option_string(ctx, "osd-level", "3");
    mpv_set_option_string(ctx, "osd-font-size", "24");
    mpv_set_option_string(ctx, "osd-align-x", "left");
    mpv_set_option_string(ctx, "osd-align-y", "top");
    mpv_set_option_string(ctx, "osd-margin-x", "10");
    mpv_set_option_string(ctx, "osd-margin-y", "10");
    mpv_set_option_string(ctx, "keep-open", "yes");
    mpv_set_option_string(ctx, "hwdec", "auto-safe");

    if (mpv_initialize(ctx) < 0) {
        std::cerr << "Fatal: mpv initialization failed.\n";
        mpv_terminate_destroy(ctx);
        return 1;
    }

    if (useKeybindApi) {
        applyNormalKeybinds(ctx, renameKeys);
    }

    mpv_observe_property(ctx, 1, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(ctx, 2, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(ctx, 3, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(ctx, 4, "chapter", MPV_FORMAT_INT64);
    mpv_observe_property(ctx, 5, "volume", MPV_FORMAT_DOUBLE);

    std::cout << "MKV Chapter Editor v" << kAppVersion << " | Loading: " << opts.inputFile << "\n"
              << "XML target: " << opts.chaptersXml << "\n"
              << "MKV target: " << opts.outputMkv << "\n"
              << "Press 'H' in the video window for full controls.\n";

    const char* loadCmd[] = {"loadfile", opts.inputFile.c_str(), nullptr};
    mpv_command(ctx, loadCmd);

    ChapterManager manager;
    AsyncJob job;
    Notification notify;
    KeyframeSearch kfSearch;
    RenameState renameState;

    bool running = true;
    int exitCode = 0;
    bool initialLoadCompleted = false;
    bool fileLoaded = false;
    bool showHelp = false;
    bool showList = false;
    bool unsavedChanges = false;
    ConfirmAction confirmAction = ConfirmAction::None;
    bool xmlOwnedBySession = false;
    bool mkvOwnedBySession = false;

    bool isPaused = true;
    bool isMuted = false;
    double playbackSpeed = 1.0;
    int activeMpvChapter = -1;
    bool osdDirty = true;

    auto lastProgressPing = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    auto setNotify = [&](NotifyLevel level, const std::string& text) {
        notify.active = true;
        notify.level = level;
        notify.text = text;
        notify.expiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(3500);
        osdDirty = true;
    };

    auto resolveChapterTargetIndex = [&](double timeSec) -> int {
        int idx = manager.findNearestChapterIndex(timeSec, kRemoveToleranceSec);
        if (idx >= 0) return idx;
        return manager.findActiveChapterIndex(timeSec);
    };

    auto performSaveXml = [&](bool notifyOnSuccess) -> bool {
        if (manager.chapters().empty()) {
            setNotify(NotifyLevel::Warning, "No chapters present. Nothing to save.");
            return false;
        }
        std::string err;
        if (!exportXML(manager.chapters(), opts.chaptersXml, opts.defaultLang, err)) {
            std::cerr << "Error: " << err << "\n";
            setNotify(NotifyLevel::Error, err);
            return false;
        }
        xmlOwnedBySession = true;
        unsavedChanges = false;
        std::cout << "Saved " << manager.chapters().size() << " chapter(s) to " << opts.chaptersXml << "\n";
        if (notifyOnSuccess) {
            setNotify(NotifyLevel::Success,
                      "Saved " + std::to_string(manager.chapters().size()) + " chapters to " +
                          std::filesystem::path(opts.chaptersXml).filename().string());
        }
        return true;
    };

    auto handleJobResult = [&](const JobCompletion& done) {
        if (!done.tempXmlToClean.empty()) {
            std::error_code ec;
            std::filesystem::remove(done.tempXmlToClean, ec);
        }

        const char* opName = (done.kind == JobKind::Patch) ? "In-place patch" : "Remux";
        if (!done.proc.launched) {
            std::cerr << "Error: Failed to launch MKVToolNix tool for " << opName << ".\n";
            setNotify(NotifyLevel::Error, std::string(opName) + " failed: could not launch executable.");
            return;
        }

        if (done.proc.exitCode == 0) {
            unsavedChanges = false;
            if (done.kind == JobKind::Remux) {
                mkvOwnedBySession = true;
            }
            std::cout << "SUCCESS: " << opName << " completed (" << done.outputTarget << ")\n";
            std::string msg = std::string(opName) + " complete";
            if (done.zeroChapters) msg += " (0 chapters)";
            msg += ": " + std::filesystem::path(done.outputTarget).filename().string();
            setNotify(NotifyLevel::Success, msg);
        } else if (done.proc.exitCode == 1) {
            // mkvmerge and mkvpropedit use exit code 1 for success with warnings.
            unsavedChanges = false;
            if (done.kind == JobKind::Remux) {
                mkvOwnedBySession = true;
            }
            std::cout << "WARNING: " << opName << " finished with warnings (exit code 1).\n";
            setNotify(NotifyLevel::Warning,
                      std::string(opName) + " finished with warnings (check terminal).");
        } else {
            std::cerr << "Error: " << opName << " failed with exit code " << done.proc.exitCode << ".\n";
            setNotify(NotifyLevel::Error,
                      std::string(opName) + " failed (exit code " + std::to_string(done.proc.exitCode) + ").");
        }
    };

    while (running && !g_interrupted.load()) {
        const auto now = std::chrono::steady_clock::now();

        if (notify.active && now >= notify.expiresAt) {
            notify.active = false;
            osdDirty = true;
        }

        JobCompletion done;
        if (job.checkFinished(done)) {
            handleJobResult(done);
            osdDirty = true;
        }

        if (osdDirty) {
            std::string msg = buildOsdMessage(manager,
                                              activeMpvChapter,
                                              unsavedChanges,
                                              isMuted,
                                              playbackSpeed,
                                              job,
                                              notify,
                                              confirmAction,
                                              opts,
                                              renameState,
                                              showHelp,
                                              showList);
            mpv_set_property_string(ctx, "osd-status-msg", msg.c_str());
            osdDirty = false;
        }

        if (fileLoaded && now - lastProgressPing >= std::chrono::milliseconds(450)) {
            const char* progCmd[] = {"show-progress", nullptr};
            mpv_command(ctx, progCmd);
            lastProgressPing = now;
        }

        double waitSec = 0.45;
        if (notify.active) {
            double rem = std::chrono::duration<double>(notify.expiresAt - now).count();
            waitSec = std::clamp(rem, 0.05, 0.25);
        } else if (job.isRunning()) {
            waitSec = 0.25;
        }

        mpv_event* event = mpv_wait_event(ctx, waitSec);

        if (event->event_id == MPV_EVENT_SHUTDOWN) {
            running = false;
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            auto* ef = static_cast<mpv_event_end_file*>(event->data);
            if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                std::cerr << "Error loading/playing '" << opts.inputFile << "': "
                          << mpv_error_string(ef->error) << "\n";
                if (!initialLoadCompleted) {
                    exitCode = 1;
                    running = false;
                } else {
                    setNotify(NotifyLevel::Error,
                              std::string("Playback error: ") + mpv_error_string(ef->error));
                }
            }
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            initialLoadCompleted = true;
            fileLoaded = true;
            osdDirty = true;

            std::vector<Chapter> loaded;
            double count = 0;
            if (mpv_get_property(ctx, "chapter-list/count", MPV_FORMAT_DOUBLE, &count) >= 0) {
                int n = static_cast<int>(count);
                for (int i = 0; i < n; ++i) {
                    std::string timeProp = "chapter-list/" + std::to_string(i) + "/time";
                    std::string titleProp = "chapter-list/" + std::to_string(i) + "/title";
                    double timeSec = 0.0;
                    if (mpv_get_property(ctx, timeProp.c_str(), MPV_FORMAT_DOUBLE, &timeSec) >= 0) {
                        char* titleStr = mpv_get_property_string(ctx, titleProp.c_str());
                        std::string title = titleStr ? titleStr : "";
                        if (titleStr) mpv_free(titleStr);
                        if (isGenericChapterTitle(title)) title.clear();
                        loaded.push_back({timeSec, title, ""});
                    }
                }
            }

            if (!loaded.empty()) {
                manager.replaceAll(std::move(loaded), false);
                std::cout << "Auto-loaded " << manager.chapters().size()
                          << " existing chapter(s) from source video.\n";
            }

            if (!opts.loadXml.empty()) {
                int xmlCount = 0;
                std::string err;
                if (manager.mergeFromXML(opts.loadXml, xmlCount, err)) {
                    std::cout << "Loaded " << xmlCount << " chapter(s) from " << opts.loadXml << "\n";
                    if (xmlCount > 0) {
                        unsavedChanges = true;
                        setNotify(NotifyLevel::Info,
                                  "Loaded " + std::to_string(xmlCount) + " chapters from XML");
                    }
                } else {
                    std::cerr << "Warning: " << err << "\n";
                    setNotify(NotifyLevel::Warning, err);
                }
            }

            syncChaptersToMpv(ctx, manager);
            double pos = 0.0;
            if (getCurrentTimePos(ctx, pos)) {
                activeMpvChapter = manager.findActiveChapterIndex(pos);
            }
        } else if (event->event_id == MPV_EVENT_SEEK) {
            if (!kfSearch.active && confirmAction != ConfirmAction::None) {
                confirmAction = ConfirmAction::None;
                osdDirty = true;
            }
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            double pos = 0.0;
            if (getCurrentTimePos(ctx, pos)) {
                if (kfSearch.active) {
                    bool moved = (kfSearch.direction > 0)
                                     ? (pos > kfSearch.originPos + 0.02)
                                     : (pos < kfSearch.originPos - 0.02);
                    bool hitBoundary = (kfSearch.direction < 0 && kfSearch.lastTarget <= 0.001) ||
                                       (kfSearch.direction > 0 && kfSearch.duration > 0.0 &&
                                        kfSearch.lastTarget >= kfSearch.duration - 0.01);
                    if (moved || hitBoundary || kfSearch.stepIndex + 1 >= KeyframeSearch::kSteps.size()) {
                        kfSearch.active = false;
                    } else {
                        ++kfSearch.stepIndex;
                        double target = kfSearch.originPos +
                                        kfSearch.direction * KeyframeSearch::kSteps[kfSearch.stepIndex];
                        target = std::max(0.0, target);
                        if (kfSearch.duration > 0.0) {
                            target = std::min(target, kfSearch.duration);
                        }
                        kfSearch.lastTarget = target;
                        std::string targetStr = std::to_string(target);
                        const char* seekArgs[] = {"seek", targetStr.c_str(), "absolute", "keyframes", nullptr};
                        mpv_command(ctx, seekArgs);
                    }
                }
                int idx = manager.findActiveChapterIndex(pos);
                if (idx != activeMpvChapter) {
                    activeMpvChapter = idx;
                }
            }
            osdDirty = true;
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto* prop = static_cast<mpv_event_property*>(event->data);
            if (prop && prop->format != MPV_FORMAT_NONE && prop->data != nullptr) {
                std::string name = prop->name ? prop->name : "";
                if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
                    bool nextPaused = (*static_cast<int*>(prop->data) != 0);
                    if (nextPaused != isPaused) {
                        isPaused = nextPaused;
                        if (confirmAction != ConfirmAction::None) {
                            confirmAction = ConfirmAction::None;
                        }
                        osdDirty = true;
                    }
                } else if (name == "mute" && prop->format == MPV_FORMAT_FLAG) {
                    isMuted = (*static_cast<int*>(prop->data) != 0);
                    osdDirty = true;
                } else if (name == "speed" && prop->format == MPV_FORMAT_DOUBLE) {
                    playbackSpeed = *static_cast<double*>(prop->data);
                    osdDirty = true;
                } else if (name == "chapter" && prop->format == MPV_FORMAT_INT64) {
                    activeMpvChapter = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    osdDirty = true;
                } else if (name == "volume" && prop->format == MPV_FORMAT_DOUBLE) {
                    double vol = *static_cast<double*>(prop->data);
                    char vbuf[48];
                    std::snprintf(vbuf, sizeof(vbuf), "Volume: %.0f%%", vol);
                    setNotify(NotifyLevel::Info, vbuf);
                }
            }
        } else if (event->event_id == MPV_EVENT_CLIENT_MESSAGE) {
            auto* msg = static_cast<mpv_event_client_message*>(event->data);
            if (!msg || msg->num_args <= 0) continue;

            std::string action = msg->args[0];

            // Handle in-window rename input messages first.
            if (renameState.active) {
                if (action == "rename-code" && msg->num_args >= 2) {
                    uint32_t cp = static_cast<uint32_t>(std::strtoul(msg->args[1], nullptr, 10));
                    if (renameState.buffer.size() < 120) {
                        appendUtf8(renameState.buffer, cp);
                        osdDirty = true;
                    }
                } else if (action == "rename-backspace") {
                    popLastUtf8Codepoint(renameState.buffer);
                    osdDirty = true;
                } else if (action == "rename-clear") {
                    renameState.buffer.clear();
                    osdDirty = true;
                } else if (action == "rename-commit") {
                    if (manager.renameChapter(renameState.targetIndex, renameState.buffer)) {
                        syncChaptersToMpv(ctx, manager);
                        unsavedChanges = true;
                        std::string finalTitle = manager.displayTitle(static_cast<size_t>(renameState.targetIndex));
                        std::cout << "Renamed chapter #" << (renameState.targetIndex + 1)
                                  << " to \"" << finalTitle << "\"\n";
                        setNotify(NotifyLevel::Success, "Renamed: \"" + finalTitle + "\"");
                    }
                    renameState.active = false;
                    if (useKeybindApi) applyNormalKeybinds(ctx, renameKeys);
                    osdDirty = true;
                } else if (action == "rename-cancel") {
                    renameState.active = false;
                    if (useKeybindApi) applyNormalKeybinds(ctx, renameKeys);
                    setNotify(NotifyLevel::Info, "Rename cancelled.");
                    osdDirty = true;
                }
                continue;
            }

            if (action != "jump-keyframe") {
                kfSearch.active = false;
            }

            // Clear pending confirmations if user triggers any unrelated action.
            if (action != "quit-app" && confirmAction == ConfirmAction::Quit) {
                confirmAction = ConfirmAction::None;
                osdDirty = true;
            } else if (action != "save-chapters" && confirmAction == ConfirmAction::OverwriteXml) {
                confirmAction = ConfirmAction::None;
                osdDirty = true;
            } else if (action != "export-and-remux" && confirmAction == ConfirmAction::OverwriteMkv) {
                confirmAction = ConfirmAction::None;
                osdDirty = true;
            } else if (action != "patch-inplace" && confirmAction == ConfirmAction::PatchInPlace) {
                confirmAction = ConfirmAction::None;
                osdDirty = true;
            }

            if (action == "toggle-help") {
                showHelp = !showHelp;
                if (showHelp) showList = false;
                osdDirty = true;
            } else if (action == "toggle-list") {
                showList = !showList;
                if (showList) showHelp = false;
                osdDirty = true;
            } else if (action == "jump-keyframe" && msg->num_args >= 2) {
                int dir = (std::atoi(msg->args[1]) >= 0) ? 1 : -1;
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) {
                    setNotify(NotifyLevel::Warning, "Video position not ready yet.");
                    continue;
                }
                double dur = 0.0;
                mpv_get_property(ctx, "duration", MPV_FORMAT_DOUBLE, &dur);
                if (dir < 0 && pos <= 0.02) continue;
                if (dir > 0 && dur > 0.0 && pos >= dur - 0.02) continue;

                kfSearch.active = true;
                kfSearch.direction = dir;
                kfSearch.originPos = pos;
                kfSearch.duration = dur;
                kfSearch.stepIndex = 0;

                double target = std::max(0.0, pos + dir * KeyframeSearch::kSteps[0]);
                if (dur > 0.0) target = std::min(target, dur);
                kfSearch.lastTarget = target;
                std::string targetStr = std::to_string(target);
                const char* seekArgs[] = {"seek", targetStr.c_str(), "absolute", "keyframes", nullptr};
                mpv_command(ctx, seekArgs);
            } else if (action == "jump-frames" && msg->num_args >= 3) {
                int frames = std::atoi(msg->args[1]);
                std::string mode = msg->args[2];

                double fps = 24.0;
                if (mpv_get_property(ctx, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps) < 0 || fps <= 0.0) {
                    if (mpv_get_property(ctx, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0 || fps <= 0.0) {
                        fps = 24.0;
                    }
                }
                double jumpSec = static_cast<double>(frames) / fps;
                std::string jumpStr = std::to_string(jumpSec);
                const char* cmdArgs[] = {"seek", jumpStr.c_str(), "relative", mode.c_str(), nullptr};
                mpv_command(ctx, cmdArgs);
            } else if (action == "quit-app") {
                if (job.isRunning()) {
                    setNotify(NotifyLevel::Warning, "Wait for remux/patch job to finish before quitting.");
                    continue;
                }
                if (unsavedChanges && confirmAction != ConfirmAction::Quit) {
                    confirmAction = ConfirmAction::Quit;
                    osdDirty = true;
                    std::cout << "\nWARNING: Unsaved chapter edits! Press Q again to quit, or S/X/P to save.\n";
                } else {
                    running = false;
                }
            } else if (action == "add-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) {
                    setNotify(NotifyLevel::Warning, "Wait until the video finishes loading.");
                    continue;
                }
                manager.addChapter(pos);
                syncChaptersToMpv(ctx, manager);
                activeMpvChapter = manager.findActiveChapterIndex(pos);
                unsavedChanges = true;
                osdDirty = true;
                std::cout << "Chapter created at " << formatTimeDisplay(pos)
                          << " (Total: " << manager.chapters().size() << ")\n";
                setNotify(NotifyLevel::Success,
                          "Added chapter at " + formatTimeDisplay(pos) +
                              " (Total: " + std::to_string(manager.chapters().size()) + ")");
            } else if (action == "remove-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) {
                    setNotify(NotifyLevel::Warning, "Wait until the video finishes loading.");
                    continue;
                }
                if (manager.chapters().empty()) {
                    setNotify(NotifyLevel::Info, "No chapters to remove.");
                    continue;
                }
                Chapter removed;
                if (manager.removeNearestChapter(pos, kRemoveToleranceSec, removed)) {
                    syncChaptersToMpv(ctx, manager);
                    activeMpvChapter = manager.findActiveChapterIndex(pos);
                    unsavedChanges = true;
                    osdDirty = true;
                    std::cout << "Removed chapter at " << formatTimeDisplay(removed.time) << "\n";
                    setNotify(NotifyLevel::Success,
                              "Removed chapter at " + formatTimeDisplay(removed.time));
                } else {
                    setNotify(NotifyLevel::Warning, "No chapter within 0.5s of current position.");
                }
            } else if (action == "move-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) {
                    setNotify(NotifyLevel::Warning, "Wait until the video finishes loading.");
                    continue;
                }
                int idx = resolveChapterTargetIndex(pos);
                if (idx < 0) {
                    setNotify(NotifyLevel::Warning, "No chapter available to move.");
                    continue;
                }
                std::string label = manager.displayTitle(static_cast<size_t>(idx));
                if (manager.moveChapter(idx, pos)) {
                    syncChaptersToMpv(ctx, manager);
                    activeMpvChapter = manager.findActiveChapterIndex(pos);
                    unsavedChanges = true;
                    osdDirty = true;
                    std::cout << "Moved \"" << label << "\" to " << formatTimeDisplay(pos) << "\n";
                    setNotify(NotifyLevel::Success,
                              "Moved \"" + label + "\" to " + formatTimeDisplay(pos));
                }
            } else if (action == "rename-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) {
                    setNotify(NotifyLevel::Warning, "Wait until the video finishes loading.");
                    continue;
                }
                int idx = resolveChapterTargetIndex(pos);
                if (idx < 0) {
                    setNotify(NotifyLevel::Warning, "No chapter selected/nearby to rename.");
                    continue;
                }
                if (useKeybindApi) {
                    int pauseVal = 1;
                    mpv_set_property(ctx, "pause", MPV_FORMAT_FLAG, &pauseVal);
                    renameState.active = true;
                    renameState.targetIndex = idx;
                    renameState.buffer = manager.chapters()[static_cast<size_t>(idx)].title;
                    applyRenameKeybinds(ctx, renameKeys);
                    osdDirty = true;
                } else if (isStdinInteractive()) {
                    std::cout << "Enter new title for Chapter #" << (idx + 1)
                              << " (blank for auto-name): " << std::flush;
                    std::string line;
                    if (std::getline(std::cin, line)) {
                        manager.renameChapter(idx, line);
                        syncChaptersToMpv(ctx, manager);
                        unsavedChanges = true;
                        setNotify(NotifyLevel::Success,
                                  "Renamed: \"" + manager.displayTitle(static_cast<size_t>(idx)) + "\"");
                    }
                } else {
                    setNotify(NotifyLevel::Warning, "Interactive rename requires libmpv >= 2.0 or TTY.");
                }
            } else if (action == "undo") {
                if (manager.undo()) {
                    syncChaptersToMpv(ctx, manager);
                    double pos = 0.0;
                    if (getCurrentTimePos(ctx, pos)) {
                        activeMpvChapter = manager.findActiveChapterIndex(pos);
                    }
                    unsavedChanges = true;
                    setNotify(NotifyLevel::Info, "Undid last chapter edit.");
                } else {
                    setNotify(NotifyLevel::Info, "Nothing to undo.");
                }
            } else if (action == "redo") {
                if (manager.redo()) {
                    syncChaptersToMpv(ctx, manager);
                    double pos = 0.0;
                    if (getCurrentTimePos(ctx, pos)) {
                        activeMpvChapter = manager.findActiveChapterIndex(pos);
                    }
                    unsavedChanges = true;
                    setNotify(NotifyLevel::Info, "Redid chapter edit.");
                } else {
                    setNotify(NotifyLevel::Info, "Nothing to redo.");
                }
            } else if (action == "next-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) continue;
                const auto& chaps = manager.chapters();
                auto it = std::upper_bound(chaps.begin(), chaps.end(), pos + 0.1,
                    [](double val, const Chapter& c) { return val < c.time; });
                if (it != chaps.end()) {
                    std::string target = std::to_string(it->time);
                    const char* seekArgs[] = {"seek", target.c_str(), "absolute", "exact", nullptr};
                    mpv_command(ctx, seekArgs);
                } else {
                    setNotify(NotifyLevel::Info, "Already at last chapter.");
                }
            } else if (action == "prev-chapter") {
                double pos = 0.0;
                if (!getCurrentTimePos(ctx, pos)) continue;
                const auto& chaps = manager.chapters();
                bool jumped = false;
                for (auto rit = chaps.rbegin(); rit != chaps.rend(); ++rit) {
                    if (rit->time < pos - 0.1) {
                        std::string target = std::to_string(rit->time);
                        const char* seekArgs[] = {"seek", target.c_str(), "absolute", "exact", nullptr};
                        mpv_command(ctx, seekArgs);
                        jumped = true;
                        break;
                    }
                }
                if (!jumped) {
                    setNotify(NotifyLevel::Info, "No earlier chapter.");
                }
            } else if (action == "save-chapters") {
                if (manager.chapters().empty()) {
                    setNotify(NotifyLevel::Warning, "No chapters present. Nothing to save.");
                    continue;
                }
                std::error_code ec;
                bool exists = std::filesystem::exists(opts.chaptersXml, ec);
                if (exists && !opts.overwrite && !xmlOwnedBySession &&
                    confirmAction != ConfirmAction::OverwriteXml) {
                    confirmAction = ConfirmAction::OverwriteXml;
                    osdDirty = true;
                    continue;
                }
                confirmAction = ConfirmAction::None;
                performSaveXml(true);
            } else if (action == "load-chapters") {
                std::string sourceXml = opts.chaptersXml;
                std::error_code ec;
                if (!std::filesystem::exists(sourceXml, ec) && !opts.explicitChaptersXml &&
                    std::filesystem::exists("chapters.xml", ec)) {
                    sourceXml = "chapters.xml";
                }
                int loadedCount = 0;
                std::string err;
                if (!manager.mergeFromXML(sourceXml, loadedCount, err)) {
                    std::cerr << "Error: " << err << "\n";
                    setNotify(NotifyLevel::Error, err);
                    continue;
                }
                syncChaptersToMpv(ctx, manager);
                double pos = 0.0;
                if (getCurrentTimePos(ctx, pos)) {
                    activeMpvChapter = manager.findActiveChapterIndex(pos);
                }
                if (loadedCount > 0) {
                    unsavedChanges = true;
                }
                std::cout << "Merged " << loadedCount << " chapter(s) from " << sourceXml << "\n";
                setNotify(NotifyLevel::Success,
                          "Merged " + std::to_string(loadedCount) + " chapters from " +
                              std::filesystem::path(sourceXml).filename().string());
            } else if (action == "export-and-remux") {
                if (!mkvmergeExec) {
                    setNotify(NotifyLevel::Error, "mkvmerge not found! Install MKVToolNix or press S for XML.");
                    continue;
                }
                if (job.isRunning()) {
                    setNotify(NotifyLevel::Warning, "A remux/patch operation is already running.");
                    continue;
                }

                std::error_code ec;
                bool mkvExists = std::filesystem::exists(opts.outputMkv, ec);
                if (mkvExists && !opts.overwrite && !mkvOwnedBySession &&
                    confirmAction != ConfirmAction::OverwriteMkv) {
                    confirmAction = ConfirmAction::OverwriteMkv;
                    osdDirty = true;
                    continue;
                }
                confirmAction = ConfirmAction::None;

                const bool zeroChapters = manager.chapters().empty();
                std::vector<std::string> cmd;
                if (zeroChapters) {
                    cmd = {*mkvmergeExec, "-o", opts.outputMkv, "--no-chapters", opts.inputFile};
                } else {
                    if (!performSaveXml(false)) {
                        continue;
                    }
                    cmd = {*mkvmergeExec, "-o", opts.outputMkv,
                           "--chapters", opts.chaptersXml,
                           "--no-chapters", opts.inputFile};
                }

                std::cout << "Starting background remux to '" << opts.outputMkv << "'...\n";
                if (job.start(ctx, JobKind::Remux, std::move(cmd), opts.outputMkv, zeroChapters)) {
                    setNotify(NotifyLevel::Info,
                              zeroChapters ? "Remuxing chapter-free MKV..."
                                           : "Remuxing MKV in background...");
                }
            } else if (action == "patch-inplace") {
                if (!mkvpropeditExec) {
                    setNotify(NotifyLevel::Error, "mkvpropedit not found! Install MKVToolNix.");
                    continue;
                }
                if (!hasMatroskaExtension(opts.inputFile)) {
                    setNotify(NotifyLevel::Error, "In-place patch only supports Matroska (.mkv/.mka/.mks/.mk3d).");
                    continue;
                }
                if (job.isRunning()) {
                    setNotify(NotifyLevel::Warning, "A remux/patch operation is already running.");
                    continue;
                }
                if (confirmAction != ConfirmAction::PatchInPlace) {
                    confirmAction = ConfirmAction::PatchInPlace;
                    osdDirty = true;
                    continue;
                }
                confirmAction = ConfirmAction::None;

                const bool zeroChapters = manager.chapters().empty();
                std::vector<std::string> cmd;
                std::filesystem::path tmpXml;
                if (zeroChapters) {
                    cmd = {*mkvpropeditExec, opts.inputFile, "--chapters", ""};
                } else {
                    std::error_code ec;
                    tmpXml = std::filesystem::temp_directory_path(ec) /
                             ("chapter_editor_patch_" + std::to_string(currentPid()) + ".xml");
                    std::string err;
                    if (!exportXML(manager.chapters(), tmpXml.string(), opts.defaultLang, err)) {
                        setNotify(NotifyLevel::Error, err);
                        continue;
                    }
                    cmd = {*mkvpropeditExec, opts.inputFile, "--chapters", tmpXml.string()};
                }

                std::cout << "Starting background in-place patch on '" << opts.inputFile << "'...\n";
                if (job.start(ctx, JobKind::Patch, std::move(cmd), opts.inputFile, zeroChapters, tmpXml)) {
                    setNotify(NotifyLevel::Info,
                              zeroChapters ? "Removing chapters in-place..."
                                           : "Patching chapters in-place...");
                }
            }
        }
    }

    job.joinIfDoneOrWait();
    JobCompletion trailing;
    if (job.checkFinished(trailing)) {
        handleJobResult(trailing);
    }

    mpv_terminate_destroy(ctx);
    std::cout << "Exiting Chapter Editor cleanly.\n";
    return exitCode;
}
