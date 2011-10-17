// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
// DESCRIPTION:
//	DOOM Network game communication and protocol,
//	all OS independend parts.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>

#include "doomfeatures.h"

#include "d_main.h"
#include "m_argv.h"
#include "m_menu.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "g_game.h"
#include "doomdef.h"
#include "doomstat.h"
#include "w_checksum.h"

#include "deh_main.h"

#include "d_loop.h"

ticcmd_t *netcmds;

// Called when a player leaves the game

static void PlayerQuitGame(player_t *player)
{
    static char exitmsg[80];
    unsigned int player_num;

    player_num = player - players;

    // Do this the same way as Vanilla Doom does, to allow dehacked
    // replacements of this message

    strncpy(exitmsg, DEH_String("Player 1 left the game"), sizeof(exitmsg));
    exitmsg[sizeof(exitmsg) - 1] = '\0';

    exitmsg[7] += player_num;

    playeringame[player_num] = false;
    players[consoleplayer].message = exitmsg;

    // TODO: check if it is sensible to do this:

    if (demorecording) 
    {
        G_CheckDemoStatus ();
    }
}

static void RunTic(ticcmd_t *cmds, boolean *ingame)
{
    extern boolean advancedemo;
    unsigned int i;

    // Check for player quits.

    for (i = 0; i < MAXPLAYERS; ++i)
    {
        if (!demoplayback && playeringame[i] && !ingame[i])
        {
            PlayerQuitGame(&players[i]);
        }
    }

    netcmds = cmds;

    // check that there are players in the game.  if not, we cannot
    // run a tic.

    if (advancedemo)
        D_DoAdvanceDemo ();

    M_Ticker();
    G_Ticker();
}

static void NullMenuTicker()
{
    // no-op.
}

static loop_interface_t strife_loop_interface = {
    D_ProcessEvents,
    G_BuildTiccmd,
    RunTic,
    NullMenuTicker
};


// Load game settings from the specified structure and 
// set global variables.

static void LoadGameSettings(net_gamesettings_t *settings,
                             net_connect_data_t *connect_data)
{
    unsigned int i;

    deathmatch = settings->deathmatch;
    ticdup = settings->ticdup;
    startepisode = settings->episode;
    startmap = settings->map;
    startskill = settings->skill;
    startloadgame = settings->loadgame;
    lowres_turn = settings->lowres_turn;
    nomonsters = settings->nomonsters;
    fastparm = settings->fast_monsters;
    respawnparm = settings->respawn_monsters;
    timelimit = settings->timelimit;

    if (lowres_turn)
    {
        printf("NOTE: Turning resolution is reduced; this is probably "
               "because there is a client recording a Vanilla demo.\n");
    }

    if (!connect_data->drone)
    {
        consoleplayer = settings->consoleplayer;
    }
    else
    {
        consoleplayer = 0;
    }
    
    for (i=0; i<MAXPLAYERS; ++i) 
    {
        playeringame[i] = i < settings->num_players;
    }
}

// Save the game settings from global variables to the specified
// game settings structure.

static void SaveGameSettings(net_gamesettings_t *settings,
                             net_connect_data_t *connect_data)
{
    // Fill in game settings structure with appropriate parameters
    // for the new game

    settings->deathmatch = deathmatch;
    settings->episode = startepisode;
    settings->map = startmap;
    settings->skill = startskill;
    settings->loadgame = startloadgame;
    settings->gameversion = gameversion;
    settings->nomonsters = nomonsters;
    settings->fast_monsters = fastparm;
    settings->respawn_monsters = respawnparm;
    settings->timelimit = timelimit;

    settings->lowres_turn = M_CheckParm("-record") > 0
                         && M_CheckParm("-longtics") == 0;

    connect_data->drone = false;

    //!
    // @category net
    //
    // Run as the left screen in three screen mode.
    //

    if (M_CheckParm("-left") > 0)
    {
        viewangleoffset = ANG90;
        connect_data->drone = true;
    }

    //! 
    // @category net
    //
    // Run as the right screen in three screen mode.
    //

    if (M_CheckParm("-right") > 0)
    {
        viewangleoffset = ANG270;
        connect_data->drone = true;
    }

    //
    // Connect data
    //

    // Game type fields:

    connect_data->gamemode = gamemode;
    connect_data->gamemission = gamemission;

    // Are we recording a demo? Possibly set lowres turn mode

    connect_data->lowres_turn = settings->lowres_turn;

    // Read checksums of our WAD directory and dehacked information

    W_Checksum(connect_data->wad_md5sum);
    DEH_Checksum(connect_data->deh_md5sum);

    connect_data->is_freedoom = 0;
}

void D_InitSinglePlayerGame(net_gamesettings_t *settings)
{
    // default values for single player

    settings->consoleplayer = 0;
    settings->num_players = 1;

    netgame = false;

    //!
    // @category net
    //
    // Start the game playing as though in a netgame with a single
    // player.  This can also be used to play back single player netgame
    // demos.
    //

    if (M_CheckParm("-solo-net") > 0)
    {
        netgame = true;
    }
}

//
// D_CheckNetGame
// Works out player numbers among the net participants
//
extern	int			viewangleoffset;

void D_CheckNetGame (void)
{
    net_connect_data_t connect_data;
    net_gamesettings_t settings;

    D_RegisterLoopCallbacks(&strife_loop_interface);

    // Call D_QuitNetGame on exit 

    I_AtExit(D_QuitNetGame, true);

    SaveGameSettings(&settings, &connect_data);

    if (D_InitNetGame(&connect_data, &settings))
    {
        netgame = true;
        autostart = true;
    }
    else
    {
        D_InitSinglePlayerGame(&settings);
    }

    LoadGameSettings(&settings, &connect_data);

    DEH_printf("startskill %i  deathmatch: %i  startmap: %i  startepisode: %i\n",
               startskill, deathmatch, startmap, startepisode);

    DEH_printf("player %i of %i (%i nodes)\n",
               consoleplayer+1, settings.num_players, settings.num_players);

    // Show players here; the server might have specified a time limit

    if (timelimit > 0 && deathmatch)
    {
        // Gross hack to work like Vanilla:

        if (timelimit == 20 && M_CheckParm("-avg"))
        {
            DEH_printf("Austin Virtual Gaming: Levels will end "
                           "after 20 minutes\n");
        }
        else
        {
            DEH_printf("Levels will end after %d minute", timelimit);
            if (timelimit > 1)
                printf("s");
            printf(".\n");
        }
    }
}