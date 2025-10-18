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
//	System interface for sound.
//
//-----------------------------------------------------------------------------

#include <SDL2/SDL.h>
#include <math.h>
#include "sounds.h"
#include "mmus2mid.h"
#include "z_zone.h"
#include "m_swap.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "doomdef.h"

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo)
//  mixing buffer, and the samplerate of the raw data.

// Needed for calling the actual sound output.
static int SAMPLECOUNT = 512;
#define NUM_CHANNELS 8
#define SAMPLERATE 11025  // Hz
#define MUSIC_BUFFER_SIZE 4096

int snd_samplerate = 44100; // music

// The actual lengths of all sound effects.
int lengths[NUMSFX];

// The actual output device.
int audio_fd;

// The channel step amount...
unsigned int channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
unsigned int channelstepremainder[NUM_CHANNELS];

// The channel data pointers, start and end.
unsigned char* channels[NUM_CHANNELS];
unsigned char* channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing,
//  used to determine oldest, which automatically
//  has lowest priority.
// In case number of active sounds exceeds
//  available channels.
int channelstart[NUM_CHANNELS];

// The sound in channel handles,
//  determined on registration,
//  might be used to unregister/stop/modify,
//  currently unused.
int channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
// Used to catch duplicates (like chainsaw).
int channelids[NUM_CHANNELS];

// Pitch to stepping lookup, unused.
int steptable[256];

// Volume lookups.
int vol_lookup[128*256];

// Hardware left and right channel volume lookup.
int* channelleftvol_lookup[NUM_CHANNELS];
int* channelrightvol_lookup[NUM_CHANNELS];

// TinySoundFont music globals
static tsf* g_tsf = NULL;
static tml_message* g_tml = NULL;
static tml_message* g_tml_current = NULL;
static double g_msec = 0;
static int music_playing = 0;
static int music_looping = 0;
static int music_paused = 0;
static float music_volume = 1.0f;
static SDL_Thread* music_thread = NULL;
static SDL_mutex* music_mutex = NULL;
static int music_thread_running = 0;
static short* music_buffer = NULL;

//
// Music rendering thread
//
int music_render_thread(void* data)
{
    while (music_thread_running)
    {
        SDL_LockMutex(music_mutex);
        
        if (music_playing && !music_paused && g_tml_current && g_tsf)
        {
            int sample_block = MUSIC_BUFFER_SIZE / 2; // stereo, so divide by 2
            
            // Process MIDI events for this time slice
            while (g_tml_current && g_msec >= g_tml_current->time)
            {
                switch (g_tml_current->type)
                {
                    case TML_PROGRAM_CHANGE:
                        tsf_channel_set_presetnumber(g_tsf, g_tml_current->channel, 
                                                    g_tml_current->program, 0);
                        break;
                    case TML_NOTE_ON:
                        tsf_channel_note_on(g_tsf, g_tml_current->channel, 
                                          g_tml_current->key, g_tml_current->velocity / 127.0f);
                        break;
                    case TML_NOTE_OFF:
                        tsf_channel_note_off(g_tsf, g_tml_current->channel, g_tml_current->key);
                        break;
                    case TML_PITCH_BEND:
                        tsf_channel_set_pitchwheel(g_tsf, g_tml_current->channel, 
                                                   g_tml_current->pitch_bend);
                        break;
                    case TML_CONTROL_CHANGE:
                        tsf_channel_midi_control(g_tsf, g_tml_current->channel, 
                                                g_tml_current->control, g_tml_current->control_value);
                        break;
                }
                g_tml_current = g_tml_current->next;
            }
            
            if (!g_tml_current && music_looping)
            {
                g_tml_current = g_tml;
                g_msec = 0;
                tsf_reset(g_tsf);
            }
            else if (!g_tml_current)
            {
                music_playing = 0;
            }
            
            // Render audio for this block
            tsf_render_short(g_tsf, music_buffer, sample_block, 0);
            
            // Apply volume
            for (int i = 0; i < MUSIC_BUFFER_SIZE; i++)
            {
                music_buffer[i] = (short)(music_buffer[i] * music_volume);
            }
            
            // Advance time by the duration of samples we just rendered
            // sample_block samples at SAMPLERATE Hz = (sample_block / SAMPLERATE) seconds
            g_msec += (sample_block * 1000.0) / SAMPLERATE;
        }
        else
        {
            if (music_buffer)
                memset(music_buffer, 0, MUSIC_BUFFER_SIZE * sizeof(short));
        }
        
        SDL_UnlockMutex(music_mutex);
        SDL_Delay(10);
    }
    
    return 0;
}

