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
//	DOOM graphics stuff for SDL library
//
//-----------------------------------------------------------------------------

#include <stdlib.h>

#include <SDL2/SDL.h>

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
static SDL_Surface	*screen_surface = NULL;
static SDL_Surface	*buffer_surface = NULL;
static SDL_Texture	*render_texture = NULL;

static int screenWidth = SCREENWIDTH;
static int screenHeight = SCREENHEIGHT;
static dboolean vid_initialized = false;
static int grabMouse;

#define I_NOUPDATE	0

/*
============================================================================

								USER INPUT

============================================================================
*/

//--------------------------------------------------------------------------
//
// PROC I_SetPalette
//
// Palette source must use 8 bit RGB elements.
//
//--------------------------------------------------------------------------

void I_SetPalette(byte *palette)
{
	if ( !vid_initialized )
		return;

	SDL_Color colormap[256];
	
	for ( int i = 0; i < 256; i++ )
	{
		colormap[i].r = gammatable[usegamma][*palette++];
		colormap[i].g = gammatable[usegamma][*palette++];
		colormap[i].b = gammatable[usegamma][*palette++];
	}
	
	SDL_SetPaletteColors(screen_surface->format->palette, colormap, 0, 256);
}

/*
============================================================================

							GRAPHICS MODE

============================================================================
*/

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
   
	SDL_Rect blit_rect = { 0, 0, SCREENWIDTH, SCREENHEIGHT };
	
	SDL_LowerBlit (screen_surface, &blit_rect, buffer_surface, &blit_rect);

	SDL_UpdateTexture (render_texture, NULL, buffer_surface->pixels,
		buffer_surface->pitch);

	SDL_RenderClear (sdl_renderer);
	SDL_RenderCopy (sdl_renderer, render_texture, NULL, NULL);
	
	SDL_RenderPresent (sdl_renderer);
}

//
// I_ReadScreen
//

void I_ReadScreen(byte *scr)
{
   int size = SCREENWIDTH*SCREENHEIGHT;

   // haleyjd
   memcpy(scr, *screens, size);
}

//--------------------------------------------------------------------------
//
// PROC I_InitGraphics
//
//--------------------------------------------------------------------------

void I_InitGraphics(void)
{
	char text[20];
	int p, bpp;
	Uint32 flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
	Uint32 rmask, gmask, bmask, amask;
	Uint32 sdl_pixel_format;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
		I_Error("Couldn't initialize video: %s", SDL_GetError());
	}

	SDL_DisplayMode DispMode;
	SDL_GetCurrentDisplayMode(0, &DispMode);

#ifdef _WIN32
	screenWidth = DispMode.h * 3.0 / 4.0;
	screenHeight = DispMode.w * 3.0 / 4.0;
#else
	screenWidth = DispMode.w * 3.0 / 4.0;
	screenHeight = DispMode.h * 3.0 / 4.0;
