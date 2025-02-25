//-----------------------------------------------------------------------------
// g_utils.c -- misc utility functions for game module
//
// $Id: g_utils.c,v 1.2 2001/09/28 13:48:34 ra Exp $
//
//-----------------------------------------------------------------------------
// $Log: g_utils.c,v $
// Revision 1.2  2001/09/28 13:48:34  ra
// I ran indent over the sources. All .c and .h files reindented.
//
// Revision 1.1.1.1  2001/05/06 17:31:54  igor_rock
// This is the PG Bund Edition V1.25 with all stuff laying around here...
//
//-----------------------------------------------------------------------------

#include "g_local.h"


void G_ProjectSource (vec3_t point, vec3_t distance, vec3_t forward, vec3_t right,
		 vec3_t result)
{
	result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1];
	result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1];
	result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + distance[2];
}


/*
=============
G_Find

Searches all active entities for the next one that holds
the matching string at fieldofs (use the FOFS() macro) in the structure.

Searches beginning at the edict after from, or the beginning if NULL
NULL will be returned if the end of the list is reached.

=============
*/
edict_t *G_Find (edict_t * from, ptrdiff_t fieldofs, char *match)
{
	char *s;

	if (!from)
		from = g_edicts;
	else
		from++;

	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		s = *(char **) ((byte *) from + fieldofs);
		if (!s)
			continue;
		if (!Q_stricmp (s, match))
			return from;
	}

	return NULL;
}


/*
=================
findradius

Returns entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
edict_t *findradius (edict_t * from, vec3_t org, float rad)
{
	vec3_t eorg;
	int j;

	if (!from)
		from = g_edicts;
	else
		from++;
	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		if (from->solid == SOLID_NOT)
			continue;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (from->s.origin[j] + (from->mins[j] + from->maxs[j]) * 0.5f);

		if (VectorLength (eorg) > rad)
			continue;

		return from;
	}

	return NULL;
}


/*
=============
G_PickTarget

Searches all active entities for the next one that holds
the matching string at fieldofs (use the FOFS() macro) in the structure.

Searches beginning at the edict after from, or the beginning if NULL
NULL will be returned if the end of the list is reached.

=============
*/
#define MAXCHOICES      8

edict_t *G_PickTarget (char *targetname)
{
	edict_t *ent = NULL;
	int num_choices = 0;
	edict_t *choice[MAXCHOICES];

	if (!targetname)
	{
		gi.dprintf ("G_PickTarget called with NULL targetname\n");
		return NULL;
	}

	while (1)
	{
		ent = G_Find (ent, FOFS (targetname), targetname);
		if (!ent)
			break;
		choice[num_choices++] = ent;
		if (num_choices == MAXCHOICES)
			break;
	}

	if (!num_choices)
	{
		gi.dprintf ("G_PickTarget: target %s not found\n", targetname);
		return NULL;
	}

	return choice[rand () % num_choices];
}



void Think_Delay (edict_t * ent)
{
	G_UseTargets (ent, ent->activator);
	G_FreeEdict (ent);
}

/*
==============================
G_UseTargets

the global "activator" should be set to the entity that initiated the firing.

If self.delay is set, a DelayedUse entity will be created that will actually
do the SUB_UseTargets after that many seconds have passed.

Centerprints any self.message to the activator.

Search for (string)targetname in all entities that
match (string)self.target and call their .use function

==============================
*/
void G_UseTargets (edict_t * ent, edict_t * activator)
{
	edict_t *t;

	//
	// check for a delay
	//
	if (ent->delay)
	{
		// create a temp object to fire at a later time
		t = G_Spawn ();
		t->classname = "DelayedUse";
		t->nextthink = level.framenum + ceil( ent->delay * HZ );
		t->think = Think_Delay;
		t->activator = activator;
		if (!activator)
			gi.dprintf ("Think_Delay with no activator\n");
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;
		return;
	}


	//
	// print the message
	//
	if ((ent->message) && !(activator->svflags & SVF_MONSTER))
	{
		gi.centerprintf (activator, "%s", ent->message);
		if (ent->noise_index)
			gi.sound (activator, CHAN_AUTO, ent->noise_index, 1, ATTN_NORM, 0);
		else
			gi.sound (activator, CHAN_AUTO, gi.soundindex ("misc/talk1.wav"), 1,
				ATTN_NORM, 0);
	}

	//
	// kill killtargets
	//
	if (ent->killtarget)
	{
		t = NULL;
		while ((t = G_Find (t, FOFS (targetname), ent->killtarget)) != NULL)
		{
			G_FreeEdict (t);
			if (!ent->inuse)
			{
				gi.dprintf ("entity was removed while using killtargets\n");
				return;
			}
		}
	}

	//
	// fire targets
	//
	if (ent->target)
	{
		t = NULL;
		while ((t = G_Find (t, FOFS (targetname), ent->target)) != NULL)
		{
			// doors fire area portals in a specific way
			if (!Q_stricmp(t->classname, "func_areaportal") &&
			   (!Q_stricmp(ent->classname, "func_door")
			 || !Q_stricmp(ent->classname, "func_door_rotating")))
				continue;

			if (t == ent)
			{
				gi.dprintf ("WARNING: Entity used itself.\n");
			}
			else
			{
				if (t->use)
					t->use(t, ent, activator);
			}
			if (!ent->inuse)
			{
				gi.dprintf ("entity was removed while using targets\n");
				return;
			}
		}
	}
}


