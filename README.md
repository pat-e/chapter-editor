# MKV Chapter Editor

MKV Chapter Editor is an ultra-lightweight, specialized tool designed for one purpose: precisely scrubbing through video files to define, edit, and export Matroska chapter markers without altering the original file (or optionally patching chapters in-place when requested) and without requiring complex video editing software.

Under the hood, it uses the highly robust `libmpv` engine for fast visual rendering, audio playback for seeking cues, and smart keyframe seeking, intentionally disabling subtitles to maximize performance. It utilizes safe hardware decoding, exports standard Matroska XML files, and integrates directly with `mkvmerge` (lossless remux) and `mkvpropedit` (optional fast in-place chapter update).

## Features

* **Smart Auto-Loading:** Automatically reads and preserves existing chapters (including custom names) already present in the source video.
* **Precision Navigation:** Step frame-by-frame or jump cleanly to the next/previous Keyframe (I-frame), even across long GOP intervals.
* **In-Window Chapter Editing:** Create, remove, move, undo/redo, and rename chapter titles right inside the video window.
* **Live OSD & Chapter List:** Top-left OSD displays current frame, timestamp, active chapter, unsaved indicator, action feedback, and a toggleable chapter list.
* **Lossless Remuxing & In-Place Patching:** Remux safely to a brand-new `.mkv` via `mkvmerge` (running asynchronously in the background), or update chapters in-place in under a second via `mkvpropedit`.
* **Standardized XML Exports:** Generates properly escaped UTF-8 Matroska `chapters.xml` files compatible with MKVToolNix GUI.

## Compilation (1-Click Install)

Automated build scripts detect your operating system, install missing dependencies (`mpv`, `cmake`, `pkg-config`, C++17 compiler, `mkvtoolnix`), compile in Release mode, and run the unit tests. Pass `--skip-deps` to skip package-manager checks.

### macOS & Linux (Ubuntu/Debian, Arch, Fedora, openSUSE)
```bash
chmod +x build.sh
./build.sh
```

### Windows (via MSYS2)
1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 Clang64** terminal from your Start Menu (do *not* use UCRT64 or MSYS).
3. Navigate to the project folder and run:
```bash
./build_win.sh
```

### Manual Dependencies & Build

If compiling manually, you will need:
* A C++17 compiler (`g++`, `clang++`, or MSVC)
* `cmake` (>= 3.16) and `pkg-config`
* `libmpv` development headers (`libmpv-dev` on Debian/Ubuntu, `mpv` on Arch/Homebrew, `mpv-devel` on Fedora via [RPM Fusion](https://rpmfusion.org/) or openSUSE)
* **Optional runtime tools:** `mkvtoolnix` (`mkvmerge` for remuxing with `X`, `mkvpropedit` for in-place patching with `P`). XML editing and export (`S`) work even without MKVToolNix installed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## How to Use

Launch the compiled `chapter-editor` executable with the target video file:

```bash
./chapter-editor [options] /path/to/your/video.mkv
```

By default, outputs are placed next to the input video as `<video>_chapters.xml` and `<video>_chaptered.mkv`. Existing files prompt for confirmation before overwriting unless `--overwrite` is passed.

### Command-Line Options

* `-o, --output <file>`: Custom output `.mkv` file path.
* `-c, --chapters <file>`: Custom `.xml` chapters target path.
* `--load-xml <file>`: Automatically load and merge chapters from an XML file on startup.
* `--lang <code>`: Default ISO 639-2 chapter language code (default: `eng`).
* `--overwrite`: Overwrite existing XML / output MKV without confirmation prompt.
* `-h, --help`: Print usage and exit.
* `-v, --version`: Print version and exit.

### Controls

All controls work directly in the mpv video window (case-insensitive for letter shortcuts):

* `H` or `?`: Toggle the on-screen help overlay.
* `T` or `Tab`: Toggle the on-screen chapter list overlay.
* `Spacebar`: Play / Pause playback.
* `F`: Toggle fullscreen.
* `M`: Mute / Unmute audio.
* `9` / `0`: Decrease / Increase volume.
* `[` / `]` / `Backspace`: Decrease / Increase / Reset playback speed (`1.0x`).
* `Left Arrow` / `Right Arrow`: Step 1 frame backward or forward.
  * **+ `Alt`**: Jump 100 frames backward or forward.
  * **+ `Shift`**: Jump 1000 frames backward or forward.
* `Up Arrow` / `Down Arrow`: Jump forward (`Up`) to the next Keyframe or backward (`Down`) to the previous Keyframe.
  * **+ `Alt`**: Jump ~100 frames forward or backward to the nearest Keyframe.
  * **+ `Shift`**: Jump ~1000 frames forward or backward to the nearest Keyframe.
* `Page Up` / `Page Down`: Jump to the next or previous chapter marker.
* `C` or `Insert`: Create a chapter marker at the exact current frame.
* `D` or `Delete`: Remove the nearest chapter marker (within `0.5s` of current position).
* `U`: Move the nearest (or currently active) chapter marker to the current frame while preserving its title.
* `R`: Rename the nearest (or currently active) chapter in-window (`Enter` saves, `Esc` cancels, `Ctrl+U` clears back to auto-name).
* `Z` or `Ctrl+Z`: Undo the last chapter edit.
* `Y` / `Ctrl+Y` / `Ctrl+Shift+Z`: Redo an undone chapter edit.
* `S`: Export and save markers to the chapters XML file.
* `L`: Load and merge markers from the chapters XML file (falls back to `./chapters.xml` if needed).
* `X`: Save chapters XML and remux a new `.mkv` in the background using `mkvmerge` (also supports exporting a chapter-free MKV when 0 chapters remain).
* `P`: Patch chapters **in-place** in the source Matroska file using `mkvpropedit` (prompts for confirmation).
* `Q`: Quit the application (prompts if there are unsaved chapter edits).

## Acknowledgements

This project would not be possible without the engineering efforts of these open-source projects:

* **[mpv (libmpv)](https://mpv.io/)**: The core video rendering and frame-accurate seeking engine driving the visual interface. ([GitHub](https://github.com/mpv-player/mpv))
* **[MKVToolNix (mkvmerge / mkvpropedit)](https://mkvtoolnix.download/)**: The industry-standard Matroska toolkit used for lossless chapter remuxing and in-place chapter updates. ([Codeberg](https://codeberg.org/mbunkus/mkvtoolnix))

## License

This project is open-sourced under the MIT License. It dynamically links to `libmpv`, which is licensed under the LGPLv2.1. See the [LICENSE](LICENSE) file for details.
