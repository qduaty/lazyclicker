#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <QPoint>
#include <windows.h>
#include <map>

BOOL IsAltTabWindow(HWND hwnd);
BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam);
BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam);
size_t calculateRectArea(RECT rect);
enum class Corner: int {none, topleft=1, topright=2, bottomleft=4, bottomright=8};
int findCorners(RECT window, RECT monitor);
RECT trimAndMoveToMonitor(RECT windowRect, RECT monRect);
QPoint windowDistanceFromCorner(RECT wrect, RECT mrect, Corner c);
/// move windows so they are on their position in the right corner and resize them so they don't cover other corners
void arrangeCorners(const std::map<HMONITOR, std::map<Corner, std::vector<HWND>>>& windowsOnMonitors,
                    const std::map<HMONITOR, RECT>& monitorRects,
                    const std::map<HWND, RECT>& windows,
                    const std::map<HWND, HMONITOR>& windowMonitor,
                    size_t unitSize);
HMONITOR findMainMonitor(HWND w, RECT &windowRect, const std::map<HMONITOR, RECT> &monitorRects);

#endif // WINDOWOPS_H