#endif

	sdl_window = SDL_CreateWindow(text, SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, screenWidth, screenHeight, flags);
		
	if ( sdl_window == NULL )
	{
		I_Error ("Couldn't initialize window: %s\n", SDL_GetError());
	}
	
	sdl_renderer = SDL_CreateRenderer (sdl_window, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC);
	
	if ( sdl_renderer == NULL )
	{
		SDL_DestroyWindow (sdl_window);
		I_Error ("Couldn't initialize renderer: %s\n", SDL_GetError());
	}

	sdl_pixel_format = SDL_GetWindowPixelFormat (sdl_window);

	screen_surface = SDL_CreateRGBSurface (0, SCREENWIDTH, SCREENHEIGHT, 8,
		0, 0, 0, 0);

	SDL_FillRect (screen_surface, NULL, 0);

	SDL_PixelFormatEnumToMasks (sdl_pixel_format, &bpp, &rmask, &gmask, 
		&bmask, &amask);

	buffer_surface = SDL_CreateRGBSurface (0, SCREENWIDTH, SCREENHEIGHT,
		bpp, rmask, gmask, bmask, amask);

	SDL_FillRect (buffer_surface, NULL, 0);

	render_texture = SDL_CreateTexture (sdl_renderer, sdl_pixel_format,
		SDL_TEXTUREACCESS_STREAMING, SCREENWIDTH, SCREENHEIGHT);

	vid_initialized = true;

	grabMouse = 1;
	SDL_SetRelativeMouseMode (SDL_TRUE);

	SDL_ShowCursor (SDL_DISABLE);

	screens[0] = screen_surface->pixels;

	I_SetPalette ((byte *)W_CacheLumpName("PLAYPAL", PU_CACHE));
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

	if (screen_surface != NULL)
		SDL_FreeSurface (screen_surface);

	if (buffer_surface != NULL)
		SDL_FreeSurface (buffer_surface);

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
static int xlatekey (SDL_Keysym *key)
{
	switch (key->sym)
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
		if (key->mod & KMOD_NUM)
			return SDLK_0;
	case SDLK_KP_1:
		if (key->mod & KMOD_NUM)
			return SDLK_1;
	case SDLK_KP_2:
		if (key->mod & KMOD_NUM)
			return SDLK_2;
		else
			return KEY_DOWNARROW;
	case SDLK_KP_3:
		if (key->mod & KMOD_NUM)
			return SDLK_3;
	case SDLK_KP_4:
		if (key->mod & KMOD_NUM)
			return SDLK_4;
		else
			return KEY_LEFTARROW;
	case SDLK_KP_5:
		return SDLK_5;
	case SDLK_KP_6:
		if (key->mod & KMOD_NUM)
			return SDLK_6;
		else
			return KEY_RIGHTARROW;
	case SDLK_KP_7:
		if (key->mod & KMOD_NUM)
			return SDLK_7;
	case SDLK_KP_8:
		if (key->mod & KMOD_NUM)
			return SDLK_8;
		else
			return KEY_UPARROW;
	case SDLK_KP_9:
		if (key->mod & KMOD_NUM)
			return SDLK_9;

	case SDLK_KP_PERIOD:
		if (key->mod & KMOD_NUM)
			return SDLK_PERIOD;
	case SDLK_KP_DIVIDE:	return SDLK_SLASH;
	case SDLK_KP_MULTIPLY:	return SDLK_ASTERISK;
	case SDLK_KP_MINUS:	return KEY_MINUS;
	case SDLK_KP_PLUS:	return SDLK_PLUS;
	case SDLK_KP_ENTER:	return KEY_ENTER;
	case SDLK_KP_EQUALS:	return KEY_EQUALS;

	default:
		return key->sym;
	}
}


/* Shamelessly stolen from PrBoom+ */
static int I_SDLtoHereticMouseState(Uint8 buttonstate)
{
	return 0
		| ((buttonstate & SDL_BUTTON(1)) ? 1 : 0)
		| ((buttonstate & SDL_BUTTON(2)) ? 2 : 0)
		| ((buttonstate & SDL_BUTTON(3)) ? 4 : 0);
}

/* This processes SDL events */
void I_GetEvent(SDL_Event *Event)
{
	event_t event;
	SDL_Keymod mod;

	switch (Event->type)
	{
	case SDL_KEYDOWN:
		mod = SDL_GetModState ();
		if (mod & (KMOD_RCTRL|KMOD_LCTRL))
		{
			if (Event->key.keysym.sym == 'g')
			{
				if (SDL_GetRelativeMouseMode () == SDL_FALSE)
				{
					grabMouse = 1;
					SDL_SetRelativeMouseMode (SDL_TRUE);
				}
				else
				{
					grabMouse = 0;
					SDL_SetRelativeMouseMode (SDL_FALSE);
				}
				break;
			}
		}
		else if (mod & (KMOD_RALT|KMOD_LALT))
		{
			if (Event->key.keysym.sym == SDLK_RETURN)
			{
				SDL_SetWindowFullscreen(sdl_window, 
					SDL_WINDOW_FULLSCREEN_DESKTOP);
				break;
			}
		}
		event.type = ev_keydown;
		event.data1 = xlatekey(&Event->key.keysym);
		D_PostEvent(&event);
		break;

	case SDL_KEYUP:
		event.type = ev_keyup;
		event.data1 = xlatekey(&Event->key.keysym);
		D_PostEvent(&event);
		break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		event.type = ev_mouse;
		event.data1 = I_SDLtoHereticMouseState(SDL_GetMouseState(NULL,NULL));
		event.data2 = event.data3 = 0;
		D_PostEvent(&event);
		break;

	case SDL_MOUSEMOTION:
		/* Ignore mouse warp events */
		if ((Event->motion.x != SCREENWIDTH/2) ||
		    (Event->motion.y != SCREENHEIGHT/2) )
		{
			/* Warp the mouse back to the center */
			/*
			if (grabMouse) {
				SDL_WarpMouse(SCREENWIDTH/2, SCREENHEIGHT/2);
			}
			*/
			event.type = ev_mouse;
			event.data1 = I_SDLtoHereticMouseState(Event->motion.state);
			event.data2 = Event->motion.xrel << 3;
			event.data3 = -Event->motion.yrel << 3;
			D_PostEvent(&event);
		}
		break;

	case SDL_QUIT:
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
============================================================================

							MOUSE

============================================================================
*/


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
