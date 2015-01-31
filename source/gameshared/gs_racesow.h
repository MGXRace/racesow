#ifndef __GS_RACESOW_H
#define __GS_RACESOW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs_racetime_s
{
	int timedelta;
	int hour;
	int min;
	int sec;
	int milli;
} rs_racetime_t;

void RS_ResetPjState(int playerNum);
void RS_ResetPsState(int playerNum);
qboolean RS_QueryPjState(int playerNum);
qboolean RS_QueryPsState(int playerNum);
void RS_IncrementWallJumps(int playerNum);
void RS_IncrementDashes(int playerNum);
void RS_IncrementJumps(int playerNum);
void RS_IncrementRockets(int playerNum);
void RS_IncrementPlasma(int playerNum);
void RS_IncrementGrenades(int playerNum);

void RS_Racetime( int timedelta, rs_racetime_t *racetime );

#ifdef __cplusplus
};
#endif

#endif // __GS_RACESOW_H