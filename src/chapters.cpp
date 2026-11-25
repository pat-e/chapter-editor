#include "chapters.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace {

std::string trimAscii(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

bool parseUnsignedBounded(const std::string& s, int64_t maxVal, int64_t& out) {
    if (s.empty()) return false;
    int64_t val = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        int digit = c - '0';
        if (val > (maxVal - digit) / 10) return false;
        val = val * 10 + digit;
    }
    out = val;
    return true;
}

double clampNonNegativeSeconds(double totalSeconds) {
    if (!std::isfinite(totalSeconds) || totalSeconds < 0.0) {
        return 0.0;
    }
    // Cap at 99,999 hours so nanosecond conversion never overflows int64_t.
    constexpr double kMaxSeconds = 359999999.0;
    return std::min(totalSeconds, kMaxSeconds);
}

// Strips XML comments (<!-- ... -->) outside of tags for simpler tag scanning.
std::string stripXmlComments(const std::string& xml) {
    std::string result;
    result.reserve(xml.size());
    size_t pos = 0;
    while (pos < xml.size()) {
        size_t commentStart = xml.find("<!--", pos);
        if (commentStart == std::string::npos) {
            result.append(xml, pos, std::string::npos);
            break;
        }
        result.append(xml, pos, commentStart - pos);
        size_t commentEnd = xml.find("-->", commentStart + 4);
        if (commentEnd == std::string::npos) {
            break;
        }
        pos = commentEnd + 3;
    }
    return result;
}

// Finds an opening XML tag `<tagName ...>` or `<tagName .../>` in [startPos, endPos).
bool findTagOpen(const std::string& xml,
                 const std::string& tagName,
                 size_t startPos,
                 size_t endPos,
                 size_t& outTagStart,
                 size_t& outAfterOpenTag,
                 bool& outSelfClosing) {
    const std::string pattern = "<" + tagName;
    size_t pos = startPos;
    while (pos < endPos) {
        size_t found = xml.find(pattern, pos);
        if (found == std::string::npos || found >= endPos) {
            return false;
        }
        size_t afterName = found + pattern.size();
        if (afterName >= endPos) {
            return false;
        }
        char nextChar = xml[afterName];
        if (nextChar != '>' && nextChar != '/' &&
            !std::isspace(static_cast<unsigned char>(nextChar))) {
            pos = afterName;
            continue;
        }

        // Scan until closing '>' respecting quoted attribute values.
        bool inSingle = false;
        bool inDouble = false;
        size_t closeAngle = std::string::npos;
        for (size_t i = afterName; i < endPos; ++i) {
            char ch = xml[i];
            if (ch == '\'' && !inDouble) {
                inSingle = !inSingle;
            } else if (ch == '"' && !inSingle) {
                inDouble = !inDouble;
            } else if (ch == '>' && !inSingle && !inDouble) {
                closeAngle = i;
                break;
            }
        }
        if (closeAngle == std::string::npos) {
            return false;
        }

        outSelfClosing = false;
        size_t j = closeAngle;
        while (j > afterName && std::isspace(static_cast<unsigned char>(xml[j - 1]))) {
            --j;
        }
        if (j > afterName && xml[j - 1] == '/') {
            outSelfClosing = true;
        }

        outTagStart = found;
        outAfterOpenTag = closeAngle + 1;
        return true;
    }
    return false;
}

// Finds a closing tag `</tagName>` in [startPos, endPos).
bool findTagClose(const std::string& xml,
                  const std::string& tagName,
                  size_t startPos,
                  size_t endPos,
                  size_t& outCloseStart,
                  size_t& outAfterClose) {
    const std::string pattern = "</" + tagName;
    size_t pos = startPos;
    while (pos < endPos) {
        size_t found = xml.find(pattern, pos);
        if (found == std::string::npos || found >= endPos) {
            return false;
        }
        size_t i = found + pattern.size();
        while (i < endPos && std::isspace(static_cast<unsigned char>(xml[i]))) {
            ++i;
        }
        if (i < endPos && xml[i] == '>') {
            outCloseStart = found;
            outAfterClose = i + 1;
            return true;
        }
        pos = found + pattern.size();
    }
    return false;
}