/*
=============
TempVector

This is just a convenience function
for making temporary vectors for function calls
=============
*/
// FIXME: re-enabled for bots
float *tv (float x, float y, float z)
{
	static int index;
	static vec3_t vecs[8];
	float *v;

	// use an array so that multiple tempvectors won't collide
	// for a while
	v = vecs[index];
	index = (index + 1) & 7;

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return v;
}


/*
=============
VectorToString

This is just a convenience function
for printing vectors
=============
*/
// char *vtos (const vec3_t v)
// {
// 	static int index;
// 	static char str[8][32];
// 	char *s;

// 	// use an array so that multiple vtos won't collide
// 	s = str[index];
// 	index = (index + 1) & 7;

// 	Q_snprintf (s, 32, "(%i %i %i)", (int) v[0], (int) v[1], (int) v[2]);

// 	return s;
// }


vec3_t VEC_UP = { 0, -1, 0 };
vec3_t MOVEDIR_UP = { 0, 0, 1 };
vec3_t VEC_DOWN = { 0, -2, 0 };
vec3_t MOVEDIR_DOWN = { 0, 0, -1 };

void G_SetMovedir (vec3_t angles, vec3_t movedir)
{
	if (VectorCompare (angles, VEC_UP))
	{
		VectorCopy (MOVEDIR_UP, movedir);
	}
	else if (VectorCompare (angles, VEC_DOWN))
	{
		VectorCopy (MOVEDIR_DOWN, movedir);
	}
	else
	{
		AngleVectors (angles, movedir, NULL, NULL);
	}

	VectorClear (angles);
}


