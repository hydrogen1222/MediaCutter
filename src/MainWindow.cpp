#include "MainWindow.h"
#include "MpvWidget.h"
#include "ClipModel.h"
#include "FFmpegRunner.h"
#include <QShortcut>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMenuBar>
#include <QAction>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTableView>
#include <QHeaderView>
#include <QProgressDialog>
#include <QMessageBox>
#include <QTranslator>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    retranslateUI();
}

void MainWindow::setupUI() {
    // 设置键盘快捷键
    QShortcut *spaceShortcut = new QShortcut(Qt::Key_Space, this);
    connect(spaceShortcut, &QShortcut::activated, this, &MainWindow::togglePlayback);

    QShortcut *leftShortcut = new QShortcut(Qt::Key_Left, this);
    connect(leftShortcut, &QShortcut::activated, [this]() {
        m_player->seek(m_player->getCurrentTime() - 1);
    });

    QShortcut *rightShortcut = new QShortcut(Qt::Key_Right, this);
    connect(rightShortcut, &QShortcut::activated, [this]() {
        m_player->seek(m_player->getCurrentTime() + 1);
    });

    QShortcut *shiftLeftShortcut = new QShortcut(Qt::SHIFT | Qt::Key_Left, this);
    connect(shiftLeftShortcut, &QShortcut::activated, [this]() {
        m_player->seek(m_player->getCurrentTime() - 5);
    });

    QShortcut *shiftRightShortcut = new QShortcut(Qt::SHIFT | Qt::Key_Right, this);
    connect(shiftRightShortcut, &QShortcut::activated, [this]() {
        m_player->seek(m_player->getCurrentTime() + 5);
    });

    QShortcut *iShortcut = new QShortcut(Qt::Key_I, this);
    connect(iShortcut, &QShortcut::activated, this, &MainWindow::markIn);

    QShortcut *oShortcut = new QShortcut(Qt::Key_O, this);
    connect(oShortcut, &QShortcut::activated, this, &MainWindow::markOut);

    QShortcut *deleteShortcut = new QShortcut(Qt::Key_Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, &MainWindow::deleteSelected);

    QShortcut *ctrlEShortcut = new QShortcut(Qt::CTRL | Qt::Key_E, this);
    connect(ctrlEShortcut, &QShortcut::activated, this, &MainWindow::exportMerged);

    QShortcut *ctrlShiftEShortcut = new QShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_E, this);
    connect(ctrlShiftEShortcut, &QShortcut::activated, this, &MainWindow::exportIndividually);
    // Central Widget
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout *rootLayout = new QHBoxLayout(central);

    // Left Side: Player Area
    QVBoxLayout *playerAreaLayout = new QVBoxLayout();
    m_player = new MpvWidget(this);
    playerAreaLayout->addWidget(m_player, 1);

    // Scrubber
    QHBoxLayout *scrubberLayout = new QHBoxLayout();
    m_currentTimeLabel = new QLabel("00:00:00.000", this);
    scrubberLayout->addWidget(m_currentTimeLabel);

    m_timecodeInput = new QLineEdit(this);
    m_timecodeInput->setPlaceholderText("HH:MM:SS.xxx");
    m_timecodeInput->setFixedWidth(120);
    connect(m_timecodeInput, &QLineEdit::returnPressed, [this]() {
        QString timecode = m_timecodeInput->text().trimmed();
        // 解析时间码格式：HH:MM:SS.xxx
        QStringList parts = timecode.split(':');
        if (parts.size() == 3) {
            bool ok1, ok2, ok3;
            int hours = parts[0].toInt(&ok1);
            int minutes = parts[1].toInt(&ok2);
            QStringList secParts = parts[2].split('.');
            int seconds = secParts[0].toInt(&ok3);
            int milliseconds = 0;
            if (secParts.size() > 1) {
                milliseconds = secParts[1].left(3).toInt();
            }
            if (ok1 && ok2 && ok3) {
                double totalSeconds = hours * 3600 + minutes * 60 + seconds + milliseconds / 1000.0;
                m_player->seek(totalSeconds);
                m_timecodeInput->clear();
            }
        }
    });
    scrubberLayout->addWidget(m_timecodeInput);

    m_scrubber = new QSlider(Qt::Horizontal, this);
    m_scrubber->setRange(0, 1000);
    connect(m_scrubber, &QSlider::sliderPressed, [this](){ m_isUserSeeking = true; });
    connect(m_scrubber, &QSlider::sliderReleased, [this](){ m_isUserSeeking = false; });
    connect(m_scrubber, &QSlider::sliderMoved, this, &MainWindow::onSliderMoved);
    scrubberLayout->addWidget(m_scrubber);

    m_durationLabel = new QLabel("00:00:00.000", this);
    scrubberLayout->addWidget(m_durationLabel);
    playerAreaLayout->addLayout(scrubberLayout);

    // Player Controls
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    m_openFileBtn = new QPushButton(this);
    connect(m_openFileBtn, &QPushButton::clicked, this, &MainWindow::openFile);
    controlsLayout->addWidget(m_openFileBtn);

    controlsLayout->addSpacing(10);

    m_playPauseBtn = new QPushButton(this);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    controlsLayout->addWidget(m_playPauseBtn);

    // Volume
    controlsLayout->addSpacing(20);
    m_volumeLabel = new QLabel(this);
    controlsLayout->addWidget(m_volumeLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(100);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeSliderChanged);
    controlsLayout->addWidget(m_volumeSlider);

    controlsLayout->addStretch();

    m_markInBtn = new QPushButton(this);
    connect(m_markInBtn, &QPushButton::clicked, this, &MainWindow::markIn);
    controlsLayout->addWidget(m_markInBtn);

    m_markInLabel = new QLabel("--:--:--.---", this);
    controlsLayout->addWidget(m_markInLabel);

    m_markOutBtn = new QPushButton(this);
    connect(m_markOutBtn, &QPushButton::clicked, this, &MainWindow::markOut);
    controlsLayout->addWidget(m_markOutBtn);

    m_markOutLabel = new QLabel("--:--:--.---", this);
    controlsLayout->addWidget(m_markOutLabel);

    playerAreaLayout->addLayout(controlsLayout);
    rootLayout->addLayout(playerAreaLayout, 3);

    // Right Side: Queue Area
    QVBoxLayout *queueAreaLayout = new QVBoxLayout();
    m_queueLabel = new QLabel(this);
    queueAreaLayout->addWidget(m_queueLabel);

    m_clipModel = new ClipModel(this);
    m_queueView = new QTableView(this);
    m_queueView->setModel(m_clipModel);
    m_queueView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_queueView->setSelectionBehavior(QAbstractItemView::SelectRows);
    queueAreaLayout->addWidget(m_queueView);

    // Reorder Buttons
    QHBoxLayout *reorderLayout = new QHBoxLayout();
    m_upBtn = new QPushButton(this);
    connect(m_upBtn, &QPushButton::clicked, this, &MainWindow::moveUp);
    reorderLayout->addWidget(m_upBtn);

    m_downBtn = new QPushButton(this);
    connect(m_downBtn, &QPushButton::clicked, this, &MainWindow::moveDown);
    reorderLayout->addWidget(m_downBtn);
    queueAreaLayout->addLayout(reorderLayout);

    QHBoxLayout *queueButtonsLayout = new QHBoxLayout();
    m_deleteBtn = new QPushButton(this);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    queueButtonsLayout->addWidget(m_deleteBtn);
    queueAreaLayout->addLayout(queueButtonsLayout);
    
    // Export Actions
    queueAreaLayout->addSpacing(10);
    m_exportIndBtn = new QPushButton(this);
    connect(m_exportIndBtn, &QPushButton::clicked, this, &MainWindow::exportIndividually);
    queueAreaLayout->addWidget(m_exportIndBtn);

    m_exportMergeBtn = new QPushButton(this);
    connect(m_exportMergeBtn, &QPushButton::clicked, this, &MainWindow::exportMerged);
    queueAreaLayout->addWidget(m_exportMergeBtn);

    rootLayout->addLayout(queueAreaLayout, 1);

    // Player Signals
    connect(m_player, &MpvWidget::timeChanged, this, &MainWindow::onTimeChanged);
    connect(m_player, &MpvWidget::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &MpvWidget::volumeChanged, this, &MainWindow::onVolumeChanged);
}

