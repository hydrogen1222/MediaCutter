#pragma once
#include <QDialog>
#include <QString>

class MpvWidget;
class FFmpegRunner;
class QLabel;
class QPushButton;
class QSlider;
class QProgressBar;
class QSpinBox;

// Modal dialog for the "Burn Subtitles" feature: permanently overlay (hardcode)
// an .ass subtitle track onto a video by re-encoding the video stream through
// libass. Workflow: pick the video, pick the .ass, preview how the styled
// subtitles look (rendered live by libass in an embedded mpv player — the same
// renderer the burn uses, so the preview matches the output), then Burn.
//
// Audio is stream-copied unchanged; only the video is re-encoded. The output
// framerate is configurable (default 30) so ASS effects (\move, \fad, transforms)
// animate smoothly even when the source is low-fps (e.g. a 5fps radio video).
class SubtitleBurnDialog : public QDialog {
    Q_OBJECT
public:
    explicit SubtitleBurnDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void chooseVideo();
    void chooseSubtitle();
    void togglePlayPause();
    void onPlayerTimeChanged(double t);
    void onPlayerDurationChanged(double d);
    void onPositionSliderPressed();
    void onPositionSliderReleased();
    void onPositionSliderMoved(int pos);
    void onVolumeChanged(int v);
    void doBurn();
    void cancelBurn();
    void onRunnerProgress(int cur, int total, const QString &status);
    void onRunnerFinished(bool success, const QString &msg);

private:
    void reloadPreview();          // (re)load video+subtitle into the player
    void updateBurnEnabled();
    QString defaultOutputPath() const;

    MpvWidget *m_player;

    // Inputs
    QPushButton *m_chooseVideoBtn = nullptr;
    QLabel      *m_videoPathLabel = nullptr;
    QString      m_videoPath;
    QPushButton *m_chooseSubBtn   = nullptr;
    QLabel      *m_subPathLabel   = nullptr;
    QString      m_subPath;

    // Transport
    QPushButton *m_playPauseBtn   = nullptr;
    QSlider     *m_positionSlider = nullptr;
    QLabel      *m_currentLabel   = nullptr;
    QLabel      *m_durationLabel  = nullptr;
    QSlider     *m_volumeSlider   = nullptr;

    // Options
    QSpinBox    *m_fpsBox         = nullptr;

    // Burn
    QPushButton *m_burnBtn        = nullptr;
    QPushButton *m_cancelBtn     = nullptr;
    QProgressBar *m_progressBar   = nullptr;
    QLabel      *m_statusLabel    = nullptr;

    double m_duration = 0.0;
    bool m_isUserSeeking = false;

    FFmpegRunner *m_runner = nullptr;
    bool m_encoding = false;
    bool m_cancelled = false;
};
