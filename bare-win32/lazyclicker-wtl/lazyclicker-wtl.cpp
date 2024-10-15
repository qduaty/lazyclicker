#include <windows.h>
#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlwin.h>
#include <atlctrls.h>
#include "resource.h"
#include "windowops.h"

CAppModule _Module;
constexpr TCHAR settingsKey[] = _T("Software\\qduaty\\lazyclicker\\Preferences");

class CMainWnd : public CWindowImpl<CMainWnd>
{
public:
    DECLARE_WND_CLASS(nullptr)

    BEGIN_MSG_MAP(CMainWnd)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_TRAYICON, OnTrayIcon)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

    LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        if (m_bAutoArrange)
            processAllWindows();
        return 0;
    }

    LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        //auto hMainIcon = LoadIcon(nullptr, (LPCTSTR)MAKEINTRESOURCE(IDI_LAZYCLICKER));

        NOTIFYICONDATA nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = m_hWnd;
        nid.uID = IDI_LAZYCLICKER;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(HINSTANCE(GetWindowLongPtr(GWLP_HINSTANCE)), MAKEINTRESOURCE(IDI_LAZYCLICKER));
        //        LoadString(hInstance, IDS_APPTOOLTIP, nidApp.szTip, MAX_LOADSTRING);
        lstrcpy(nid.szTip, _T("Click to arrange windows"));
        Shell_NotifyIcon(NIM_ADD, &nid);
        SetTimer(1, 1000);

        m_bAutoArrange = readRegistryValue<std::wstring, REG_SZ>(settingsKey, L"actionAuto_arrange_windows") == L"true";

        return 0;
    }

    LRESULT OnTrayIcon(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONDOWN:
            processAllWindows();
            break;
        case WM_RBUTTONUP:
            {
                CMenu menu;
                menu.CreatePopupMenu();
                menu.AppendMenu(MF_STRING | (m_bAutoArrange ? MF_CHECKED : 0), ID_TRAYMENU_OPTION_AUTO_ARRANGE, _T("Auto arrange windows"));
                menu.AppendMenu(MF_STRING, ID_TRAYMENU_OPTION_QUIT, _T("Quit and unregister"));

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(m_hWnd);
                menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
            }
            break;
            case WM_LBUTTONDBLCLK:
                ShowWindow(IsWindowVisible() ? SW_HIDE : SW_NORMAL);
            break;
        }

        return 0;
    }

    LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
    {
        switch (LOWORD(wParam))
        {
        case ID_TRAYMENU_OPTION_AUTO_ARRANGE:
            m_bAutoArrange = !m_bAutoArrange;
            writeRegistryValue<std::wstring, REG_SZ>(settingsKey, L"actionAuto_arrange_windows", m_bAutoArrange ? L"true" : L"false");
            break;
        case ID_TRAYMENU_OPTION_QUIT:
            quitAndUnregister();
            break;
        default:
            break;
        }

        return 0;
    }

    LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        NOTIFYICONDATA nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = m_hWnd;
        nid.uID = 1;
        Shell_NotifyIcon(NIM_DELETE, &nid);

        PostQuitMessage(0);
        return 0;
    }

private:
    void quitAndUnregister() 
    {
        DestroyWindow();
    }

    BOOL m_bAutoArrange = FALSE;

    enum { WM_TRAYICON = WM_USER + 1, ID_TRAYMENU_OPTION_AUTO_ARRANGE = 1001, ID_TRAYMENU_OPTION_QUIT = 1002 };
};

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR /*lpCmdLine*/, int nCmdShow)
{
    _Module.Init(nullptr, hInstance);

    CMainWnd wnd;
    auto hWnd = wnd.Create(nullptr, CWindow::rcDefault, _T("WTL Tray App"), WS_OVERLAPPEDWINDOW, WS_EX_TRANSPARENT);
    //wnd.ShowWindow(nCmdShow);
    wnd.UpdateWindow();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    _Module.Term();
    return static_cast<int>(msg.wParam);
}
