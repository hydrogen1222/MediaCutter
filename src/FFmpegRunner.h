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
    void cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output, bool mergeAfterCut = true, bool hasVideo = true);
    // Abort the in-progress export. Kills the running FFmpeg process and
    // emits finished(false, ...). Safe to call when nothing is running.
    void cancel();

    // Synthesize a video from one static cover image + one audio file (the
    // "radio video" feature). The image is looped for the whole (optionally
    // trimmed [startSec,endSec]) audio length and re-encoded — this is the
    // only re-encoding path in the app. framing is one of: "Native",
    // "1080x1080", "1920x1080", "1280x720". Emits progress(percent,100,...)
    // during the encode and finished() when done.
    void createRadioVideo(const QString &imagePath, const QString &audioPath,
                          double startSec, double endSec, const QString &framing,
                          const QString &outputPath);

signals:
    void progress(int current, int total, const QString &status);
    void finished(bool success, const QString &message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void runNextStep();
    void finalizeIndividualExport();
    // Emits finished() exactly once per export. QProcess emits both
    // errorOccurred() and finished() when a process dies from a signal
    // (crash or our own cancel()/kill()), so without this guard the
    // handlers would emit finished() twice.
    void emitFinished(bool success, const QString &message);
    // Parses ffmpeg's `-progress pipe:1` output (key=value lines on stdout)
    // into percentage progress for the radio-video encode. Progress comes on
    // stdout, so stderr stays intact for summarizeFfmpegError() on failure —
    // no accumulation or drain-ordering hazard.
    void parseRadioProgress();

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

    // Radio-video state
    bool m_radioVideoMode = false;
    double m_audioDuration = 0.0;   // full audio duration (progress denominator)
    double m_startSec = 0.0;
    double m_endSec = -1.0;         // -1 => whole file
    QByteArray m_progressStdoutBuf; // unprocessed tail of -progress stdout
};
