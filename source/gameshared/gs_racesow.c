#include "q_arch.h"
#include "q_math.h"
#include "q_shared.h"
#include "q_comref.h"
#include "q_collision.h"
#include "gs_public.h"

// Prejump / Preshot validation
static int pj_jumps[MAX_CLIENTS] = {0};
static int pj_dashes[MAX_CLIENTS] = {0};
static int pj_walljumps[MAX_CLIENTS] = {0};
static int ps_rockets[MAX_CLIENTS] = {0};
static int ps_plasma[MAX_CLIENTS] = {0};
static int ps_grenades[MAX_CLIENTS] = {0};

/**
 * RS_ResetPjState
 * Fully reset the prejump state for a given player
 * @param playerNum the player's client number
 */
void RS_ResetPjState(int playerNum)
{
	pj_jumps[playerNum] = 0;
	pj_dashes[playerNum] = 0;
	pj_walljumps[playerNum] = 0;
}

/**
 * RS_ResetPsState
 * Reset the preshot state for a given player
 * @param playerNum the player's client number
 */
void RS_ResetPsState(int playerNum)
{
	ps_rockets[playerNum] = 0;
	ps_plasma[playerNum] = 0;
	ps_grenades[playerNum] = 0;
}

/**
 * RS_IncrementRockets
 * Increment the rocket count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementRockets(int playerNum)
{
	ps_rockets[playerNum]++;
}

/**
 * RS_IncrementPlasma
 * Increment the plasma count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementPlasma(int playerNum)
{
	ps_plasma[playerNum]++;
}

/**
 * RS_IncrementGrenades
 * Increment the grenade count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementGrenades(int playerNum)
{
	ps_grenades[playerNum]++;
}

/**
 * RS_IncrementJumps
 * Increment the jump count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementJumps(int playerNum)
{
	pj_jumps[playerNum]++;
}

/**
 * RS_IncrementDashes
 * Increment the dash count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementDashes(int playerNum)
{
	pj_dashes[playerNum]++;
}

/**
 * RS_IncrementWallJumps
 * Increment the dash count for a given player
 * @param playerNum the player's client number
 */
void RS_IncrementWallJumps(int playerNum)
{
	pj_walljumps[playerNum]++;
}

/**
 * RS_QueryPjState
 * Determines if the player has prejumped or not
 * @param playerNum the player's client number
 * @return qtrue if the player has prejumped
 */
qboolean RS_QueryPjState(int playerNum)
{
	if ( pj_jumps[playerNum] > 1 ||
		pj_dashes[playerNum] > 1 ||
		pj_walljumps[playerNum] > 1 )
		return qtrue;
	else
		return qfalse;
}

/**
 * RS_QueryPsState
 * Determines if the player has preshot or not
 * @param playerNum the player's client number
 * @return qtrue if the player has preshot
 */
qboolean RS_QueryPsState(int playerNum)
{
	if ( ps_rockets[playerNum] > 1 ||
		ps_plasma[playerNum] > 20 ||
		ps_grenades[playerNum] > 2 )
		return qtrue;
	else
		return qfalse;
}
		
/**
 * Calculate the racetime of a race in readable units
 * Be sure to free the result when done
 * @param  time     Timedelta in milliseconds
 * @param  racetime Destination racetime
 * @return          Void
 */
void RS_Racetime( int timedelta, rs_racetime_t *racetime )
{
	racetime->timedelta = timedelta;
	racetime->hour = timedelta / 3600000;
	timedelta -= racetime->hour * 3600000;
	racetime->min = timedelta / 60000;
	timedelta -= racetime->min * 60000;
	racetime->sec = timedelta / 1000;
	timedelta -= racetime->sec * 1000;
	racetime->milli = timedelta;
}