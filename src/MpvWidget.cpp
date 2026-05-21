// MpvWidget.cpp
#include "MpvWidget.h"
#include <QOpenGLContext>
#include <QMetaObject>
#include <QDebug>
#include <QDir>
#include <stdexcept>

void *MpvWidget::getProcAddress(void *ctx, const char *name) {
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx) return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

void MpvWidget::onMpvWakeup(void *ctx) {
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onMpvRenderUpdate(void *ctx) {
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "maybeUpdate", Qt::QueuedConnection);
}

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent) {
    m_mpv = mpv_create();
    if (!m_mpv) throw std::runtime_error("could not create mpv context");

    mpv_request_log_messages(m_mpv, "info");

    if (mpv_initialize(m_mpv) < 0) {
        throw std::runtime_error("could not initialize mpv");
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);

    mpv_set_wakeup_callback(m_mpv, MpvWidget::onMpvWakeup, this);
}

MpvWidget::~MpvWidget() {
    makeCurrent();
    if (m_renderCtx) {
        mpv_render_context_free(m_renderCtx);
        m_renderCtx = nullptr;
    }
    doneCurrent();
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void MpvWidget::initializeGL() {
    mpv_opengl_init_params glInit{ &MpvWidget::getProcAddress, nullptr };
    int advanced = 1;

    mpv_render_param params[]{
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };

    if (mpv_render_context_create(&m_renderCtx, m_mpv, params) < 0) {
        throw std::runtime_error("failed to initialize mpv render context");
    }
    mpv_render_context_set_update_callback(m_renderCtx, MpvWidget::onMpvRenderUpdate, this);
}

void MpvWidget::paintGL() {
    if (!m_renderCtx) return;

    const qreal dpr = devicePixelRatioF();
    const int w = static_cast<int>(width()  * dpr);
    const int h = static_cast<int>(height() * dpr);

    mpv_opengl_fbo fbo{ static_cast<int>(defaultFramebufferObject()), w, h, 0 };
    int flipY = 1;
    mpv_render_param params[]{
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flipY },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    mpv_render_context_render(m_renderCtx, params);
}

void MpvWidget::maybeUpdate() {
    if (!m_renderCtx) return;
    // 仅当 mpv 真的有新帧时才触发重绘
    uint64_t flags = mpv_render_context_update(m_renderCtx);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        update();
    }
}

void MpvWidget::onMpvEvents() {
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        handleMpvEvent(event);
    }
}

void MpvWidget::handleMpvEvent(mpv_event *event) {
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
            if (strcmp(prop->name, "time-pos") == 0) {
                emit timeChanged(val);
            } else if (strcmp(prop->name, "duration") == 0) {
                emit durationChanged(val);
            } else if (strcmp(prop->name, "volume") == 0) {
                emit volumeChanged(static_cast<int>(val));
            }
            break;
        }
        default:
            break;
    }
}

void MpvWidget::loadFile(const QString &path) {
    m_fileName = path;
    QString nativePath = QDir::toNativeSeparators(path);
    QByteArray utf8Path = nativePath.toUtf8();
    const char *cmd[] = { "loadfile", utf8Path.constData(), nullptr };

    qDebug() << "mpv: loading file:" << nativePath;
    int err = mpv_command(m_mpv, cmd);
    if (err < 0) {
        qDebug() << "mpv loadfile error:" << mpv_error_string(err);
    }
}

void MpvWidget::playPause() {
    const char *cmd[] = { "cycle", "pause", nullptr };
    mpv_command(m_mpv, cmd);
}

void MpvWidget::seek(double seconds) {
    QByteArray secBytes = QString::number(seconds, 'f', 3).toUtf8();
    const char *cmd[] = { "seek", secBytes.constData(), "absolute", nullptr };
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
