#include "windowops.h"
#include <map>
#include <vector>
#include <set>
#include <memory>
#include <psapi.h>
#include <uxtheme.h>
#include <shellscalingapi.h>
#include <array>

using namespace std;

int windowops_maxIncrease = 0;

/// <summary>
/// enum class wrapper
/// </summary>
/// <typeparam name="E">enum class</typeparam>
/// <typeparam name="I">underlying int</typeparam>
template<typename E, typename I>struct flags {
    I v;
    flags() = default;
    flags(E x) : v(I(x)) {}
    auto operator=(E x) { v = I(x); return *this; }
    auto operator=(I x) { v = x; return *this; }
    operator I() const { return v; }
    I operator&(E x) const { return v & I(x); }
    bool operator==(E x) const { return v == I(x); }
};

enum class Corner : int { top = 0, left = 0, topleft = top | left, right = 1, topright = top | right,
                          bottom = 2, bottomleft = bottom | left, bottomright = bottom | right };

struct Rect : public RECT
{
    constexpr size_t width() const { return right - left; }
    constexpr size_t height() const { return bottom - top; }
    constexpr size_t area() const { return (bottom - top) * (right - left); }

    size_t diameter() const
    {
        auto width = right - left;
        auto height = bottom - top;
        return sqrt(width * width + height * height);
    }

    constexpr bool isDifferentSize(const RECT& r2) const
    {
        auto w1 = right - left;
        auto w2 = r2.right - r2.left;
        auto h1 = bottom - top;
        auto h2 = r2.bottom - r2.top;
        bool result = (w1 != w2) || (h1 != h2);
        return result;
    }

    void moveInside(const Rect& monRect)
    {
        int maxw = monRect.right - monRect.left;
        int maxh = monRect.bottom - monRect.top;
        right -= max(0L, right - left - maxw);
        bottom -= max(0L, bottom - top - maxh);
        left -= max(0L, right - monRect.right);
        top -= max(0L, bottom - monRect.bottom);
    }

    int distanceFromCorner(const Rect &mrect, flags<Corner, int> c) const
    {
        POINT mcorner = {}, wcorner = {};
        bool isRight = c & Corner::right;
        mcorner.x = isRight ? mrect.right : mrect.left;
        wcorner.x = isRight ? right : left;
        bool isBottom = c & Corner::bottom;
        mcorner.y = isBottom ? mrect.bottom : mrect.top;
        wcorner.y = isBottom ? bottom : top;
        int distX = wcorner.x - mcorner.x;
        int distY = wcorner.y - mcorner.y;
        return int(sqrt(distX * distX + distY * distY));
    }



    constexpr Rect(const RECT& other) : RECT(other) {}
    Rect() = default;
};

map<HMONITOR, string> monitorNames;
map<HWND, pair<string, string>> windowTitles;
map<HWND, tuple<HMONITOR, Corner, Rect>> oldWindowMonitor; /// previous windows placement for tracking changes
vector<HWND> bulkMinimizedWindows;
set<HWND> unmovableWindows;
double sf0 = 0;
// WINDOWS UTILITY FUNCTIONS

string GetProcessNameFromHWND(HWND hwnd)
{
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess)
    {
        char processName[MAX_PATH] = "<unknown>";
        if (GetModuleBaseNameA(hProcess, nullptr, processName, sizeof(processName)))
            return processName;

        CloseHandle(hProcess);
    }
    return "";
}


// https://stackoverflow.com/questions/7277366/why-does-enumwindows-return-more-windows-than-i-expected
BOOL IsAltTabWindow(HWND hwnd)
{
    if (!hwnd) return FALSE;
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

// VISITOR PROCEDURES AND OTHER PROGRAM LOGIC

BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam)
{
    auto &windows = *reinterpret_cast<map<HWND, Rect>*>(lParam);
    if(!IsAltTabWindow(hWnd)) return TRUE;

    int length = GetWindowTextLength(hWnd);
    if(!length) return TRUE;
    auto buffer = make_unique<char[]>(length + 1);
    GetWindowTextA(hWnd, buffer.get(), length + 1);

    Rect &rect = windows[hWnd];
    GetWindowRect(hWnd, &rect);
    string processName = GetProcessNameFromHWND(hWnd);
    if (processName == "ApplicationFrameHost.exe" || IsIconic(hWnd))
    {
        windows.erase(hWnd);
        windowTitles.erase(hWnd);
    }
    else windowTitles[hWnd] = {processName, buffer.get()};
    return TRUE;
}

BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam)
{
    auto &monitorRects = *reinterpret_cast<map<HMONITOR, RECT>*>(lParam);
    monitorRects[monitor] = *pRect;
    return TRUE;
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
            for(int i = 0; i < windows.size(); i++)
            {
                auto &wrect = windowRects.at(windows[i]);
                MoveWindow(windows[i], mrect.left, mrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
            }
    }
}

void arrangeWindowsInMonitorCorners(const map<HMONITOR, map<flags<Corner, int>, vector<HWND>>>& windowsOrderInCorners,
    const map<HMONITOR, Rect>& monitorRects,
    map<HWND, Rect>& windowRects)
{
    UINT dpiX, dpiY;
    if (sf0 == 0)
    {
        HMONITOR primaryMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        GetDpiForMonitor(primaryMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        sf0 = 100 * dpiY / 96.0; // scaling factor of primary monitor for theme size correction
        cerr << "sf0=" << sf0 << '%' << endl;
    }
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        int unitSize = 16;
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        double sf = 100 * dpiY / 96.0;
        HTHEME theme = nullptr;
        int borderWidth = 0;
        int borderHeight = 0;
        auto mrect = monitorRects.at(mon);
        // 1° distribute windows in corners
        for(auto &[corner, windows]: mcvw)
        {
            for(int i = 0; i < windows.size(); i++)
            {
                if(!theme)
                {
                    theme = OpenThemeData(windows[i], L"WINDOW");
                    unitSize = (GetThemeSysSize(theme, SM_CYSIZE) + GetThemeSysSize(theme, SM_CXPADDEDBORDER) * 2) * sf / sf0;
                    //cerr << monitorNames[mon] << ':' << sf << "%, " << unitSize << "px" << endl;
                    borderHeight = GetThemeSysSize(theme, SM_CXPADDEDBORDER);
                    borderWidth = borderHeight * sf / 100;
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
                long dx = max(dx0, long(mcvw.at(otherCorner).size() * unitSize));
                //long maxIncreaseX = windowsWithSizeChanged.count(windows[i]) ? 0 : windowops_maxIncrease;
                long maxIncreaseX = windowops_maxIncrease;
                long maxIncreaseY = maxIncreaseX;
                if(oldWindowMonitor.count(windows[i])) 
                {
                    RECT oldWrect = get<2>(oldWindowMonitor[windows[i]]);
                    maxIncreaseX = max(maxIncreaseX, oldWrect.right - oldWrect.left - (wrect.right - wrect.left));
                    maxIncreaseY = max(maxIncreaseY, oldWrect.bottom - oldWrect.top - (wrect.bottom - wrect.top));
                }
                if(wrect.right - wrect.left + maxIncreaseX > mrect.right - mrect.left)
                {
                    wrect.left = mrect.left - borderWidth;
                    wrect.right = mrect.right + borderWidth;
                }
                if(wrect.bottom - wrect.top + maxIncreaseY > mrect.bottom - mrect.top)
                {
                    wrect.top = mrect.top - borderHeight;
                    wrect.bottom = mrect.bottom + borderHeight;
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
                    newRect.top = mrect.top - borderHeight + (windows.size() - i - 1) * unitSize;
                    newRect.bottom = min(wrect.bottom + newRect.top - wrect.top, mrect.bottom - dy + borderHeight);
                }
                wrect = newRect;
                constexpr const char *cornerNames[] =
                    {"Corner::topleft", "Corner::topright", "Corner::bottomleft", "Corner::bottomright"};
                cout << "Moving window " << windows[i] << '(' << monitorNames[mon] << '@';
                cout << cornerNames[int(corner)] << ':' << i << ')' <<" to relative: " << wrect.left - mrect.left << ':';
                cout << wrect.top - mrect.top << ':' << wrect.right - mrect.right << ':' << wrect.bottom - mrect.bottom << '[';
                auto result = MoveWindow(windows[i], wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
                if (!result)
                {
                    unmovableWindows.insert(windows[i]);
                    oldWindowMonitor.erase(windows[i]);
                }

                cout << result << ']' << endl;
            }
        }
        CloseThemeData(theme);
    }
}

pair<HMONITOR, Corner> findMainMonitorAndCorner(HWND w, Rect &wrect, const map<HMONITOR, Rect> &monitorRects)
{
    size_t maxArea = 0;
    pair<HMONITOR, Corner> result = {};
    for(auto &[m,r]: monitorRects)
    {
        Rect rect;
        IntersectRect(&rect, &r, &wrect);
        size_t area = rect.area();
        if (area > maxArea)
        {
            result.first = m;
            maxArea = area;
        }
    }
    if(result.first)
    {
        Rect mrect = monitorRects.at(result.first);
        int minDist = mrect.diameter();
        Corner selectedCorner;
        for(int i = 0; i < 4; i++)
        {
            int dist = wrect.distanceFromCorner(mrect, Corner(i));
            if(dist < minDist)
            {
                minDist = dist;
                selectedCorner = Corner(i);
            }
        }
    }
    return result;
}

// API FUNCTIONS

void processAllWindows(bool force)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    map<HMONITOR, Rect> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&monitorRects));
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        r = info.rcWork;
        monitorNames[m] = info.szDevice;
    }

    map<HWND, Rect> windowRects;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));
    // int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    // int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // unmaximize windows to get rid of related issues
    for (auto& [w, r] : windowRects)
        if (IsZoomed(w))
        {
            ShowWindow(w, SW_RESTORE);
            MoveWindow(w, r.left, r.top, r.width(), r.height(), true);
        }


    // find main monitor for each window
    map<HWND, tuple<HMONITOR, Corner, Rect>> windowMonitor;
    for (auto &[w, r] : windowRects)
    {
        auto mc = findMainMonitorAndCorner(w, r, monitorRects);
        // if(!monitor) monitor = monitorRects.rbegin()->first; // that would steal space for invisible windows, consider filtering them better because some fall into this category incorrectly
        if(mc.first) windowMonitor[w] = {mc.first, mc.second, r};
        // else
        //     cout << "No monitor for window " << w << endl;
    }

    bool changed = false;

    for(auto&[w,r]: oldWindowMonitor)
    {
        if(!windowMonitor.count(w) || get<0>(windowMonitor.at(w)) != get<0>(r))
        {
            changed = true;
            break;
        }
    }

    set<HWND>deletedUnmovableWindows;
    for (auto& w : unmovableWindows) if(!windowMonitor.count(w)) deletedUnmovableWindows.insert(w);
    for (auto& w : deletedUnmovableWindows) unmovableWindows.erase(w);
    deletedUnmovableWindows.clear();

    set<HWND> windowsWithSizeChanged;
    if(!changed)
        for(auto&[w,r]: windowMonitor)
        {
            bool existed = oldWindowMonitor.count(w);
            bool sameMonitor = existed && get<0>(oldWindowMonitor.at(w)) == get<0>(r);
            if(!existed || !sameMonitor)
            {
                changed = true;
                windowsWithSizeChanged.clear();
                break;
            }
            else if (existed && sameMonitor && get<2>(oldWindowMonitor[w]).isDifferentSize(get<2>(windowMonitor[w])))
            {
                changed = true;
                windowsWithSizeChanged.insert(w);
            }
        }

    if(!force && !changed) return;

    cout << "Enumerating Monitors...\n";
    for (auto& [m, s] : monitorNames)
    {
        auto& rect = monitorRects[m];
        cout << monitorNames[m] << ": " << rect.left << ':' << rect.top << ':' << rect.right << ':' << rect.bottom << std::endl;
    }

    cout << "Enumerating Windows...\n";
    for (auto& [w, ss] : windowTitles)
    {
        auto& rect = windowRects[w];
        cout << w << ": " << ss.second << '(' << ss.first << ')' << ':' << rect.left << ':' << rect.top << ':'
             << rect.right << ':' << rect.bottom << std::endl;
    }

    // preserve maximum sizes of windows
    // // TODO reimplement as maximumWindowSizes
    //map<HWND, RECT> oldWindowRects;
    //for(auto &[w, t]: oldWindowMonitor) oldWindowRects[w] = get<2>(t);

    oldWindowMonitor = windowMonitor;
    //for(auto &[w, t]: oldWindowMonitor)
    //    if(oldWindowRects.count(w))
    //    {
    //        const RECT& oldR = oldWindowRects[w];
    //        RECT& newR = get<2>(oldWindowMonitor[w]);
    //        if(oldR.right - oldR.left >= newR.right - newR.left && oldR.bottom - oldR.top >= newR.bottom - newR.top)
    //            newR = oldR;
    //    }

    // sort windows according to size and distribute them in corners
    map<HMONITOR, multimap<size_t, pair<HWND, Corner>>> windowsOnMonitor;
    for(auto &[w, mc]: windowMonitor)
        windowsOnMonitor[get<0>(mc)].insert({windowRects[w].height(), {w, get<1>(mc)}});
    map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> windowsOrderInCorners;
    for(auto &[m, mwc]: windowsOnMonitor)
    {
        for(int i = 0; i < 4; i++) windowsOrderInCorners[m][Corner(i)]; // ensure all window sets exist
        auto mr = monitorRects[m];
        int i = 0;
        std::array<Corner, 4> corners { Corner::topleft, Corner::bottomleft, Corner::topright, Corner::bottomright };
        bool smallWindowsEnded = false;
        for (auto& [s, wc] : mwc)
        {
            if (!smallWindowsEnded && s >= 0.9 * mr.height())
            {
                smallWindowsEnded = true;
                i = 0;
                corners = { Corner::topleft, Corner::topright, Corner::bottomleft, Corner::bottomright };
            }
            if (!unmovableWindows.count(wc.first)) windowsOrderInCorners[m][corners[i++ % 4]].push_back(wc.first);
        }
    }

    arrangeWindowsInMonitorCorners(windowsOrderInCorners, monitorRects, windowRects);
    // save window sizes after adjustment for size change detection to remain stable
    for (auto& [w, mcr] : oldWindowMonitor) if (windowRects.count(w)) get<2>(mcr) = windowRects[w];
}

