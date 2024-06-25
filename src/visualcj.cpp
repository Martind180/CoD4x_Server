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
#include "g_shared.h"
#include "g_sv_shared.h"
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

#define NR_SAMPLES_FPS_AVERAGING 20

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
static int VCJ_PlayerPrevServerTime[MAX_CLIENTS] = {0};

// Allow WASD
static bool VCJ_playerWASDCallbackEnabled[MAX_CLIENTS] = {0};

// For button monitoring
static int VCJ_previousButtons[MAX_CLIENTS] = {0};
static playermovement_t VCJ_playerMovement[MAX_CLIENTS] = {{0}};

//Sliding on slope monitoring
static bool VCJ_PlayerSliding[MAX_CLIENTS] = {0};

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
static float VCJ_zvel[MAX_CLIENTS];

// For client FPS calculation
static int VCJ_clientFrameTimes[MAX_CLIENTS][NR_SAMPLES_FPS_AVERAGING] = {{0}}; // Client frame times storage, per client, with x samples
static int VCJ_clientFrameTimesSampleIdx[MAX_CLIENTS] = {0}; // Index in VCJ_clientFrameTimes, per client
static int VCJ_prevClientFrameTimes[MAX_CLIENTS] = {0};
static int VCJ_avgFrameTimeMs[MAX_CLIENTS] = {0};

/**************************************************************************
 * Forward declarations for static functions                              *
 **************************************************************************/
static void VCJ_onConnect(scr_entref_t id);
//static void Gsc_ClientUserInfoChanged(scr_entref_t id);
//static void Gsc_ScaleOverTime(scr_entref_t hudelem);
//static void Gsc_GetConfigStringByIndex();

/**************************************************************************
 * Static functions                                                       *
 **************************************************************************/

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

/**************************************************************************
 * Global functions                                                       *
 **************************************************************************/


/**************************************************************************
 * OpenCJ functions                                                       *
 **************************************************************************/

void VCJ_init(void)
{
    // Init callbacks
    VCJ_callbacks[VCJ_CB_RPGFIRED]                = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_RPGFired",                  qfalse);
    //VCJ_callbacks[VCJ_CB_WEAPONFIRED]             = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_WeaponFired",               qfalse);
    //VCJ_callbacks[VCJ_CB_USERINFO]                = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_UserInfoChanged",           qfalse);
    //VCJ_callbacks[VCJ_CB_STARTJUMP]               = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_StartJump",                 qfalse);
    VCJ_callbacks[VCJ_CB_FPSCHANGE] 				= GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_FPSChange", 				qfalse);
    VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE]         = GScr_LoadScriptAndLabel("maps/mp/gametypes/_callbacksetup", "CodeCallback_OnGroundChange",            qfalse);
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
    Scr_AddMethod("allowelevate",                    PlayerCmd_allowElevate,                          qfalse);
    Scr_AddMethod("allowhalfbeat",                   PlayerCmd_allowHalfBeat,                         qfalse);
    Scr_AddMethod("player_onconnect",                VCJ_onConnect,                                   qfalse);
    Scr_AddMethod("setoriginandangles",              (xmethod_t)Gsc_Player_setOriginAndAngles,        qfalse);
    Scr_AddMethod("switchtoweaponseamless",          (xmethod_t)Gsc_Player_switchToWeaponSeamless,    qfalse);
    Scr_AddMethod("getPMFlags",                      (xmethod_t)Gsc_Player_GetPMFlags,                qfalse);
    Scr_AddMethod("getPMTime",                       (xmethod_t)Gsc_Player_GetPMTime,                 qfalse);
    Scr_AddMethod("setPMFlags",                      (xmethod_t)Gsc_Player_SetPMFlags,                qfalse);
    Scr_AddMethod("addVelocity",                     (xmethod_t)Gsc_Player_Velocity_Add,              qfalse);

    Scr_AddFunction("vectorscale",         Gsc_Utils_VectorScale,                 qfalse);
}

