// OpenCJ main file since plugin API does not allow us to do enough

/**************************************************************************
 * Includes                                                               *
 **************************************************************************/

#include <stdlib.h>
#include <stdbool.h>
#include <float.h>
#include <math.h>

#include "visualcj.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include "scr_vm.h"
#include "scr_vm_functions.h"
#include "xassets/weapondef.h"
#include "bg_public.h"
#include "cscr_stringlist.h"
#include "server_public.h"

/**************************************************************************
 * Extern functions without available prototype                           *
 **************************************************************************/

extern int GScr_LoadScriptAndLabel(const char *scriptName, const char *labelName, qboolean mandatory);

/**************************************************************************
 * Defines                                                                *
 **************************************************************************/



/**************************************************************************
 * Types                                                                  *
 **************************************************************************/

typedef struct
{
    char forward;
    char right;
} playermovement_t;

/**************************************************************************
 * Global variables                                                       *
 **************************************************************************/

// Callbacks for... everything we need to inform the GSC of
static int VCJ_callbacks[VCJ_CB_COUNT];

// Per player elevator
static bool VCJ_playerElevationPermissions[MAX_CLIENTS] = {0};
static bool VCJ_isPlayerElevating[MAX_CLIENTS] = {0};

// Per Player HB
static bool VCJ_playerHalfbeatPermissions[MAX_CLIENTS] = {0};

// Allow WASD
static bool VCJ_playerWASDCallbackEnabled[MAX_CLIENTS] = {0};

// For button monitoring
static int VCJ_previousButtons[MAX_CLIENTS] = {0};
static playermovement_t VCJ_playerMovement[MAX_CLIENTS] = {{0}};

// Ground monitoring
bool VCJ_clientOnGround[MAX_CLIENTS];
// Bounce monitoring
bool VCJ_clientCanBounce[MAX_CLIENTS];
float VCJ_clientBouncePrevVelocity[MAX_CLIENTS];
// Height monitoring
static float VCJ_clientMaxHeight[MAX_CLIENTS] = {0};

//Hb monitoring
static float VCJ_xvel[MAX_CLIENTS];
static float VCJ_yvel[MAX_CLIENTS];

/**************************************************************************
 * Forward declarations for static functions                              *
 **************************************************************************/

//static void Gsc_GetFollowersAndMe(scr_entref_t id);
//static void Gsc_StopFollowingMe(scr_entref_t id);
//static void Gsc_GetMaxHeight(scr_entref_t id);
//static void Gsc_ResetMaxHeight(scr_entref_t id);
//static void Gsc_ClientUserInfoChanged(scr_entref_t id);
//static void Gsc_ScaleOverTime(scr_entref_t hudelem);
//static void Gsc_GetConfigStringByIndex();

/**************************************************************************
 * Static functions                                                       *
 **************************************************************************/

static void PlayerCmd_ClientCommand(scr_entref_t arg)
{
    mvabuf;

    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        gentity_t *gentity = &g_entities[entityNum];

        if (!gentity->client)
        {
            Scr_ObjectError(va("Entity: %i is not a player", entityNum));
        }
        else
        {
            if (Scr_GetNumParam() != 0)
            {
                Scr_Error("Usage: self ClientCommand()\n");
            }
            else
            {
                ClientCommand(entityNum);
            }
        }
    }
}

static void PlayerCmd_FollowPlayer(scr_entref_t arg)
{
    mvabuf;

    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        gentity_t *gentity = &g_entities[entityNum];
        if (!gentity->client)
        {
            Scr_ObjectError(va("Entity: %i is not a player", entityNum));
        }
        else
        {
            gentity = g_entities + entityNum;
            if (Scr_GetNumParam() != 1)
            {
                Scr_Error("Usage: self followplayer(newplayer entid)\n");
            }
            else
            {
                int target = Scr_GetInt(0);
                if (target >= 0)
                {
                    extern qboolean Cmd_FollowClient_f(gentity_t *, int);
                    Cmd_FollowClient_f(gentity, target);
                }
                else
                {
                    StopFollowing(gentity);
                }
            }
        }
    }
}

