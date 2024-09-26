#ifndef WINDOWOPS_H
#define WINDOWOPS_H
#include <QPoint>
#include <windows.h>
#include <map>

BOOL IsAltTabWindow(HWND hwnd);
BOOL CALLBACK enumWindowsProc(HWND hWnd, LPARAM lParam);
BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC dc, LPRECT pRect, LPARAM lParam);
size_t calculateRectArea(RECT rect);
template<typename E, typename I>struct flags {
    I v;
    flags(E v): v(I(v)) {}
    operator I() const {return v;}
    I operator&(E x) const {return v & I(x);}
};
enum class Corner: int {none, top=1, left=2, topleft=top|left, right=4, topright=top|right, bottom=8, bottomleft=bottom|left, bottomright=bottom|right};
int findCorners(RECT window, RECT monitor);
RECT trimAndMoveToMonitor(RECT windowRect, RECT monRect);
QPoint windowDistanceFromCorner(RECT wrect, RECT mrect, Corner c);
/// move windows so they are on their position in the right corner and resize them so they don't cover other corners
void arrangeWindowsInMonitorCorners(const std::map<HMONITOR, std::map<flags<Corner, int>, std::vector<HWND>>>& windowsOnMonitors,
                    const std::map<HMONITOR, RECT>& monitorRects,
                    std::map<HWND, RECT>& windows,
                    size_t unitSize);
HMONITOR findMainMonitor(HWND w, RECT &windowRect, const std::map<HMONITOR, RECT> &monitorRects);

#endif // WINDOWOPS_H
