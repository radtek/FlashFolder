/* This file is part of FlashFolder. 
 * Copyright (C) 2007 zett42 ( zett42 at users.sourceforge.net ) 
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/** \file Main file to compile the "fflib.dll".
 *  This DLL is the work horse of FlashFolder - it contains a hook function
 *  that watches every window activation in the system. If a window is a common
 *  file dialog, FlashFolder subclasses the window procedure.
 */

#include "stdafx.h"

#pragma warning(disable:4786)  // disable STL-template-related warnings
#pragma warning(disable:4995)  // disable strsafe.h warnings

#include "fflib.h"
#include "fflib_exports.h"
#include "resource.h"
#include "FileDlg_base.h"
#include "CmnFileDlgHook.h"
#include "MsoFileDlgHook.h"
#include "CmnFolderDlgHook.h"

using namespace std;

//-----------------------------------------------------------------------------------------
// global variables  
//-----------------------------------------------------------------------------------------

HINSTANCE g_hInstDll = NULL;

HWND g_hFileDialog = NULL;				// handle of file open/save dialog 

auto_ptr<FileDlgHook_base> g_spFileDlgHook;       // ptr to the hook instance
auto_ptr<FileDlgHook_base> g_spOpenWithDlgHook;   // ptr to the hook instance

HWND g_hToolWnd = NULL;					// handle of cool external tool window
WNDPROC g_wndProcToolWindowEditPath = NULL;
HIMAGELIST g_hToolbarImages = NULL;
HFONT g_hStdFont = NULL;
HHOOK g_hGetMessageHook = NULL;
HACCEL g_hAccTable = NULL;

TCHAR g_favIniFilePath[MAX_PATH+1];		// Path to INI-File with favorite folders

RegistryProfile g_profile;   
MemoryProfile g_profileDefaults;

TCHAR g_currentExePath[ MAX_PATH + 1 ] = _T("");
LPCTSTR g_currentExeName = _T("");
TCHAR g_currentExeDir[ MAX_PATH + 1 ] = _T("");

Rect g_toolbarOffset;                    // Toolbar position / width offset to adjust for some XP themes.

bool g_isFileDlgActive = false;
bool g_isToolWndActive = false;
bool g_isToolWndVisible = false;

//--- options read from profile
int g_globalHistoryMaxEntries;


//-----------------------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------------------

void GetSharedData();
HACCEL CreateMyAcceleratorTable();
LPCTSTR GetCommandName( int cmd );


//-----------------------------------------------------------------------------------------
// DLL entry point
//-----------------------------------------------------------------------------------------
BOOL APIENTRY DllMain( HINSTANCE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
    switch( ul_reason_for_call )
    {
		case DLL_PROCESS_ATTACH:
		{
			// Disable unneeded DLL-callbacks (DLL_THREAD_ATTACH).
			::DisableThreadLibraryCalls( hModule );

			g_hInstDll = hModule;

			::GetModuleFileName( NULL, g_currentExePath, MAX_PATH );
			if( LPTSTR p = _tcsrchr( g_currentExePath, _T('\\') ) )
			{
				g_currentExeName = p + 1;
				StringCbCopy( g_currentExeDir, sizeof(g_currentExeDir), g_currentExePath );
				g_currentExeDir[ p - g_currentExePath ] = 0;
			}
			
			DebugOut( _T("[fflib] DLL_PROCESS_ATTACH (pid %08Xh, \"%s\")\n"), 
				::GetCurrentProcessId(), g_currentExePath );
		}
		break;

		case DLL_PROCESS_DETACH:
		{
			DebugOut( _T("[fflib] DLL_PROCESS_DETACH (pid %08Xh)\n"), ::GetCurrentProcessId() );
		}
		break;
    }
    return TRUE;
}

//-----------------------------------------------------------------------------------------

void AdjustToolWindowPos()
{
	//calculates the position + size of the tool window accordingly to the size
	// of the file dialog

	Rect rc;
	::GetWindowRect( g_hFileDialog, &rc );

	Rect rcTool; 
	::GetWindowRect( g_hToolWnd, &rcTool );
	rcTool.left = rc.left + g_toolbarOffset.left;
	rcTool.top = rc.top - rcTool.bottom + rcTool.top + g_toolbarOffset.top;
	rcTool.right = rc.right + g_toolbarOffset.left + g_toolbarOffset.right;
	rcTool.bottom = rc.top + g_toolbarOffset.top;

	::SetWindowPos( g_hToolWnd, NULL, rcTool.left, rcTool.top, rcTool.right - rcTool.left, rcTool.bottom - rcTool.top, 
		            SWP_NOZORDER | SWP_NOACTIVATE );
}

//-----------------------------------------------------------------------------------------

void UpdatePathEdit()
{
    TCHAR folderPath[MAX_PATH + 1] = _T("");
	g_spFileDlgHook->GetFolder( folderPath );
    SetDlgItemText(g_hToolWnd, ID_FF_PATH, folderPath);
}

//-----------------------------------------------------------------------------------------

void AddCurrentFolderToHistory()
{
    TCHAR folderPath[MAX_PATH + 1];
	if( ! g_spFileDlgHook->GetFolder( folderPath ) )
		return;

	HistoryLst globalHistory;
	globalHistory.LoadFromProfile( g_profile, _T("GlobalFolderHistory") );
	globalHistory.SetMaxEntries( g_globalHistoryMaxEntries );
	globalHistory.AddFolder( folderPath );
	globalHistory.SaveToProfile( g_profile, _T("GlobalFolderHistory") );
}

//----------------------------------------------------------------------------------------

void GotoLastDir()
{
	SetForegroundWindow(g_hFileDialog);

	HistoryLst history;

	//load + display the history
	if( ! history.LoadFromProfile( g_profile, _T("GlobalFolderHistory") ) )
		return;

	SetDlgItemText( g_hToolWnd, ID_FF_PATH, history.GetList().front().c_str() );

	tstring path = GetExistingDirOrParent( history.GetList().front().c_str() );
	if( ! path.empty() )
		g_spFileDlgHook->SetFolder( path.c_str() );
}

//-----------------------------------------------------------------------------------------

HMENU CreateFolderMenu( const vector<tstring> &folderList, HMENU hMenu = NULL )
{
    if( ! hMenu ) hMenu = CreatePopupMenu();
	if( hMenu == NULL ) 
		return NULL;

	for( size_t i = 0; i < folderList.size(); i++ )
	{
		if( folderList[ i ] == _T("-") )
			::AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );
		else
	        ::AppendMenu( hMenu, MF_STRING, i + 1, folderList[i].c_str() );
	}
	return hMenu;
}