//
// This function loads the sound data from the WAD lump,
//  for single sound.
//
void* getsfx(char* sfxname, int* len)
{
    unsigned char* sfx;
    unsigned char* paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    sprintf(name, "ds%s", sfxname);

    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);
    
    size = W_LumpLength(sfxlump);
    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Pads the sound effect out to the mixing buffer size.
    paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

    // Allocate from zone memory.
    paddedsfx = (unsigned char*)Z_Malloc(paddedsize+8, PU_STATIC, 0);

    // Now copy and pad.
    memcpy(paddedsfx, sfx, size);
    for (i=size; i<paddedsize+8; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump.
    Z_Free(sfx);
    
    // Preserve padded length.
    *len = paddedsize;

    // Return allocated padded data.
    return (void *)(paddedsfx + 8);
}

//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short handlenums = 0;
    int i;
    int rc = -1;
    int oldest = gametic;
    int oldestnum = 0;
    int slot;
    int rightvol;
    int leftvol;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful ||
        sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        // Loop all channels, check.
        for (i=0; i<NUM_CHANNELS; i++)
        {
            // Active, and using the same SFX?
            if ((channels[i]) && (channelids[i] == sfxid))
            {
                // Reset.
                channels[i] = 0;
                break;
            }
        }
    }

    // Loop all channels to find oldest SFX.
    for (i=0; (i<NUM_CHANNELS) && (channels[i]); i++)
    {
        if (channelstart[i] < oldest)
        {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }

    // Tales from the cryptic.
    if (i == NUM_CHANNELS)
        slot = oldestnum;
    else
        slot = i;

    // Set pointer to raw data.
    channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    // Set pointer to end of raw data.
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Reset current handle number, limited to 0..100.
    if (!handlenums)
        handlenums = 100;

    // Assign current handle number.
    channelhandles[slot] = rc = handlenums++;

    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    // Separation, that is, orientation/stereo.
    seperation += 1;

    // Per left/right channel.
    volume *= 8;
    leftvol = volume - ((volume*seperation*seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume*seperation*seperation) >> 16);

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
        I_Error("rightvol out of bounds");
    
    if (leftvol < 0 || leftvol > 127)
        I_Error("leftvol out of bounds");
    
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    channelids[slot] = sfxid;

    return rc;
}

//
// SFX API
//
void I_SetChannels()
{
    int i;
    int j;
    int* steptablemid = steptable + 128;
    
    for (i=-128; i<128; i++)
        steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);
    
    // Generates volume lookup tables
    for (i=0; i<128; i++)
        for (j=0; j<256; j++)
            vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}

void I_SetSfxVolume(int volume)
{
    snd_SfxVolume = volume;
}

int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    // UNUSED
    priority = 0;
    
    SDL_LockAudio();
    id = addsfx(id, vol, steptable[pitch], sep);
    SDL_UnlockAudio();
    
    return id;
}

void I_StopSound(int handle)
{
    // UNUSED.
    handle = 0;
}

int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}

//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the given
//  mixing buffer, and clamping it to the allowed
//  range.
//

