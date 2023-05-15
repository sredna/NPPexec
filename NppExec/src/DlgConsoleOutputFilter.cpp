/*
This file is part of NppExec
Copyright (C) 2013 DV <dvv81 (at) ukr (dot) net>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <windows.h>
//#define _WIN32_IE    0x0400
#include <commctrl.h>

#include "DlgConsoleOutputFilter.h"
#include "Resource.h"
#include "NppExec.h"


// for Dev-C++
#ifndef  TCS_HOTTRACK
#define  TCS_HOTTRACK  0x0040
#endif


CConsoleOutputFilterDlg      ConsoleOutputFilterDlg;


static const TCHAR* const cszQuickReference_Mask =
    _T("Quick Reference\r\n") \
    _T("--------------------\r\n") \
    _T("\r\n") \
    _T("You can use wildcard characters in exclude and include mask(s):\r\n") \
    _T(" * (asterisk) matches any 0 or more characters\r\n") \
    _T(" ? (question mark) matches any single character\r\n") \
    _T("\r\n") \
    _T("Example 1.\r\n") \
    _T("Include only those lines which start with \"begin\" and end with \"end\".\r\n") \
    _T("Solution.\r\n") \
    _T("Set include mask to:  begin*end\r\n") \
    _T("\r\n") \
    _T("Example 2.\r\n") \
    _T("Include only those lines which start with \"begin\" and end with \"end\" ") \
    _T("but do not contain \"string\".\r\n") \
    _T("Solution.\r\n") \
    _T("Set include mask to:  begin*end\r\n") \
    _T("Set exclude mask to:  *string*\r\n") \
    _T("");

static const TCHAR* const cszQuickReference_HighLight =
    _T("Use wildcard characters in the edit boxes:\t* (asterisk) matches any 0 or more characters\t\t") \
    _T("\t\t\t\t\t\t\t? (question mark) matches any single character\r\n") \
    _T("You can also use the following Keywords:\t%LINE% or %L%   line in the file to which it shall Jump\r\n") \
    _T("\t\t\t\t\t%CHAR% or %C%   character position in the line to jump\r\n") \
    _T("\t\t\t\t\t%FILE% or %F%   relative or absolute file name (..\\..\\directory\\file.ext)\r\n") \
    _T("\t\t\t\t\t%ABSFILE% or %A%   absolute file name (C:\\directory\\file.ext)\r\n") \
    _T("Example: %ABSFILE%:%LINE%: *error* => detects the error lines generated by gcc") \
    _T("");


static INT_PTR CALLBACK ConsoleOutputFilterProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK ConsoleReplaceFilterProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK ConsoleHighLightFilterProc(HWND, UINT, WPARAM, LPARAM);

static HWND  s_hDlg = NULL;
static HHOOK s_hMsgHook = NULL;

static LRESULT CALLBACK GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
   LPMSG lpMsg = (LPMSG) lParam;

   if ( (nCode >= 0) && (wParam == PM_REMOVE) )
   {
      // Don't translate non-input events.
      if ( (lpMsg->message >= WM_KEYFIRST) && (lpMsg->message <= WM_KEYLAST) )
      {
         if ( IsDialogMessage(s_hDlg, lpMsg) )
         {
            // The value returned from this hookproc is ignored, 
            // and it cannot be used to tell Windows the message has been handled.
            // To avoid further processing, convert the message to WM_NULL 
            // before returning.
            lpMsg->message = WM_NULL;
            lpMsg->lParam  = 0;
            lpMsg->wParam  = 0;
         }
      }
   }

   return CallNextHookEx(s_hMsgHook, nCode, wParam, lParam);
}


INT_PTR CALLBACK ConsoleOutputFilterDlgProc( HWND   hDlg
                                           , UINT   uMessage
                                           , WPARAM wParam
                                           , LPARAM lParam
                                           )
{
    if ( uMessage == WM_COMMAND )
    {
        switch (LOWORD(wParam))
        {
            case IDOK:
            {
                ConsoleOutputFilterDlg.OnBtOK();
                ConsoleOutputFilterDlg.OnEndDialog();
                EndDialog(hDlg, 1);
                return 1;
            }
            case IDCANCEL:
            {
                ConsoleOutputFilterDlg.OnBtCancel();
                ConsoleOutputFilterDlg.OnEndDialog();
                EndDialog(hDlg, 0);
                return 1;
            }
            default:
                break;
        }
    }

    else if ( uMessage == WM_INITDIALOG )
    {
        ConsoleOutputFilterDlg.OnInitDialog(hDlg);
    }

    else if ( uMessage == WM_NOTIFY )
    {
        ConsoleOutputFilterDlg.OnNotify( hDlg, (LPNMHDR) lParam );
    }

    return 0;
}


CConsoleOutputFilterDlg::CConsoleOutputFilterDlg() : CAnyWindow()
{
    m_nLastTab = 2; // Highlight tab
    m_hBrushBkCheckbox = NULL;
}

CConsoleOutputFilterDlg::~CConsoleOutputFilterDlg()
{
}

void updateHistoryContent(unsigned int opt_id, int items, CListT<tstr>& History)
{
    const CStaticOptionsManager& Options = Runtime::GetNppExec().GetOptions();
    int len;

    for ( int i = 0; i < items; i++ )
    {
        const TCHAR* cszText = Options.GetStr(opt_id + i, &len);
        if ( len > 0 )
        {
            if ( History.GetCount() > 0 )
            {
                tstr S = cszText;
                CListItemT<tstr>* p = History.FindExact(S);
                while ( p )
                {
                    History.Delete(p);
                    p = History.FindExact(S);
                }
            }
        }
    }
}

void CConsoleOutputFilterDlg::OnBtOK()
{
    CStaticOptionsManager& Options = Runtime::GetNppExec().GetOptions();
    TCHAR str[OUTPUTFILTER_BUFSIZE];
    
    if ( m_hTabDlg[DLG_FLTR] )
    {
        int mask = 0;
        for (int i = 0; i < FILTER_ITEMS; i++)
        {
            if ( m_ch_Include[i].IsChecked() )  mask |= (0x01 << i);
        }
        Options.SetInt(OPTI_CONFLTR_INCLMASK, mask);

        mask = 0;
        for (int i = 0; i < FILTER_ITEMS; i++)
        {
            if ( m_ch_Exclude[i].IsChecked() )  mask |= (0x01 << i);
        }
        Options.SetInt(OPTI_CONFLTR_EXCLMASK, mask);
        
        Options.SetInt( OPTB_CONFLTR_ENABLE, m_ch_FilterEnable.IsChecked() );
        Options.SetInt( OPTB_CONFLTR_EXCLALLEMPTY, m_ch_ExcludeAllEmpty.IsChecked() );
        Options.SetInt( OPTB_CONFLTR_EXCLDUPEMPTY, m_ch_ExcludeDupEmpty.IsChecked() );

        for (int i = 0; i < FILTER_ITEMS; i++)
        {
            str[0] = 0;
            m_cb_Include[i].GetWindowText(str, OUTPUTFILTER_BUFSIZE - 1);
            Options.SetStr(OPTS_CONFLTR_INCLLINE1 + i, str);
            str[0] = 0;
            m_cb_Exclude[i].GetWindowText(str, OUTPUTFILTER_BUFSIZE - 1);
            Options.SetStr(OPTS_CONFLTR_EXCLLINE1 + i, str);
        }

        updateHistoryContent(OPTS_CONFLTR_EXCLLINE1, FILTER_ITEMS, m_ExcludeHistory);
        updateHistoryContent(OPTS_CONFLTR_INCLLINE1, FILTER_ITEMS, m_IncludeHistory);
    }

    if ( m_hTabDlg[DLG_RPLC] )
    {
        int mask = 0;
        for (int i = 0; i < REPLACE_ITEMS; i++)
        {
            if ( m_ch_RFind[i].IsChecked() )  mask |= (0x01 << i);
        }
        Options.SetInt(OPTI_CONFLTR_R_FINDMASK, mask);

        mask = 0;
        for (int i = 0; i < REPLACE_ITEMS; i++)
        {
            if ( m_ch_RCase[i].IsChecked() )  mask |= (0x01 << i);
        }
        Options.SetInt(OPTI_CONFLTR_R_CASEMASK, mask);
        
        Options.SetInt( OPTB_CONFLTR_R_ENABLE, m_ch_REnable.IsChecked() );
        Options.SetInt( OPTB_CONFLTR_R_EXCLEMPTY, m_ch_RExcludeEmpty.IsChecked() );

        for (int i = 0; i < REPLACE_ITEMS; i++)
        {
            str[0] = 0;
            m_cb_RFind[i].GetWindowText(str, OUTPUTFILTER_BUFSIZE - 1);
            Options.SetStr(OPTS_CONFLTR_R_FIND1 + i, str);
            str[0] = 0;
            m_cb_RRplc[i].GetWindowText(str, OUTPUTFILTER_BUFSIZE - 1);
            Options.SetStr(OPTS_CONFLTR_R_RPLC1 + i, str);
        }

        updateHistoryContent(OPTS_CONFLTR_R_FIND1, REPLACE_ITEMS, m_RFindHistory);
        updateHistoryContent(OPTS_CONFLTR_R_RPLC1, REPLACE_ITEMS, m_RRplcHistory);
    }

    if ( m_hTabDlg[DLG_HGLT] )
    {
        CWarningAnalyzer& WarningAnalyzer = Runtime::GetNppExec().GetWarningAnalyzer();
        CWarningAnalyzer::TEffect Effect;

        for ( int i = 0; i < RECOGNITION_ITEMS; i++ )
        {
            Effect.Enable     = m_ch_Recognition[i].IsChecked() ? true : false;
            Effect.Italic     = m_ch_Recognition_Style[i][FILTER_REC_ITALIC    ].IsChecked() ? true : false;
            Effect.Bold       = m_ch_Recognition_Style[i][FILTER_REC_BOLD      ].IsChecked() ? true : false;
            Effect.Underlined = m_ch_Recognition_Style[i][FILTER_REC_UNDERLINED].IsChecked() ? true : false;

            str[1] = 0;
            m_ed_Recognition_Color[i][FILTER_REC_RED  ].GetWindowText( str, OUTPUTFILTER_BUFSIZE - 1 );
            Effect.Red   = CWarningAnalyzer::xtou( str[0], str[1] );

            str[1] = 0;
            m_ed_Recognition_Color[i][FILTER_REC_GREEN].GetWindowText( str, OUTPUTFILTER_BUFSIZE - 1 );
            Effect.Green = CWarningAnalyzer::xtou( str[0], str[1] );

            str[1] = 0;
            m_ed_Recognition_Color[i][FILTER_REC_BLUE ].GetWindowText( str, OUTPUTFILTER_BUFSIZE - 1 );
            Effect.Blue  = CWarningAnalyzer::xtou( str[0], str[1] );

            m_cb_Recognition[i].GetWindowText(str, OUTPUTFILTER_BUFSIZE - 1);
            WarningAnalyzer.SetMask( i, str );
        
            Options.SetStr(OPTS_CONFLTR_RCGNMSK1 + i, str);
        
            if ( str[0] )
            {
                WarningAnalyzer.SetEffect( i, Effect );
            }
        }

        updateHistoryContent(OPTS_CONFLTR_RCGNMSK1, RECOGNITION_ITEMS, m_HighlightHistory);
    }
    
}

void CConsoleOutputFilterDlg::OnBtCancel()
{
    if ( m_hTabDlg[DLG_FLTR] )
    {
        updateHistoryContent(OPTS_CONFLTR_EXCLLINE1, FILTER_ITEMS, m_ExcludeHistory);
        updateHistoryContent(OPTS_CONFLTR_INCLLINE1, FILTER_ITEMS, m_IncludeHistory);
    }
    
    if ( m_hTabDlg[DLG_HGLT] )
    {
        updateHistoryContent(OPTS_CONFLTR_RCGNMSK1, RECOGNITION_ITEMS, m_HighlightHistory);
    }

    if ( m_hTabDlg[DLG_RPLC] )
    {
        updateHistoryContent(OPTS_CONFLTR_R_FIND1, REPLACE_ITEMS, m_RFindHistory);
        updateHistoryContent(OPTS_CONFLTR_R_RPLC1, REPLACE_ITEMS, m_RRplcHistory);
    }
}

void CConsoleOutputFilterDlg::OnChFilterEnable()
{
    BOOL bFilterEnabled = m_ch_FilterEnable.IsChecked();
    for (int i = 0; i < FILTER_ITEMS; i++)
    {
        m_ch_Include[i].EnableWindow( bFilterEnabled );
        m_cb_Include[i].EnableWindow( bFilterEnabled );
        m_ch_Exclude[i].EnableWindow( bFilterEnabled );
        m_cb_Exclude[i].EnableWindow( bFilterEnabled );
    }
    m_ch_ExcludeAllEmpty.EnableWindow( bFilterEnabled );
    m_ch_ExcludeDupEmpty.EnableWindow( bFilterEnabled );
}

void CConsoleOutputFilterDlg::OnChRplcFilterEnable()
{
    BOOL bFilterEnabled = m_ch_REnable.IsChecked();
    for (int i = 0; i < REPLACE_ITEMS; i++)
    {
        ::EnableWindow( ::GetDlgItem(m_hTabDlg[DLG_RPLC], IDC_ST_RFIND1 + i), bFilterEnabled );
        ::EnableWindow( ::GetDlgItem(m_hTabDlg[DLG_RPLC], IDC_ST_RRPLC1 + i), bFilterEnabled );
        m_ch_RFind[i].EnableWindow( bFilterEnabled );
        m_ch_RCase[i].EnableWindow( bFilterEnabled );
        m_cb_RFind[i].EnableWindow( bFilterEnabled );
        m_cb_RRplc[i].EnableWindow( bFilterEnabled );
    }
    m_ch_RExcludeEmpty.EnableWindow( bFilterEnabled );
}

void updateComboBoxContent(CAnyComboBox cb[], int index, int items,
                           CListT<tstr>& History)
{
    TCHAR szText[OUTPUTFILTER_BUFSIZE];
    TCHAR szTemp[OUTPUTFILTER_BUFSIZE];
    
    if ( index >= 0 )
    {
        int n1, n2;
        int len = cb[index].GetText(szText, OUTPUTFILTER_BUFSIZE-1);

        szText[len] = 0;
        cb[index].GetEditSel(&n1, &n2);
        cb[index].ResetContent();
        cb[index].SetText(szText);
        cb[index].SetEditSel(n1, n2);

        for (int i = 0; i < items; i++)
        {
            if ( cb[i].GetText(szTemp, OUTPUTFILTER_BUFSIZE-1) > 0 )
            {
                if ( cb[index].FindStringExact(szTemp) == CB_ERR )
                {
                    cb[index].AddString(szTemp);
                }
            }
        }
    }
    else
    {
        while ( --items >= 0 )
        {
            if ( cb[items].GetText(szTemp, OUTPUTFILTER_BUFSIZE-1) > 0 )
            {
                tstr S = szTemp;
                CListItemT<tstr>* p = History.FindExact(S);
                while ( p )
                {
                    History.Delete(p);
                    p = History.FindExact(S);
                }
                History.InsertFirst(S);
            }
        }
    }
    
    if ( index >= 0 )
    {
        const CListItemT<tstr>* p = History.GetFirst();
        while ( p )
        {
            if ( cb[index].FindStringExact(p->GetItem().c_str()) == CB_ERR )
            {
                cb[index].AddString( p->GetItem().c_str() );
            }
            p = p->GetNext();
        }

        if ( cb[index].GetCount() <= 0 )
        {
            cb[index].AddString(szText);
        }
        
        int i = cb[index].FindStringExact(szText);
        if ( i != CB_ERR )
        {
            cb[index].SetCurSel(i);
        }
    }
}

void CConsoleOutputFilterDlg::OnCbnDropDown(unsigned int idComboBox)
{
    if ( (idComboBox >= IDC_CB_EXCLUDE1) && 
         (idComboBox < IDC_CB_EXCLUDE1 + FILTER_ITEMS) )
    {
        updateComboBoxContent(m_cb_Exclude, idComboBox - IDC_CB_EXCLUDE1, 
          FILTER_ITEMS, m_ExcludeHistory);
        return;
    }

    if ( (idComboBox >= IDC_CB_INCLUDE1) &&
         (idComboBox < IDC_CB_INCLUDE1 + FILTER_ITEMS) )
    {
        updateComboBoxContent(m_cb_Include, idComboBox - IDC_CB_INCLUDE1, 
          FILTER_ITEMS, m_IncludeHistory);
        return;
    }

    if ( (idComboBox >= IDC_CB_HIGHLIGHT1) &&
         (idComboBox < IDC_CB_HIGHLIGHT1 + RECOGNITION_ITEMS) )
    {
        updateComboBoxContent(m_cb_Recognition, idComboBox - IDC_CB_HIGHLIGHT1,
          RECOGNITION_ITEMS, m_HighlightHistory);
        return;
    }

    if ( (idComboBox >= IDC_CB_RFIND1) &&
         (idComboBox < IDC_CB_RFIND1 + REPLACE_ITEMS) )
    {
        updateComboBoxContent(m_cb_RFind, idComboBox - IDC_CB_RFIND1,
          REPLACE_ITEMS, m_RFindHistory);
        return;
    }

    if ( (idComboBox >= IDC_CB_RRPLC1) &&
         (idComboBox < IDC_CB_RRPLC1 + REPLACE_ITEMS) )
    {
        updateComboBoxContent(m_cb_RRplc, idComboBox - IDC_CB_RRPLC1,
          REPLACE_ITEMS, m_RRplcHistory);
        return;
    }
}

INT_PTR CConsoleOutputFilterDlg::OnCtlColorStatic(HWND hDlg, WPARAM wParam, LPARAM lParam)
{
    if ( hDlg )
    {
        bool isOneOfUnnamedCheckboxes = false;
        HWND hCtrlWnd = (HWND) lParam;

        if ( hDlg == m_hTabDlg[DLG_FLTR] )
        {
            for ( int i = 0; i < FILTER_ITEMS; i++ )
            {
                if ( (hCtrlWnd == m_ch_Include[i].m_hWnd) ||
                     (hCtrlWnd == m_ch_Exclude[i].m_hWnd) )
                {
                    // This is one of our unnamed checkboxes...
                    isOneOfUnnamedCheckboxes = true;
                    break;
                }
            }
        }
        else if ( hDlg == m_hTabDlg[DLG_HGLT] )
        {
            for ( int i = 0; i < RECOGNITION_ITEMS; i++ )
            {
                if ( (hCtrlWnd == m_ch_Recognition[i].m_hWnd) ||
                     (hCtrlWnd == m_ch_Recognition_Style[i][0].m_hWnd) ||
                     (hCtrlWnd == m_ch_Recognition_Style[i][1].m_hWnd) ||
                     (hCtrlWnd == m_ch_Recognition_Style[i][2].m_hWnd) )
                {
                    // This is one of our unnamed checkboxes...
                    isOneOfUnnamedCheckboxes = true;
                    break;
                }
            }
        }

        if ( isOneOfUnnamedCheckboxes )
        {
            if ( hCtrlWnd == ::GetFocus() )
            {
                // The checkbox is focused. 
                // Let's modify its background color...
                ::SetBkColor( (HDC)wParam, RGB_BK_CHECKBOX );
                return (INT_PTR)m_hBrushBkCheckbox;
            }
        }
    }

    return 0;
}

void CConsoleOutputFilterDlg::OnNotify(HWND hDlg, LPNMHDR pnmhdr)
{
    if ( pnmhdr->code == TCN_SELCHANGE )
    {
        for ( int i = 0; i < DLG_COUNT; i++ )
        {
            if ( m_hTabDlg[i] )
                ::ShowWindow( m_hTabDlg[i], SW_HIDE );
        }

        m_nLastTab = TabCtrl_GetCurSel(m_hTabs);
        if ( (m_nLastTab >= DLG_FLTR) && (m_nLastTab <= DLG_HGLT) )
        {
            if ( !m_hTabDlg[m_nLastTab] )
            {
                static const DLGPROC pDlgProc[DLG_COUNT] = {
                    ConsoleOutputFilterProc,
                    ConsoleReplaceFilterProc,
                    ConsoleHighLightFilterProc
                };

                static const unsigned int nDlgId[DLG_COUNT] = {
                    IDD_CONSOLE_OUTPUTFILTER,
                    IDD_CONSOLE_REPLACEFILTER,
                    IDD_CONSOLE_HIGHLIGHTFILTER
                };

                m_hTabDlg[m_nLastTab] = CreateDialog( 
                  (HINSTANCE) Runtime::GetNppExec().m_hDllModule,
                  MAKEINTRESOURCE(nDlgId[m_nLastTab]),
                  hDlg,
                  pDlgProc[m_nLastTab]
                );
            }
            ::ShowWindow( m_hTabDlg[m_nLastTab], SW_SHOW );
            if ( ::GetFocus() != m_hTabs )
                ::SetFocus( m_hTabDlg[m_nLastTab] );
        }
    }
}

void CConsoleOutputFilterDlg::OnInitDialog(HWND hDlg)
{
    TC_ITEM tie;

    // dialog items initialization
    m_hWnd = hDlg;

    m_hBrushBkCheckbox = ::CreateSolidBrush(RGB_BK_CHECKBOX);

    s_hDlg = hDlg;
    s_hMsgHook = ::SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, NULL, GetCurrentThreadId());

    for ( int i = 0; i < DLG_COUNT; i++ )
    {
        m_hTabDlg[i] = NULL;
    }
    
    m_hTabs = CreateWindowEx( 0
                            , WC_TABCONTROL
                            , _T("coucou")
                            , WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | WS_TABSTOP | WS_GROUP | TCS_HOTTRACK
                            , 10, 10, 575, 440
                            , hDlg
                            , NULL
                            , (HINSTANCE)Runtime::GetNppExec().m_hDllModule
                            , NULL
                            );

    if ( m_hTabs )
    {
        HFONT hFont = (HFONT) SendMessage( hDlg, WM_GETFONT, 0, 0 );
        SendMessage( m_hTabs, WM_SETFONT, (WPARAM) hFont, 0 );
    }

    // adding tabs...
    
    tie.mask = TCIF_TEXT;

    tie.pszText = (LPTSTR) _T("Filter");
    TabCtrl_InsertItem(m_hTabs, 1, &tie);

    tie.pszText = (LPTSTR) _T("Replace");
    TabCtrl_InsertItem(m_hTabs, 2, &tie);
    
    tie.pszText = (LPTSTR) _T("HighLight");
    TabCtrl_InsertItem(m_hTabs, 3, &tie);

    // set active tab
    NMHDR nmh;    
    nmh.code = TCN_SELCHANGE;
    nmh.hwndFrom = m_hTabs;
    nmh.idFrom = 0;
    TabCtrl_SetCurSel(m_hTabs, m_nLastTab); // initially: Highlight tab
                                            // (see the constructor)
    SendMsg( WM_NOTIFY, 0, (LPARAM) &nmh );

    // resizing
    HWND hClient = NULL;
    for ( int i = 0; i < DLG_COUNT; i++ )
    {
        if ( m_hTabDlg[i] )
        {
            hClient = m_hTabDlg[i];
            break;
        }
    }
    
    if ( hClient )
    {
        RECT rcClientWnd;

        if ( ::GetWindowRect(hClient, &rcClientWnd) )
        {
            RECT rcWnd;
            RECT rcClnt;
            int  width;
            int  height;

            ::GetWindowRect(m_hTabs, &rcWnd);
            //::GetClientRect(m_hTabs, &rcClnt);
            // GetClientRect for m_hTabs returns the same width & height
            // as GetWindowRect. VERY usefull, isn't it?

            width = (rcClientWnd.right - rcClientWnd.left) + 6;
            height = (rcClientWnd.bottom - rcClientWnd.top) + 29;
            
            ::MoveWindow(m_hTabs, 10, 10, width, height, FALSE);
            if ( ::GetWindowRect(m_hTabs, &rcClientWnd) )
            {
                ::GetWindowRect(hDlg, &rcWnd);
                ::GetClientRect(hDlg, &rcClnt);

                width = (rcClientWnd.right - rcClientWnd.left) + 20 +
                    (rcWnd.right - rcWnd.left) - (rcClnt.right - rcClnt.left);
                height = rcWnd.bottom - rcWnd.top;

                ::MoveWindow(
                    hDlg, 
                    0, 
                    0, 
                    width, 
                    height, 
                    FALSE
                );
            }
        }
    }

    // finally...
    this->CenterWindow(Runtime::GetNppExec().m_nppData._nppHandle);
}

void CConsoleOutputFilterDlg::OnInitDlgFltr(HWND hDlgFltr)
{
    SetWindowPos( hDlgFltr
                , HWND_TOP
                , 12, 35, 0, 0
                , SWP_NOSIZE
                );

    // initialization...
    HWND hQR = ::GetDlgItem(hDlgFltr, IDC_ST_HELP_MASK);
    if ( hQR )
    {
        ::SetWindowText(hQR, cszQuickReference_Mask);
    }

    for ( int i = 0; i < FILTER_ITEMS; i++ )
    {
        m_cb_Include[i].m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CB_INCLUDE1 + i);
        m_ch_Include[i].m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CH_INCLUDE1 + i);
        m_cb_Exclude[i].m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CB_EXCLUDE1 + i);
        m_ch_Exclude[i].m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CH_EXCLUDE1 + i);

        m_cb_Include[i].LimitText(WARN_MASK_SIZE - 24);
        m_cb_Exclude[i].LimitText(WARN_MASK_SIZE - 24);
    }

    m_ch_FilterEnable.m_hWnd    = ::GetDlgItem(hDlgFltr, IDC_CH_FILTER_ENABLE);
    m_ch_ExcludeAllEmpty.m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CH_EXCLUDE_ALLEMPTY);
    m_ch_ExcludeDupEmpty.m_hWnd = ::GetDlgItem(hDlgFltr, IDC_CH_EXCLUDE_DUPEMPTY);

    // settings...
    const CStaticOptionsManager& Options = Runtime::GetNppExec().GetOptions();
    m_ch_FilterEnable.SetCheck(Options.GetBool(OPTB_CONFLTR_ENABLE));
    m_ch_ExcludeAllEmpty.SetCheck(Options.GetBool(OPTB_CONFLTR_EXCLALLEMPTY));
    m_ch_ExcludeDupEmpty.SetCheck(Options.GetBool(OPTB_CONFLTR_EXCLDUPEMPTY));

    for ( int i = 0; i < FILTER_ITEMS; i++ )
    {
        m_ch_Include[i].SetCheck(Options.GetInt(OPTI_CONFLTR_INCLMASK) & (0x01 << i));
        m_ch_Exclude[i].SetCheck(Options.GetInt(OPTI_CONFLTR_EXCLMASK) & (0x01 << i));
        m_cb_Include[i].SetWindowText(Options.GetStr(OPTS_CONFLTR_INCLLINE1 + i));
        m_cb_Exclude[i].SetWindowText(Options.GetStr(OPTS_CONFLTR_EXCLLINE1 + i));
    }

    updateComboBoxContent(m_cb_Exclude, -1, FILTER_ITEMS, m_ExcludeHistory);
    updateComboBoxContent(m_cb_Include, -1, FILTER_ITEMS, m_IncludeHistory);
    
    // finally...
    OnChFilterEnable();
}

void CConsoleOutputFilterDlg::OnInitDlgHglt(HWND hDlgHglt)
{
    SetWindowPos( hDlgHglt
                , HWND_TOP
                , 12, 35, 0, 0
                , SWP_NOSIZE
                );

    // initialization...
    HWND hQR = ::GetDlgItem(hDlgHglt, IDC_ST_HELP_HIGHLIGHT);
    if ( hQR )
    {
        ::SetWindowText(hQR, cszQuickReference_HighLight);
    }

    for ( int i = 0; i < RECOGNITION_ITEMS; i++ )
    {
        m_ch_Recognition[i].m_hWnd          = ::GetDlgItem(hDlgHglt, IDC_CH_HIGHLIGHT1 + i);
        m_cb_Recognition[i].m_hWnd          = ::GetDlgItem(hDlgHglt, IDC_CB_HIGHLIGHT1 + i);
        m_ed_Recognition_Color[i][0].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_ED_HIGHLIGHT_R1 + i);
        m_ed_Recognition_Color[i][1].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_ED_HIGHLIGHT_G1 + i);
        m_ed_Recognition_Color[i][2].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_ED_HIGHLIGHT_B1 + i);
        m_ch_Recognition_Style[i][0].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_CH_HIGHLIGHT_I1 + i);
        m_ch_Recognition_Style[i][1].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_CH_HIGHLIGHT_B1 + i);
        m_ch_Recognition_Style[i][2].m_hWnd = ::GetDlgItem(hDlgHglt, IDC_CH_HIGHLIGHT_U1 + i);

        m_cb_Recognition[i].LimitText(WARN_MASK_SIZE - 24);
    }

    // settings...
    
    CWarningAnalyzer& WarningAnalyzer = Runtime::GetNppExec().GetWarningAnalyzer();
    TCHAR RecMask[OUTPUTFILTER_BUFSIZE];

    for ( int i = 0; i < RECOGNITION_ITEMS; i++ )
    {
        m_cb_Recognition[i].SetWindowText( WarningAnalyzer.GetMask(i, RecMask, OUTPUTFILTER_BUFSIZE-1) );
        if ( RecMask[0] )
        {
            CWarningAnalyzer::TEffect Effect;
            TCHAR col[4];

            WarningAnalyzer.GetEffect( i, Effect );
            m_ch_Recognition[i].SetCheck( Effect.Enable );
            m_ch_Recognition_Style[i][FILTER_REC_ITALIC    ].SetCheck( Effect.Italic     );
            m_ch_Recognition_Style[i][FILTER_REC_BOLD      ].SetCheck( Effect.Bold       );
            m_ch_Recognition_Style[i][FILTER_REC_UNDERLINED].SetCheck( Effect.Underlined );
            m_ed_Recognition_Color[i][FILTER_REC_RED  ].SetWindowText( CWarningAnalyzer::utox( Effect.Red,   col, 3 ) );
            m_ed_Recognition_Color[i][FILTER_REC_GREEN].SetWindowText( CWarningAnalyzer::utox( Effect.Green, col, 3 ) );
            m_ed_Recognition_Color[i][FILTER_REC_BLUE ].SetWindowText( CWarningAnalyzer::utox( Effect.Blue,  col, 3 ) );
        }
    }

    updateComboBoxContent(m_cb_Recognition, -1, RECOGNITION_ITEMS, m_HighlightHistory);
}

void CConsoleOutputFilterDlg::OnInitDlgRplc(HWND hDlgRplc)
{
    SetWindowPos( hDlgRplc
                , HWND_TOP
                , 12, 35, 0, 0
                , SWP_NOSIZE
                );

    // initialization...
    for ( int i = 0; i < REPLACE_ITEMS; i++ )
    {
        m_ch_RFind[i].m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CH_RPLC1 + i);
        m_ch_RCase[i].m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CH_RCASE1 + i);
        m_cb_RFind[i].m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CB_RFIND1 + i);
        m_cb_RRplc[i].m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CB_RRPLC1 + i);

        m_cb_RFind[i].LimitText(WARN_MASK_SIZE - 24);
        m_cb_RRplc[i].LimitText(WARN_MASK_SIZE - 24);
    }

    m_ch_REnable.m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CH_RPLCFILTER_ENABLE);
    m_ch_RExcludeEmpty.m_hWnd = ::GetDlgItem(hDlgRplc, IDC_CH_EXCLUDE_EMPTYRSLT);

    // settings...
    const CStaticOptionsManager& Options = Runtime::GetNppExec().GetOptions();
    m_ch_REnable.SetCheck(Options.GetBool(OPTB_CONFLTR_R_ENABLE));
    m_ch_RExcludeEmpty.SetCheck(Options.GetBool(OPTB_CONFLTR_R_EXCLEMPTY));

    for ( int i = 0; i < REPLACE_ITEMS; i++ )
    {
        m_ch_RFind[i].SetCheck(Options.GetInt(OPTI_CONFLTR_R_FINDMASK) & (0x01 << i));
        m_ch_RCase[i].SetCheck(Options.GetInt(OPTI_CONFLTR_R_CASEMASK) & (0x01 << i));
        m_cb_RFind[i].SetWindowText(Options.GetStr(OPTS_CONFLTR_R_FIND1 + i));
        m_cb_RRplc[i].SetWindowText(Options.GetStr(OPTS_CONFLTR_R_RPLC1 + i));
    }

    updateComboBoxContent(m_cb_RFind, -1, REPLACE_ITEMS, m_RFindHistory);
    updateComboBoxContent(m_cb_RRplc, -1, REPLACE_ITEMS, m_RRplcHistory);

    // finally...
    OnChRplcFilterEnable();
}

void CConsoleOutputFilterDlg::OnEndDialog()
{
    UnhookWindowsHookEx(s_hMsgHook);
    s_hMsgHook = NULL;
    // Don't set s_hDlg to NULL as GetMsgProc may be executing

    for ( int i = 0; i < DLG_COUNT; i++ )
    {
        if ( m_hTabDlg[i] )
        {
            ::DestroyWindow(m_hTabDlg[i]);
            m_hTabDlg[i] = NULL;
        }
    }

    if ( m_hTabs )
    {
        ::DestroyWindow(m_hTabs);
        m_hTabs = NULL;
    }

    if ( m_hBrushBkCheckbox )
    {
        ::DeleteObject(m_hBrushBkCheckbox);
        m_hBrushBkCheckbox = NULL;
    }
}

INT_PTR CALLBACK ConsoleOutputFilterProc(HWND hDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
            ConsoleOutputFilterDlg.OnInitDlgFltr(hDlg);
            return 1;
        
        case WM_COMMAND:
            if ( LOWORD(wParam) == IDC_CH_FILTER_ENABLE )
            {
                if ( HIWORD(wParam) == BN_CLICKED )
                {
                    ConsoleOutputFilterDlg.OnChFilterEnable();
                    return 1;
                }
            }
            if (HIWORD(wParam) == CBN_DROPDOWN)
            {
                ConsoleOutputFilterDlg.OnCbnDropDown(LOWORD(wParam));
            }
            return 0;

        case WM_CTLCOLORSTATIC:
            return ConsoleOutputFilterDlg.OnCtlColorStatic(hDlg, wParam, lParam);

        default:
            return 0;
    }
}

INT_PTR CALLBACK ConsoleReplaceFilterProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch ( uMsg )
    {
        case WM_INITDIALOG:
            ConsoleOutputFilterDlg.OnInitDlgRplc(hDlg);
            return 1;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CH_RPLCFILTER_ENABLE)
            {
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    ConsoleOutputFilterDlg.OnChRplcFilterEnable();
                    return 1;
                }
            }
            if (HIWORD(wParam) == CBN_DROPDOWN)
            {
                ConsoleOutputFilterDlg.OnCbnDropDown(LOWORD(wParam));
            }
            return 0;

        case WM_CTLCOLORSTATIC:
            return ConsoleOutputFilterDlg.OnCtlColorStatic(hDlg, wParam, lParam);

        default:
            return 0;
    }
}

INT_PTR CALLBACK ConsoleHighLightFilterProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
            ConsoleOutputFilterDlg.OnInitDlgHglt(hDlg);
            return 1;

        case WM_COMMAND:    
            if (HIWORD(wParam) == CBN_DROPDOWN)
            {
                ConsoleOutputFilterDlg.OnCbnDropDown(LOWORD(wParam));
            }
            return 0;

        case WM_CTLCOLORSTATIC:
            return ConsoleOutputFilterDlg.OnCtlColorStatic(hDlg, wParam, lParam);

        default:
            return 0;
    }
}