void MainWindow::retranslateUI() {
    setWindowTitle(tr("Media Cutter"));
    
    // Menu
    menuBar()->clear();
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *openAction = fileMenu->addAction(tr("&Open File"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);
    QAction *resetDirAction = fileMenu->addAction(tr("Reset Default Directory"));
    connect(resetDirAction, &QAction::triggered, this, &MainWindow::resetDefaultDirectory);

    QMenu *langMenu = menuBar()->addMenu(tr("&Language"));
    QAction *zhAction = langMenu->addAction("简体中文");
    connect(zhAction, &QAction::triggered, this, [this]() { switchLanguage("zh_CN"); });
    QAction *enAction = langMenu->addAction("English");
    connect(enAction, &QAction::triggered, this, [this]() { switchLanguage(QString()); });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *toggleThemeAction = viewMenu->addAction(tr("Toggle Theme"));
    connect(toggleThemeAction, &QAction::triggered, this, &MainWindow::toggleTheme);

    // Pointers
    m_openFileBtn->setText(tr("Open File"));
    m_playPauseBtn->setText(tr("Play/Pause"));
    m_volumeLabel->setText(tr("Volume:"));
    m_markInBtn->setText(tr("Mark In"));
    m_markOutBtn->setText(tr("Mark Out"));
    m_upBtn->setText(tr("Move Up"));
    m_downBtn->setText(tr("Move Down"));
    m_deleteBtn->setText(tr("Delete Selected"));
    m_exportIndBtn->setText(tr("Export Separately"));
    m_exportMergeBtn->setText(tr("Export Merged"));
    m_queueLabel->setText(tr("Export Queue:"));
    m_clipModel->updateHeaders();
}

void MainWindow::openFile() {
    QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Open Media"), getLastDirectory(),
        tr("Media Files (*.mp4 *.mkv *.avi *.mov *.mp3 *.flac *.wav *.ogg *.aac *.wma *.mka *.m4a *.opus *.webm *.ts *.flv *.wmv);;Video Files (*.mp4 *.mkv *.avi *.mov *.webm *.ts *.flv *.wmv);;Audio Files (*.mp3 *.flac *.wav *.ogg *.aac *.wma *.mka *.m4a *.opus);;All Files (*)"));
    if (!fileNames.isEmpty()) {
        m_currentFilePath = fileNames.first();
        saveLastDirectory(m_currentFilePath);
        m_player->loadFile(m_currentFilePath);
        m_markIn = 0;
        m_markOut = -1;
        m_markInLabel->setText("--:--:--.---");
        m_markOutLabel->setText("--:--:--.---");
    }
}