void I_UpdateSound(void *unused, Uint8 *stream, int len)
{
    unsigned int sample;
    int dl;
    int dr;
    signed short* leftout;
    signed short* rightout;
    signed short* leftend;
    int step;
    int chan;
    
    // Clear stream first
    memset(stream, 0, len);
    
    // Render music directly here if playing
    if (music_playing && !music_paused && g_tsf && g_tml_current)
    {
        SDL_LockMutex(music_mutex);
        
        signed short* music_out = (signed short*)stream;
        int samples_to_render = SAMPLECOUNT; // mono samples, not stereo
        
        // Process MIDI events up to current time
        while (g_tml_current && g_msec >= g_tml_current->time)
        {
            switch (g_tml_current->type)
            {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(g_tsf, g_tml_current->channel, 
                                                g_tml_current->program, 0);
                    break;
                case TML_NOTE_ON:
                    tsf_channel_note_on(g_tsf, g_tml_current->channel, 
                                      g_tml_current->key, g_tml_current->velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(g_tsf, g_tml_current->channel, g_tml_current->key);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(g_tsf, g_tml_current->channel, 
                                               g_tml_current->pitch_bend);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(g_tsf, g_tml_current->channel, 
                                            g_tml_current->control, g_tml_current->control_value);
                    break;
            }
            g_tml_current = g_tml_current->next;
        }
        
        // Check for loop
        if (!g_tml_current && music_looping)
        {
            g_tml_current = g_tml;
            g_msec = 0;
            tsf_reset(g_tsf);
        }
        else if (!g_tml_current)
        {
            music_playing = 0;
        }
        
        // Render music for this buffer
        if (music_playing)
        {
            tsf_render_short(g_tsf, music_out, samples_to_render, 0);
            
            // Apply volume
            for (int i = 0; i < samples_to_render * 2; i++) // *2 for stereo
            {
                music_out[i] = (short)(music_out[i] * music_volume * 0.5f);
            }
            
            // Advance time
            g_msec += (samples_to_render * 1000.0) / SAMPLERATE;
        }
        
        SDL_UnlockMutex(music_mutex);
    }
    
    leftout = (signed short *)stream;
    rightout = ((signed short *)stream)+1;
    step = 2;
    leftend = leftout + SAMPLECOUNT*step;

    while (leftout != leftend)
    {
        dl = *leftout;
        dr = *rightout;

        for (chan = 0; chan < NUM_CHANNELS; chan++)
        {
            if (channels[chan])
            {
                sample = *channels[chan];
                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];
                channelstepremainder[chan] += channelstep[chan];
                channels[chan] += channelstepremainder[chan] >> 16;
                channelstepremainder[chan] &= 65536-1;

                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }
        
        if (dl > 0x7fff)
            *leftout = 0x7fff;
        else if (dl < -0x8000)
            *leftout = -0x8000;
        else
            *leftout = dl;

        if (dr > 0x7fff)
            *rightout = 0x7fff;
        else if (dr < -0x8000)
            *rightout = -0x8000;
        else
            *rightout = dr;

        leftout += step;
        rightout += step;
    }
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    // UNUSED.
    handle = vol = sep = pitch = 0;
}

void I_ShutdownSound(void)
{
    // Stop music thread
    music_thread_running = 0;
    if (music_thread)
    {
        SDL_WaitThread(music_thread, NULL);
        music_thread = NULL;
    }
    
    if (music_mutex)
    {
        SDL_DestroyMutex(music_mutex);
        music_mutex = NULL;
    }
    
    if (g_tsf)
    {
        tsf_close(g_tsf);
        g_tsf = NULL;
    }
    
    if (music_buffer)
    {
        free(music_buffer);
        music_buffer = NULL;
    }
    
    SDL_CloseAudio();
}

void I_InitSound()
{
    SDL_AudioSpec wanted;
    int i;
    
    fprintf(stderr, "I_InitSound: ");
    
    wanted.freq = SAMPLERATE;
    if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
        wanted.format = AUDIO_S16MSB;
    else
        wanted.format = AUDIO_S16LSB;
    wanted.channels = 2;
    wanted.samples = SAMPLECOUNT;
    wanted.callback = I_UpdateSound;
    
    if (SDL_OpenAudio(&wanted, NULL) < 0)
    {
        fprintf(stderr, "SDL_OpenAudio: couldn't open audio\n");
        return;
    }
    
    SAMPLECOUNT = wanted.samples;
    fprintf(stderr, " configured audio device with %d samples/slice\n", SAMPLECOUNT);

    const char* soundfont_paths[] = {
        "./soundfont.sf2",
        "/usr/share/soundfonts/default.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/soundfonts/freepats-general-midi.sf2",
        NULL
    };
    
    for (i = 0; soundfont_paths[i] != NULL; i++)
    {
        g_tsf = tsf_load_filename(soundfont_paths[i]);
        if (g_tsf)
        {
            fprintf(stderr, "TinySoundFont: loaded %s\n", soundfont_paths[i]);
            break;
        }
    }
    
    if (g_tsf)
    {
        tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, SAMPLERATE, -10.0f);
        music_mutex = SDL_CreateMutex();
    }
    else
    {
        fprintf(stderr, "Warning: No soundfont found, music disabled\n");
    }
    
    tsf_set_max_voices(g_tsf, 64);
    
    fprintf(stderr, "I_InitSound: ");
    
    for (i=1; i<NUMSFX; i++)
    {
        if (!S_sfx[i].link)
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        else
        {
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[(S_sfx[i].link - S_sfx)/sizeof(sfxinfo_t)];
        }
    }

    fprintf(stderr, " pre-cached all sound data\n");
    fprintf(stderr, "I_InitSound: sound module ready\n");
    fprintf(stderr, "I_InitSound: music module ready\n");
    SDL_PauseAudio(0);
}

