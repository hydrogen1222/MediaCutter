#include "RadioVideoDialog.h"
#include "AudioPlayerWidget.h"
#include "FFmpegRunner.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QMessageBox>
#include <QPixmap>
#include <QFileInfo>
#include <QSettings>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFrame>

namespace {
QString formatHMS(double seconds) {
    if (seconds < 0) seconds = 0;
    const int h = static_cast<int>(seconds / 3600);
    const int m = static_cast<int>((seconds - h * 3600) / 60);
    const int s = static_cast<int>(seconds - h * 3600 - m * 60);
    const int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    return QString("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QString lastDir() {
    QSettings settings("MediaCutter", "MediaCutter");
    return settings.value("lastOpenDir", QCoreApplication::applicationDirPath()).toString();
}
void saveLastDir(const QString &path) {
    QSettings settings("MediaCutter", "MediaCutter");
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
}
} // namespace

RadioVideoDialog::RadioVideoDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Create Radio Video"));
    setMinimumWidth(560);

    m_player = new AudioPlayerWidget(this);

    auto *root = new QVBoxLayout(this);

    // ---- Cover image ----
    auto *imageRow = new QHBoxLayout();
    m_chooseImageBtn = new QPushButton(tr("Choose Image..."), this);
    connect(m_chooseImageBtn, &QPushButton::clicked, this, &RadioVideoDialog::chooseImage);
    m_imagePathLabel = new QLabel(tr("(no image)"), this);
    m_imagePathLabel->setStyleSheet("color: gray;");
    imageRow->addWidget(m_chooseImageBtn);
    imageRow->addWidget(m_imagePathLabel, 1);
    root->addLayout(imageRow);

    m_imagePreview = new QLabel(this);
    m_imagePreview->setMinimumSize(180, 180);
    m_imagePreview->setMaximumHeight(180);
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setFrameShape(QFrame::Box);
    m_imagePreview->setText(tr("Cover preview"));
    m_imagePreview->setStyleSheet("color: gray; background: #1a1a1a;");
    root->addWidget(m_imagePreview);

    // ---- Audio ----
    auto *audioRow = new QHBoxLayout();
    m_chooseAudioBtn = new QPushButton(tr("Choose Audio..."), this);
    connect(m_chooseAudioBtn, &QPushButton::clicked, this, &RadioVideoDialog::chooseAudio);
    m_audioPathLabel = new QLabel(tr("(no audio)"), this);
    m_audioPathLabel->setStyleSheet("color: gray;");
    audioRow->addWidget(m_chooseAudioBtn);
    audioRow->addWidget(m_audioPathLabel, 1);
    root->addLayout(audioRow);