static void VCJ_onConnect(scr_entref_t id)
{
    if (id.classnum != 0)
    {
        // Not an entity
        return;
    }

    gentity_t *gent = &g_entities[id.entnum];
    if (!gent || !gent->client)
    {
        // Not a player
        return;
    }

    int clientNum = gent->client->ps.clientNum;
    VCJ_clearPlayerFPS(clientNum);

    VCJ_previousButtons[id.entnum] = 0;
    memset(&VCJ_playerMovement[id.entnum], 0, sizeof(VCJ_playerMovement[id.entnum]));
    VCJ_clientOnGround[id.entnum] = false;
    VCJ_clientCanBounce[id.entnum] = false;
    VCJ_clientBouncePrevVelocity[id.entnum] = 0.0f;
}

void VCJ_onFrame(void)
{

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
    VCJ_zvel[client - svs.clients] = client->gentity->client->ps.velocity[2];
}

void VCJ_onClientMoveCommand(client_t *client, usercmd_t *ucmd)
{
    if (!client || !ucmd || !client->gentity || !client->gentity->client) return;

    gclient_t *gclient = client->gentity->client;

    // ---- 1. Client FPS determination
    int clientNum = client - svs.clients;
	int time = ucmd->serverTime;
    int avgFrameTime = 0;
    if (VCJ_updatePlayerFPS(clientNum, time, &avgFrameTime))
    {
        // Client FPS changed, report this to GSC via callback
		if (VCJ_callbacks[VCJ_CB_FPSCHANGE])
		{
			Scr_AddInt(avgFrameTime);
			short ret = Scr_ExecEntThread(client->gentity, VCJ_callbacks[VCJ_CB_FPSCHANGE], 1);
			Scr_FreeThread(ret);
		}
	}

    // When spectating, client->gentity is the person you're spectating. We don't want reporting for them!
    if (gclient->sess.sessionState == SESS_STATE_PLAYING)
    {
        bool isOnGround = (gclient->ps.groundEntityNum != 1023);
        //---- 2. Player HB determination

        int frametime = ucmd->serverTime - VCJ_PlayerPrevServerTime[clientNum];
        int accel = round((float)(frametime * g_gravity->current.floatval) * 0.001);

        if(VCJ_zvel[clientNum] - gclient->ps.velocity[2] == accel)
        {
            if(fabs(VCJ_xvel[clientNum]) < fabs(gclient->ps.velocity[0]) && fabs(gclient->ps.velocity[0]) > 300 && ucmd->forwardmove == 0 && (ucmd->rightmove == -127 || ucmd->rightmove == 127))
            {
                //posssible hb in x dir
                if(!VCJ_playerHalfbeatPermissions[clientNum])
                {
                    gclient->ps.velocity[0] = VCJ_xvel[clientNum];
                }

                int callback = VCJ_callbacks[VCJ_CB_PLAYER_HB];
                if(callback)
                {
                    Scr_AddInt(ucmd->serverTime);
                    gentity_t *ent = SV_GentityNum(clientNum);
                    int threadId = Scr_ExecEntThread(ent, callback, 1);
                    Scr_FreeThread(threadId);
                }
            }
            if(fabs(VCJ_yvel[clientNum]) < fabs(gclient->ps.velocity[1]) && fabs(gclient->ps.velocity[1]) > 300 && ucmd->forwardmove == 0 && (ucmd->rightmove == -127 || ucmd->rightmove == 127))
            {
                //posssible hb in x dir
                if(!VCJ_playerHalfbeatPermissions[clientNum])
                {
                    gclient->ps.velocity[1] = VCJ_yvel[clientNum];
                }

                int callback = VCJ_callbacks[VCJ_CB_PLAYER_HB];
                if(callback)
                {
                    Scr_AddInt(ucmd->serverTime);
                    gentity_t *ent = SV_GentityNum(clientNum);
                    int threadId = Scr_ExecEntThread(ent, callback, 1);
                    Scr_FreeThread(threadId);
                }
            }
        }


        // 3. onGround reporting
        if (isOnGround != VCJ_clientOnGround[clientNum])
        {
            VCJ_clientOnGround[clientNum] = isOnGround;

            // This callback can spam! Filtering on GSC side required.
            if (VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE])
            {
                Scr_AddVector(gclient->ps.origin);
                Scr_AddInt(ucmd->serverTime);
                Scr_AddBool(isOnGround);
                short ret = Scr_ExecEntThread(client->gentity, VCJ_callbacks[VCJ_CB_ONGROUND_CHANGE], 3);
                Scr_FreeThread(ret);
            }
        }

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
        VCJ_PlayerPrevServerTime[clientNum] = ucmd->serverTime;
    }
}