bool toggleMinimizeAllWindows()
{
    if (bulkMinimizedWindows.size())
    {
        for (auto& w : bulkMinimizedWindows)
            ShowWindow(w, SW_RESTORE);
        bulkMinimizedWindows.clear();
        return false;
    }
    else
    {
        for (auto& [w, mcr] : oldWindowMonitor)
            if (!IsIconic(w))
            {
                ShowWindow(w, SW_MINIMIZE);
                bulkMinimizedWindows.push_back(w);
            }
        return true;
    }
}

// REGISTRY FUNCTIONS

bool deleteRegistryValue(const std::basic_string<TCHAR>& key, const std::basic_string<TCHAR>& name)
{
    HKEY hKey;
    bool result = false;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (RegDeleteValue(hKey, name.data()) == ERROR_SUCCESS)
        {
            std::wcout << L"Value deleted successfully" << std::endl;
            result = true;
        }
        else
        {
            std::cerr << "Failed to delete value" << std::endl;
        }
        RegCloseKey(hKey);
    }
    else
    {
        std::cerr << "Failed to open key" << std::endl;
    }

    return result;
}

bool deleteRegistrySubkey(const std::basic_string<TCHAR>& key, const std::basic_string<TCHAR>& name)
{
    HKEY hKey;
    bool result = false;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        if (RegDeleteKey(hKey, name.data()) == ERROR_SUCCESS)
        {
            std::wcout << L"Registry key deleted successfully" << std::endl;
            result = true;
        }
        else
        {
            std::cerr << "Failed to delete registry key" << std::endl;
        }
        RegCloseKey(hKey);
    }
    else
    {
        std::cerr << "Failed to open key" << std::endl;
    }
    return result;
}

