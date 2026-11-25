#include "chapters.hpp"
#include "process.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond "\n";    \
            ++g_failures;                                                                \
        }                                                                                \
    } while (false)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        auto _a = (actual);                                                              \
        auto _e = (expected);                                                            \
        if (!(_a == _e)) {                                                               \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #actual        \
                      << " == " << _a << " (expected " << _e << ")\n";                   \
            ++g_failures;                                                                \
        }                                                                                \
    } while (false)

void testTimeFormattingAndParsing() {
    CHECK_EQ(formatTimeXML(0.0), std::string("00:00:00.000000000"));
    CHECK_EQ(formatTimeDisplay(0.0), std::string("00:00:00.000"));

    // Rounding boundary checks: must never emit :60.000
    CHECK_EQ(formatTimeDisplay(59.9999), std::string("00:01:00.000"));
    CHECK_EQ(formatTimeDisplay(3599.9999), std::string("01:00:00.000"));
    CHECK_EQ(formatTimeXML(59.9999999999), std::string("00:01:00.000000000"));

    // Multi-hour and clamping checks
    CHECK_EQ(formatTimeXML(3661.25), std::string("01:01:01.250000000"));
    CHECK_EQ(formatTimeXML(-5.0), std::string("00:00:00.000000000"));
    CHECK_EQ(formatTimeDisplay(std::numeric_limits<double>::quiet_NaN()), std::string("00:00:00.000"));
    CHECK_EQ(formatTimeDisplay(std::numeric_limits<double>::infinity()), std::string("00:00:00.000"));

    double secs = 0.0;
    CHECK(parseTime("00:01:30.500", secs));
    CHECK(std::abs(secs - 90.5) < 1e-9);

    CHECK(parseTime("  01:02:03.123456789 \n", secs));
    CHECK(std::abs(secs - 3723.123456789) < 1e-9);

    CHECK(parseTime("105:00:00", secs));
    CHECK(std::abs(secs - 378000.0) < 1e-9);

    // Malformed strings must fail cleanly
    CHECK(!parseTime("", secs));
    CHECK(!parseTime("invalid", secs));
    CHECK(!parseTime("12:34", secs));
    CHECK(!parseTime("00:60:00.000", secs));
    CHECK(!parseTime("00:00:60.000", secs));
    CHECK(!parseTime("-01:00:00.000", secs));
    CHECK(!parseTime("00:00:12.", secs));
    CHECK(!parseTime("00:00:12.abc", secs));
    CHECK(!parseTime("00:00:01:02", secs));
}

void testXmlEscapingAndTitles() {
    const std::string raw = "Tom & Jerry: <Episode \"1\"> 'Pilot'";
    const std::string escaped = xmlEscape(raw);
    CHECK_EQ(escaped, std::string("Tom &amp; Jerry: &lt;Episode &quot;1&quot;&gt; &apos;Pilot&apos;"));
    CHECK_EQ(xmlUnescape(escaped), raw);

    // Single-pass unescaping must NOT double-unescape &amp;lt; into <
    CHECK_EQ(xmlUnescape("&amp;lt;tag&amp;gt;"), std::string("&lt;tag&gt;"));

    // Decimal & hex numeric character references (including UTF-8 and emoji)
    CHECK_EQ(xmlUnescape("Caf&#233; &amp; M&#xFC;nchen &#x1F3AC;"), std::string("Café & München \xF0\x9F\x8E\xAC"));

    // Generic chapter title detection
    CHECK(isGenericChapterTitle("Chapter 1"));
    CHECK(isGenericChapterTitle("Chapter 42"));
    CHECK(!isGenericChapterTitle("Chapter 1: Intro"));
    CHECK(!isGenericChapterTitle("Chapter One"));
    CHECK(!isGenericChapterTitle("Chapter "));
    CHECK(!isGenericChapterTitle("Chapter"));
    CHECK(!isGenericChapterTitle(""));
}

