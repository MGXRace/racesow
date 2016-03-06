#include <vector>

#include "g_local.h"
#include "../qcommon/cjson.h"
#include "../matchmaker/mm_query.h"
#include "../qalgo/base64.h"
#include "../qalgo/sha2.h"

static stat_query_api_t *rs_sqapi;

cvar_t *sv_mm_authkey;
cvar_t *rs_statsEnabled;
cvar_t *rs_statsUrl;
cvar_t *rs_statsId;

void RS_InitQuery( void )
{
	sv_mm_authkey = trap_Cvar_Get( "sv_mm_authkey", "", CVAR_ARCHIVE );
	rs_statsEnabled = trap_Cvar_Get( "rs_statsEnabled", "0", CVAR_ARCHIVE );
	rs_statsUrl = trap_Cvar_Get( "rs_statsUrl", "", CVAR_ARCHIVE );
	rs_statsId = trap_Cvar_Get( "rs_statsId", "", CVAR_ARCHIVE );
	rs_sqapi = trap_GetStatQueryAPI();
	if( !rs_sqapi )
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
}

void RS_ShutdownQuery( void )
{
}

/**
 * Parse a race record from a database query into a race string
 * @param  record The json object of the record
 * @return        String representing the race
 */
static char *RS_ParseRace( cJSON *record )
{
	static char args[1024];
	cJSON *node;
	std::vector<int> argv;
	int cpNum, cpTime;

	// Parse the world record into a string to hand to angelscript
	// "racetime cp1 cp2 cp3... cpN"
	argv.push_back( cJSON_GetObjectItem( record, "time" )->valueint );
	node = cJSON_GetObjectItem( record, "checkpoints" )->child;
	for( ; node; node=node->next )
	{
		cpNum = cJSON_GetObjectItem( node, "number" )->valueint;
		cpTime = cJSON_GetObjectItem( node, "time" )->valueint;
		if( cpNum + 1 >= (int)argv.size() )
			argv.resize( cpNum + 2 );
		argv[cpNum + 1] = cpTime;
	}

	memset( args, 0, sizeof( args ) );
	for(std::vector<int>::iterator it = argv.begin(); it != argv.end(); ++it)
		Q_strncatz( args, va( "%d ", *it ), sizeof( args ) - 1 );

	return args;
}

/**
 * Sign a query with the generated server token
 * @param query The query to sign
 */
static void RS_SignQuery( stat_query_t *query )
{
	int uTime = (int)time( NULL );
	unsigned char digest[SHA256_DIGEST_SIZE];
	char *digest64, *message = va( "%d|%s", uTime, sv_mm_authkey->string );
	size_t outlen;

	// Make the auth token
	sha256( (const unsigned char*)message, strlen( message ), digest );
	digest64 = (char*)base64_encode( digest, (size_t)SHA256_DIGEST_SIZE, &outlen );

	rs_sqapi->SetField( query, "uTime", va( "%d", uTime ) );
	rs_sqapi->SetField( query, "sToken", va( "%d.%s", rs_statsId->integer, digest64 ) );
	free( digest64 );
}

/**
 * AuthNick callback function
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t player to nick check
 */
void RS_AuthNick_Done( stat_query_t *query, bool success, void *customp )
{
	static char simpName[MAX_NAME_CHARS];
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data;
	int playerNum;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		// Query failed, give them benefit of the doubt
		player->failTime = 0;
		player->nickStatus = QSTATUS_SUCCESS;
		return;
	}

	data = ((cJSON*)rs_sqapi->GetRoot( query ))->child;

	// Did they change name before the query returned?
	Q_strncpyz( simpName, COM_RemoveColorTokens( player->client->netname ), sizeof( simpName ) );
	if( Q_stricmp( simpName, data->string ) )
	{
		RS_PlayerUserinfoChanged( player, NULL );
		return;
	}

	// Update players latest nick
	Q_strncpyz( player->last, simpName, sizeof( player->last ) );

	// Is it protected?
	if( data->type == cJSON_True )
	{
		player->failTime = game.realtime + ( 1000 * RS_NICK_TIMEOUT );
		player->thinkTime = game.realtime;
		player->nickStatus = QSTATUS_SUCCESS;
		return;
	}

	// Not protected, deactivate the timer
	player->failTime = 0;
	player->thinkTime = 0;
	player->nickStatus = QSTATUS_SUCCESS;
}

/**
 * Validate a given nickname
 * @param player The player to check nickstatus for
 * @param nick   The simplified nickname to check
 */
