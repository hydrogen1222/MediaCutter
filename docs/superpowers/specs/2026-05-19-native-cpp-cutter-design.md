# Design Spec: Native C++/Qt Media Cutter (libmpv + FFmpeg)

## 1. Overview
A high-performance, native desktop application (Windows/Linux) for lossless video and audio clipping. By transitioning to C++ and Qt, the application gains native hardware-accelerated playback for all formats (including HEVC/AV1) via `libmpv`, while maintaining a lightweight footprint and using FFmpeg for fast, no-reencoding exports.

## 2. Technical Stack
- **Language:** C++17
- **UI Framework:** Qt 6 (Widgets)
- **Media Engine:** [libmpv](https://mpv.io/) (Embedded for native rendering)
- **Export Engine:** [FFmpeg](https://ffmpeg.org/) (CLI via `QProcess`)
- **Build System:** CMake
- **Package Manager:** vcpkg (for Qt and system dependencies)

## 3. Architecture
### 3.1 Component Breakdown
- **`MainWindow`**: The main container managing the overall layout, menu bar, and coordination between sub-components.
- **`MpvWidget`**: A custom `QWidget` that serves as the rendering surface for `libmpv`. It wraps the C API of libmpv into a Qt-friendly interface (Signals/Slots).
- **`ClipModel`**: A data model managing the list of selected segments (Start Time, End Time, Name).
- **`FFmpegRunner`**: A utility class (running in a background `QThread`) that constructs and executes FFmpeg commands to prevent UI blocking during export.

### 3.2 Data Flow
1. **Loading**: User selects a file -> `MpvWidget` loads the media via `libmpv`.
2. **Interaction**: User scrubs the timeline or types a timestamp -> `MpvWidget` seeks the media.
3. **Marking**: User clicks "Mark In/Out" -> Current timestamp is captured and added to the `ClipModel`.
4. **Exporting**: User clicks "Export" -> `FFmpegRunner` receives the clip list and input file path -> Executes `ffmpeg -c copy` -> Reports progress/completion back to the UI.

## 4. UI/UX Design (Qt Widgets)
- **Main Layout**: A `QHBoxLayout` splitting the screen into a player area and a queue area.
- **Player Area (Left)**:
  - `MpvWidget` for video display.
  - `QSlider` for the scrubber.
  - `QHBoxLayout` with "Mark In", "Mark Out" buttons and a `QLineEdit` for precise time entry.
- **Queue Area (Right)**:
  - `QListView` or `QTableWidget` to show segments.
  - "Export Individual" and "Merge & Export" buttons at the bottom.
- **Styling**: `QProxyStyle` or QSS (Qt Style Sheets) will be used to provide a modern dark-themed appearance.

## 5. File Structure
```
D:/Codex/cut/
├── CMakeLists.txt
├── mpv-2.dll               (Runtime dependency)
├── ffmpeg.exe              (Runtime dependency)
├── src/
│   ├── main.cpp
│   ├── MainWindow.cpp/h
│   ├── MpvWidget.cpp/h
│   ├── ClipModel.cpp/h
│   └── FFmpegRunner.cpp/h
├── third_party/
│   └── mpv/
│       ├── include/        (Header files)
│       └── lib/            (Import libraries)
└── docs/
    └── superpowers/
        └── specs/          (This spec)
```

## 6. Implementation Strategy
### 6.1 Phase 1: Environment & Scaffolding
- Configure CMake to find Qt6 and libmpv.
- Create the basic `QMainWindow` and verify the build environment.

### 6.2 Phase 2: Native Player Integration
- Implement `MpvWidget` using `mpv_render_context`.
- Handle native window embedding (passing `winId()` to mpv).
- Implement basic playback controls (play/pause, seek).

### 6.3 Phase 3: Clipping Logic
- Implement the "Mark In/Out" system.
- Build the `ClipModel` and the right-hand side queue UI.

### 6.4 Phase 4: FFmpeg Backend
- Implement the `QProcess` wrapper for FFmpeg.
- Handle individual segment clipping and `concat` demuxer for merging.

## 7. Constraints & Limitations
- **Keyframe Accuracy**: Exporting with `-c copy` is still subject to GOP/keyframe boundaries.
- **Dependency Management**: Users must have `ffmpeg.exe` in the application directory or system path. `libmpv-2.dll` must be bundled with the executable.
