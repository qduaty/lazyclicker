#include "windowops.h"
#include <map>
#include <vector>
#include <set>
#include <Psapi.h>
#include <Uxtheme.h>
#include <ShellScalingApi.h>
#include <array>
#include <list>
#include <queue>
#include <cassert>

using namespace std;

int windowops_maxIncrease = 0;
bool avoidTopRightCorner = false;
bool increaseUnitSizeForTouch = true;

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
    explicit operator E() const { return E(v); }
    static friend I operator&(flags f, E x) { return f.v & I(x); }
    static friend I operator^(flags f, E x) { return f.v ^ I(x); }
    static friend bool operator==(flags f, E x) { return f.v == I(x); }
    flags& operator|=(E x) { v |= I(x); return *this; }
};

enum class Corner : int { top = 0, left = 0, topleft = top | left, right = 1, topright = top | right,
                          bottom = 2, bottomleft = bottom | left, bottomright = bottom | right };

struct Rect : public RECT
{
    constexpr long width() const { return right - left; }
    constexpr long height() const { return bottom - top; }
    constexpr size_t area() const { return (bottom - top) * (right - left); }

    size_t diameter() const
    {
        auto width = right - left;
        auto height = bottom - top;
        return size_t(sqrt(width * width + height * height));
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
        POINT mcorner = {};
        POINT wcorner = {};
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

    static friend bool operator!=(const RECT& self, const RECT& other)
    {
        return self.left != other.left || self.right != other.right || self.top != other.top || self.bottom != other.bottom;
    }

    constexpr Rect(const RECT& other) : RECT(other) {}
    Rect() = default;
};

map<HMONITOR, string> monitorNames;
map<HWND, pair<string, string>> windowTitles;
map<HWND, tuple<HMONITOR, Corner, Rect>> oldWindowMonitor; /// previous windows placement for tracking changes
set<HWND> unmovableWindows;

// WINDOWS UTILITY FUNCTIONS

static string GetProcessNameFromHWND(HWND hwnd)
{
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId); hProcess)
    {
        if (array<char, MAX_PATH> processName{ "<unknown>" }; GetModuleBaseNameA(hProcess, nullptr, processName.data(), sizeof(processName)))
            return processName.data();

        CloseHandle(hProcess);
    }
    return "";
}