bool CreateConsole()
{
    bool result = AllocConsole();
    if (result) 
    {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        std::cout << "Console created successfully." << std::endl;
    }
    return result;
}

template<> std::optional<std::wstring>
readRegistryValue<std::wstring, REG_SZ>(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name)
{
    HKEY hKey;
    std::optional<std::wstring> result;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dataType;
        DWORD dataSize;
        if (RegQueryValueEx(hKey, name.data(), NULL, &dataType, NULL, &dataSize) == ERROR_SUCCESS && dataType == REG_SZ)
        {
            BYTE* data = new BYTE[dataSize];
            if (RegQueryValueEx(hKey, name.data(), NULL, &dataType, data, &dataSize) == ERROR_SUCCESS)
                result = reinterpret_cast<wchar_t*>(data);
            delete[] data;
        }
        RegCloseKey(hKey);
    }
    return result;
}

template<>bool
writeRegistryValue<std::wstring, REG_SZ>(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name, const std::wstring& v)
{
    HKEY hKey;
    bool result = false;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(hKey, name.data(), 0, REG_SZ, (const BYTE*)v.data(), sizeof(wchar_t) * v.size()) == ERROR_SUCCESS)
        {
            result = true;
        }
        else
        {
            std::cerr << "Failed to set value" << std::endl;
        }
        RegCloseKey(hKey);
    }
    else
    {
        std::cerr << "Failed to create/open key" << std::endl;
    }

    return result;
}

