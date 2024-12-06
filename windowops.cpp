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
    constexpr size_t width() const { return right - left; }
    constexpr size_t height() const { return bottom - top; }
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

static PROCESS_DPI_AWARENESS GetProcessDpiAwarenessFromHWND(HWND hwnd)
{
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;

    if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId); hProcess)
    {
        GetProcessDpiAwareness(hProcess, &awareness);
        CloseHandle(hProcess);
    }
    return awareness;
}


// https://stackoverflow.com/questions/7277366/why-does-enumwindows-return-more-windows-than-i-expected
static BOOL IsAltTabWindow(HWND hwnd)
{
    if (!hwnd) return FALSE;
    TITLEBARINFO ti {sizeof(TITLEBARINFO)};
    HWND hwndTry;
    HWND hwndWalk = nullptr;

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

static BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam)
{
    auto &windows = *bit_cast<map<HWND, Rect>*>(lParam);
    if(!IsAltTabWindow(hWnd)) return TRUE;

    int length = GetWindowTextLength(hWnd);
    if(!length) return TRUE;
    auto buffer = make_unique<char[]>(length + 1);
    GetWindowTextA(hWnd, buffer.get(), length + 1);

    Rect &rect = windows[hWnd];
    GetWindowRect(hWnd, &rect);
    if (string processName = GetProcessNameFromHWND(hWnd); processName == "ApplicationFrameHost.exe" || IsIconic(hWnd))
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
        borderWidth = GetThemeSysSize(theme, SM_CXPADDEDBORDER) * sf / 100;
        borderHeight = int(borderWidth * 100 / sf);
        CloseThemeData(theme);
        return true;
    }
    return false;
}