void RS_AuthNick( rs_authplayer_t *player, char *nick )
{
	stat_query_t *query;
	char *b64name;

	if( !rs_statsEnabled->integer )
		return;

	// If a player changes nick while a query is inprogress
	// The callback will start another query when finished
	if( player->nickStatus == QSTATUS_PENDING )
		return;

	b64name = (char*)base64_encode( (unsigned char *)nick, strlen( nick ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/nick/%s", rs_statsUrl->string, b64name ), true );
	free( b64name );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_AuthNick_Done, (void*)player );
	rs_sqapi->Send( query );
	player->nickStatus = QSTATUS_PENDING;
	query = NULL;
}

/**
 * AuthMap callback function
 * @param query Query calling this function
 * @param success True on any response
 * @param customp Extra parameters, should be NULL
 * @return void
 */
void RS_AuthMap_Done( stat_query_t *query, bool success, void *customp )
{
	cJSON *data, *node;

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_Printf( "%sError:%s Failed to query map.\nDisabling statistics reporting.", 
					S_COLOR_RED, S_COLOR_WHITE );
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	authmap.id = cJSON_GetObjectItem( data, "id" )->valueint;
	G_Printf( "Map id: %d\n", authmap.id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( node->type != cJSON_Object )
		return;

	G_Gametype_ScoreEvent( NULL, "rs_loadmap", RS_ParseRace( node ) );
}

/**
 * Get map id and world record
 * @return void
 */
void RS_AuthMap( void )
{
	stat_query_t *query;

	if( !rs_statsEnabled->integer )
		return;

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/map/%s", rs_statsUrl->string, authmap.b64name ), true );
	rs_sqapi->SetCallback( query, RS_AuthMap_Done, NULL );

	RS_SignQuery( query );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for report race
 * @return void
 */
void RS_ReportRace_Done( stat_query_t *query, bool success, void *customp )
{
}

/**
 * Report a race to the database
 * @param player      Player who made the record
 * @param rtime       Time of the race
 * @param cp          Checkpoints of the race
 * @param cpNum       Number of checkpoints
 * @param oneliner	  Whether the current oneliner should be removed (in case of a new record)
 */
void RS_ReportRace( rs_authplayer_t *player, int rtime, int *cp, int cpNum, bool oneliner )
{
	stat_query_t *query;
	int i;

	if( !rs_statsEnabled->integer )
		return;

	if( !player->id )
		return;

	// Use cJSON to format the checkpoint array
	cJSON *arr = cJSON_CreateArray();
	for( i = 0; i < cpNum; i++ )
		cJSON_AddItemToArray( arr, cJSON_CreateNumber( cp[i] ) );

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/race/", rs_statsUrl->string ), false );
	rs_sqapi->SetField( query, "pid", va( "%d", player->id ) );
	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );
	rs_sqapi->SetField( query, "time", va( "%d", rtime ) );
	if( oneliner )
		rs_sqapi->SetField( query, "co", "1" );  // new record made: Clear Oneliner.
	else
		rs_sqapi->SetField( query, "co", "0" );
	rs_sqapi->SetField( query, "checkpoints", cJSON_Print( arr ) );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_ReportRace_Done, (void*)player );
	rs_sqapi->Send( query );
	query = NULL;
	cJSON_Delete( arr );
}

/**
 * Report Map data
 * @param tags     Space separated list of tags to add to the map
 * @param oneliner Oneliner message to leave for the map
 * @param force	   report map regardless of playTime
 */
void RS_ReportMap( const char *tags, const char *oneliner, bool force )
{
	char tagset[1024], *token, *b64tags;
	stat_query_t *query;
	cJSON *arr = cJSON_CreateArray();

	if( !rs_statsEnabled->integer )
		return;

	// if no tags/oneliner and playTime smaller than 5 seconds, refuse to avoid spamming the database
	if( !force && tags == NULL && oneliner == NULL && authmap.playTime < 5000 ) {
		return;
	}

	// Make the taglist
	Q_strncpyz( tagset, ( tags ? tags : "" ), sizeof( tagset ) );
	token = strtok( tagset, " " );
	while( token != NULL )
	{
		cJSON_AddItemToArray( arr, cJSON_CreateString( token ) );
		token = strtok( NULL, " " );
	}
	token = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)token, strlen( token ), NULL );

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/map/%s", rs_statsUrl->string, authmap.b64name ), false );
	rs_sqapi->SetField( query, "playTime", va( "%d", authmap.playTime ) );
	rs_sqapi->SetField( query, "races", va( "%d", authmap.races ) );
	rs_sqapi->SetField( query, "tags", b64tags );
	free( b64tags );
	if( oneliner )
		rs_sqapi->SetField( query, "oneliner", oneliner );
	else
		rs_sqapi->SetField( query, "oneliner", "" );

	// Reset the fields
	authmap.playTime = 0;
	authmap.races = 0;

	RS_SignQuery( query );
	rs_sqapi->Send( query );
	cJSON_Delete( arr );
	query = NULL;
}

