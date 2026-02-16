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
//	System interface for sound
//
//-----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

//=============================
// Audio / Mixer configuration
//=============================

#ifndef DEVICE_RATE
#define DEVICE_RATE 48000
#endif

#ifndef SFX_RATE
#define SFX_RATE 11025
#endif

static int SAMPLECOUNT = 512;
#define NUM_CHANNELS 8

//=============================
// Globals (SFX mixer)
//=============================

static SDL_AudioStream* audio_stream = NULL;
static Sint16* audio_buffer = NULL;
static int audio_buffer_size = 0;

int snd_samplerate = SFX_RATE;
int lengths[NUMSFX];
int audio_fd;

unsigned int channelstep[NUM_CHANNELS];
unsigned int channelstepremainder[NUM_CHANNELS];
unsigned char* channels[NUM_CHANNELS];
unsigned char* channelsend[NUM_CHANNELS];

int channelstart[NUM_CHANNELS];
int channelhandles[NUM_CHANNELS];
int channelids[NUM_CHANNELS];
int steptable[256];
int vol_lookup[128 * 256];
int* channelleftvol_lookup[NUM_CHANNELS];
int* channelrightvol_lookup[NUM_CHANNELS];

//=============================
// TinySoundFont music globals
//=============================

static tsf* g_tsf = NULL;
static tml_message* g_tml = NULL;
static tml_message* g_tml_current = NULL;
static double g_msec = 0.0;
static int music_playing = 0;
static int music_looping = 0;
static int music_paused = 0;
static float music_volume = 1.0f;
static SDL_Mutex* music_mutex = NULL;
static uint8_t bank_msb[16];
static uint8_t bank_lsb[16];

//=============================
// Utilities
//=============================

static void Music_ResetChannelState(void)
{
    memset(bank_msb, 0, sizeof(bank_msb));
    memset(bank_lsb, 0, sizeof(bank_lsb));
}

static int Music_GetBank(int ch)
{
    return ((int)bank_msb[ch] << 7) | (int)bank_lsb[ch];
}

//=============================
// WAD SFX loading
//=============================

void* getsfx(char* sfxname, int* len)
{
    unsigned char* sfx;
    unsigned char* paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    sprintf(name, "ds%s", sfxname);
    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);
    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
    paddedsfx = (unsigned char*)Z_Malloc(paddedsize + 8, PU_STATIC, 0);

    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
        paddedsfx[i] = 128;

    Z_Free(sfx);
    *len = paddedsize;

    return (void*)(paddedsfx + 8);
}

//=============================
// Channel management
//=============================

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

    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful ||
        sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        for (i = 0; i < NUM_CHANNELS; i++)
        {
            if ((channels[i]) && (channelids[i] == sfxid))
            {
                channels[i] = 0;
                break;
            }
        }
    }

    for (i = 0; (i < NUM_CHANNELS) && (channels[i]); i++)
    {
        if (channelstart[i] < oldest)
        {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }
    slot = (i == NUM_CHANNELS) ? oldestnum : i;

    channels[slot] = (unsigned char*)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums)
        handlenums = 100;

    channelhandles[slot] = rc = handlenums++;

    channelstep[slot] = (unsigned int)step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    seperation += 1;
    volume *= 8;

    leftvol = volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume * seperation * seperation) >> 16);

    if (rightvol < 0 || rightvol > 127) I_Error("rightvol out of bounds");
    if (leftvol < 0 || leftvol > 127)   I_Error("leftvol out of bounds");

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];

    channelids[slot] = sfxid;
    return rc;
}

//=============================
// SFX API
//=============================

void I_SetChannels()
{
    int i, j;
    int* steptablemid = steptable + 128;

    for (i = -128; i < 128; i++)
        steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++)
            vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
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
    (void)priority;
    int base_step = steptable[pitch];
    int scaled_step = (int)((long long)base_step * SFX_RATE / DEVICE_RATE);

    id = addsfx(id, vol, scaled_step, sep);
    return id;
}

void I_StopSound(int handle)
{
    (void)handle;
}

int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}

//=============================
// Mixing / Submit
//=============================

