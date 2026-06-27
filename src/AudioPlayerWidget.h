#pragma once
#include <QObject>
#include <QString>
#include <mpv/client.h>

// Audio-only libmpv controller. Mirrors the mpv glue in MpvWidget (create/init,
// property observation, wakeup callback, event loop, loadfile/seek/setVolume)
// but creates NO render context and does NO OpenGL — it only decodes/plays
// audio. Used by RadioVideoDialog to preview the soundtrack and set Mark In/Out.
//
// (A headless playback controller is naturally a QObject, not a QWidget: it has
// no on-screen content of its own — the dialog owns the transport controls and
// wires them to this engine.)
class AudioPlayerWidget : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayerWidget(QObject *parent = nullptr);
    ~AudioPlayerWidget() override;

    void loadFile(const QString &path);
    void playPause();
    void setPaused(bool paused);
    void seek(double seconds);
    void setVolume(int percent);
    double getDuration();
    double getCurrentTime();
    QString getFileName() const { return m_fileName; }

signals:
    void timeChanged(double time);
    void durationChanged(double duration);
    void volumeChanged(int volume);
    // true while actually playing; lets the dialog toggle its Play/Pause label.
    void playingChanged(bool playing);

private slots:
    void onMpvEvents();

private:
    static void onMpvWakeup(void *ctx);
    void handleMpvEvent(mpv_event *event);

    mpv_handle *m_mpv = nullptr;
    QString m_fileName;
};