// Finds the matching `</ChapterAtom>` for an open atom whose body begins at `bodyStart`.
bool findMatchingChapterAtomClose(const std::string& xml,
                                  size_t bodyStart,
                                  size_t& outCloseStart) {
    int depth = 1;
    size_t cursor = bodyStart;
    const size_t limit = xml.size();

    while (cursor < limit) {
        size_t openStart = 0;
        size_t openAfter = 0;
        bool selfClosing = false;
        bool hasOpen = findTagOpen(xml, "ChapterAtom", cursor, limit, openStart, openAfter, selfClosing);

        size_t closeStart = 0;
        size_t closeAfter = 0;
        bool hasClose = findTagClose(xml, "ChapterAtom", cursor, limit, closeStart, closeAfter);

        if (!hasClose) {
            return false;
        }

        if (hasOpen && openStart < closeStart) {
            if (!selfClosing) {
                ++depth;
            }
            cursor = openAfter;
        } else {
            --depth;
            if (depth == 0) {
                outCloseStart = closeStart;
                return true;
            }
            cursor = closeAfter;
        }
    }
    return false;
}

// Builds the text of an atom excluding nested <ChapterAtom>...</ChapterAtom> sub-blocks.
std::string extractDirectAtomContent(const std::string& xml, size_t bodyStart, size_t bodyEnd) {
    std::string direct;
    size_t cursor = bodyStart;
    while (cursor < bodyEnd) {
        size_t nestedStart = 0;
        size_t nestedAfterOpen = 0;
        bool selfClosing = false;
        if (!findTagOpen(xml, "ChapterAtom", cursor, bodyEnd, nestedStart, nestedAfterOpen, selfClosing)) {
            direct.append(xml, cursor, bodyEnd - cursor);
            break;
        }
        direct.append(xml, cursor, nestedStart - cursor);
        if (selfClosing) {
            cursor = nestedAfterOpen;
            continue;
        }
        size_t nestedCloseStart = 0;
        if (!findMatchingChapterAtomClose(xml, nestedAfterOpen, nestedCloseStart) ||
            nestedCloseStart >= bodyEnd) {
            break;
        }
        size_t dummyClose = 0;
        size_t afterNestedClose = 0;
        if (!findTagClose(xml, "ChapterAtom", nestedCloseStart, bodyEnd, dummyClose, afterNestedClose)) {
            break;
        }
        cursor = afterNestedClose;
    }
    return direct;
}

bool extractTagText(const std::string& xml, const std::string& tagName, std::string& outText) {
    size_t tagStart = 0;
    size_t contentStart = 0;
    bool selfClosing = false;
    if (!findTagOpen(xml, tagName, 0, xml.size(), tagStart, contentStart, selfClosing)) {
        return false;
    }
    if (selfClosing) {
        outText.clear();
        return true;
    }
    size_t closeStart = 0;
    size_t afterClose = 0;
    if (!findTagClose(xml, tagName, contentStart, xml.size(), closeStart, afterClose)) {
        return false;
    }
    outText = xml.substr(contentStart, closeStart - contentStart);
    return true;
}

} // namespace

void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return; // Skip invalid UTF-16 surrogates.
        }
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

void popLastUtf8Codepoint(std::string& str) {
    while (!str.empty()) {
        unsigned char c = static_cast<unsigned char>(str.back());
        str.pop_back();
        // Stop once we remove a leading byte (ASCII or UTF-8 start byte 11xxxxxx).
        if ((c & 0xC0) != 0x80) {
            break;
        }
    }
}

std::string formatTimeXML(double totalSeconds) {
    const double clamped = clampNonNegativeSeconds(totalSeconds);
    const int64_t totalNs = static_cast<int64_t>(std::llround(clamped * 1000000000.0));
    const int64_t ns = totalNs % 1000000000LL;
    const int64_t totalWholeSecs = totalNs / 1000000000LL;
    const int64_t secs = totalWholeSecs % 60LL;
    const int64_t totalMins = totalWholeSecs / 60LL;
    const int64_t mins = totalMins % 60LL;
    const int64_t hours = totalMins / 60LL;

    char buffer[48];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02lld:%02lld:%02lld.%09lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(mins),
                  static_cast<long long>(secs),
                  static_cast<long long>(ns));
    return std::string(buffer);
}

std::string formatTimeDisplay(double totalSeconds) {
    const double clamped = clampNonNegativeSeconds(totalSeconds);
    const int64_t totalMs = static_cast<int64_t>(std::llround(clamped * 1000.0));
    const int64_t ms = totalMs % 1000LL;
    const int64_t totalWholeSecs = totalMs / 1000LL;
    const int64_t secs = totalWholeSecs % 60LL;
    const int64_t totalMins = totalWholeSecs / 60LL;
    const int64_t mins = totalMins % 60LL;
    const int64_t hours = totalMins / 60LL;

    char buffer[40];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02lld:%02lld:%02lld.%03lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(mins),
                  static_cast<long long>(secs),
                  static_cast<long long>(ms));
    return std::string(buffer);
}

