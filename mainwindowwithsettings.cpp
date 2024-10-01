#include "mainwindowwithsettings.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QApplication>

MainWindowWithSettings::MainWindowWithSettings(QWidget *parent):
    QMainWindow(parent),
    settings("HKEY_CURRENT_USER\\Software\\qduaty\\" + qApp->applicationName(), QSettings::NativeFormat),
    settingsRunOnStartup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat)
 {}

void MainWindowWithSettings::saveSettings()
{
    settings.beginGroup("Preferences");
    for(auto child: findChildren<QCheckBox*>())
        settings.setValue(child->objectName(), child->isChecked());
    for(auto child: findChildren<QAction*>())
        if(child->isCheckable())
            settings.setValue(child->objectName(), child->isChecked());
    for(auto child: findChildren<QRadioButton*>())
        settings.setValue(child->objectName(), child->isChecked());
    for(auto child: findChildren<QLineEdit*>())
        if(!dynamic_cast<QSpinBox*>(child->parent()) && !dynamic_cast<QDoubleSpinBox*>(child->parent()))
            settings.setValue(child->objectName(), child->text());
    for(auto child: findChildren<QSlider*>())
        settings.setValue(child->objectName(), child->value());
    for(auto child: findChildren<QSpinBox*>())
        settings.setValue(child->objectName(), child->value());
    for(auto child: findChildren<QDoubleSpinBox*>())
        settings.setValue(child->objectName(), child->value());
    for(auto child: findChildren<QTabWidget*>())
        settings.setValue(child->objectName(), child->currentIndex());
    settings.endGroup();
}

void MainWindowWithSettings::loadSettings()
{
    settings.beginGroup("Preferences");
    if(settings.value("first_time", true).toBool())
    {
        saveSettings();
        settings.setValue("first_time", false);
    }
    else
    {
        for(auto child: findChildren<QCheckBox*>())
            child->setChecked(settings.value(child->objectName(), false).toBool());
        for(auto child: findChildren<QAction*>())
            if(child->isCheckable())
                child->setChecked(settings.value(child->objectName(), false).toBool());
        for(auto child: findChildren<QRadioButton*>())
            child->setChecked(settings.value(child->objectName(), false).toBool());
        for(auto child: findChildren<QLineEdit*>())
            if(!dynamic_cast<QSpinBox*>(child->parent()) && !dynamic_cast<QDoubleSpinBox*>(child->parent()))
                child->setText(settings.value(child->objectName(), "").toString());
        for(auto child: findChildren<QSlider*>())
            child->setValue(settings.value(child->objectName(), 0).toInt());
        for(auto child: findChildren<QSpinBox*>())
            child->setValue(settings.value(child->objectName(), 0).toInt());
        for(auto child: findChildren<QDoubleSpinBox*>())
            child->setValue(settings.value(child->objectName(), 0).toDouble());
        for(auto child: findChildren<QTabWidget*>())
            child->setCurrentIndex(settings.value(child->objectName(), 0).toInt());
    }
    settings.endGroup();
}
void MainWindowWithSettings::registerForStartup() {
    settingsRunOnStartup.setValue(QApplication::applicationName(),
                                  QCoreApplication::applicationFilePath().replace('/', "\\"));
}

void MainWindowWithSettings::quitAndUnregister() {
    settingsRunOnStartup.remove(QApplication::applicationName());
    settings.clear();
    QCoreApplication::instance()->quit();
}
