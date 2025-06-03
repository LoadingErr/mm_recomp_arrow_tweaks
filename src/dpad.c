#include "modding.h"
#include "global.h"
#include "sys_cmpdma.h"
#include "rt64_extended_gbi.h"
#include "recompconfig.h"
#include "recomputils.h"
#include "z64item.h"
#include "z64save.h"
#include "z64interface.h"
#include "kaleido_manager.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "z64horse.h"
#include "z64player.h"

#define ARROW_LIMB_MAX 5

#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"

typedef enum {
    CYCLING_MODE_NONE,
    CYCLING_MODE_L,
    CYCLING_MODE_R,
} ArrowCycling;

#define CFG_CYCLING_MODE ((ArrowCycling)recomp_get_config_u32("arrow_cycling"))

// Handling for arrows and magic:
#define ARROW_DEATH_TIMER_MAX 40
typedef struct {
    int type_change_timer;
    u8 lastArrow;
    u8 currentArrow;
    int arrow_death_timer;
} NextFrameArrowUpdateInfo;
NextFrameArrowUpdateInfo magic_arrow_info;

RECOMP_CALLBACK("*", recomp_on_init) void on_startup () {
    magic_arrow_info.type_change_timer = 0;
    magic_arrow_info.arrow_death_timer = 0;
}

extern u8 sMagicArrowCosts[];

s32 Player_UpperAction_7(Player* thisx, PlayState* play);
s32 Player_UpperAction_8(Player* thisx, PlayState* play);

bool Player_isHoldingBow(Player* this, PlayState* play) {
    return (this->heldItemAction == PLAYER_IA_BOW || 
             this->heldItemAction == PLAYER_IA_BOW_FIRE || 
             this->heldItemAction == PLAYER_IA_BOW_ICE || 
             this->heldItemAction == PLAYER_IA_BOW_LIGHT);
}

bool Player_IsAiming(Player* this, PlayState* play) {
    return (Player_isHoldingBow(this, play)) &&
             (this->upperActionFunc == Player_UpperAction_8 ||
              this->upperActionFunc == Player_UpperAction_7);
}

bool Player_IsArrowNocked(Player* this, PlayState* play) {
    return ((Player_isHoldingBow(this, play)) &&
             (this->upperActionFunc == Player_UpperAction_7));
}

bool shouldAllowArrowCycling(Player* this, PlayState* play) {
    return (recomp_get_config_u32("arrow_cycling_aiming_required") && Player_IsAiming(this, play)) 
        || (!recomp_get_config_u32("arrow_cycling_aiming_required") && Player_isHoldingBow(this, play));
}

