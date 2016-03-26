#include "g_local.h"

cvar_t *rs_grenade_minKnockback;
cvar_t *rs_grenade_maxKnockback;
cvar_t *rs_grenade_splash;
cvar_t *rs_grenade_speed;
cvar_t *rs_grenade_timeout;
cvar_t *rs_grenade_gravity;
cvar_t *rs_grenade_friction;
cvar_t *rs_grenade_prestep;
cvar_t *rs_grenade_splashfrac;
cvar_t *rs_rocket_minKnockback;
cvar_t *rs_rocket_maxKnockback;
cvar_t *rs_rocket_splash;
cvar_t *rs_rocket_speed;
cvar_t *rs_rocket_prestep;
cvar_t *rs_rocket_antilag;
cvar_t *rs_rocket_splashfrac;
cvar_t *rs_plasma_minKnockback;
cvar_t *rs_plasma_maxKnockback;
cvar_t *rs_plasma_splash;
cvar_t *rs_plasma_speed;
cvar_t *rs_plasma_prestep;
cvar_t *rs_plasma_hack;
cvar_t *rs_plasma_splashfrac;
cvar_t *rs_gunblade_minKnockback;
cvar_t *rs_gunblade_maxKnockback;
cvar_t *rs_gunblade_splash;
cvar_t *rs_gunblade_splashfrac;

/**
 * RS_Init
 * Initializes the racesow specific variables
 */
void RS_Init( void )
{
	rs_grenade_minKnockback = trap_Cvar_Get( "rs_grenade_minKnockback", "1", CVAR_ARCHIVE );
	rs_grenade_maxKnockback = trap_Cvar_Get( "rs_grenade_maxKnockback", "120", CVAR_ARCHIVE );
	rs_grenade_splash = trap_Cvar_Get( "rs_grenade_splash", "170", CVAR_ARCHIVE );
	rs_grenade_speed = trap_Cvar_Get( "rs_grenade_speed", "800", CVAR_ARCHIVE );
	rs_grenade_timeout = trap_Cvar_Get( "rs_grenade_timeout", "1650", CVAR_ARCHIVE );
	rs_grenade_gravity = trap_Cvar_Get( "rs_grenade_gravity", "1.22", CVAR_ARCHIVE );
	rs_grenade_friction = trap_Cvar_Get( "rs_grenade_friction", "0.85", CVAR_ARCHIVE );
	rs_grenade_prestep = trap_Cvar_Get( "rs_grenade_prestep", "24", CVAR_ARCHIVE );
	rs_grenade_splashfrac = trap_Cvar_Get( "rs_grenade_splashfrac", "2.5", CVAR_ARCHIVE );
	rs_rocket_minKnockback = trap_Cvar_Get( "rs_rocket_minKnockback", "1", CVAR_ARCHIVE );
	rs_rocket_maxKnockback = trap_Cvar_Get( "rs_rocket_maxKnockback", "108", CVAR_ARCHIVE );
	rs_rocket_splash = trap_Cvar_Get( "rs_rocket_splash", "120", CVAR_ARCHIVE );
	rs_rocket_speed = trap_Cvar_Get( "rs_rocket_speed", "950", CVAR_ARCHIVE );
	rs_rocket_prestep = trap_Cvar_Get( "rs_rocket_prestep", "10", CVAR_ARCHIVE );
	rs_rocket_antilag = trap_Cvar_Get( "rs_rocket_antilag", "0", CVAR_ARCHIVE );
	rs_rocket_splashfrac = trap_Cvar_Get( "rs_rocket_splashfrac", "1", CVAR_ARCHIVE );
	rs_plasma_minKnockback = trap_Cvar_Get( "rs_plasma_minKnockback", "1", CVAR_ARCHIVE );
	rs_plasma_maxKnockback = trap_Cvar_Get( "rs_plasma_maxKnockback", "24", CVAR_ARCHIVE );
	rs_plasma_splash = trap_Cvar_Get( "rs_plasma_splash", "40", CVAR_ARCHIVE );
	rs_plasma_speed = trap_Cvar_Get( "rs_plasma_speed", "1700", CVAR_ARCHIVE );
	rs_plasma_prestep = trap_Cvar_Get( "rs_plasma_prestep", "0", CVAR_ARCHIVE );
	rs_plasma_hack = trap_Cvar_Get( "rs_plasma_hack", "1", CVAR_ARCHIVE );
	rs_plasma_splashfrac = trap_Cvar_Get( "rs_plasma_splashfrac", "1", CVAR_ARCHIVE );
	rs_gunblade_minKnockback = trap_Cvar_Get( "rs_gunblade_minKnockback", "10", CVAR_ARCHIVE ); // TODO: decide gunblade values
	rs_gunblade_maxKnockback = trap_Cvar_Get( "rs_gunblade_maxKnockback", "60", CVAR_ARCHIVE );
	rs_gunblade_splash = trap_Cvar_Get( "rs_gunblade_splash", "80", CVAR_ARCHIVE );
	rs_gunblade_splashfrac = trap_Cvar_Get( "rs_gunblade_splashfrac", "1.3", CVAR_ARCHIVE );

	RS_InitQuery();
	RS_InitAuth();
}