//-----------------------------------------------------------------------------------------

int DisplayFolderMenu( HMENU hMenu, int buttonID )
{
	HWND hTb = GetDlgItem(g_hToolWnd, ID_FF_TOOLBAR);
	Rect rc;
	SendMessage(hTb, TB_GETRECT, buttonID, (LPARAM) &rc);
	::ClientToScreen( hTb, reinterpret_cast<POINT*>( &rc ) ); 
	::ClientToScreen( hTb, reinterpret_cast<POINT*>( &rc.right ) ); 

	int id = TrackPopupMenu(hMenu, 
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, 
		rc.left, rc.bottom, 0, g_hToolWnd, NULL);

	if( id > 0 )
	{
        // get directory path from menu item text
        TCHAR path[MAX_PATH+1] = _T("");
        MENUITEMINFO mi = { sizeof(mi) };
        mi.fMask = MIIM_STRING;
        mi.dwTypeData = path;
        mi.cch = MAX_PATH;
        ::GetMenuItemInfo( hMenu, id, FALSE, &mi );

		// strip additional text to get the path only
		LPCTSTR pPath = mi.dwTypeData;
		if( pPath[ 0 ] == '[' )
			pPath = _tcsstr( pPath, _T("] ") ) + 2;

		if( IsFilePath( pPath ) )
        {
			tstring existingDir = GetExistingDirOrParent( pPath );
			if( ! existingDir.empty() )
			{ 
				SetDlgItemText( g_hToolWnd, ID_FF_PATH, existingDir.c_str() );
			
				SetForegroundWindow( g_hFileDialog );
				g_spFileDlgHook->SetFolder( existingDir.c_str() );
			}
        }
	}

	return id;
}

//-----------------------------------------------------------------------------------------

void DisplayMenu_GlobalHist()
{
	HistoryLst history;

	//load + display the history
	if (history.LoadFromProfile( g_profile, _T("GlobalFolderHistory") ) )
	{
		HMENU hMenu = CreateFolderMenu(history.GetList());
		DisplayFolderMenu( hMenu, ID_FF_GLOBALHIST );
		DestroyMenu(hMenu);
	}
		
	SetForegroundWindow(g_hFileDialog);
}

//-------------------------------------------------------------------------------------------------
/// Show a menu with the current folders of the application and other file managers.

void DisplayMenu_OpenDirs()
{
	HMENU hMenu = ::CreatePopupMenu();

    //--- get current Total Commander folders (if any) and add them to the menu

	CTotalCmdUtils tcmdUtils( FindTopTcWnd() );
    if( tcmdUtils.GetTCmdWnd() )
    {
        TCHAR leftDir[MAX_PATH+1] = _T("");
        TCHAR rightDir[MAX_PATH+1] = _T("");
        if( tcmdUtils.GetDirs( leftDir, MAX_PATH, rightDir, MAX_PATH ) )
        {
			if( ! leftDir[ 0 ] == 0 )
			{
				TCHAR s1[ MAX_PATH + 20 ] = _T("[TC] ");
				StringCbCat( s1, sizeof(s1), leftDir );
				::AppendMenu( hMenu, MF_STRING, 2000, s1 );
			}
			if( ! rightDir[ 0 ] == 0 )
			{
				TCHAR s2[ MAX_PATH + 20 ] = _T("[TC] ");
				StringCbCat( s2, sizeof(s2), rightDir );
				::AppendMenu( hMenu, MF_STRING, 2001, s2 );
			}
			::AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );
        }
    }

	//--- get current Windows Explorer folders (if any) and add them to the menu

	std::vector<tstring> explorerPathes;
	if( GetAllExplorerPathes( &explorerPathes ) > 0 )
	{
		for( int i = 0; i != explorerPathes.size(); ++i )
		{
			TCHAR s[ MAX_PATH + 20 ] = _T("[Explorer] ");
			StringCbCat( s, sizeof(s), explorerPathes[ i ].c_str() );
			::AppendMenu( hMenu, MF_STRING, 2100 + i, s );
		}
		::AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );
	}
	
    //--- add application dir to the menu

	TCHAR exeDir[ MAX_PATH + 20 ] = _T("[Application] ");
	StringCbCat( exeDir, sizeof(exeDir), g_currentExeDir );
	::AppendMenu( hMenu, MF_STRING, 1, exeDir );
    
    //--- 
    
	DisplayFolderMenu( hMenu, ID_FF_OPENDIRS );
    
	DestroyMenu( hMenu );
		
	SetForegroundWindow(g_hFileDialog);
}

//-----------------------------------------------------------------------------------------
// display favorite folders menu

void FavMenu_Create( HMENU hMenu, const FavoritesList& favs, size_t& iItem )
{
	while( iItem < favs.size() )
	{
		const FavoritesList::value_type& fav = favs[ iItem ];
		
		if( fav.title == _T("--") )
		{
			// end of submenu

			++iItem;
			return;
		}
		else if( fav.title.size() > 1 && fav.title.substr( 0, 1 ) == _T("-") )
		{
			// insert submenu recursively

			HMENU hSubMenu = ::CreatePopupMenu();
			::AppendMenu( hMenu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>( hSubMenu ),
				fav.title.substr( 1 ).c_str() );

			++iItem;

			FavMenu_Create( hSubMenu, favs, iItem );			
		}
		else if( fav.title == _T("-") )
		{
			// insert divider
			
			::AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );

			++iItem;
		}
		else
		{
			// insert normal item

			::AppendMenu( hMenu, MF_STRING, iItem + 1, fav.title.c_str() );

			++iItem;
		}
	}
}

//-----------------------------------------------------------------------------------------------

int FavMenu_Display( HWND hWndParent, int x, int y, const FavoritesList& favs )
{
    HMENU hMenu = ::CreatePopupMenu();
	size_t iItem = 0;
	FavMenu_Create( hMenu, favs, iItem );

	if( ! favs.empty() )
		::AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );

	::AppendMenu( hMenu, MF_STRING, 1000, _T("&Add current folder") );
	::AppendMenu( hMenu, MF_STRING, 1001, _T("&Configure...") );

	int id = ::TrackPopupMenu( hMenu, TPM_RETURNCMD | TPM_NONOTIFY, 
		x, y, 0, hWndParent, NULL );

	::DestroyMenu( hMenu );

    return id;	
}

//-----------------------------------------------------------------------------------------------
/// Get path of "FFConfig.exe" - prefer x64 version, if available. 

