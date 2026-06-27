#pragma once
#include <QDialog>
#include <QString>

class AudioPlayerWidget;
class FFmpegRunner;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QProgressBar;

// Modal dialog for the "Create Radio Video" feature: combine one static cover
// image with one (optionally trimmed) audio file into a video whose video
// stream is the looping image and whose audio stream is the soundtrack. The
// audio is previewed with a libmpv AudioPlayerWidget; Mark In/Out define an
// optional trim. Export runs FFmpegRunner::createRadioVideo (the only
// re-encoding path in the app). Opened from MainWindow's File menu.
class RadioVideoDialog : public QDialog {
    Q_OBJECT
public:
    explicit RadioVideoDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void chooseImage();
    void chooseAudio();
    void togglePlayPause();
    void onPlayerTimeChanged(double t);
    void onPlayerDurationChanged(double d);
    void onPlayerPlayingChanged(bool playing);
    void onPositionSliderPressed();
    void onPositionSliderReleased();
    void onPositionSliderMoved(int pos);
    void onVolumeChanged(int v);
    void markIn();
    void markOut();
    void resetMarks();
    void doExport();
    void cancelExport();
    void onRunnerProgress(int cur, int total, const QString &status);
    void onRunnerFinished(bool success, const QString &msg);

private:
    void setMarkIn(double t);
    void setMarkOut(double t);
    void updateExportEnabled();
    QString defaultOutputPath() const;

    AudioPlayerWidget *m_player;

    // Image
    QPushButton *m_chooseImageBtn = nullptr;
    QLabel      *m_imagePreview  = nullptr;
    QLabel      *m_imagePathLabel = nullptr;
    QString      m_imagePath;

    // Audio
    QPushButton *m_chooseAudioBtn = nullptr;
    QLabel      *m_audioPathLabel = nullptr;
    QString      m_audioPath;

    // Transport
    QPushButton *m_playPauseBtn   = nullptr;
    QSlider     *m_positionSlider = nullptr;
    QLabel      *m_currentLabel   = nullptr;
    QLabel      *m_durationLabel  = nullptr;
    QSlider     *m_volumeSlider   = nullptr;

    // Marks
    QPushButton *m_markInBtn   = nullptr;
    QPushButton *m_markOutBtn   = nullptr;
    QPushButton *m_resetMarksBtn = nullptr;
    QLabel      *m_markInLabel  = nullptr;
    QLabel      *m_markOutLabel = nullptr;
    double m_markIn = 0.0;
    double m_markOut = -1.0;   // -1 = unset => whole file
    double m_duration = 0.0;
    bool m_isUserSeeking = false;

    // Framing + export
    QComboBox   *m_framingCombo = nullptr;
    QPushButton *m_exportBtn    = nullptr;
    QPushButton *m_cancelBtn   = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel      *m_statusLabel  = nullptr;

    FFmpegRunner *m_runner = nullptr;
    bool m_encoding = false;
    bool m_cancelled = false;
};
