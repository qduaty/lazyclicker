#include "mainwindow.h"
#include "windowops.h"
#include <QApplication>
#include <iostream>
#include <windows.h>
#include <helpers.h>
#include <QSystemTrayIcon>
#include <QMessageBox>

using namespace std;

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
    constexpr int taskbarHeight = 48;
    for(auto &[m,r]:monitorRects) r.bottom -= taskbarHeight;
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
        int i = 3;
        for(auto&[s, w]: mw)
        {
            Corner corners[] {Corner::topleft, Corner::topright, Corner::bottomright, Corner::bottomleft};
            Corner corner = corners[i%4];
            windowsOrderInCorners[m][corner].push_back(w);
            constexpr char *cornerNames[] = {"Corner::topleft", "Corner::topright", "Corner::bottomright", "Corner::bottomleft"};
            cout << w << ":" << cornerNames[i%4] << endl;
            i++;
        }
    }

    arrangeWindowsInMonitorCorners(windowsOrderInCorners, monitorRects, windowRects, 40);

    // To fit all windows in all corners
    // 1. Calculate position for all corners the window may cover, ie. if it crosses the line of window corners in every monitor corner
    // 2. In pairs of monitor corners (top-bottom) assign the higher value
    // 3. When placing windows, always move corners horizontally but skip vertical step if there's no window assigned to a position
    ;

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