    // ---- Transport ----
    auto *transportRow = new QHBoxLayout();
    m_playPauseBtn = new QPushButton(tr("Play"), this);
    m_playPauseBtn->setFixedWidth(80);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &RadioVideoDialog::togglePlayPause);
    m_currentLabel = new QLabel("00:00:00.000", this);
    m_currentLabel->setFixedWidth(110);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 1000);
    m_durationLabel = new QLabel("00:00:00.000", this);
    m_durationLabel->setFixedWidth(110);
    connect(m_positionSlider, &QSlider::sliderPressed, this, &RadioVideoDialog::onPositionSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &RadioVideoDialog::onPositionSliderReleased);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &RadioVideoDialog::onPositionSliderMoved);
    transportRow->addWidget(m_playPauseBtn);
    transportRow->addWidget(m_currentLabel);
    transportRow->addWidget(m_positionSlider, 1);
    transportRow->addWidget(m_durationLabel);
    root->addLayout(transportRow);

    auto *volRow = new QHBoxLayout();
    auto *volLabel = new QLabel(tr("Volume:"), this);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(120);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &RadioVideoDialog::onVolumeChanged);
    volRow->addWidget(volLabel);
    volRow->addWidget(m_volumeSlider);
    volRow->addStretch();
    root->addLayout(volRow);

    // ---- Mark In / Out ----
    auto *marksRow = new QHBoxLayout();
    m_markInBtn = new QPushButton(tr("Mark In"), this);
    m_markOutBtn = new QPushButton(tr("Mark Out"), this);
    m_resetMarksBtn = new QPushButton(tr("Reset Marks"), this);
    connect(m_markInBtn, &QPushButton::clicked, this, &RadioVideoDialog::markIn);
    connect(m_markOutBtn, &QPushButton::clicked, this, &RadioVideoDialog::markOut);
    connect(m_resetMarksBtn, &QPushButton::clicked, this, &RadioVideoDialog::resetMarks);
    m_markInLabel = new QLabel("--:--:--.---", this);
    m_markOutLabel = new QLabel("--:--:--.---", this);
    marksRow->addWidget(m_markInBtn);
    marksRow->addWidget(m_markInLabel);
    marksRow->addSpacing(12);
    marksRow->addWidget(m_markOutBtn);
    marksRow->addWidget(m_markOutLabel);
    marksRow->addSpacing(12);
    marksRow->addWidget(m_resetMarksBtn);
    marksRow->addStretch();
    root->addLayout(marksRow);

    // ---- Framing ----
    auto *framingRow = new QHBoxLayout();
    auto *framingLabel = new QLabel(tr("Resolution:"), this);
    m_framingCombo = new QComboBox(this);
    m_framingCombo->addItem(tr("1080x1080 (Square)"), QStringLiteral("1080x1080"));
    m_framingCombo->addItem(tr("Native (no resize)"), QStringLiteral("Native"));
    m_framingCombo->addItem(tr("1920x1080 (16:9)"), QStringLiteral("1920x1080"));
    m_framingCombo->addItem(tr("1280x720 (16:9)"), QStringLiteral("1280x720"));
    framingRow->addWidget(framingLabel);
    framingRow->addWidget(m_framingCombo);
    framingRow->addStretch();
    root->addLayout(framingRow);

    // ---- Export ----
    auto *exportRow = new QHBoxLayout();
    m_exportBtn = new QPushButton(tr("Export"), this);
    m_exportBtn->setEnabled(false);
    connect(m_exportBtn, &QPushButton::clicked, this, &RadioVideoDialog::doExport);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &RadioVideoDialog::cancelExport);
    exportRow->addWidget(m_exportBtn);
    exportRow->addWidget(m_progressBar, 1);
    exportRow->addWidget(m_cancelBtn);
    root->addLayout(exportRow);

    m_statusLabel = new QLabel(QString(), this);
    root->addWidget(m_statusLabel);

    // ---- Player signals ----
    connect(m_player, &AudioPlayerWidget::timeChanged, this, &RadioVideoDialog::onPlayerTimeChanged);
    connect(m_player, &AudioPlayerWidget::durationChanged, this, &RadioVideoDialog::onPlayerDurationChanged);
    connect(m_player, &AudioPlayerWidget::playingChanged, this, &RadioVideoDialog::onPlayerPlayingChanged);
}

void RadioVideoDialog::chooseImage() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Cover Image"), lastDir(),
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.webp *.tiff);;All Files (*)"));
    if (path.isEmpty()) return;
    QPixmap pix(path);
    if (pix.isNull()) {
        QMessageBox::warning(this, tr("Invalid Image"), tr("Could not load the selected image file."));
        return;
    }
    m_imagePath = path;
    m_imagePathLabel->setStyleSheet(QString());
    m_imagePathLabel->setText(QFileInfo(path).fileName());
    m_imagePreview->setStyleSheet("background: #1a1a1a;");
    m_imagePreview->setText(QString());
    m_imagePreview->setPixmap(pix.scaled(m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    saveLastDir(path);
    updateExportEnabled();
}

void RadioVideoDialog::chooseAudio() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Audio File"), lastDir(),
        tr("Audio Files (*.mp3 *.flac *.wav *.ogg *.aac *.m4a *.opus *.wma *.mka);;All Files (*)"));
    if (path.isEmpty()) return;
    m_audioPath = path;
    m_audioPathLabel->setStyleSheet(QString());
    m_audioPathLabel->setText(QFileInfo(path).fileName());
    // Load paused so picking a file doesn't blast audio; user presses Play.
    m_player->setPaused(true);
    m_player->loadFile(path);
    saveLastDir(path);
    updateExportEnabled();
}

void RadioVideoDialog::togglePlayPause() {
    m_player->playPause();
}

void RadioVideoDialog::onPlayerTimeChanged(double t) {
    if (!m_isUserSeeking && m_duration > 0) {
        m_positionSlider->setValue(static_cast<int>((t / m_duration) * 1000));
    }
    m_currentLabel->setText(formatHMS(t));

    // Loop the marked region if both marks are set (preview the exact clip).
    if (m_markOut >= 0 && m_markOut > m_markIn) {
        if (t >= m_markOut || t < m_markIn - 0.05) {
            m_player->seek(m_markIn);
        }
    }
}

void RadioVideoDialog::onPlayerDurationChanged(double d) {
    m_duration = d;
    m_durationLabel->setText(formatHMS(d));
}

void RadioVideoDialog::onPlayerPlayingChanged(bool playing) {
    m_playPauseBtn->setText(playing ? tr("Pause") : tr("Play"));
}

void RadioVideoDialog::onPositionSliderPressed() {
    m_isUserSeeking = true;
}

