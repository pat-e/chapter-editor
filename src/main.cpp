#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <mpv/client.h>

// Converts seconds to HH:MM:SS.nnnnnnnnn format required by Matroska XML
std::string formatTime(double totalSeconds) {
    int hours = static_cast<int>(totalSeconds) / 3600;
    int mins = (static_cast<int>(totalSeconds) % 3600) / 60;
    double secs = totalSeconds - (hours * 3600) - (mins * 60);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%06.3f", hours, mins, secs);
    return std::string(buffer);
}

// Converts HH:MM:SS.nnn back to seconds
double parseTime(const std::string& timeStr) {
    int h = 0, m = 0;
    double s = 0.0;
    if (sscanf(timeStr.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
        return (h * 3600.0) + (m * 60.0) + s;
    }
    return 0.0;
}

// Deduplicates and sorts the chapter markers
void sanitizeChapters(std::vector<double>& chapters) {
    std::sort(chapters.begin(), chapters.end());
    // Remove duplicates that are within 0.1 seconds of each other
    auto it = std::unique(chapters.begin(), chapters.end(), 
        [](double a, double b) { return std::abs(a - b) < 0.1; });
    chapters.erase(it, chapters.end());
}

void exportXML(const std::vector<double>& chapters, const std::string& filename) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to create " << filename << std::endl;
        return;
    }

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<Chapters>\n  <EditionEntry>\n    <EditionFlagDefault>1</EditionFlagDefault>\n    <EditionUID>1</EditionUID>\n";
    
    for (size_t i = 0; i < chapters.size(); ++i) {
        out << "    <ChapterAtom>\n";
        out << "      <ChapterTimeStart>" << formatTime(chapters[i]) << "</ChapterTimeStart>\n";
        out << "      <ChapterDisplay>\n";
        out << "        <ChapterString>Chapter " << (i + 1) << "</ChapterString>\n";
        out << "        <ChapterLanguage>eng</ChapterLanguage>\n";
        out << "      </ChapterDisplay>\n";
        out << "    </ChapterAtom>\n";
    }
    
    out << "  </EditionEntry>\n</Chapters>\n";
    out.close();
    std::cout << "Successfully exported " << chapters.size() << " chapters to " << filename << std::endl;
}

void loadXML(std::vector<double>& chapters, const std::string& filename) {
    std::ifstream in(filename);
    if (!in) {
        std::cout << "Could not open " << filename << " to load chapters.\n";
        return;
    }
    
    std::string line;
    std::string tag = "<ChapterTimeStart>";
    std::string endTag = "</ChapterTimeStart>";
    int loadedCount = 0;
    
    while (std::getline(in, line)) {
        size_t start = line.find(tag);
        if (start != std::string::npos) {
            start += tag.length();
            size_t end = line.find(endTag, start);
            if (end != std::string::npos) {
                std::string timeStr = line.substr(start, end - start);
                chapters.push_back(parseTime(timeStr));
                loadedCount++;
            }
        }
    }
    sanitizeChapters(chapters);
    std::cout << "Successfully loaded and merged " << loadedCount << " chapters from " << filename << ".\n";
}