static void PlayerCmd_RenameClient(scr_entref_t arg)
{
    mvabuf;

    if (arg.classnum)
    {
            Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        gentity_t *gentity = &g_entities[entityNum];
        if (!gentity->client)
        {
            Scr_ObjectError(va("Entity: %i is not a player", entityNum));
        }
        else
        {
            gentity = g_entities + entityNum;
            gclient_t *client = gentity->client;
            if (Scr_GetNumParam() != 1)
            {
                Scr_Error("Usage: (cleanedName =) self renameClient(<string>)\n");
            }

            char *s = Scr_GetString(0);
            renameClient(client, s);
            Scr_AddString(client->sess.newnetname);
            return;
        }
    }

    Scr_AddUndefined();
}

static void PlayerCmd_StartRecord(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        client_t *client = &svs.clients[entityNum];

        if (Scr_GetNumParam() != 1)
        {
            Scr_Error("Usage: self startrecord(<string>)\n");
        }
        else
        {
            char *demoname = Scr_GetString(0);
            if (demoname && (demoname[0] != '\0'))
            {
                SV_RecordClient(client, demoname);
            }
        }
    }
    Scr_AddInt(0);
}

static void PlayerCmd_StopRecord(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        client_t *client = &svs.clients[entityNum];
        SV_StopRecord(client);
    }
    Scr_AddInt(0);
}

static void PlayerCmd_allowElevate(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        bool canElevate = Scr_GetInt(0);
        if (VCJ_playerElevationPermissions[entityNum] != canElevate)
        {
            VCJ_playerElevationPermissions[entityNum] = canElevate;
            VCJ_isPlayerElevating[entityNum] = false;
        }
    }
}

static void PlayerCmd_allowHalfBeat(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        int entityNum = arg.entnum;
        bool canHalfbeat = Scr_GetInt(0);
        if (VCJ_playerHalfbeatPermissions[entityNum] != canHalfbeat)
        {
            VCJ_playerHalfbeatPermissions[entityNum] = canHalfbeat;
        }
    }
}

static void PlayerCmd_EnableWASDCallback(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        VCJ_playerWASDCallbackEnabled[arg.entnum] = true;
    }
}

static void PlayerCmd_DisableWASDCallback(scr_entref_t arg)
{
    if (arg.classnum)
    {
        Scr_ObjectError("Not an entity");
    }
    else
    {
        VCJ_playerWASDCallbackEnabled[arg.entnum] = false;
    }
}

void PlayerCmd_SetMoveSpeed_Wrap(scr_entref_t id)
{
    if (id.classnum)
    {
        Scr_ObjectError("Not an entity");
        return;
    }

    if (Scr_GetNumParam() != 1)
    {
        Scr_Error("Usage: self setmovespeed( <integer> )\n");
        return;
    }

    int entityNum = id.entnum;
    gentity_t *gentity = &g_entities[entityNum];
    if (!gentity->client)
    {
        Scr_ObjectError(va("Entity: %i is not a player", entityNum));
        return;
    }

    int speed = Scr_GetInt(0);

    if ((speed <= 0) || (speed > 1000))
    {
        Scr_Error("setmovespeed range is between 0 and 1000\n");
        return;
    }

    gentity->client->sess.moveSpeedScaleMultiplier = (float)speed / g_speed->integer;
}

/**************************************************************************
 * Global functions                                                       *
 **************************************************************************/

void renameClient(gclient_t *client, char *s)
{
    if (!client || !s || (s[0] == '\0')) return;

    int i = 0;
    // Strip leading spaces
    while (s[i] == ' ')
    {
        s++; // Skip over the space
        i++;
    }

    i = 0;
    while (s[i] != '\0')
    {
        if (((unsigned char)s[i] < 0x1F) || ((unsigned char)s[i] == 0x7F))
        {
            s[i] = '?';
        }
        i++;
    }

    // Enforce a minimum name length. Since this could require more characters than were allocated, allocate a new buffer
    char buf[64] = {0};
    Q_strncpyz(buf, s, sizeof(buf));
    char *pCleanedNewName = (char *)buf;
    int newNameLen = strlen(pCleanedNewName);
    int minNameLen = 3;
    if (newNameLen < minNameLen)
    {
        for (int i = 0; i < (minNameLen - newNameLen); i++)
        {
            pCleanedNewName[newNameLen + i] = (i % 10) + '0'; // Limit to 0-9
        }
    }

    ClientCleanName(pCleanedNewName, client->sess.cs.netname, sizeof(client->sess.cs.netname), qtrue);
    CS_SetPlayerName(&client->sess.cs, client->sess.cs.netname);
    Q_strncpyz(client->sess.newnetname, client->sess.cs.netname, sizeof(client->sess.newnetname));
}