// https://stackoverflow.com/questions/7277366/why-does-enumwindows-return-more-windows-than-i-expected
static BOOL IsAltTabWindow(HWND hwnd)
{
    if (!hwnd) return FALSE;
    if(!IsWindowVisible(hwnd)) return FALSE;

    HWND hwndWalk = nullptr;
    HWND hwndTry = GetAncestor(hwnd, GA_ROOTOWNER);
    while(hwndTry != hwndWalk)
    {
        hwndWalk = hwndTry;
        hwndTry = GetLastActivePopup(hwndWalk);
        if(IsWindowVisible(hwndTry)) break;
    }
    if(hwndWalk != hwnd) return FALSE;

    // the following removes some task tray programs and "Program Manager"
    TITLEBARINFO ti {sizeof(TITLEBARINFO)};
    GetTitleBarInfo(hwnd, &ti);
    if(ti.rgstate[0] & STATE_SYSTEM_INVISIBLE) return FALSE;

    // Tool windows should not be displayed either, these do not appear in the
    // task bar.
    if(GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return FALSE;

    return TRUE;
}

// VISITOR PROCEDURES AND OTHER PROGRAM LOGIC

static BOOL CALLBACK enumWindowsProc(HWND hWnd, map<HWND, Rect>* pWindows)
{
    auto &windows = *pWindows;
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

static BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC__ const */*dc*/, RECT const *pRect, map<HMONITOR, RECT>* monitorRects)
{
    (*monitorRects)[monitor] = *pRect;
    return TRUE;
}

static void resetAllWindowPositions(const map<HMONITOR, map<flags<Corner, int>, vector<HWND>>> &windowsOrderInCorners,
                                    const map<HMONITOR, RECT> &monitorRects,
                                    map<HWND, RECT> &windowRects,
                                    size_t /*unitSize*/)
{
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        auto &mrect = monitorRects.at(mon);
        for(auto &[corner, windows]: mcvw)
            for(auto& w : windows)
            {
                auto const &wrect = windowRects.at(w);
                MoveWindow(w, mrect.left, mrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
            }
    }
}

static bool loadThemeData(const HWND& w, int& unitSize, double sf0, double sf, int& borderWidth, int& borderHeight)
{
    if (HTHEME theme = OpenThemeData(w, L"WINDOW"))
    {
        unitSize = int((GetThemeSysSize(theme, SM_CYSIZE) + GetThemeSysSize(theme, SM_CXPADDEDBORDER) * 2) * sf / sf0);
        borderWidth = int(GetThemeSysSize(theme, SM_CXPADDEDBORDER) * sf / 100);
        borderHeight = int(borderWidth * 100 / sf);
        CloseThemeData(theme);
        return true;
    }
    return false;
}

static void displayMovedWindowDetails(const HWND& w, const HMONITOR& mon, const flags<Corner, int>& corner, 
                                      tuple<int, int, long, long> params, const pair<Rect, Rect>& rects)
{
    auto& [wrect, mrect] = rects;
    auto &[i, unitSize, dx1, dy] = params;
    array<const char*, 4> cornerNames{ "Corner::topleft", "Corner::topright", "Corner::bottomleft", "Corner::bottomright" };
    cout << "Moved window " << w << " [" << windowTitles[w].first << "] " << '(' << monitorNames[mon] << '@';
    cout << cornerNames[int(corner)] << ':' << i << ')';
    cout << "; dx=" << dx1 / unitSize << ", dy=" << dy / unitSize;
    cout << "; relative: " << wrect.left - mrect.left << ':';
    cout << wrect.top - mrect.top << ':' << wrect.right - mrect.right << ':' << wrect.bottom - mrect.bottom << endl;
}

static bool isMonitorTouchCapable(HMONITOR__ const* mon)
{
    UINT32 deviceCount = 0;
    GetPointerDevices(&deviceCount, nullptr);
    if (!deviceCount) return false;
    auto pointerDevices = make_unique<POINTER_DEVICE_INFO[]>(deviceCount);
    if (!GetPointerDevices(&deviceCount, pointerDevices.get())) return false;
    for (int i = 0; i < deviceCount; i++) if (pointerDevices[i].monitor == mon) return true;
    return false;
}

static void adjustWindowsInCorner(std::map<HWND, Rect>& windowRects,
                                  const HMONITOR& mon,
                                  const Rect& mrect,
                                  const flags<Corner, int>& corner,
                                  const std::map<flags<Corner, int>, std::multimap<size_t, HWND>>& mcvw, 
                                  tuple<int /*unitSize*/, SIZE /*borderSize*/, bool /*multiMonitor*/> settings)
{
    auto [unitSize, borderSize, multiMonitor] = settings;
    const auto& windows = mcvw.at(corner);
    bool verticalScreen = mrect.height() > mrect.width();
    int i = verticalScreen ? int(windows.size() - 1) : 0;
    for (auto& [s, w] : windows)
    {
        if (auto dpiAwareness = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(w));
            multiMonitor && dpiAwareness != DPI_AWARENESS_PER_MONITOR_AWARE)
            borderSize = { 0, 0 }; // prevent dpi unaware windows from being resized in context of a different screen

        flags<Corner, int> otherCorner;
        // 1°
        otherCorner = corner ^ Corner::bottom;
        long dy = max(0, long(mcvw.at(otherCorner).size()) - i) * unitSize - borderSize.cy;
        // 2°
        otherCorner = corner ^ Corner::right;
        long dx = long(mcvw.at(otherCorner).size()) * unitSize;
        otherCorner = corner ^ Corner::bottomright;
        dx = max(dx, long(mcvw.at(otherCorner).size()) * unitSize) - borderSize.cx;
        auto& wrect = windowRects.at(w);
        if (wrect.width() + windowops_maxIncrease > mrect.width())
        {
            wrect.left = mrect.left - borderSize.cx;
            wrect.right = mrect.right + borderSize.cx;
        }
        if (wrect.height() + windowops_maxIncrease > mrect.height())
        {
            wrect.top = mrect.top - borderSize.cy;
            wrect.bottom = mrect.bottom + borderSize.cy;
        }
        RECT newRect = wrect;
        if (corner & Corner::right)
        {
            newRect.right = mrect.right + borderSize.cx - i * unitSize;
            newRect.left = max(newRect.right - wrect.width(), mrect.left + dx);
        }
        else
        {
            newRect.left = mrect.left - borderSize.cx + i * unitSize;
            newRect.right = min(newRect.left + wrect.width(), mrect.right - dx);
        }
        if (corner & Corner::bottom)
        {
            newRect.bottom = mrect.bottom + borderSize.cy - (long(windows.size()) - i - 1) * unitSize;
            newRect.top = max(newRect.bottom - wrect.height(), mrect.top + dy);
        }
        else
        {
            newRect.top = mrect.top - borderSize.cy + (long(windows.size()) - i - 1) * unitSize;
            newRect.bottom = min(newRect.top + wrect.height(), mrect.bottom - dy);
        }
        wrect = newRect;
        if (MoveWindow(w, wrect.left, wrect.top, wrect.width(), wrect.height(), TRUE))
            displayMovedWindowDetails(w, mon, corner, { i, unitSize, dx, dy }, { wrect, mrect });
        else
        {
            unmovableWindows.insert(w);
            oldWindowMonitor.erase(w);
        }

        if (verticalScreen) i--;
        else i++;
    }
}

