// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	Simple basic typedefs, isolated here to make it easier
//	 separating modules.
//    
//-----------------------------------------------------------------------------


#ifndef __DOOMTYPE__
#define __DOOMTYPE__


#ifndef __BYTEBOOL__
#define __BYTEBOOL__
// Fixed to use builtin bool type with C++.
typedef unsigned char byte;
#undef true
#undef false
typedef enum {
	false = 0,
	true = 1
} dboolean;
#endif

#include <stdint.h>
#include <limits.h>
#define D_MAXINT INT_MAX
#define D_MININT INT_MIN
#define D_MAXSHORT  SHRT_MAX

#define MAXCHAR         ((char)0x7f)
#define MINCHAR         ((char)0x80)

#define arrlen(array) (sizeof(array) / sizeof(*array))

#define __LONG64_TYPE__ long long
typedef __LONG64_TYPE__ Long64;
typedef unsigned __LONG64_TYPE__ ULong64;

// Predefined with some OS.
#ifdef LINUX
#include <values.h>
#else
#ifndef MAXCHAR
#define MAXCHAR		((char)0x7f)
#endif
#ifndef MAXSHORT
#define MAXSHORT	((short)0x7fff)
#endif

// Max pos 32-bit int.
#ifndef MAXINT
#define MAXINT		((int)0x7fffffff)	
#endif
#ifndef MAXLONG
#define MAXLONG		((long)0x7fffffff)
#endif
#ifndef MINCHAR
#define MINCHAR		((char)0x80)
#endif
#ifndef MINSHORT
#define MINSHORT	((short)0x8000)
#endif

// Max negative 32-bit integer.
#ifndef MININT
#define MININT		((int)0x80000000)	
#endif
#ifndef MINLONG
#define MINLONG		((long)0x80000000)
#endif
#endif




#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
