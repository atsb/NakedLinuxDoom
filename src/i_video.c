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
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff
//
//-----------------------------------------------------------------------------

#include <stdlib.h>

#include <SDL3/SDL.h>

#include "m_swap.h"
#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "z_zone.h"
#include "d_main.h"
#include "w_wad.h"
#include "doomdef.h"

// Public Data

int DisplayTicker = 0;

dboolean useexterndriver;
dboolean mousepresent;


// Extern Data

extern int usemouse, usejoystick;

// Private Data

static SDL_Window 	*sdl_window	= NULL;
static SDL_Renderer	*sdl_renderer	= NULL;
static SDL_Texture	*render_texture = NULL;
static Uint32		*pixel_buffer = NULL;  // 32-bit ARGB buffer
static Uint32		palette[256];          // Converted palette

static int screenWidth = SCREENWIDTH;
static int screenHeight = SCREENHEIGHT;
static dboolean vid_initialized = false;
static int grabMouse;

#define I_NOUPDATE	0

//--------------------------------------------------------------------------
//
// PROC I_SetPalette
//
// Palette source must use 8 bit RGB elements.
//
//--------------------------------------------------------------------------

// fixed palette, not gamma corrected previously but now is.  Gibbon.
void I_SetPalette(byte* pal)
{
	if (!vid_initialized)
		return;

	int g = usegamma;
	if (g < 0) g = 0;
	if (g > 4) g = 4;

	for (int i = 0; i < 256; i++)
	{
		byte r = *pal++;
		byte g_val = *pal++;
		byte b = *pal++;

		palette[i] = ((Uint32)0xFF << 24) |
			((Uint32)gammatable[g][r] << 16) |
			((Uint32)gammatable[g][g_val] << 8) |
			((Uint32)gammatable[g][b]);
	}
}

/*
==============
=
= I_Update
=
==============
*/

