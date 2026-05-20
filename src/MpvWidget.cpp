// MpvWidget.cpp
#include "MpvWidget.h"
#include <stdexcept>
#include <QDebug>
#include <QDir>

MpvWidget::MpvWidget(QWidget *parent) : QWidget(parent) {
    // 关键：告诉 Qt 这个窗口由外部系统（mpv）绘制，防止 Qt 自己的背景覆盖视频
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);

    m_mpv = mpv_create();
    if (!m_mpv) throw std::runtime_error("could not create mpv context");
    
    // 开启调试日志，方便排查加载失败的原因
    mpv_request_log_messages(m_mpv, "info");

    // 设置视频输出后端（在 Windows 上通常是 d3d11 或 gpu）
    const char *vo = "gpu";
    mpv_set_property_string(m_mpv, "vo", vo);
    // 开启硬件加速
    const char *hwdec = "auto";
    mpv_set_property_string(m_mpv, "hwdec", hwdec);

    // Set "wid" property to embed mpv in this widget
    int64_t wid = (int64_t)winId();
    mpv_set_property(m_mpv, "wid", MPV_FORMAT_INT64, &wid);
    
    // Initialize mpv
    if (mpv_initialize(m_mpv) < 0) {
        throw std::runtime_error("could not initialize mpv");
    }

    // Observe properties
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MpvWidget::pollEvents);
    m_timer->start(20); // 稍微加快轮询速度
}

MpvWidget::~MpvWidget() {
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
    }
}

void MpvWidget::loadFile(const QString &path) {
    m_fileName = path;
    QString nativePath = QDir::toNativeSeparators(path);
    
    // 关键修复：必须先转为 QByteArray 并保持其实例存活，直到 mpv_command 执行完毕
    QByteArray utf8Path = nativePath.toUtf8();
    const char *cmd[] = {"loadfile", utf8Path.constData(), nullptr};
    
    qDebug() << "mpv: loading file:" << nativePath;
    int err = mpv_command(m_mpv, cmd);
    if (err < 0) {
        qDebug() << "mpv loadfile error:" << mpv_error_string(err);
    }
}

void MpvWidget::playPause() {
    // 显式保留指令，防止指针失效
    const char *cmd[] = {"cycle", "pause", nullptr};
    mpv_command(m_mpv, cmd);
}

void MpvWidget::seek(double seconds) {
    // 关键修复：必须保持 QByteArray 存活直到命令执行完
    QByteArray secBytes = QString::number(seconds, 'f', 3).toUtf8();
    const char *cmd[] = {"seek", secBytes.constData(), "absolute", nullptr};
    
    int err = mpv_command(m_mpv, cmd);
    if (err < 0) {
        qDebug() << "mpv seek error:" << mpv_error_string(err);
    }
}

void MpvWidget::setVolume(int percent) {
    double vol = static_cast<double>(percent);
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

double MpvWidget::getDuration() {
    double duration = 0;
    mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
    return duration;
}

double MpvWidget::getCurrentTime() {
    double time = 0;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
    return time;
}

void MpvWidget::pollEvents() {
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        
        switch (event->event_id) {
            case MPV_EVENT_LOG_MESSAGE: {
                mpv_event_log_message *msg = (mpv_event_log_message *)event->data;
                qDebug() << "mpv:" << msg->prefix << ":" << msg->text;
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property *prop = (mpv_event_property *)event->data;
                if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                    emit timeChanged(*(double *)prop->data);
                } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                    emit durationChanged(*(double *)prop->data);
                } else if (strcmp(prop->name, "volume") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                    emit volumeChanged(static_cast<int>(*(double *)prop->data));
                }
                break;
            }
            default:
                break;
        }
    }
}