bool parseTime(const std::string& timeStr, double& outSeconds) {
    const std::string trimmed = trimAscii(timeStr);
    if (trimmed.empty()) return false;

    const size_t colon1 = trimmed.find(':');
    if (colon1 == std::string::npos) return false;
    const size_t colon2 = trimmed.find(':', colon1 + 1);
    if (colon2 == std::string::npos) return false;
    if (trimmed.find(':', colon2 + 1) != std::string::npos) return false;

    const std::string hStr = trimmed.substr(0, colon1);
    const std::string mStr = trimmed.substr(colon1 + 1, colon2 - colon1 - 1);
    const std::string sFull = trimmed.substr(colon2 + 1);

    int64_t hours = 0;
    int64_t mins = 0;
    if (!parseUnsignedBounded(hStr, 99999999LL, hours)) return false;
    if (mStr.size() > 2 || !parseUnsignedBounded(mStr, 59, mins)) return false;

    const size_t dotPos = sFull.find('.');
    std::string sIntStr;
    std::string sFracStr;
    if (dotPos == std::string::npos) {
        sIntStr = sFull;
    } else {
        sIntStr = sFull.substr(0, dotPos);
        sFracStr = sFull.substr(dotPos + 1);
        if (sFracStr.empty()) return false;
        for (char c : sFracStr) {
            if (c < '0' || c > '9') return false;
        }
    }

    int64_t secs = 0;
    if (sIntStr.size() > 2 || !parseUnsignedBounded(sIntStr, 59, secs)) return false;

    double frac = 0.0;
    double scale = 0.1;
    for (char c : sFracStr) {
        frac += static_cast<double>(c - '0') * scale;
        scale *= 0.1;
    }

    outSeconds = static_cast<double>(hours) * 3600.0 +
                 static_cast<double>(mins) * 60.0 +
                 static_cast<double>(secs) + frac;
    return true;
}

std::string xmlEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&':  out.append("&amp;"); break;
            case '<':  out.append("&lt;"); break;
            case '>':  out.append("&gt;"); break;
            case '"':  out.append("&quot;"); break;
            case '\'': out.append("&apos;"); break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string xmlUnescape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] != '&') {
            out.push_back(input[i]);
            ++i;
            continue;
        }

        size_t semi = input.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(input[i]);
            ++i;
            continue;
        }

        const std::string entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") {
            out.push_back('&');
            i = semi + 1;
        } else if (entity == "lt") {
            out.push_back('<');
            i = semi + 1;
        } else if (entity == "gt") {
            out.push_back('>');
            i = semi + 1;
        } else if (entity == "quot") {
            out.push_back('"');
            i = semi + 1;
        } else if (entity == "apos") {
            out.push_back('\'');
            i = semi + 1;
        } else if (entity.size() >= 2 && entity[0] == '#') {
            uint64_t code = 0;
            bool valid = true;
            if (entity[1] == 'x' || entity[1] == 'X') {
                if (entity.size() == 2) valid = false;
                for (size_t k = 2; k < entity.size() && valid; ++k) {
                    char ch = entity[k];
                    code <<= 4;
                    if (ch >= '0' && ch <= '9') {
                        code |= static_cast<uint64_t>(ch - '0');
                    } else if (ch >= 'a' && ch <= 'f') {
                        code |= static_cast<uint64_t>(ch - 'a' + 10);
                    } else if (ch >= 'A' && ch <= 'F') {
                        code |= static_cast<uint64_t>(ch - 'A' + 10);
                    } else {
                        valid = false;
                    }
                    if (code > 0x10FFFF) valid = false;
                }
            } else {
                for (size_t k = 1; k < entity.size() && valid; ++k) {
                    char ch = entity[k];
                    if (ch < '0' || ch > '9') {
                        valid = false;
                        break;
                    }
                    code = code * 10 + static_cast<uint64_t>(ch - '0');
                    if (code > 0x10FFFF) valid = false;
                }
            }

            if (valid && code > 0 && !(code >= 0xD800 && code <= 0xDFFF)) {
                appendUtf8(out, static_cast<uint32_t>(code));
                i = semi + 1;
            } else {
                out.push_back(input[i]);
                ++i;
            }
        } else {
            out.push_back(input[i]);
            ++i;
        }
    }
    return out;
}