void I_SubmitSound(void)
{
    if (!audio_stream || !audio_buffer)
        return;

    unsigned int sample;
    int dl;
    int dr;
    signed short* leftout;
    signed short* rightout;
    signed short* leftend;
    int step;
    int chan;

    memset(audio_buffer, 0, audio_buffer_size);

    if (music_playing && !music_paused && g_tsf && g_tml_current)
    {
        SDL_LockMutex(music_mutex);

        signed short* music_out = audio_buffer;
        int samples_to_render = SAMPLECOUNT;

        while (g_tml_current && g_msec >= g_tml_current->time)
        {
            int ch = g_tml_current->channel;
            switch (g_tml_current->type)
            {
            case TML_PROGRAM_CHANGE:
            {
                int program = g_tml_current->program;
                int bank = Music_GetBank(ch);

                if (ch == 9 && bank == 0) bank = 128;

                tsf_channel_set_presetnumber(g_tsf, ch, program, bank);
                break;
            }
            case TML_NOTE_ON:
                tsf_channel_note_on(g_tsf, ch,
                    g_tml_current->key, g_tml_current->velocity / 127.0f);
                break;
            case TML_NOTE_OFF:
                tsf_channel_note_off(g_tsf, ch, g_tml_current->key);
                break;
            case TML_PITCH_BEND:
                tsf_channel_set_pitchwheel(g_tsf, ch, g_tml_current->pitch_bend);
                break;
            case TML_CONTROL_CHANGE:
            {
                if (g_tml_current->control == 0)      bank_msb[ch] = (uint8_t)g_tml_current->control_value;
                else if (g_tml_current->control == 32) bank_lsb[ch] = (uint8_t)g_tml_current->control_value;
                tsf_channel_midi_control(g_tsf, ch,
                    g_tml_current->control, g_tml_current->control_value);
                break;
            }
            }
            g_tml_current = g_tml_current->next;
        }

        if (!g_tml_current && music_looping)
        {
            g_tml_current = g_tml;
            g_msec = 0.0;
            tsf_reset(g_tsf);
            Music_ResetChannelState();
        }
        else if (!g_tml_current)
        {
            music_playing = 0;
        }

        if (music_playing)
        {
            tsf_render_short(g_tsf, music_out, samples_to_render, 0);

            float gain = (music_volume) * 0.7f;
            int count = samples_to_render * 2;
            for (int i = 0; i < count; i++)
            {
                int v = (int)((float)music_out[i] * gain);
                if (v > 32767) v = 32767;
                else if (v < -32768) v = -32768;
                music_out[i] = (short)v;
            }
            g_msec += (samples_to_render * 1000.0) / (double)DEVICE_RATE;
        }

        SDL_UnlockMutex(music_mutex);
    }
    leftout = audio_buffer;
    rightout = audio_buffer + 1;
    step = 2;
    leftend = leftout + SAMPLECOUNT * step;

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
                channels[chan] += (channelstepremainder[chan] >> 16);
                channelstepremainder[chan] &= (65536 - 1);

                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }
        if (dl > 0x7fff) *leftout = 0x7fff;
        else if (dl < -0x8000) *leftout = -0x8000;
        else *leftout = (signed short)dl;

        if (dr > 0x7fff) *rightout = 0x7fff;
        else if (dr < -0x8000) *rightout = -0x8000;
        else *rightout = (signed short)dr;

        leftout += step;
        rightout += step;
    }

    SDL_PutAudioStreamData(audio_stream, audio_buffer, audio_buffer_size);
}

static void audio_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
{
    (void)userdata; (void)stream; (void)total_amount;
    if (additional_amount > 0)
        I_SubmitSound();
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    (void)handle; (void)vol; (void)sep; (void)pitch; // Unused
}

//=============================
// Shutdown / Init
//=============================

void I_ShutdownSound(void)
{
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

    if (audio_buffer)
    {
        free(audio_buffer);
        audio_buffer = NULL;
        audio_buffer_size = 0;
    }

    if (audio_stream)
    {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
}

void I_InitSound()
{
    SDL_AudioSpec spec;
    int i;

    fprintf(stderr, "I_InitSound: ");

    SDL_zero(spec);
    spec.freq = DEVICE_RATE;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;

    audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        NULL,
        NULL
    );

    if (!audio_stream)
    {
        fprintf(stderr, "SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetAudioStreamGetCallback(audio_stream, audio_callback, NULL);
    audio_buffer_size = SAMPLECOUNT * 2 * sizeof(Sint16);
    audio_buffer = (Sint16*)malloc(audio_buffer_size);
    if (!audio_buffer)
    {
        fprintf(stderr, "Couldn't allocate audio buffer\n");
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
        return;
    }

    fprintf(stderr, " configured audio device at %d Hz, %d frames/slice\n", (int)spec.freq, SAMPLECOUNT);
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
        tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, DEVICE_RATE, -6.0f);

        music_mutex = SDL_CreateMutex();
        tsf_set_max_voices(g_tsf, 192);
    }
    else
    {
        fprintf(stderr, "Warning: No soundfont found, music disabled\n");
    }

    fprintf(stderr, "I_InitSound: ");

    // Pre-cache all SFX
    for (i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link)
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        else
        {
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[(S_sfx[i].link - S_sfx) / sizeof(sfxinfo_t)];
        }
    }

    fprintf(stderr, " pre-cached all sound data\n");
    fprintf(stderr, "I_InitSound: sound module ready\n");
    fprintf(stderr, "I_InitSound: music module ready\n");

    // Start audio stream
    SDL_ResumeAudioStreamDevice(audio_stream);
}

//=============================
// MUSIC API
//=============================

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
        g_msec = 0.0;
        tsf_reset(g_tsf);
        Music_ResetChannelState();
        SDL_UnlockMutex(music_mutex);
    }
}

void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
    music_volume = (float)volume / 15.0f;
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

int I_RegisterSong(void* data, int size)
{
    int err;
    MIDI mididata;
    UBYTE* mid;
    int midlen;

    if (!g_tsf)
        return 0;

    I_UnRegisterSong(1);

    memset(&mididata, 0, sizeof(MIDI));

    if ((err = mmus2mid((byte*)data, &mididata, 89, 0)))
    {
        fprintf(stderr, "Error loading music: %d\n", err);
        return 0;
    }

    MIDIToMidi(&mididata, &mid, &midlen);

    SDL_LockMutex(music_mutex);
    g_tml = tml_load_memory(mid, midlen);
    g_tml_current = g_tml;
    g_msec = 0.0;
    Music_ResetChannelState();
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