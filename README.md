# Media Cutter

[简体中文](./README.zh-CN.md) | English

A high-performance, native desktop application for lossless video and audio clipping. Unlike web-based solutions, this app uses a native media engine for seamless playback of all formats (HEVC/AV1/4K) and leverages FFmpeg for lightning-fast, no-reencoding exports.

`To be honest, it is a product of vibe coding`

## Key Features

- **Native Performance**: Powered by `libmpv` with full hardware acceleration.
- **Lossless Export**: High-speed clipping without quality loss using FFmpeg's stream copy.
- **Flexible Workflow**: Supports reordering segments and exporting them individually or merged.
- **Modern UI**: Clean dark-themed interface built with Qt Widgets.
- **Multilingual Support**: Ready for internationalization (i18n).

## Technical Stack

- **Framework**: Qt 6 (Widgets)
- **Media Engine**: libmpv
- **Backend Processor**: FFmpeg CLI
- **Build System**: CMake
- **Package Manager**: vcpkg

## Prerequisites

To build this project, you need:

1. **Visual Studio 2022** with "Desktop development with C++" workload.
2. **vcpkg**: Microsoft's C++ package manager.
3. **FFmpeg**: Must be available in your system `PATH`.
4. **libmpv Development Files**: Headers and libraries placed in `third_party/mpv/`.

## Build Instructions

1. **Clone the repository**:
   ```bash
   git clone https://github.com/YOUR_USERNAME/MediaCutter.git
   cd MediaCutter
   ```

2. **Install Dependencies via vcpkg**:
   ```bash
   vcpkg install qtbase:x64-windows
   ```

3. **Configure and Build**:
   - Open the folder in **Visual Studio 2022**.
   - Set the CMake toolchain file to your `vcpkg.cmake` path.
   - Click **Build > Rebuild All**.

## License

MIT License - See [LICENSE](LICENSE) for details.