/**
 * Report player statistics to the database
 * @param player The player to report
 */
void RS_ReportPlayer( rs_authplayer_t *player )
{
	stat_query_t *query;
	char *b64name;

	if( !rs_statsEnabled->integer )
		return;

	// Not authenticated
	if( !player->id )
		return;

	// Form the query
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/player/%s", rs_statsUrl->string, b64name ), false );
	free( b64name );

	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );
	rs_sqapi->SetField( query, "playTime", va( "%d", player->playTime ) );
	rs_sqapi->SetField( query, "races", va( "%d", player->races ) );

	// reset the fields
	player->playTime = 0;
	player->races = 0;

	RS_SignQuery( query );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for ReportNick to notify player of changes
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp Player who updated his nick
 */
void RS_ReportNick_Done( stat_query_t *query, bool success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	int playerNum = (int)( player->client - game.clients );
	cJSON *data = (cJSON*)rs_sqapi->GetRoot( query );

	// invalid response?
	if( !data || rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( &game.edicts[ playerNum + 1 ],
					"%sError: %sFailed to update nickname.\n",
					S_COLOR_RED, S_COLOR_WHITE );

		if( data && data->child )
			G_PrintMsg( &game.edicts[ playerNum + 1 ],
						"%sError: %s%s\n",
						S_COLOR_RED, S_COLOR_WHITE, data->child->valuestring );

		return;
	}

	G_PrintMsg( &game.edicts[ playerNum + 1 ],
				"%sSuccessfully updated your nickname to %s\n",
				S_COLOR_WHITE, data->child->string );
}

/**
 * Update a players registered nickname
 * @param player The player to update
 * @param nick   The nickname to update with
 */
void RS_ReportNick( rs_authplayer_t *player, const char *nick )
{
	stat_query_t *query;
	char *b64name;
	
	if( !rs_statsEnabled->integer )
		return;

	// Not authenticated
	if( !player->id )
		return;

	// Form the query
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/nick/%s", rs_statsUrl->string, b64name ), false );
	free( b64name );

	rs_sqapi->SetField( query, "nick", nick );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_ReportNick_Done, (void*)player );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * QueryPlayer callback handler
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t of the player being queried
 */
void RS_QueryPlayer_Done( stat_query_t *query, bool success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data, *node;
	int playerNum;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	RS_PlayerReset( player );
	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( NULL, "%sError:%s %s%s failed to authenticate as %s\n", 
					S_COLOR_RED, S_COLOR_WHITE, player->client->netname, S_COLOR_WHITE, player->login );
		player->status = QSTATUS_FAILED;
		return;
	}

	data = (cJSON*)rs_sqapi->GetRoot( query );
	player->status = QSTATUS_SUCCESS;
	player->id = cJSON_GetObjectItem( data, "id" )->valueint;
	player->admin = cJSON_GetObjectItem( data, "admin" )->type == cJSON_True;
	Q_strncpyz( player->nick, cJSON_GetObjectItem( data, "simplified" )->valuestring, sizeof( player->nick ) );

	// Update protected nick
	RS_PlayerUserinfoChanged( player, NULL );

	// Notify of login
	G_PrintMsg( NULL, "%s%s authenticated as %s\n", player->client->netname, S_COLOR_WHITE, player->login );
	if( player->admin )
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "You are an admin. Player id: %d\n", player->id );
	else
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "Player id: %d\n", player->id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( node->type != cJSON_Object )
		return;
	G_Gametype_ScoreEvent( player->client, "rs_loadplayer", RS_ParseRace( node ) );
}

/**
 * Fetch racesow data associated with a player
 * @param player Playerauth to authenticate
 */
void RS_QueryPlayer( rs_authplayer_t *player )
{
	stat_query_t *query;
	char *b64name;

	if( !rs_statsEnabled->integer )
		return;

	// Form the query and query parameters
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/player/%s", rs_statsUrl->string, b64name ), true );
	free( b64name );

	rs_sqapi->SetField( query, "mid", va( "%d", authmap.id ) );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryPlayer_Done, (void*)player );
	rs_sqapi->Send( query );
	player->status = QSTATUS_PENDING;
	query = NULL;
}

