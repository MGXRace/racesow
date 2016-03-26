#include <vector>

#include "g_local.h"
#include "../qcommon/cjson.h"
#include "../matchmaker/mm_query.h"
#include "../qalgo/base64.h"
#include "../qalgo/sha2.h"

static const int RS_MAPLIST_ITEMS = 50;		// Number of results per page in maplist
static const int RS_MAPLIST_TAGLEN = 150;	// Maximum number of chars for tags in maplist

static stat_query_api_t *rs_sqapi;

cvar_t *rs_statsEnabled;
cvar_t *rs_statsUrl;
cvar_t *rs_statsToken;

// Helper function to set object fields
static void rs_setString( cJSON *parent, const char *key, const char *value )
{
	cJSON_AddItemToObject( parent, key, cJSON_CreateString( value ) );
}

void RS_InitQuery( void )
{
	rs_statsEnabled = trap_Cvar_Get( "rs_statsEnabled", "0", CVAR_ARCHIVE );
	rs_statsUrl = trap_Cvar_Get( "rs_statsUrl", "", CVAR_ARCHIVE );
	rs_statsToken = trap_Cvar_Get( "rs_statsToken", "", CVAR_ARCHIVE );
	rs_sqapi = trap_GetStatQueryAPI();
	if( !rs_sqapi )
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
}

void RS_ShutdownQuery( void )
{
}

/**
 * Parse a race record from a database query into a race string
 * "racetime cp1 cp2 cp3... cpN"
 * @param  record The json object of the record
 * @return        String representing the race
 */
static char *RS_ParseRace( cJSON *record )
{
	static char args[1024];
	cJSON *node, *leaf;
	std::vector<int> argv;
	int cpNum;

	memset( args, 0, sizeof( args ) );

	// Send an empty string if we don't have a time
	node = cJSON_GetObjectItem( record, "time" );
	if( !node || node->type != cJSON_Number )
		return args;

	Q_strncatz( args, va( "%d ", node->valueint ), sizeof( args ) - 1 );

	// We're done if there are no checkpoints
	node = cJSON_GetObjectItem( record, "checkpoints" );
	if( !node || node->type != cJSON_Object )
		return args;

	for( node = node->child; node; node = node->next )
	{
		// Get the cp's index
		leaf = cJSON_GetObjectItem( node, "number" );
		if( !leaf || leaf->type != cJSON_Number )
			continue;

		// Make sure the cp array is large enough
		cpNum = leaf->valueint;
		if( cpNum >= (int)argv.size() )
			argv.resize( cpNum + 1);

		// Get the cp's time
		leaf = cJSON_GetObjectItem( node, "time" );
		if( !leaf || leaf->type != cJSON_Number )
			continue;

		argv[cpNum] = leaf->valueint;
	}

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
	rs_sqapi->Header( query, "Authorization", va( "Token %s", rs_statsToken->string ) );
}

/**
 * AuthNick callback function
 * @param query   Query calling this function
 * @param success True on any response
 * @param customp rs_authplayer_t player to nick check
 */