static void adjustWindowsInCorner(const flags<Corner, int>& corner,
                                  const std::map<flags<Corner, int>, std::multimap<size_t, HWND>>& mcvw, 
                                  int unitSize,
                                  std::map<HWND, Rect>& windowRects, 
                                  const Rect& mrect, 
                                  SIZE borderSize, 
                                  const HMONITOR& mon,
                                  bool multiMonitor)
{
    int i = 0;
    const auto& windows = mcvw.at(corner);
    for (auto& [s, w] : windows)
    {
        if (multiMonitor && GetProcessDpiAwarenessFromHWND(w) != PROCESS_PER_MONITOR_DPI_AWARE) borderSize = { 0, 0 };

        flags<Corner, int> otherCorner;
        // 1°
        otherCorner = corner ^ Corner::right;
        long dx0 = long(mcvw.at(otherCorner).size()) * unitSize;
        // 2°
        otherCorner = corner ^ Corner::bottom;
        long n = max(0, long(mcvw.at(otherCorner).size()) - i);
        long dy = n * unitSize;
        // 3°
        otherCorner = corner ^ Corner::bottomright;
        long dx1 = max(dx0, long(mcvw.at(otherCorner).size()) * unitSize);
        long maxIncreaseX = windowops_maxIncrease;
        long maxIncreaseY = maxIncreaseX;
        bool result = false;
        auto& wrect = windowRects.at(w);
        if (oldWindowMonitor.contains(w))
        {
            auto& oldWrect = get<Rect>(oldWindowMonitor[w]);
            maxIncreaseX = max(maxIncreaseX, oldWrect.right - oldWrect.left - (wrect.right - wrect.left));
            maxIncreaseY = max(maxIncreaseY, oldWrect.bottom - oldWrect.top - (wrect.bottom - wrect.top));
        }
        int borderWidth = borderSize.cx;
        int borderHeight = borderSize.cy;
        if (wrect.right - wrect.left + maxIncreaseX > mrect.right - mrect.left)
        {
            wrect.left = mrect.left - borderWidth;
            wrect.right = mrect.right + borderWidth;
        }
        if (wrect.bottom - wrect.top + maxIncreaseY > mrect.bottom - mrect.top)
        {
            wrect.top = mrect.top - borderHeight;
            wrect.bottom = mrect.bottom + borderHeight;
        }
        RECT newRect;
        if (corner & Corner::right)
        {
            newRect.right = mrect.right + borderWidth - i * unitSize;
            newRect.left = max(wrect.left + newRect.right - wrect.right, mrect.left + dx1 - borderWidth);
        }
        else
        {
            newRect.left = mrect.left - borderWidth + i * unitSize;
            newRect.right = min(wrect.right + newRect.left - wrect.left, mrect.right - dx1 + borderWidth);
        }
        if (corner & Corner::bottom)
        {
            newRect.bottom = mrect.bottom + borderWidth - (long(windows.size()) - i - 1) * unitSize;
            newRect.top = max(wrect.top + newRect.bottom - wrect.bottom, mrect.top + dy - borderHeight);
        }
        else
        {
            newRect.top = mrect.top - borderHeight + (long(windows.size()) - i - 1) * unitSize;
            newRect.bottom = min(wrect.bottom + newRect.top - wrect.top, mrect.bottom - dy + borderHeight);
        }
        wrect = newRect;
        result = MoveWindow(w, wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top, TRUE);
        if (result)
        {
            array<const char*, 4> cornerNames{ "Corner::topleft", "Corner::topright", "Corner::bottomleft", "Corner::bottomright" };
            cout << "Moved window " << w << " [" << windowTitles[w].first << "] " << '(' << monitorNames[mon] << '@';
            cout << cornerNames[int(corner)] << ':' << i << ')';
            cout << "; dx=" << dx1 / unitSize << ", dy=" << dy / unitSize;
            cout << "; relative: " << wrect.left - mrect.left << ':';
            cout << wrect.top - mrect.top << ':' << wrect.right - mrect.right << ':' << wrect.bottom - mrect.bottom << '[';
        }
        else
        {
            unmovableWindows.insert(w);
            oldWindowMonitor.erase(w);
        }

        cout << result << ']' << endl;
        i++;
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
        
        bool multiMonitor = windowsOrderInCorners.size() > 1;
        for(int i = 0; i < 4; i++)
            adjustWindowsInCorner(Corner(i), mcvw, unitSize, windowRects, mrect, { borderWidth, borderHeight }, mon, multiMonitor);
    }
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
            int dist = wrect.distanceFromCorner(mrect, Corner(i));
            if(dist < minDist)
            {
                minDist = dist;
                corner = Corner(i);
            }
        }
    }
    return { mon, corner };
}

// API FUNCTIONS

