import sys

with open("src/main.cpp", "r") as f:
    content = f.read()

# Remove static osd-status-msg assignment
content = content.replace(
    'mpv_set_option_string(ctx, "osd-status-msg", (baseMsg + defaultOsd).c_str());',
    '// osd-status-msg is set dynamically in the loop'
)

# Remove toggle-help manual set
content = content.replace(
    '''                if (action == "toggle-help") {
                    showHelp = !showHelp;
                    // Dynamically update the osd-status-msg property during runtime
                    std::string newMsg = baseMsg + (showHelp ? helpOsd : defaultOsd);
                    mpv_set_property_string(ctx, "osd-status-msg", newMsg.c_str());
                }''',
    '''                if (action == "toggle-help") {
                    showHelp = !showHelp;
                }
                else if (action == "quit-app") {
                    if (unsavedChanges && !confirmQuit) {
                        std::cout << "\\nWARNING: You have unsaved chapter edits!\\n";
                        std::cout << "Press 'S' or 'X' to save them, or press 'Q' again to quit without saving.\\n";
                        confirmQuit = true;
                    } else {
                        running = false;
                    }
                }'''
)

# Add the dynamic update to the main loop
content = content.replace(
    '''        // mpv does not have a native option to keep the OSD progress bar permanently visible.
        // We must periodically trigger 'show-progress' to keep it on screen.
        const char* progCmd[] = {"show-progress", NULL};
        mpv_command_async(ctx, 0, progCmd);''',
    '''        // Dynamically update the OSD text based on state
        std::string currentMsg = baseMsg;
        if (confirmQuit) {
            currentMsg += "\\n\\n{\\\\c&H0000FF&}WARNING: Unsaved edits! Press Q again to quit, or S/X to save.{\\\\c&HFFFFFF&}";
        } else if (showHelp) {
            currentMsg += helpOsd;
        } else {
            currentMsg += defaultOsd;
        }
        mpv_set_property_string(ctx, "osd-status-msg", currentMsg.c_str());
        
        // mpv does not have a native option to keep the OSD progress bar permanently visible.
        // We must periodically trigger 'show-progress' to keep it on screen.
        const char* progCmd[] = {"show-progress", NULL};
        mpv_command_async(ctx, 0, progCmd);'''
)

with open("src/main.cpp", "w") as f:
    f.write(content)