void GetConfigProcessPath( LPWSTR path, DWORD nChars )
{
	WCHAR dir[ 4096 ] = L"";
	GetAppDir( g_hInstDll, dir, _countof( dir ) );
	wcscpy_s( path, nChars, dir );
	wcscat_s( path, nChars, L"FFConfig64.exe" );
	if( FileExists( path ) )
		return;
	wcscpy_s( path, nChars, dir );
	wcscat_s( path, nChars, L"FFConfig.exe" );	
}

//-----------------------------------------------------------------------------------------------

void FavMenu_StartEditor( HWND hWndParent )
{
	TCHAR path[ 4096 ] = L"";
	GetConfigProcessPath( path, _countof( path ) );

	TCHAR params[ 256 ] = _T("");
	StringCbPrintf( params, sizeof(params),	_T("%d --fav"), hWndParent ); 

	::ShellExecute( hWndParent, _T("open"), path, params, NULL, SW_SHOW );
}

//-----------------------------------------------------------------------------------------------

void FavMenu_AddDir( HWND hWndParent, FavoritesList& favs, LPCTSTR pPath, LPCTSTR pTargetPath = _T("") )
{
	TCHAR path[ 4096 ] = L"";
	GetConfigProcessPath( path, _countof( path ) );

	TCHAR params[ 4096 ] = _T("");
	StringCbPrintf( params, sizeof(params),	_T("%d --addfav \"%s\""), hWndParent, 
		RemoveBackslash( pPath ).c_str() );
	if( pTargetPath[ 0 ] != 0 )
	{	
		StringCbCat( params, sizeof(params), _T(" \"") );
		StringCbCat( params, sizeof(params), RemoveBackslash( pTargetPath ).c_str() );
		StringCbCat( params, sizeof(params), _T("\"") );
	}
	::ShellExecute( hWndParent, _T("open"), path, params, NULL, SW_SHOW );
}

//-----------------------------------------------------------------------------------------------

void FavMenu_DisplayForFileDialog()
{
	//--- show menu

	FavoritesList favs;
	GetDirFavorites( &favs );

	HWND hTb = GetDlgItem( g_hToolWnd, ID_FF_TOOLBAR );
	Rect rc;
	::SendMessage( hTb, TB_GETRECT, ID_FF_FAVORITES, reinterpret_cast<LPARAM>( &rc ) );
	ClientToScreenRect( hTb, &rc );

	int id = FavMenu_Display( g_hToolWnd, rc.left, rc.bottom, favs );

	//--- execute selected command

	if( id == 1000 )
	{
		TCHAR path[ MAX_PATH + 1 ];
		if( g_spFileDlgHook->GetFolder( path ) )
			FavMenu_AddDir( g_hFileDialog, favs, path );
	}
	else if( id == 1001 )
	{
		FavMenu_StartEditor( g_hFileDialog );
	}
	else if( id > 0 )
	{
		//--- execute favorites menu item

		const FavoritesItem& fav = favs[ id - 1 ];

		tstring path;
		tstring token, args;
		SplitTcCommand( fav.command.c_str(), &token, &args );

		if( _tcsicmp( token.c_str(), _T("cd") ) == 0 )
			path = args;
		else if( IsFilePath( fav.command.c_str() ) )
			path = fav.command;

		path = GetExistingDirOrParent( path.c_str() );
		if( ! path.empty() )
			if( DirectoryExists( path.c_str() ) )
			{
				SetDlgItemText( g_hToolWnd, ID_FF_PATH, path.c_str() );
			
				SetForegroundWindow( g_hFileDialog );
				g_spFileDlgHook->SetFolder( path.c_str() );
			}
	}		

	SetForegroundWindow( g_hFileDialog );
}

//-----------------------------------------------------------------------------------------------

void DisplayMenu_Config()
{
	const int buttonId = ID_FF_CONFIG;

	// get menu position
	HWND hTb = ::GetDlgItem( g_hToolWnd, ID_FF_TOOLBAR );
	Rect rc;
	::SendMessage( hTb, TB_GETRECT, buttonId, (LPARAM) &rc );
	::ClientToScreen( hTb, reinterpret_cast<POINT*>( &rc ) ); 
	::ClientToScreen( hTb, reinterpret_cast<POINT*>( &rc.right ) ); 

	HMENU hMenu = ::CreatePopupMenu();
	::AppendMenu( hMenu, MF_STRING, 1, _T("Options...") );
	::AppendMenu( hMenu, MF_STRING, 2, _T("Check for updates") );
	::AppendMenu( hMenu, MF_STRING, 3, _T("About FlashFolder...") );

	int id = TrackPopupMenu(hMenu, 
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, 
		rc.left, rc.bottom, 0, g_hToolWnd, NULL);

	TCHAR path[ 4096 ] = L"";
	GetConfigProcessPath( path, _countof( path ) );
	
	TCHAR params[ 256 ] = _T("");
	StringCbPrintf( params, sizeof(params), _T("%d"), g_hFileDialog ); 
	
	if( id == 1 )
	{
		::ShellExecute( g_hFileDialog, _T("open"), path, params, NULL, SW_SHOW );
	}
	else if( id == 2 )
	{
		StringCbCat( params, sizeof(params), _T(" --updatecheck") );
		::ShellExecute( g_hFileDialog, _T("open"), path, params, NULL, SW_SHOW );	
	}
	else if( id == 3 )
	{
		StringCbCat( params, sizeof(params), _T(" --about") );
		::ShellExecute( g_hFileDialog, _T("open"), path, params, NULL, SW_SHOW );
	}

	::SetForegroundWindow( g_hFileDialog );
}

//-----------------------------------------------------------------------------------------