bool VCJ_updatePlayerFPS(int clientNum, int time, int *pAvgFrameTimeMs)
{
    VCJ_clientFrameTimes[clientNum][VCJ_clientFrameTimesSampleIdx[clientNum]] = time - VCJ_prevClientFrameTimes[clientNum];
    VCJ_prevClientFrameTimes[clientNum] = time;

    // There are x sample slots, if all are used we restart at begin
    if (++VCJ_clientFrameTimesSampleIdx[clientNum] >= NR_SAMPLES_FPS_AVERAGING)
    {
        VCJ_clientFrameTimesSampleIdx[clientNum] = 0;
    }

    // Sum frame times so we can use it to calculate the average
    float sumFrameTime = 0;
    for (int i = 0; i < NR_SAMPLES_FPS_AVERAGING; i++)
    {
        sumFrameTime += (float)VCJ_clientFrameTimes[clientNum][i];
    }

    // Check if client frame time is different from what we previously reported
    bool hasFPSChanged = false;
    int avgFrameTime = (int)round(sumFrameTime / NR_SAMPLES_FPS_AVERAGING);
    if (VCJ_avgFrameTimeMs[clientNum] != avgFrameTime)
    {
        // Client FPS changed, report this to GSC via callback
        VCJ_avgFrameTimeMs[clientNum] = avgFrameTime;
        if (pAvgFrameTimeMs)
        {
            *pAvgFrameTimeMs = avgFrameTime;
            hasFPSChanged = true;
        }
    }

    return hasFPSChanged;
}

void VCJ_clearPlayerFPS(int clientNum)
{
    memset(VCJ_clientFrameTimes[clientNum], 0, sizeof(VCJ_clientFrameTimes[clientNum]));
    VCJ_clientFrameTimesSampleIdx[clientNum] = 0;
    VCJ_prevClientFrameTimes[clientNum] = 0;
    VCJ_avgFrameTimeMs[clientNum] = 0;
}

/**************************************************************************
 * GSC commands                                                           *
 **************************************************************************/

void Gsc_Player_setOriginAndAngles(int id)
{
	//sets origin, angles
	//resets pm_flags and velocity
	//keeps stance

	#define PMF_DUCKED 0x2
	#define PMF_PRONE 0x1 //untested
	#define EF_TELEPORT_BIT 0x2

	gentity_s *ent;
	vec3_t origin;
	vec3_t angles;
	ent = &g_entities[id];
	if(!ent->client)
	{
		Scr_ObjectError(va("entity %i is not a player", id));
	}

	Scr_GetVector(0, origin);
	Scr_GetVector(1, angles);

	bool isUsingTurret;
	isUsingTurret = ((ent->client->ps.otherFlags & 4) != 0  && (ent->client->ps.eFlags & 0x300) != 0);
	//stop using MGs
	if(isUsingTurret)
	{
		G_ClientStopUsingTurret(&g_entities[ent->client->ps.viewlocked_entNum]);
	}

	G_EntUnlink(ent);

	//unlink client from linkto() stuffs

	if (ent->r.linked)
	{
		SV_UnlinkEntity(ent);
	}

	//clear flags
	ent->client->ps.pm_flags &= (PMF_DUCKED | PMF_PRONE);//keep stance
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT; //alternate teleport flag, unsure why

	//set times
	ent->client->ps.pm_time = 0;
	ent->client->ps.jumpTime = 0; //to reset wallspeed effects

	//set origin
	VectorCopy(origin, ent->client->ps.origin);
    G_SetOrigin(ent, origin);


	//reset velocity
	ent->client->ps.velocity[0] = 0;
	ent->client->ps.velocity[1] = 0;
	ent->client->ps.velocity[2] = 0;


	ent->client->ps.sprintState.sprintButtonUpRequired = 0;
	ent->client->ps.sprintState.sprintDelay = 0;
	ent->client->ps.sprintState.lastSprintStart = 0;
	ent->client->ps.sprintState.lastSprintEnd = 0;
	ent->client->ps.sprintState.sprintStartMaxLength = 0;


	//pretend we're not proning so that prone angle is ok after having called SetClientViewAngle (otherwise it gets a correction)
	int flags = ent->client->ps.pm_flags;
	ent->client->ps.pm_flags &= ~PMF_PRONE;

	SetClientViewAngle(ent, angles);

	//reset velocity
	ent->client->ps.velocity[0] = 0;
	ent->client->ps.velocity[1] = 0;
	ent->client->ps.velocity[2] = 0;

	//restore prone if any
	ent->client->ps.pm_flags = flags;

	SV_LinkEntity(ent);
}