/**
 * Callback for top
 * @return void
 */
void RS_QueryTop_Done( stat_query_t *query, bool success, void *customp )
{
	int count, playerNum, i, status, indent;
	rs_racetime_t top, racetime, timediff, oldtop, besttop;
	cJSON *data, *node, *player, *tmp, *oldnode, *curnode;
	char *mapname, *name, *simplified;
	const char *oneliner, *error_message, *oldoneliner;
	bool firstoldtime = true, firstnewtime = true, oldtime = false; // topall

	gclient_t *client = (gclient_t *)customp;
	playerNum = (int)( client - game.clients );

	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	status = rs_sqapi->GetStatus( query );
	if( status != 200 )
	{
		if( status == 400 )
		{
			// We assume the response is properly formed
			data = (cJSON*)rs_sqapi->GetRoot( query );
			error_message = cJSON_GetObjectItem( data, "error" )->valuestring;
		} else {
			error_message = "Failed to query database";
		}
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sError:%s %s\n",
					S_COLOR_RED, S_COLOR_WHITE, error_message );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	count = cJSON_GetObjectItem( data, "count" )->valueint;
	mapname = cJSON_GetObjectItem( data, "map" )->valuestring;
	oneliner = va( "\"%s\"", cJSON_GetObjectItem( data, "oneliner" )->valuestring );

	if( strlen(oneliner) == 2 )  // don't print empty oneliner
		oneliner = "";

	// Check whether we received the result from topall command
	tmp = cJSON_GetObjectItem( data, "oldoneliner" );
	if( tmp )
	{
		// Print results for topall
		oldoneliner = va( "\"%s\"", tmp->valuestring );
		if( strlen(oldoneliner) == 2 )  // don't print empty oneliner
			oldoneliner = "";

		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sAll-time top %s%d%s times on map %s%s%s\n",
					S_COLOR_ORANGE, S_COLOR_YELLOW, count, S_COLOR_ORANGE, S_COLOR_YELLOW, mapname, S_COLOR_GREEN );

		// read both new and old top times (if any)
		node = cJSON_GetObjectItem( data, "races" )->child;
		if( node )
			RS_Racetime( cJSON_GetObjectItem( node, "time" )->valueint, &top );

		oldnode = cJSON_GetObjectItem( data, "oldraces" )->child;
		if( oldnode )
			RS_Racetime( cJSON_GetObjectItem( oldnode, "time" )->valueint, &oldtop );

		// select current top time to refer from
		if( node && oldnode )
		{
			if( top.timedelta <= oldtop.timedelta )
				besttop = top;
			else
				besttop = oldtop;
		}
		else
			besttop = oldnode != NULL ? oldtop : top; // assign top by default; we wont enter loop if node is null

		i = 0;
		while( node || oldnode )
		{
			if( node && oldnode )
			{
				// there is both a new and an old time remaining, compare them
				if( cJSON_GetObjectItem( node, "time" )->valueint <=
						cJSON_GetObjectItem( oldnode, "time" )->valueint )
					oldtime = false;  // the new time is faster or equally fast
				else
					oldtime = true;  // the old time is faster
			}
			else if( node )
				oldtime = false;  // only new time(s) remaining
			else
				oldtime = true;  // only old time(s) remaining

			// point curnode to the appropriate node
			curnode = oldtime ? oldnode : node;

			// Calculate the racetime and difftime from top for each record
			RS_Racetime( cJSON_GetObjectItem( curnode, "time" )->valueint, &racetime );
			RS_Racetime( racetime.timedelta - besttop.timedelta, &timediff );
			player = cJSON_GetObjectItem( curnode, "player" );
			name = cJSON_GetObjectItem( player, "name" )->valuestring;
			simplified = cJSON_GetObjectItem( player, "simplified" )->valuestring;

			// add spaces for colorcodes to the indentation level
			indent = 16 + (strlen(name) - strlen(simplified));

			// Print the row; oldtime is printed with grey rank and date
			G_PrintMsg( &game.edicts[ playerNum + 1 ], "%s%2d. %s%-*s %s%02d:%02d.%03d %s+[%02d:%02d.%03d] %s(%s) %s%s%s\n",
				( oldtime ? S_COLOR_GREY : S_COLOR_WHITE ), i + 1, S_COLOR_WHITE,
				indent, name,
				S_COLOR_GREEN, ( racetime.hour * 60 ) + racetime.min, racetime.sec, racetime.milli,
				S_COLOR_YELLOW,
				( timediff.hour * 60 ) + timediff.min, timediff.sec, timediff.milli,
				( oldtime ? S_COLOR_GREY : S_COLOR_WHITE ), cJSON_GetObjectItem( curnode, "created" )->valuestring,
				S_COLOR_YELLOW, ( oldtime && firstoldtime ? oldoneliner : !oldtime && firstnewtime ? oneliner : "" ), S_COLOR_GREEN );

			// increment rank number
			i++;
			if( i == count )
				return;

			// set 'first' boolean to false and traverse linked list for next node
			if( oldtime )
			{
				firstoldtime = false;
				oldnode = oldnode->next;
			}
			else
			{
				firstnewtime = false;
				node = node->next;
			}
		}
	}
	else
	{
		// Print results of a normal top/topold query
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sTop %s%d%s times on map %s%s%s\n",
						S_COLOR_ORANGE, S_COLOR_YELLOW, count, S_COLOR_ORANGE, S_COLOR_YELLOW, mapname, S_COLOR_GREEN );

		node = cJSON_GetObjectItem( data, "races" )->child;
		if( node )
			RS_Racetime( cJSON_GetObjectItem( node, "time" )->valueint, &top );

		for( i = 0; node != NULL; i++, node=node->next )
		{
			// Calculate the racetime and difftime from top for each record
			RS_Racetime( cJSON_GetObjectItem( node, "time" )->valueint, &racetime );
			RS_Racetime( racetime.timedelta - top.timedelta, &timediff );
			player = cJSON_GetObjectItem( node, "player" );
			name = cJSON_GetObjectItem( player, "name" )->valuestring;
			simplified = cJSON_GetObjectItem( player, "simplified" )->valuestring;

			// add spaces for colorcodes to the indentation level
			indent = 16 + (strlen(name) - strlen(simplified));

			// Print the row
			G_PrintMsg( &game.edicts[ playerNum + 1 ], "%s%2d. %-*s %s%02d:%02d.%03d %s+[%02d:%02d.%03d] %s(%s) %s%s%s\n",
				S_COLOR_WHITE, i + 1,
				indent, name,
				S_COLOR_GREEN, ( racetime.hour * 60 ) + racetime.min, racetime.sec, racetime.milli,
				S_COLOR_YELLOW,
				( timediff.hour * 60 ) + timediff.min, timediff.sec, timediff.milli,
				S_COLOR_WHITE, cJSON_GetObjectItem( node, "created" )->valuestring,
				S_COLOR_YELLOW, ( i == 0 ? oneliner : "" ), S_COLOR_GREEN );
		}
	}
}

