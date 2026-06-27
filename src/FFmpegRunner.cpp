#include "FFmpegRunner.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QStringList>

// FFmpeg prints its full banner, build configuration, and per-frame progress
// to stderr even on a normal failure, so dumping readAllStandardError() into a
// message box gives the user hundreds of unreadable lines. Pull out only the
// lines that actually explain the failure.
static QString summarizeFfmpegError(const QByteArray &rawStderr) {
    const QString text = QString::fromLocal8Bit(rawStderr).trimmed();
    if (text.isEmpty()) return QStringLiteral("FFmpeg process failed (no error output).");
    const QStringList lines = text.split('\n');

    static const QStringList noisePrefixes = {
        QStringLiteral("frame="), QStringLiteral("Press "),
        QStringLiteral("configuration:"), QStringLiteral("built with"),
        QStringLiteral("libav"), QStringLiteral("libsw"),
        QStringLiteral("ffmpeg version"), QStringLiteral("  usage: ")
    };

    QStringList relevant;
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty()) continue;
        bool isNoise = false;
        for (const QString &p : noisePrefixes) {
            if (t.startsWith(p, Qt::CaseInsensitive)) { isNoise = true; break; }
        }
        if (isNoise) continue;
        if (t.contains("error", Qt::CaseInsensitive) ||
            t.contains("failed", Qt::CaseInsensitive) ||
            t.contains("invalid", Qt::CaseInsensitive) ||
            t.contains("not found", Qt::CaseInsensitive) ||
            t.contains("no such", Qt::CaseInsensitive) ||
            t.contains("could not", Qt::CaseInsensitive) ||
            t.contains("permission", Qt::CaseInsensitive) ||
            t.contains("automatic encoder", Qt::CaseInsensitive) ||
            t.contains("encoder not found", Qt::CaseInsensitive)) {
            relevant << t;
        }
    }

    if (relevant.isEmpty()) {
        // No recognizable error keyword: fall back to the last few non-noise lines.
        for (auto it = lines.rbegin(); it != lines.rend() && relevant.size() < 6; ++it) {
            const QString t = it->trimmed();
            if (t.isEmpty()) continue;
            bool isNoise = false;
            for (const QString &p : noisePrefixes) {
                if (t.startsWith(p, Qt::CaseInsensitive)) { isNoise = true; break; }
            }
            if (isNoise) continue;
            relevant.prepend(t);
        }
    }

    while (relevant.size() > 15) relevant.removeFirst();
    if (relevant.isEmpty()) return QStringLiteral("FFmpeg process failed:\n%1").arg(text);
    return QStringLiteral("FFmpeg process failed:\n%1").arg(relevant.join('\n'));
}

// Probe whether libx264 is compiled into the user's FFmpeg (cached on first
// call). We cannot assume optional encoders are present — libmp3lame was
// absent on one test machine — so prefer libx264 (with -tune stillimage) when
// available and fall back to the always-built mpeg4 otherwise.
static bool libx264Available() {
    static const bool available = []() {
        QProcess probe;
        probe.start("ffmpeg", QStringList() << "-hide_banner" << "-encoders");
        if (!probe.waitForFinished(3000)) return false;
        return probe.readAllStandardOutput().contains("libx264");
    }();
    return available;
}

// Get a media file's duration (seconds) without an ffprobe dependency: run
// `ffmpeg -i <file>` (no output) and parse the "Duration: HH:MM:SS.xx" line it
// prints to stderr. Works for audio or video. Returns 0.0 if it can't be
// determined (the caller degrades gracefully — no progress %, but the encode
// still runs via -shortest).
static double probeMediaDuration(const QString &path) {
    QProcess probe;
    probe.start("ffmpeg", QStringList() << "-hide_banner" << "-i" << path);
    if (!probe.waitForFinished(3000)) return 0.0;
    const QString out = QString::fromLocal8Bit(probe.readAllStandardError());
    QRegularExpression re(QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    QRegularExpressionMatch m = re.match(out);
    if (!m.hasMatch()) return 0.0;
    return m.captured(1).toInt() * 3600.0
         + m.captured(2).toInt() * 60.0
         + m.captured(3).toDouble();
}

// Escape a path for use as the value of an ffmpeg filtergraph option such as
// `ass=<path>`. ffmpeg parses filter args in two levels: the filtergraph
// (where space, ',', ';', '[' and ']' separate, and single quotes group) and
// the option value (where ':' separates options and '\' escapes). To survive
// both, the value is wrapped in single quotes (protecting space / ',' / ';' /
// brackets at level 1) and within it '\' and ':' are backslash-escaped (so the
// ':' never splits the ass filter's filename/original_size options — needed for
// Windows drive letters like C:). A path with an embedded single quote can't be
// represented this way, so callers copy such files to a safe temp path first.
static QString escapeFilterPath(const QString &path) {
    QString p = path;
    p.replace('\\', "\\\\");   // escape backslashes first (don't re-escape the ones we add below)
    p.replace(':', "\\:");      // option separator at level 2
    return QStringLiteral("'") + p + QStringLiteral("'");   // group at level 1
}

FFmpegRunner::FFmpegRunner(QObject *parent) : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::finished, this, &FFmpegRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FFmpegRunner::onProcessError);
}

