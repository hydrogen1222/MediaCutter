#include "FFmpegRunner.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QStringList>

// Sliding-window stall watchdog cadence (see FFmpegRunner.h). The timer ticks
// every WATCHDOG_POLL_MS; if out_time_us hasn't advanced for WATCHDOG_STALL_MS,
// the encode is treated as deadlocked. 20s is generous: a healthy single-shot
// encode (static image at 5fps, or a subtitle burn) advances far faster than
// realtime, so 20s of frozen out_time_us is a real stall, not a slow frame.
static constexpr int WATCHDOG_POLL_MS = 2000;
static constexpr int WATCHDOG_STALL_MS = 20000;

// FFmpeg prints its full banner, build configuration, and per-frame progress
// to stderr even on a normal failure, so dumping readAllStandardError() into a
// message box gives the user hundreds of unreadable lines. Pull out only the
// lines that actually explain the failure.
static QString summarizeFfmpegError(const QByteArray &rawStderr) {
    const QString text = QString::fromUtf8(rawStderr).trimmed();
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
    const QString out = QString::fromUtf8(probe.readAllStandardError());
    QRegularExpression re(QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    QRegularExpressionMatch m = re.match(out);
    if (!m.hasMatch()) return 0.0;
    return m.captured(1).toInt() * 3600.0
         + m.captured(2).toInt() * 60.0
         + m.captured(3).toDouble();
}

// Locate the first usable VAAPI render node (e.g. /dev/dri/renderD128). Render
// nodes have minor numbers >= 128; we probe renderD128..renderD159 and return
// the first that exists. Empty if none (=> no VAAPI on this machine).
static QString findVaapiDevice() {
    for (int i = 0; i < 32; ++i) {
        const QString path = QStringLiteral("/dev/dri/renderD%1").arg(128 + i);
        if (QFileInfo(path).exists()) return path;
    }
    return QString();
}

// Run a tiny throwaway encode to confirm an encoder actually works on THIS
// hardware. A compiled-in encoder (h264_nvenc etc.) is useless without the
// matching GPU + driver, so listing it in `ffmpeg -encoders` is not enough -
// we must exercise it. Returns true only if ffmpeg exited 0 within the timeout.
static bool encoderWorks(const QStringList &args) {
    QProcess p;
    p.start("ffmpeg", args);
    if (!p.waitForFinished(8000)) { p.kill(); p.waitForFinished(2000); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// Set when a hardware encoder passed the synthetic probe but then deadlocked
// on the real encode (the startup watchdog fired). Once set, skip hardware for
// the rest of the session so subsequent exports go straight to the reliable
// libx264 path instead of re-triggering the same hang each time. The flag is
// process-local (resets on app restart), so a driver update fixes things.
static bool g_hwEncoderBad = false;

// Best usable hardware H.264 encoder, probed once and cached. Priority:
// nvenc (NVIDIA) > qsv (Intel) > vaapi (Linux Intel/AMD). Each candidate is
// actually exercised (not just listed) before it is accepted, so a machine
// with no matching GPU falls through to the next. An empty `name` means "no
// working hardware encoder - use the software path".
//
// h264_amf (AMD/Windows) is intentionally NOT probed: its synthetic probe
// passes even when the real encode deadlocks (observed on AMD 780M / RDNA3:
// 0% progress, no CPU/GPU use), and no probe args we tried could tell the two
// apart. Auto-selecting it made exports hang forever. AMD users on Windows get
// libx264 (all CPU cores); AMD users on Linux still get VAAPI. If AMF stabilizes
// in the future it can be re-added behind the startup watchdog.
struct HwEncoder { QString name; QString vaapiDevice; };
static HwEncoder detectHwEncoder() {
    // A HW encoder already proved unreliable this session: skip it entirely.
    if (g_hwEncoderBad) return HwEncoder{};
    static const HwEncoder cached = []() {
        HwEncoder r;
        // NVENC (NVIDIA): accepts software frames directly (uploads internally).
        if (encoderWorks(QStringList()
                << "-hide_banner" << "-y"
                << "-f" << "lavfi" << "-i" << "testsrc=duration=0.2:size=320x240:rate=5"
                << "-pix_fmt" << "yuv420p"
                << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-tune" << "hq"
                << "-rc" << "vbr" << "-cq" << "20" << "-b:v" << "0"
                << "-f" << "null" << "-")) {
            r.name = QStringLiteral("h264_nvenc");
            return r;
        }
        // Quick Sync (Intel): accepts software frames (uploads internally).
        if (encoderWorks(QStringList()
                << "-hide_banner" << "-y"
                << "-f" << "lavfi" << "-i" << "testsrc=duration=0.2:size=320x240:rate=5"
                << "-pix_fmt" << "yuv420p"
                << "-c:v" << "h264_qsv" << "-preset" << "veryfast" << "-global_quality" << "20"
                << "-f" << "null" << "-")) {
            r.name = QStringLiteral("h264_qsv");
            return r;
        }
        // VAAPI (Linux Intel/AMD): needs a render device + an explicit hwupload
        // because the ass/scale filters run in software.
        const QString dev = findVaapiDevice();
        if (!dev.isEmpty() &&
            encoderWorks(QStringList()
                << "-hide_banner" << "-y"
                << "-vaapi_device" << dev
                << "-f" << "lavfi" << "-i" << "testsrc=duration=0.2:size=320x240:rate=5"
                << "-vf" << "format=nv12,hwupload"
                << "-c:v" << "h264_vaapi" << "-rc_mode" << "CQP" << "-qp" << "20"
                << "-f" << "null" << "-")) {
            r.name = QStringLiteral("h264_vaapi");
            r.vaapiDevice = dev;
            return r;
        }
        return r;  // none - caller falls back to libx264
    }();
    return cached;
}

// Everything a single-shot encode needs to know about its video encoder,
// produced once per export. Priority: a working hardware encoder (nvenc/qsv/
// vaapi) > libx264 > mpeg4. baseFilter is the filter chain BEFORE any
// hardware upload (e.g. "scale=...,format=yuv420p" or "ass=<path>"); the plan
// appends any format/hwupload suffix the chosen encoder requires. crf is the
// libx264 CRF and also the hardware QP target (lower = better). stillImage adds
// libx264's -tune stillimage (radio video only).
struct VideoEncodePlan {
    QString label;          // human-readable encoder name for the status line
    QStringList globalArgs; // options that must precede -i (e.g. -vaapi_device)
    QString filter;         // complete -vf value
    QStringList args;       // -c:v ... + quality/preset/thread/pix_fmt flags
};
static VideoEncodePlan buildVideoEncodePlan(const QString &baseFilter, int crf, bool stillImage, bool forceSoftware) {
    VideoEncodePlan p;
    const HwEncoder hw = forceSoftware ? HwEncoder{} : detectHwEncoder();
    const QString q = QString::number(qBound(1, crf, 51));

    if (hw.name == QLatin1String("h264_nvenc")) {
        p.label = QStringLiteral("NVENC");
        p.filter = baseFilter + QStringLiteral(",format=yuv420p");
        p.args << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-tune" << "hq"
               << "-rc" << "vbr" << "-cq" << q << "-b:v" << "0";
    } else if (hw.name == QLatin1String("h264_qsv")) {
        p.label = QStringLiteral("QuickSync");
        p.filter = baseFilter + QStringLiteral(",format=yuv420p");
        p.args << "-c:v" << "h264_qsv" << "-preset" << "veryfast" << "-global_quality" << q;
    } else if (hw.name == QLatin1String("h264_vaapi")) {
        p.label = QStringLiteral("VAAPI");
        p.globalArgs << "-vaapi_device" << hw.vaapiDevice;
        p.filter = baseFilter + QStringLiteral(",format=nv12,hwupload");
        p.args << "-c:v" << "h264_vaapi" << "-rc_mode" << "CQP" << "-qp" << q;
    } else if (libx264Available()) {
        // Software fallback. -preset veryfast is ~4x faster than the default
        // "medium" at the same CRF (same visual quality, only ~25% larger files),
        // and -threads 0 lets libx264 use every logical CPU core for frame
        // threading - the "use all resources" lever for machines with no GPU.
        p.label = QStringLiteral("libx264");
        p.filter = baseFilter;
        p.args << "-c:v" << "libx264"
               << "-preset" << "veryfast"
               << "-crf" << q
               << "-threads" << "0"
               << "-pix_fmt" << "yuv420p";
        if (stillImage) p.args << "-tune" << "stillimage";
    } else {
        p.label = QStringLiteral("mpeg4");
        p.filter = baseFilter;
        p.args << "-c:v" << "mpeg4" << "-q:v" << "2"
               << "-threads" << "0"
               << "-pix_fmt" << "yuv420p";
    }
    return p;
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

FFmpegRunner::FFmpegRunner(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)), m_watchdog(new QTimer(this)) {
    connect(m_process, &QProcess::finished, this, &FFmpegRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FFmpegRunner::onProcessError);
    // Keep ffmpeg's stderr drained into m_stderrBuf at all times (a full OS
    // pipe could otherwise block ffmpeg) and surface it for diagnostics.
    connect(m_process, &QProcess::readyReadStandardError, this, &FFmpegRunner::onFfmpegStderr);
    // Recurring stall watchdog: ticks every WATCHDOG_POLL_MS while a single-shot
    // encode runs, and acts only when out_time_us hasn't advanced for
    // WATCHDOG_STALL_MS (see onWatchdogTimeout). Not single-shot - a mid-encode
    // hang must be caught, not just a first-frame one.
    m_watchdog->setSingleShot(false);
    connect(m_watchdog, &QTimer::timeout, this, &FFmpegRunner::onWatchdogTimeout);
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
        m_stderrBuf.clear();
        qDebug().noquote() << "[ffmpeg] cut cmd:" << args;
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
        m_stderrBuf.clear();
        qDebug().noquote() << "[ffmpeg] merge cmd:" << args;
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
    m_watchdog->stop();
    qDebug().noquote() << "[ffmpeg] finished exitCode=" << exitCode
                       << "status=" << exitStatus
                       << "singleShot=" << m_singleShot
                       << "retrying=" << m_retrying;

    if (m_retrying) {
        // The watchdog killed a hung hardware-encoder run. Retry once on the
        // always-reliable libx264 path. Defer via the event loop so the
        // QProcess fully settles (its paired errorOccurred() has drained)
        // before we restart it. m_retrying stays true until the deferred call
        // runs, which also suppresses that paired errorOccurred() signal.
        QMetaObject::invokeMethod(this, [this]() {
            m_retrying = false;
            if (m_finished) return;  // cancelled while the retry was deferred
            if (m_retryWithSoftware) {
                m_retryWithSoftware();
            } else {
                emitFinished(false, tr("Encoding stalled and no software fallback is available."));
            }
        }, Qt::QueuedConnection);
        return;
    }

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
            // m_stderrBuf holds ffmpeg's drained stderr (progress went via
            // stdout on -progress pipe:1), so it has the real failure reason.
            emitFinished(false, summarizeFfmpegError(m_stderrBuf));
        }
        return;
    }

    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        if (!m_isMerging) {
            m_currentIndex++;
        }
        runNextStep();
    } else {
        emitFinished(false, summarizeFfmpegError(m_stderrBuf));
    }
}

