#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "windowops.h"
#include <QDir>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent):
    MainWindowWithSettings(parent),
    ui(new Ui::MainWindow),
    trayIcon(new QSystemTrayIcon(QIcon(":/mainicon.png"), this))
{
    ui->setupUi(this);
    loadSettings();
    trayIcon->setToolTip("Click to arrange windows");
    trayIcon->show();
    connect(trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
    auto trayIconMenu = new QMenu(this);
    connect(ui->actionQuit_and_unregister, &QAction::triggered, this, &MainWindow::quitAndUnregister);
    trayIconMenu->addAction(ui->actionAuto_arrange_windows);
    trayIconMenu->addAction(ui->actionQuit_and_unregister);
    trayIcon->setContextMenu(trayIconMenu);
    registerForStartup();
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, processAllWindows);
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::on_actionAuto_arrange_windows_toggled(bool value)
{
    if(value) timer.start();
    else timer.stop();
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason)
    {
    case QSystemTrayIcon::ActivationReason::Trigger:
        processAllWindows();
        break;
    case QSystemTrayIcon::ActivationReason::DoubleClick:
    {
        auto iconGeometry = trayIcon->geometry();
        auto w = geometry().width();
        auto h = geometry().height();
        auto x = iconGeometry.x() + iconGeometry.width() / 2 - w / 2;
        auto y = iconGeometry.y() - h;
        setGeometry(x, y, w, h);
        if(isHidden()) show(); else hide();
        break;
    }
    case QSystemTrayIcon::ActivationReason::Context:
        break;
    default:
        break;
    }
}