float vectoyaw (vec3_t vec)
{
	float yaw;

	// FIXES HERE FROM 3.20 -FB
	if ( /*vec[YAW] == 0 && */ vec[PITCH] == 0)
	{
		yaw = 0;
		if (vec[YAW] > 0)
			yaw = 90;
		else if (vec[YAW] < 0)
			yaw = -90;
	}
	// ^^^
	else
	{
		yaw = (int) (atan2 (vec[YAW], vec[PITCH]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	return yaw;
}


void vectoangles (vec3_t value1, vec3_t angles)
{
	float forward;
	float yaw, pitch;

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else if (value1[2] < 0)
			pitch = 270;
		else
			pitch = 180;
	}
	else
	{
		// FIXES HERE FROM 3.20  -FB
		// zucc changing casts to floats
		if (value1[0])
			yaw = (float) (atan2 (value1[1], value1[0]) * 180 / M_PI);
		else if (value1[1] > 0)
			yaw = 90;
		else
			yaw = -90;
		// ^^^
		if (yaw < 0)
			yaw += 360;

		forward = sqrtf (value1[0] * value1[0] + value1[1] * value1[1]);
		pitch = (float) (atan2 (value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	angles[PITCH] = 360 - pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}

char *G_CopyString (char *in)
{
	char *out;

	out = gi.TagMalloc (strlen(in) + 1, TAG_LEVEL);
	strcpy (out, in);
	return out;
}


void G_InitEdict (edict_t * e)
{
	e->inuse = true;
	e->classname = "noclass";
	e->gravity = 1.0;
	e->s.number = e - g_edicts;
	e->typeNum = NO_NUM;
	e->svflags = 0;
}

/*
=================
G_Spawn

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *G_Spawn (void)
{
	int i;
	edict_t *e;

	e = &g_edicts[game.maxclients + 1];
	for (i = game.maxclients + 1; i < globals.num_edicts; i++, e++)
	{
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (!e->inuse && (e->freetime < 2 || level.time - e->freetime > 0.5f))
		{
			G_InitEdict (e);
			return e;
		}
	}

	if (i == game.maxentities)
	{
		// Try again without respecting freetime restrictions.
		e = &g_edicts[ game.maxclients + 1 ];
		for( i = game.maxclients + 1; i < game.maxentities; i ++, e ++ )
		{
			if( ! e->inuse )
			{
				G_InitEdict( e );
				return e;
			}
		}

		gi.error ("ED_Alloc: no free edicts");
	}

	globals.num_edicts++;
	G_InitEdict (e);
	return e;
}

// Like G_Spawn, but start at the end and return NULL if nothing is available.
// This is only used for persistent decals, to ensure that if there are too many
// visible entities, the oldest decals are what get omitted from the packet.
edict_t *G_Spawn_Decal( void )
{
	int i;
	edict_t *e;

	// Expand num_edicts to maximum.
	// This is okay because the unused middle entities won't be transmitted.
	while( globals.num_edicts < game.maxentities )
	{
		e = &g_edicts[ globals.num_edicts ];
		if( (e - g_edicts) > (game.maxclients + BODY_QUEUE_SIZE) )
		{
			e->inuse = false;
			e->svflags = SVF_NOCLIENT;
		}
		globals.num_edicts ++;
	}

	e = &g_edicts[ globals.num_edicts - 1 ];
	for( i = globals.num_edicts - 1; i > game.maxclients; i --, e -- )
	{
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if( !e->inuse && (e->freetime < 2 || level.time - e->freetime > 0.5f) )
		{
			G_InitEdict( e );
			return e;
		}
	}

	return NULL;
}

/*
=================
G_FreeEdict

Marks the edict as free
=================
*/
void G_FreeEdict (edict_t * ed)
{
	gi.unlinkentity (ed);		// unlink from world

	if ((ed - g_edicts) <= (game.maxclients + BODY_QUEUE_SIZE))
	{
		//              gi.dprintf("tried to free special edict\n");
		return;
	}

	memset (ed, 0, sizeof (*ed));
	ed->classname = "freed";
	ed->freetime = level.time;
	ed->inuse = false;
	ed->typeNum = NO_NUM;
}


/*
============
G_TouchTriggers

============
*/
void G_TouchTriggers (edict_t * ent)
{
	int i, num;
	edict_t *touch[MAX_EDICTS_OLD], *hit;

	// dead things don't activate triggers!
	if ((ent->client || (ent->svflags & SVF_MONSTER)) && (ent->health <= 0))
		return;

	num = gi.BoxEdicts (ent->absmin, ent->absmax, touch, q_countof(touch), AREA_TRIGGERS);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for (i = 0; i < num; i++)
	{
		hit = touch[i];
		if (!hit->inuse)
			continue;
		if (!hit->touch)
			continue;
		hit->touch (hit, ent, NULL, NULL);
	}
}

/*
============
G_TouchSolids

Call after linking a new trigger in during gameplay
to force all entities it covers to immediately touch it
============
*/
void G_TouchSolids (edict_t * ent)
{
	int i, num;
	edict_t *touch[MAX_EDICTS_OLD], *hit;

	num = gi.BoxEdicts (ent->absmin, ent->absmax, touch, q_countof(touch), AREA_SOLID);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for (i = 0; i < num; i++)
	{
		hit = touch[i];
		if (!hit->inuse)
			continue;
		if (ent->touch)
			ent->touch (hit, ent, NULL, NULL);
		if (!ent->inuse)
			break;
	}
}


size_t G_HighlightStr(char *dst, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t i, len = ret >= size ? size - 1 : ret;

		for (i = 0; i < len; i++)
			dst[i] = src[i] | 0x80;
		dst[i] = 0;
	}

	return ret;
}

/*
==============================================================================

Kill box

==============================================================================
*/

/*
=================
KillBox

Kills all entities that would touch the proposed new positioning
of ent.  Ent should be unlinked before calling this!
=================
*/
qboolean KillBox (edict_t * ent)
{
	trace_t tr;

	while (1)
	{
		tr = gi.trace (ent->s.origin, ent->mins, ent->maxs, ent->s.origin, NULL, MASK_PLAYERSOLID);
		if (!tr.ent)
			break;

		if (tr.ent && tr.ent == ent) break; //rekkie -- prevent bots from killing themselves on first spawn

		// nail it
		T_Damage (tr.ent, ent, ent, vec3_origin, ent->s.origin, vec3_origin,
			100000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);

		// if we didn't kill it, fail
		if (tr.ent->solid)
			return false;
	}

	return true;			// all clear
}

/*
=============
visible

returns 1 if the entity is visible to self, even if not infront ()
=============
*/
qboolean visible(edict_t *self, edict_t *other, int mask)
{
	vec3_t	spot1, spot2, add;
	trace_t	trace;
	int		i;

	VectorCopy(self->s.origin, spot1);
	spot1[2] += self->viewheight;

	VectorCopy(other->s.origin, spot2);
	spot2[2] += other->viewheight;

	PRETRACE();
	for (i = 0; i < 10; i++)
	{
		trace = gi.trace(spot1, vec3_origin, vec3_origin, spot2, self, mask);

		if (trace.fraction == 1.0) {
			POSTTRACE();
			return true;
		}

		if (trace.ent == world && trace.surface && (trace.surface->flags & (SURF_TRANS33 | SURF_TRANS66)))
		{
			mask &= ~MASK_WATER;
			VectorSubtract(trace.endpos, spot1, add);
			VectorNormalize(add);
			VectorMA(trace.endpos, 10, add, spot1);
			continue;
		}

		break;
	}
	POSTTRACE();

	return false;
}

#ifndef NO_BOTS
/*
=============
ai_visible

returns 1 if the entity is visible to self, even if not infront ()
=============
*/
qboolean ai_visible( edict_t *self, edict_t *other )
{
	return visible( self, other, MASK_OPAQUE );
}

/*
=============
infront

returns 1 if the entity is in front (in sight) of self
=============
*/
qboolean infront( edict_t *self, edict_t *other )
{
	vec3_t vec = {0.f,0.f,0.f};
	float dot = 0.f;
	vec3_t forward = {0.f,0.f,0.f};
	
	AngleVectors( self->s.angles, forward, NULL, NULL );
	VectorSubtract( other->s.origin, self->s.origin, vec );
	VectorNormalize( vec );
	dot = DotProduct( vec, forward );
	
	if( dot > 0.3f )
		return true;
	return false;
}
#endif

/*
=============
Toggle Cvars
=============
*/

void disablecvar(cvar_t *cvar, char *msg)
{
	// Cvar is already disabled, do nothing
	if (!cvar->value)
		return;

	if (msg)
		gi.dprintf("DISABLING %s: %s\n", cvar->name, msg);
	else
		gi.dprintf("DISABLING %s\n", cvar->name);

	gi.cvar_forceset(cvar->name, "0");
}

void enablecvar(cvar_t *cvar, char *msg)
{
	// Cvar is already enabled, do nothing
	if (cvar->value)
		return;

	if (msg)
		gi.dprintf("ENABLING %s: %s\n", cvar->name, msg);
	else
		gi.dprintf("ENABLING %s\n", cvar->name);

	gi.cvar_forceset(cvar->name, "1");
}

/*
Supply integer in seconds, calculates number of seconds
based on variable FPS (HZ)
*/
int eztimer(int seconds){
	return (level.framenum + seconds * HZ);
}

float sigmoid(float x) {
    return 1 / (1 + exp(-x));
}

/*
Reverse-lookup, find the edict belonging to the gclient
*/
edict_t* FindEdictByClient(gclient_t* client)
{
    int index = client - game.clients; // Calculate the index of the client in the clients array

    if (index >= 0 && index < game.maxclients)
    {
        return &g_edicts[index + 1]; // g_edicts[0] is the world entity, so players start from g_edicts[1]
    }

    return NULL; // Return NULL if the client is not valid
}