static void adjustWindowsInMonitorCorners(const map<HMONITOR, map<flags<Corner, int>, multimap<size_t, HWND>>>& windowsOrderInCorners,
                                          const map<HMONITOR, Rect>& monitorRects,
                                          map<HWND, Rect>& windowRects)
{
    UINT dpiX;
    UINT dpiY;
    static double sf0 = 0;
    if (sf0 == 0)
    {
        HMONITOR primaryMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        GetDpiForMonitor(primaryMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        sf0 = 100 * dpiY / 96.0; // scaling factor of primary monitor for theme size correction
        cerr << "sf0=" << sf0 << '%' << endl;
    }
    for(auto &[mon, mcvw]: windowsOrderInCorners)
    {
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        double sf = 100 * dpiY / 96.0;
        int unitSize = 16;
        int borderWidth = 0;
        int borderHeight = 0;
        auto &mrect = monitorRects.at(mon);
        for (auto &[_, windows] : mcvw)
            if(windows.size() && loadThemeData(get<HWND>(*windows.begin()), unitSize, sf0, sf, borderWidth, borderHeight))
                break;
        bool multiMonitor = monitorRects.size() > 1;
        if (increaseUnitSizeForTouch && isMonitorTouchCapable(mon)) unitSize = unitSize * 3 / 2;
        for(int i = 0; i < 4; i++)
            adjustWindowsInCorner(windowRects, mon, mrect, Corner(i), mcvw, { unitSize, { borderWidth, borderHeight }, multiMonitor });
    }
}

static bool shouldAvoidTopRightCorner(HMONITOR__ const* mon)
{
    if (avoidTopRightCorner) return true;
    if(increaseUnitSizeForTouch && isMonitorTouchCapable(mon)) return true;
    return false;
}

static pair<HMONITOR, Corner> findMainMonitorAndCorner(Rect const &wrect, const map<HMONITOR, Rect> &monitorRects)
{
    size_t maxArea = 0;
    HMONITOR mon = nullptr;
    Corner corner = Corner::topleft;
    for(auto &[m, r]: monitorRects)
    {
        Rect rect {};
        IntersectRect(&rect, &r, &wrect);
        size_t area = rect.area();
        if (area > maxArea)
        {
            mon = m;
            maxArea = area;
        }
    }
    if(mon)
    {
        Rect mrect = monitorRects.at(mon);
        auto minDist = mrect.diameter();
        for(int i = 0; i < 4; i++)
        {
            auto c = Corner(i);
            if (shouldAvoidTopRightCorner(mon) && c == Corner::topright) continue;
            int dist = wrect.distanceFromCorner(mrect, c);
            if(dist < minDist)
            {
                minDist = dist;
                corner = c;
            }
        }
    }
    return { mon, corner };
}

// API FUNCTIONS

static void distributeNewWindowsInCorners(std::multimap<long, std::pair<HWND, Corner>>& mwc, const std::set<HWND>& newWindows, 
    HMONITOR mon, const Rect& mrect, std::queue<Corner>& freeCorners, std::map<flags<Corner, int>, 
    std::multimap<size_t, HWND>>& order)
{
    using enum Corner;
    int i = 0;
    array<Corner, 4> corners{ topleft, bottomleft, bottomright, topright};
    bool smallWindowsEnded = false;
    for (auto& [s, wc] : mwc)
    {
        auto& [w, c] = wc;
        if (!newWindows.contains(w) || unmovableWindows.contains(w)) continue;

        if (!smallWindowsEnded && s >= mrect.height() * 9 / 10)
        {
            smallWindowsEnded = true;
            i = 0;
            corners = { bottomright, bottomleft, topleft, topright };
        }
        if (freeCorners.size())
        {
            c = freeCorners.front();
            freeCorners.pop();
        }
        else
        {
            c = corners[i % 4];
            i++;
            if (shouldAvoidTopRightCorner(mon) && c == Corner::topright)
            {
                c = corners[i % 4];
                i++;
            }
        }
        order[c].insert({ s, w });
    }
}

static auto distributeWindowsInCorners(const map<HWND, Rect>& windowRects,
                                       const map<HWND, tuple<HMONITOR, Corner, Rect>>& windowMonitor,
                                       const set<HWND>& newWindows,
                                       const map<HMONITOR, Rect>& monitorRects)
{
    map<HMONITOR, map<flags<Corner, int>, multimap<size_t, HWND>>> windowsOrderInCorners;
    map<HMONITOR, multimap<long, pair<HWND, Corner>>> windowsOnMonitor;
    for (auto& [w, mc] : windowMonitor) windowsOnMonitor[get<HMONITOR>(mc)].insert({ windowRects.at(w).height(), {w, get<Corner>(mc)} });

    for (auto& [m, mwc] : windowsOnMonitor)
    {
        for (int i = 0; i < 4; i++) windowsOrderInCorners[m][Corner(i)]; // ensure all window sets exist
        for (auto& [s, wc] : mwc)
        {
            auto& [w, c] = wc;
            if (newWindows.contains(w) || unmovableWindows.contains(w)) continue;
            windowsOrderInCorners[m][c].insert({ s, w });
        }

        queue<Corner> freeCorners;
        size_t maxNumWindows = 0;
        for (auto const& [c, vw] : windowsOrderInCorners[m]) maxNumWindows = max(maxNumWindows, vw.size());
#if 0
        for (auto const& [c, vw] : windowsOrderInCorners[m]) 
            for (int i = 0; i < maxNumWindows - vw.size(); i++)
            {
                auto corner = Corner(int(c));
                if (corner == Corner::topright && shouldAvoidTopRightCorner(m)) continue;
                freeCorners.push(corner);
            }        
#else
        using enum Corner;
        array<Corner, 4> corners{ topleft, bottomleft, topright, bottomright};
        for(auto corner: corners)
        {
            if (corner == topright && shouldAvoidTopRightCorner(m)) continue;
            auto const& vw = windowsOrderInCorners[m][corner];
            for (int i = 0; i < maxNumWindows - vw.size(); i++)
            {
                freeCorners.push(corner);
            }
        }
#endif           

        distributeNewWindowsInCorners(mwc, newWindows, m, monitorRects.at(m), freeCorners, windowsOrderInCorners[m]);
    }
    return windowsOrderInCorners;
}

static void displayMonitorsAndWindows(std::map<HMONITOR, Rect>& monitorRects, std::map<HWND, Rect>& windowRects)
{
    cout << "Monitors:\n";
    for (auto const& [m, s] : monitorNames)
    {
        auto const& rect = monitorRects[m];
        cout << monitorNames[m] << ": " << rect.left << ':' << rect.top << ':' << rect.right << ':' << rect.bottom;
        cout << '(' << rect.right - rect.left << 'x' << rect.bottom - rect.top << ')' << endl;
    }

    cout << "Windows:\n";
    for (auto const& [w, ss] : windowTitles)
    {
        auto const& rect = windowRects[w];
        cout << w << ": " << ss.second << '(' << ss.first << ')' << ':' << rect.left << ':' << rect.top << ':'
             << rect.right << ':' << rect.bottom << "dpiAwareness=" 
             << GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(w)) << ", style=" 
             << hex << GetWindowLong(w, GWL_EXSTYLE) << dec << endl;
    }
}

