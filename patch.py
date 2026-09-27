import sys

with open("src/main.cpp", "r") as f:
    content = f.read()

content = content.replace(
    'syncChaptersToMpv(ctx, chapterMarkers);\n                    std::cout << "Chapter added',
    'syncChaptersToMpv(ctx, chapterMarkers);\n                    unsavedChanges = true;\n                    confirmQuit = false;\n                    std::cout << "Chapter added'
)

content = content.replace(
    'syncChaptersToMpv(ctx, chapterMarkers);\n                    } else {',
    'syncChaptersToMpv(ctx, chapterMarkers);\n                        unsavedChanges = true;\n                        confirmQuit = false;\n                    } else {'
)

content = content.replace(
    'std::cout << "Chapters safely written to " << xmlFile << " (Remux bypassed).\\n";',
    'std::cout << "Chapters safely written to " << xmlFile << " (Remux bypassed).\\n";\n                    unsavedChanges = false;'
)

content = content.replace(
    'std::cout << "SUCCESS! New file created: " << outputFile << "\\n";',
    'std::cout << "SUCCESS! New file created: " << outputFile << "\\n";\n                        unsavedChanges = false;'
)

with open("src/main.cpp", "w") as f:
    f.write(content)