// Sync internal chapter list to mpv so the OSD progress bar accurately renders the chapter ticks
void syncChaptersToMpv(mpv_handle *ctx, const std::vector<double>& chapters) {
    mpv_node_list array;
    array.num = chapters.size();
    array.keys = NULL;
    
    std::vector<mpv_node> map_nodes(chapters.size());
    std::vector<mpv_node_list> map_lists(chapters.size());
    std::vector<mpv_node> title_nodes(chapters.size());
    std::vector<mpv_node> time_nodes(chapters.size());
    std::vector<std::string> title_strs(chapters.size());
    
    std::vector<char*> map_keys(chapters.size() * 2);
    std::vector<mpv_node> map_values(chapters.size() * 2);

    for (size_t i = 0; i < chapters.size(); ++i) {
        title_strs[i] = "Chapter " + std::to_string(i + 1);
        
        title_nodes[i].format = MPV_FORMAT_STRING;
        title_nodes[i].u.string = (char*)title_strs[i].c_str();
        
        time_nodes[i].format = MPV_FORMAT_DOUBLE;
        time_nodes[i].u.double_ = chapters[i];
        
        size_t idx = i * 2;
        map_keys[idx] = (char*)"title";
        map_keys[idx + 1] = (char*)"time";
        
        map_values[idx] = title_nodes[i];
        map_values[idx + 1] = time_nodes[i];
        
        map_lists[i].num = 2;
        map_lists[i].keys = &map_keys[idx];
        map_lists[i].values = &map_values[idx];
        
        map_nodes[i].format = MPV_FORMAT_NODE_MAP;
        map_nodes[i].u.list = &map_lists[i];
    }
    
    array.values = map_nodes.empty() ? NULL : map_nodes.data();
    
    mpv_node root;
    root.format = MPV_FORMAT_NODE_ARRAY;
    root.u.list = &array;
    
    mpv_set_property(ctx, "chapter-list", MPV_FORMAT_NODE, &root);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Please open with a video file parameter.\n";
        std::cerr << "Usage: " << argv[0] << " <video_file.mkv>\n";
        return 1;
    }

    std::string inputFile = argv[1];
    
    // Quick check if the file exists and is readable
    std::ifstream fileCheck(inputFile);
    if (!fileCheck.good()) {
        std::cerr << "Could not open file " << inputFile << ", please check if file is readable and a MKV file.\n";
        return 1;
    }
    fileCheck.close();
    std::vector<double> chapterMarkers;

    // Create a temporary input.conf file to force our custom keybindings into mpv
    std::string confFile = "chapter_bindings.conf";
    std::ofstream conf(confFile);
    conf << "RIGHT frame-step\n";
    conf << "LEFT frame-back-step\n";
    conf << "UP seek 1 keyframes\n";
    conf << "DOWN seek -1 keyframes\n";
    conf << "Alt+RIGHT script-message jump-frames 100 exact\n";
    conf << "Shift+RIGHT script-message jump-frames 1000 exact\n";
    conf << "Alt+LEFT script-message jump-frames -100 exact\n";
    conf << "Shift+LEFT script-message jump-frames -1000 exact\n";
    conf << "Alt+UP script-message jump-frames 100 keyframes\n";
    conf << "Shift+UP script-message jump-frames 1000 keyframes\n";
    conf << "Alt+DOWN script-message jump-frames -100 keyframes\n";
    conf << "Shift+DOWN script-message jump-frames -1000 keyframes\n";
    conf << "SPACE cycle pause\n";
    conf << "c script-message add-chapter\n";
    conf << "d script-message remove-chapter\n";
    conf << "DEL script-message remove-chapter\n";
    conf << "PGUP script-message next-chapter\n";
    conf << "PGDWN script-message prev-chapter\n";
    conf << "s script-message save-chapters\n";
    conf << "l script-message load-chapters\n";
    conf << "x script-message export-and-remux\n";
    conf << "h script-message toggle-help\n";
    conf << "? script-message toggle-help\n";
    conf << "q script-message quit-app\n";
    conf.close();

    mpv_handle *ctx = mpv_create();
    if (!ctx) {
        std::cerr << "Fatal: Failed to create mpv context." << std::endl;
        return 1;
    }

    // Configure mpv for our specific, stripped-down visual needs
    mpv_set_option_string(ctx, "input-conf", confFile.c_str());
    mpv_set_option_string(ctx, "pause", "yes");        // Start paused
    mpv_set_option_string(ctx, "aid", "no");           // Disable audio track
    mpv_set_option_string(ctx, "sid", "no");           // Disable subtitles
    mpv_set_option_string(ctx, "osd-level", "3");      // Always show OSD status msg
    mpv_set_option_string(ctx, "osd-font-size", "25"); // Better visibility
    mpv_set_option_string(ctx, "osd-align-x", "left"); // Align text top-left
    mpv_set_option_string(ctx, "osd-align-y", "top");
    mpv_set_option_string(ctx, "osd-margin-x", "10");
    mpv_set_option_string(ctx, "osd-margin-y", "10");
    std::string baseMsg = "Frame: ${estimated-frame-number} / ${estimated-frame-count}\\nTime:  ${playback-time} / ${duration}";
    std::string defaultOsd = "\\n\\nPress 'h' for help";
    std::string helpOsd = "\\n\\nMKV Chapter Editor Help\\n"
                          "[h] / [?] : Toggle Help\\n"
                          "[SPACE] : Play / Pause\\n"
                          "[LEFT] / [RIGHT] : Step 1 Frame\\n"
                          " + Alt: 100 Frames  |  + Shift: 1000 Frames\\n"
                          "[UP] / [DOWN] : Jump 1 sec Keyframes\\n"
                          " + Alt: 100 Frames (KF) | + Shift: 1000 Frames (KF)\\n"
                          "[C] : Add Chapter\\n"
                          "[D] / [DEL] : Remove Chapter\\n"
                          "[PGUP] / [PGDN] : Jump Chapters\\n"
                          "[S] : Save XML only\\n"
                          "[L] : Load chapters.xml\\n"
                          "[X] : Save & Remux\\n"
                          "[Q] : Quit without saving";
                          
    // osd-status-msg is set dynamically in the loop
    mpv_set_option_string(ctx, "keep-open", "yes");    // Don't quit when video ends
    mpv_set_option_string(ctx, "hwdec", "auto-safe");  // Enable safe hardware decoding
    
    if (mpv_initialize(ctx) < 0) {
        std::cerr << "Fatal: mpv initialization failed." << std::endl;
        return 1;
    }

    std::cout << "Starting Chapter Editor. Loading: " << inputFile << "\n";
    std::cout << "Controls:\n";
    std::cout << " - SPACE: Play / Pause\n";
    std::cout << " - LEFT/RIGHT: Step 1 Frame\n";
    std::cout << "    + Alt: Jump 100 Frames  |  + Shift: Jump 1000 Frames\n";
    std::cout << " - UP/DOWN: Jump 1 sec Keyframes (I-frames)\n";
    std::cout << "    + Alt: Jump 100 Frames (KF) | + Shift: Jump 1000 Frames (KF)\n";
    std::cout << " - C: Add Chapter  |  D or DEL: Remove Chapter\n";
    std::cout << " - PG-UP/PG-DOWN: Jump to next/prev set chapter\n";
    std::cout << " - S: Save chapters to XML only\n";
    std::cout << " - L: Load existing chapters.xml\n";
    std::cout << " - X: Save chapters and remux video\n";
    std::cout << " - H or ?: Toggle on-screen help\n";
    std::cout << " - Q: Quit without saving\n";

    // Tell mpv to load the file
    const char *cmd[] = {"loadfile", inputFile.c_str(), NULL};
    mpv_command(ctx, cmd);

    bool running = true;
    bool showHelp = false;
    bool unsavedChanges = false;
    bool confirmQuit = false;
    
    while (running) {
        // Wait for an event from mpv (blocks until an event occurs or 0.5s timeout)
        mpv_event *event = mpv_wait_event(ctx, 0.5);
        
        // Dynamically update the OSD text based on state
        std::string currentMsg = baseMsg;
        if (confirmQuit) {
            currentMsg += "\n\n{\\c&H0000FF&}WARNING: Unsaved edits! Press Q again to quit, or S/X to save.{\\c&HFFFFFF&}";
        } else if (showHelp) {
            currentMsg += helpOsd;
        } else {
            currentMsg += defaultOsd;
        }
        mpv_set_property_string(ctx, "osd-status-msg", currentMsg.c_str());
        
        // mpv does not have a native option to keep the OSD progress bar permanently visible.
        // We must periodically trigger 'show-progress' to keep it on screen.
        const char* progCmd[] = {"show-progress", NULL};
        mpv_command_async(ctx, 0, progCmd);
        
        if (event->event_id == MPV_EVENT_SHUTDOWN) {
            running = false;
        }
        else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            // Auto-load internal MKV chapters if they exist
            double count = 0;
            if (mpv_get_property(ctx, "chapter-list/count", MPV_FORMAT_DOUBLE, &count) >= 0) {
                int chapters_found = 0;
                for (int i = 0; i < static_cast<int>(count); ++i) {
                    std::string prop = "chapter-list/" + std::to_string(i) + "/time";
                    double time_sec = 0;
                    if (mpv_get_property(ctx, prop.c_str(), MPV_FORMAT_DOUBLE, &time_sec) >= 0) {
                        chapterMarkers.push_back(time_sec);
                        chapters_found++;
                    }
                }
                if (chapters_found > 0) {
                    sanitizeChapters(chapterMarkers);
                    syncChaptersToMpv(ctx, chapterMarkers);
                    std::cout << "Auto-loaded " << chapters_found << " existing chapters from the source MKV file.\n";
                }
            }
        }
        else if (event->event_id == MPV_EVENT_CLIENT_MESSAGE) {
            // This catches the 'script-message' events defined in our input.conf
            mpv_event_client_message *msg = (mpv_event_client_message *)event->data;
            if (msg->num_args > 0) {
                std::string action = msg->args[0];
                
                // If they do anything else, cancel the quit confirmation
                if (action != "quit-app") {
                    confirmQuit = false;
                }
                
                if (action == "toggle-help") {
                    showHelp = !showHelp;
                }
                else if (action == "jump-frames") {
                    if (msg->num_args >= 3) {
                        int frames = std::stoi(msg->args[1]);
                        std::string mode = msg->args[2];
                        
                        double fps = 24.0;
                        mpv_get_property(ctx, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps);
                        if (fps <= 0.0) {
                            mpv_get_property(ctx, "container-fps", MPV_FORMAT_DOUBLE, &fps);
                        }
                        if (fps <= 0.0) fps = 24.0; // fallback
                        
                        double jump_sec = frames / fps;
                        std::string jump_str = std::to_string(jump_sec);
                        const char* cmdArgs[] = {"seek", jump_str.c_str(), "relative", mode.c_str(), NULL};
                        mpv_command(ctx, cmdArgs);
                    }
                }
                else if (action == "quit-app") {
                    if (unsavedChanges && !confirmQuit) {
                        std::cout << "\nWARNING: You have unsaved chapter edits!\n";
                        std::cout << "Press 'S' or 'X' to save them, or press 'Q' again to quit without saving.\n";
                        confirmQuit = true;
                    } else {
                        running = false;
                    }
                }
                else if (action == "add-chapter") {
                    double time_sec = 0.0;
                    mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &time_sec);
                    chapterMarkers.push_back(time_sec);
                    sanitizeChapters(chapterMarkers);
                    syncChaptersToMpv(ctx, chapterMarkers);
                    unsavedChanges = true;
                    confirmQuit = false;
                    std::cout << "Chapter added at: " << formatTime(time_sec) << " (Total: " << chapterMarkers.size() << ")\n";
                }
                else if (action == "remove-chapter") {
                    double time_sec = 0.0;
                    mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &time_sec);
                    if (chapterMarkers.empty()) continue;
                    
                    // Find closest chapter within a 0.5s tolerance to allow easy removal
                    auto it = std::min_element(chapterMarkers.begin(), chapterMarkers.end(),
                        [time_sec](double a, double b) {
                            return std::abs(a - time_sec) < std::abs(b - time_sec);
                        });
                        
                    if (it != chapterMarkers.end() && std::abs(*it - time_sec) < 0.5) {
                        std::cout << "Chapter removed at: " << formatTime(*it) << "\n";
                        chapterMarkers.erase(it);
                        syncChaptersToMpv(ctx, chapterMarkers);
                        unsavedChanges = true;
                        confirmQuit = false;
                    } else {
                        std::cout << "No chapter found near current position to remove.\n";
                    }
                }
                else if (action == "next-chapter") {
                    double time_sec = 0.0;
                    mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &time_sec);
                    auto it = std::upper_bound(chapterMarkers.begin(), chapterMarkers.end(), time_sec + 0.1);
                    if (it != chapterMarkers.end()) {
                        std::string targetTime = std::to_string(*it);
                        const char* cmdArgs[] = {"seek", targetTime.c_str(), "absolute", "exact", NULL};
                        mpv_command(ctx, cmdArgs);
                    }
                }
                else if (action == "prev-chapter") {
                    double time_sec = 0.0;
                    mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &time_sec);
                    // Iterate backwards to find the nearest previous chapter
                    for (auto rit = chapterMarkers.rbegin(); rit != chapterMarkers.rend(); ++rit) {
                        if (*rit < time_sec - 0.1) {
                            std::string targetTime = std::to_string(*rit);
                            const char* cmdArgs[] = {"seek", targetTime.c_str(), "absolute", "exact", NULL};
                            mpv_command(ctx, cmdArgs);
                            break;
                        }
                    }
                }
                else if (action == "save-chapters") {
                    if (chapterMarkers.empty()) {
                        std::cout << "No chapters present. Nothing to save.\n";
                        continue;
                    }
                    
                    sanitizeChapters(chapterMarkers);
                    std::string xmlFile = "chapters.xml";
                    exportXML(chapterMarkers, xmlFile);
                    std::cout << "Chapters safely written to " << xmlFile << " (Remux bypassed).\n";
                    unsavedChanges = false;
                }
                else if (action == "load-chapters") {
                    std::string xmlFile = "chapters.xml";
                    loadXML(chapterMarkers, xmlFile);
                    syncChaptersToMpv(ctx, chapterMarkers);
                }
                else if (action == "export-and-remux") {
                    if (chapterMarkers.empty()) {
                        std::cout << "No chapters present. Ignoring remux command.\n";
                        continue;
                    }
                    
                    sanitizeChapters(chapterMarkers);
                    
                    std::string xmlFile = "chapters.xml";
                    exportXML(chapterMarkers, xmlFile);
                    
                    std::string outputFile = "chaptered_output.mkv";
                    std::string mkvmergeCmd = "mkvmerge -o \"" + outputFile + "\" --chapters \"" + xmlFile + "\" \"" + inputFile + "\"";
                    
                    std::cout << "Starting remux process...\n";
                    std::cout << "Executing: " << mkvmergeCmd << "\n";
                    
                    int sysResult = std::system(mkvmergeCmd.c_str());
                    if (sysResult == 0) {
                        std::cout << "SUCCESS! New file created: " << outputFile << "\n";
                        unsavedChanges = false;
                    } else {
                        std::cerr << "ERROR: mkvmerge failed. Is MKVToolNix installed and in your PATH?\n";
                    }
                }
            }
        }
    }

    // Cleanup
    mpv_terminate_destroy(ctx);
    std::remove(confFile.c_str()); // Remove temporary config file
    
    std::cout << "Exiting Chapter Editor cleanly." << std::endl;
    return 0;
}