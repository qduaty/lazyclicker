#include <Windows.h>
#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlwin.h>
#include <atlctrls.h>
#include "resource.h"
#include "windowops.h"

#define WM_TRAYICON (WM_USER + 1)
#define WM_SLIDER_CHANGE (WM_USER + 2)
#define WM_CHECKBOX_CHANGE (WM_USER + 3)

CAppModule _Module;
constexpr TCHAR settingsKey[] = _T("Software\\qduaty\\lazyclicker\\Preferences");
constexpr TCHAR startupKey[] = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
class CMainWnd;

using namespace std;

class CSettingsDlg : public CDialogImpl<CSettingsDlg>
{
public:
    CSettingsDlg(CMainWnd* pMainWindow) : pMainWindow(pMainWindow) {}

    constexpr static int IDD = IDD_SETTINGS_DIALOG;

    BEGIN_MSG_MAP(CSettingsDlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        MSG_WM_HSCROLL(OnHScroll)
        COMMAND_HANDLER(IDC_CHECK_DOUBLE_DISTANCE_AROUND_TOPRIGHT_CORNER, BN_CLICKED, OnCheckBoxClicked)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL const& /*bHandled*/)
    {
        // Initialize controls here
        m_slider.Attach(GetDlgItem(IDC_SLIDER_ALLOWED_INCREASE));
        RECT sliderRect;
        m_slider.GetClientRect(&sliderRect);
        m_slider.SetRange(0, sliderRect.right - sliderRect.left);
        m_slider.SetPos(allowedIncrease);
        m_checkBox.Attach(GetDlgItem(IDC_CHECK_DOUBLE_DISTANCE_AROUND_TOPRIGHT_CORNER));
        m_checkBox.SetCheck(doubleDistanceAroundTopRightCorner);
        return TRUE;
    }

    LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND__ const */*hWndCtl*/, BOOL const& /*bHandled*/)
    {
        m_slider.Detach();
        EndDialog(wID);
        return 0;
    }

    void OnHScroll(int nScrollCode, short [[maybe_unused]] nPos, HWND hwndScrollBar);
    LRESULT OnCheckBoxClicked(WORD /*wNotifyCode*/, WORD /*wID*/, HWND__ const* /*hWndCtl*/, BOOL const& /*bHandled*/);
    int allowedIncrease = 0;
    bool doubleDistanceAroundTopRightCorner = false;
private:
    CMainWnd* pMainWindow = nullptr;
    CTrackBarCtrl m_slider;
    CButton m_checkBox;
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
        MESSAGE_HANDLER(WM_CHECKBOX_CHANGE, OnCheckboxChange)
    END_MSG_MAP()

    LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL const& /*bHandled*/) const
    {
        if (m_bAutoArrange)
            arrangeAllWindows();
        return 0;
    }

    LRESULT OnSliderChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL const& /*bHandled*/) const
    {
        windowops_maxIncrease = static_cast<int>(wParam);
        writeRegistryValue<DWORD, REG_DWORD>(settingsKey, L"allowedIncrease", windowops_maxIncrease);
        arrangeAllWindows();
        return 0;
    }

    LRESULT OnCheckboxChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL const& /*bHandled*/) const
    {
        doubleDistanceAroundTopRightCorner = static_cast<bool>(wParam);
        writeRegistryValue<DWORD, REG_DWORD>(settingsKey, L"doubleDistanceAroundTopRightCorner", doubleDistanceAroundTopRightCorner);
        arrangeAllWindows();
        return 0;
    }

    LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
    {
        SetTimer(1, 1000);

        auto hInstance = HINSTANCE(GetWindowLongPtr(GWLP_HINSTANCE));
        m_bAutoArrange = readRegistryValue<wstring, REG_SZ>(settingsKey, L"actionAuto_arrange_windows") == L"true";
        updateTrayIcon(true);
        windowops_maxIncrease = readRegistryValue<DWORD, REG_DWORD>(settingsKey, L"allowedIncrease").value_or(0);
        settingsDlg.allowedIncrease = windowops_maxIncrease;
        doubleDistanceAroundTopRightCorner = readRegistryValue<DWORD, REG_DWORD>(settingsKey, L"doubleDistanceAroundTopRightCorner").value_or(0);
        settingsDlg.doubleDistanceAroundTopRightCorner = doubleDistanceAroundTopRightCorner;
        TCHAR processName[MAX_PATH] = { 0 };
        if (GetModuleFileName(hInstance, processName, MAX_PATH))
            writeRegistryValue<wstring, REG_SZ>(startupKey, L"lazyclicker", processName);

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
                arrangeAllWindows();
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

    LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL const& /*bHandled*/)
    {
        switch (LOWORD(wParam))
        {
        case ID_TRAYMENU_OPTION_AUTO_ARRANGE:
            m_bAutoArrange = !m_bAutoArrange;
            writeRegistryValue<wstring, REG_SZ>(settingsKey, L"actionAuto_arrange_windows", m_bAutoArrange ? L"true" : L"false");
            updateTrayIcon(false);
            break;
        case ID_TRAYMENU_OPTION_QUIT:
            DestroyWindow();
            break;
        case ID_TRAYMENU_OPTION_QUIT_AND_UNREGISTER:
            quitAndUnregister();
            break;
        case ID_TRAYMENU_TOGGLE_MINIMIZE_ALL:
            if(!toggleMinimizeAllWindows()) arrangeAllWindows(true); 
            break;
        default:
            break;
        }

        return 0;
    }

    LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL const& /*bHandled*/)
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
        LoadString(hInstance, (m_bAutoArrange ? IDS_APPTOOLTIP2 : IDS_APPTOOLTIP1), nid.szTip, size(nid.szTip));
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

static int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpCmdLine, int nCmdShow)
{
    if (lpCmdLine == wstring_view(L"--console")) CreateConsole();
    _Module.Init(nullptr, hInstance);

    CMainWnd wnd;
    wnd.Create(nullptr, CWindow::rcDefault, _T("WTL Tray App"), WS_OVERLAPPEDWINDOW, WS_EX_TRANSPARENT);
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

inline void CSettingsDlg::OnHScroll(int nScrollCode, short [[maybe_unused]] nPos, HWND hwndScrollBar)
{
    if (nScrollCode == SB_ENDSCROLL && hwndScrollBar == m_slider.m_hWnd)
    {
        allowedIncrease = m_slider.GetPos();
        pMainWindow->SendMessage(WM_SLIDER_CHANGE, allowedIncrease);
    }
}

inline LRESULT CSettingsDlg::OnCheckBoxClicked(WORD, WORD, HWND__ const*, BOOL const&) 
{
    doubleDistanceAroundTopRightCorner = m_checkBox.GetCheck();
    pMainWindow->SendMessage(WM_CHECKBOX_CHANGE, doubleDistanceAroundTopRightCorner);
    return 0;
}
