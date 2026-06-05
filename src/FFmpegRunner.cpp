#include "FFmpegRunner.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QFileInfo>

FFmpegRunner::FFmpegRunner(QObject *parent) : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::finished, this, &FFmpegRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &FFmpegRunner::onProcessError);
}

void FFmpegRunner::cutAndMerge(const QString &input, const std::vector<Segment> &segments, const QString &output, bool mergeAfterCut) {
    m_input = input;
    m_segments = segments;
    m_output = output;
    m_currentIndex = 0;
    m_isMerging = false;
    m_mergeAfterCut = mergeAfterCut;
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
        
        QFileInfo info(m_input);
        QString ext = info.suffix();
        if (ext.isEmpty()) ext = "mp4";
        
        QString tempFile = m_tempDir.path() + QString("/temp_%1.%2").arg(m_currentIndex).arg(ext);
        m_tempFiles << tempFile;

        QStringList args;
        args << "-ss" << QString::number(s.start, 'f', 3)
             << "-to" << QString::number(s.end, 'f', 3)
             << "-i" << m_input
             << "-map" << "0"
             << "-c" << "copy"
             << "-avoid_negative_ts" << "make_zero"
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

    for (int i = 0; i < m_tempFiles.size(); ++i) {
        QString newPath = targetDir + "/" + baseName + QString("_%1.%2").arg(i + 1).arg(ext);
        QFile::remove(newPath);
        if (!QFile::rename(m_tempFiles[i], newPath)) {
            // If rename fails (e.g. cross-device boundary), fallback to copy + delete
            if (QFile::copy(m_tempFiles[i], newPath)) {
                QFile::remove(m_tempFiles[i]);
            } else {
                emit finished(false, QString("Failed to move segment %1 to destination (rename and copy failed)").arg(i + 1));
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