void FFmpegRunner::cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output, bool mergeAfterCut, bool hasVideo) {
    m_input = input;
    m_segments = segments;
    m_output = output;
    m_currentIndex = 0;
    m_isMerging = false;
    m_mergeAfterCut = mergeAfterCut;
    m_hasVideo = hasVideo;
    m_tempFiles.clear();

    if (m_segments.empty()) {
        emitFinished(false, "No segments to export");
        return;
    }

    if (!m_tempDir.isValid()) {
        emitFinished(false, "Failed to create temporary directory");
        return;
    }

    runNextStep();
}

void FFmpegRunner::runNextStep() {
    if (m_currentIndex < m_segments.size()) {
        // Cutting step
        const Segment &s = m_segments[m_currentIndex];
        
        QFileInfo outputInfo(m_output);
        QString ext = outputInfo.suffix();
        if (ext.isEmpty()) ext = "mp4";
        
        QString tempFile = m_tempDir.path() + QString("/temp_%1.%2").arg(m_currentIndex).arg(ext);
        m_tempFiles << tempFile;

        QFileInfo inputInfo(m_segments[m_currentIndex].filePath);
        QString inputExt = inputInfo.suffix().toLower();
        QString outputExt = ext.toLower();

        bool sameExtension = (inputExt == outputExt);

        QStringList args;

        if (!m_hasVideo) {
            // ===== Audio-only export =====
            // Always re-encode audio (never stream copy). Rationale:
            // - Audio re-encoding is extremely fast (1000x+ realtime for lossless, 100x+ for lossy)
            // - Stream copy causes broken timestamps / seek tables in FLAC, OGG, and other formats
            // - Lossless formats (FLAC, WAV) remain bit-perfect after re-encoding
            // - Place -ss/-to after -i for precise output-based seeking
            args << "-i" << m_segments[m_currentIndex].filePath
                 << "-ss" << QString::number(s.start, 'f', 6)
                 << "-to" << QString::number(s.end, 'f', 6)
                 << "-map" << "0:a"
                 << "-vn";

            // Select the best encoder for the target format
            // Principle: maximize quality, aim for lossless or near-lossless
            // For lossy formats: do NOT hardcode encoder names (e.g. libmp3lame)
            // because they may not be compiled into the user's FFmpeg build.
            // Instead, let FFmpeg auto-select the encoder and only guide the
            // quality/bitrate. The setting must be chosen per format because
            // a single fixed -b:a 320k is NOT universally valid: libvorbis and
            // libopus reject 320k on mono streams (their bitrate caps are
            // channel-count dependent), which would silently fail the export.
            if (outputExt == "flac") {
                // Lossless: built-in FFmpeg encoder, always available
                args << "-c:a" << "flac";
            } else if (outputExt == "wav") {
                // Lossless: built-in FFmpeg encoder, always available
                args << "-c:a" << "pcm_s24le";
            } else if (outputExt == "ogg") {
                // Vorbis: use VBR quality mode (-b:a is rejected on mono
                // streams because vorbis' per-channel bitrate cap is lower
                // than mp3's). q=10 is the maximum quality. FFmpeg still
                // auto-selects libvorbis here; we only set the quality.
                args << "-q:a" << "10";
            } else if (outputExt == "opus") {
                // Opus is transparent far below its ceiling; 256k is generous
                // and safe for both mono and stereo (320k is rejected on mono).
                args << "-b:a" << "256k";
            } else {
                // mp3, aac, m4a, wma, mka, etc. — let FFmpeg auto-select the
                // encoder and use a high bitrate. These encoders accept 320k
                // on mono (mp3 max = 320k; aac/m4a handle it fine).
                args << "-b:a" << "320k";
            }
        } else {
            // ===== Video export =====
            // NEVER re-encode video. This software's philosophy is cutting, not re-encoding.
            // Always use stream copy for both video and audio streams.
            // Input-seeking (-ss before -i) is fast because FFmpeg seeks to the nearest keyframe.
            args << "-ss" << QString::number(s.start, 'f', 6)
                 << "-to" << QString::number(s.end, 'f', 6)
                 << "-i" << m_segments[m_currentIndex].filePath
                 << "-map" << "0"
                 << "-c" << "copy";
        }
        
        args << "-avoid_negative_ts" << "make_zero"
             << "-y" << tempFile;

        emit progress(m_currentIndex, m_segments.size() + 1, QString("Cutting segment %1...").arg(m_currentIndex + 1));
        m_process->start("ffmpeg", args);
    } else if (!m_isMerging && m_mergeAfterCut) {
        // Merging step
        m_isMerging = true;
        QString concatFilePath = m_tempDir.path() + "/concat.txt";
        QFile concatFile(concatFilePath);
        if (concatFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&concatFile);
            for (const QString &f : m_tempFiles) {
                QString escapedPath = f;
                escapedPath.replace("\\", "/");
                out << "file '" << escapedPath << "'\n";
            }
            concatFile.close();
        } else {
            emitFinished(false, "Failed to create concat file");
            return;
        }

        QStringList args;
        args << "-f" << "concat"
             << "-safe" << "0"
             << "-i" << concatFilePath
             << "-map" << "0"
             << "-c" << "copy"
             << "-y" << m_output;

        emit progress(m_segments.size(), m_segments.size() + 1, "Merging segments...");
        m_process->start("ffmpeg", args);
    } else {
        if (!m_mergeAfterCut) {
            finalizeIndividualExport();
        } else {
            emit progress(m_segments.size() + 1, m_segments.size() + 1, "Done");
            emitFinished(true, "Export completed successfully");
        }
    }
}