bool isGenericChapterTitle(const std::string& title) {
    const std::string prefix = "Chapter ";
    if (title.size() <= prefix.size()) return false;
    if (title.compare(0, prefix.size(), prefix) != 0) return false;
    for (size_t i = prefix.size(); i < title.size(); ++i) {
        if (title[i] < '0' || title[i] > '9') {
            return false;
        }
    }
    return true;
}

void sanitizeChapters(std::vector<Chapter>& chapters, double dedupTolerance) {
    std::stable_sort(chapters.begin(), chapters.end(),
        [](const Chapter& a, const Chapter& b) {
            return a.time < b.time;
        });

    if (chapters.empty()) return;

    std::vector<Chapter> merged;
    merged.reserve(chapters.size());
    merged.push_back(chapters[0]);

    for (size_t i = 1; i < chapters.size(); ++i) {
        if (std::abs(chapters[i].time - merged.back().time) < dedupTolerance) {
            if (merged.back().title.empty() && !chapters[i].title.empty()) {
                merged.back().title = chapters[i].title;
            }
            if (merged.back().language.empty() && !chapters[i].language.empty()) {
                merged.back().language = chapters[i].language;
            }
        } else {
            merged.push_back(chapters[i]);
        }
    }
    chapters.swap(merged);
}

bool exportXML(const std::vector<Chapter>& chapters,
               const std::string& filename,
               const std::string& defaultLang,
               std::string& errorMsg) {
    if (chapters.empty()) {
        errorMsg = "No chapters to export.";
        return false;
    }

    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out) {
        errorMsg = "Failed to open '" + filename + "' for writing.";
        return false;
    }

    const std::string fallbackLang = defaultLang.empty() ? "eng" : defaultLang;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<Chapters>\n"
        << "  <EditionEntry>\n"
        << "    <EditionFlagDefault>1</EditionFlagDefault>\n"
        << "    <EditionUID>1</EditionUID>\n";

    for (size_t i = 0; i < chapters.size(); ++i) {
        std::string title = chapters[i].title;
        if (title.empty()) {
            title = "Chapter " + std::to_string(i + 1);
        }
        const std::string& lang = chapters[i].language.empty() ? fallbackLang : chapters[i].language;

        out << "    <ChapterAtom>\n";
        out << "      <ChapterTimeStart>" << formatTimeXML(chapters[i].time) << "</ChapterTimeStart>\n";
        out << "      <ChapterDisplay>\n";
        out << "        <ChapterString>" << xmlEscape(title) << "</ChapterString>\n";
        out << "        <ChapterLanguage>" << xmlEscape(lang) << "</ChapterLanguage>\n";
        out << "      </ChapterDisplay>\n";
        out << "    </ChapterAtom>\n";
    }

    out << "  </EditionEntry>\n</Chapters>\n";
    out.flush();

    if (!out.good()) {
        errorMsg = "I/O error while writing '" + filename + "'.";
        return false;
    }
    return true;
}

bool loadXML(std::vector<Chapter>& chapters,
             const std::string& filename,
             int& loadedCount,
             std::string& errorMsg) {
    loadedCount = 0;

    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        errorMsg = "Could not open '" + filename + "' to load chapters.";
        return false;
    }

    std::string rawContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        errorMsg = "Failed while reading '" + filename + "'.";
        return false;
    }

    const std::string content = stripXmlComments(rawContent);
    size_t pos = 0;

    while (pos < content.size()) {
        size_t atomStart = 0;
        size_t bodyStart = 0;
        bool selfClosing = false;
        if (!findTagOpen(content, "ChapterAtom", pos, content.size(), atomStart, bodyStart, selfClosing)) {
            break;
        }
        if (selfClosing) {
            pos = bodyStart;
            continue;
        }

        size_t atomCloseStart = 0;
        if (!findMatchingChapterAtomClose(content, bodyStart, atomCloseStart)) {
            break;
        }

        const std::string directAtom = extractDirectAtomContent(content, bodyStart, atomCloseStart);

        std::string timeText;
        if (extractTagText(directAtom, "ChapterTimeStart", timeText)) {
            double parsedTime = 0.0;
            if (parseTime(timeText, parsedTime)) {
                std::string title;
                if (extractTagText(directAtom, "ChapterString", title)) {
                    title = trimAscii(xmlUnescape(title));
                    if (isGenericChapterTitle(title)) {
                        title.clear();
                    }
                }

                std::string lang;
                if (extractTagText(directAtom, "ChapterLanguage", lang)) {
                    lang = trimAscii(xmlUnescape(lang));
                }

                chapters.push_back({parsedTime, title, lang});
                ++loadedCount;
            }
        }

        // Advance past opening tag only so nested <ChapterAtom> children are also parsed.
        pos = bodyStart;
    }

    sanitizeChapters(chapters);
    return true;
}

