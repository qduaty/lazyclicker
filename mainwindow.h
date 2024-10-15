#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "mainwindowwithsettings.h"
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public MainWindowWithSettings
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionAuto_arrange_windows_toggled(bool);
    void on_maxIncrease_valueChanged(int);
private:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);

    Ui::MainWindow *ui;
    QSystemTrayIcon* trayIcon;
    QTimer timer;
};
#endif // MAINWINDOW_H