void FFmpegRunner::finalizeIndividualExport() {
    QFileInfo outputInfo(m_output);
    QString targetDir = outputInfo.absolutePath();
    QString baseName = outputInfo.baseName();
    QString ext = outputInfo.suffix();

    int startIndex = 1;
    QDir dir(targetDir);
    QStringList filters;
    filters << QString("%1_*.%2").arg(baseName).arg(ext);
    QStringList existingFiles = dir.entryList(filters, QDir::Files);

    int maxIndex = 0;
    QRegularExpression regex(QString("^%1_(\\d+)\\.%2$").arg(QRegularExpression::escape(baseName)).arg(QRegularExpression::escape(ext)), QRegularExpression::CaseInsensitiveOption);
    for (const QString &filename : existingFiles) {
        QRegularExpressionMatch match = regex.match(filename);
        if (match.hasMatch()) {
            int num = match.captured(1).toInt();
            if (num > maxIndex) {
                maxIndex = num;
            }
        }
    }

    if (maxIndex > 0) {
        startIndex = maxIndex + 1;
    }

    for (int i = 0; i < m_tempFiles.size(); ++i) {
        QString newPath = targetDir + "/" + baseName + QString("_%1.%2").arg(startIndex + i).arg(ext);
        QFile::remove(newPath);
        if (!QFile::rename(m_tempFiles[i], newPath)) {
            // If rename fails (e.g. cross-device boundary), fallback to copy + delete
            if (QFile::copy(m_tempFiles[i], newPath)) {
                QFile::remove(m_tempFiles[i]);
            } else {
                emitFinished(false, QString("Failed to move segment %1 to destination (rename and copy failed)").arg(startIndex + i));
                return;
            }
        }
    }

    emitFinished(true, "Individual segments exported successfully");
}

void FFmpegRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_singleShot) {
        // Single-shot encode (radio video or subtitle burn): no step-loop.
        // Reset the flag so a late finished()/errorOccurred() pair (e.g. after
        // cancel()->kill()) can't re-enter this branch. emitFinished() dedupes
        // the notification.
        m_singleShot = false;
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            emit progress(100, 100, m_progressLabel);
            emitFinished(true, m_successMessage);
        } else {
            // Progress travelled via stdout (-progress pipe:1), so stderr is
            // untouched and still holds the real failure explanation.
            emitFinished(false, summarizeFfmpegError(m_process->readAllStandardError()));
        }
        return;
    }

    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        if (!m_isMerging) {
            m_currentIndex++;
        }
        runNextStep();
    } else {
        emitFinished(false, summarizeFfmpegError(m_process->readAllStandardError()));
    }
}

