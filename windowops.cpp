#include "windowops.h"
#include <iostream>
#include <psapi.h>
#include <uxtheme.h>
#include <shellscalingapi.h>

using namespace std;

constexpr int allowIncreaseByUnits = 2;
map<HMONITOR, string> monitorNames;

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
    string processName = GetProcessNameFromHWND(hWnd);
    if(processName == "ApplicationFrameHost.exe") // an invisible window
        windows.erase(hWnd);
    else if(rect.left == -32000 && rect.top == -32000) // minimized
        windows.erase(hWnd);
    else
        cout << hWnd << ": " << buffer << '(' << processName << ')' << ':' << rect.left << ':' << rect.top << ':'
             << rect.right << ':' << rect.bottom << std::endl;
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

int windowDistanceFromCorner(RECT wrect, RECT mrect, flags<Corner, int> c)
{
    QPoint mcorner, wcorner;
    bool isRight = c & Corner::right;
    mcorner.setX(isRight ? mrect.right : mrect.left);
    wcorner.setX(isRight ? wrect.right : wrect.left);
    bool isBottom = c & Corner::bottom;
    mcorner.setY(isBottom ? mrect.bottom : mrect.top);
    wcorner.setY(isBottom ? wrect.bottom : wrect.top);
    int distX = wcorner.x() - mcorner.x();
    int distY = wcorner.y() - mcorner.y();
    return sqrt(distX * distX + distY * distY);
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
    int borderWidth = 0;
    int unitSize = 16;
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        HTHEME theme = nullptr;
        auto mrect = monitorRects.at(mon);
        // 1° distribute windows in corners
        for(auto &[corner, windows]: mcvw)
        {
            for(int i = 0; i < windows.size(); i++)
            {
                if(!theme)
                {
                    DEVICE_SCALE_FACTOR sf;
                    GetScaleFactorForMonitor(mon, &sf);
                    theme = OpenThemeData(windows[i], L"WINDOW");
                    borderWidth = GetThemeSysSize(theme, SM_CXPADDEDBORDER) * sf / 100;
                    unitSize = (GetThemeSysSize(theme, SM_CYSIZE) + GetThemeSysSize(theme, SM_CXPADDEDBORDER) * 2) * sf / 100;
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
                    newRect.left = max(wrect.left + newRect.right - wrect.right, mrect.left + dx - borderWidth);
                }
                else
                {
                    newRect.left = mrect.left - borderWidth + i * unitSize;
                    newRect.right = min(wrect.right + newRect.left - wrect.left, mrect.right - dx + borderWidth);
                }
                if(corner & Corner::bottom)
                {
                    newRect.bottom = mrect.bottom + borderWidth - (windows.size() - i  - 1) * unitSize;
                    newRect.top = max(wrect.top + newRect.bottom - wrect.bottom, mrect.top + dy - borderWidth);
                }
                else
                {
                    newRect.top = mrect.top - borderWidth + (windows.size() - i - 1) * unitSize;
                    newRect.bottom = min(wrect.bottom + newRect.top - wrect.top, mrect.bottom - dy + borderWidth);
                }
                wrect = newRect;
                constexpr const char *cornerNames[] =
                    {"Corner::topleft", "Corner::topright", "Corner::bottomleft", "Corner::bottomright"};
                cout << "Will move window " << windows[i] << '(' << monitorNames[mon] << '@';
                cout << cornerNames[int(corner)] << ':' << i << ')' <<" to relative: " << newRect.left << ':';
                cout << newRect.top << ':' << newRect.right - mrect.right << ':' << newRect.bottom - mrect.bottom << endl;
                MoveWindow(windows[i], newRect.left, newRect.top, newRect.right - newRect.left, newRect.bottom - newRect.top, TRUE);
            }
        }
    }
}

int diameter(const RECT& rect)
{
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    return sqrt(width * width + height * height);
}

pair<HMONITOR, Corner> findMainMonitorAndCorner(HWND w, RECT &wrect, const map<HMONITOR, RECT> &monitorRects)
{
    size_t maxArea = 0;
    pair<HMONITOR, Corner> result = {};
    for(auto &[m,r]: monitorRects)
    {
        RECT rect;
        IntersectRect(&rect, &r, &wrect);
        size_t area = calculateRectArea(rect);
        if (area > maxArea)
        {
            result.first = m;
            maxArea = area;
        }
    }
    if(result.first)
    {
        RECT mrect = monitorRects.at(result.first);
        int minDist = diameter(mrect);
        Corner selectedCorner;
        for(int i = 0; i < 4; i++)
        {
            int dist = windowDistanceFromCorner(wrect, mrect, Corner(i));
            if(dist < minDist)
            {
                minDist = dist;
                selectedCorner = Corner(i);
            }
        }
    }
    return result;
}

map<HWND, RECT> existingWindowRects;
void processAllWindows()
{
    cout << "Enumerating Monitors...\n";
    map<HMONITOR, RECT> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&monitorRects));
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        r = info.rcWork;
        monitorNames[m] = info.szDevice;
        cout << info.szDevice << ": " << r.left << ':' << r.top << ':' << r.right << ':' << r.bottom << std::endl;
    }

    cout << "Enumerating Windows...\n";
    map<HWND, RECT> windowRects;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));
    // int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    // int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    bool changed = false;
    for(auto&[w,r]: existingWindowRects)
    {
        if(!windowRects.count(w))
        {
            changed = true;
            break;
        }
    }
    for(auto&[w,r]: windowRects)
    {
        if(!existingWindowRects.count(w))
        {
            changed = true;
            break;
        }
    }

    existingWindowRects = windowRects;

    if(!changed) return;

    // find main monitor for each window
    map<HWND, pair<HMONITOR, Corner>> windowMonitor;
    for (auto &[w, r] : windowRects)
    {
        auto mc = findMainMonitorAndCorner(w, r, monitorRects);
        // if(!monitor) monitor = monitorRects.rbegin()->first; // that would steal space for invisible windows, consider filtering them better because some fall into this category incorrectly
        if(mc.first) windowMonitor[w] = mc;
        // else
        //     cout << "No monitor for window " << w << endl;
    }

    // sort windows according to size and distribute them in corners
    map<HMONITOR, multimap<size_t, pair<HWND, Corner>>> windowsOnMonitor;
    for(auto &[w, mc]: windowMonitor) windowsOnMonitor[mc.first].insert({calculateRectArea(windowRects[w]), {w, mc.second}});
    map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> windowsOrderInCorners;
    for(auto &[m, mwc]: windowsOnMonitor)
    {
        for(int i = 0; i < 4; i++) windowsOrderInCorners[m][Corner(i)]; // ensure all window sets exist
        int i = 3;
        for(auto&[s, wc]: mwc)
        {
            // SHOWDEBUG(w);
            windowsOrderInCorners[m][Corner(i%4)].push_back(wc.first);
            i++;
        }
    }

    arrangeWindowsInMonitorCorners(windowsOrderInCorners, monitorRects, windowRects);
}