u8 getArrowMagic(u8 bowItem) {
    u8 magicArrowIndex = bowItem - ITEM_BOW_FIRE;
    u8 arrowType;
    if (magicArrowIndex >= 0 && magicArrowIndex <= 2) {
        arrowType = ARROW_TYPE_FIRE + magicArrowIndex;
    } else {
        arrowType = ARROW_TYPE_NORMAL;
    }

    ArrowMagic magicArrowType = ARROW_GET_MAGIC_FROM_TYPE (arrowType);

    if ((ARROW_GET_MAGIC_FROM_TYPE (arrowType) >= ARROW_MAGIC_FIRE) && 
        (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {
        return sMagicArrowCosts [magicArrowType];
    }
    
    return 0;
}

void SetArrowMagicInfoHandler(Player* this, PlayState* play, u8 lastArrow, u8 currentArrow) {
    magic_arrow_info.lastArrow = lastArrow;
    magic_arrow_info.currentArrow = currentArrow;
    if(Player_IsArrowNocked(this, play)) {
        magic_arrow_info.type_change_timer = 3;
    }
}

void UpdateArrowMagicHandler(Player* this, PlayState* play) {
    switch(magic_arrow_info.type_change_timer) {
        case 3:
            if (magic_arrow_info.currentArrow == ITEM_BOW) {
                Magic_Reset(play);
            }
            break;
        case 2:
            if (magic_arrow_info.lastArrow != ITEM_BOW) {
                // Magic_Add(play, getArrowMagic(arrow_update.lastArrow));
            }
            break;
        case 1:
            if (magic_arrow_info.currentArrow != ITEM_BOW) {
                Magic_Consume(play, getArrowMagic(magic_arrow_info.currentArrow), MAGIC_CONSUME_WAIT_PREVIEW);
            }
            break;
        default:
            return;
    }

    magic_arrow_info.type_change_timer--;
}

Gfx* Gfx_DrawRect_DropShadowEx(Gfx* gfx, u16 lorigin, u16 rorigin, s16 rectLeft, s16 rectTop, s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy,
                              s16 r, s16 g, s16 b, s16 a) {
    s16 dropShadowAlpha = a;

    if (a > 100) {
        dropShadowAlpha = 100;
    }

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, dropShadowAlpha);
    gEXTextureRectangle(gfx++, lorigin, rorigin, (rectLeft + 2) * 4, (rectTop + 2) * 4, (rectLeft + rectWidth + 2) * 4,
                        (rectTop + rectHeight + 2) * 4, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);

    gEXTextureRectangle(gfx++, lorigin, rorigin, rectLeft * 4, rectTop * 4, (rectLeft + rectWidth) * 4, (rectTop + rectHeight) * 4,
                        G_TX_RENDERTILE, 0, 0, dsdx, dtdy);

    return gfx;
}

#include "overlays/actors/ovl_En_Bom/z_en_bom.h"

bool Player_ItemIsInUse(Player* this, ItemId item);
PlayerItemAction Player_ItemToItemAction(Player* this, ItemId item);
EquipSlot func_8082FD0C(Player* this, PlayerItemAction itemAction);
void Player_UseItem(PlayState* play, Player* this, ItemId item);
void func_80838A20(PlayState* play, Player* this);
void func_80839978(PlayState* play, Player* this);
void func_80839A10(PlayState* play, Player* this);
extern s32 sPlayerHeldItemButtonIsHeldDown;

#define CHECK_ITEM_IS_BOW(item) ((item == ITEM_BOW) || ((item >= ITEM_BOW_FIRE) && (item <= ITEM_BOW_LIGHT)))

s32 func_808305BC(PlayState* play, Player* this, ItemId* item, ArrowType* typeParam);

bool deferBowMagicAudio = false;

#include "controller.h"

ItemId cyclingArrows[] = { ITEM_BOW, ITEM_BOW_FIRE, ITEM_BOW_ICE, ITEM_BOW_LIGHT };
int cyclingArrowCount = sizeof(cyclingArrows) / sizeof(cyclingArrows[4]);
int currentArrowIndex = 0;

u16 sPlayerItemButtons[] = {
    BTN_B,
    BTN_CLEFT,
    BTN_CDOWN,
    BTN_CRIGHT,
};

void CycleArrows(Player* this, PlayState* play, Input* input, bool using_r) {
    EquipSlot bowButton = EQUIP_SLOT_NONE;

    // Find button equipped with bow
    for (EquipSlot i = EQUIP_SLOT_C_LEFT; i <= EQUIP_SLOT_C_RIGHT; i++) {
        u8 equippedItem = gSaveContext.save.saveInfo.equips.buttonItems[0][i];
        if (CHECK_ITEM_IS_BOW(equippedItem)) {
            bowButton = i;
            break;
        }
    }

    if (bowButton == EQUIP_SLOT_NONE) {
        return;
    }

    bool bowButtonPressed = CHECK_BTN_ALL(input->press.button, sPlayerItemButtons[bowButton]);

    // Store the current value of the equipped bow button
    u8 previousBowItem = gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];

    // Check for shoulder press
    if (CHECK_BTN_ALL(input->press.button, using_r ? BTN_R : BTN_L)) {
        if ((magic_arrow_info.arrow_death_timer > 0) && (cyclingArrows[currentArrowIndex] != ITEM_BOW)) {
            Audio_PlaySfx(NA_SE_SY_ERROR);
            return;
        }

        do {
            currentArrowIndex++;

            if (currentArrowIndex >= cyclingArrowCount) {
                currentArrowIndex = 0;
            }
        } while (!((cyclingArrows[currentArrowIndex] == ITEM_BOW && INV_CONTENT(ITEM_BOW) == ITEM_BOW) ||
                   (cyclingArrows[currentArrowIndex] == ITEM_BOW_FIRE && INV_CONTENT(ITEM_ARROW_FIRE) == ITEM_ARROW_FIRE) ||
                   (cyclingArrows[currentArrowIndex] == ITEM_BOW_ICE && INV_CONTENT(ITEM_ARROW_ICE) == ITEM_ARROW_ICE) ||
                   (cyclingArrows[currentArrowIndex] == ITEM_BOW_LIGHT && INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_ARROW_LIGHT)));

        // Set the current item
        ItemId currentItem = cyclingArrows[currentArrowIndex];
        gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton] = currentItem;
        Interface_LoadItemIcon(play, bowButton);

        // Update held item action
        switch (currentItem) {
            case ITEM_BOW:
                this->heldItemAction = PLAYER_IA_BOW;
                this->itemAction = PLAYER_IA_BOW;
                break;
            case ITEM_BOW_FIRE:
                this->heldItemAction = PLAYER_IA_BOW_FIRE;
                this->itemAction = PLAYER_IA_BOW_FIRE;
                break;
            case ITEM_BOW_ICE:
                this->heldItemAction = PLAYER_IA_BOW_ICE;
                this->itemAction = PLAYER_IA_BOW_ICE;
                break;
            case ITEM_BOW_LIGHT:
                this->heldItemAction = PLAYER_IA_BOW_LIGHT;
                this->itemAction = PLAYER_IA_BOW_LIGHT;
                break;
            default:
                break;
        }
    }

    if (Player_IsAiming(this, play) && 
        !Player_IsHoldingHookshot(this)) {
        input->press.button &= ~BTN_R;
        input->press.button &= ~BTN_L;
    }

    // Kill current arrow and spawn new one upon cycling
    u8 bowItem = gSaveContext.save.saveInfo.equips.buttonItems [0] [bowButton]; 
    if ((bowButtonPressed && deferBowMagicAudio) || (bowItem != previousBowItem)) { 
        u8 magicArrowIndex = bowItem - ITEM_BOW_FIRE;
        if (this->heldActor != NULL) {
            u8 arrowType;
            if (magicArrowIndex >= 0 && magicArrowIndex <= 2) {
                arrowType = ARROW_TYPE_FIRE + magicArrowIndex;
            } else {
                arrowType = ARROW_TYPE_NORMAL;
            }

            Actor_Kill(this->heldActor);
            if (this->unk_B28 >= 0) {
                s32 var_v1 = ABS_ALT (this->unk_B28);
                ItemId item;
                ArrowMagic magicArrowType;

                if (this->unk_B28 >= 0) {
                    magicArrowType = ARROW_GET_MAGIC_FROM_TYPE (arrowType);

                    if ((ARROW_GET_MAGIC_FROM_TYPE (arrowType) >= ARROW_MAGIC_FIRE) && 
                    (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {

                        if (((void)0, gSaveContext.save.saveInfo.playerData.magic) < sMagicArrowCosts [magicArrowType]) { 
                            arrowType = ARROW_TYPE_NORMAL;
                            magicArrowType = ARROW_MAGIC_INVALID;
                        }
                    }
                }

                this->heldActor = Actor_SpawnAsChild(
                    &play->actorCtx, &this->actor, play, ACTOR_EN_ARROW, this->actor.world.pos.x,
                    this->actor.world.pos.y, this->actor.world.pos.z, 0, this->actor.shape.rot.y, 0, arrowType);
            }
            SetArrowMagicInfoHandler(this, play, previousBowItem, bowItem);
        }
        
        if (magicArrowIndex >= 0 && magicArrowIndex <= 2) {
            if (((gSaveContext.magicState == MAGIC_STATE_CONSUME) && (gSaveContext.save.saveInfo.playerData.magic >= sMagicArrowCosts[magicArrowIndex]))) {
                Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + magicArrowIndex);
                deferBowMagicAudio = false;
            } else {
                deferBowMagicAudio = true;
            }
        }
    }
}