void FFmpegRunner::onProcessError(QProcess::ProcessError error) {
    // QProcess emits both errorOccurred() and finished() when a process dies
    // from a signal (a genuine crash, or our own cancel()/kill()); emitFinished()
    // guarantees we notify the caller only once, regardless of which handler
    // runs first. The "crashed" message is suppressed by MainWindow when the
    // caller cancelled, so it only surfaces for a real crash.
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "FFmpeg executable not found or failed to start. Check if FFmpeg is installed and in PATH.";
            break;
        case QProcess::Crashed:
            errorMsg = "FFmpeg process crashed or was terminated.";
            break;
        case QProcess::Timedout:
            errorMsg = "FFmpeg process timed out.";
            break;
        case QProcess::ReadError:
            errorMsg = "Error reading from FFmpeg process.";
            break;
        case QProcess::WriteError:
            errorMsg = "Error writing to FFmpeg process.";
            break;
        case QProcess::UnknownError:
        default:
            errorMsg = "Unknown FFmpeg process error.";
            break;
    }
    emitFinished(false, errorMsg);
}

void FFmpegRunner::cancel() {
    if (m_finished) return;
    if (m_process->state() == QProcess::NotRunning) {
        // Between steps: nothing to kill, just report.
        emitFinished(false, "Export cancelled by user");
    } else {
        // Killing triggers errorOccurred()+finished(); emitFinished() will
        // dedupe, and MainWindow suppresses the dialog because it set its
        // own cancelled flag before calling us.
        m_process->kill();
    }
}

void FFmpegRunner::emitFinished(bool success, const QString &message) {
    if (m_finished) return;
    m_finished = true;
    emit finished(success, message);
}