void testSanitizeAndXmlRoundTrip() {
    std::vector<Chapter> chaps = {
        {20.0, "Outro", ""},
        {10.05, "Boss Fight", "jpn"},
        {10.0, "", ""},
        {0.0, "Chapter 1: Awakening", "eng"}
    };
    sanitizeChapters(chaps);
    CHECK_EQ(chaps.size(), size_t(3));
    CHECK_EQ(chaps[0].title, std::string("Chapter 1: Awakening"));
    CHECK_EQ(chaps[1].title, std::string("Boss Fight"));
    CHECK_EQ(chaps[1].language, std::string("jpn"));
    CHECK_EQ(chaps[2].title, std::string("Outro"));

    const std::filesystem::path tmpXml =
        std::filesystem::temp_directory_path() / "chapter_editor_test_roundtrip.xml";

    std::string err;
    CHECK(exportXML(chaps, tmpXml.string(), "eng", err));

    std::vector<Chapter> loaded;
    int loadedCount = 0;
    CHECK(loadXML(loaded, tmpXml.string(), loadedCount, err));
    CHECK_EQ(loadedCount, 3);
    CHECK_EQ(loaded.size(), size_t(3));
    CHECK_EQ(loaded[0].title, std::string("Chapter 1: Awakening"));
    CHECK_EQ(loaded[1].title, std::string("Boss Fight"));
    CHECK_EQ(loaded[1].language, std::string("jpn"));
    CHECK_EQ(loaded[2].title, std::string("Outro"));
    CHECK_EQ(loaded[2].language, std::string("eng"));

    // Test XML comments, attributes, and nested <ChapterAtom> handling
    const std::filesystem::path nestedXml =
        std::filesystem::temp_directory_path() / "chapter_editor_test_nested.xml";
    {
        std::ofstream out(nestedXml);
        out << "<?xml version=\"1.0\"?>\n"
            << "<Chapters><EditionEntry>\n"
            << "  <!-- <ChapterAtom><ChapterTimeStart>00:00:05.000</ChapterTimeStart></ChapterAtom> -->\n"
            << "  <ChapterAtom id=\"outer\">\n"
            << "    <ChapterTimeStart>00:00:10.000</ChapterTimeStart>\n"
            << "    <ChapterAtom>\n"
            << "      <ChapterTimeStart>00:00:15.000</ChapterTimeStart>\n"
            << "      <ChapterDisplay><ChapterString>Sub &amp; Part</ChapterString></ChapterDisplay>\n"
            << "    </ChapterAtom>\n"
            << "    <ChapterDisplay><ChapterString>Outer Main</ChapterString></ChapterDisplay>\n"
            << "  </ChapterAtom>\n"
            << "  <ChapterAtom>\n"
            << "    <ChapterTimeStart>00:00:25.000</ChapterTimeStart>\n"
            << "    <ChapterDisplay><ChapterString>Chapter 3</ChapterString></ChapterDisplay>\n"
            << "  </ChapterAtom>\n"
            << "  <ChapterAtom>\n"
            << "    <ChapterTimeStart>unclosed\n";
    }
    std::vector<Chapter> nestedLoaded;
    int nestedCount = 0;
    CHECK(loadXML(nestedLoaded, nestedXml.string(), nestedCount, err));
    CHECK_EQ(nestedCount, 3);
    CHECK_EQ(nestedLoaded.size(), size_t(3));
    CHECK_EQ(nestedLoaded[0].title, std::string("Outer Main"));
    CHECK_EQ(nestedLoaded[1].title, std::string("Sub & Part"));
    CHECK_EQ(nestedLoaded[2].title, std::string("")); // Generic "Chapter 3" cleared

    std::error_code ec;
    std::filesystem::remove(tmpXml, ec);
    std::filesystem::remove(nestedXml, ec);
}

void testChapterManagerAndOsdHelpers() {
    ChapterManager mgr;
    mgr.addChapter(10.0, "Intro");
    mgr.addChapter(30.0, "Middle");
    CHECK_EQ(mgr.chapters().size(), size_t(2));
    CHECK_EQ(mgr.findActiveChapterIndex(5.0), -1);
    CHECK_EQ(mgr.findActiveChapterIndex(10.0), 0);
    CHECK_EQ(mgr.findActiveChapterIndex(29.9), 0);
    CHECK_EQ(mgr.findActiveChapterIndex(30.0), 1);

    CHECK(mgr.moveChapter(1, 25.0));
    CHECK(std::abs(mgr.chapters()[1].time - 25.0) < 1e-9);
    CHECK_EQ(mgr.chapters()[1].title, std::string("Middle"));

    CHECK(mgr.renameChapter(0, "Opening Scene"));
    CHECK_EQ(mgr.chapters()[0].title, std::string("Opening Scene"));

    Chapter removed;
    CHECK(mgr.removeNearestChapter(25.2, 0.5, removed));
    CHECK_EQ(removed.title, std::string("Middle"));
    CHECK_EQ(mgr.chapters().size(), size_t(1));

    // Undo/redo chain
    CHECK(mgr.canUndo());
    CHECK(mgr.undo());
    CHECK_EQ(mgr.chapters().size(), size_t(2));
    CHECK_EQ(mgr.chapters()[1].title, std::string("Middle"));
    CHECK(mgr.canRedo());
    CHECK(mgr.redo());
    CHECK_EQ(mgr.chapters().size(), size_t(1));

    // UTF-8 pop and OSD escaping
    std::string utf = "A\xC3\xA4";
    popLastUtf8Codepoint(utf);
    CHECK_EQ(utf, std::string("A"));

    CHECK_EQ(escapeMpvOsdText("a\\b ${foo}\nline2"), std::string("a\\\\b $${foo} line2"));
}

void testProcessExecution() {
    ProcessResult missing = runProcess({"definitely_nonexistent_binary_999999"}, true);
    CHECK(!missing.launched);
    CHECK_EQ(missing.exitCode, -1);

#ifndef _WIN32
    // Verify arguments containing shell metacharacters are passed verbatim without shell expansion.
    const std::filesystem::path probeFile =
        std::filesystem::temp_directory_path() / "chapter_editor_process_probe.txt";
    const std::string dangerousArg = "$(echo PWNED) \"$HOME\" 'single'";
    ProcessResult echoRes = runProcess(
        {"/bin/sh", "-c", "printf '%s' \"$1\" > \"$2\"", "sh", dangerousArg, probeFile.string()},
        true);
    CHECK(echoRes.launched);
    CHECK_EQ(echoRes.exitCode, 0);

    std::ifstream in(probeFile, std::ios::binary);
    std::string written((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_EQ(written, dangerousArg);

    std::error_code ec;
    std::filesystem::remove(probeFile, ec);
#endif
}

} // namespace

int main() {
    testTimeFormattingAndParsing();
    testXmlEscapingAndTitles();
    testSanitizeAndXmlRoundTrip();
    testChapterManagerAndOsdHelpers();
    testProcessExecution();

    if (g_failures == 0) {
        std::cout << "All chapter-editor unit tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " assertion(s) failed.\n";
    return 1;
}