void RS_AuthNick_Done( stat_query_t *query, bool success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data, *node;
	int playerNum, status;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
	{
		G_Printf( "RS_AuthNick: Client disconnected\n" );
		return;
	}

	data = (cJSON*)rs_sqapi->GetRoot( query );
	status = rs_sqapi->GetStatus( query );

	if( !data || status != 200 )
	{
		// Query failed, give them benefit of the doubt
		G_Printf( "RS_AuthNick: Query for %s failed with status %d\n", player->last, status );
		player->failTime = 0;
		player->nickStatus = QSTATUS_FAILED;
		return;
	}

	// Innocent until proven guilty
	player->failTime = 0;
	player->thinkTime = 0;
	player->nickStatus = QSTATUS_SUCCESS;

	// Did they change name before the query returned?
	if( Q_stricmp( player->last, COM_RemoveColorTokens( player->client->netname ) ) )
	{
		G_Printf( "RS_AuthNick: %s nick changed since last query, reissuing query\n", player->last );
		RS_PlayerUserinfoChanged( player, NULL );
		return;
	}

	// Check for a valid "results" entry in the response JSON
	data = cJSON_GetObjectItem( data, "results" );
	if( !data || data->type != cJSON_Array )
	{
		G_Printf( "RS_AuthNick: Failed, query for %s got an unexpected response\n", player->last );
		return;
	}

	// Look through the "results" array for any player with a different username
	// There should only be one result in the array
	for( data = data->child; data; data = data->next )
	{
		if( data->type != cJSON_Object )
			continue;

		node = cJSON_GetObjectItem( data, "id" );
		if( node && node->type == cJSON_Number && node->valueint != player->id )
		{
			// Nick belongs to someone else
			G_Printf( "RS_AuthNick: Success, %s is protected\n", player->last );
			player->failTime = game.realtime + ( 1000 * RS_NICK_TIMEOUT );
			player->thinkTime = game.realtime;
			return;
		}
	}

	G_Printf( "RS_AuthNick: Success, %s is validated\n", player->last );
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

	G_Printf( "RS_AuthNick: %s\n", nick );

	// Issue a query for players with simplified name matching nick
	b64name = (char*)base64_encode( (unsigned char *)nick, strlen( nick ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/players/", rs_statsUrl->string ), true );
	rs_sqapi->SetField( query, "simplified", b64name );
	free( b64name );

	// Set this as last attempted nick
	Q_strncpyz( player->last, nick, sizeof( player->last ) );

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
	int status;

	status = rs_sqapi->GetStatus( query );
	if( status < 200 || status > 299 )
	{
		G_Printf( "%sError:%s Failed to query map.\nDisabling statistics reporting.\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
		return;
	}

	data = (cJSON*)rs_sqapi->GetRoot( query );

	node = cJSON_GetObjectItem( data, "id" );
	if( !node || node->type != cJSON_Number )
	{
		G_Printf( "%sError:%s Failed to query map.\nDisabling statistics reporting.\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		trap_Cvar_ForceSet( rs_statsEnabled->name, "0" );
		return;
	}

	authmap.id = node->valueint;
	G_Printf( "RS_AuthMap: Success, map id: %d\n", authmap.id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( !node || node->type != cJSON_Object )
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

	G_Printf( "RS_AuthMap: Querying Map %s\n", authmap.b64name );

	// Form the query
	// Must be a PATCH request to create the map if needed
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/maps/%s/?record", rs_statsUrl->string, authmap.b64name ), false );
	rs_sqapi->SetCallback( query, RS_AuthMap_Done, NULL );

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for report race
 * @return void
 */
void RS_ReportRace_Done( stat_query_t *query, bool success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	int status = rs_sqapi->GetStatus( query );

	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_ReportRace: Failed for %s with status %d\n", player->login, status );
		return;
	}

	G_Printf( "RS_ReportRace: Success %s\n", player->login);
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
	cJSON *data, *checkpoints, *checkpoint;
	int i;

	if( !rs_statsEnabled->integer )
		return;

	if( !player->id )
		return;

	// Form the query
	G_Printf( "RS_ReportRace: Reporting %s %d\n", player->login, rtime );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/races/%d;%d/", rs_statsUrl->string, authmap.id, player->id ), false );

	data = cJSON_CreateObject();
	rs_setString( data, "time", va( "%d", rtime ) );
	rs_setString( data, "playtime_add", va( "%lu", player->mapTime ) );
	rs_setString( data, "races_add", va( "%d", player->mapRaces ) );

	checkpoints = cJSON_CreateArray();
	for( i = 0; i < cpNum; ++i )
	{
		checkpoint = cJSON_CreateObject();
		rs_setString( checkpoint, "number", va( "%d", i ) );
		rs_setString( checkpoint, "time", va( "%d", cp[i] ) );
		cJSON_AddItemToArray( checkpoints, checkpoint );
	}
	cJSON_AddItemToObject( data, "checkpoints", checkpoints );

	// Reset map playtime
	player->mapTime = 0;
	player->mapRaces = 0;

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
	rs_sqapi->SetCallback( query, RS_ReportRace_Done, (void*)player );
	rs_sqapi->SendJson( query, (stat_query_section_t *)data );
	cJSON_Delete( data );
	query = NULL;
}

/**
 * Callback for report map
 * @return void
 */
void RS_ReportMap_Done( stat_query_t *query, bool success, void *customp )
{
	int status = rs_sqapi->GetStatus( query );
	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_ReportMap: Failed with status %d\n", status );
		return;
	}

	G_Printf( "RS_ReportMap: Success\n" );
}

/**
 * Report Map data
 * @param tags     Space separated list of tags to add to the map
 * @param oneliner Oneliner message to leave for the map
 * @param force	   report map regardless of playTime
 */
void RS_ReportMap( const char *tags, const char *oneliner, bool force )
{
	char tagset[1024], *token;
	stat_query_t *query;
	stat_query_section_t *section;

	if( !rs_statsEnabled->integer )
		return;

	// if no tags/oneliner and playTime smaller than 5 seconds, refuse to avoid spamming the database
	if( !force && tags == NULL && oneliner == NULL && authmap.playTime < 5000 ) {
		return;
	}

	// Form the query
	G_Printf( "RS_ReportMap: Reporting\n" );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/maps/%s/", rs_statsUrl->string, authmap.b64name ), false );
	rs_sqapi->SetField( query, "playtime_add", va( "%d", authmap.playTime ) );
	rs_sqapi->SetField( query, "races_add", va( "%d", authmap.races ) );
	if( oneliner )
		rs_sqapi->SetField( query, "oneliner", oneliner );

	// Make the taglist
	section = rs_sqapi->CreateArray( query, 0, "tags_add" );
	Q_strncpyz( tagset, ( tags ? tags : "" ), sizeof( tagset ) );
	for( token = strtok( tagset, " " ); token != NULL; token = strtok( NULL, " " ) )
	{
		rs_sqapi->AddArrayString( section, token );
	}

	// Reset the fields
	authmap.playTime = 0;
	authmap.races = 0;

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
	// TODO - this segfaults if the callback is called after the mapchange occurs
	// rs_sqapi->SetCallback( query, RS_ReportMap_Done, NULL );
	rs_sqapi->Send( query );
	query = NULL;
}

/**
 * Callback for report player
 * @return void
 */
void RS_ReportPlayer_Done( stat_query_t *query, bool success, void *customp )
{
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	int status = rs_sqapi->GetStatus( query );
	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_ReportPlayer: Failed for %s with status %d\n", player->login, status );
		return;
	}

	G_Printf( "RS_ReportPlayer: Success %s\n", player->login );
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
	G_Printf( "RS_ReportPlayer: Reporting %s\n", player->login );
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/players/%s/", rs_statsUrl->string, b64name ), false );
	free( b64name );

	rs_sqapi->SetField( query, "playtime_add", va( "%lu", player->playTime ) );
	rs_sqapi->SetField( query, "races_add", va( "%d", player->playRaces ) );

	// reset the fields
	player->playTime = 0;
	player->playRaces = 0;

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
	// TODO - this segfaults if the callback is called after the mapchange occurs
	// rs_sqapi->SetCallback( query, RS_ReportPlayer_Done, (void*)player );
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
	int playerNum, status;
	rs_authplayer_t *player = ( rs_authplayer_t* )customp;
	cJSON *data = (cJSON*)rs_sqapi->GetRoot( query );

	playerNum = (int)( player->client - game.clients );
	status = rs_sqapi->GetStatus( query );

	// Status code is enough to determine success
	if( !data || status < 200 || status > 299 )
	{
		G_Printf( "RS_ReportNick: Failed, could not update %s\n", player->login );
		G_PrintMsg( &game.edicts[ playerNum + 1 ],
					"%sError: %sThat name is already taken.\n",
					S_COLOR_RED, S_COLOR_WHITE );

		return;
	}

	G_Printf( "RS_ReportNick: Success, updated %s\n", player->login );
	G_PrintMsg( &game.edicts[ playerNum + 1 ], "%sSuccessfully updated your nickname\n", S_COLOR_WHITE );
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
	G_Printf( "RS_ReportNick: Updating %s's protectednick to %s\n", player->login, nick );
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/players/%s/", rs_statsUrl->string, b64name ), false );
	free( b64name );

	rs_sqapi->SetField( query, "simplified", nick );

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
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
	int playerNum, status;

	// Did they disconnect?
	playerNum = (int)( player->client - game.clients );
	if( playerNum < 0 || playerNum >= gs.maxclients )
	{
		G_Printf( "RS_QueryPlayer: Client disconnected\n" );
		return;
	}

	RS_PlayerReset( player );
	status = rs_sqapi->GetStatus( query );
	data = (cJSON*)rs_sqapi->GetRoot( query );

	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_QueryPlayer: Failed, query for %s failed with status %d\n", player->login, status );
		G_PrintMsg( NULL, "%sError:%s Failed to authenticate %s\n", S_COLOR_RED, S_COLOR_WHITE, player->login );
		player->status = QSTATUS_FAILED;
		return;
	}

	G_Printf( "RS_QueryPlayer: Success, %s authenticated\n", player->login );
	player->status = QSTATUS_SUCCESS;

	node = cJSON_GetObjectItem( data, "id" );
	if( node && node->type == cJSON_Number )
		player->id = node->valueint;

	node = cJSON_GetObjectItem( data, "admin" );
	if( node && node->type == cJSON_True )
		player->admin = true;

	node = cJSON_GetObjectItem( data, "simplified" );
	if( node && node->type == cJSON_String )
		Q_strncpyz( player->nick, node->valuestring, sizeof( player->nick ) );

	// Update protected nick
	RS_PlayerUserinfoChanged( player, NULL );

	// Notify of login
	// TODO - Delay these, atm player doesn't see because they print while still connecting
	G_PrintMsg( NULL, "%s%s authenticated as %s\n", player->client->netname, S_COLOR_WHITE, player->login );
	if( player->admin )
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "You are an admin. Player id: %d\n", player->id );
	else
		G_PrintMsg( &game.edicts[ playerNum + 1 ], "Player id: %d\n", player->id );

	// Check for a world record
	node = cJSON_GetObjectItem( data, "record" );
	if( !node || node->type != cJSON_Object )
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
	// This has to be a PATCH request, since we are potentially creating a
	// new model.
	G_Printf( "RS_QueryPlayer: Querying player %s\n", player->login );
	b64name = (char*)base64_encode( (unsigned char *)player->login, strlen( player->login ), NULL );
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/players/%s/?mid=%d", rs_statsUrl->string, b64name, authmap.id ), false );
	free( b64name );

	RS_SignQuery( query );
	rs_sqapi->CustomRequest( query, "PATCH" );
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
		url = va( "%s/api/races", rs_statsUrl->string );
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

typedef struct 
{
	gclient_t *client;			/**< Client who requested maps */
	int page;					/**< page number requested */
} rs_querymap_cbdata_t;

static char *rs_querymap_tagstring( cJSON *tags )
{
	static char tag_string[RS_MAPLIST_TAGLEN];
	cJSON* tag;

	memset( tag_string, 0, sizeof( tag_string ) );

	if( !tags || tags->type != cJSON_Array )
		return tag_string;

	for( tag = tags->child; tag && strlen( tag_string ) < sizeof( tag_string ) - 1; tag = tag->next )
	{
		if( tag->type != cJSON_String )
			continue;

		Q_strncatz( tag_string, tag->valuestring, sizeof( tag_string ) - 1 );
		Q_strncatz( tag_string, " ", sizeof( tag_string ) - 1 );
	}

	return tag_string;
}

void RS_QueryMaps_Done( stat_query_t *query, bool success, void *customp )
{
	edict_t *ent;
	gclient_t *client;
	int page, playerNum, status, i;
	cJSON *data, *node;

	// Unpack and free cbdata ASAP
	rs_querymap_cbdata_t *cbdata = (rs_querymap_cbdata_t *)customp;
	client = cbdata->client;
	page = cbdata->page;
	G_Free( cbdata );

	playerNum = (int)( client - game.clients );
	ent = &game.edicts[ playerNum + 1 ];
	status = rs_sqapi->GetStatus( query );

	if( playerNum < 0 || playerNum >= gs.maxclients )
	{
		G_Printf( "RS_QueryMaps: Client disconnected\n" );
		return;
	}

	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_QueryMaps: Failed with status %d\n", status );
		G_PrintMsg( ent, "%sError:%s Maplist query failed\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	data = (cJSON*)rs_sqapi->GetRoot( query );
	data = cJSON_GetObjectItem( data, "results" );
	if( !data || data->type != cJSON_Array )
	{
		G_Printf( "RS_QueryMaps: Failed, unexpected response\n" );
		return;
	}

	G_Printf( "RS_QueryMaps: Success\n", status );
	i = ( page - 1 ) * RS_MAPLIST_ITEMS;
	data = data->child;
	while( data )
	{
		node = cJSON_GetObjectItem( data, "name" );
		if( node && node->type == cJSON_String )
			G_PrintMsg( ent, "%s# %d%s: %-25s %s\n",
				S_COLOR_ORANGE, i + 1, S_COLOR_WHITE, node->valuestring,
				rs_querymap_tagstring( cJSON_GetObjectItem( data, "tags" ) ) );

		data = data->next;
		++i;
	}
}

void RS_QueryMaps( gclient_t *client, const char *pattern, const char *tags, int page )
{
	stat_query_t *query;
	rs_querymap_cbdata_t *cbdata;
	char tagset[1024], *token, *b64pattern;
	int	playerNum = (int)( client - game.clients );

	if( !rs_statsEnabled->integer )
	{
		G_PrintMsg( &game.edicts[playerNum +1], "%sError:%s No database connected\n", 
					S_COLOR_RED, S_COLOR_WHITE );
		return;
	}

	G_Printf( "RS_QueryMaps: %s\n", client->netname );

	// Make callback data
	cbdata = (rs_querymap_cbdata_t *)G_Malloc( sizeof( rs_querymap_cbdata_t ) );
	cbdata->client = client;
	cbdata->page = page < 1 ? 1 : page;

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/maps/", rs_statsUrl->string ), true );
	rs_sqapi->SetField( query, "page", va( "%d", cbdata->page ) );

	// Map pattern
	if( pattern )
	{
		b64pattern = (char*)base64_encode( (unsigned char *)pattern, strlen( pattern ), NULL );
		rs_sqapi->SetField( query, "pattern", b64pattern );
		free( b64pattern );
	}

	// Map tags
	if( tags )
	{
		Q_strncpyz( tagset, tags, sizeof( tagset ) );
		for( token = strtok( tagset, " " ); token != NULL; token = strtok( NULL, " " ) )
		{
			rs_sqapi->SetField( query, "t", token );
		}
	}

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryMaps_Done, (void*)cbdata );
	rs_sqapi->Send( query );
	query = NULL;
}

void RS_QueryRandmap_Done( stat_query_t *query, bool success, void *customp )
{
	cJSON *data, *node;
	char *mapname;
	char **votedata = (char**)customp;
	int status = rs_sqapi->GetStatus( query );
	data = (cJSON*)rs_sqapi->GetRoot( query );

	// invalid response?
	if( status < 200 || status > 299 )
	{
		G_Printf( "RS_QueryRandmap: Failed with status %d\n", status );
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	// Did we get any results?
	data = cJSON_GetObjectItem( data, "results" );
	if( !data || data->type != cJSON_Array )
	{
		G_Printf( "RS_QueryRandmap: Failed, no results\n" );
		G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
		G_CallVotes_Reset();
		return;
	}

	for( data = data->child; data; data = data->next )
	{
		if( data->type != cJSON_Object )
			continue;

		node = cJSON_GetObjectItem( data, "name" );
		if( !node || node->type != cJSON_String )
			continue;

		// do we have the map or is it the current map?
		if( !trap_ML_FilenameExists( node->valuestring ) || !Q_stricmp( node->valuestring, level.mapname ) )
			continue;

		// Found a map, set vote data and return
		// mapname will be freed by G_CallVotes_Reset() in g_callvotes.cpp
		mapname = (char *)G_Malloc( MAX_STRING_CHARS );
		Q_strncpyz( mapname, node->valuestring, MAX_STRING_CHARS );
		*votedata = mapname;
		G_PrintMsg( NULL, "Randmap picked: %s\n", mapname );
		return;
	}

	G_PrintMsg( NULL, "Invalid map picked, vote canceled\n" );
	G_CallVotes_Reset();
}

/**
 * Fetch a page from maplist in random order for the callvote
 * @param tags Argv from the callvote command
 * @param data Data store for the callvote
 */
void RS_QueryRandmap( char* tags[], void *data )
{
	stat_query_t *query;

	// Form the query
	query = rs_sqapi->CreateRootQuery( NULL, va( "%s/api/maps/", rs_statsUrl->string ), true );
	rs_sqapi->SetField( query, "rand", "" );

	// Map tags
	while( *tags )
	{
		rs_sqapi->SetField( query, "t", *tags++ );
	}

	RS_SignQuery( query );
	rs_sqapi->SetCallback( query, RS_QueryRandmap_Done, data );
	rs_sqapi->Send( query );
	query = NULL;
}
