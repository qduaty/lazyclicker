#include "mainwindow.h"

#include <QApplication>
#include <iostream>
#include <windows.h>
#include <helpers.h>
#include <QSystemTrayIcon>
#include <QMessageBox>
using namespace std;

map<HWND, RECT> windows;
map<HMONITOR, RECT> monitorRects;
map<HWND, HMONITOR> windowMonitor;

// https://stackoverflow.com/questions/7277366/why-does-enumwindows-return-more-windows-than-i-expected
BOOL IsAltTabWindow(HWND hwnd)
{
    TITLEBARINFO ti {sizeof(TITLEBARINFO)};
    HWND hwndTry, hwndWalk = nullptr;

    if(!IsWindowVisible(hwnd)) return FALSE;

    hwndTry = GetAncestor(hwnd, GA_ROOTOWNER);
    while(hwndTry != hwndWalk)
    {
        hwndWalk = hwndTry;
        hwndTry = GetLastActivePopup(hwndWalk);
        if(IsWindowVisible(hwndTry)) break;
    }
    if(hwndWalk != hwnd) return FALSE;

    // the following removes some task tray programs and "Program Manager"
    GetTitleBarInfo(hwnd, &ti);
    if(ti.rgstate[0] & STATE_SYSTEM_INVISIBLE) return FALSE;

    // Tool windows should not be displayed either, these do not appear in the
    // task bar.
    if(GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return FALSE;

    return TRUE;
}

BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam)
{
    auto &windows = *reinterpret_cast<map<HWND, RECT>*>(lParam);
    if(!IsAltTabWindow(hWnd)) return TRUE;

    int length = GetWindowTextLength(hWnd);
    if(!length) return TRUE;
    char buffer[length + 1];
    GetWindowTextA(hWnd, buffer, length + 1);

    RECT &rect = windows[hWnd];
    GetWindowRect(hWnd, &rect);

    cout << hWnd << ": " << buffer << ':' << rect.left << ':' << rect.top << ':' << rect.right << ':' << rect.bottom << std::endl;
    return TRUE;
}

BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam)
{
    auto &monitorRects = *reinterpret_cast<map<HMONITOR, RECT>*>(lParam);
    monitorRects[monitor] = *pRect;
    return TRUE;
}

inline size_t calculateArea(RECT rect)
{
    return (rect.bottom - rect.top) * (rect.right - rect.left);
}

enum class corner: int {none, topleft=1, topright=2, bottomleft=4, bottomright=8};

int findCorners(RECT window, RECT monitor)
{
    enum side: int {none, top=1, right=2, bottom=4, left=8};
    int sides = int(side::none);
    const float snapDistance = 0.1;
    size_t maxX = snapDistance * (monitor.right - monitor.left);
    size_t maxY = snapDistance * (monitor.bottom - monitor.top);
    if(abs(window.bottom - monitor.bottom) < maxY) sides |= int(side::bottom);
    if(abs(window.top - monitor.top) < maxY) sides |= int(side::top);
    if(abs(window.left - monitor.left) < maxX) sides |= int(side::left);
    if(abs(window.right - monitor.right) < maxX) sides |= int(side::right);
    int result = (int)corner::none;
    if(sides & side::top && sides & side::left) result |= (int)corner::topleft;
    if(sides & side::top && sides & side::right) result |= (int)corner::topright;
    if(sides & side::bottom && sides & side::left) result |= (int)corner::bottomleft;
    if(sides & side::bottom && sides & side::right) result |= (int)corner::bottomright;
    return result;
}

RECT trimAndMoveToMonitor(RECT window, RECT monitor)
{
    int maxw = monitor.right - monitor.left;
    int maxh = monitor.bottom - monitor.top;
    window.right -= max(0L, window.right - window.left - maxw);
    window.bottom -= max(0L, window.bottom - window.top - maxh);
    window.left -= max(0L, window.right - monitor.right);
    window.top -= max(0L, window.bottom - monitor.bottom);
    return window;
}