/**
 * RS_Shutdown
 * Racesow cleanup
 */
void RS_Shutdown( void )
{
	RS_ShutdownQuery();
	RS_ShutdownAuth();
}

/**
 * Racesow thinking, called every game frame
 */
void RS_Think( void )
{
	RS_ThinkAuth();
}

/**
 * RS_removeProjectiles
 * Removes all projectiles for a given player
 * @param owner The player whose projectiles to remove
 */
void RS_removeProjectiles( edict_t *owner )
{
	edict_t *ent;

	for( ent = game.edicts + gs.maxclients; ENTNUM( ent ) < game.numentities; ent++ )
	{
		if( ent->r.inuse && !ent->r.client && ent->r.svflags & SVF_PROJECTILE && ent->r.solid != SOLID_NOT && ent->r.owner == owner )
			G_FreeEdict( ent );
	}
}

/**
 * Clear racetime hud stats for a player
 * @param client Client to clear stats for
 */
void RS_clearHUDStats( gclient_t *client )
{
	const short notset_1 = (short)( ( STAT_NOTSET >> 16 ) & 0xFFFF );
	const short notset_2 = (short)( STAT_NOTSET & 0xFFFF );

	client->ps.stats[STAT_TIME_SELF] = notset_1;
	client->ps.stats[STAT_TIME_BEST] = notset_1;
	client->ps.stats[STAT_TIME_RECORD] = notset_1;
	client->ps.stats[STAT_TIME_ALPHA] = notset_1;
	client->ps.stats[STAT_TIME_BETA] = notset_1;
	client->ps.stats[STAT_TIME_SELF2] = notset_2;
	client->ps.stats[STAT_TIME_BEST2] = notset_2;
	client->ps.stats[STAT_TIME_RECORD2] = notset_2;
	client->ps.stats[STAT_TIME_ALPHA2] = notset_2;
	client->ps.stats[STAT_TIME_BETA2] = notset_2;
}

/**
 * RS_SplashFrac
 * Racesow version of G_SplashFrac by Weqo
 */
void RS_SplashFrac( const vec3_t origin, const vec3_t mins, const vec3_t maxs, const vec3_t point, float maxradius, vec3_t pushdir, float *kickFrac, float *dmgFrac, float splashFrac )
{
	vec3_t boxcenter = { 0, 0, 0 };
	float distance = 0;
	int i;
	float innerradius;
	float outerradius;
	float g_distance;
	float h_distance;

	if( maxradius <= 0 )
	{
		if( kickFrac )
			*kickFrac = 0;
		if( dmgFrac)
			*dmgFrac = 0;

		return;
	}

	innerradius = ( maxs[0] + maxs[1] - mins[0] - mins[1] ) * 0.25;
	outerradius = ( maxs[2] - mins[2] ); // cylinder height

	// find center of the box
	for( i = 0; i < 3; i++ )
		boxcenter[i] = origin[i] + maxs[i] + mins[i];

	// find box radius to explosion origin direction
	VectorSubtract( boxcenter, point, pushdir );

	g_distance = sqrt( pushdir[0]*pushdir[0] + pushdir[1]*pushdir[1] ); // distance on virtual ground
	h_distance = fabs( pushdir[2] );				    // corrected distance in height

	if( ( h_distance <= outerradius / 2 ) || ( g_distance > innerradius ) )
		distance = g_distance - innerradius;

	if( ( h_distance > outerradius / 2 ) || ( g_distance <= innerradius ) )
		distance = h_distance - outerradius / 2;

	if( ( h_distance > outerradius / 2 ) || ( g_distance > innerradius ) )
		distance = sqrt( ( g_distance - innerradius ) * ( g_distance - innerradius ) + ( h_distance - outerradius / 2 ) * ( h_distance - outerradius / 2 ) );

	if( dmgFrac )
	{
		// soft sin curve
		*dmgFrac = sin( DEG2RAD( ( distance / maxradius ) * 80 ) );
		clamp( *dmgFrac, 0.0f, 1.0f );
	}

	if( kickFrac )
	{
		distance = fabs( distance / maxradius );
		clamp( distance, 0.0f, 1.0f );
		*kickFrac = 1.0 - pow( distance, splashFrac );
	}

	VectorSubtract( boxcenter, point, pushdir );
	VectorNormalizeFast( pushdir );
}

/**
 * Print previous mapname for a player
 */
void RS_Cmd_Prevmap_f( edict_t *ent )
{
	cvar_t *prevmap = trap_Cvar_Get( "rs_prevmap", "", CVAR_ARCHIVE );
	G_PrintMsg( ent, prevmap->string );
}