LRESULT CALLBACK ToolWindowEditPathProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//window proc for the path edit ctrl of the tool window

	switch( uMsg )
	{
		case WM_KEYDOWN:
			if (wParam == VK_RETURN)
			{
				TCHAR path[MAX_PATH + 1] = _T("");
				GetWindowText(hwnd, path, MAX_PATH );
				if( DirectoryExists( path ) )
				{
					SetForegroundWindow(g_hFileDialog);
					g_spFileDlgHook->SetFolder( path );
				}
			}
			else if (wParam == VK_ESCAPE) 
				PostMessage(g_hFileDialog, WM_CLOSE, 0, 0);
			break;

		case WM_LBUTTONDBLCLK:
			//select all on dbl-click / select & copy on ctrl + dbl-click
			if (wParam == MK_LBUTTON)
			{
				SendMessage(hwnd, EM_SETSEL, 0, -1);
				return 0;
			}
			else if (wParam & MK_CONTROL)
			{
				SendMessage(hwnd, EM_SETSEL, 0, -1);
				SendMessage(hwnd, WM_COPY, 0, 0);		
				return 0;
			}
			break;
			
		case WM_PAINT:
		{
			if( IsCompositionSupportedAndActive() && ::GetFocus() != hwnd )
			{
				// Paint only background of parent (glowing text) when not editing.								
				PaintDC dc( hwnd );
				if( dc )
				{
					Rect rcClient; ::GetClientRect( hwnd, &rcClient );
					PaintBuf buf( rcClient );
					::DrawThemeParentBackground( hwnd, buf, &dc.PS().rcPaint );
					::BitBlt( dc, dc.PS().rcPaint.left, dc.PS().rcPaint.top, buf.GetWidth(), buf.GetHeight(), 
					          buf, dc.PS().rcPaint.left, dc.PS().rcPaint.top, SRCCOPY );
				}		
				return 0;
			}
		
			break;
		}
		
		case WM_SETFOCUS:
			// redraw parent background to remove glowing text
			::InvalidateRect( g_hToolWnd, NULL, FALSE );
			break;

		case WM_KILLFOCUS:
			// redraw parent background to show glowing text
			::InvalidateRect( g_hToolWnd, NULL, FALSE );
			break;

		case WM_SETTEXT:
			// redraw parent background to update glowing text
			if( ::GetFocus() != hwnd )
			{
				LRESULT res = CallWindowProc( g_wndProcToolWindowEditPath, hwnd, uMsg, wParam, lParam );
				::InvalidateRect( g_hToolWnd, NULL, FALSE );
				return res;
			}
			break;
	}

	//call original message handler
    return CallWindowProc( g_wndProcToolWindowEditPath, hwnd, uMsg, wParam, lParam );
}

//-----------------------------------------------------------------------------------------------

struct PressTbButton
{
	PressTbButton( UINT cmd ) : m_cmd( cmd ) 
	{
		::SendDlgItemMessage( g_hToolWnd, ID_FF_TOOLBAR, TB_PRESSBUTTON, m_cmd, TRUE );
	} 
	~PressTbButton()
	{
		::SendDlgItemMessage( g_hToolWnd, ID_FF_TOOLBAR, TB_PRESSBUTTON, m_cmd, FALSE );
	}
	
	UINT m_cmd;
};

//----------------------------------------------------------------------------------------------------

void ExecuteToolbarCommand( UINT cmd )
{
	switch( cmd )
	{
		case ID_FF_LASTDIR:
			GotoLastDir();
			break;
		case ID_FF_SHOWALL:
			g_spFileDlgHook->SetFilter( _T("*.*") );
			break;
		case ID_FF_FOCUSPATH:
		{
			::SetForegroundWindow( g_hToolWnd );
			HWND hEdit = ::GetDlgItem( g_hToolWnd, ID_FF_PATH );
			::SendMessage( g_hToolWnd, WM_NEXTDLGCTL, (WPARAM) hEdit, TRUE ); 
			break;
		}
		case ID_FF_GLOBALHIST:
		{
			PressTbButton ptb( ID_FF_GLOBALHIST );				
			DisplayMenu_GlobalHist();
			break;
		}
		case ID_FF_OPENDIRS:
		{
			PressTbButton ptb( ID_FF_OPENDIRS );				
			DisplayMenu_OpenDirs();
			break;
		}
		case ID_FF_FAVORITES:
		{
			PressTbButton ptb( ID_FF_FAVORITES );		
			FavMenu_DisplayForFileDialog();
			break;
		}
		case ID_FF_CONFIG:
		{
			PressTbButton ptb( ID_FF_CONFIG );
			DisplayMenu_Config();
			break;
		}
		default:
			DebugOut( _T("[fflib] ERROR: invalid command") );
	}	
}

//-----------------------------------------------------------------------------------------

LRESULT CALLBACK GetMessageProc( int code, WPARAM wParam, LPARAM lParam )
{
	if( code == HC_ACTION && wParam == PM_REMOVE && g_hAccTable )
	{
		MSG* msg = reinterpret_cast<MSG*>( lParam );

		if( ::TranslateAccelerator( g_hToolWnd, g_hAccTable, msg ) )
		{
			// prevent further message processing
			msg->message = WM_NULL;
			msg->hwnd = NULL;
			msg->wParam = msg->lParam = 0;
		}
	}

	return ::CallNextHookEx( NULL, code, wParam, lParam );
}

//-----------------------------------------------------------------------------------------
// Window proc of the FlashFolder toolbar window

