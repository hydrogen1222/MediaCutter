# Media Cutter (C++/Qt)

[简体中文](./README.zh-CN.md) | English

A high-performance, native desktop application for lossless video and audio clipping. Unlike web-based solutions, this app uses a native media engine for seamless playback of all formats (HEVC/AV1/4K) and leverages FFmpeg for lightning-fast, no-reencoding exports.

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

---

# 媒体剪辑器 (C++/Qt)

基于 C++ 和 Qt 开发的高性能原生无损音视频剪辑工具。通过直接嵌入原生媒体引擎，解决了 Web 架构在播放 HEVC/AV1 等编码时的卡顿和兼容性问题。

## 核心特性

- **原生性能**：基于 `libmpv`，支持全格式硬件加速播放。
- **无损导出**：利用 FFmpeg 的流拷贝技术，实现极速、零质量损失的剪辑。
- **灵活工作流**：支持片段排序、分段导出或合并导出。
- **现代 UI**：基于 Qt Widgets 的深色主题界面。
- **多语言支持**：内置国际化支持。

## 环境要求

1. **Visual Studio 2022** (勾选“使用 C++ 的桌面开发”)。
2. **vcpkg** (用于管理 Qt6 依赖)。
3. **FFmpeg** (需添加到系统环境变量 `PATH`)。
4. **libmpv 开发包** (放置于 `third_party/mpv/` 目录)。

## 编译步骤

1. 使用 VS 2022 打开项目文件夹。
2. 在 CMake 设置中指定 `vcpkg.cmake` 工具链。
3. 点击“全部生成”。
