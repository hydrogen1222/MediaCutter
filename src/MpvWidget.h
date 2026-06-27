// MpvWidget.h
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QString>
#include <mpv/client.h>
#include <mpv/render_gl.h>

class MpvWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    void loadFile(const QString &path);
    // Queue an external subtitle file (e.g. a .ass) to load alongside the next
    // loadFile() call, rendered by libass. Must be set BEFORE loadFile() because
    // mpv binds sub-files at open time. Used by the subtitle-burn preview.
    void setExternalSubtitle(const QString &path);
    void playPause();
    void seek(double seconds);
    void setVolume(int percent);
    double getDuration();
    double getCurrentTime();
    QString getFileName() const { return m_fileName; }
    bool hasVideo();

signals:
    void timeChanged(double time);
    void durationChanged(double duration);
    void volumeChanged(int volume);

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void onMpvEvents();
    void maybeUpdate();

private:
    static void onMpvWakeup(void *ctx);
    static void onMpvRenderUpdate(void *ctx);
    static void *getProcAddress(void *ctx, const char *name);

    void handleMpvEvent(mpv_event *event);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_renderCtx = nullptr;
    QString m_fileName;
};