static void distributeNewWindowsInCorners(std::multimap<size_t, std::pair<HWND, Corner>>& mwc, const std::set<HWND>& newWindows, 
    const Rect& mr, std::queue<Corner>& freeCorners, std::map<flags<Corner, int>, std::multimap<size_t, HWND>>& order)
{
    using enum Corner;
    int i = 0;
    array<Corner, 4> corners{ topright, bottomright, topleft, bottomleft };
    bool smallWindowsEnded = false;
    for (auto& [s, wc] : mwc)
    {
        auto& [w, c] = wc;
        if (!newWindows.contains(w) || unmovableWindows.contains(w)) continue;

        if (!smallWindowsEnded && s >= mr.height() * 9 / 10)
        {
            smallWindowsEnded = true;
            i = 0;
            corners = { topleft, topright, bottomleft, bottomright };
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
    map<HMONITOR, multimap<size_t, pair<HWND, Corner>>> windowsOnMonitor;
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
        for (auto const& [c, vw] : windowsOrderInCorners[m]) 
            for (int i = 0; i < maxNumWindows - vw.size(); i++) 
                freeCorners.push(Corner(int(c)));

        distributeNewWindowsInCorners(mwc, newWindows, monitorRects.at(m), freeCorners, windowsOrderInCorners[m]);
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
            << rect.right << ':' << rect.bottom << endl;
    }
}

void processAllWindows(bool force)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    map<HMONITOR, Rect> monitorRects;
    EnumDisplayMonitors(nullptr, nullptr, MONITORENUMPROC(enumMonitorsProc), reinterpret_cast<LPARAM>(&monitorRects));
    for(auto &[m, r]: monitorRects)
    {
        MONITORINFOEXA info {sizeof(MONITORINFOEXA)};
        GetMonitorInfoA(m, &info);
        r = info.rcWork;
        monitorNames[m] = info.szDevice;
    }

    map<HWND, Rect> windowRects;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));

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

    bool changed = false;
    set<HWND> newWindows;

    for(auto &[w, r]: oldWindowMonitor)
        if(!windowMonitor.contains(w)) 
            changed = true;
        else if(get<HMONITOR>(windowMonitor.at(w)) != get<HMONITOR>(r))
        {
            changed = true;
            newWindows.insert(w);
        }

    set<HWND> deletedUnmovableWindows;
    for (auto& w : unmovableWindows) if(!windowMonitor.contains(w)) deletedUnmovableWindows.insert(w);
    for (auto& w : deletedUnmovableWindows) unmovableWindows.erase(w);
    deletedUnmovableWindows.clear();

    for(auto &[w, r]: windowMonitor)
    {
        if (!oldWindowMonitor.contains(w))
        {
            changed = true;
            auto mrect = monitorRects[get<HMONITOR>(r)];
            if (GetWindowLong(w, GWL_STYLE) & WS_MAXIMIZEBOX == 0)
            {
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                flags<Corner, int> c = Corner::topleft;
                if (abs(cursorPos.x - mrect.right) < abs(cursorPos.x - mrect.left)) c |= Corner::right;
                if (abs(cursorPos.y - mrect.bottom) < abs(cursorPos.y - mrect.top)) c |= Corner::bottom;
                get<Corner>(r) = Corner(c);
            }
            else newWindows.insert(w);
        }
        else if (get<Rect>(oldWindowMonitor[w]) != get<Rect>(windowMonitor[w]))
            changed = true;
    }

    if(!force && !changed) return;

    displayMonitorsAndWindows(monitorRects, windowRects);
    auto windowsOrderInCorners = distributeWindowsInCorners(windowRects, windowMonitor, newWindows, monitorRects);

    // TODO there is a reference to oldWindowMonitor in adjustWindowsInMonitorCorners, try to move 
    // this line after and check if windows are resized correctly, otherwise remove that reference
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
    HKEY hKey;
    bool result = false;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (RegDeleteValue(hKey, name.data()) == ERROR_SUCCESS)
        {
            wcout << L"Value deleted successfully" << endl;
            result = true;
        }
        else
        {
            cerr << "Failed to delete value" << endl;
        }
        RegCloseKey(hKey);
    }
    else
    {
        cerr << "Failed to open key" << endl;
    }

    return result;
}

bool deleteRegistrySubkey(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name)
{
    HKEY hKey;
    bool result = false;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        if (RegDeleteKey(hKey, name.data()) == ERROR_SUCCESS)
        {
            wcout << L"Registry key deleted successfully" << endl;
            result = true;
        }
        else
        {
            cerr << "Failed to delete registry key" << endl;
        }
        RegCloseKey(hKey);
    }
    else
    {
        cerr << "Failed to open key" << endl;
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

        cout << "Console created successfully." << endl;
    }
    return result;
}

template<> optional<wstring>
readRegistryValue<wstring, REG_SZ>(basic_string_view<TCHAR> key, basic_string_view<TCHAR> name)
{
    HKEY hKey;
    optional<wstring> result;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dataType;
        if (DWORD dataSize; RegQueryValueEx(hKey, name.data(), nullptr, &dataType, nullptr, &dataSize) == ERROR_SUCCESS && dataType == REG_SZ)
        {
            auto data = make_unique<BYTE[]>(dataSize);
            if (RegQueryValueEx(hKey, name.data(), nullptr, &dataType, data.get(), &dataSize) == ERROR_SUCCESS)
                result = bit_cast<wchar_t*>(data.get());
        }
        RegCloseKey(hKey);
    }
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