//
// MUSIC API
//

void I_ShutdownMusic(void)
{
    I_UnRegisterSong(1);
}

void I_PlaySong(int handle, int looping)
{
    if (handle && g_tml && g_tsf)
    {
        SDL_LockMutex(music_mutex);
        music_playing = 1;
        music_paused = 0;
        music_looping = looping;
        g_tml_current = g_tml;
        g_msec = 0;
        tsf_reset(g_tsf);
        SDL_UnlockMutex(music_mutex);
    }
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
    music_volume = volume / 15.0f;
}

void I_PauseSong(int handle)
{
    if (handle)
    {
        SDL_LockMutex(music_mutex);
        music_paused = 1;
        SDL_UnlockMutex(music_mutex);
    }
}

void I_ResumeSong(int handle)
{
    if (handle)
    {
        SDL_LockMutex(music_mutex);
        music_paused = 0;
        SDL_UnlockMutex(music_mutex);
    }
}

void I_StopSong(int handle)
{
    if (handle)
    {
        SDL_LockMutex(music_mutex);
        music_playing = 0;
        SDL_UnlockMutex(music_mutex);
    }
}

void I_UnRegisterSong(int handle)
{
    if (handle)
    {
        I_StopSong(handle);
        SDL_LockMutex(music_mutex);
        if (g_tml)
        {
            tml_free(g_tml);
            g_tml = NULL;
            g_tml_current = NULL;
        }
        SDL_UnlockMutex(music_mutex);
    }
}

int I_RegisterSong(void *data, int size)
{
    int err;
    MIDI mididata;
    UBYTE *mid;
    int midlen;
    
    if (!g_tsf)
        return 0;
    
    I_UnRegisterSong(1);
    
    memset(&mididata, 0, sizeof(MIDI));
    
    if ((err = mmus2mid((byte *)data, &mididata, 89, 0)))
    {
        fprintf(stderr, "Error loading music: %d\n", err);
        return 0;
    }

    MIDIToMidi(&mididata, &mid, &midlen);
    
    SDL_LockMutex(music_mutex);
    g_tml = tml_load_memory(mid, midlen);
    SDL_UnlockMutex(music_mutex);
    
    free(mid);
    
    if (!g_tml)
    {
        fprintf(stderr, "Failed to load MIDI\n");
        return 0;
    }
    
    return 1;
}

int I_QrySongPlaying(int handle)
{
    int playing = 0;
    if (handle)
    {
        SDL_LockMutex(music_mutex);
        playing = music_playing;
        SDL_UnlockMutex(music_mutex);
    }
    return playing;
}

