#include "g_local.h"
#include "../qalgo/base64.h"

rs_authmap_t authmap;
rs_authplayer_t *authplayers;

void RS_InitAuth( void )
{
	authplayers = ( rs_authplayer_t* )G_Malloc( gs.maxclients * sizeof( authplayers[0] ) );
	authmap.b64name = (char*)base64_encode( (unsigned char *)level.mapname, strlen( level.mapname ), NULL );

	// Authenticate the map
	RS_AuthMap();
}

void RS_ShutdownAuth( void )
{
	RS_ReportMap( NULL, NULL, true );
	G_Free( authplayers );
	free( authmap.b64name );
}

void RS_ThinkAuth( void )
{
	rs_authplayer_t *player;
	edict_t *ent;
	static char simplified[MAX_NAME_CHARS];
	int remaining, playerNum;
	bool maptime = false;

	for( player = authplayers; player < authplayers + gs.maxclients; player++ )
	{
		if( !player->client )
			continue;

		if( player->client->team != TEAM_SPECTATOR )
		{
			player->playTime += game.frametime;
			// If map's playtime hasn't been updated yet with elapsed time since previous frame, do it now
			if( !maptime )
			{
				authmap.playTime += game.frametime;
				maptime = true; // maptime is only updated once for >0 players
			}
		}

		// auth hud stat
		player->client->ps.stats[STAT_AUTH_STATE] = player->id == 0 ? STAT_NOTSET :
		                                            player->admin ? 2 : 1;

		// Protected nick status
		if( player->thinkTime && player->thinkTime < game.realtime )
		{
			remaining = ( 500 + (int)player->failTime - (int)game.realtime ) / 1000;
			playerNum = (int)( player->client - game.clients );
			ent = PLAYERENT( playerNum );
			Q_strncpyz( simplified, COM_RemoveColorTokens( player->client->netname ), sizeof( simplified ) );

			if( !Q_stricmp( simplified, player->nick ) )
			{
				// Are they authd with this nickname?
				player->failTime = 0;
				player->thinkTime = 0;
			}
			else if( remaining < 1 && ent )
			{
				// Timed out, rename the client
				Info_SetValueForKey( player->client->userinfo, "name", "Player" );
				ClientUserinfoChanged( ent, player->client->userinfo );
				player->thinkTime = 0;
				player->failTime = 0;
			}
			else
			{
				// Send a warning and set next think time
				if( player->status != QSTATUS_PENDING )
				{
					G_PrintMsg( &game.edicts[playerNum + 1], "%sWarning: %s%s%s is protected, please change your name within %d seconds\n",
						S_COLOR_ORANGE, S_COLOR_WHITE, player->client->netname, S_COLOR_WHITE, remaining );

					if( remaining > 5 )
						player->thinkTime = game.realtime + 5000;
					else
						player->thinkTime = game.realtime + 1000;
				}
				else
				{
					// Waiting on auth query, delay think 1 second
					player->failTime += 1000;
					player->thinkTime = game.realtime + 1000;
				}
			}
		}
	}
}

/**
 * Check if a player already failed a validation with this name
 * @param client  The client to check
 * @param name    The name the client wants to use
 * @return        True if the player is allowed to rename to 'name'
 */
bool RS_SetName( gclient_t *client, const char *name )
{
	rs_authplayer_t *player;
	static char simpNew[MAX_NAME_CHARS];

	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return false;

	player = &authplayers[playerNum];
	Q_strncpyz( simpNew, COM_RemoveColorTokens( name ), sizeof( simpNew ) );

	// Player authed as this nick, give them permission
	if( !Q_stricmp( simpNew, player->nick ) )
		return true;

	// Dont let a player change to the last name they queried
	return Q_stricmp( simpNew, player->last ) != 0;
}

/**
 * Print playtime for the given playtime on this map
 * @param client  The client of which to print the playtime
 */
void RS_Playtime ( gclient_t *client ) {
	rs_authplayer_t *player;

	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients ) {
		return;
	}

	player = &authplayers[playerNum];
	G_PrintMsg( &game.edicts[playerNum + 1], "player->playtime: %d\t authmap.playtime: %d\n", player->playTime, authmap.playTime );
}

