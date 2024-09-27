#include "mainwindow.h"
#include "windowops.h"
#include <QApplication>
#include <iostream>
#include <windows.h>
#include <helpers.h>
#include <QSystemTrayIcon>
#include <QMessageBox>

using namespace std;
//constexpr int taskbarHeight = 24;
constexpr int taskbarHeight = 48;
constexpr int unitSize = 40;

int main(int argc, char *argv[])
{
    cout << "Enumerating Windows...\n";
    map<HWND, RECT> windowRects;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));
    // int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    // int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    cout << "Enumerating Monitors...\n";
    map<HMONITOR, RECT> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&monitorRects));
    for(auto &[m,r]: monitorRects) r.bottom -= taskbarHeight;
    map<HMONITOR, string> monitorNames;
    // just list monitor names
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        monitorNames[m] = info.szDevice;
        cout << info.szDevice << ": " << r.left << ':' << r.top << ':' << r.right << ':' << r.bottom << std::endl;
    }

    // find main monitor for each window
    map<HWND, HMONITOR> windowMonitor;
    for (auto &[w, r] : windowRects)
    {
        HMONITOR monitor = findMainMonitor(w, r, monitorRects);
        // if(!monitor) monitor = monitorRects.rbegin()->first; // that would steal space for invisible windows, consider filtering them better because some fall into this category incorrectly
        if(monitor) windowMonitor[w] = monitor;
        cout << w << ":" << monitorNames[monitor] << endl;
    }

    // sort windows according to size and distribute them in corners
    map<HMONITOR, map<size_t, HWND>> windowsOnMonitor;
    for(auto &[w, m]: windowMonitor) windowsOnMonitor[m].insert({calculateRectArea(windowRects[w]), w});
    map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> windowsOrderInCorners;
    for(auto &[m, mw]: windowsOnMonitor)
    {
        for(int i = 0; i < 4; i++) windowsOrderInCorners[m][Corner(i)]; // ensure all window sets exist
        int i = 3;
        for(auto&[s, w]: mw)
        {
            Corner corner = Corner(i%4);
            windowsOrderInCorners[m][corner].push_back(w);
            constexpr const char *cornerNames[] = {"Corner::topleft", "Corner::topright", "Corner::bottomleft", "Corner::bottomright"};
            cout << w << ":" << cornerNames[i%4] << endl;
            i++;
        }
    }

    arrangeWindowsInMonitorCorners(windowsOrderInCorners, monitorRects, windowRects, unitSize);

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
