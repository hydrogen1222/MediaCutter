# Playback Controls & Scrubber Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add functional playback controls (Play/Pause) and a scrubber (QSlider) with time display to the Media Cutter application.

**Architecture:** 
- `MpvWidget` will use `mpv_observe_property` to get updates for "time-pos" and "duration" and a `QTimer` to poll for mpv events.
- `MainWindow` will host the UI elements and coordinate between user input (seeking, play/pause) and `MpvWidget` state.
- Time formatting will be handled in `MainWindow` for display.

**Tech Stack:** C++, Qt 6, libmpv

---

### Task 1: Update MpvWidget for Event Polling and Property Observation

**Files:**
- Modify: `src/MpvWidget.h`
- Modify: `src/MpvWidget.cpp`

- [ ] **Step 1: Update `MpvWidget.h` to include QTimer and private slot for polling**

```cpp
// ... existing includes ...
#include <QTimer>

class MpvWidget : public QWidget {
    Q_OBJECT
public:
    // ... existing ...
private slots:
    void pollEvents();
private:
    mpv_handle *m_mpv;
    QTimer *m_timer;
};
```

- [ ] **Step 2: Update `MpvWidget.cpp` constructor to initialize timer and observe properties**

```cpp
MpvWidget::MpvWidget(QWidget *parent) : QWidget(parent) {
    // ... existing initialization ...
    
    // Observe properties
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MpvWidget::pollEvents);
    m_timer->start(50); // Poll every 50ms
}
```

- [ ] **Step 3: Implement `pollEvents` in `MpvWidget.cpp`**

```cpp
void MpvWidget::pollEvents() {
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                emit timeChanged(*(double *)prop->data);
            } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                emit durationChanged(*(double *)prop->data);
            }
        }
    }
}
```

- [ ] **Step 4: Commit**

```bash
git add src/MpvWidget.h src/MpvWidget.cpp
git commit -m "feat: implement mpv event polling and property observation"
```

### Task 2: Add UI Elements to MainWindow

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update `MainWindow.h` to include UI pointers**

```cpp
#pragma once
#include <QMainWindow>

class MpvWidget;
class QSlider;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void openFile();
    void onTimeChanged(double time);
    void onDurationChanged(double duration);
    void onSliderMoved(int position);
    void formatTime(double seconds, QLabel *label);

private:
    MpvWidget *m_player;
    QSlider *m_scrubber;
    QLabel *m_currentTimeLabel;
    QLabel *m_durationLabel;
    QPushButton *m_playPauseBtn;
    bool m_isUserSeeking = false;
};
```

- [ ] **Step 2: Update `MainWindow.cpp` to create UI layout and connect signals**

```cpp
// ... existing includes ...
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // ... window setup ...

    // Central Widget & Layout
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    m_player = new MpvWidget(this);
    mainLayout->addWidget(m_player);

    // Controls Layout
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    
    m_playPauseBtn = new QPushButton("Play/Pause", this);
    connect(m_playPauseBtn, &QPushButton::clicked, m_player, &MpvWidget::playPause);
    controlsLayout->addWidget(m_playPauseBtn);

    m_currentTimeLabel = new QLabel("00:00:00", this);
    controlsLayout->addWidget(m_currentTimeLabel);

    m_scrubber = new QSlider(Qt::Horizontal, this);
    m_scrubber->setRange(0, 1000); // We'll map this to 0-duration
    connect(m_scrubber, &QSlider::sliderPressed, [this](){ m_isUserSeeking = true; });
    connect(m_scrubber, &QSlider::sliderReleased, [this](){ m_isUserSeeking = false; });
    connect(m_scrubber, &QSlider::sliderMoved, this, &MainWindow::onSliderMoved);
    controlsLayout->addWidget(m_scrubber);

    m_durationLabel = new QLabel("00:00:00", this);
    controlsLayout->addWidget(m_durationLabel);

    mainLayout->addLayout(controlsLayout);

    // Player Signals
    connect(m_player, &MpvWidget::timeChanged, this, &MainWindow::onTimeChanged);
    connect(m_player, &MpvWidget::durationChanged, this, &MainWindow::onDurationChanged);
}
```

- [ ] **Step 3: Implement event handlers in `MainWindow.cpp`**

```cpp
void MainWindow::onTimeChanged(double time) {
    if (!m_isUserSeeking) {
        double duration = m_player->getDuration();
        if (duration > 0) {
            m_scrubber->setValue((int)((time / duration) * 1000));
        }
    }
    formatTime(time, m_currentTimeLabel);
}

void MainWindow::onDurationChanged(double duration) {
    formatTime(duration, m_durationLabel);
}

void MainWindow::onSliderMoved(int position) {
    double duration = m_player->getDuration();
    if (duration > 0) {
        double seekTo = (position / 1000.0) * duration;
        m_player->seek(seekTo);
    }
}

void MainWindow::formatTime(double seconds, QLabel *label) {
    int s = (int)seconds;
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int secs = s % 60;
    label->setText(QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));
}
```

- [ ] **Step 4: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: add playback controls and scrubber UI to MainWindow"
```

### Task 3: Verification

- [ ] **Step 1: Build the application**

Run: `cmake --build build --config Release`
Expected: Successful build.

- [ ] **Step 2: Run the application and test controls**

1. Open a video file.
2. Verify "Play/Pause" button works.
3. Verify timer updates as video plays.
4. Verify duration is correctly shown.
5. Verify scrubbing (dragging slider) works and seeks correctly.

- [ ] **Step 3: Final Commit**

```bash
git add .
git commit -m "docs: complete Task 3 implementation"
```