void FFmpegRunner::createRadioVideo(const QString &imagePath, const QString &audioPath,
                                    double startSec, double endSec, const QString &framing,
                                    const QString &outputPath) {
    // Reset state for this (possibly reused) runner.
    m_singleShot = true;
    m_finished = false;
    m_progressStdoutBuf.clear();
    m_successMessage = tr("Radio video created successfully.");
    m_progressLabel = tr("Creating radio video...");

    startSec = qMax(0.0, startSec);
    const double audioDuration = probeMediaDuration(audioPath);
    if (endSec < 0.0) {
        endSec = audioDuration;             // whole file
    } else if (audioDuration > 0.0 && endSec > audioDuration) {
        endSec = audioDuration;             // clamp overshoot
    }
    const double dur = (endSec > startSec) ? (endSec - startSec) : audioDuration;
    m_progressTotal = dur;

    // Verified command:
    //   ffmpeg -y -loop 1 -framerate 5 -i <image> [-ss <s> -to <e>] -i <audio>
    //          -vf <vf> -c:v <libx264 -tune stillimage | mpeg4>
    //          -g 5 -keyint_min 5 -r 5 -pix_fmt yuv420p -c:a aac -b:a 192k
    //          -t <dur> -progress pipe:1 <out>
    QStringList args;
    args << "-y" << "-loop" << "1" << "-framerate" << "5" << "-i" << imagePath;

    // Trim = INPUT options on the audio input (verified: -ss 2 -to 5 -> 3.000s).
    // Apply only when the user actually narrowed the range below the whole file.
    const bool trimmed = (startSec > 0.0) ||
                         (audioDuration > 0.0 && endSec < audioDuration - 0.001);
    if (trimmed) {
        args << "-ss" << QString::number(startSec, 'f', 6)
             << "-to" << QString::number(endSec, 'f', 6);
    }
    args << "-i" << audioPath;

    // Video filter per framing choice. Every option ends with even-dimension
    // safety + yuv420p so libx264/mpeg4 accept the frames.
    QString vf;
    if (framing == QStringLiteral("Native")) {
        vf = QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p");
    } else if (framing == QStringLiteral("1920x1080")) {
        vf = QStringLiteral("scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    } else if (framing == QStringLiteral("1280x720")) {
        vf = QStringLiteral("scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    } else {
        // "1080x1080" (and any unrecognized value) — square is the radio default.
        vf = QStringLiteral("scale=1080:1080:force_original_aspect_ratio=decrease,pad=1080:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    }
    args << "-vf" << vf;

    if (libx264Available()) {
        args << "-c:v" << "libx264" << "-tune" << "stillimage";
    } else {
        args << "-c:v" << "mpeg4";
    }
    // Frequent keyframes so the output is seekable: at 5 fps, -g 5 = one
    // keyframe per second. With libx264's default GOP of 250 frames the
    // keyframes were 50s apart, so players (PotPlayer etc.) jumped 50s per
    // seek — unusable. The extra I-frames cost little for a static image.
    args << "-g" << "5" << "-keyint_min" << "5";
    // Low fps (5) for a static image: visually identical to 25fps but far
    // smaller files — important for multi-hour radio episodes.
    args << "-r" << "5" << "-pix_fmt" << "yuv420p"
         << "-c:a" << "aac" << "-b:a" << "192k";

    // Cap output duration explicitly. -shortest is UNRELIABLE with -loop 1 —
    // in testing it produced ~2x the audio length (24s for a 12s clip) because
    // the looped-image/audio stream-end timing miscomputes. -t with the known
    // (probed or marked) duration is exact. Fall back to -shortest only when the
    // duration is genuinely unknown (probe failed AND no marks set).
    if (dur > 0.0) {
        args << "-t" << QString::number(dur, 'f', 3);
    } else {
        args << "-shortest";
    }
    // Progress stats go to stdout (parseable key=value); errors stay on
    // stderr for summarizeFfmpegError().
    args << "-progress" << "pipe:1"
         << outputPath;

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FFmpegRunner::parseEncodeProgress, Qt::UniqueConnection);

    emit progress(0, 100, m_progressLabel);
    m_process->start("ffmpeg", args);
}

void FFmpegRunner::burnSubtitles(const QString &videoPath, const QString &assPath,
                                 const QString &outputPath, int fps, int crf) {
    m_singleShot = true;
    m_finished = false;
    m_progressStdoutBuf.clear();
    m_successMessage = tr("Subtitles burned successfully.");
    m_progressLabel = tr("Burning subtitles...");

    // The .ass is read by the `ass` filter (libass) — same renderer mpv uses for
    // the preview, so what the user saw is what gets burned in. Copy the file
    // into our temp dir under a safe name so the filter never sees a path with
    // spaces / commas / quotes (escapeFilterPath handles the common cases, but a
    // temp copy is bulletproof). ASS fonts are either embedded ([Fonts]) or
    // system-installed, so copying the single .ass is sufficient.
    if (!m_tempDir.isValid()) {
        emitFinished(false, tr("Failed to create temporary directory."));
        return;
    }
    const QString tempAss = m_tempDir.path() + QStringLiteral("/subs.ass");
    QFile::remove(tempAss);
    if (!QFile::copy(assPath, tempAss)) {
        emitFinished(false, tr("Could not read the subtitle file:\n%1").arg(assPath));
        return;
    }

    // Progress denominator = the input video's duration (probed).
    m_progressTotal = probeMediaDuration(videoPath);

    // Verified command:
    //   ffmpeg -y -i <video> -vf ass=<escaped ass> -c:v <libx264 -crf N | mpeg4>
    //          -r <fps> -pix_fmt yuv420p -c:a copy -progress pipe:1 <out>
    QStringList args;
    args << "-y" << "-i" << videoPath
         << "-vf" << (QStringLiteral("ass=") + escapeFilterPath(tempAss));

    if (libx264Available()) {
        args << "-c:v" << "libx264" << "-crf" << QString::number(crf) << "-preset" << "medium";
    } else {
        args << "-c:v" << "mpeg4" << "-q:v" << "2";
    }
    // A normal fps so ASS effects (\move, transforms) animate smoothly even
    // when the source is low-fps (e.g. a 5fps radio video).
    args << "-r" << QString::number(qMax(1, fps))
         << "-pix_fmt" << "yuv420p"
         << "-c:a" << "copy";   // audio is unchanged by subtitle burn
    args << "-progress" << "pipe:1"
         << outputPath;

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FFmpegRunner::parseEncodeProgress, Qt::UniqueConnection);

    emit progress(0, 100, m_progressLabel);
    m_process->start("ffmpeg", args);
}

void FFmpegRunner::parseEncodeProgress() {
    if (!m_singleShot) return;
    m_progressStdoutBuf += m_process->readAllStandardOutput();

    // Parse only complete (newline-terminated) lines so a half-written value is
    // never read; keep any trailing partial line for the next call. This also
    // bounds the buffer to ~one chunk — no unbounded growth over long encodes.
    const int nl = m_progressStdoutBuf.lastIndexOf('\n');
    if (nl < 0) return;
    const QString complete = QString::fromLatin1(m_progressStdoutBuf.left(nl + 1));
    m_progressStdoutBuf = m_progressStdoutBuf.mid(nl + 1);

    // out_time_us = output time in microseconds, running 0..encode-duration.
    static const QRegularExpression re(QStringLiteral("out_time_us=(\\d+)"));
    auto it = re.globalMatch(complete);
    double cur = -1.0;
    while (it.hasNext()) cur = it.next().captured(1).toLongLong() / 1000000.0;
    if (cur < 0.0) return;

    if (m_progressTotal <= 0.0) return;
    emit progress(qBound(0, static_cast<int>(cur / m_progressTotal * 100.0), 99), 100,
                  m_progressLabel);
}