/**************************************************************************
 * OpenCJ functions                                                       *
 **************************************************************************/

void VCJ_init(void)
{
    // Init callbacks
    //VCJ_callbacks[VCJ_CB_PLAYERCOMMAND]           = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_PlayerCommand",             qfalse);
    VCJ_callbacks[VCJ_CB_RPGFIRED]                = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_RPGFired",                  qfalse);
    //VCJ_callbacks[VCJ_CB_WEAPONFIRED]             = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_WeaponFired",               qfalse);
    //VCJ_callbacks[VCJ_CB_SPECTATORCLIENTCHANGED]  = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_SpectatorClientChanged",    qfalse);
    //VCJ_callbacks[VCJ_CB_USERINFO]                = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_UserInfoChanged",           qfalse);
    //VCJ_callbacks[VCJ_CB_STARTJUMP]               = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_StartJump",                 qfalse);
    //VCJ_callbacks[VCJ_CB_MELEEBUTTONPRESSED]      = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_MeleeButton",               qfalse);
    //VCJ_callbacks[VCJ_CB_USEBUTTONPRESSED]        = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_UseButton",                 qfalse);
    //VCJ_callbacks[VCJ_CB_ATTACKBUTTONPRESSED]     = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_AttackButton",              qfalse);
    //VCJ_callbacks[VCJ_CB_MOVEFORWARD]             = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_MoveForward",               qfalse);
    //VCJ_callbacks[VCJ_CB_MOVELEFT]                = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_MoveLeft",                  qfalse);
    //VCJ_callbacks[VCJ_CB_MOVEBACKWARD]            = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_MoveBackward",              qfalse);
    //VCJ_callbacks[VCJ_CB_MOVERIGHT]               = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_MoveRight",                 qfalse);
    //VCJ_callbacks[VCJ_CB_FPSCHANGE] 				= GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_FPSChange", 				qfalse);
    //VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE]         = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_OnGroundChange",            qfalse);
    VCJ_callbacks[VCJ_CB_PLAYER_BOUNCED]          = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_PlayerBounced",             qfalse);
    VCJ_callbacks[VCJ_CB_ON_PLAYER_ELE]           = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_OnElevate",                 qfalse);
    VCJ_callbacks[VCJ_CB_PLAYER_HB]           = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_OnHalfbeat",                 qfalse);
}

int VCJ_getCallback(VCJ_callback_t callbackType)
{
    if ((callbackType <= VCJ_CB_UNKNOWN) || (callbackType >= VCJ_CB_COUNT))
    {
        return 0;
    }

    return VCJ_callbacks[callbackType];
}

void VCJ_clearPlayerMovementCheckVars(int clientNum)
{
    VCJ_playerMovement[clientNum].forward = 0;
    VCJ_playerMovement[clientNum].right = 0;
}

void VCJ_addMethodsAndFunctions(void)
{
    Scr_AddMethod("allowelevate",           PlayerCmd_allowElevate,             qfalse);
    Scr_AddMethod("allowhalfbeat",           PlayerCmd_allowHalfBeat,             qfalse);
}

void VCJ_onFrame(void)
{
    //mysql_handle_result_callbacks();
}

void VCJ_onJumpCheck(struct pmove_t *pm)
{

}

void VCJ_onStartJump(struct pmove_t *pm)
{
    if(VCJ_callbacks[VCJ_CB_STARTJUMP])
    {
        Scr_AddInt(pm->cmd.serverTime);
        short ret = Scr_ExecEntThread(&g_entities[pm->ps->clientNum], VCJ_callbacks[VCJ_CB_STARTJUMP], 1);
        Scr_FreeThread(ret);
    }
}