void FFmpegRunner::onProcessError(QProcess::ProcessError error) {
    qDebug().noquote() << "[ffmpeg] errorOccurred=" << error
                       << "retrying=" << m_retrying
                       << "watchdogKilled=" << m_watchdogKilled;
    if (m_retrying) return;  // watchdog-driven HW kill: onProcessFinished handles the retry
    if (m_watchdogKilled) return;  // watchdog-driven kill (HW retry or SW stall):
                                   // onProcessFinished emits the ffmpeg stderr summary
    m_watchdog->stop();
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
    m_watchdog->stop();
    m_retrying = false;  // don't auto-retry after a user-initiated cancel
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

void FFmpegRunner::onFfmpegStderr() {
    const QByteArray chunk = m_process->readAllStandardError();
    if (chunk.isEmpty()) return;
    m_stderrBuf += chunk;
    // Echo ffmpeg's own output to the console for single-shot encodes, where a
    // hang is otherwise invisible (0% progress, no error). ffmpeg with
    // -nostats -hide_banner prints only the input/output/stream-mapping lines
    // plus any warnings/errors - so this shows exactly how far it got. Cut/merge
    // (stream copy, fast, never reported to hang) is kept quiet to avoid
    // flooding the console with per-frame stats.
    if (m_singleShot) {
        qDebug().noquote() << "[ffmpeg]" << QString::fromUtf8(chunk).trimmed();
    }
}

void FFmpegRunner::onWatchdogTimeout() {
    // Recurring tick of the sliding-window stall detector. m_lastProgressAt is
    // reset whenever out_time_us strictly advances (parseEncodeProgress); if it
    // hasn't been reset for WATCHDOG_STALL_MS, no frame has advanced in that
    // window - a genuine stall (first-frame deadlock OR mid-encode hang). The
    // old single-shot watchdog disarmed on the first frame and could not catch
    // a hang that started later; this tick model closes that gap.
    if (m_finished) return;
    if (!m_lastProgressAt.hasExpired(WATCHDOG_STALL_MS)) return;  // healthy - keep ticking

    m_watchdog->stop();
    // Mark the kill as ours so onProcessError() doesn't surface the generic
    // "crashed" message and drown out the real ffmpeg stderr summary that
    // onProcessFinished() emits.
    m_watchdogKilled = true;
    qDebug().noquote() << "[ffmpeg] WATCHDOG fired - no advancing frame in"
                       << (WATCHDOG_STALL_MS / 1000) << "s."
                       << "forceSoftware=" << m_forceSoftware
                       << "usedHw=" << m_usedHw
                       << "gotProgress=" << m_gotProgress
                       << "stderrSoFar:\n" << QString::fromUtf8(m_stderrBuf).trimmed();

    if (!m_forceSoftware && m_usedHw) {
        // A hardware encoder passed its synthetic probe but stalled on the real
        // encode (no advancing frame). Disable HW for the rest of the session so
        // the next export skips it, then retry THIS encode on libx264, which
        // always works. Killing triggers errorOccurred() (suppressed by
        // m_retrying) + finished() (which defers the software retry).
        g_hwEncoderBad = true;
        m_retrying = true;
        qDebug() << "[ffmpeg] watchdog: HW encoder stalled - retrying on libx264";
        m_process->kill();
        return;
    }
    // Already on the software path (no HW was available, or the HW retry also
    // stalled) and still no advancing frame - genuinely stuck. Kill and let the
    // normal failure path report it. Retrying libx264-with-libx264 is pointless.
    qDebug() << "[ffmpeg] watchdog: encode stalled - no fallback available";
    m_process->kill();
}

void FFmpegRunner::createRadioVideo(const QString &imagePath, const QString &audioPath,
                                    double startSec, double endSec, const QString &framing,
                                    const QString &outputPath, double audioDurationHint) {
    // Remember how to replay this exact encode on the software path, in case
    // the watchdog detects a hung hardware encoder and needs to retry.
    m_retryWithSoftware = [this, imagePath, audioPath, startSec, endSec, framing, outputPath, audioDurationHint]() {
        createRadioVideoImpl(imagePath, audioPath, startSec, endSec, framing, outputPath, true, audioDurationHint);
    };
    createRadioVideoImpl(imagePath, audioPath, startSec, endSec, framing, outputPath, false, audioDurationHint);
}

void FFmpegRunner::createRadioVideoImpl(const QString &imagePath, const QString &audioPath,
                                        double startSec, double endSec, const QString &framing,
                                        const QString &outputPath, bool forceSoftware,
                                        double audioDurationHint) {
    // Reset state for this (possibly reused) runner / attempt.
    m_singleShot = true;
    m_finished = false;
    m_progressStdoutBuf.clear();
    m_stderrBuf.clear();
    m_lastLoggedPct = -1;
    m_forceSoftware = forceSoftware;
    m_usedHw = false;
    m_gotProgress = false;
    m_retrying = false;
    m_watchdogKilled = false;
    m_lastOutTimeSec = -1.0;
    m_successMessage = tr("Radio video created successfully.");
    m_progressLabel = tr("Creating radio video...");

    startSec = qMax(0.0, startSec);
    // Prefer the duration the caller already knows (mpv decoded the file and
    // reported it - authoritative). Fall back to probing only when the caller
    // didn't pass one. This matters for ASF/WMA radio rips whose container
    // header has no clean duration: ffmpeg prints "Duration: N/A", the probe
    // regex returns 0, and the old code then used -shortest + m_progressTotal=0
    // -> the UI sat at 0% forever with no progress (the blind-0% bug). With
    // mpv's value we use -t and get a real progress denominator.
    const double audioDuration = (audioDurationHint > 0.0)
        ? audioDurationHint
        : probeMediaDuration(audioPath);
    if (endSec < 0.0) {
        endSec = audioDuration;             // whole file
    } else if (audioDuration > 0.0 && endSec > audioDuration) {
        endSec = audioDuration;             // clamp overshoot
    }
    const double dur = (endSec > startSec) ? (endSec - startSec) : audioDuration;
    m_progressTotal = dur;

    // Video filter per framing choice. Every option ends with even-dimension
    // safety + yuv420p so software encoders accept the frames; the chosen
    // encoder's plan appends any extra format/hwupload suffix it needs.
    QString vf;
    if (framing == QStringLiteral("Native")) {
        vf = QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p");
    } else if (framing == QStringLiteral("1920x1080")) {
        vf = QStringLiteral("scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    } else if (framing == QStringLiteral("1280x720")) {
        vf = QStringLiteral("scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    } else {
        // "1080x1080" (and any unrecognized value) - square is the radio default.
        vf = QStringLiteral("scale=1080:1080:force_original_aspect_ratio=decrease,pad=1080:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p");
    }

    // Pick the best encoder (hardware if usable, else libx264/mpeg4) and build
    // its args + any required filter suffix. For a static image the bitrate is
    // tiny regardless of preset, so libx264's -preset veryfast trades a little
    // size for a big speedup. Reflect the chosen encoder in the status line so
    // the user can see HW acceleration is active.
    const VideoEncodePlan plan = buildVideoEncodePlan(vf, 23, true /*stillImage*/, forceSoftware);
    m_usedHw = (plan.label == QLatin1String("NVENC") ||
                plan.label == QLatin1String("QuickSync") ||
                plan.label == QLatin1String("VAAPI"));
    m_progressLabel = tr("Creating radio video...") + QStringLiteral(" [%1]").arg(plan.label);

    // Verified command:
    //   ffmpeg -y [<vaapi_device>] -loop 1 -framerate 5 -i <image>
    //          [-ss <s> -to <e>] -i <audio> -vf <vf> <encoder args>
    //          -g 5 -keyint_min 5 -r 5 -af aresample=async=1:first_pts=0
    //          -c:a aac -b:a 192k -t <dur> -progress pipe:1 <out>
    QStringList args;
    args << "-y" << "-hide_banner" << "-nostats" << plan.globalArgs
         << "-loop" << "1" << "-framerate" << "5" << "-i" << imagePath;

    // Trim = INPUT options on the audio input (verified: -ss 2 -to 5 -> 3.000s).
    // Apply only when the user actually narrowed the range below the whole file.
    const bool trimmed = (startSec > 0.0) ||
                         (audioDuration > 0.0 && endSec < audioDuration - 0.001);
    if (trimmed) {
        args << "-ss" << QString::number(startSec, 'f', 6)
             << "-to" << QString::number(endSec, 'f', 6);
    }
    args << "-i" << audioPath;

    args << "-vf" << plan.filter << plan.args;
    // Frequent keyframes so the output is seekable: at 5 fps, -g 5 = one
    // keyframe per second. With libx264's default GOP of 250 frames the
    // keyframes were 50s apart, so players (PotPlayer etc.) jumped 50s per
    // seek — unusable. The extra I-frames cost little for a static image.
    args << "-g" << "5" << "-keyint_min" << "5";
    // Low fps (5) for a static image: visually identical to 25fps but far
    // smaller files — important for multi-hour radio episodes.
    // Resync the audio to a clean, monotonically increasing timeline starting
    // at 0. ASF/WMA radio rips can carry non-monotonic audio timestamps that
    // make the AAC encoder warn "Queue input is backward in time" /
    // "Non-monotonic DTS" and, with -shortest, stall the encode at 0% CPU.
    // async=1 drops/inserts samples to kill timestamp drift; first_pts=0
    // anchors the start. It is a no-op on files whose timestamps are already
    // clean (the common case), so it is safe to apply unconditionally.
    args << "-r" << "5"
         << "-af" << "aresample=async=1:first_pts=0"
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

    qDebug().noquote() << "[ffmpeg] radio-video cmd:" << args
                       << (forceSoftware ? "(software fallback)" : "");
    emit progress(0, 100, m_progressLabel);
    m_process->start("ffmpeg", args);
    qDebug() << "[ffmpeg] radio-video started; watchdog" << (WATCHDOG_STALL_MS / 1000) << "s";
    // Arm the sliding-window stall watchdog: it ticks every WATCHDOG_POLL_MS
    // and kills the encode if out_time_us doesn't advance for WATCHDOG_STALL_MS
    // at any point (first-frame deadlock or mid-encode hang). m_lastProgressAt
    // starts now so the first window covers the encoder warm-up.
    m_lastProgressAt.start();
    m_watchdog->start(WATCHDOG_POLL_MS);
}

void FFmpegRunner::burnSubtitles(const QString &videoPath, const QString &assPath,
                                 const QString &outputPath, int fps, int crf,
                                 double videoDurationHint) {
    // Remember how to replay this exact encode on the software path, in case
    // the watchdog detects a hung hardware encoder and needs to retry.
    m_retryWithSoftware = [this, videoPath, assPath, outputPath, fps, crf, videoDurationHint]() {
        burnSubtitlesImpl(videoPath, assPath, outputPath, fps, crf, true, videoDurationHint);
    };
    burnSubtitlesImpl(videoPath, assPath, outputPath, fps, crf, false, videoDurationHint);
}

void FFmpegRunner::burnSubtitlesImpl(const QString &videoPath, const QString &assPath,
                                     const QString &outputPath, int fps, int crf, bool forceSoftware,
                                     double videoDurationHint) {
    m_singleShot = true;
    m_finished = false;
    m_progressStdoutBuf.clear();
    m_stderrBuf.clear();
    m_lastLoggedPct = -1;
    m_forceSoftware = forceSoftware;
    m_usedHw = false;
    m_gotProgress = false;
    m_retrying = false;
    m_watchdogKilled = false;
    m_lastOutTimeSec = -1.0;
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

    // Progress denominator = the input video's duration. Prefer the caller's
    // hint (mpv, which already decoded the file); fall back to probing only
    // when no hint was supplied.
    m_progressTotal = (videoDurationHint > 0.0)
        ? videoDurationHint
        : probeMediaDuration(videoPath);

    // Pick the encoder (hardware if usable, else libx264/mpeg4). -preset
    // veryfast replaces "medium": ~4x faster at the same CRF (same visual
    // quality, slightly larger files), and -threads 0 uses every CPU core. The
    // crf (18 = visually lossless) is also the hardware QP target. The chosen
    // encoder is surfaced in the status line.
    const QString baseFilter = QStringLiteral("ass=") + escapeFilterPath(tempAss);
    const VideoEncodePlan plan = buildVideoEncodePlan(baseFilter, crf, false /*stillImage*/, forceSoftware);
    m_usedHw = (plan.label == QLatin1String("NVENC") ||
                plan.label == QLatin1String("QuickSync") ||
                plan.label == QLatin1String("VAAPI"));
    m_progressLabel = tr("Burning subtitles...") + QStringLiteral(" [%1]").arg(plan.label);

    // Verified command:
    //   ffmpeg -y [<vaapi_device>] -i <video> -vf ass=<escaped ass>[,format=...,hwupload]
    //          <encoder args> -r <fps> -c:a copy -progress pipe:1 <out>
    QStringList args;
    args << "-y" << "-hide_banner" << "-nostats" << plan.globalArgs
         << "-i" << videoPath
         << "-vf" << plan.filter
         << plan.args;
    // A normal fps so ASS effects (\move, transforms) animate smoothly even
    // when the source is low-fps (e.g. a 5fps radio video).
    args << "-r" << QString::number(qMax(1, fps))
         << "-c:a" << "copy";   // audio is unchanged by subtitle burn
    args << "-progress" << "pipe:1"
         << outputPath;

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FFmpegRunner::parseEncodeProgress, Qt::UniqueConnection);

    qDebug().noquote() << "[ffmpeg] burn-subtitles cmd:" << args
                       << (forceSoftware ? "(software fallback)" : "");
    emit progress(0, 100, m_progressLabel);
    m_process->start("ffmpeg", args);
    qDebug() << "[ffmpeg] burn-subtitles started; watchdog" << (WATCHDOG_STALL_MS / 1000) << "s";
    // Arm the sliding-window stall watchdog (see createRadioVideoImpl).
    m_lastProgressAt.start();
    m_watchdog->start(WATCHDOG_POLL_MS);
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
    // Only a STRICTLY POSITIVE out_time_us proves the encoder has produced a
    // frame past time 0. A value of 0 appears in early -progress blocks before
    // any frame is done, and a deadlocked encoder can emit those 0-time blocks
    // indefinitely - so 0 must NOT refresh the stall watchdog (that was the
    // v1.9.1 bug: AMF spammed out_time_us=0, the watchdog was disarmed, and the
    // encode hung at 0% forever).
    if (cur > 0.0) {
        m_gotProgress = true;
        // Refresh the sliding-window stall watchdog ONLY when out_time_us
        // strictly advances beyond the last value seen. A stalled-but-alive
        // encoder can keep emitting -progress blocks with a frozen out_time_us;
        // without the strict-advance check those would refresh the window and
        // hide the stall. The recurring watchdog (onWatchdogTimeout) fires if
        // this doesn't happen for WATCHDOG_STALL_MS.
        if (cur > m_lastOutTimeSec + 0.001) {
            m_lastOutTimeSec = cur;
            m_lastProgressAt.start();
        }
    }

    if (m_progressTotal <= 0.0) return;
    const int pct = qBound(0, static_cast<int>(cur / m_progressTotal * 100.0), 99);
    emit progress(pct, 100, m_progressLabel);
    // Throttled console trace: one line per whole-percent change. On a healthy
    // encode this proves frames are flowing; on a hung one it stays silent (the
    // absence is itself the diagnostic - "started" then nothing).
    if (pct != m_lastLoggedPct) {
        m_lastLoggedPct = pct;
        qDebug().noquote() << "[ffmpeg] progress" << pct << "% ("
                           << QString::number(cur, 'f', 1) << "s /"
                           << QString::number(m_progressTotal, 'f', 1) << "s )";
    }
}
