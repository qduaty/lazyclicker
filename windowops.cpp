#include "windowops.h"
#include <iostream>
#include <psapi.h>
#include <uxtheme.h>

using namespace std;

constexpr int allowIncreaseByUnits = 2;

string GetProcessNameFromHWND(HWND hwnd)
{
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess) {
        char processName[MAX_PATH] = "<unknown>";

        if (GetModuleBaseNameA(hProcess, NULL, processName, sizeof(processName)))
            return processName;

        CloseHandle(hProcess);
    }
    return "";
}


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

    cout << hWnd << ": " << buffer << '(' << GetProcessNameFromHWND(hWnd) << ')' << ':' << rect.left << ':' << rect.top << ':' << rect.right << ':' << rect.bottom << std::endl;
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
    int sides = 0;
    const float snapDistance = 0.1;
    size_t maxX = snapDistance * (monitor.right - monitor.left);
    size_t maxY = snapDistance * (monitor.bottom - monitor.top);
    if(abs(window.bottom - monitor.bottom) < maxY) sides |= (1 << int(Corner::bottom));
    if(abs(window.top - monitor.top) < maxY) sides |= (1 << int(Corner::top));
    if(abs(window.left - monitor.left) < maxX) sides |= (1 << int(Corner::left));
    if(abs(window.right - monitor.right) < maxX) sides |= (1 << int(Corner::right));
    return sides;
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
    bool isRight = int(c) & int(Corner::right);
    mcorner.setX(isRight ? mrect.right : mrect.left);
    wcorner.setX(isRight ? wrect.right : wrect.left);
    bool isBottom = int(c) & int(Corner::bottom);
    mcorner.setY(isBottom ? mrect.bottom : mrect.top);
    wcorner.setY(isBottom ? wrect.bottom : wrect.top);
    return {wcorner.x() - mcorner.x(), wcorner.y() - mcorner.y()};
}

void resetAllWindowPositions(const map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> &windowsOrderInCorners,
                                    const map<HMONITOR, RECT> &monitorRects,
                                    map<HWND, RECT> &windowRects,
                                    size_t unitSize)
{
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        auto mrect = monitorRects.at(mon);
        for(auto &[corner, windows]: mcvw)
        {
            for(int i = 0; i < windows.size(); i++)
            {
                auto &wrect = windowRects.at(windows[i]);
                MoveWindow(windows[i], mrect.left, mrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
            }
        }
    }
}

void arrangeWindowsInMonitorCorners(const map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> &windowsOrderInCorners,
                    const map<HMONITOR, RECT> &monitorRects,
                    map<HWND, RECT> &windowRects)

{
    HTHEME theme = nullptr;
    int borderWidth = 0;
    int unitSize = 16;
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        auto mrect = monitorRects.at(mon);
        // 1° distribute windows in corners
        for(auto &[corner, windows]: mcvw)
        {
            for(int i = 0; i < windows.size(); i++)
            {
                if(!theme)
                {
                    theme = OpenThemeData(windows[i], L"WINDOW");
                    borderWidth = GetThemeSysSize(theme, SM_CXPADDEDBORDER) * 3 / 2;
                    unitSize = (GetThemeSysSize(theme, SM_CYSIZE) + GetThemeSysSize(theme, SM_CXPADDEDBORDER) * 2) * 4 / 3;
                }
                auto &wrect = windowRects.at(windows[i]);
                flags<Corner, int> otherCorner;
                // 1°
                otherCorner = corner ^ int(Corner::right);
                long dx0 = (mcvw.at(otherCorner).size()) * unitSize;
                // 2°
                otherCorner = corner ^ int(Corner::bottom);
                long dy = int(mcvw.at(otherCorner).size()) * unitSize;
                // 3°
                otherCorner = corner ^ int(Corner::bottomright);
                long dx = max(dx0, long(mcvw.at(otherCorner).size() * unitSize) - dy);
                if(wrect.right - wrect.left + allowIncreaseByUnits * unitSize > mrect.right - mrect.left)
                {
                    wrect.left = mrect.left - borderWidth;
                    wrect.right = mrect.right + borderWidth;
                }
                if(wrect.bottom - wrect.top + allowIncreaseByUnits * unitSize > mrect.bottom - mrect.top)
                {
                    wrect.top = mrect.top - borderWidth;
                    wrect.bottom = mrect.bottom + borderWidth;
                }
                RECT newRect;
                if(corner & Corner::right)
                {
                    newRect.right = mrect.right + borderWidth - i * unitSize;
                    newRect.left = max(wrect.left + newRect.right - wrect.right, dx - borderWidth);
                }
                else
                {
                    newRect.left = mrect.left - borderWidth + i * unitSize;
                    newRect.right = min(wrect.right + newRect.left - wrect.left, mrect.right - dx + borderWidth);
                }
                if(corner & Corner::bottom)
                {
                    newRect.bottom = mrect.bottom + borderWidth - (windows.size() - i  - 1) * unitSize;
                    newRect.top = max(wrect.top + newRect.bottom - wrect.bottom, dy - borderWidth);
                }
                else
                {
                    newRect.top = mrect.top - borderWidth + (windows.size() - i - 1) * unitSize;
                    newRect.bottom = min(wrect.bottom + newRect.top - wrect.top, mrect.bottom - dy + borderWidth);
                }
                wrect = newRect;
                cout << "Will move window " << windows[i] << '('<<i<<')'<<" to: " << newRect.left << ':' <<  newRect.top << ':' << newRect.right - mrect.right << ':' << newRect.bottom - mrect.bottom << endl;
                MoveWindow(windows[i], newRect.left, newRect.top, newRect.right - newRect.left, newRect.bottom - newRect.top, TRUE);
            }
        }
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
