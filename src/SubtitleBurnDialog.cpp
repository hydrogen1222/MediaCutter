#include "SubtitleBurnDialog.h"
#include "MpvWidget.h"
#include "FFmpegRunner.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QSpinBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QSettings>
#include <QCloseEvent>
#include <QCoreApplication>

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

SubtitleBurnDialog::SubtitleBurnDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Burn Subtitles"));
    setMinimumWidth(620);

    m_player = new MpvWidget(this);

    auto *root = new QVBoxLayout(this);

    // ---- Preview (video + styled .ass overlay rendered by libass) ----
    root->addWidget(m_player, 1);

    auto *transportRow = new QHBoxLayout();
    m_playPauseBtn = new QPushButton(tr("Play"), this);
    m_playPauseBtn->setFixedWidth(80);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &SubtitleBurnDialog::togglePlayPause);
    m_currentLabel = new QLabel("00:00:00.000", this);
    m_currentLabel->setFixedWidth(110);
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 1000);
    m_durationLabel = new QLabel("00:00:00.000", this);
    m_durationLabel->setFixedWidth(110);
    connect(m_positionSlider, &QSlider::sliderPressed, this, &SubtitleBurnDialog::onPositionSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &SubtitleBurnDialog::onPositionSliderReleased);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &SubtitleBurnDialog::onPositionSliderMoved);
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
    connect(m_volumeSlider, &QSlider::valueChanged, this, &SubtitleBurnDialog::onVolumeChanged);
    volRow->addWidget(volLabel);
    volRow->addWidget(m_volumeSlider);
    volRow->addStretch();
    root->addLayout(volRow);

    // ---- Inputs ----
    auto *videoRow = new QHBoxLayout();
    m_chooseVideoBtn = new QPushButton(tr("Choose Video..."), this);
    connect(m_chooseVideoBtn, &QPushButton::clicked, this, &SubtitleBurnDialog::chooseVideo);
    m_videoPathLabel = new QLabel(tr("(no video)"), this);
    m_videoPathLabel->setStyleSheet("color: gray;");
    videoRow->addWidget(m_chooseVideoBtn);
    videoRow->addWidget(m_videoPathLabel, 1);
    root->addLayout(videoRow);

    auto *subRow = new QHBoxLayout();
    m_chooseSubBtn = new QPushButton(tr("Choose Subtitle (.ass)..."), this);
    connect(m_chooseSubBtn, &QPushButton::clicked, this, &SubtitleBurnDialog::chooseSubtitle);
    m_subPathLabel = new QLabel(tr("(no subtitle)"), this);
    m_subPathLabel->setStyleSheet("color: gray;");
    subRow->addWidget(m_chooseSubBtn);
    subRow->addWidget(m_subPathLabel, 1);
    root->addLayout(subRow);

    // ---- Options ----
    auto *optRow = new QHBoxLayout();
    auto *fpsLabel = new QLabel(tr("Output FPS:"), this);
    m_fpsBox = new QSpinBox(this);
    m_fpsBox->setRange(1, 120);
    m_fpsBox->setValue(30);
    m_fpsBox->setToolTip(tr("Output framerate. A normal value (e.g. 30) keeps subtitle effects (move, fade, transforms) smooth even when the source video is low-framerate (e.g. a radio video)."));
    optRow->addWidget(fpsLabel);
    optRow->addWidget(m_fpsBox);
    optRow->addStretch();
    root->addLayout(optRow);

    // ---- Burn ----
    auto *burnRow = new QHBoxLayout();
    m_burnBtn = new QPushButton(tr("Burn"), this);
    m_burnBtn->setEnabled(false);
    connect(m_burnBtn, &QPushButton::clicked, this, &SubtitleBurnDialog::doBurn);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SubtitleBurnDialog::cancelBurn);
    burnRow->addWidget(m_burnBtn);
    burnRow->addWidget(m_progressBar, 1);
    burnRow->addWidget(m_cancelBtn);
    root->addLayout(burnRow);

    m_statusLabel = new QLabel(QString(), this);
    root->addWidget(m_statusLabel);

    // ---- Player signals ----
    connect(m_player, &MpvWidget::timeChanged, this, &SubtitleBurnDialog::onPlayerTimeChanged);
    connect(m_player, &MpvWidget::durationChanged, this, &SubtitleBurnDialog::onPlayerDurationChanged);
}

void SubtitleBurnDialog::chooseVideo() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Video"), lastDir(),
        tr("Video Files (*.mp4 *.mkv *.mov *.webm *.avi *.ts *.flv *.wmv);;All Files (*)"));
    if (path.isEmpty()) return;
    m_videoPath = path;
    m_videoPathLabel->setStyleSheet(QString());
    m_videoPathLabel->setText(QFileInfo(path).fileName());
    saveLastDir(path);
    reloadPreview();
    updateBurnEnabled();
}