static bool detectChangedWindows(std::map<HWND, std::tuple<HMONITOR, Corner, Rect>>& windowMonitor, 
                                 std::set<HWND>& newWindows, 
                                 const std::map<HMONITOR, Rect>& monitorRects)
{
    bool changed = false;
    for (auto& [w, r] : oldWindowMonitor)
        if (!windowMonitor.contains(w))
            changed = true;
        else if (get<HMONITOR>(windowMonitor.at(w)) != get<HMONITOR>(r))
        {
            changed = true;
            newWindows.insert(w);
        }

    set<HWND> deletedUnmovableWindows;
    for (auto& w : unmovableWindows) if (!windowMonitor.contains(w)) deletedUnmovableWindows.insert(w);
    for (auto& w : deletedUnmovableWindows) unmovableWindows.erase(w);
    deletedUnmovableWindows.clear();

    for (auto& [w, r] : windowMonitor)
    {
        if (!oldWindowMonitor.contains(w))
        {
            changed = true;
            auto &mrect = monitorRects.at(get<HMONITOR>(r));
            if ((GetWindowLong(w, GWL_STYLE) & WS_MAXIMIZEBOX) == 0)
            {
                using enum Corner;
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                flags<Corner, int> c = topleft;
                if (abs(cursorPos.x - mrect.right) < abs(cursorPos.x - mrect.left)) c |= right;
                if (abs(cursorPos.y - mrect.bottom) < abs(cursorPos.y - mrect.top)) c |= bottom;
                get<Corner>(r) = Corner(c);
            }
            else newWindows.insert(w);
        }
        else if (get<Rect>(oldWindowMonitor[w]) != get<Rect>(windowMonitor[w]))
            changed = true;
    }
    return changed;
}

