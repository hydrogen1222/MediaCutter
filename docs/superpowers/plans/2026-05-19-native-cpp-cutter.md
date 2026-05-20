# Native C++/Qt Media Cutter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a high-performance native desktop application for lossless video/audio clipping using C++, Qt 6, and libmpv.

**Architecture:** A native Qt Widgets application that embeds libmpv for hardware-accelerated playback and uses FFmpeg via background processes for lossless clipping.

**Tech Stack:** C++17, Qt 6, libmpv, FFmpeg, CMake, vcpkg.

---

### Task 1: Project Scaffolding & Build System

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/MainWindow.h`
- Create: `src/MainWindow.cpp`

- [ ] **Step 1: Create CMakeLists.txt**
Configure CMake to find Qt6 and libmpv. Use vcpkg for Qt6 and assume libmpv is in `third_party/mpv`.

```cmake
cmake_minimum_required(VERSION 3.16)
project(MediaCutter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

include_directories(third_party/mpv/include)
link_directories(third_party/mpv/lib)

add_executable(MediaCutter 
    src/main.cpp 
    src/MainWindow.cpp
)

target_link_libraries(MediaCutter PRIVATE Qt6::Widgets mpv)

add_custom_command(TARGET MediaCutter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/libmpv-2.dll"
    $<TARGET_FILE_DIR:MediaCutter>
)
```

- [ ] **Step 2: Create main.cpp**
Basic Qt application entry point.

```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
```

- [ ] **Step 3: Create MainWindow skeleton**
Basic window with a layout.

```cpp
// MainWindow.h
#pragma once
#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
};

// MainWindow.cpp
#include "MainWindow.h"
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Media Cutter");
    resize(1000, 700);
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);
}
```

- [ ] **Step 4: Verify Build**
Open the folder in VS 2022, configure CMake with vcpkg toolchain, and build.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt src/main.cpp src/MainWindow.h src/MainWindow.cpp
git commit -m "chore: initial project scaffolding"
```

### Task 2: Native Player Integration (MpvWidget)

**Files:**
- Create: `src/MpvWidget.h`
- Create: `src/MpvWidget.cpp`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Create MpvWidget class**
A QWidget that manages a `mpv_handle` and renders to its `winId()`.

```cpp
// MpvWidget.h
#pragma once
#include <QWidget>
#include <mpv/client.h>

class MpvWidget : public QWidget {
    Q_OBJECT
public:
    MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget();
    void loadFile(const QString &path);
    void playPause();
    void seek(double seconds);
    double getDuration();
    double getCurrentTime();

signals:
    void timeChanged(double time);
    void durationChanged(double duration);

private:
    mpv_handle *m_mpv;
};
```

- [ ] **Step 2: Implement MpvWidget logic**
Initialize mpv and set the "wid" property to the widget's native window ID.

```cpp
// MpvWidget.cpp
#include "MpvWidget.h"
#include <stdexcept>

MpvWidget::MpvWidget(QWidget *parent) : QWidget(parent) {
    m_mpv = mpv_create();
    if (!m_mpv) throw std::runtime_error("could not create mpv context");
    
    int64_t wid = winId();
    mpv_set_property(m_mpv, "wid", MPV_FORMAT_INT64, &wid);
    mpv_initialize(m_mpv);
}

MpvWidget::~MpvWidget() {
    mpv_terminate_destroy(m_mpv);
}

void MpvWidget::loadFile(const QString &path) {
    const char *cmd[] = {"loadfile", path.toUtf8().constData(), nullptr};
    mpv_command(m_mpv, cmd);
}
// (Implement other stubs for play, seek, etc. using mpv_command/mpv_get_property)
```

- [ ] **Step 3: Integrate into MainWindow**
Add MpvWidget to the layout and add a "Open File" action.

- [ ] **Step 4: Commit**
```bash
git add src/MpvWidget.h src/MpvWidget.cpp
git commit -m "feat: integrate libmpv player widget"
```

### Task 3: Playback Controls & Scrubber

**Files:**
- Modify: `src/MainWindow.cpp`
- Modify: `src/MpvWidget.cpp`

- [ ] **Step 1: Implement mpv event polling**
Use a timer or a dedicated thread to poll `mpv_wait_event` and emit signals for time updates.

- [ ] **Step 2: Add QSlider and time display**
Add a slider and a label to `MainWindow`. Connect `MpvWidget::timeChanged` to update the slider.

- [ ] **Step 3: Bi-directional seeking**
Connect slider changes back to `MpvWidget::seek`.

- [ ] **Step 4: Commit**
```bash
git commit -m "feat: add playback controls and scrubber"
```

### Task 4: Segment Management (Queue)

**Files:**
- Create: `src/ClipModel.h`
- Create: `src/ClipModel.cpp`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Implement ClipModel**
A simple list-based data structure (or `QAbstractListModel`) to store `struct Segment { double start, end; }`.

- [ ] **Step 2: Add "Mark In/Out" UI**
Add buttons to `MainWindow`. Store current time as start/end and add to queue.

- [ ] **Step 3: Display Queue**
Use a `QListWidget` or `QTableWidget` to show segments in the right-hand panel.

- [ ] **Step 4: Commit**
```bash
git commit -m "feat: implement segment queue and marking"
```

### Task 5: FFmpeg Export Implementation

**Files:**
- Create: `src/FFmpegRunner.h`
- Create: `src/FFmpegRunner.cpp`

- [ ] **Step 1: Implement FFmpegRunner**
Use `QProcess` to execute `ffmpeg.exe`.

```cpp
void FFmpegRunner::cut(const QString &input, double start, double end, const QString &output) {
    QStringList args;
    args << "-ss" << QString::number(start) 
         << "-to" << QString::number(end)
         << "-i" << input 
         << "-c" << "copy" << "-y" << output;
    m_process->start("ffmpeg", args);
}
```

- [ ] **Step 2: Implement Merge (Concat)**
Write a temporary file and run `ffmpeg -f concat`.

- [ ] **Step 3: Commit**
```bash
git commit -m "feat: implement ffmpeg export logic"
```

### Task 6: Final Polish & Styling

**Files:**
- Create: `src/style.qss`
- Modify: `src/main.cpp`

- [ ] **Step 1: Apply Dark Theme**
Use a QSS stylesheet to give the app a modern appearance.

- [ ] **Step 2: Error Handling & Cleanup**
Add message boxes for errors (e.g., ffmpeg missing).

- [ ] **Step 3: Final Verification**
Test with 1.mp4 and verify it plays and exports correctly.

- [ ] **Step 4: Commit**
```bash
git commit -m "style: add dark theme and final polish"
```
