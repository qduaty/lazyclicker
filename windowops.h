#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <windows.h>
#include <optional>
#include <string>
#include <iostream>

extern int windowops_maxIncrease;

void processAllWindows();
void toggleMinimizeAllWindows();

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

template<> std::optional<std::wstring>
readRegistryValue<std::wstring, REG_SZ>(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name);
template<>bool
writeRegistryValue<std::wstring, REG_SZ>(const std::basic_string_view<TCHAR> key, const std::basic_string_view<TCHAR> name, const std::wstring& v);
bool deleteRegistryValue(const std::basic_string<TCHAR>& key, const std::basic_string<TCHAR>& name);
bool deleteRegistrySubkey(const std::basic_string<TCHAR>& key, const std::basic_string<TCHAR>& name);
#endif // WINDOWOPS_H
