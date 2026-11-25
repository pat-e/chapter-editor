cmake_minimum_required(VERSION 3.16)

project(MKVChapterEditor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_library(chapter_core STATIC
    src/chapters.cpp
    src/process.cpp
)
target_include_directories(chapter_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(chapter_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
)

add_executable(chapter-editor src/main.cpp)
target_link_libraries(chapter-editor PRIVATE
    chapter_core
    PkgConfig::MPV
    Threads::Threads
)
target_compile_options(chapter-editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
)

install(TARGETS chapter-editor RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_chapters tests/test_chapters.cpp)
    target_link_libraries(test_chapters PRIVATE chapter_core)
    target_compile_options(test_chapters PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME chapter_unit_tests COMMAND test_chapters)
endif()