void VCJ_onUserInfoChanged(gentity_t *ent)
{
    if(VCJ_callbacks[VCJ_CB_USERINFO])
	{
		int threadId = Scr_ExecEntThread(ent, VCJ_callbacks[VCJ_CB_USERINFO], 0);
		Scr_FreeThread(threadId);
	}
}

void VCJ_beforeClientMoveCommand(client_t *client, usercmd_t *ucmd)
{
    if (!client || !ucmd || !client->gentity || !client->gentity->client) return;


    VCJ_xvel[client - svs.clients] = client->gentity->client->ps.velocity[0];
    VCJ_yvel[client - svs.clients] = client->gentity->client->ps.velocity[1];
}

void VCJ_onClientMoveCommand(client_t *client, usercmd_t *ucmd)
{
    if (!client || !ucmd || !client->gentity || !client->gentity->client) return;

    gclient_t *gclient = client->gentity->client;

    // ---- 1. Client FPS determination
    int clientNum = client - svs.clients;
	int time = ucmd->serverTime;
    int avgFrameTime = 0;
    //if (VCJ_updatePlayerFPS(clientNum, time, &avgFrameTime))
    //{
        // Client FPS changed, report this to GSC via callback
	//	if (VCJ_callbacks[VCJ_CB_FPSCHANGE])
	//	{
	//		Scr_AddInt(avgFrameTime);
	//		short ret = Scr_ExecEntThread(client->gentity, VCJ_callbacks[VCJ_CB_FPSCHANGE], 1);
	//		Scr_FreeThread(ret);
	//	}
	//}

    // When spectating, client->gentity is the person you're spectating. We don't want reporting for them!
    if (gclient->sess.sessionState == SESS_STATE_PLAYING)
    {
        //HB detect
        if(fabs(VCJ_xvel[clientNum]) < fabs(client->gentity->client->ps.velocity[0]) && fabs(client->gentity->client->ps.velocity[0]) > 250 && ucmd->forwardmove == 0)
        {
            //posssible hb in x dir
            int callback = VCJ_callbacks[VCJ_CB_PLAYER_HB];
            if(callback)
            {
                Scr_AddInt(ucmd->serverTime);
                gentity_t *ent = SV_GentityNum(clientNum);
                int threadId = Scr_ExecEntThread(ent, callback, 1);
                Scr_FreeThread(threadId);
            }
        }
        if(fabs(VCJ_yvel[clientNum]) < fabs(client->gentity->client->ps.velocity[1]) && fabs(client->gentity->client->ps.velocity[1]) > 250 && ucmd->forwardmove == 0)
        {
            //posssible hb in x dir
            int callback = VCJ_callbacks[VCJ_CB_PLAYER_HB];
            if(callback)
            {
                Scr_AddInt(ucmd->serverTime);
                gentity_t *ent = SV_GentityNum(clientNum);
                int threadId = Scr_ExecEntThread(ent, callback, 1);
                Scr_FreeThread(threadId);
            }
        }


        // 3. onGround reporting
        //bool isOnGround = (gclient->ps.groundEntityNum != 1023);
        //if (isOnGround != VCJ_clientOnGround[clientNum])
        //{
         //   VCJ_clientOnGround[clientNum] = isOnGround;

            // This callback can spam! Filtering on GSC side required.
        //    if (VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE])
        //    {
         //       Scr_AddVector(gclient->ps.origin);
         //       Scr_AddInt(ucmd->serverTime);
         //       Scr_AddBool(isOnGround);
          //      short ret = Scr_ExecEntThread(client->gentity, VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE], 3);
          //      Scr_FreeThread(ret);
          //  }
        //}


        // 4. Report bounce occurring (https://xoxor4d.github.io/research/cod4-doublebounce/)
        bool canBounce = ((gclient->ps.pm_flags & 0x4000) != 0);
        if (canBounce != VCJ_clientCanBounce[clientNum])
        {
            // If the player can no longer bounce, it means they just bounced!
            if (!canBounce)
            {
                // If the Z velocity went up, it means they bounced. Unless new velocity is 0, then they loaded or whatever
                if ((gclient->ps.velocity[2] > VCJ_clientBouncePrevVelocity[clientNum]) && (fabs(gclient->ps.velocity[2] - 0.0f) >= FLT_EPSILON))
                {
                    if (VCJ_callbacks[VCJ_CB_PLAYER_BOUNCED])
                    {
                        Scr_AddInt(gclient->sess.cmd.serverTime);
                        short ret = Scr_ExecEntThread(client->gentity, VCJ_callbacks[VCJ_CB_PLAYER_BOUNCED], 1);
                        Scr_FreeThread(ret);
                    }
                }
                else
                {
                    // Z velocity didn't go up, so they just slid off something, or spawn/setorigin/setvelocity whatever.
                }
            }
            else
            {
                // Player can bounce again after not being able to bounce anymore
            }
            VCJ_clientCanBounce[clientNum] = canBounce;
        }

        // Always update the previous velocity
        VCJ_clientBouncePrevVelocity[clientNum] = gclient->ps.velocity[2];
        // And the maximum height they reached
        VCJ_clientMaxHeight[clientNum] = std::fmax(gclient->ps.origin[2], VCJ_clientMaxHeight[clientNum]);
    }
}