std::string escapeMpvOsdText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\') {
            out.append("\\\\");
        } else if (c == '$') {
            out.append("$$");
        } else if (c == '\r' || c == '\n') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void ChapterManager::pushUndoState() {
    undoStack_.push_back(chapters_);
    if (undoStack_.size() > kMaxUndoHistory) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void ChapterManager::replaceAll(std::vector<Chapter> next, bool recordUndo) {
    sanitizeChapters(next);
    if (recordUndo) {
        pushUndoState();
    } else {
        undoStack_.clear();
        redoStack_.clear();
    }
    chapters_ = std::move(next);
}

std::string ChapterManager::displayTitle(size_t index) const {
    if (index >= chapters_.size()) return "";
    if (!chapters_[index].title.empty()) {
        return chapters_[index].title;
    }
    return "Chapter " + std::to_string(index + 1);
}

int ChapterManager::findActiveChapterIndex(double timeSec) const {
    int active = -1;
    for (size_t i = 0; i < chapters_.size(); ++i) {
        if (chapters_[i].time <= timeSec + 0.05) {
            active = static_cast<int>(i);
        } else {
            break;
        }
    }
    return active;
}

int ChapterManager::findNearestChapterIndex(double timeSec, double maxDistance) const {
    if (chapters_.empty()) return -1;
    int bestIdx = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < chapters_.size(); ++i) {
        double d = std::abs(chapters_[i].time - timeSec);
        if (d < bestDist) {
            bestDist = d;
            bestIdx = static_cast<int>(i);
        }
    }
    if (bestIdx >= 0 && bestDist < maxDistance) {
        return bestIdx;
    }
    return -1;
}

void ChapterManager::addChapter(double timeSec, const std::string& title, const std::string& lang) {
    pushUndoState();
    std::string cleanTitle = isGenericChapterTitle(title) ? "" : title;
    chapters_.push_back({clampNonNegativeSeconds(timeSec), cleanTitle, lang});
    sanitizeChapters(chapters_);
}

bool ChapterManager::removeNearestChapter(double timeSec, double tolerance, Chapter& removedOut) {
    int idx = findNearestChapterIndex(timeSec, tolerance);
    if (idx < 0) return false;
    pushUndoState();
    removedOut = chapters_[static_cast<size_t>(idx)];
    chapters_.erase(chapters_.begin() + idx);
    return true;
}

bool ChapterManager::moveChapter(int index, double newTime) {
    if (index < 0 || static_cast<size_t>(index) >= chapters_.size()) {
        return false;
    }
    pushUndoState();
    chapters_[static_cast<size_t>(index)].time = clampNonNegativeSeconds(newTime);
    sanitizeChapters(chapters_);
    return true;
}

bool ChapterManager::renameChapter(int index, const std::string& newTitle) {
    if (index < 0 || static_cast<size_t>(index) >= chapters_.size()) {
        return false;
    }
    const std::string trimmed = trimAscii(newTitle);
    const std::string normalized = isGenericChapterTitle(trimmed) ? "" : trimmed;
    if (chapters_[static_cast<size_t>(index)].title == normalized) {
        return true;
    }
    pushUndoState();
    chapters_[static_cast<size_t>(index)].title = normalized;
    return true;
}

bool ChapterManager::mergeFromXML(const std::string& filename, int& loadedCount, std::string& errorMsg) {
    std::vector<Chapter> merged = chapters_;
    if (!loadXML(merged, filename, loadedCount, errorMsg)) {
        return false;
    }
    if (loadedCount > 0) {
        pushUndoState();
        chapters_ = std::move(merged);
    }
    return true;
}

bool ChapterManager::undo() {
    if (undoStack_.empty()) return false;
    redoStack_.push_back(std::move(chapters_));
    chapters_ = std::move(undoStack_.back());
    undoStack_.pop_back();
    return true;
}

bool ChapterManager::redo() {
    if (redoStack_.empty()) return false;
    undoStack_.push_back(std::move(chapters_));
    chapters_ = std::move(redoStack_.back());
    redoStack_.pop_back();
    return true;
}
