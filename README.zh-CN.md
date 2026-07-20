# 视频剪辑器

简体中文 | [English](./README.md)

基于 C++ 和 Qt 开发的高性能原生无损音视频剪辑工具。通过直接嵌入原生媒体引擎，解决了 Web 架构在播放 HEVC/AV1 等编码时的卡顿和兼容性问题。

`这是AI编程的产物`

## 核心特性

- **原生性能**：基于 `libmpv` OpenGL 渲染，支持全格式硬件加速播放。
- **无损导出**：利用 FFmpeg 的流拷贝技术，实现极速、零质量损失的剪辑。
- **电台视频与硬字幕压制**：用封面图+音频合成视频，或将 `.ass` 字幕硬压到视频中。压制时自动检测并优先使用 GPU 硬件编码（NVENC / QuickSync / VAAPI），无可用 GPU 时回退到 libx264（`-preset veryfast`，调用全部 CPU 核心）。当前使用的编码器会显示在进度条状态中。AMF 被刻意跳过：在某些 AMD/Windows 驱动组合下（如 780M）它能通过探测却在实际压制时死锁，因此 Windows 上的 AMD 改用 libx264（全部核心），Linux 上的 AMD 仍可用 VAAPI。作为兜底，若选中的硬件编码器在实际压制时卡住，会自动放弃并改用 libx264 重试，导出绝不会一直卡死。
- **灵活工作流**：支持片段排序、分段导出或合并导出。
- **现代 UI**：基于 Qt Widgets 的深色主题界面。
- **多语言支持**：内置简体中文与英文，运行时可切换。

## 技术栈

- **框架**：Qt 6 (Widgets + OpenGL)
- **媒体引擎**：libmpv (render API)
- **剪切后端**：FFmpeg CLI
- **构建系统**：CMake
- **包管理器**：vcpkg (Windows)

## 使用方法

1. **文件 → 打开文件** 加载视频。
2. 使用播放控件浏览视频，在片段起点按 **设为起点**，终点按 **设为终点**，片段即加入导出队列。
3. 在队列中可上下移动或删除片段。
4. **合并导出** 将所有片段合为一个文件，**分段导出** 将每段保存为独立文件。导出采用 FFmpeg 流拷贝，不重新编码，无画质损失。

## 键盘快捷键

- **空格键 (Space)**：播放 / 暂停
- **左 / 右方向键 (Left / Right)**：向后 / 向前快进 1 秒
- **Shift + 左 / 右方向键**：向后 / 向前快进 5 秒
- **I**：设为起点 (Mark In)
- **O**：设为终点 (Mark Out，并自动加入导出队列)
- **Delete**：从队列中删除所选片段
- **Ctrl + E**：合并导出
- **Ctrl + Shift + E**：分段导出

## 环境要求

### Windows

1. **Visual Studio 2022**（勾选"使用 C++ 的桌面开发"）。
2. **vcpkg**（用于管理 Qt6 依赖）。
3. **CMake**（VS 2022 自带，或独立安装 ≥ 3.16）。
4. **FFmpeg**（需添加到系统环境变量 `PATH`）。
5. **libmpv**：首次配置时 CMake 会自动下载 Windows 开发包。

### Linux

1. **CMake** ≥ 3.16
2. **Qt 6** 开发包（Widgets、OpenGLWidgets、LinguistTools）
3. **libmpv** 开发包
4. **FFmpeg**（在 `PATH` 中）
5. **pkg-config**

<details>
<summary>各发行版安装命令（点击展开）</summary>

**Debian / Ubuntu：**
```bash
sudo apt install cmake qt6-base-dev libmpv-dev ffmpeg pkg-config
```

**Fedora：**
```bash
sudo dnf install cmake qt6-qtbase-devel mpv-libs-devel ffmpeg pkgconfig
```

**Arch：**
```bash
sudo pacman -S cmake qt6-base mpv ffmpeg pkgconf
```

**Gentoo：**
```bash
sudo emerge dev-build/cmake dev-qt/qtbase media-video/mpv media-video/ffmpeg dev-util/pkgconf
```
</details>

## 编译步骤

### Windows

1. **克隆仓库**：
   ```bash
   git clone https://github.com/YOUR_USERNAME/MediaCutter.git
   cd MediaCutter
   ```

2. **通过 vcpkg 安装 Qt6**：
   ```bash
   vcpkg install qtbase:x64-windows
   ```

3. **配置并编译**：
   - 用 **Visual Studio 2022** 打开项目文件夹。
   - 在 CMake 设置中指定 `vcpkg.cmake` 工具链路径。
   - 点击"全部生成"。

   或命令行：
   ```bash
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ```

### Linux

1. **克隆仓库**：
   ```bash
   git clone https://github.com/YOUR_USERNAME/MediaCutter.git
   cd MediaCutter
   ```

2. **安装依赖**（见上方"环境要求"中对应发行版的命令）。

3. **配置并编译**：
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   ```

4. **运行**：
   ```bash
   ./build/MediaCutter
   ```

## 翻译

翻译文件位于 `translations/`。新增或修改语言的方法：

1. 编辑或创建 `translations/media-cutter_<locale>.ts`。
2. 重新编译 —— CMake 会自动将 `.ts` 编译为 `.qm` 并嵌入二进制。
3. 运行时通过 **语言** 菜单切换。

## 许可证

MIT License - 详见 [LICENSE](LICENSE)。