void RadioVideoDialog::onPositionSliderReleased() {
    m_isUserSeeking = false;
}

void RadioVideoDialog::onPositionSliderMoved(int pos) {
    if (m_duration > 0) {
        m_player->seek((pos / 1000.0) * m_duration);
    }
}

void RadioVideoDialog::onVolumeChanged(int v) {
    m_player->setVolume(v);
}

void RadioVideoDialog::markIn() {
    setMarkIn(m_player->getCurrentTime());
}

void RadioVideoDialog::markOut() {
    setMarkOut(m_player->getCurrentTime());
}

void RadioVideoDialog::setMarkIn(double t) {
    m_markIn = qMax(0.0, t);
    m_markInLabel->setText(formatHMS(m_markIn));
    // Keep the pair ordered: if Out is set but now before In, clear Out.
    if (m_markOut >= 0 && m_markOut <= m_markIn) {
        m_markOut = -1.0;
        m_markOutLabel->setText("--:--:--.---");
    }
    updateExportEnabled();
}

void RadioVideoDialog::setMarkOut(double t) {
    if (t <= m_markIn) {
        QMessageBox::information(this, tr("Mark Out"),
            tr("Mark Out must be after Mark In (%1).").arg(formatHMS(m_markIn)));
        return;
    }
    m_markOut = t;
    m_markOutLabel->setText(formatHMS(m_markOut));
    updateExportEnabled();
}

void RadioVideoDialog::resetMarks() {
    m_markIn = 0.0;
    m_markOut = -1.0;
    m_markInLabel->setText("--:--:--.---");
    m_markOutLabel->setText("--:--:--.---");
    updateExportEnabled();
}

void RadioVideoDialog::updateExportEnabled() {
    bool ok = !m_imagePath.isEmpty() && !m_audioPath.isEmpty() && !m_encoding;
    if (ok && m_markOut >= 0 && m_markOut <= m_markIn + 0.001) ok = false;
    m_exportBtn->setEnabled(ok);
}

QString RadioVideoDialog::defaultOutputPath() const {
    const QString base = m_audioPath.isEmpty()
        ? QStringLiteral("radio_video")
        : QFileInfo(m_audioPath).baseName();
    return lastDir() + "/" + base + "_radio.mp4";
}

void RadioVideoDialog::doExport() {
    if (m_imagePath.isEmpty() || m_audioPath.isEmpty()) return;

    const QString output = QFileDialog::getSaveFileName(
        this, tr("Save Radio Video"), defaultOutputPath(),
        tr("Video Files (*.mp4 *.mkv *.mov *.webm);;All Files (*)"));
    if (output.isEmpty()) return;
    saveLastDir(output);

    m_encoding = true;
    m_cancelled = false;
    m_exportBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Creating radio video..."));

    m_runner = new FFmpegRunner(this);
    connect(m_runner, &FFmpegRunner::progress, this, &RadioVideoDialog::onRunnerProgress);
    connect(m_runner, &FFmpegRunner::finished, this, &RadioVideoDialog::onRunnerFinished);

    const double endSec = (m_markOut < 0) ? -1.0 : m_markOut;
    // Pass mpv's known audio duration so FFmpegRunner can cap the output with
    // -t and show real progress, instead of re-probing (which fails for ASF/WMA
    // radio rips that report "Duration: N/A" - the blind-0% bug).
    m_runner->createRadioVideo(m_imagePath, m_audioPath, m_markIn, endSec,
                               m_framingCombo->currentData().toString(), output,
                               m_player->getDuration());
}

void RadioVideoDialog::onRunnerProgress(int cur, int total, const QString &status) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(cur);
    m_statusLabel->setText(status);
}

void RadioVideoDialog::onRunnerFinished(bool success, const QString &msg) {
    const bool wasCancelled = m_cancelled;
    m_encoding = false;
    m_cancelled = false;
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    m_cancelBtn->setEnabled(false);
    updateExportEnabled();

    if (wasCancelled) {
        m_statusLabel->setText(tr("Cancelled."));
        m_progressBar->setValue(0);
        return;
    }
    if (success) {
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("Done"));
        QMessageBox::information(this, tr("Export Success"),
                                 tr("Radio video created successfully!"));
    } else {
        m_statusLabel->setText(tr("Export failed."));
        QMessageBox::critical(this, tr("Export Failed"), msg);
    }
}

void RadioVideoDialog::cancelExport() {
    if (!m_encoding) return;
    m_cancelled = true;
    m_statusLabel->setText(tr("Cancelling..."));
    if (m_runner) m_runner->cancel();
}

void RadioVideoDialog::closeEvent(QCloseEvent *e) {
    if (m_encoding) {
        // Don't allow closing mid-encode; route through Cancel instead.
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}
