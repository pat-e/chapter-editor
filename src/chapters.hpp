#ifndef CHAPTER_EDITOR_CHAPTERS_HPP
#define CHAPTER_EDITOR_CHAPTERS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Chapter {
    double time = 0.0;
    std::string title;     // Empty means auto-generated ("Chapter N")
    std::string language;  // Empty means default language (e.g. "eng")
};

// Converts seconds to Matroska XML format HH:MM:SS.nnnnnnnnn using integer nanosecond rounding.
std::string formatTimeXML(double totalSeconds);

// Converts seconds to display format HH:MM:SS.mmm using integer millisecond rounding.
std::string formatTimeDisplay(double totalSeconds);

// Parses HH:MM:SS[.fraction] into seconds (locale-independent). Returns false if malformed.
bool parseTime(const std::string& timeStr, double& outSeconds);

// Escapes special XML characters (&, <, >, ", ').
std::string xmlEscape(const std::string& input);

// Unescapes standard XML entities and decimal/hex numeric character references in a single pass.
std::string xmlUnescape(const std::string& input);

// Returns true only if title strictly matches "Chapter <digits>" (e.g., "Chapter 1").
bool isGenericChapterTitle(const std::string& title);

// Sorts chapters by time and deduplicates entries within dedupTolerance, preserving non-empty metadata.
void sanitizeChapters(std::vector<Chapter>& chapters, double dedupTolerance = 0.1);

// Exports chapters to a Matroska XML file.
bool exportXML(const std::vector<Chapter>& chapters,
               const std::string& filename,
               const std::string& defaultLang,
               std::string& errorMsg);

// Loads and merges chapters from a Matroska XML file into `chapters`.
bool loadXML(std::vector<Chapter>& chapters,
             const std::string& filename,
             int& loadedCount,
             std::string& errorMsg);

// Escapes text for safe inclusion inside mpv's osd-status-msg (escapes '\', '$', and newlines).
std::string escapeMpvOsdText(const std::string& text);

// Appends a Unicode codepoint encoded as UTF-8.
void appendUtf8(std::string& out, uint32_t codepoint);

// Removes the last UTF-8 codepoint from a string.
void popLastUtf8Codepoint(std::string& str);

// Manages chapter markers along with bounded undo/redo history.
class ChapterManager {
public:
    static constexpr size_t kMaxUndoHistory = 100;

    const std::vector<Chapter>& chapters() const { return chapters_; }

    void replaceAll(std::vector<Chapter> next, bool recordUndo = false);

    std::string displayTitle(size_t index) const;
    int findActiveChapterIndex(double timeSec) const;
    int findNearestChapterIndex(double timeSec, double maxDistance) const;

    void addChapter(double timeSec, const std::string& title = "", const std::string& lang = "");
    bool removeNearestChapter(double timeSec, double tolerance, Chapter& removedOut);
    bool moveChapter(int index, double newTime);
    bool renameChapter(int index, const std::string& newTitle);
    bool mergeFromXML(const std::string& filename, int& loadedCount, std::string& errorMsg);

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    bool undo();
    bool redo();

private:
    void pushUndoState();

    std::vector<Chapter> chapters_;
    std::vector<std::vector<Chapter>> undoStack_;
    std::vector<std::vector<Chapter>> redoStack_;
};

#endif // CHAPTER_EDITOR_CHAPTERS_HPP