void RS_QueryTop( gclient_t *client, const char* mapname, int limit, int cmd)
{
	stat_query_t *query;
	char *url,
		*b64name = (char*)base64_encode( (unsigned char *)mapname, strlen( mapname ), NULL );
	int	playerNum = (int)( client - game.clients );

	if( !rs_statsEnabled->integer )
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s No database connected\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// Form the query
	if( cmd == RS_MAP_TOP)
		url = va( "%s/api/race", rs_statsUrl->string );
	else if( cmd == RS_MAP_TOPOLD )
		url = va( "%s/oldapi/race", rs_statsUrl->string );
	else if( cmd == RS_MAP_TOPALL )
		url = va( "%s/api/raceall", rs_statsUrl->string );
	else
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s Unrecognized command\n",
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	query = rs_sqapi->CreateRootQuery( NULL, url, true );
	rs_sqapi->SetField( query, "map", b64name );
	rs_sqapi->SetField( query, "limit", va( "%d", limit ) );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryTop_Done, (void*)client );
	rs_sqapi->Send( query );
	free( b64name );
	query = NULL;
}

void RS_QueryMaps_Done( stat_query_t *query, bool success, void *customp )
{
	edict_t *ent;
	int playerNum, start, i, j;
	cJSON *data, *node, *tag;
	static char tag_string[1024];

	gclient_t *client = (gclient_t *)customp;
	playerNum = (int)( client - game.clients );

	if( playerNum < 0 || playerNum >= gs.maxclients )
		return;

	ent = &game.edicts[ playerNum + 1 ];

	if( rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( ent, "%sError:%s Maplist query failed\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// We assume the response is properly formed
	data = (cJSON*)rs_sqapi->GetRoot( query );
	start = cJSON_GetObjectItem( data, "start" )->valueint;

	node = cJSON_GetObjectItem( data, "maps" )->child;
	for( i = 0; node != NULL; i++, node=node->next )
	{
		// Clear tag string
		memset( tag_string, 0, sizeof( tag_string ) );
		tag = cJSON_GetObjectItem( node, "tags" )->child;

		// Form a string of max 20 tags
		for( j = 0; tag != NULL && j < 20 ; j++, tag=tag->next )
			Q_strncatz( tag_string, va( "%s%s", ( j == 0 ? "" : ", " ), tag->valuestring ),
						sizeof( tag_string ) - 1 );

		// Print the row
		G_PrintMsg( ent, "%s# %d%s: %-25s %s\n",
						S_COLOR_ORANGE, start + i + 1, S_COLOR_WHITE,
						cJSON_GetObjectItem( node, "name" )->valuestring, tag_string );
	}
}

void RS_QueryMaps( gclient_t *client, const char *pattern, const char *tags, int page )
{
	stat_query_t *query;
	char tagset[1024], *token, *b64tags, *b64pattern;
	cJSON *arr = cJSON_CreateArray();
	int	playerNum = (int)( client - game.clients );

	if( !rs_statsEnabled->integer )
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s No database connected\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	// Make the pattern
	b64pattern = (char*)base64_encode( (unsigned char *)pattern, strlen( pattern ), NULL );
	// Make the taglist
	Q_strncpyz( tagset, tags, sizeof( tagset ) );
	token = strtok( tagset, " " );
	while( token != NULL )
	{
		cJSON_AddItemToArray( arr, cJSON_CreateString( token ) );
		token = strtok( NULL, " " );
	}
	token = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)token, strlen( token ), NULL );

	// which page to display?
	page = page == 0 ? 0 : page - 1;

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/map/", rs_statsUrl->string ), true );
	rs_sqapi->SetField( query, "pattern", b64pattern );
	rs_sqapi->SetField( query, "tags", b64tags );
	rs_sqapi->SetField( query, "start", va( "%d", page * RS_MAPLIST_ITEMS ) );
	rs_sqapi->SetField( query, "limit", va( "%d", RS_MAPLIST_ITEMS ) );

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryMaps_Done, (void*)client );
	rs_sqapi->Send( query );
	free( b64pattern );
	free( b64tags );
	query = NULL;
	cJSON_Delete( arr );
}

