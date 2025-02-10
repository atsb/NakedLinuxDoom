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
//	Fixed point arithemtics, implementation.
//
//-----------------------------------------------------------------------------


#ifndef __M_FIXED__
#define __M_FIXED__

#include <stdlib.h>
#include "doomtype.h"

#ifdef __GNUG__
#pragma interface
#endif


//
// Fixed point, 32bit as 16.16.
//
#define FRACBITS		16
#define FRACUNIT		(1<<FRACBITS)

typedef int fixed_t;

//
// Fixed Point Multiplication
//

#ifdef _WIN32
inline static fixed_t FixedMul(fixed_t a, fixed_t b)
{
    return (fixed_t)((Long64)a * b >> FRACBITS);
}
#else
__inline__ static fixed_t FixedMul(fixed_t a, fixed_t b)
{
  return (fixed_t)((Long64) a*b >> FRACBITS);
}
#endif

//
// Fixed Point Division
//

#ifdef _WIN32
inline static fixed_t FixedDiv(fixed_t a, fixed_t b)
{
  return (abs(a)>>14) >= abs(b) ? ((a^b)>>31) ^ INT_MAX :
    (fixed_t)(((Long64) a << FRACBITS) / b);
}
#else
__inline__ static fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    return (abs(a) >> 14) >= abs(b) ? ((a ^ b) >> 31) ^ INT_MAX :
        (fixed_t)(((Long64)a << FRACBITS) / b);
}
#endif

#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
