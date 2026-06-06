#pragma once
#include <QMainWindow>
#include <QString>
#include <QTranslator>
#include <QLineEdit>

class MpvWidget;
class QSlider;
class QLabel;
class QPushButton;
class QTableView;
class ClipModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

private:
    void formatTime(double seconds, QLabel *label);
private slots:
    void openFile();
    void onTimeChanged(double time);
    void onDurationChanged(double duration);
    void onVolumeChanged(int volume);
    void onSliderMoved(int position);
    void onVolumeSliderChanged(int value);
    void togglePlayback();
    
    // Task 4 & 5 slots
    void markIn();
    void markOut();
    void deleteSelected();
    void moveUp();
    void moveDown();
    void exportMerged();
    void exportIndividually();

private:
    void setupUI();
    void retranslateUI();
    void switchLanguage(const QString &locale);
    void toggleTheme();

    QString getLastDirectory() const;
    void saveLastDirectory(const QString &path);
    void resetDefaultDirectory();

    MpvWidget *m_player;
    QSlider *m_scrubber;
    QLabel *m_currentTimeLabel;
    QLabel *m_durationLabel;
    QPushButton *m_playPauseBtn;
    QLineEdit *m_timecodeInput;
    QPushButton *m_openFileBtn;
    QPushButton *m_seekBack5Btn;
    QPushButton *m_seekBack1Btn;
    QPushButton *m_seekForward1Btn;
    QPushButton *m_seekForward5Btn;
    
    // Volume
    QSlider *m_volumeSlider;
    QLabel *m_volumeLabel;

    // Task 4 & 5
    QPushButton *m_markInBtn;
    QPushButton *m_markOutBtn;
    QPushButton *m_upBtn;
    QPushButton *m_downBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_exportIndBtn;
    QPushButton *m_exportMergeBtn;
    QPushButton *m_loopBtn;
    QLabel *m_queueLabel;
    bool m_isLoopingSegment = false;

    bool m_isUserSeeking = false;

    // Segment Queue
    ClipModel *m_clipModel;
    QTableView *m_queueView;
    double m_markIn = 0;
    double m_markOut = -1;
    QLabel *m_markInLabel;
    QLabel *m_markOutLabel;
    
    QString m_currentFilePath;
    QTranslator m_translator;
    bool m_translatorInstalled = false;
    bool m_isDarkTheme = true;
};