void arrangeAllWindows(bool force)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    map<HMONITOR, Rect> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, MONITORENUMPROC(enumMonitorsProc), bit_cast<LPARAM>(&monitorRects));
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        r = info.rcWork;
        monitorNames[m] = info.szDevice;
    }

    map<HWND, Rect> windowRects;
    EnumWindows(WNDENUMPROC(enumWindowsProc), bit_cast<LPARAM>(&windowRects));

    // unmaximize windows to get rid of related issues
    for (auto const& [w, r] : windowRects)
        if (IsZoomed(w))
        {
            ShowWindow(w, SW_RESTORE);
            MoveWindow(w, r.left, r.top, int(r.width()), int(r.height()), true);
        }

    // find main monitor for each window
    map<HWND, tuple<HMONITOR, Corner, Rect>> windowMonitor;
    for (auto &[w, r] : windowRects)
    {
        auto [m, c] = findMainMonitorAndCorner(r, monitorRects);
        // if(!monitor) monitor = monitorRects.rbegin()->first; // TODO that would steal space for invisible windows, consider filtering them better because some fall into this category incorrectly
        if(m) windowMonitor[w] = {m, c, r};
        // else
        //     cout << "No monitor for window " << w << endl;
    }

    set<HWND> newWindows;
    bool changed = detectChangedWindows(windowMonitor, newWindows, monitorRects);
    if(!force && !changed) return;

    displayMonitorsAndWindows(monitorRects, windowRects);
    auto windowsOrderInCorners = distributeWindowsInCorners(windowRects, windowMonitor, newWindows, monitorRects);

    oldWindowMonitor = windowMonitor;
    adjustWindowsInMonitorCorners(windowsOrderInCorners, monitorRects, windowRects);
    // save window sizes after adjustment for size change detection to remain stable
    for (auto& [w, mcr] : oldWindowMonitor) if (windowRects.contains(w)) get<Rect>(mcr) = windowRects[w];
}

