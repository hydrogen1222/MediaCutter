# FFmpeg Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement video segment export and merging using FFmpeg.

**Architecture:** Use a `FFmpegRunner` class to manage sequential `QProcess` calls for cutting and concatenating video segments. `MainWindow` will provide the UI trigger and progress feedback.

**Tech Stack:** Qt (QProcess, QProgressDialog, QFileDialog), FFmpeg (CLI).

---

### Task 1: Update ClipModel

**Files:**
- Modify: `src/ClipModel.h`

- [ ] **Step 1: Add segments() method to ClipModel.h**
Add a method to return the internal segments vector.

```cpp
const std::vector<Segment>& segments() const { return m_segments; }
```

- [ ] **Step 2: Commit**
```bash
git add src/ClipModel.h
git commit -m "feat: add segments() getter to ClipModel"
```

### Task 2: Implement FFmpegRunner

**Files:**
- Create: `src/FFmpegRunner.h`
- Create: `src/FFmpegRunner.cpp`

- [ ] **Step 1: Create FFmpegRunner.h**
Define the class with signals and the `cutAndMerge` method.

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <vector>
#include <QProcess>
#include <QTemporaryDir>
#include "ClipModel.h"

class FFmpegRunner : public QObject {
    Q_OBJECT
public:
    explicit FFmpegRunner(QObject *parent = nullptr);
    void cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output);

signals:
    void progress(int current, int total, const QString &status);
    void finished(bool success, const QString &message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void runNextStep();
    
    QString m_input;
    std::vector<Segment> m_segments;
    QString m_output;
    int m_currentIndex = 0;
    QProcess *m_process;
    QTemporaryDir m_tempDir;
    QStringList m_tempFiles;
    bool m_isMerging = false;
};
```

- [ ] **Step 2: Create FFmpegRunner.cpp**
Implement the logic to run FFmpeg commands sequentially.

```cpp
#include "FFmpegRunner.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

FFmpegRunner::FFmpegRunner(QObject *parent) : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::finished, this, &FFmpegRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FFmpegRunner::onProcessError);
}

void FFmpegRunner::cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output) {
    m_input = input;
    m_segments = segments;
    m_output = output;
    m_currentIndex = 0;
    m_isMerging = false;
    m_tempFiles.clear();

    if (m_segments.empty()) {
        emit finished(false, "No segments to export");
        return;
    }

    if (!m_tempDir.isValid()) {
        emit finished(false, "Failed to create temporary directory");
        return;
    }

    runNextStep();
}

void FFmpegRunner::runNextStep() {
    if (m_currentIndex < m_segments.size()) {
        // Cutting step
        const Segment &s = m_segments[m_currentIndex];
        QString tempFile = m_tempDir.path() + QString("/temp_%1.ts").arg(m_currentIndex);
        m_tempFiles << tempFile;

        QStringList args;
        args << "-ss" << QString::number(s.start, 'f', 3)
             << "-to" << QString::number(s.end, 'f', 3)
             << "-i" << m_input
             << "-c" << "copy"
             << "-y" << tempFile;

        emit progress(m_currentIndex, m_segments.size() + 1, QString("Cutting segment %1...").arg(m_currentIndex + 1));
        m_process->start("ffmpeg", args);
    } else if (!m_isMerging) {
        // Prepare concat file
        m_isMerging = true;
        QString concatFilePath = m_tempDir.path() + "/concat.txt";
        QFile concatFile(concatFilePath);
        if (concatFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&concatFile);
            for (const QString &f : m_tempFiles) {
                out << "file '" << f << "'\n";
            }
            concatFile.close();
        } else {
            emit finished(false, "Failed to create concat file");
            return;
        }

        // Merging step
        QStringList args;
        args << "-f" << "concat"
             << "-safe" << "0"
             << "-i" << concatFilePath
             << "-c" << "copy"
             << "-y" << m_output;

        emit progress(m_segments.size(), m_segments.size() + 1, "Merging segments...");
        m_process->start("ffmpeg", args);
    } else {
        emit progress(m_segments.size() + 1, m_segments.size() + 1, "Done");
        emit finished(true, "Export completed successfully");
    }
}

void FFmpegRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        if (!m_isMerging) {
            m_currentIndex++;
        }
        runNextStep();
    } else {
        emit finished(false, QString("FFmpeg process failed with exit code %1").arg(exitCode));
    }
}

void FFmpegRunner::onProcessError(QProcess::ProcessError error) {
    emit finished(false, QString("FFmpeg process error: %1").arg(error));
}
```

- [ ] **Step 3: Commit**
```bash
git add src/FFmpegRunner.h src/FFmpegRunner.cpp
git commit -m "feat: implement FFmpegRunner for segment export"
```

### Task 3: Update MainWindow

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update MainWindow.h**
Add `exportAll` slot and `m_ffmpegRunner` (optional, can be local if managed).

```cpp
private slots:
    // ... existing slots
    void exportAll();

private:
    // ... existing members
    // FFmpegRunner can be instantiated in the slot
```

- [ ] **Step 2: Update MainWindow.cpp**
Connect the "Export All" button and implement `exportAll`.

```cpp
// In constructor, connect the button:
// connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportAll);

// Implement the slot:
#include "FFmpegRunner.h"
#include <QProgressDialog>
#include <QMessageBox>

void MainWindow::exportAll() {
    if (m_clipModel->rowCount() == 0) {
        QMessageBox::warning(this, "Export", "No segments to export.");
        return;
    }

    QString output = QFileDialog::getSaveFileName(this, "Export All", "", "Video Files (*.mp4 *.mkv *.avi *.mov);;All Files (*)");
    if (output.isEmpty()) return;

    FFmpegRunner *runner = new FFmpegRunner(this);
    QProgressDialog *progress = new QProgressDialog("Exporting...", "Cancel", 0, m_clipModel->rowCount() + 1, this);
    progress->setWindowModality(Qt::WindowModal);

    connect(runner, &FFmpegRunner::progress, progress, [progress](int current, int total, const QString &status){
        progress->setMaximum(total);
        progress->setValue(current);
        progress->setLabelText(status);
    });

    connect(runner, &FFmpegRunner::finished, this, [this, runner, progress](bool success, const QString &message){
        progress->close();
        if (success) {
            QMessageBox::information(this, "Export", "Export successful!");
        } else {
            QMessageBox::critical(this, "Export", "Export failed: " + message);
        }
        runner->deleteLater();
        progress->deleteLater();
    });

    // Handle cancellation
    connect(progress, &QProgressDialog::canceled, runner, [runner](){
        // Ideally tell runner to abort, for now just cleanup
        runner->deleteLater();
    });

    runner->cutAndMerge(m_player->getFileName(), m_clipModel->segments(), output);
}
```
*Note: I need to add `getFileName()` to `MpvWidget` or store it in `MainWindow`.*
Actually, `MainWindow::openFile` stores it in `m_player->loadFile(fileName)`. I should add `getFileName()` to `MpvWidget`.

- [ ] **Step 3: Add getFileName() to MpvWidget**
Modify `src/MpvWidget.h` and `src/MpvWidget.cpp`.

```cpp
// MpvWidget.h
QString getFileName() const { return m_fileName; }
private:
QString m_fileName;

// MpvWidget.cpp
void MpvWidget::loadFile(const QString &fileName) {
    m_fileName = fileName;
    // ... existing load logic
}
```

- [ ] **Step 4: Commit**
```bash
git add src/MainWindow.h src/MainWindow.cpp src/MpvWidget.h src/MpvWidget.cpp
git commit -m "feat: connect Export All button to FFmpegRunner"
```

### Task 4: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add FFmpegRunner.cpp to executable sources**

```cmake
add_executable(MediaCutter
    src/main.cpp
    src/MainWindow.cpp
    src/MpvWidget.cpp
    src/ClipModel.cpp
    src/FFmpegRunner.cpp
)
```

- [ ] **Step 2: Commit**
```bash
git add CMakeLists.txt
git commit -m "build: add FFmpegRunner to CMake"
```

### Task 5: Verification

- [ ] **Step 1: Build the project**
Run `cmake --build build --config Release`

- [ ] **Step 2: Verify binary exists**
Check `build/Release/MediaCutter.exe`

- [ ] **Step 3: Manual Test (Optional but recommended if environment allows)**
Try to open a file, mark segments, and export.