/**
 * Print player info for debugging
 */
static void RS_PlayerInfo( rs_authplayer_t *player )
{
	G_Printf( "client: %d\n", player->client );
	G_Printf( "status: %d\n", player->status );
	G_Printf( "nickStatus: %d\n", player->nickStatus );
	G_Printf( "nick: %s\n", player->nick );
	G_Printf( "last: %s\n", player->last );
	G_Printf( "admin: %d\n", player->admin ? 1 : 0 );
	G_Printf( "id: %d\n", player->id );
	G_Printf( "failTime: %d\n", player->failTime );
	G_Printf( "playTime: %d\n", player->playTime );
	G_Printf( "races: %d\n", player->races );
}

/**
 * Setup the auth info for a player who just connected and entered the game
 * @param client Client who connected
 */
void RS_PlayerEnter( gclient_t *client )
{
	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;

	rs_authplayer_t *player = &authplayers[playerNum];
	memset( player, 0, sizeof( *player ) );
	RS_PlayerReset( player );
	player->client = client;

	// Send first nick query
	RS_PlayerUserinfoChanged( player, NULL );
}


/**
 * Destroy the authinfo for a player who just disconnected
 * @param client Client who disconnected
 */
void RS_PlayerDisconnect( gclient_t *client )
{
	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;

	rs_authplayer_t *player = &authplayers[playerNum];
	RS_ReportPlayer( player );
	memset( player, 0, sizeof( rs_authplayer_t ) );
}

/**
 * Finds rs_authplayer_t associated to the client and writes playtime to db
 * @param client Client whose playtime is being written to db
 */
void RS_PlayerUpdatePlaytime( gclient_t *client )
{
	int playerNum = (int)( client - game.clients );
	if( playerNum < 0 && playerNum >= gs.maxclients )
		return;

	rs_authplayer_t *player = &authplayers[playerNum];
	RS_ReportPlayer( player );
}

/**
 * Reset the auth specific fields on a player
 * @param player The player to reset
 */
void RS_PlayerReset( rs_authplayer_t *player )
{
	// update statistics if needed
	RS_ReportPlayer( player );

	// Clear all the stats
	player->id = 0;
	player->status = QSTATUS_NONE;
	player->admin = false;
	player->playTime = 0;
	player->races = 0;
	player->nick[0] = '\0';
}

/**
 * Player's userinfo changed
 * Check for nickname changes and update protectednick
 * @param player  Player who changed info
 * @param oldname Name before the userinfo change
 */
void RS_PlayerUserinfoChanged( rs_authplayer_t *player, char *oldname )
{
	char simpOld[MAX_NAME_CHARS], simpNew[MAX_NAME_CHARS];
	int num;
	char *login;

	if( !player->client )
		return;

	// Player mm login came back, time to send rs queries?
	if( player->client->mm_session > 0 && player->status == QSTATUS_NONE )
	{
		login = Info_ValueForKey( player->client->userinfo, "cl_mm_login" );
		if( login && strlen( login ) )
		{
			Q_strncpyz( player->login, login, strlen( login ) + 1 );
			RS_QueryPlayer( player );
		}
	}

	Q_strncpyz( simpNew, COM_RemoveColorTokens( player->client->netname ), sizeof( simpNew ) );
	Q_strlwr( simpNew );

	// Did they actually change nick?
	if( oldname )
	{
		Q_strncpyz( simpOld, COM_RemoveColorTokens( oldname ), sizeof( simpOld ) ); 
		if( !Q_stricmp( simpOld, simpNew ) )
			return;
	}

	if( !Q_stricmp( simpNew, player->nick ) ||		// Are they already authd with this nickname?
		!Q_stricmp( simpNew, "player" )	||			// Are they named player?
		sscanf( simpNew, "player(%d)", &num ) )
	{
		player->failTime = 0;
		player->thinkTime = 0;
		return;
	}

	// Not authenticated with new name, check if name is protected
	RS_AuthNick( player, simpNew );
}
