#ifndef __VISUALCJ_H
#define __VISUALCJ_H


#ifdef __cplusplus
extern "C" {
#endif

#include "scr_vm.h"
#include "bg_public.h"

// Enums
typedef enum
{
    VCJ_CB_UNKNOWN = 0, // Always 0, so handlers can see if no handler was found
    VCJ_CB_PLAYERCOMMAND,
    VCJ_CB_RPGFIRED,
    VCJ_CB_WEAPONFIRED,
    VCJ_CB_SPECTATORCLIENTCHANGED,
    VCJ_CB_USERINFO,
    VCJ_CB_STARTJUMP,
    VCJ_CB_MELEEBUTTONPRESSED,
    VCJ_CB_USEBUTTONPRESSED,
    VCJ_CB_ATTACKBUTTONPRESSED,
    VCJ_CB_MOVEFORWARD,
    VCJ_CB_MOVELEFT,
    VCJ_CB_MOVEBACKWARD,
    VCJ_CB_MOVERIGHT,
    VCJ_CB_FPSCHANGE,
    VCJ_CB_ONGROUND_CHANGE,
    VCJ_CB_PLAYER_BOUNCED,
    VCJ_CB_ON_PLAYER_ELE,

    VCJ_CB_COUNT, // Always keep this as last entry
} VCJ_callback_t;

// Prototypes
void renameClient(gclient_t *, char *);

void VCJ_init(void);
void VCJ_onFrame(void);
void VCJ_onStartJump(struct pmove_t *);
void VCJ_onJumpCheck(struct pmove_t *);
void VCJ_onUserInfoChanged(gentity_t *);
void VCJ_onClientMoveCommand(client_t *, usercmd_t *);
void VCJ_addMethodsAndFunctions(void);
int VCJ_getCallback(VCJ_callback_t);
void VCJ_clearPlayerMovementCheckVars(int);

void Ext_RPGFiredCallback(gentity_t *, gentity_t *);
//void Ext_WeaponFiredCallback(gentity_t *);
int Ext_IsPlayerAllowedToEle(struct pmove_t *);
void Ext_PlayerTryingToEle(struct pmove_t *);
void Ext_PlayerNotEle(struct pmove_t *);
//void Ext_SpectatorClientChanged(gentity_t *, int, int);

#ifdef __cplusplus
}
#endif

#endif // __VISUALCJ_H