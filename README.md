# MKV Chapter Editor

MKV Chapter Editor is an ultra-lightweight, specialized tool designed for one purpose: precisely scrubbing through video files to define, edit, and export Matroska chapter markers without altering the original file or requiring complex video editing software.

Under the hood, it uses the highly robust `libmpv` engine for fast visual rendering, audio playback for seeking cues, and precise I-frame seeking, intentionally stripping out subtitles to maximize performance. It safely utilizes hardware decoding, allows you to save standard Matroska XML files, and integrates directly with `mkvmerge` to seamlessly output a new chaptered file when you are done.

## Features

* **Smart Auto-Loading:** Automatically reads and imports any existing chapters already present in the source MKV file.
* **Precision Navigation:** Jump exactly to video Keyframes (I-frames) for standard-compliant chapter generation.
* **Zero-Overhead Playback:** Subtitles are disabled to maximize performance while retaining audio for sound cue navigation. Hardware decoding is natively supported.
* **Visual Chapter Ticks:** The progress bar dynamically draws tick marks for all loaded and newly edited chapters in real-time.
* **Lossless Remuxing:** Creates a brand-new `.mkv` copy integrating your chapters via MKVToolNix.
* **Standardized Exports:** Generates official Matroska-compliant `chapters.xml` files that can be directly imported into the MKVToolNix GUI.

## Compilation (1-Click Install)

We have provided fully automated build scripts that detect your operating system, automatically grab all necessary development libraries (`mpv`, `cmake`, C++ compilers), and build the application for you.

### macOS & Linux (Ubuntu/Debian, Arch, Fedora)
1. Clone this repository or download the source code.
2. Open a terminal in the project directory.
3. Make the script executable and run it:
```bash
chmod +x build.sh
./build.sh
```

### Windows (via MSYS2)
Windows compilation is fully supported using the modern MSYS2 environment.
1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 Clang64** terminal from your Start Menu (Do *not* open the UCRT64 or MSYS environments).
3. Navigate to your project folder using `cd` (e.g., `cd /c/Users/YourName/Downloads/MKV-Chapter-Editor`).
4. Run the Windows build script:
```bash
./build_win.sh
```

## How to Use

Once compiled, an executable named `chapter-editor` (or `chapter-editor.exe` on Windows) will be present in the main directory. Launch the application via the command line, passing the target video file as an argument:

```bash
./chapter-editor /path/to/your/video.mkv
```

*Note: The application will automatically verify that the target video file exists and is readable before launching. If you launch it without a path, it will print usage instructions.*

### Controls

The program operates entirely via keyboard shortcuts. An on-screen overlay will assist you while the program is running.

* `H` or `?`: Toggle the on-screen help menu.
* `Spacebar`: Play / Pause playback (Test exactly how the video plays from your chosen chapter point).
* `Left Arrow` / `Right Arrow`: Step exactly one frame backward or forward.
  * **+ `Alt`**: Jump 100 frames backward or forward.
  * **+ `Shift`**: Jump 1000 frames backward or forward.
* `Up Arrow` / `Down Arrow`: Jump backward or forward to the next/previous Keyframe (I-frame).
  * **+ `Alt`**: Jump ~100 frames backward or forward to the nearest Keyframe.
  * **+ `Shift`**: Jump ~1000 frames backward or forward to the nearest Keyframe.
* `Page Up` / `Page Down`: Jump immediately to the next or previous chapter marker you have placed.
* `C` or `Insert`: Create a Chapter marker at the exact current frame.
* `D` or `Delete`: Delete the nearest chapter marker (works within a ~0.5-second tolerance of your current position).
* `S`: Securely export and save your markers to a `chapters.xml` file without remuxing.
* `L`: Load and merge markers from an existing `chapters.xml` file in the current directory.
* `X`: Export your markers to `chapters.xml` AND automatically trigger `mkvmerge` to create `chaptered_output.mkv`.
* `Q`: Quit the application (Safely prompts you if there are unsaved chapter edits).

## Acknowledgements

This project is a simple wrapper and would not be possible without the incredible, decades-long engineering efforts of the following open-source projects. I heavily rely on their work to make this tool function:

* **[mpv (libmpv)](https://mpv.io/)**: The core video rendering and frame-accurate seeking engine driving the visual interface. ([GitHub](https://github.com/mpv-player/mpv))
* **[MKVToolNix (mkvmerge)](https://mkvtoolnix.download/)**: The industry-standard Matroska tool utilized securely for all lossless chapter remuxing and MKV data handling. ([Codeberg](https://codeberg.org/mbunkus/mkvtoolnix))

## License

This project is open-sourced under the MIT License. It dynamically links to `libmpv`, which is licensed under the LGPLv2.1. See the `LICENSE` file for details.