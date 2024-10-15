# lazyclicker <img src="mainicon.png" width=24>
There is a feature on Windows that allows to access windows with 
no clicking. It is available as a mouse accessibility option and 
is equivalent to Linux' X-Mouse. However, when a window rectangle 
is inside a bigger one, it may get accidentally covered.

This program arranges visible windows on Windows screens so they 
all have a visible corner. Maximized or prearranged windows will 
be slightly reduced, windows that spanned multiple screens will be 
reduced to a single screen, and those that are almost full screen
will be enlarged.
## Current status
The app installs itself for startup and provides a menu option for 
uninstall. Single click on the tray icon triggers windows 
rearrangement. Alternatively, a menu option allows to 
schedule windows auto-arrangement every second.
## Prerequisities
- Windows 11 (may work on 10 but was not tested)
- QtCreator for Qt6 or Visual Studio (2022) for WTL implementation
## TODO
- perhaps add an option to skip the topright corner, so that 
windows can be raised with clicking
- test on a big screen and perhaps implement conditional middle-side placement of smallest windows
- finish implementing corner preservation so that windows do not make meaningless moves