void MainWindow::togglePlayback() {
    m_player->playPause();
}

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

void MainWindow::onVolumeChanged(int volume) {
    if (!m_volumeSlider->isSliderDown()) {
        m_volumeSlider->setValue(volume);
    }
}

void MainWindow::onSliderMoved(int position) {
    double duration = m_player->getDuration();
    if (duration > 0) {
        double seekTo = (position / 1000.0) * duration;
        m_player->seek(seekTo);
    }
}

void MainWindow::onVolumeSliderChanged(int value) {
    m_player->setVolume(value);
}

void MainWindow::formatTime(double seconds, QLabel *label) {
    int h = static_cast<int>(seconds / 3600);
    int m = static_cast<int>((seconds - h * 3600) / 60);
    int s = static_cast<int>(seconds - h * 3600 - m * 60);
    int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    label->setText(QString("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0')));
}

void MainWindow::markIn() {
    m_markIn = m_player->getCurrentTime();
    formatTime(m_markIn, m_markInLabel);
}

void MainWindow::markOut() {
    m_markOut = m_player->getCurrentTime();
    formatTime(m_markOut, m_markOutLabel);
    
    if (m_markOut > m_markIn && !m_currentFilePath.isEmpty()) {
        m_clipModel->addSegment(m_currentFilePath, m_markIn, m_markOut);
        // Reset marks for next segment
        m_markIn = m_markOut;
        m_markOut = -1;
        formatTime(m_markIn, m_markInLabel);
        m_markOutLabel->setText("--:--:--.---");
    }
}

void MainWindow::deleteSelected() {
    auto selection = m_queueView->selectionModel()->selectedRows();
    std::vector<int> rows;
    for (const auto &index : selection) {
        rows.push_back(index.row());
    }
    std::sort(rows.rbegin(), rows.rend());
    for (int row : rows) {
        m_clipModel->removeSegment(row);
    }
}

void MainWindow::moveUp() {
    auto selection = m_queueView->selectionModel()->selectedRows();
    if (selection.empty()) return;
    int row = selection.first().row();
    m_clipModel->moveUp(row);
    m_queueView->selectRow(row - 1);
}

void MainWindow::moveDown() {
    auto selection = m_queueView->selectionModel()->selectedRows();
    if (selection.empty()) return;
    int row = selection.first().row();
    m_clipModel->moveDown(row);
    m_queueView->selectRow(row + 1);
}

void MainWindow::exportMerged() {
    if (m_currentFilePath.isEmpty()) return;
    auto segments = m_clipModel->segments();
    if (segments.empty()) {
        QMessageBox::warning(this, tr("Empty Queue"), tr("No segments to export."));
        return;
    }

    QString filter = tr("Media Files (*.mp4 *.mkv *.avi *.mov *.mp3 *.flac *.wav *.ogg *.aac *.mka);;All Files (*)");
    QString output = QFileDialog::getSaveFileName(this, tr("Save Merged Media"), getLastDirectory() + "/merged_output.mp4", filter);
    if (output.isEmpty()) return;

    QProgressDialog *progress = new QProgressDialog(tr("Exporting merged media..."), tr("Cancel"), 0, static_cast<int>(segments.size()) + 1, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->show();

    FFmpegRunner *runner = new FFmpegRunner(this);
    connect(runner, &FFmpegRunner::progress, progress, [progress](int current, int total, const QString &status) {
        progress->setMaximum(total);
        progress->setValue(current);
        progress->setLabelText(status);
    });
    connect(runner, &FFmpegRunner::finished, this, [=](bool success, QString msg) {
        progress->close();
        if (success) {
            QMessageBox::information(this, tr("Export Success"), tr("Media exported successfully!"));
        } else {
            QMessageBox::critical(this, tr("Export Failed"), msg);
        }
        runner->deleteLater();
    });

    runner->cutAndMerge(m_currentFilePath, segments, output, true);
}

void MainWindow::exportIndividually() {
    if (m_currentFilePath.isEmpty()) return;
    auto segments = m_clipModel->segments();
    if (segments.empty()) {
        QMessageBox::warning(this, tr("Empty Queue"), tr("No segments to export."));
        return;
    }

    QString filter = tr("Media Files (*.mp4 *.mkv *.avi *.mov *.mp3 *.flac *.wav *.ogg *.aac *.mka);;All Files (*)");
    QString output = QFileDialog::getSaveFileName(this, tr("Save Individual Segments (Suffixes will be added)"), getLastDirectory() + "/segment.mp4", filter);
    if (output.isEmpty()) return;

    QProgressDialog *progress = new QProgressDialog(tr("Exporting individual segments..."), tr("Cancel"), 0, static_cast<int>(segments.size()) + 1, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->show();

    FFmpegRunner *runner = new FFmpegRunner(this);
    connect(runner, &FFmpegRunner::progress, progress, [progress](int current, int total, const QString &status) {
        progress->setMaximum(total);
        progress->setValue(current);
        progress->setLabelText(status);
    });
    connect(runner, &FFmpegRunner::finished, this, [=](bool success, QString msg) {
        progress->close();
        if (success) {
            QMessageBox::information(this, tr("Export Success"), tr("All segments exported successfully!"));
        } else {
            QMessageBox::critical(this, tr("Export Failed"), msg);
        }
        runner->deleteLater();
    });

    runner->cutAndMerge(m_currentFilePath, segments, output, false);
}

void MainWindow::switchLanguage(const QString &locale) {
    if (m_translatorInstalled) {
        qApp->removeTranslator(&m_translator);
        m_translatorInstalled = false;
    }

    if (!locale.isEmpty()) {
        // 先找资源里的 :/translations/，再尝试可执行目录旁边
        const QString name = QString("media-cutter_%1").arg(locale);
        if (m_translator.load(name, ":/translations") ||
            m_translator.load(name, QCoreApplication::applicationDirPath())) {
            qApp->installTranslator(&m_translator);
            m_translatorInstalled = true;
        }
    }

    retranslateUI();
}

void MainWindow::toggleTheme() {
    m_isDarkTheme = !m_isDarkTheme;

    QFile styleFile;
    if (m_isDarkTheme) {
        styleFile.setFileName(":/style.qss");
    } else {
        styleFile.setFileName(":/style-light.qss");
    }

    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        qApp->setStyleSheet(styleSheet);
    }

    retranslateUI();
}

QString MainWindow::getLastDirectory() const {
    QSettings settings("MediaCutter", "MediaCutter");
    return settings.value("lastOpenDir", QCoreApplication::applicationDirPath()).toString();
}

void MainWindow::saveLastDirectory(const QString &path) {
    QSettings settings("MediaCutter", "MediaCutter");
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
}

void MainWindow::resetDefaultDirectory() {
    QSettings settings("MediaCutter", "MediaCutter");
    settings.remove("lastOpenDir");
}
