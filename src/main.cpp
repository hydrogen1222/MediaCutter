#include <QApplication>
#include <QFile>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // Load stylesheet
    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        a.setStyleSheet(styleSheet);
    } else {
        // Fallback for development if resources not compiled
        QFile styleFileLocal("src/style.qss");
        if (styleFileLocal.open(QFile::ReadOnly)) {
            QString styleSheet = QLatin1String(styleFileLocal.readAll());
            a.setStyleSheet(styleSheet);
        }
    }

    MainWindow w;
    w.show();
    return a.exec();
}