/**************************************************************************
 * GSC commands                                                           *
 **************************************************************************/




/**************************************************************************
 * Functions that are called from ASM/C (mostly callbacks or checks)        *
 **************************************************************************/

void Ext_RPGFiredCallback(gentity_t *player, gentity_t *rpg)
{
    int callback = VCJ_callbacks[VCJ_CB_RPGFIRED];
    if (callback != 0)
    {
        Scr_AddInt(player->client->lastServerTime);
        Scr_AddFloat(player->client->ps.viewangles[0]);
        Scr_AddString(BG_GetWeaponDef(rpg->s.weapon)->szInternalName);
        Scr_AddEntity(rpg);
        int threadId = Scr_ExecEntThread(player, callback, 4);
        Scr_FreeThread(threadId);
    }
}

void Ext_WeaponFiredCallback(gentity_t *player)
{
    int callback = VCJ_callbacks[VCJ_CB_WEAPONFIRED];
    if (callback != 0)
    {
        Scr_AddInt(player->client->lastServerTime);
        Scr_AddString(BG_GetWeaponDef(player->s.weapon)->szInternalName);
        int threadId = Scr_ExecEntThread(player, callback, 2);
        Scr_FreeThread(threadId);
    }
}

// Called by ASM when a player is trying to start an elevate (spammed while this occurs)
int Ext_IsPlayerAllowedToEle(struct pmove_t *pmove)
{
    Ext_PlayerTryingToEle(pmove);
    return (VCJ_playerElevationPermissions[pmove->ps->clientNum]);
}

//Called by Ext_IsPlayerAllowedToEle because then we know a player is trying to elevate (this can get spammed)
void Ext_PlayerTryingToEle(struct pmove_t *pmove)
{
    int clientNum = pmove->ps->clientNum;
    // Only do the callback if the player wasn't already trying to elevate
    if (!VCJ_isPlayerElevating[clientNum])
    {
        VCJ_isPlayerElevating[clientNum] = true;
        //Let GSC know that a player is trying to elevate, and whether or not they are allowed to
        int callback = VCJ_callbacks[VCJ_CB_ON_PLAYER_ELE];
        if(callback)
        {
            gentity_t *ent = SV_GentityNum(clientNum);
            Scr_AddBool(VCJ_playerElevationPermissions[clientNum]);
            int threadId = Scr_ExecEntThread(ent, callback, 1);
            Scr_FreeThread(threadId);
        }
    }
}

//Called by ASM when we know a player is not trying to elevate at this point of time (needed to reset callback for player trying to elevate)
void Ext_PlayerNotEle(struct pmove_t *pmove)
{
    if (VCJ_isPlayerElevating[pmove->ps->clientNum])
    {
        VCJ_isPlayerElevating[pmove->ps->clientNum] = false;
    }
}

#ifdef __cplusplus
}
#endif
