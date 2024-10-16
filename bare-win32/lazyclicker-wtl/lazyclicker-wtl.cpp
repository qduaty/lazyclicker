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
constexpr TCHAR startupKey[] = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
class CMainWnd;

class CSettingsDlg : public CDialogImpl<CSettingsDlg>
{
public:
    CSettingsDlg(CMainWnd* pMainWindow) : pMainWindow(pMainWindow) {}

    constexpr static int IDD = IDD_SETTINGS_DIALOG;

    BEGIN_MSG_MAP(CSettingsDlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        MSG_WM_HSCROLL(OnHScroll)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        // Initialize controls here
        m_slider.Attach(GetDlgItem(IDC_SLIDER_ALLOWED_INCREASE));
        RECT sliderRect;
        m_slider.GetClientRect(&sliderRect);
        m_slider.SetRange(0, sliderRect.right - sliderRect.left);
//        slider.SetRange(0, 100);
        m_slider.SetPos(allowedIncrease);
        // CUpDownCtrl spin = GetDlgItem(IDC_SPIN1);
        return TRUE;
    }

    LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
    {
        m_slider.Detach();
        EndDialog(wID);
        return 0;
    }

    void OnHScroll(int nScrollCode, short nPos, HWND hwndScrollBar);

    CMainWnd* pMainWindow = nullptr;
    int allowedIncrease;
private:
    CTrackBarCtrl m_slider;
};

class CMainWnd : public CWindowImpl<CMainWnd>
{
public:
    CMainWnd() : settingsDlg(this) {}
    DECLARE_WND_CLASS(nullptr)

    BEGIN_MSG_MAP(CMainWnd)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_TRAYICON, OnTrayIcon)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SLIDER_CHANGE, OnSliderChange)
    END_MSG_MAP()

    LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        if (m_bAutoArrange)
            processAllWindows();
        return 0;
    }

    LRESULT OnSliderChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        windowops_maxIncrease = static_cast<int>(wParam);
        writeRegistryValue<DWORD, REG_DWORD>(settingsKey, L"allowedIncrease", windowops_maxIncrease);
        processAllWindows();
        return 0;
    }

    LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        SetTimer(1, 1000);

        auto hInstance = HINSTANCE(GetWindowLongPtr(GWLP_HINSTANCE));
        m_bAutoArrange = readRegistryValue<std::wstring, REG_SZ>(settingsKey, L"actionAuto_arrange_windows") == L"true";
        updateTrayIcon(true);
        windowops_maxIncrease = readRegistryValue<DWORD, REG_DWORD>(settingsKey, L"allowedIncrease").value_or(0);
        settingsDlg.allowedIncrease = windowops_maxIncrease;
        TCHAR processName[MAX_PATH] = { 0 };
        if (GetModuleFileName(hInstance, processName, MAX_PATH))
            writeRegistryValue<std::wstring, REG_SZ>(startupKey, L"lazyclicker", processName);

        return 0;
    }

    LRESULT OnTrayIcon(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONDOWN:
            if (m_bAutoArrange)
                toggleMinimizeAllWindows();
            else
                processAllWindows();
            break;
        case WM_RBUTTONUP:
            {
                CMenu menu;
                menu.CreatePopupMenu();
                menu.AppendMenu(MF_STRING | (m_bAutoArrange ? MF_CHECKED : 0), ID_TRAYMENU_OPTION_AUTO_ARRANGE, _T("Auto arrange windows"));
                if(!m_bAutoArrange) menu.AppendMenu(MF_STRING, ID_TRAYMENU_TOGGLE_MINIMIZE_ALL, _T("Toggle minimize all windows"));
                menu.AppendMenu(MF_STRING, ID_TRAYMENU_OPTION_QUIT, _T("Quit"));
                menu.AppendMenu(MF_STRING, ID_TRAYMENU_OPTION_QUIT_AND_UNREGISTER, _T("Uninstall"));

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(m_hWnd);
                menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
            }
            break;
            case WM_LBUTTONDBLCLK:
                settingsDlg.DoModal();
                //ShowWindow(IsWindowVisible() ? SW_HIDE : SW_NORMAL);
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
            updateTrayIcon(false);
            break;
        case ID_TRAYMENU_OPTION_QUIT:
            DestroyWindow();
            break;
        case ID_TRAYMENU_OPTION_QUIT_AND_UNREGISTER:
            quitAndUnregister();
            break;
        case ID_TRAYMENU_TOGGLE_MINIMIZE_ALL:
            if(!toggleMinimizeAllWindows()) processAllWindows(true); 
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
    void updateTrayIcon(bool create)
    {
        //auto hMainIcon = LoadIcon(nullptr, (LPCTSTR)MAKEINTRESOURCE(IDI_LAZYCLICKER));

        NOTIFYICONDATA nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = m_hWnd;
        nid.uID = IDI_LAZYCLICKER;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        auto hInstance = HINSTANCE(GetWindowLongPtr(GWLP_HINSTANCE));
        nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAZYCLICKER));
        LoadString(hInstance, (m_bAutoArrange ? IDS_APPTOOLTIP2 : IDS_APPTOOLTIP1), nid.szTip, std::size(nid.szTip));
        Shell_NotifyIcon(create ? NIM_ADD : NIM_MODIFY, &nid);
    }

    void quitAndUnregister()
    {
        deleteRegistryValue(startupKey, L"lazyclicker");
        deleteRegistrySubkey(L"Software\\qduaty\\lazyclicker", L"Preferences");
        deleteRegistrySubkey(L"Software\\qduaty", L"lazyclicker");
        deleteRegistrySubkey(L"Software", L"qduaty");
        DestroyWindow();
    }

    BOOL m_bAutoArrange = FALSE;
    CSettingsDlg settingsDlg;

    enum { 
        ID_TRAYMENU_OPTION_AUTO_ARRANGE = 1001, 
        ID_TRAYMENU_OPTION_QUIT = 1002, 
        ID_TRAYMENU_OPTION_QUIT_AND_UNREGISTER = 1003,
        ID_TRAYMENU_TOGGLE_MINIMIZE_ALL = 1004
    };
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

void CSettingsDlg::OnHScroll(int nScrollCode, short nPos, HWND hwndScrollBar)
{
    if (nScrollCode == SB_ENDSCROLL && hwndScrollBar == m_slider.m_hWnd)
    {
        allowedIncrease = m_slider.GetPos();
        pMainWindow->SendMessage(WM_SLIDER_CHANGE, allowedIncrease);
    }
}
