# Media Cutter

[简体中文](./README.zh-CN.md) | English

A high-performance, native desktop application for lossless video and audio clipping. Unlike web-based solutions, this app uses a native media engine for seamless playback of all formats (HEVC/AV1/4K) and leverages FFmpeg for lightning-fast, no-reencoding exports.

`To be honest, it is a product of vibe coding`

## Key Features

- **Native Performance**: Powered by `libmpv` with full hardware acceleration via OpenGL rendering.
- **Lossless Export**: High-speed clipping without quality loss using FFmpeg's stream copy.
- **Flexible Workflow**: Supports reordering segments and exporting them individually or merged.
- **Modern UI**: Clean dark-themed interface built with Qt Widgets.
- **Multilingual Support**: Built-in English and Simplified Chinese (switchable at runtime).

## Technical Stack

- **Framework**: Qt 6 (Widgets + OpenGL)
- **Media Engine**: libmpv (render API)
- **Backend Processor**: FFmpeg CLI
- **Build System**: CMake
- **Package Manager**: vcpkg (Windows)

## Usage

1. **File → Open File** to load a video.
2. Use the playback controls to navigate. Press **Mark In** at the start of a clip and **Mark Out** at the end — the segment is added to the export queue.
3. Reorder or delete segments in the queue as needed.
4. **Export Merged** to combine all segments into one file, or **Export Separately** to save each segment individually. Export uses FFmpeg stream copy — no re-encoding, no quality loss.

## Prerequisites

### Windows

1. **Visual Studio 2022** with the "Desktop development with C++" workload.
2. **vcpkg**: Microsoft's C++ package manager.
3. **CMake** (bundled with VS 2022 or standalone ≥ 3.16).
4. **FFmpeg**: Must be available in your system `PATH`.
5. **libmpv**: The CMake script will automatically download the Windows dev package on first configure.

### Linux

1. **CMake** ≥ 3.16
2. **Qt 6** development packages (Widgets, OpenGLWidgets, LinguistTools)
3. **libmpv** development package
4. **FFmpeg** (in `PATH`)
5. **pkg-config**

<details>
<summary>Package install commands (click to expand)</summary>

**Debian / Ubuntu:**
```bash
sudo apt install cmake qt6-base-dev libmpv-dev ffmpeg pkg-config
```

**Fedora:**
```bash
sudo dnf install cmake qt6-qtbase-devel mpv-libs-devel ffmpeg pkgconfig
```

**Arch:**
```bash
sudo pacman -S cmake qt6-base mpv ffmpeg pkgconf
```

**Gentoo:**
```bash
sudo emerge dev-build/cmake dev-qt/qtbase media-video/mpv media-video/ffmpeg dev-util/pkgconf
```
</details>

## Build Instructions

### Windows

1. **Clone the repository**:
   ```bash
   git clone https://github.com/YOUR_USERNAME/MediaCutter.git
   cd MediaCutter
   ```

2. **Install Qt6 via vcpkg**:
   ```bash
   vcpkg install qtbase:x64-windows
   ```

3. **Configure and build**:
   - Open the folder in **Visual Studio 2022**.
   - Set the CMake toolchain file to your `vcpkg.cmake` path.
   - Click **Build → Rebuild All**.

   Or from the command line:
   ```bash
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ```

### Linux

1. **Clone the repository**:
   ```bash
   git clone https://github.com/YOUR_USERNAME/MediaCutter.git
   cd MediaCutter
   ```

2. **Install dependencies** (see Prerequisites above for your distro).

3. **Configure and build**:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   ```

4. **Run**:
   ```bash
   ./build/MediaCutter
   ```

## Translating

Translation files live in `translations/`. To add or update a language:

1. Edit or create `translations/media-cutter_<locale>.ts`.
2. Rebuild — CMake will compile `.ts` into `.qm` and embed it in the binary automatically.
3. Switch language at runtime via **Language** menu.

## License

MIT License - See [LICENSE](LICENSE) for details.
