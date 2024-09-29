#ifndef MAINWINDOWWITHSETTINGS_H
#define MAINWINDOWWITHSETTINGS_H

#include <QMainWindow>
#include <QSettings>


class MainWindowWithSettings: public QMainWindow
{
public:
    MainWindowWithSettings(QWidget *parent);
    virtual void saveSettings();
    virtual void loadSettings();
    void registerForStartup();
    void quitAndUnregister();
protected:
    QSettings settings, settingsRunOnStartup;
};

#endif // MAINWINDOWWITHSETTINGS_H