void RS_QueryRandmap_Done( stat_query_t *query, bool success, void *customp )
{
	char *mapname;
	char **votedata = (char**)customp;
	cJSON *data = (cJSON*)rs_sqapi->GetRoot( query );

	// invalid response?
	if( !data || rs_sqapi->GetStatus( query ) != 200 )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// Did we get any results?
	int count = cJSON_GetObjectItem( data, "count" )->valueint;
	if( !count )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	cJSON *map = cJSON_GetObjectItem( cJSON_GetObjectItem( data, "maps" )->child, "name" );

	// do we have the map or is it the current map?
	if( !trap_ML_FilenameExists( map->valuestring ) || !Q_stricmp( map->valuestring, level.mapname ) )
	{
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// allocate memory for the mapname and store it
	mapname = ( char * ) G_Malloc( MAX_STRING_CHARS );
	Q_strncpyz( mapname, map->valuestring, MAX_STRING_CHARS );

	// Point callvote data to the mapname. It can safely be freed by G_CallVotes_Reset() in g_callvotes.cpp
	*votedata = mapname;
	G_PrintMsg( NULL, "Randmap picked: %s\n", mapname );
}

void RS_QueryRandmap( char* tags[], void *data )
{
	stat_query_t *query;
	char *b64tags;
	cJSON *arr = cJSON_CreateArray();

	// Format the tags
	while( *tags )
		cJSON_AddItemToArray( arr, cJSON_CreateString( *tags++ ) );
	b64tags = cJSON_Print( arr );
	b64tags = (char*)base64_encode( (unsigned char *)b64tags, strlen( b64tags ), NULL );

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/map/", rs_statsUrl->string ), true );
	rs_sqapi->SetField( query, "pattern", "" );
	rs_sqapi->SetField( query, "tags", b64tags );
	rs_sqapi->SetField( query, "rand", "1" );
	
	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryRandmap_Done, data );
	rs_sqapi->Send( query );
	query = NULL;
	free( b64tags );
	cJSON_Delete( arr );
}
