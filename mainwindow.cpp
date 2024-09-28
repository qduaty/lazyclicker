#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "helpers.h"
#include "windowops.h"
#include <QDir>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    settings("HKEY_CURRENT_USER\\Software\\qduaty\\" + qApp->applicationName(), QSettings::NativeFormat),
    settingsRunOnStartup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat),
    trayIcon(new QSystemTrayIcon(this))
{
    ui->setupUi(this);
    SHOWDEBUG(QDir::current().absolutePath().toLocal8Bit().toStdString());
    trayIcon->setIcon(QIcon("../../mainicon.png"));
    trayIcon->show();
    connect(trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
    auto trayIconMenu = new QMenu(this);
    auto quitAction = new QAction(tr("&Uninstall"), this);
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitAndUnregister);
    trayIconMenu->addAction(quitAction);
    trayIcon->setContextMenu(trayIconMenu);
    registerForStartup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason)
    {
    case QSystemTrayIcon::ActivationReason::Trigger:
        processAllWindows();
        break;
    case QSystemTrayIcon::ActivationReason::DoubleClick:
        if(isHidden()) show(); else hide();
        break;
    case QSystemTrayIcon::ActivationReason::Context:
        break;
    default:
        break;
    }
}

void MainWindow::registerForStartup() {
    settingsRunOnStartup.setValue(QApplication::applicationName(),
                                  QCoreApplication::applicationFilePath().replace('/', "\\"));
}

void MainWindow::quitAndUnregister() {
    settingsRunOnStartup.remove(QApplication::applicationName());
    settings.clear();
    QCoreApplication::instance()->quit();
}
