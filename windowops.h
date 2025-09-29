#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <Windows.h>
#include <optional>
#include <iostream>
#include <bit>

extern int windowops_maxIncrease;
extern bool avoidTopRightCorner;
extern bool increaseUnitSizeForTouch;

void arrangeAllWindows(bool force = false, bool reset = false);
/// <summary>
/// </summary>
/// <returns>windows were minimized</returns>
bool toggleMinimizeAllWindows();

template<typename T, int RegType> inline std::optional<T> 
readRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name)
{
    std::optional<T> result;
    if (HKEY hKey; ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey))
    {
        DWORD dataType;
        if (DWORD dataSize; ERROR_SUCCESS == RegQueryValueEx(hKey, name.data(), nullptr, &dataType, nullptr, &dataSize) && dataType == RegType)
            if (auto data = std::make_unique<BYTE[]>(dataSize); 
                ERROR_SUCCESS == RegQueryValueEx(hKey, name.data(), nullptr, &dataType, data.get(), &dataSize))
                result = *std::bit_cast<T*>(data.get());
        RegCloseKey(hKey);
    }
    return result;
}

template<typename T, int RegType> requires std::_Different_from<T, std::basic_string<TCHAR>>
inline bool writeRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name, const T& v)
{
    HKEY hKey;
    bool result = false;
    if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))
    {
        if (ERROR_SUCCESS == RegSetValueEx(hKey, name.data(), 0, RegType, (const BYTE*)&v, sizeof(T))) result = true;
        else std::cerr << "Failed to set value" << std::endl;
        RegCloseKey(hKey);
    }
    else std::cerr << "Failed to create/open key" << std::endl;
    return result;
}

template<> std::optional<std::wstring>
readRegistryValue<std::basic_string<TCHAR>, REG_SZ>(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
template<>bool
writeRegistryValue<std::basic_string_view<TCHAR>, REG_SZ>(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name, const std::basic_string_view<TCHAR>& v);
bool deleteRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
bool deleteRegistrySubkey(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
/// <summary>
/// Create win32 console in order to access standard pipes
/// </summary>
bool CreateConsole();

#endif // WINDOWOPS_H
