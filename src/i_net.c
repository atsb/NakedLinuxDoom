// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id: i_net.c,v 1.4 1998/05/16 09:41:03 jim Exp $
//
//  Copyright (C) 1999 by
//  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 
//  02111-1307, USA.
//
// DESCRIPTION:
//  Network stub
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "z_zone.h"
#include "doomstat.h"
#include "i_system.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"

#include "i_net.h"

//
// I_InitNetwork
//
void I_InitNetwork(void)
{
	doomcom = malloc(sizeof(*doomcom));
	memset(doomcom, 0, sizeof(*doomcom));

	// Check for -dup parameter (for demo compatibility)
	int i = M_CheckParm("-dup");
	if (i && i < myargc - 1)
	{
		doomcom->ticdup = myargv[i + 1][0] - '0';
		if (doomcom->ticdup < 1)
			doomcom->ticdup = 1;
		if (doomcom->ticdup > 9)
			doomcom->ticdup = 9;
	}
	else
		doomcom->ticdup = 1;

	if (M_CheckParm("-extratic"))
		doomcom->extratics = 1;
	else
		doomcom->extratics = 0;

	// Check if user tried to use -net
	if (M_CheckParm("-net"))
	{
		printf("WARNING: Networking disabled in this build\n");
		printf("         Ignoring -net parameter\n");
	}

	// Single player game
	netgame = false;
	doomcom->id = DOOMCOM_ID;
	doomcom->numplayers = doomcom->numnodes = 1;
	doomcom->deathmatch = false;
	doomcom->consoleplayer = 0;
}

//
// I_NetCmd
// 
// Stub - does nothing
//
void I_NetCmd(void)
{

}

