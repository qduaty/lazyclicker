#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <QSystemTrayIcon>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    Ui::MainWindow *ui;
    QSystemTrayIcon* trayIcon;
    // QTimer timer;
    QSettings settings, settingsRunOnStartup;
};
#endif // MAINWINDOW_H
