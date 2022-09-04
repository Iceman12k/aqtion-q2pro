/*
Copyright (C) 2010 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "sound.h"

#if USE_FIXED_LIBAL
#include "qal/fixed.h"
#else
#include "qal/dynamic.h"
#endif

// translates from AL coordinate system to quake
#define AL_UnpackVector(v)  -v[1],v[2],-v[0]
#define AL_CopyVector(a,b)  ((b)[0]=-(a)[1],(b)[1]=(a)[2],(b)[2]=-(a)[0])

// OpenAL implementation should support at least this number of sources
#define MIN_CHANNELS    16

static ALuint       s_srcnums[MAX_CHANNELS];
static ALboolean    s_loop_points;
static ALboolean    s_source_spatialize;
static int          s_framecount;

static void AL_SoundInfo(void)
{
    Com_Printf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_Printf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_Printf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_Printf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));
    Com_Printf("Number of sources: %d\n", s_numchannels);
}

static bool AL_Init(void)
{
    int i;

    Com_DPrintf("Initializing OpenAL\n");

    if (!QAL_Init()) {
        goto fail0;
    }

    // check for linear distance extension
    if (!qalIsExtensionPresent("AL_EXT_LINEAR_DISTANCE")) {
        Com_SetLastError("AL_EXT_LINEAR_DISTANCE extension is missing");
        goto fail1;
    }

    // generate source names
    qalGetError();
    for (i = 0; i < MAX_CHANNELS; i++) {
        qalGenSources(1, &s_srcnums[i]);
        if (qalGetError() != AL_NO_ERROR) {
            break;
        }
    }

    Com_DPrintf("Got %d AL sources\n", i);

    if (i < MIN_CHANNELS) {
        Com_SetLastError("Insufficient number of AL sources");
        goto fail1;
    }

    s_numchannels = i;

    s_loop_points = qalIsExtensionPresent("AL_SOFT_loop_points");
    s_source_spatialize = qalIsExtensionPresent("AL_SOFT_source_spatialize");

    Com_Printf("OpenAL initialized.\n");
    return true;

fail1:
    QAL_Shutdown();
fail0:
    Com_EPrintf("Failed to initialize OpenAL: %s\n", Com_GetLastError());
    return false;
}

static void AL_Shutdown(void)
{
    Com_Printf("Shutting down OpenAL.\n");

    if (s_numchannels) {
        // delete source names
        qalDeleteSources(s_numchannels, s_srcnums);
        memset(s_srcnums, 0, sizeof(s_srcnums));
        s_numchannels = 0;
    }

    QAL_Shutdown();
}

static sfxcache_t *AL_UploadSfx(sfx_t *s)
{
    ALsizei size = s_info.samples * s_info.width * s_info.channels;
    ALenum format = AL_FORMAT_MONO8 + (s_info.channels - 1) * 2 + (s_info.width - 1);
    ALuint name;

    qalGetError();
    qalGenBuffers(1, &name);
    qalBufferData(name, format, s_info.data, size, s_info.rate);
    if (qalGetError() != AL_NO_ERROR) {
        s->error = Q_ERR_LIBRARY_ERROR;
        return NULL;
    }

    // specify OpenAL-Soft style loop points
    if (s_info.loopstart > 0 && s_loop_points) {
        ALint points[2] = { s_info.loopstart, s_info.samples };
        qalBufferiv(name, AL_LOOP_POINTS_SOFT, points);
    }

    // allocate placeholder sfxcache
    sfxcache_t *sc = s->cache = S_Malloc(sizeof(*sc));
    sc->length = s_info.samples * 1000LL / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->channels = s_info.channels;
    sc->size = size;
    sc->bufnum = name;

    return sc;
}

static void AL_DeleteSfx(sfx_t *s)
{
    sfxcache_t *sc = s->cache;
    if (sc) {
        ALuint name = sc->bufnum;
        qalDeleteBuffers(1, &name);
    }
}

static int AL_GetBeginofs(float timeofs)
{
    return s_paintedtime + timeofs * 1000;
}

static void AL_Spatialize(channel_t *ch)
{
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    // no attenuation = no spatialization
    if (S_IsFullVolume(ch)) {
        VectorCopy(listener_origin, origin);
    } else if (ch->fixed_origin) {
        VectorCopy(ch->origin, origin);
    } else {
        CL_GetEntitySoundOrigin(ch->entnum, origin);
    }

    if (s_source_spatialize) {
        qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, !S_IsFullVolume(ch));
    }

    qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(origin));
}

static void AL_StopChannel(channel_t *ch)
{
    if (!ch->sfx)
        return;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    // stop it
    qalSourceStop(ch->srcnum);
    qalSourcei(ch->srcnum, AL_BUFFER, AL_NONE);
    memset(ch, 0, sizeof(*ch));
}

static void AL_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    ch->srcnum = s_srcnums[ch - s_channels];
    qalGetError();
    qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
    qalSourcei(ch->srcnum, AL_LOOPING, ch->autosound || sc->loopstart >= 0);
    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
    qalSourcef(ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME);
    qalSourcef(ch->srcnum, AL_MAX_DISTANCE, 8192);
    qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * (8192 - SOUND_FULLVOLUME));

    AL_Spatialize(ch);

    // play it
    qalSourcePlay(ch->srcnum);
    if (qalGetError() != AL_NO_ERROR) {
        AL_StopChannel(ch);
    }
}

static void AL_IssuePlaysounds(void)
{
    // start any playsounds
    while (1) {
        playsound_t *ps = s_pendingplays.next;
        if (ps == &s_pendingplays)
            break;  // no more pending sounds
        if (ps->begin > s_paintedtime)
            break;
        S_IssuePlaysound(ps);
    }
}

static void AL_StopAllSounds(void)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;
        AL_StopChannel(ch);
    }
}

static channel_t *AL_FindLoopingSound(int entnum, sfx_t *sfx)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->autosound)
            continue;
        if (entnum && ch->entnum != entnum)
            continue;
        if (ch->sfx != sfx)
            continue;
        return ch;
    }

    return NULL;
}

static void AL_AddLoopSounds(void)
{
    int         i;
    int         sounds[MAX_EDICTS];
    channel_t   *ch, *ch2;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t  *ent;

    if (cls.state != ca_active || sv_paused->integer || !s_ambient->integer)
        return;

    S_BuildSoundList(sounds);

    for (i = 0; i < cl.frame.numEntities; i++) {
        if (!sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        num = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        ent = &cl.entityStates[num];

        ch = AL_FindLoopingSound(ent->number, sfx);
        if (ch) {
            ch->autoframe = s_framecount;
            ch->end = s_paintedtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        // attempt to synchronize with existing sounds of the same type
        ch2 = AL_FindLoopingSound(0, sfx);
        if (ch2) {
            ALfloat offset = 0;
            qalGetSourcef(ch2->srcnum, AL_SAMPLE_OFFSET, &offset);
            qalSourcef(s_srcnums[ch - s_channels], AL_SAMPLE_OFFSET, offset);
        }

        ch->autosound = true;   // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        ch->master_vol = 1.0f;
        ch->dist_mult = SOUND_LOOPATTENUATE;
        ch->end = s_paintedtime + sc->length;

        AL_PlayChannel(ch);
    }
}

static void AL_Update(void)
{
    int         i;
    channel_t   *ch;
    vec_t       orientation[6];

    if (!s_active)
        return;

    s_paintedtime = cl.time;

    // set listener parameters
    qalListener3f(AL_POSITION, AL_UnpackVector(listener_origin));
    AL_CopyVector(listener_forward, orientation);
    AL_CopyVector(listener_up, orientation + 3);
    qalListenerfv(AL_ORIENTATION, orientation);
    qalListenerf(AL_GAIN, s_volume->value);
    qalDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                AL_StopChannel(ch);
                continue;
            }
        } else {
            ALenum state;

            qalGetError();
            qalGetSourcei(ch->srcnum, AL_SOURCE_STATE, &state);
            if (qalGetError() != AL_NO_ERROR || state == AL_STOPPED) {
                AL_StopChannel(ch);
                continue;
            }
        }

#if USE_DEBUG
        if (s_show->integer) {
            ALfloat offset = 0;
            qalGetSourcef(ch->srcnum, AL_SAMPLE_OFFSET, &offset);
            Com_Printf("%d %.1f %.1f %s\n", i, ch->master_vol, offset, ch->sfx->name);
        }
#endif

        AL_Spatialize(ch);         // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    AL_AddLoopSounds();

    AL_IssuePlaysounds();
}

const sndapi_t snd_openal = {
    .init = AL_Init,
    .shutdown = AL_Shutdown,
    .update = AL_Update,
    .activate = S_StopAllSounds,
    .sound_info = AL_SoundInfo,
    .upload_sfx = AL_UploadSfx,
    .delete_sfx = AL_DeleteSfx,
    .get_begin_ofs = AL_GetBeginofs,
    .play_channel = AL_PlayChannel,
    .stop_channel = AL_StopChannel,
    .stop_all_sounds = AL_StopAllSounds,
};