void I_Update (void)
{
	// draws little dots on the bottom of the screen
	if(devparm)
	{
		static int lasttic;
		byte *s = screens[0];
		
		int i = I_GetTime();
		int tics = i - lasttic;
		lasttic = i;
		if (tics > 20)
		{
			tics = 20;
		}
		for (i=0 ; i<tics*2 ; i+=2)
			s[(SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
		for ( ; i<20*2 ; i+=2)
			s[(SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
	}
	
	void* pixels;
	int pitch;
	
	if (SDL_LockTexture(render_texture, NULL, &pixels, &pitch))
	{
		byte* src = screens[0];
		Uint32* dst = (Uint32*)pixels;
		
#if defined(__aarch64__) || defined(__ARM_ARCH_8__) || defined(__arm64__)
		__builtin_prefetch(palette, 0, 3);
		
		int total = SCREENWIDTH * SCREENHEIGHT;
		int i = 0;
		
		for (; i <= total - 16; i += 16)
		{
			__builtin_prefetch(src + i + 64, 0, 0);
			
			dst[i+0]  = palette[src[i+0]];
			dst[i+1]  = palette[src[i+1]];
			dst[i+2]  = palette[src[i+2]];
			dst[i+3]  = palette[src[i+3]];
			dst[i+4]  = palette[src[i+4]];
			dst[i+5]  = palette[src[i+5]];
			dst[i+6]  = palette[src[i+6]];
			dst[i+7]  = palette[src[i+7]];
			dst[i+8]  = palette[src[i+8]];
			dst[i+9]  = palette[src[i+9]];
			dst[i+10] = palette[src[i+10]];
			dst[i+11] = palette[src[i+11]];
			dst[i+12] = palette[src[i+12]];
			dst[i+13] = palette[src[i+13]];
			dst[i+14] = palette[src[i+14]];
			dst[i+15] = palette[src[i+15]];
		}
		
		for (; i < total; i++)
			dst[i] = palette[src[i]];
#else
		for (int y = 0; y < SCREENHEIGHT; y++)
		{
			Uint32* dst_row = (Uint32*)((Uint8*)pixels + y * pitch);
			byte* src_row = src + y * SCREENWIDTH;
			
			int x = 0;
			for (; x <= SCREENWIDTH - 8; x += 8)
			{
				dst_row[x+0] = palette[src_row[x+0]];
				dst_row[x+1] = palette[src_row[x+1]];
				dst_row[x+2] = palette[src_row[x+2]];
				dst_row[x+3] = palette[src_row[x+3]];
				dst_row[x+4] = palette[src_row[x+4]];
				dst_row[x+5] = palette[src_row[x+5]];
				dst_row[x+6] = palette[src_row[x+6]];
				dst_row[x+7] = palette[src_row[x+7]];
			}
			
			for (; x < SCREENWIDTH; x++)
				dst_row[x] = palette[src_row[x]];
		}
#endif
		
		SDL_UnlockTexture(render_texture);
	}
	SDL_RenderTexture(sdl_renderer, render_texture, NULL, NULL);
	SDL_RenderPresent(sdl_renderer);
}

//
// I_ReadScreen
//

void I_ReadScreen(byte *scr)
{
	int size = SCREENWIDTH*SCREENHEIGHT;

	// haleyjd
	memcpy(scr, screens[0], size);
}

//--------------------------------------------------------------------------
//
// PROC I_InitGraphics
//
//--------------------------------------------------------------------------

// new and improved video scaling.  Gibbon.
void I_InitGraphics(void)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		I_Error("Couldn't initialize video: %s", SDL_GetError());
	}

	sdl_window = SDL_CreateWindow("DOOM - SDL3", 0, 0, SDL_WINDOW_FULLSCREEN);

	if (sdl_window == NULL)
	{
		I_Error("Couldn't initialize window: %s\n", SDL_GetError());
	}

	sdl_renderer = SDL_CreateRenderer(sdl_window, NULL);

	if (sdl_renderer == NULL)
	{
		SDL_DestroyWindow(sdl_window);
		I_Error("Couldn't initialize renderer: %s\n", SDL_GetError());
	}

	SDL_SetRenderLogicalPresentation(sdl_renderer, SCREENWIDTH, SCREENHEIGHT,
		SDL_LOGICAL_PRESENTATION_STRETCH);

	SDL_SetRenderVSync(sdl_renderer, 1);

	render_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, SCREENWIDTH, SCREENHEIGHT);

	if (render_texture == NULL)
	{
		SDL_DestroyRenderer(sdl_renderer);
		SDL_DestroyWindow(sdl_window);
		I_Error("Couldn't create texture: %s\n", SDL_GetError());
	}

	SDL_SetTextureScaleMode(render_texture, SDL_SCALEMODE_NEAREST);

	vid_initialized = true;
	grabMouse = 1;
	SDL_SetWindowRelativeMouseMode(sdl_window, true);
	SDL_HideCursor();

	screens[0] = (byte*)malloc(SCREENWIDTH * SCREENHEIGHT);

	I_SetPalette((byte*)W_CacheLumpName("PLAYPAL", PU_CACHE));
}

//--------------------------------------------------------------------------
//
// PROC I_ShutdownGraphics
//
//--------------------------------------------------------------------------

void I_ShutdownGraphics(void)
{
	if (!vid_initialized)
		return;

	// Free our 8-bit buffer
	if (screens[0])
		free(screens[0]);

	if (render_texture != NULL)
		SDL_DestroyTexture (render_texture);

	if (sdl_renderer != NULL)
		SDL_DestroyRenderer (sdl_renderer);

	if (sdl_window != NULL)
		SDL_DestroyWindow (sdl_window);

	vid_initialized = false;
}

//===========================================================================

//
//  Translates the key
//
//===========================================================================
static int xlatekey (SDL_Keycode key, SDL_Keymod mod)
{
	switch (key)
	{
	case SDLK_LEFT:		return KEY_LEFTARROW;
	case SDLK_RIGHT:	return KEY_RIGHTARROW;
	case SDLK_DOWN:		return KEY_DOWNARROW;
	case SDLK_UP:		return KEY_UPARROW;
	case SDLK_ESCAPE:	return KEY_ESCAPE;
	case SDLK_RETURN:	return KEY_ENTER;

	case SDLK_F1:		return KEY_F1;
	case SDLK_F2:		return KEY_F2;
	case SDLK_F3:		return KEY_F3;
	case SDLK_F4:		return KEY_F4;
	case SDLK_F5:		return KEY_F5;
	case SDLK_F6:		return KEY_F6;
	case SDLK_F7:		return KEY_F7;
	case SDLK_F8:		return KEY_F8;
	case SDLK_F9:		return KEY_F9;
	case SDLK_F10:		return KEY_F10;
	case SDLK_F11:		return KEY_F11;
	case SDLK_F12:		return KEY_F12;

	case SDLK_BACKSPACE:	return KEY_BACKSPACE;
	case SDLK_PAUSE:	return KEY_PAUSE;
	case SDLK_EQUALS:	return KEY_EQUALS;
	case SDLK_MINUS:	return KEY_MINUS;

	case SDLK_LSHIFT:
	case SDLK_RSHIFT:
		return KEY_RSHIFT;

	case SDLK_LCTRL:
	case SDLK_RCTRL:
		return KEY_RCTRL;

	case SDLK_LALT:
		return KEY_LALT;
	case SDLK_RALT:
		return KEY_RALT;

	case SDLK_KP_0:
		return (mod & SDL_KMOD_NUM) ? SDLK_0 : 0;
	case SDLK_KP_1:
		return (mod & SDL_KMOD_NUM) ? SDLK_1 : 0;
	case SDLK_KP_2:
		return (mod & SDL_KMOD_NUM) ? SDLK_2 : KEY_DOWNARROW;
	case SDLK_KP_3:
		return (mod & SDL_KMOD_NUM) ? SDLK_3 : 0;
	case SDLK_KP_4:
		return (mod & SDL_KMOD_NUM) ? SDLK_4 : KEY_LEFTARROW;
	case SDLK_KP_5:
		return SDLK_5;
	case SDLK_KP_6:
		return (mod & SDL_KMOD_NUM) ? SDLK_6 : KEY_RIGHTARROW;
	case SDLK_KP_7:
		return (mod & SDL_KMOD_NUM) ? SDLK_7 : 0;
	case SDLK_KP_8:
		return (mod & SDL_KMOD_NUM) ? SDLK_8 : KEY_UPARROW;
	case SDLK_KP_9:
		return (mod & SDL_KMOD_NUM) ? SDLK_9 : 0;

	case SDLK_KP_PERIOD:
		return (mod & SDL_KMOD_NUM) ? SDLK_PERIOD : 0;
	case SDLK_KP_DIVIDE:	return SDLK_SLASH;
	case SDLK_KP_MULTIPLY:	return SDLK_ASTERISK;
	case SDLK_KP_MINUS:	return KEY_MINUS;
	case SDLK_KP_PLUS:	return SDLK_PLUS;
	case SDLK_KP_ENTER:	return KEY_ENTER;
	case SDLK_KP_EQUALS:	return KEY_EQUALS;

	default:
		return key;
	}
}

/* Mouse button conversion */
static int I_SDLtoHereticMouseState(Uint8 buttonstate)
{
	return 0
		| ((buttonstate & SDL_BUTTON_LMASK) ? 1 : 0)
		| ((buttonstate & SDL_BUTTON_MMASK) ? 2 : 0)
		| ((buttonstate & SDL_BUTTON_RMASK) ? 4 : 0);
}

/* This processes events */
void I_GetEvent(SDL_Event *Event)
{
	event_t event;
	SDL_Keymod mod;

	switch (Event->type)
	{
        case SDL_EVENT_KEY_DOWN:
	mod = SDL_GetModState();
	if (mod & (SDL_KMOD_RCTRL|SDL_KMOD_LCTRL))
	{
		if (Event->key.key == SDLK_G)
		{
			if (!SDL_GetWindowRelativeMouseMode(sdl_window))
			{
				grabMouse = 1;
				SDL_SetWindowRelativeMouseMode(sdl_window, true);
			}
			else
			{
				grabMouse = 0;
				SDL_SetWindowRelativeMouseMode(sdl_window, false);
			}
			break;
		}
	}
	else if (mod & (SDL_KMOD_RALT|SDL_KMOD_LALT))
	{
		if (Event->key.key == SDLK_RETURN)
		{
			SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_FULLSCREEN);
			break;
		}
	}
	event.type = ev_keydown;
	event.data1 = xlatekey(Event->key.key, Event->key.mod);
	D_PostEvent(&event);
	break;

        case SDL_EVENT_KEY_UP:
	        event.type = ev_keyup;
	        event.data1 = xlatekey(Event->key.key, Event->key.mod);
	        D_PostEvent(&event);
	        break;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		event.type = ev_mouse;
		event.data1 = I_SDLtoHereticMouseState(SDL_GetMouseState(NULL,NULL));
		event.data2 = event.data3 = 0;
		D_PostEvent(&event);
		break;

	case SDL_EVENT_MOUSE_MOTION:
		if ((Event->motion.x != SCREENWIDTH/2) ||
		    (Event->motion.y != SCREENHEIGHT/2) )
		{
			event.type = ev_mouse;
			event.data1 = I_SDLtoHereticMouseState(Event->motion.state);
		        event.data2 = (int)(Event->motion.xrel * 8.0f);
		        event.data3 = (int)(-Event->motion.yrel * 8.0f);
			D_PostEvent(&event);
		}
		break;

	case SDL_EVENT_QUIT:
		I_Quit();
		break;

	default:
		break;
	}
}

//
// I_StartTic
//
void I_StartTic (void)
{
	SDL_Event Event;
	while ( SDL_PollEvent(&Event) )
		I_GetEvent(&Event);
}

/*
================
=
= StartupMouse
=
================
*/

void I_StartupMouse (void)
{
	mousepresent = 1;
}