void SubtitleBurnDialog::chooseSubtitle() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Subtitle"), lastDir(),
        tr("Subtitles (*.ass *.ssa *.srt *.sub);;All Files (*)"));
    if (path.isEmpty()) return;
    m_subPath = path;
    m_subPathLabel->setStyleSheet(QString());
    m_subPathLabel->setText(QFileInfo(path).fileName());
    saveLastDir(path);
    reloadPreview();
    updateBurnEnabled();
}

void SubtitleBurnDialog::reloadPreview() {
    if (m_videoPath.isEmpty()) return;
    // setExternalSubtitle MUST precede loadFile (mpv binds sub-files at open).
    if (!m_subPath.isEmpty()) {
        m_player->setExternalSubtitle(m_subPath);
    }
    m_player->loadFile(m_videoPath);
}

void SubtitleBurnDialog::togglePlayPause() {
    m_player->playPause();
}

void SubtitleBurnDialog::onPlayerTimeChanged(double t) {
    if (!m_isUserSeeking && m_duration > 0) {
        m_positionSlider->setValue(static_cast<int>((t / m_duration) * 1000));
    }
    m_currentLabel->setText(formatHMS(t));
}

void SubtitleBurnDialog::onPlayerDurationChanged(double d) {
    m_duration = d;
    m_durationLabel->setText(formatHMS(d));
}

void SubtitleBurnDialog::onPositionSliderPressed() {
    m_isUserSeeking = true;
}

void SubtitleBurnDialog::onPositionSliderReleased() {
    m_isUserSeeking = false;
}

void SubtitleBurnDialog::onPositionSliderMoved(int pos) {
    if (m_duration > 0) {
        m_player->seek((pos / 1000.0) * m_duration);
    }
}

void SubtitleBurnDialog::onVolumeChanged(int v) {
    m_player->setVolume(v);
}

void SubtitleBurnDialog::updateBurnEnabled() {
    bool ok = !m_videoPath.isEmpty() && !m_subPath.isEmpty() && !m_encoding;
    m_burnBtn->setEnabled(ok);
}

QString SubtitleBurnDialog::defaultOutputPath() const {
    const QString base = m_videoPath.isEmpty()
        ? QStringLiteral("video")
        : QFileInfo(m_videoPath).baseName();
    return lastDir() + "/" + base + "_sub.mp4";
}

void SubtitleBurnDialog::doBurn() {
    if (m_videoPath.isEmpty() || m_subPath.isEmpty()) return;

    const QString output = QFileDialog::getSaveFileName(
        this, tr("Save Burned Video"), defaultOutputPath(),
        tr("Video Files (*.mp4 *.mkv *.mov *.webm);;All Files (*)"));
    if (output.isEmpty()) return;
    saveLastDir(output);

    m_encoding = true;
    m_cancelled = false;
    m_burnBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Burning subtitles..."));

    m_runner = new FFmpegRunner(this);
    connect(m_runner, &FFmpegRunner::progress, this, &SubtitleBurnDialog::onRunnerProgress);
    connect(m_runner, &FFmpegRunner::finished, this, &SubtitleBurnDialog::onRunnerFinished);

    // Pass mpv's known video duration as the progress denominator (avoids a
    // redundant ffmpeg probe; consistent with the radio-video path).
    m_runner->burnSubtitles(m_videoPath, m_subPath, output, m_fpsBox->value(), 18,
                            m_player->getDuration());
}

void SubtitleBurnDialog::onRunnerProgress(int cur, int total, const QString &status) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(cur);
    m_statusLabel->setText(status);
}

void SubtitleBurnDialog::onRunnerFinished(bool success, const QString &msg) {
    const bool wasCancelled = m_cancelled;
    m_encoding = false;
    m_cancelled = false;
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    m_cancelBtn->setEnabled(false);
    updateBurnEnabled();

    if (wasCancelled) {
        m_statusLabel->setText(tr("Cancelled."));
        m_progressBar->setValue(0);
        return;
    }
    if (success) {
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("Done"));
        QMessageBox::information(this, tr("Burn Success"),
                                 tr("Subtitles burned successfully!"));
    } else {
        m_statusLabel->setText(tr("Burn failed."));
        QMessageBox::critical(this, tr("Burn Failed"), msg);
    }
}

void SubtitleBurnDialog::cancelBurn() {
    if (!m_encoding) return;
    m_cancelled = true;
    m_statusLabel->setText(tr("Cancelling..."));
    if (m_runner) m_runner->cancel();
}

void SubtitleBurnDialog::closeEvent(QCloseEvent *e) {
    if (m_encoding) {
        // Don't allow closing mid-burn; route through Cancel instead.
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}