// Patching magic arrow spawning.
void Player_SetUpperAction(PlayState* play, Player* this, PlayerUpperActionFunc upperActionFunc);

extern u16 D_8085CFB0[];

RECOMP_PATCH s32 func_808306F8(Player* this, PlayState* play) {
    // Kafei prevention.
    if (this->actor.id != ACTOR_PLAYER) {
        return false;
    }

    if ((this->heldItemAction >= PLAYER_IA_BOW_FIRE) && (this->heldItemAction <= PLAYER_IA_BOW_LIGHT) &&
        (gSaveContext.magicState != MAGIC_STATE_IDLE)) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
    } else {
        Player_SetUpperAction(play, this, Player_UpperAction_7);

        this->stateFlags3 |= PLAYER_STATE3_40;
        this->unk_ACC = 14;

        if (this->unk_B28 >= 0) {
            s32 var_v1 = ABS_ALT(this->unk_B28);
            ItemId item;
            ArrowType arrowType;
            ArrowMagic magicArrowType;

            if (var_v1 != 2) {
                Player_PlaySfx(this, D_8085CFB0[var_v1 - 1]);
            }

            if (!Player_IsHoldingHookshot(this) && (func_808305BC(play, this, &item, &arrowType) > 0)) {
                if (this->unk_B28 >= 0) {
                    magicArrowType = ARROW_GET_MAGIC_FROM_TYPE(arrowType);

                    if ((ARROW_GET_MAGIC_FROM_TYPE(arrowType) >= ARROW_MAGIC_FIRE) &&
                        (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {
                        if (((void)0, gSaveContext.save.saveInfo.playerData.magic) < sMagicArrowCosts[magicArrowType]) {
                            arrowType = ARROW_TYPE_NORMAL;
                            magicArrowType = ARROW_MAGIC_INVALID;
                        }
                    } else if ((arrowType == ARROW_TYPE_DEKU_BUBBLE) &&
                               (!CHECK_WEEKEVENTREG(WEEKEVENTREG_08_01) || (play->sceneId != SCENE_BOWLING))) {
                        magicArrowType = ARROW_MAGIC_DEKU_BUBBLE;
                    } else {
                        magicArrowType = ARROW_MAGIC_INVALID;
                    }

                    this->heldActor = Actor_SpawnAsChild(
                        &play->actorCtx, &this->actor, play, ACTOR_EN_ARROW, this->actor.world.pos.x,
                        this->actor.world.pos.y, this->actor.world.pos.z, 0, this->actor.shape.rot.y, 0, arrowType);

                    if ((this->heldActor != NULL) && (magicArrowType > ARROW_MAGIC_INVALID)) {
                        Magic_Consume(play, sMagicArrowCosts[magicArrowType], (this->transformation == PLAYER_FORM_DEKU ? MAGIC_CONSUME_NOW : MAGIC_CONSUME_WAIT_PREVIEW));
                    }
                }
            }
        }
        return true;
    }
    return false;
}

RECOMP_DECLARE_EVENT(recomp_on_play_main(PlayState* play)); 

// Handles draining magic when fired:
RECOMP_HOOK("func_80831194") void pre_func_80831194(PlayState* play, Player* this) {
    // Kafei prevention.
    if (this->actor.id != ACTOR_PLAYER) {
        return;
    }

    magic_arrow_info.arrow_death_timer = ARROW_DEATH_TIMER_MAX;
    if (gSaveContext.minigameStatus == MINIGAME_STATUS_ACTIVE || play->bButtonAmmoPlusOne != 0) {
        return;
    }
    
    if (this->heldActor != NULL) {
        if (!Player_IsHoldingHookshot(this)) {
            if (
                gSaveContext.magicState == MAGIC_CONSUME_WAIT_PREVIEW
            ) {
                gSaveContext.magicState = MAGIC_STATE_CONSUME;
            }
        }
    }
}

// Prevent minimap toggle while aiming if L config selected
RECOMP_HOOK("MapDisp_Update")
void pre_MapDisp_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    static int originalMapDisplayValue = -1;

    if (CFG_CYCLING_MODE == CYCLING_MODE_L) {
        if (Player_IsAiming(player, play) && !Player_IsHoldingHookshot(player)) {
            if (originalMapDisplayValue == -1) {
                originalMapDisplayValue = R_MINIMAP_DISABLED;
            }
            R_MINIMAP_DISABLED = originalMapDisplayValue;
            AudioSfx_StopById(NA_SE_SY_CAMERA_ZOOM_UP);
            AudioSfx_StopById(NA_SE_SY_CAMERA_ZOOM_DOWN);
        } else {
            if (originalMapDisplayValue != -1) {
                R_MINIMAP_DISABLED = originalMapDisplayValue;
                originalMapDisplayValue = -1;
            }
        }
    }
}

