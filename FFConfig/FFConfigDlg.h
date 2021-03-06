/* This file is part of FlashFolder. 
 * Copyright (C) 2007-2012 zett42.de ( zett42 at users.sourceforge.net ) 
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
 *
 */
#pragma once

#include "PageGeneric.h"
#include "PageCommonFileDlg.h"
#include "PageCommonDirDlg.h"
#include "PageMsoFileDlg.h"
#include "PageFileManager.h"
#include "PageShortcuts.h"
#include "PageToolbar.h"

//-----------------------------------------------------------------------------------------------

class CFFConfigDlg : public TreePropSheet::CTreePropSheet
{
public:
	CFFConfigDlg(CWnd* pParent = NULL);	// standard constructor

	enum { IDD = IDD_FFCONFIG_DIALOG };

	enum { WM_APP_PAGE_CHANGED = WM_APP + 0x100 };

private:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	virtual BOOL OnInitDialog();

	afx_msg LRESULT OnPageChanged( WPARAM wp, LPARAM lp );
	DECLARE_MESSAGE_MAP()

private:
	CPageGeneric m_pageGeneric;
	CPageCommonFileDlg m_pageCommonFileDlg;
	CPageCommonDirDlg m_pageCommonDirDlg;
	CPageMsoFileDlg m_pageMsoFileDlg;
	CPageFileManager m_pageFileManager;
	CPageShortcuts m_pageShortcuts;
	CPageToolbar m_pageToolbar;
};
