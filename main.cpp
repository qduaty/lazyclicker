#include "mainwindow.h"
#include <QApplication>
#include <windows.h>

#include <QSystemTrayIcon>
#include <QMessageBox>

using namespace std;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
    MainWindow w;
    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        QMessageBox::critical(nullptr, qApp->applicationName(), QObject::tr("I couldn't detect any system tray on this system."));
        return 1;
    }
    // w.setWindowFlags(Qt::Popup);
    // w.show();
    return a.exec();
}
