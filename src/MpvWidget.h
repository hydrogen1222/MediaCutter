// MpvWidget.h
#pragma once
#include <QWidget>
#include <mpv/client.h>
#include <QTimer>

class MpvWidget : public QWidget {
    Q_OBJECT
public:
    MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget();
    void loadFile(const QString &path);
    void playPause();
    void seek(double seconds);
    void setVolume(int percent);
    double getDuration();
    double getCurrentTime();
    QString getFileName() const { return m_fileName; }

signals:
    void timeChanged(double time);
    void durationChanged(double duration);
    void volumeChanged(int volume);

private slots:
    void pollEvents();

private:
    mpv_handle *m_mpv;
    QTimer *m_timer;
    QString m_fileName;
};
