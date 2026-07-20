#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <vector>
#include <QProcess>
#include <QTemporaryDir>
#include "ClipModel.h"

class FFmpegRunner : public QObject {
    Q_OBJECT
public:
    explicit FFmpegRunner(QObject *parent = nullptr);
    void cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output, bool mergeAfterCut = true, bool hasVideo = true);
    // Abort the in-progress export. Kills the running FFmpeg process and
    // emits finished(false, ...). Safe to call when nothing is running.
    void cancel();

    // Synthesize a video from one static cover image + one audio file (the
    // "radio video" feature). The image is looped for the whole (optionally
    // trimmed [startSec,endSec]) audio length and re-encoded. framing is one
    // of: "Native", "1080x1080", "1920x1080", "1280x720". Emits
    // progress(percent,100,...) during the encode and finished() when done.
    void createRadioVideo(const QString &imagePath, const QString &audioPath,
                          double startSec, double endSec, const QString &framing,
                          const QString &outputPath);

    // Burn (hardcode) an .ass subtitle track into a video by re-encoding the
    // video stream with a libass overlay (the "burn hard subtitles" feature).
    // Audio is stream-copied unchanged. fps sets the output framerate — use a
    // normal value (e.g. 30) so ASS effects (\move, \fad, transforms) animate
    // smoothly even when the source is low-fps (e.g. a 5fps radio video).
    // crf is the libx264 CRF (lower = better; ~18 is visually lossless) and also
    // the QP target when a hardware encoder is used. The encoder is chosen
    // automatically: a working hardware encoder (NVENC/QuickSync/AMF/VAAPI) if
    // one is usable, otherwise libx264 (-preset veryfast, all CPU cores) or the
    // mpeg4 fallback. Emits progress + finished like above.
    void burnSubtitles(const QString &videoPath, const QString &assPath,
                       const QString &outputPath, int fps, int crf);

signals:
    void progress(int current, int total, const QString &status);
    void finished(bool success, const QString &message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onWatchdogTimeout();

private:
    void runNextStep();
    void finalizeIndividualExport();
    // Emits finished() exactly once per export. QProcess emits both
    // errorOccurred() and finished() when a process dies from a signal
    // (crash or our own cancel()/kill()), so without this guard the
    // handlers would emit finished() twice.
    void emitFinished(bool success, const QString &message);
    // Parses ffmpeg's `-progress pipe:1` output (key=value lines on stdout)
    // into percentage progress for any single-shot encode (radio video or
    // subtitle burn). Progress comes on stdout, so stderr stays intact for
    // summarizeFfmpegError() on failure — no accumulation or drain-ordering
    // hazard.
    void parseEncodeProgress();
    // Implementation behind createRadioVideo()/burnSubtitles(): does the real
    // work. forceSoftware bypasses hardware-encoder selection (used by the
    // startup watchdog when a HW encoder that passed its probe deadlocks on
    // the real encode, since libx264 always works).
    void createRadioVideoImpl(const QString &imagePath, const QString &audioPath,
                              double startSec, double endSec, const QString &framing,
                              const QString &outputPath, bool forceSoftware);
    void burnSubtitlesImpl(const QString &videoPath, const QString &assPath,
                           const QString &outputPath, int fps, int crf, bool forceSoftware);

    QString m_input;
    std::vector<Segment> m_segments;
    QString m_output;
    int m_currentIndex = 0;
    QProcess *m_process;
    QTemporaryDir m_tempDir;
    QStringList m_tempFiles;
    bool m_isMerging = false;
    bool m_mergeAfterCut = true;
    bool m_hasVideo = true;
    bool m_finished = false;

    // Single-shot encode state (shared by createRadioVideo & burnSubtitles).
    bool m_singleShot = false;      // true => no cut/merge step-loop
    double m_progressTotal = 0.0;   // encode duration (progress denominator)
    QString m_successMessage;       // emitted on success
    QString m_progressLabel;       // status text shown during progress
    QByteArray m_progressStdoutBuf; // unprocessed tail of -progress stdout

    // Startup watchdog for single-shot encodes (radio video / subtitle burn).
    // A hardware encoder that passed the synthetic probe can still deadlock on
    // the real encode (observed: AMF on AMD 780M shows no progress, no CPU/GPU
    // use). If no output frame arrives within the window, we kill the run and
    // retry once on libx264, then disable HW for the rest of the session so the
    // next export doesn't re-trigger the hang.
    QTimer *m_watchdog;
    bool m_forceSoftware = false;  // current attempt bypasses HW selection
    bool m_gotProgress = false;    // a real out_time_us has been seen
    bool m_retrying = false;       // watchdog killed the HW run; retry pending
    std::function<void()> m_retryWithSoftware; // replays the encode on libx264
};