INT_PTR CALLBACK ToolDlgProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	static bool s_inMenu = false;

    switch( uMsg )
    {
		case WM_INITDIALOG:
		{
			if( IsCompositionSupportedAndActive() )
			{
				// make full glass window
				MARGINS glassMargins = { -1, -1, -1, -1 };
				::DwmExtendFrameIntoClientArea( hwnd, &glassMargins );

				// Inform the window of the frame change made by WM_NCCALCSIZE.
				::SetWindowPos( hwnd, NULL, 0, 0, 0, 0, 
					SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
			}

			g_hAccTable = CreateMyAcceleratorTable();

			// Register local hook for processing keyboard accelerators
			g_hGetMessageHook = ::SetWindowsHookEx( WH_GETMESSAGE, GetMessageProc, NULL, ::GetCurrentThreadId() );

			// no default focus wanted
			return FALSE;
		}
		break;

		case WM_NCDESTROY:
		{
			if( g_hGetMessageHook )
			{
				::UnhookWindowsHookEx( g_hGetMessageHook );
				g_hGetMessageHook = NULL;
			}
			if( g_hAccTable )
			{
				::DestroyAcceleratorTable( g_hAccTable );
				g_hAccTable = NULL;
			}
		}
		break;
    
		case WM_COMMAND:
		{
			WORD wNotifyCode = HIWORD( wParam );             // notification code 
			WORD wID = LOWORD( wParam );                     // item, control, or accelerator identifier 
			HWND hwndCtl = reinterpret_cast<HWND>( lParam );      // handle of control 

			DebugOut( L"WM_COMMAND" );

			if( wNotifyCode == BN_CLICKED || wNotifyCode == 1 )
			{
				ExecuteToolbarCommand( wID );
			}
		}
		break;

		case WM_NOTIFY:
		{
			NMHDR* pnm = reinterpret_cast<NMHDR*>( lParam );
			switch( pnm->code )
			{
				case TBN_DROPDOWN:
				{
					// drop-down button pressed

					NMTOOLBAR* pnmt = reinterpret_cast<NMTOOLBAR*>( pnm );
					ExecuteToolbarCommand( pnmt->iItem );

					::SetWindowLongPtr( hwnd, DWLP_MSGRESULT, TBDDRET_DEFAULT );
					return TRUE;
				}
				
				case TTN_NEEDTEXT:
				{
					// tooltip text requested
				
					NMTTDISPINFO* pTTT = reinterpret_cast<NMTTDISPINFO*>( pnm );

					pTTT->hinst = NULL;

					const int TOOLTIP_BUFSIZE = MAX_PATH + 64;
					static TCHAR s_tooltipBuf[ TOOLTIP_BUFSIZE + 1 ];
					s_tooltipBuf[ 0 ] = 0;
					pTTT->lpszText = s_tooltipBuf;
				
					if( pTTT->hdr.idFrom == ID_FF_LASTDIR )
					{
						// use most recent entry of global history as tooltip
						tstring sLastDir = g_profile.GetString( _T("GlobalFolderHistory"), _T("0") );
						if( sLastDir.empty() )
							::LoadString( g_hInstDll, (UINT) pTTT->hdr.idFrom, s_tooltipBuf, TOOLTIP_BUFSIZE );
						else
							StringCbCopy( s_tooltipBuf, sizeof(s_tooltipBuf), sLastDir.c_str() );
					}
					else
					{
						::LoadString( g_hInstDll, (UINT) pTTT->hdr.idFrom, s_tooltipBuf, TOOLTIP_BUFSIZE );
					}

					// append hotkey name
					if( int hotkey = g_profile.GetInt( _T("Hotkeys"), GetCommandName( (int) pTTT->hdr.idFrom ) ) )
					{
						StringCchCat( s_tooltipBuf, TOOLTIP_BUFSIZE, _T("\nShortcut: ") ); 					
						TCHAR hkName[ 256 ]; GetHotkeyName( hkName, 255, hotkey );
						StringCchCat( s_tooltipBuf, TOOLTIP_BUFSIZE, hkName ); 					
					}
					
					return TRUE;
				}

				case NM_CUSTOMDRAW:
				{
					LPNMTBCUSTOMDRAW pcd = reinterpret_cast<LPNMTBCUSTOMDRAW>( lParam );

					if( IsCompositionSupportedAndActive() )
					{
						HWND hTb = ::GetDlgItem( hwnd, ID_FF_TOOLBAR );
						if( pcd->nmcd.hdr.hwndFrom == hTb && pcd->nmcd.dwDrawStage == CDDS_PREERASE )
						{
							// make toolbar background transparent on aero glass
							::FillRect( pcd->nmcd.hdc, GetClientRect( hTb ), GetStockBrush( BLACK_BRUSH ) );
							::SetWindowLongPtr( hwnd, DWLP_MSGRESULT, CDRF_SKIPDEFAULT );
							return TRUE;								
						}
					}				
				}
			}
		}
		break;

		case WM_HOTKEY:
		{
			// hotkeys make only sense if the hooked dialog is the foreground window
			HWND hwndFg = ::GetForegroundWindow();
			if( hwndFg != hwnd && hwndFg != g_hFileDialog )
				break;

			ATOM hotkeyAtom = static_cast<ATOM>( wParam );
			TCHAR atomName[ 256 ] = _T("");
			::GlobalGetAtomName( hotkeyAtom, atomName, 255 );

			tstring ffGuid = tstring( _T(".") ) + tstring( FF_GUID );

			HWND hTb = ::GetDlgItem( hwnd, ID_FF_TOOLBAR );
			
			UINT cmd = 0;
			const UINT IS_MENU = 0x10000;

			if( tstring( _T("ff_LastFolder") ) + ffGuid == atomName )
				cmd = ID_FF_LASTDIR;
			else if( tstring( _T("ff_ViewAllFiles") ) + ffGuid == atomName )
				cmd = ID_FF_SHOWALL;
			else if( tstring( _T("ff_FocusPathEdit") ) + ffGuid == atomName )
				cmd = ID_FF_FOCUSPATH;
			else if( tstring( _T("ff_MenuFolderHistory") ) + ffGuid == atomName )
				cmd = ID_FF_GLOBALHIST | IS_MENU;
			else if( tstring( _T("ff_MenuOpenFolders") ) + ffGuid == atomName )
				cmd = ID_FF_OPENDIRS   | IS_MENU;
			else if( tstring( _T("ff_MenuFavorites") ) + ffGuid == atomName )
				cmd = ID_FF_FAVORITES  | IS_MENU;

			if( s_inMenu )
			{
				// cancel currently open menu
				::SendMessage( hwnd, WM_CANCELMODE, 0, 0 );
				return FALSE;
			}

			if( cmd & IS_MENU )
				::SendMessage( hTb, TB_PRESSBUTTON, cmd & 0xFFFF, TRUE );

			s_inMenu = true;
			ExecuteToolbarCommand( cmd & 0xFFFF );
			s_inMenu = false;

			if( cmd & IS_MENU )
				::SendMessage( hTb, TB_PRESSBUTTON, cmd & 0xFFFF, FALSE );
		}
		break;

		case WM_WINDOWPOSCHANGING:
		{
			// Make sure the toolbar window is initially shown hidden, since dialog manager always
			// calls ShowWindow( SW_SHOW ).
			WINDOWPOS* wp = (WINDOWPOS*) lParam;
		    if( ! g_isToolWndVisible )
				wp->flags &= ~SWP_SHOWWINDOW;
		}
		break;

		case WM_WINDOWPOSCHANGED:
		{
			//resize path edit control
			
			WINDOWPOS *wp = reinterpret_cast<WINDOWPOS*>( lParam );
			
			Rect rcClient; ::GetClientRect( hwnd, &rcClient );

			HWND hPath = GetDlgItem( hwnd, ID_FF_PATH );
			Rect rcPath = GetChildRect( hwnd, hPath ); 

			Rect rcDivR( 0, 0, 4, 1 );
			::MapDialogRect( hwnd, &rcDivR ); 

			::SetWindowPos( hPath, NULL, 0, 0, rcClient.right - rcPath.left - rcDivR.right, rcPath.bottom - rcPath.top, 
				SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE );

			// Update glowing path text
			if( IsCompositionSupportedAndActive() && GetFocus() != hPath )
				InvalidateRect( hwnd, NULL, FALSE );
		}
		break;

		case WM_CLOSE:
			::PostMessage( g_hFileDialog, WM_CLOSE, 0, 0 );
		break;

		case WM_TIMER:
			g_spFileDlgHook->OnTimer();
		break;
	
		case WM_NCHITTEST:
		{
			// Make sure the window cannot be resized since we have the resize border
			// solely to match the style of the file dialog.
			LRESULT ht = ::DefWindowProc( hwnd, WM_NCHITTEST, wParam, lParam );
			
			if( ht != HTNOWHERE && ht != HTTRANSPARENT )
				ht = HTCLIENT;
				
			::SetWindowLongPtr( hwnd, DWLP_MSGRESULT, ht );
			return TRUE;								
		}
		break;

		case WM_ERASEBKGND:
		{
			::SetWindowLongPtr( hwnd, DWLP_MSGRESULT, TRUE );
			return TRUE;
		}
		break;
		
		case WM_PAINT:
		{
			PaintDC dc( hwnd );
			ToolDlgProc( hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>( static_cast<HDC>( dc ) ), PRF_CLIENT );
			return TRUE;
		}
		break;
		
		// Processing this message helps with some transparency issues when putting controls on aero glass.
		case WM_PRINTCLIENT:
		{
			HDC dc = reinterpret_cast<HDC>( wParam );
			Rect rcClient = GetClientRect( hwnd );
			
			if( IsCompositionSupportedAndActive() )
			{
				HWND hEdit = ::GetDlgItem( hwnd, ID_FF_PATH );
				if( ::GetFocus() != hEdit )
				{
					// Draw glowing text for background of edit control when it is not focused.
					// So the text looks good on aero glass but still has enough contrast on dark background.
					
					Theme theme( hwnd, L"CompositedWindow::Window" );

					PaintBuf buf( rcClient );

					HFONT font = reinterpret_cast<HFONT>( ::SendMessage( hEdit, WM_GETFONT, 0, 0 ) );
					AutoSelectObj selFont( buf, font );

					WCHAR text[ MAX_PATH ] = L"";
					::GetWindowText( hEdit, text, _countof( text ) );

					Rect rcText = GetChildRect( hwnd, hEdit );
					rcText.left += static_cast<int>( ::SendMessage( hEdit, EM_GETMARGINS, 0, 0 ) & 0xFFFF );

					DTTOPTS opt = { sizeof( opt ), DTT_COMPOSITED | DTT_GLOWSIZE };
					opt.iGlowSize = 12;
					::DrawThemeTextEx( theme, buf, WP_CAPTION, CS_ACTIVE, text, -1, DT_NOPREFIX | DT_PATH_ELLIPSIS, &rcText, &opt );

					::BitBlt( dc, 0, 0, rcClient.right, rcClient.bottom, buf, 0, 0, SRCCOPY );
				}
				else
				{
					::FillRect( dc, rcClient, GetStockBrush( BLACK_BRUSH ) );
				}
			}
			else
			{
				::FillRect( dc, rcClient, ::GetSysColorBrush( COLOR_BTNFACE ) );
			}
			
			return TRUE;
		}		
		break;
		
		case WM_NCACTIVATE:
		{
			// Keep the toolbar always in the inactive visual state, which looks better.
			if( wParam )
				return TRUE;
		}	
		break;
		
		case WM_NCCALCSIZE:
		{
			// Extend client area into whole glass window (removing non-client area).
			if( IsCompositionSupportedAndActive() && wParam != FALSE )
			{
				::SetWindowLongPtr( hwnd, DWLP_MSGRESULT, 0 );
				return TRUE;
			}
		}		
		break;
	}

	return FALSE; 
}