bool toggleMinimizeAllWindows()
{
    static vector<HWND> bulkMinimizedWindows;

    if (bulkMinimizedWindows.size())
    {
        for (auto& w : bulkMinimizedWindows)
            ShowWindow(w, SW_RESTORE);
        bulkMinimizedWindows.clear();
        return false;
    }
    else
    {
        for (auto const& [w, mcr] : oldWindowMonitor)
            if (!IsIconic(w))
            {
                ShowWindow(w, SW_MINIMIZE);
                bulkMinimizedWindows.push_back(w);
            }
        return true;
    }
}

// REGISTRY FUNCTIONS

bool deleteRegistryValue(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name)
{
    bool result = false;
    if (HKEY hKey; ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_SET_VALUE, &hKey))
    {
        if (ERROR_SUCCESS == RegDeleteValue(hKey, name.data()))
        {
            wcout << L"Value deleted successfully" << endl;
            result = true;
        }
        else cerr << "Failed to delete value" << endl;
        RegCloseKey(hKey);
    }
    else cerr << "Failed to open key" << endl;
    return result;
}

bool deleteRegistrySubkey(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name)
{
    bool result = false;
    if (HKEY hKey; ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_WRITE, &hKey))
    {
        if (ERROR_SUCCESS == RegDeleteKey(hKey, name.data()))
        {
            wcout << L"Registry key deleted successfully" << endl;
            result = true;
        }
        else cerr << "Failed to delete registry key" << endl;
        RegCloseKey(hKey);
    }
    else cerr << "Failed to open key" << endl;
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

        cout << "Console created successfully." << endl;
    }
    return result;
}

template<> optional<basic_string<TCHAR>>
readRegistryValue<basic_string<TCHAR>, REG_SZ>(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name)
{
    optional<basic_string<TCHAR>> result;
    if (HKEY hKey; ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey))
    {
        DWORD dataType;
        if (DWORD dataSize; ERROR_SUCCESS == RegQueryValueEx(hKey, name.data(), nullptr, &dataType, nullptr, &dataSize) && dataType == REG_SZ)
            if (auto data = make_unique<BYTE[]>(dataSize); 
                ERROR_SUCCESS == RegQueryValueEx(hKey, name.data(), nullptr, &dataType, data.get(), &dataSize))
                result = bit_cast<TCHAR*>(data.get());
        RegCloseKey(hKey);
    }
    return result;
}

template<>bool
writeRegistryValue<basic_string_view<TCHAR>, REG_SZ>(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name, const basic_string_view<TCHAR>& v)
{
    bool result = false;
    if (HKEY hKey; ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))
    {
        if (ERROR_SUCCESS == RegSetValueEx(hKey, name.data(), 0, REG_SZ, bit_cast<const BYTE*>(v.data()), DWORD(sizeof(TCHAR) * v.size())))
            result = true;
        else cerr << "Failed to set value" << endl;
        RegCloseKey(hKey);
    }
    else cerr << "Failed to create/open key" << endl;

    return result;
}

template<>bool
writeRegistryValue<wstring, REG_SZ>(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name, const wstring& v)
{
    HKEY hKey;
    bool result = false;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(hKey, name.data(), 0, REG_SZ, (const BYTE*)v.data(), DWORD(sizeof(wchar_t) * v.size())) == ERROR_SUCCESS)
            result = true;
        else 
            cerr << "Failed to set value" << endl;
        RegCloseKey(hKey);
    }
    else 
        cerr << "Failed to create/open key" << endl;

    return result;
}
