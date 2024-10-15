#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <windows.h>
#include <map>
#include <vector>
#include <string>
#include <any>
#include <optional>
#include <iostream>

extern int allowIncreaseByUnits;
BOOL IsAltTabWindow(HWND hwnd);
BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam);
BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam);
size_t calculateRectArea(RECT rect);
template<typename E, typename I>struct flags {
    I v;
    flags() = default;
    flags(E x): v(I(x)) {}
    auto operator=(E x) { v = I(x); return *this; }
    auto operator=(I x) { v = x; return *this; }
    operator I() const {return v;}
    I operator&(E x) const {return v & I(x);}
    bool operator==(E x) const {return v == I(x);}
};
enum class Corner: int {top=0, left=0, topleft=top|left, right=1, topright=top|right, bottom=2, bottomleft=bottom|left, bottomright=bottom|right};
RECT trimAndMoveToMonitor(RECT windowRect, RECT monRect);
int windowDistanceFromCorner(RECT wrect, RECT mrect, flags<Corner, int> c);
void resetAllWindowPositions(const std::map<HMONITOR, std::map<flags<Corner, int>, std::vector<HWND>>> &windowsOrderInCorners,
                             const std::map<HMONITOR, RECT> &monitorRects,
                             std::map<HWND, RECT> &windowRects,
                             size_t unitSize);
/// move windows so they are on their position in the right corner and resize them so they don't cover other corners
void arrangeWindowsInMonitorCorners(const std::map<HMONITOR, std::map<flags<Corner, int>, std::vector<HWND>>>& windowsOnMonitors,
                    const std::map<HMONITOR, RECT>& monitorRects,
                    std::map<HWND, RECT>& windows);
std::pair<HMONITOR, Corner> findMainMonitorAndCorner(HWND w, RECT &windowRect, const std::map<HMONITOR, RECT> &monitorRects);

void processAllWindows();



template<typename T, int RegType> inline std::optional<T> 
readRegistryValue(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name)
{
    HKEY hKey;
    std::optional<T> result;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dataType;
        DWORD dataSize;
        if (RegQueryValueEx(hKey, name.data(), NULL, &dataType, NULL, &dataSize) == ERROR_SUCCESS && dataType == RegType)
        {
            BYTE* data = new BYTE[dataSize];
            if (RegQueryValueEx(hKey, name.data(), NULL, &dataType, data, &dataSize) == ERROR_SUCCESS)
                result = *reinterpret_cast<T*>(data);
            delete[] data;
        }
        RegCloseKey(hKey);
    }
    return result;
}

template<> inline std::optional<std::wstring>
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

template<typename T, int RegType>inline bool
writeRegistryValue(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name, const T& v)
{
    HKEY hKey;
    bool result = false;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(hKey, name.data(), 0, RegType, (const BYTE*)&v, sizeof(T)) == ERROR_SUCCESS)
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

template<>inline bool
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

#endif // WINDOWOPS_H