//-----------------------------------------------------------------------------------------

BOOL CALLBACK ToolWndSetFont(HWND hwnd, LPARAM lParam)
{
	SendMessage(hwnd, WM_SETFONT, lParam, 0);
	return TRUE;
}

//-----------------------------------------------------------------------------------------

void CreateToolWindow( bool isFileDialog )
{
	bool isThemed = IsThemeSupportedAndActive();
	bool isComposited = IsCompositionSupportedAndActive();

	//--- create the external tool window ---
	{
		g_isToolWndVisible = false;

		// Register unique class name so FF can be identified by other tools.
		WNDCLASS wc = { 0 };
		wc.lpszClassName = FF_WNDCLASSNAME;
		wc.hInstance = g_hInstDll;
		wc.hCursor = ::LoadCursor( NULL, IDC_ARROW );
		wc.lpfnWndProc = DefDlgProc;
		wc.cbWndExtra = DLGWINDOWEXTRA;
		::RegisterClass( &wc );

		g_hToolWnd = ::CreateDialog( g_hInstDll, MAKEINTRESOURCE( IDD_TOOLWND ), g_hFileDialog, ToolDlgProc );
		if( g_hToolWnd == NULL )
			return;

		int height = 0;
		DWORD wndPosFlags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;

		if( isComposited )
		{
			height = MapDialogY( g_hToolWnd, 20 );			
		}
		else
		{
			LONG_PTR style = ::GetWindowLongPtr( g_hToolWnd, GWL_STYLE );
			::SetWindowLongPtr( g_hToolWnd, GWL_STYLE, style & ~( WS_CAPTION | DS_MODALFRAME ) );
			wndPosFlags |= SWP_FRAMECHANGED;

			height = MapDialogY( g_hToolWnd, 18 );
		}

		::SetWindowPos( g_hToolWnd, NULL, 0, 0, 0, height, wndPosFlags );			
		AdjustToolWindowPos();
	}

	Rect rcClient = GetClientRect( g_hToolWnd );

	//--- create the toolbar ---

	HWND hTb = NULL;
	Rect tbRect;
	{
		vector<TBBUTTON> tbButtons;
		{
			TBBUTTON btn = { 5, ID_FF_LASTDIR,                 0, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}
		{
			TBBUTTON btn = { 0, ID_FF_GLOBALHIST,              0, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}
		{
			TBBUTTON btn = { 2, ID_FF_OPENDIRS,  TBSTATE_ENABLED, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}
		{
			TBBUTTON btn = { 3, ID_FF_FAVORITES, TBSTATE_ENABLED, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}	  
		if( isFileDialog )
		{
			TBBUTTON btn = { 4, ID_FF_SHOWALL,   TBSTATE_ENABLED, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}
		{
			TBBUTTON btn = { 6, ID_FF_CONFIG,    TBSTATE_ENABLED, BTNS_BUTTON, 0, 0, 0, 0 };
			tbButtons.push_back( btn );
		}

		// Check whether the 32 bpp version of the toolbar bitmap is supported. 
		// For this, OS must be >= WinXP and display mode >= 16 bpp.
		bool isToolbar32bpp = false;
		if( OSVERSION >= WINVER_XP )
		{
			DC dc( ::CreateIC( _T("DISPLAY"), NULL, NULL, NULL ) );
			isToolbar32bpp = ::GetDeviceCaps( dc, BITSPIXEL ) >= 16;
		}
		
		UINT tbStyle = WS_CHILD | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT |
			CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN | TBSTYLE_TOOLTIPS |
			( isComposited ? TBSTYLE_CUSTOMERASE : 0 );

		hTb = ::CreateToolbarEx( g_hToolWnd, tbStyle,
			ID_FF_TOOLBAR, (int) tbButtons.size(), 
			isToolbar32bpp ? NULL : g_hInstDll, isToolbar32bpp ? 0 : ID_FF_TOOLBAR, 
			&tbButtons[ 0 ], (int) tbButtons.size(), 16,16, 16,16, sizeof(TBBUTTON) );

		::SendMessage( hTb, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS ); // | TBSTYLE_EX_DOUBLEBUFFER );
		::ShowWindow( hTb, SW_SHOW );

		if( isToolbar32bpp )
		{
			g_hToolbarImages = ::ImageList_LoadImage( g_hInstDll, MAKEINTRESOURCE( ID_FF_TOOLBAR_XP ), 
				16, 1, CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION );
			::SendMessage( hTb, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>( g_hToolbarImages ) );
		}

		// calculate width of the toolbar from position of last button
		::SendMessage( hTb, TB_AUTOSIZE, 0, 0 );
		::SendMessage( hTb, TB_GETRECT, tbButtons.back().idCommand, reinterpret_cast<LPARAM>( &tbRect ) );

		tbRect.top = ( rcClient.bottom - tbRect.bottom ) / 2;
		tbRect.left = tbRect.top;
		tbRect.bottom += tbRect.top;
		tbRect.right += tbRect.left;
				
		SetWindowPos( hTb, NULL, tbRect.left, tbRect.top, tbRect.Width(), tbRect.Height(), SWP_NOZORDER | SWP_NOACTIVATE );
	}

	//--- create + sub-class the edit control 
	{
		Rect rcDiv( 0, 0, 6, 1 ); 
		Rect rcDivR( 0, 0, 4, 1 ); 
		// use themed border if possible
		DWORD edStyleEx = isThemed ? WS_EX_CLIENTEDGE : WS_EX_STATICEDGE;
		if( OSVERSION >= WINVER_VISTA && isThemed )
		{
			rcDiv.bottom = rcDivR.bottom = 2;
			edStyleEx = 0;
		}
		::MapDialogRect( g_hToolWnd, &rcDiv ); 
		::MapDialogRect( g_hToolWnd, &rcDivR ); 
		int xEdit = tbRect.right + rcDiv.right;
		int editHeight = MapDialogY( g_hToolWnd, 11 );

		HWND hEdit = ::CreateWindowEx( edStyleEx, _T("Edit"), NULL, 
			WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 
			xEdit, ( rcClient.bottom - editHeight ) / 2, 
			rcClient.right - rcClient.left - xEdit - rcDivR.right, 
			editHeight, 
			g_hToolWnd, (HMENU) ID_FF_PATH, g_hInstDll, NULL);

		if( isComposited )
			::SetWindowTheme( hEdit, NULL, L"EditComposited::Edit" );
							
		// enable auto-complete for the edit control
		::SHAutoComplete( hEdit, SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON );
		//sub-class the edit control to handle key-stroke messages
		g_wndProcToolWindowEditPath = reinterpret_cast<WNDPROC>(  
			::SetWindowLongPtr( hEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>( &ToolWindowEditPathProc ) ) );
	}

    //--- set default font for all child controls
    
    if( ! g_hStdFont )
		g_hStdFont = CreateStandardOsFont();  
	EnumChildWindows( g_hToolWnd, ToolWndSetFont, (LPARAM) g_hStdFont );

	//--- read options from global configuration

	g_globalHistoryMaxEntries = g_profile.GetInt( _T("main"), _T("MaxGlobalHistoryEntries") );

	g_toolbarOffset.left = g_profile.GetInt( _T("Toolbar"), _T("OffsetX") );
	g_toolbarOffset.top = g_profile.GetInt( _T("Toolbar"), _T("OffsetY") );
	g_toolbarOffset.right = g_profile.GetInt( _T("Toolbar"), _T("OffsetWidth") );
 
	//--- enable toolbar buttons / leave disabled ---
	
	if( ! g_profile.GetString( _T("GlobalFolderHistory"), _T("0") ).empty() )
	{
		SendMessage( hTb, TB_SETSTATE, ID_FF_GLOBALHIST, MAKELONG( TBSTATE_ENABLED, 0 ) );
		SendMessage( hTb, TB_SETSTATE, ID_FF_LASTDIR, MAKELONG(TBSTATE_ENABLED, 0 ) );
	}
}

//-----------------------------------------------------------------------------------------------

HACCEL CreateMyAcceleratorTable()
{
	struct
	{
		LPCWSTR name;
		UINT id;
	}
	cmdList[] = {
		{ L"ff_LastFolder", ID_FF_LASTDIR },
		{ L"ff_ViewAllFiles", ID_FF_SHOWALL },
		{ L"ff_FocusPathEdit", ID_FF_FOCUSPATH },
		{ L"ff_MenuFolderHistory", ID_FF_GLOBALHIST },
		{ L"ff_MenuOpenFolders", ID_FF_OPENDIRS },
		{ L"ff_MenuFavorites", ID_FF_FAVORITES },
		{ L"ff_MenuConfig", ID_FF_CONFIG }
	};

	vector< ACCEL > accel;

	for( int i = 0; i < _countof( cmdList ); ++i )
	{
		int hotkey = g_profile.GetInt( L"Hotkeys", cmdList[ i ].name );  
		if( hotkey != 0 )
		{
			UINT vk, mod;
			SplitHotKey( &vk, &mod, hotkey );
			ACCEL ac = { FVIRTKEY, vk, cmdList[ i ].id };
			if( mod & MOD_CONTROL )
				ac.fVirt |= FCONTROL;
			if( mod & MOD_ALT )
				ac.fVirt |= FALT;
			if( mod & MOD_SHIFT )
				ac.fVirt |= FSHIFT;
			accel.push_back( ac );
		}
	}

	return ::CreateAcceleratorTable( &accel[ 0 ], static_cast<int>( accel.size() ) );
}

//-----------------------------------------------------------------------------------------------

LPCTSTR GetCommandName( int cmd )
{
	switch( cmd )
	{
		case ID_FF_LASTDIR:     return _T("ff_LastFolder");
		case ID_FF_SHOWALL:     return _T("ff_ViewAllFiles");
		case ID_FF_FOCUSPATH:   return _T("ff_FocusPathEdit");
		case ID_FF_GLOBALHIST:  return _T("ff_MenuFolderHistory");
		case ID_FF_OPENDIRS:    return _T("ff_MenuOpenFolders");
		case ID_FF_FAVORITES:   return _T("ff_MenuFavorites");
		case ID_FF_CONFIG:      return _T("ff_MenuConfig");
	}
	return _T("");
}

//-----------------------------------------------------------------------------------------

// callbacks that will be called by the file dialog hook

namespace FileDlgHookCallbacks
{
	void OnInitDone()
	{
		//--- initial show + update of the tool window ---
		UpdatePathEdit();
		g_isToolWndVisible = true;
		::ShowWindow( g_hToolWnd, SW_SHOWNA );
	}

	void OnFolderChange()
	{
		UpdatePathEdit();
	}

	void OnResize()
	{
		//reposition the tool window to "dock" it onto the file dialog
		AdjustToolWindowPos();
	}

	void OnEnable( bool bEnable )
	{
		::EnableWindow( g_hToolWnd, bEnable );
	}

	void OnShow( bool bShow )
	{
		::ShowWindow( g_hToolWnd, bShow ? SW_SHOW : SW_HIDE );
	}

	void OnActivate( WPARAM wParam, LPARAM lParam )
	{}

	void OnDestroy( bool isOkBtnPressed )
	{
		//--- add folder to history if file dialog was closed with OK

		if( isOkBtnPressed )
			AddCurrentFolderToHistory();			

		//--- destroy tool window + class

		::DestroyWindow( g_hToolWnd );
		g_hToolWnd = NULL;
		g_hFileDialog = NULL;

		::UnregisterClass( FF_WNDCLASSNAME, g_hInstDll );

		//--- destroy additional resources

		if( g_hToolbarImages )
		{
			::ImageList_Destroy( g_hToolbarImages );
			g_hToolbarImages = NULL;
		}
		
		if( g_hStdFont )
		{
			::DeleteObject( g_hStdFont );
			g_hStdFont = NULL;
		}
	}

	void SetTimer( DWORD interval )
	{
		::SetTimer( g_hToolWnd, 1, interval, NULL );
	}

}; //namespace FileDlgHookCallbacks

//-----------------------------------------------------------------------------------------------
// Check if hook for the current program and the given type of dialog is enabled.

bool IsCurrentProgramEnabledForDialog( FileDlgType fileDlgType )
{
	// Check if FlashFolder is globally disabled for given kind of dialog.
	TCHAR* pProfileGroup = _T("");
	switch( fileDlgType.mainType )
	{
		case FDT_COMMON:
			pProfileGroup = _T("CommonFileDlg");
		break;
		case FDT_MSOFFICE:
			pProfileGroup = _T("MSOfficeFileDlg");
		break;
		case FDT_COMMON_OPENWITH:
			pProfileGroup = _T("CommonOpenWithDlg");
		break;
		case FDT_COMMON_FOLDER:
			pProfileGroup = _T("CommonFolderDlg");
		break;	
	}
	if( g_profile.GetInt( pProfileGroup, _T("EnableHook") ) == 0 )
		return false;

	// Check if EXE filename is in the excludes list for given dialog type.
	tstring excludesGroup = pProfileGroup;
	excludesGroup += _T(".Excludes");
	for( int i = 0;; ++i )
	{
		TCHAR key[10];
		StringCbPrintf( key, sizeof(key), _T("%d"), i );
		tstring path = g_profile.GetString( excludesGroup.c_str(), key );
		if( path.empty() )
			break;
		if( _tcsicmp( g_currentExeName, path.c_str() ) == 0 )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------------------

void InitHook( HWND hwnd, FileDlgType fileDlgType )
{
	//--- initialise hook for this dialog

	bool isFileDialog = ( fileDlgType.mainType == FDT_COMMON || fileDlgType.mainType == FDT_MSOFFICE );

	g_hFileDialog = hwnd;

	CreateToolWindow( isFileDialog );

	// create an instance of a file dialog hook class depending on the
	// type of file dialog
	switch( fileDlgType.mainType )
	{
		case FDT_COMMON:
		{
			g_spFileDlgHook.reset( new CmnFileDlgHook );
			g_spFileDlgHook->Init( hwnd, g_hToolWnd );
		}
		break;
		case FDT_MSOFFICE:
		{
			g_spFileDlgHook.reset( new MsoFileDlgHook( fileDlgType.subType ) );
			g_spFileDlgHook->Init( hwnd, g_hToolWnd );
		}
		break;
		case FDT_COMMON_FOLDER:
		{
			g_spFileDlgHook.reset( new CmnFolderDlgHook );
			g_spFileDlgHook->Init( hwnd, g_hToolWnd );
		}
		break;
	}
}

//-----------------------------------------------------------------------------------------
/// This function gets called in the context of the hooked process.
/// (DLL-Export)

LRESULT CALLBACK FFHook_CBT( int nCode, WPARAM wParam, LPARAM lParam )
{
	HWND hwnd = reinterpret_cast<HWND>( wParam );

	if( nCode == HCBT_ACTIVATE )
	{
		// Check whether a file dialog must be hooked.
		// For now, we can only handle one running file dialog per application, but
		// this should be enough in nearly all cases.
		if( g_hFileDialog == NULL )
		{
			FileDlgType fileDlgType = GetFileDlgType( hwnd );
			if( fileDlgType.mainType != FDT_NONE )
			{
				DebugOut( L"[fflib] file dialog detected, type %d.%d", 
					fileDlgType.mainType, fileDlgType.subType );
			
				if( g_profileDefaults.IsEmpty() )
				{
					GetProfileDefaults( &g_profileDefaults );
					g_profile.SetRoot( _T("zett42\\FlashFolder") );
					g_profile.SetDefaults( &g_profileDefaults );
				}

				if( IsCurrentProgramEnabledForDialog( fileDlgType ) )
					InitHook( hwnd, fileDlgType );
			}
		} //if		
	} //if

	return CallNextHookEx( NULL, nCode, wParam, lParam );
}

//-----------------------------------------------------------------------------------------
/// This function can be used to force loading of this delay-loaded DLL immediately
/// so its module handle can be referenced.
/// (DLL-Export)

HINSTANCE GetFFLibHandle()
{
	return g_hInstDll;
}
