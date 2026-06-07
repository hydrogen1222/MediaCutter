#include "FFmpegRunner.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

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
        emit finished(false, "No segments to export");
        return;
    }

    if (!m_tempDir.isValid()) {
        emit finished(false, "Failed to create temporary directory");
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
            if (outputExt == "flac") {
                args << "-c:a" << "flac";
            } else if (outputExt == "wav") {
                args << "-c:a" << "pcm_s16le";
            } else if (outputExt == "mp3") {
                args << "-c:a" << "libmp3lame" << "-q:a" << "0";
            } else if (outputExt == "ogg") {
                args << "-c:a" << "libvorbis" << "-q:a" << "6";
            } else if (outputExt == "opus") {
                args << "-c:a" << "libopus" << "-b:a" << "128k";
            } else if (outputExt == "aac" || outputExt == "m4a") {
                args << "-c:a" << "aac" << "-b:a" << "192k";
            } else if (outputExt == "wma") {
                args << "-c:a" << "wmav2" << "-b:a" << "192k";
            } else if (outputExt == "mka") {
                // MKA is a container; auto-select based on source codec
            }
            // else: let FFmpeg auto-select encoder for other formats
        } else {
            // ===== Video export =====
            // For video, input-seeking (-ss before -i) is fast and works well
            // because FFmpeg seeks to the nearest keyframe.
            args << "-ss" << QString::number(s.start, 'f', 6)
                 << "-to" << QString::number(s.end, 'f', 6)
                 << "-i" << m_segments[m_currentIndex].filePath
                 << "-map" << "0";

            if (sameExtension) {
                args << "-c" << "copy";
            } else {
                args << "-c:v" << "libx264"
                     << "-preset" << "superfast"
                     << "-crf" << "18"
                     << "-c:a" << "aac";
            }
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
            emit finished(false, "Failed to create concat file");
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
            emit finished(true, "Export completed successfully");
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
                emit finished(false, QString("Failed to move segment %1 to destination (rename and copy failed)").arg(startIndex + i));
                return;
            }
        }
    }

    emit finished(true, "Individual segments exported successfully");
}

void FFmpegRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        if (!m_isMerging) {
            m_currentIndex++;
        }
        runNextStep();
    } else {
        QString errorMsg = m_process->readAllStandardError();
        emit finished(false, QString("FFmpeg process failed: %1").arg(errorMsg));
    }
}

void FFmpegRunner::onProcessError(QProcess::ProcessError error) {
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "FFmpeg executable not found or failed to start. Check if FFmpeg is installed and in PATH.";
            break;
        case QProcess::Crashed:
            errorMsg = "FFmpeg process crashed.";
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
    emit finished(false, errorMsg);
}