QPoint windowDistanceFromCorner(RECT wrect, RECT mrect, corner c)
{
    QPoint mcorner, wcorner;
    switch(c){
    case corner::bottomleft:
        mcorner = {mrect.left, mrect.bottom};
        wcorner = {wrect.left, wrect.bottom};
        break;
    case corner::bottomright:
        mcorner = {mrect.right, mrect.bottom};
        wcorner = {wrect.right, wrect.bottom};
        break;
    case corner::topleft:
        mcorner = {mrect.left, mrect.top};
        wcorner = {wrect.left, wrect.top};
        break;
    case corner::topright:
        mcorner = {mrect.right, mrect.top};
        wcorner = {wrect.right, wrect.top};
        break;
    default:
        break;
    }
    return {wcorner.x() - mcorner.x(), wcorner.y() - mcorner.y()};
}

void arrangeCorners(const map<HMONITOR, map<corner, vector<HWND>>>& windowsOnMonitors, size_t unitSize)
{
    for(auto &[m, mcvw]: windowsOnMonitors)
        for(auto &[c, vw]: mcvw)
        {
            int hinc = c == corner::topleft || c == corner::bottomleft ? unitSize : -unitSize;
            int vinc = c == corner::bottomright || c == corner::bottomleft ? unitSize : -unitSize;
            int voffset0 = -vinc * vw.size();
            SHOWDEBUG(vw.size());
            for(int i = 0; i < vw.size(); i++)
            {
                QPoint offset {i * hinc, i * vinc};
                auto wrect = windows[vw[i]];
                offset -= windowDistanceFromCorner(wrect, monitorRects[windowMonitor[vw[i]]], c);
                // TODO this resets all windows to {0, 0}, don't remove
                // MoveWindow(vw[i], 0, 0, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
                cout << "Would move window " << vw[i] << " to: " << wrect.left + offset.x() << ':' <<  wrect.top + offset.y() << endl;
                MoveWindow(vw[i], wrect.left + offset.x(), wrect.top + offset.y(), wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
            }
        }
}

int main(int argc, char *argv[])
{
    windows.clear();
    cout << "Enumerating Windows...\n";
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windows));
    // int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    // int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    monitorRects.clear();
    cout << "Enumerating Monitors...\n";
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&monitorRects));

    // just list monitor names
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        cout << info.szDevice << ": " << r.left << ':' << r.top << ':' << r.right << ':' << r.bottom << std::endl;
    }

    // find main monitor for each window
    for(auto &[w, r]: windows)
    {
        HDC dc = GetDC(w);
        decltype(monitorRects) windowMonRects;
        EnumDisplayMonitors(dc, &r, enumMonitorsProc, reinterpret_cast<LPARAM>(&windowMonRects));
        ReleaseDC(w, dc);
        if(windowMonRects.size())
        {
            size_t maxArea = 0;
            cout << "Monitors for window:" << w << endl;
            for(auto &[m, r]: windowMonRects)
            {
                MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
                GetMonitorInfoA(m, &info);
                cout << info.szDevice << ": " << r.left << ':' << r.top << ':' << r.right << ':' << r.bottom << std::endl;

                size_t area = calculateArea(r);
                if(area > maxArea)
                {
                    windowMonitor[w] = m;
                    maxArea = area;
                }
            }
            if(maxArea) windows[w] = trimAndMoveToMonitor(windows[w], monitorRects[windowMonitor[w]]);
        }
    }

    // sort windows according to size and distribute them in corners
    map<HMONITOR, map<size_t, HWND>> windowsOnMonitor;
    for(auto &[w, m]: windowMonitor) windowsOnMonitor[m].insert({calculateArea(windows[w]), w});
    map<HMONITOR, map<corner, vector<HWND>>> windowsOrderInCorners;
    for(auto &[m, mw]: windowsOnMonitor)
    {
        int i = 0;
        for(auto&[s, w]: mw)
        {
            windowsOrderInCorners[m][corner(1 << (i % 4))].push_back(w);
            i++;
        }
    }

    // move windows so they are on their position in the right corner and resize them so they don't cover other corners
    arrangeCorners(windowsOrderInCorners, 40);

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


