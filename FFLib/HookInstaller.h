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
 *
 */
/* \file Class to manage the file dialog hook.
*/ 
#pragma once

#include "fflib_exports.h"

class FFHookInstaller
{
public:
	FFHookInstaller() {}
	~FFHookInstaller()          { Uninstall(); }
	
	bool Install();
	
	bool Uninstall();
	
	bool IsInstalled() const    { return GetHHook() != NULL; }
	
	HHOOK GetHHook() const      { return m_sharedHHook ? *m_sharedHHook : NULL; }
	
private:
	CAtlFileMapping<HHOOK> m_sharedHHook;
};