void Gsc_Player_switchToWeaponSeamless(int id)
{
    const char *szWeaponName = Scr_GetString(0);
    if (szWeaponName)
    {
        playerState_t *ps = SV_GameClientNum(id);
        int weaponIdx = G_GetWeaponIndexForName(szWeaponName);
        if (BG_IsWeaponValid(ps, weaponIdx))
        {
            ps->weapon = weaponIdx;
            //ps->weaponstate = 0; // WEAPON_READY
            SV_GameSendServerCommand(id, 1, va("%c %i", 'a', weaponIdx));
        }
    }
}

void Gsc_Player_SetPMFlags(int id)
{
    int numParams = Scr_GetNumParam();
    const char *szSyntax = "expected 1-2 arguments: <pm_flags> [pm_time]";
    if ((numParams < 1) || (numParams > 2))
    {
        Scr_ObjectError(szSyntax);
        return;
    }

    int flags = Scr_GetInt(0);

    int time = Scr_GetInt(1);

    playerState_t *ps = SV_GameClientNum(id);

    if (ps != NULL)
    {
        ps->pm_flags = flags;
        if (numParams > 1)
        {
            ps->pm_time = time;
        }
    }
}

void Gsc_Player_GetPMFlags(int id)
{
    playerState_t *ps = SV_GameClientNum(id);
    Scr_AddInt(ps->pm_flags);
}

void Gsc_Player_GetPMTime(int id)
{
    playerState_t *ps = SV_GameClientNum(id);
    Scr_AddInt(ps->pm_time);
}

void Gsc_Player_Velocity_Add(int id)
{
    vec3_t velocity;
    Scr_GetVector(0, velocity);

    playerState_t *ps = SV_GameClientNum(id);
    if (ps)
    {
        ps->velocity[0] += velocity[0];
        ps->velocity[1] += velocity[1];
        ps->velocity[2] += velocity[2];
        Scr_AddInt(1);
    }
    else
    {
        Scr_AddInt(0);
    }
}

void Gsc_Utils_VectorScale()
{
    vec3_t vector;
    Scr_GetVector(0, vector);
    float scale = Scr_GetFloat(1);

    vector[0] *= scale;
    vector[1] *= scale;
    vector[2] *= scale;
    Scr_AddVector(vector);
}


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

void Ext_PM_AirMove(struct pmove_t *pmove, struct pml_t *pml)
{
	int clientNum = pmove->ps->clientNum;

	if (pml->groundTrace.normal[2] > .3f && pml->groundTrace.normal[2] < .7f)
	{
	    VCJ_PlayerSliding[clientNum] = true;
	}
	else
	{
	    VCJ_PlayerSliding[clientNum] = false;
	}
}

#ifdef __cplusplus
}
#endif
