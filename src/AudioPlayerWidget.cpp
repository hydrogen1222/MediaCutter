// AudioPlayerWidget.cpp
#include "AudioPlayerWidget.h"
#include <QMetaObject>
#include <QDebug>
#include <QDir>
#include <stdexcept>
#include <cstring>

void AudioPlayerWidget::onMpvWakeup(void *ctx) {
    QMetaObject::invokeMethod(static_cast<AudioPlayerWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

AudioPlayerWidget::AudioPlayerWidget(QObject *parent) : QObject(parent) {
    m_mpv = mpv_create();
    if (!m_mpv) throw std::runtime_error("could not create mpv context");

    mpv_request_log_messages(m_mpv, "info");

    if (mpv_initialize(m_mpv) < 0) {
        throw std::runtime_error("could not initialize mpv");
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause",   MPV_FORMAT_DOUBLE);

    mpv_set_wakeup_callback(m_mpv, AudioPlayerWidget::onMpvWakeup, this);

    // Keep open after EOF so we can seek and replay.
    mpv_set_property_string(m_mpv, "keep-open", "yes");
    // Frame-accurate seeking so the Mark In/Out times are exact.
    mpv_set_property_string(m_mpv, "hr-seek", "yes");
    // Audio-only intent: never decode/select a video stream, so a video file
    // accidentally picked here cannot try to init a video output (we have none).
    mpv_set_property_string(m_mpv, "vid", "no");
}

AudioPlayerWidget::~AudioPlayerWidget() {
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void AudioPlayerWidget::onMpvEvents() {
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        handleMpvEvent(event);
    }
}

void AudioPlayerWidget::handleMpvEvent(mpv_event *event) {
    switch (event->event_id) {
        case MPV_EVENT_LOG_MESSAGE: {
            auto *msg = static_cast<mpv_event_log_message *>(event->data);
            qDebug() << "mpv:" << msg->prefix << ":" << msg->text;
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (prop->format != MPV_FORMAT_DOUBLE || !prop->data) break;
            double val = *static_cast<double *>(prop->data);
            if (std::strcmp(prop->name, "time-pos") == 0) {
                emit timeChanged(val);
            } else if (std::strcmp(prop->name, "duration") == 0) {
                emit durationChanged(val);
            } else if (std::strcmp(prop->name, "volume") == 0) {
                emit volumeChanged(static_cast<int>(val));
            } else if (std::strcmp(prop->name, "pause") == 0) {
                // pause is 0 while playing, nonzero while paused.
                emit playingChanged(val == 0.0);
            }
            break;
        }
        default:
            break;
    }
}

void AudioPlayerWidget::loadFile(const QString &path) {
    m_fileName = path;
    QString nativePath = QDir::toNativeSeparators(path);
    QByteArray utf8Path = nativePath.toUtf8();
    const char *cmd[] = { "loadfile", utf8Path.constData(), nullptr };

    qDebug() << "mpv: loading audio:" << nativePath;
    int err = mpv_command(m_mpv, cmd);
    if (err < 0) {
        qDebug() << "mpv loadfile error:" << mpv_error_string(err);
    }
}

void AudioPlayerWidget::playPause() {
    const char *cmd[] = { "cycle", "pause", nullptr };
    mpv_command(m_mpv, cmd);
}

void AudioPlayerWidget::setPaused(bool paused) {
    int flag = paused ? 1 : 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void AudioPlayerWidget::seek(double seconds) {
    QByteArray secBytes = QString::number(seconds, 'f', 3).toUtf8();
    const char *cmd[] = { "seek", secBytes.constData(), "absolute+exact", nullptr };
    int err = mpv_command(m_mpv, cmd);
    if (err < 0) {
        qDebug() << "mpv seek error:" << mpv_error_string(err);
    }
}

void AudioPlayerWidget::setVolume(int percent) {
    double vol = static_cast<double>(percent);
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

double AudioPlayerWidget::getDuration() {
    double duration = 0;
    mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
    return duration;
}

double AudioPlayerWidget::getCurrentTime() {
    double time = 0;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
    return time;
}
