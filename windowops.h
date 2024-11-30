#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <Windows.h>
#include <optional>
#include <iostream>
#include <bit>

extern int windowops_maxIncrease;

void processAllWindows(bool force = false);
/// <summary>
/// </summary>
/// <returns>windows were minimized</returns>
bool toggleMinimizeAllWindows();

template<typename T, int RegType> inline std::optional<T> 
readRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name)
{
    HKEY hKey;
    std::optional<T> result;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, key.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dataType;
        if (DWORD dataSize; RegQueryValueEx(hKey, name.data(), nullptr, &dataType, nullptr, &dataSize) == ERROR_SUCCESS && dataType == RegType)
        {
            auto data = std::make_unique<BYTE[]>(dataSize);
            if (RegQueryValueEx(hKey, name.data(), nullptr, &dataType, data.get(), &dataSize) == ERROR_SUCCESS)
                result = *std::bit_cast<T*>(data.get());
        }
        RegCloseKey(hKey);
    }
    return result;
}

template<typename T, int RegType>inline bool
writeRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name, const T& v)
{
    HKEY hKey;
    bool result = false;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, key.data(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
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

template<> std::optional<std::wstring>
readRegistryValue<std::wstring, REG_SZ>(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
template<>bool
writeRegistryValue<std::wstring, REG_SZ>(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name, const std::wstring& v);
bool deleteRegistryValue(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
bool deleteRegistrySubkey(std::basic_string_view<TCHAR> key, std::basic_string_view<TCHAR> name);
/// <summary>
/// Create win32 console in order to access standard pipes
/// </summary>
bool CreateConsole();

#endif // WINDOWOPS_H