RECOMP_HOOK("Player_UpdateCommon") void pre_Player_UpdateCommon(Player* this, PlayState* play, Input* input) {
    // Kafei prevention.
    if (this->actor.id != ACTOR_PLAYER) {
        return;
    }

    // Prevent shielding while aiming if R config selected
    if (CFG_CYCLING_MODE == CYCLING_MODE_R) {
        if (Player_IsAiming(this, play) &&
            !Player_IsHoldingHookshot(this)) { 
            this->stateFlags1 &= ~PLAYER_STATE1_400000; 
            input->cur.button &= ~BTN_R; 
        } else {
            this->stateFlags1 |= PLAYER_STATE1_400000; 
        }
    }
    
    // If an arrow is destroyed, delay a few frames to make before switching is allowed.
    // Prevents crashes.
    if (magic_arrow_info.arrow_death_timer > 0) {
        magic_arrow_info.arrow_death_timer--;
    }

    // Cycle arrows upon button press
    if (shouldAllowArrowCycling(this, play) && 
        !Player_IsHoldingHookshot(this)  && 
        (CFG_CYCLING_MODE == CYCLING_MODE_L)) {
            CycleArrows(this, play, input, false);
    } else if (shouldAllowArrowCycling(this, play) &&
        !Player_IsHoldingHookshot(this)  && 
        (CFG_CYCLING_MODE == CYCLING_MODE_R)) {
            CycleArrows(this, play, input, true);
    }
    
    UpdateArrowMagicHandler(this, play);
}

