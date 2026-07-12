#ifndef ARROW_CYCLING_H_
#define ARROW_CYCLING_H_

#define ARROW_LIMB_MAX 5

#include <stdbool.h>

#include "PR/ultratypes.h"
#include "modding.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "z64item.h"
#include "z64play.h"
#include "z64player.h"

typedef enum {
  CYCLING_MODE_NONE,
  CYCLING_MODE_L,
  CYCLING_MODE_R,
} ArrowCycling;

RECOMP_IMPORT("*", u32 recomp_get_config_u32(const char* key));

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

typedef struct {
  ItemId item;
  u8 slot;
  bool (*is_available)();
} CyclingArrowEntry;

#define CYCLING_ARROW_BUILTIN_COUNT 4

#define CYCLING_ARROW_TYPES_MAX 16

extern u16 D_8085CFB0[];
extern u8 sMagicArrowCosts[];

void Player_SetUpperAction(PlayState* play, Player* this,
                           PlayerUpperActionFunc upperActionFunc);
s32 Player_UpperAction_7(Player* thisx, PlayState* play);
s32 Player_UpperAction_8(Player* thisx, PlayState* play);
bool Player_ItemIsInUse(Player* this, ItemId item);
PlayerItemAction Player_ItemToItemAction(Player* this, ItemId item);
EquipSlot func_8082FD0C(Player* this, PlayerItemAction itemAction);
void Player_UseItem(PlayState* play, Player* this, ItemId item);
void func_80838A20(PlayState* play, Player* this);
void func_80839978(PlayState* play, Player* this);
void func_80839A10(PlayState* play, Player* this);
extern s32 sPlayerHeldItemButtonIsHeldDown;

#define CHECK_ITEM_IS_BOW(item) \
  ((item == ITEM_BOW) || ((item >= ITEM_BOW_FIRE) && (item <= ITEM_BOW_LIGHT)))

s32 func_808305BC(PlayState* play, Player* this, ItemId* item,
                  ArrowType* typeParam);

RECOMP_EXPORT int AddArrowEntry(CyclingArrowEntry entry);
RECOMP_EXPORT int RemoveArrowEntry(int index);

#endif  // ARROW_CYCLING_H_
