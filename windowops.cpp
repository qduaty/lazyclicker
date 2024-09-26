#include "windowops.h"
#include "helpers.h"
#include <iostream>

using namespace std;

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

WINBOOL enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam)
{
    auto &monitorRects = *reinterpret_cast<map<HMONITOR, RECT>*>(lParam);
    monitorRects[monitor] = *pRect;
    return TRUE;
}

size_t calculateRectArea(RECT rect)
{
    return (rect.bottom - rect.top) * (rect.right - rect.left);
}

int findCorners(RECT window, RECT monitor)
{
    enum Side: int {none, top=1, right=2, bottom=4, left=8};
    int sides = int(Side::none);
    const float snapDistance = 0.1;
    size_t maxX = snapDistance * (monitor.right - monitor.left);
    size_t maxY = snapDistance * (monitor.bottom - monitor.top);
    if(abs(window.bottom - monitor.bottom) < maxY) sides |= int(Side::bottom);
    if(abs(window.top - monitor.top) < maxY) sides |= int(Side::top);
    if(abs(window.left - monitor.left) < maxX) sides |= int(Side::left);
    if(abs(window.right - monitor.right) < maxX) sides |= int(Side::right);
    int result = (int)Corner::none;
    if(sides & Side::top && sides & Side::left) result |= (int)Corner::topleft;
    if(sides & Side::top && sides & Side::right) result |= (int)Corner::topright;
    if(sides & Side::bottom && sides & Side::left) result |= (int)Corner::bottomleft;
    if(sides & Side::bottom && sides & Side::right) result |= (int)Corner::bottomright;
    return result;
}

RECT trimAndMoveToMonitor(RECT windowRect, RECT monRect)
{
    int maxw = monRect.right - monRect.left;
    int maxh = monRect.bottom - monRect.top;
    windowRect.right -= max(0L, windowRect.right - windowRect.left - maxw);
    windowRect.bottom -= max(0L, windowRect.bottom - windowRect.top - maxh);
    windowRect.left -= max(0L, windowRect.right - monRect.right);
    windowRect.top -= max(0L, windowRect.bottom - monRect.bottom);
    return windowRect;
}

QPoint windowDistanceFromCorner(RECT wrect, RECT mrect, Corner c)
{
    QPoint mcorner, wcorner;
    switch(c){
    case Corner::bottomleft:
        mcorner = {mrect.left, mrect.bottom};
        wcorner = {wrect.left, wrect.bottom};
        break;
    case Corner::bottomright:
        mcorner = {mrect.right, mrect.bottom};
        wcorner = {wrect.right, wrect.bottom};
        break;
    case Corner::topleft:
        mcorner = {mrect.left, mrect.top};
        wcorner = {wrect.left, wrect.top};
        break;
    case Corner::topright:
        mcorner = {mrect.right, mrect.top};
        wcorner = {wrect.right, wrect.top};
        break;
    default:
        break;
    }
    return {wcorner.x() - mcorner.x(), wcorner.y() - mcorner.y()};
}

void arrangeWindowsInMonitorCorners(const map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> &windowsOrderInCorners,
                    const map<HMONITOR, RECT> &monitorRects,
                    map<HWND, RECT> &windowRects,
                    size_t unitSize)
{
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        for(auto &[corner, windows]: mcvw)
        {
            for(int i = 0; i < windows.size(); i++)
            {
                auto &wrect = windowRects.at(windows[i]);
                auto mrect = monitorRects.at(mon);
                // TODO this resets all windows to {0, 0}, don't remove
                // MoveWindow(vw[i], 0, 0, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
                RECT newRect;
                if(corner & Corner::left)
                {
                    newRect.left = mrect.left + i * unitSize;
                    newRect.right = wrect.right + newRect.left - wrect.left;
                }
                else
                {
                    newRect.right = mrect.right - i * unitSize;
                    newRect.left = wrect.left + newRect.right - wrect.right;
                }
                if(corner & Corner::top)
                {
                    newRect.top = mrect.top + (windows.size() - i - 1) * unitSize;
                    newRect.bottom = wrect.bottom + newRect.top - wrect.top;
                }
                else
                {
                    newRect.bottom = mrect.bottom - (windows.size() - i  - 1) * unitSize;
                    newRect.top = wrect.top + newRect.bottom - wrect.bottom;
                }
                wrect = newRect;
                cout << "Will move window " << windows[i] << '('<<i<<')'<<" to: " << wrect.left << ':' <<  wrect.top << endl;
                MoveWindow(windows[i], wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
            }
        }
        // for(auto &[corner, windows]: mcvw)
        // {
        //     int hinc = corner == Corner::topleft || corner == Corner::bottomleft ? unitSize : -unitSize;
        //     int vinc = corner == Corner::topleft || corner == Corner::topright ? unitSize : -unitSize;
        //     int voffset0 = -vinc * windows.size();
        //     for(int i = 0; i < windows.size(); i++)
        //     {
        //         cout << "Will move window " << windows[i] << " to: " << wrect.left << ':' <<  wrect.top << endl;
        //         MoveWindow(windows[i], wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
        //     }
        // }
    }
}

HMONITOR findMainMonitor(HWND w, RECT &windowRect, const map<HMONITOR, RECT> &monitorRects)
{
    HMONITOR monitor = {};
    HDC dc = GetDC(w);
    map<HMONITOR, RECT> windowMonRects;
    EnumDisplayMonitors(dc, &windowRect, enumMonitorsProc, reinterpret_cast<LPARAM>(&windowMonRects));
    ReleaseDC(w, dc);
    if (windowMonRects.size())
    {
        size_t maxArea = 0;
        // cout << "Monitors for window:" << w << endl;
        for (auto &[m, r] : windowMonRects)
        {
            // MONITORINFOEXA info{sizeof(MONITORINFOEXA)};
            // GetMonitorInfoA(m, &info);
            // cout << info.szDevice << ": " << r.left << ':' << r.top << ':' << r.right << ':' << r.bottom << std::endl;

            size_t area = calculateRectArea(r);
            if (area > maxArea) {
                monitor = m;
                maxArea = area;
            }
        }
        if (maxArea) windowRect = trimAndMoveToMonitor(windowRect, monitorRects.at(monitor));
    }
    return monitor;
}
