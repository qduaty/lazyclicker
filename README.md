# lazyclicker <img src="mainicon.png" width=24>
Arranges visible windows on Windows screens so they all have a visible corner.
This works like a 3/4-corner windows cascade. Maximized or prearranged windows
will be slightly reduced, windows that spanned multiple screens will be reduced
to a single screen, and big windows may be slightly enlarged to fit the screen.
There are optimizations for small, big, touch and vertical screens.

Motivation: the mouse accessibility option on Windows that allows raising 
windows without clicking (equivalent to Linux' X-mouse) is not very useful if
windows are covered by others.

## Current status
- Installs itself for startup and provides a menu option for uninstall
- Has manual and automatic mode
- Can hide and restore all windows
- Double click on the tray icon shows a settings window
- Can avoid placing windows in the top-right corner for raising windows by 
clicking
## Prerequisities
- Windows 11 (may work on 10 but was not tested)
- Visual Studio (2022) for WTL implementation or QtCreator for Qt6
