#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
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
    // audioDurationHint: the audio length in seconds if the caller already
    // knows it (mpv decoded the file and reported it). Used as the progress
    // denominator and to cap the output with -t. When 0 (unknown) it is
    // re-probed via `ffmpeg -i`; if that also fails - some ASF/WMA radio rips
    // report "Duration: N/A" - the encode falls back to -shortest and no
    // progress % is shown (the blind-0% bug). Prefer passing the hint.
    void createRadioVideo(const QString &imagePath, const QString &audioPath,
                          double startSec, double endSec, const QString &framing,
                          const QString &outputPath, double audioDurationHint = 0.0);

    // Burn (hardcode) an .ass subtitle track into a video by re-encoding the
    // video stream with a libass overlay (the "burn hard subtitles" feature).
    // Audio is stream-copied unchanged. fps sets the output framerate — use a
    // normal value (e.g. 30) so ASS effects (\move, \fad, transforms) animate
    // smoothly even when the source is low-fps (e.g. a 5fps radio video).
    // crf is the libx264 CRF (lower = better; ~18 is visually lossless) and also
    // the QP target when a hardware encoder is used. The encoder is chosen
    // automatically: a working hardware encoder (NVENC/QuickSync/VAAPI) if one is
    // usable, otherwise libx264 (-preset veryfast, all CPU cores) or the mpeg4
    // fallback. (AMF is intentionally skipped - see detectHwEncoder.) A
    // sliding-window watchdog also retries on libx264 if a hardware encoder
    // stalls mid-encode, and reports failure if a software encode stalls - see
    // onWatchdogTimeout. Emits progress + finished like above.
    // videoDurationHint: the input video's length in seconds if the caller
    // already knows it (mpv). Used as the progress denominator. 0 => re-probe.
    void burnSubtitles(const QString &videoPath, const QString &assPath,
                       const QString &outputPath, int fps, int crf,
                       double videoDurationHint = 0.0);

signals:
    void progress(int current, int total, const QString &status);
    void finished(bool success, const QString &message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onWatchdogTimeout();
    // Drains ffmpeg's stderr as it arrives: accumulates it for
    // summarizeFfmpegError() on failure, and (for single-shot encodes) echoes
    // each chunk to the console so we can see where ffmpeg is when it stalls -
    // the UI shows only parsed progress, so without this a hung ffmpeg is a
    // silent 0% with no clue why.
    void onFfmpegStderr();

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
    // watchdog when a HW encoder that passed its probe deadlocks on the real
    // encode, since libx264 always works). durationHint is the caller-known
    // media length (audio for radio, video for burn); 0 => re-probe.
    void createRadioVideoImpl(const QString &imagePath, const QString &audioPath,
                              double startSec, double endSec, const QString &framing,
                              const QString &outputPath, bool forceSoftware,
                              double audioDurationHint);
    void burnSubtitlesImpl(const QString &videoPath, const QString &assPath,
                           const QString &outputPath, int fps, int crf, bool forceSoftware,
                           double videoDurationHint);

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
    QByteArray m_stderrBuf;         // ffmpeg's stderr, drained live (for errors + diagnostics)
    int m_lastLoggedPct = -1;       // last progress % written to console (throttle)

    // Sliding-window stall watchdog for single-shot encodes (radio video /
    // subtitle burn). m_watchdog is a RECURRING timer (WATCHDOG_POLL_MS); each
    // tick checks m_lastProgressAt. The latter is reset whenever out_time_us
    // strictly advances (parseEncodeProgress), so a healthy encode - which keeps
    // emitting advancing frames - never trips it. If no frame advances for
    // WATCHDOG_STALL_MS at ANY point (a first-frame deadlock OR a mid-encode
    // hang), we kill the run: retry once on libx264 if a HW encoder was in use
    // (and disable HW for the rest of the session), else report failure. The
    // old single-shot watchdog disarmed on the first frame, so a hang that
    // started after frame 1 was invisible - this closes that gap. m_lastOutTimeSec
    // makes the "strictly advances" check exact, so an encoder that stays alive
    // spamming the SAME out_time_us (the AMF 0-time symptom) still trips it.
    QTimer *m_watchdog;
    QElapsedTimer m_lastProgressAt; // reset on each advancing frame; stall = expired
    double m_lastOutTimeSec = -1.0; // last advancing out_time_us/1e6 (-1 = none yet)
    bool m_forceSoftware = false;  // current attempt bypasses HW selection
    bool m_usedHw = false;        // current attempt actually uses a HW encoder
    bool m_gotProgress = false;    // a real out_time_us has been seen (diagnostics)
    bool m_retrying = false;       // watchdog killed the HW run; retry pending
    bool m_watchdogKilled = false; // watchdog killed this run; suppress the "crashed" emit
    std::function<void()> m_retryWithSoftware; // replays the encode on libx264
};
