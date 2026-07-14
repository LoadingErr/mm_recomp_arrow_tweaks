#include "arrow_cycling.h"

// z_en_arrow.h expects this game-specific limb count to be defined first.
#define ARROW_LIMB_MAX 5
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "recomputils.h"

#define CYCLING_ARROW_BUILTIN_COUNT 4
#define CYCLING_ARROW_TYPES_MAX 16

RECOMP_IMPORT("*", u32 recomp_get_config_u32(const char* key));

typedef enum {
  CYCLING_MODE_NONE,
  CYCLING_MODE_L,
  CYCLING_MODE_R,
} ArrowCycling;

#define CFG_CYCLING_MODE ((ArrowCycling)recomp_get_config_u32("arrow_cycling"))

enum {
  kArrowDeathCooldownFrames = 40,
  kMagicArrowChangeResetFrame = 3,
  kMagicArrowChangeWaitFrame = 2,
  kMagicArrowChangeConsumeFrame = 1,
};

typedef struct {
  int typeChangeTimer;
  ItemId previousBowItem;
  ItemId currentBowItem;
  int arrowDeathTimer;
} ArrowMagicChangeState;

extern u16 D_8085CFB0[];
extern u8 sMagicArrowCosts[];

void Player_SetUpperAction(PlayState* play, Player* player,
                           PlayerUpperActionFunc upperActionFunc);
s32 Player_UpperAction_7(Player* player, PlayState* play);
s32 Player_UpperAction_8(Player* player, PlayState* play);
s32 func_808305BC(PlayState* play, Player* player, ItemId* item,
                  ArrowType* typeParam);

static ArrowMagicChangeState sArrowMagicChange;
static bool sDeferBowMagicAudio;
static int sCyclingArrowCount = CYCLING_ARROW_BUILTIN_COUNT;
static int sCurrentArrowIndex;

static bool IsInventoryItemAvailable(ItemId item) {
  return INV_CONTENT(item) == item;
}

static bool IsNormalArrowAvailable(void) {
  return IsInventoryItemAvailable(ITEM_BOW);
}

static bool IsFireArrowAvailable(void) {
  return IsInventoryItemAvailable(ITEM_ARROW_FIRE);
}

static bool IsIceArrowAvailable(void) {
  return IsInventoryItemAvailable(ITEM_ARROW_ICE);
}

static bool IsLightArrowAvailable(void) {
  return IsInventoryItemAvailable(ITEM_ARROW_LIGHT);
}

static CyclingArrowEntry sCyclingArrows[CYCLING_ARROW_TYPES_MAX] = {
    {ITEM_BOW, SLOT_BOW, IsNormalArrowAvailable},
    {ITEM_BOW_FIRE, SLOT_BOW, IsFireArrowAvailable},
    {ITEM_BOW_ICE, SLOT_BOW, IsIceArrowAvailable},
    {ITEM_BOW_LIGHT, SLOT_BOW, IsLightArrowAvailable},
};

static bool IsMagicBowItem(ItemId item) {
  return item >= ITEM_BOW_FIRE && item <= ITEM_BOW_LIGHT;
}

static bool IsBowItem(ItemId item) {
  return item == ITEM_BOW || IsMagicBowItem(item);
}

static ArrowType GetArrowTypeForBowItem(ItemId bowItem) {
  if (!IsMagicBowItem(bowItem)) {
    return ARROW_TYPE_NORMAL;
  }

  return (ArrowType)(ARROW_TYPE_FIRE + bowItem - ITEM_BOW_FIRE);
}

static ArrowMagic GetMagicArrowType(ArrowType arrowType) {
  if (!ARROW_IS_MAGICAL(arrowType)) {
    return ARROW_MAGIC_INVALID;
  }

  return (ArrowMagic)ARROW_GET_MAGIC_FROM_TYPE(arrowType);
}

static u8 GetMagicArrowCost(ItemId bowItem) {
  ArrowMagic magicArrow = GetMagicArrowType(GetArrowTypeForBowItem(bowItem));

  if (magicArrow == ARROW_MAGIC_INVALID) {
    return 0;
  }

  return sMagicArrowCosts[magicArrow];
}

static bool HasEnoughMagic(ArrowMagic magicArrow) {
  return gSaveContext.save.saveInfo.playerData.magic >=
         sMagicArrowCosts[magicArrow];
}

static ArrowType GetAffordableArrowType(ArrowType arrowType) {
  ArrowMagic magicArrow = GetMagicArrowType(arrowType);

  if (magicArrow != ARROW_MAGIC_INVALID && !HasEnoughMagic(magicArrow)) {
    return ARROW_TYPE_NORMAL;
  }

  return arrowType;
}

static ArrowMagic ResolveNockedArrowMagic(PlayState* play,
                                          ArrowType* arrowType) {
  ArrowMagic magicArrow = GetMagicArrowType(*arrowType);

  if (magicArrow != ARROW_MAGIC_INVALID) {
    if (!HasEnoughMagic(magicArrow)) {
      *arrowType = ARROW_TYPE_NORMAL;
      return ARROW_MAGIC_INVALID;
    }
    return magicArrow;
  }

  if (*arrowType == ARROW_TYPE_DEKU_BUBBLE &&
      (!CHECK_WEEKEVENTREG(WEEKEVENTREG_08_01) ||
       play->sceneId != SCENE_BOWLING)) {
    return ARROW_MAGIC_DEKU_BUBBLE;
  }

  return ARROW_MAGIC_INVALID;
}

static bool Player_IsHoldingBow(const Player* player) {
  switch (player->heldItemAction) {
    case PLAYER_IA_BOW:
    case PLAYER_IA_BOW_FIRE:
    case PLAYER_IA_BOW_ICE:
    case PLAYER_IA_BOW_LIGHT:
      return true;
    default:
      return false;
  }
}

static bool Player_IsAimingBow(const Player* player) {
  return Player_IsHoldingBow(player) &&
         (player->upperActionFunc == Player_UpperAction_8 ||
          player->upperActionFunc == Player_UpperAction_7);
}

static bool Player_IsArrowNocked(const Player* player) {
  return Player_IsHoldingBow(player) &&
         player->upperActionFunc == Player_UpperAction_7;
}

static bool Player_CanCycleArrows(Player* player) {
  return Player_IsAimingBow(player) && !Player_IsHoldingHookshot(player);
}

static bool Player_IsUsingMagicBow(const Player* player) {
  return player->heldItemAction >= PLAYER_IA_BOW_FIRE &&
         player->heldItemAction <= PLAYER_IA_BOW_LIGHT;
}

static bool IsMainPlayer(const Player* player) {
  return player->actor.id == ACTOR_PLAYER;
}

static Actor* SpawnNockedArrow(Player* player, PlayState* play,
                               ArrowType arrowType) {
  return Actor_SpawnAsChild(
      &play->actorCtx, &player->actor, play, ACTOR_EN_ARROW,
      player->actor.world.pos.x, player->actor.world.pos.y,
      player->actor.world.pos.z, 0, player->actor.shape.rot.y, 0, arrowType);
}

static void ScheduleArrowMagicChange(const Player* player,
                                     ItemId previousBowItem,
                                     ItemId currentBowItem) {
  sArrowMagicChange.previousBowItem = previousBowItem;
  sArrowMagicChange.currentBowItem = currentBowItem;

  if (Player_IsArrowNocked(player)) {
    sArrowMagicChange.typeChangeTimer = kMagicArrowChangeResetFrame;
  }
}

static void UpdateArrowMagicChange(PlayState* play) {
  switch (sArrowMagicChange.typeChangeTimer) {
    case kMagicArrowChangeResetFrame:
      if (sArrowMagicChange.currentBowItem == ITEM_BOW) {
        Magic_Reset(play);
      }
      break;
    case kMagicArrowChangeWaitFrame:
      // Leave one frame for the previous magic preview to settle.
      break;
    case kMagicArrowChangeConsumeFrame:
      if (sArrowMagicChange.currentBowItem != ITEM_BOW) {
        Magic_Consume(play, GetMagicArrowCost(sArrowMagicChange.currentBowItem),
                      MAGIC_CONSUME_WAIT_PREVIEW);
      }
      break;
    default:
      return;
  }

  sArrowMagicChange.typeChangeTimer--;
}

static bool IsEmptyArrowEntry(const CyclingArrowEntry* entry) {
  return entry->is_available == NULL;
}

static bool IsArrowEntryAvailable(const CyclingArrowEntry* entry) {
  return !IsEmptyArrowEntry(entry) && entry->is_available();
}

static const u16 sPlayerItemButtons[] = {
    BTN_B,
    BTN_CLEFT,
    BTN_CDOWN,
    BTN_CRIGHT,
};

static EquipSlot FindBowButton(void) {
  for (EquipSlot button = EQUIP_SLOT_C_LEFT; button <= EQUIP_SLOT_C_RIGHT;
       button++) {
    ItemId item = gSaveContext.save.saveInfo.equips.buttonItems[0][button];
    if (IsBowItem(item)) {
      return button;
    }
  }

  return EQUIP_SLOT_NONE;
}

static void SyncCurrentArrowIndex(EquipSlot bowButton) {
  u8 equippedItem = gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];
  u8 equippedSlot = C_SLOT_EQUIP(0, bowButton) & 0xFF;

  for (int i = 0; i < sCyclingArrowCount; i++) {
    CyclingArrowEntry* entry = &sCyclingArrows[i];
    if (!IsEmptyArrowEntry(entry) && entry->item == equippedItem &&
        entry->slot == equippedSlot) {
      sCurrentArrowIndex = i;
      return;
    }
  }
}

static void SelectNextAvailableArrow(void) {
  do {
    sCurrentArrowIndex++;
    if (sCurrentArrowIndex >= sCyclingArrowCount) {
      sCurrentArrowIndex = 0;
    }
  } while (!IsArrowEntryAvailable(&sCyclingArrows[sCurrentArrowIndex]));
}

static void UpdatePlayerBowAction(Player* player, ItemId bowItem) {
  PlayerItemAction itemAction;

  switch (bowItem) {
    case ITEM_BOW:
      itemAction = PLAYER_IA_BOW;
      break;
    case ITEM_BOW_FIRE:
      itemAction = PLAYER_IA_BOW_FIRE;
      break;
    case ITEM_BOW_ICE:
      itemAction = PLAYER_IA_BOW_ICE;
      break;
    case ITEM_BOW_LIGHT:
      itemAction = PLAYER_IA_BOW_LIGHT;
      break;
    default:
      return;
  }

  player->heldItemAction = itemAction;
  player->itemAction = itemAction;
}

static void EquipArrow(PlayState* play, Player* player, EquipSlot bowButton,
                       const CyclingArrowEntry* entry) {
  gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton] = entry->item;
  C_SLOT_EQUIP(0, bowButton) = entry->slot;
  Interface_LoadItemIcon(play, bowButton);
  UpdatePlayerBowAction(player, entry->item);
}

static void ReplaceNockedArrow(Player* player, PlayState* play,
                               ItemId previousBowItem, ItemId currentBowItem) {
  ArrowType arrowType = GetArrowTypeForBowItem(currentBowItem);

  Actor_Kill(player->heldActor);
  if (player->unk_B28 >= 0) {
    player->heldActor =
        SpawnNockedArrow(player, play, GetAffordableArrowType(arrowType));
  }

  ScheduleArrowMagicChange(player, previousBowItem, currentBowItem);
}

static void UpdateBowMagicAudio(ItemId bowItem, bool selectedArrowChanged) {
  ArrowMagic magicArrow = GetMagicArrowType(GetArrowTypeForBowItem(bowItem));

  if (magicArrow != ARROW_MAGIC_INVALID) {
    if (selectedArrowChanged) {
      Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + magicArrow);
    }
    sDeferBowMagicAudio = false;
    return;
  }

  if (selectedArrowChanged) {
    Audio_PlaySfx(NA_SE_PL_CHANGE_ARMS);
  }
  sDeferBowMagicAudio = true;
}

static void CycleArrows(Player* player, PlayState* play, Input* input,
                        u16 cycleButton) {
  EquipSlot bowButton = FindBowButton();
  if (bowButton == EQUIP_SLOT_NONE) {
    return;
  }

  SyncCurrentArrowIndex(bowButton);

  bool bowButtonPressed =
      CHECK_BTN_ALL(input->press.button, sPlayerItemButtons[bowButton]);
  ItemId previousBowItem =
      gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];

  if (CHECK_BTN_ALL(input->press.button, cycleButton)) {
    if (sArrowMagicChange.arrowDeathTimer > 0 &&
        sCyclingArrows[sCurrentArrowIndex].item != ITEM_BOW) {
      Audio_PlaySfx(NA_SE_SY_ERROR);
      return;
    }

    SelectNextAvailableArrow();
    EquipArrow(play, player, bowButton, &sCyclingArrows[sCurrentArrowIndex]);
  }

  if (Player_CanCycleArrows(player)) {
    input->press.button &= ~(BTN_R | BTN_L);
  }

  ItemId currentBowItem =
      gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];
  bool refreshNockedArrow = (bowButtonPressed && sDeferBowMagicAudio) ||
                            currentBowItem != previousBowItem;
  if (!refreshNockedArrow) {
    return;
  }

  if (player->heldActor != NULL) {
    ReplaceNockedArrow(player, play, previousBowItem, currentBowItem);
  }

  bool selectedArrowChanged =
      sCyclingArrows[sCurrentArrowIndex].item != previousBowItem;
  UpdateBowMagicAudio(currentBowItem, selectedArrowChanged);
}

static void TrySpawnNockedArrow(Player* player, PlayState* play) {
  ItemId item;
  ArrowType arrowType;

  if (Player_IsHoldingHookshot(player) ||
      func_808305BC(play, player, &item, &arrowType) <= 0 ||
      player->unk_B28 < 0) {
    return;
  }

  ArrowMagic magicArrow = ResolveNockedArrowMagic(play, &arrowType);
  player->heldActor = SpawnNockedArrow(player, play, arrowType);

  if (player->heldActor != NULL && magicArrow > ARROW_MAGIC_INVALID) {
    Magic_Consume(play, sMagicArrowCosts[magicArrow],
                  player->transformation == PLAYER_FORM_DEKU
                      ? MAGIC_CONSUME_NOW
                      : MAGIC_CONSUME_WAIT_PREVIEW);
  }
}

RECOMP_CALLBACK("*", recomp_on_init) void on_startup() {
  sArrowMagicChange.typeChangeTimer = 0;
  sArrowMagicChange.arrowDeathTimer = 0;
}

RECOMP_PATCH s32 func_808306F8(Player* player, PlayState* play) {
  // This hook also runs for Kafei's actor, which must not be modified.
  if (!IsMainPlayer(player)) {
    return false;
  }

  if (Player_IsUsingMagicBow(player) &&
      gSaveContext.magicState != MAGIC_STATE_IDLE) {
    Audio_PlaySfx(NA_SE_SY_ERROR);
    return false;
  }

  Player_SetUpperAction(play, player, Player_UpperAction_7);
  player->stateFlags3 |= PLAYER_STATE3_40;
  player->unk_ACC = 14;

  if (player->unk_B28 >= 0) {
    s32 nockIndex = ABS_ALT(player->unk_B28);
    if (nockIndex != 2) {
      Player_PlaySfx(player, D_8085CFB0[nockIndex - 1]);
    }

    TrySpawnNockedArrow(player, play);
  }

  return true;
}

// Handles draining magic when an arrow is fired.
RECOMP_HOOK("func_80831194")
void pre_func_80831194(PlayState* play, Player* player) {
  // This hook also runs for Kafei's actor, which must not be modified.
  if (!IsMainPlayer(player)) {
    return;
  }

  sArrowMagicChange.arrowDeathTimer = kArrowDeathCooldownFrames;
  if (gSaveContext.minigameStatus == MINIGAME_STATUS_ACTIVE ||
      play->bButtonAmmoPlusOne != 0 || player->heldActor == NULL ||
      Player_IsHoldingHookshot(player)) {
    return;
  }

  if (gSaveContext.magicState == MAGIC_CONSUME_WAIT_PREVIEW) {
    gSaveContext.magicState = MAGIC_STATE_CONSUME;
  }
}

// Prevent the minimap toggle while cycling with L.
RECOMP_HOOK("MapDisp_Update")
void pre_MapDisp_Update(PlayState* play) {
  Player* player = GET_PLAYER(play);
  static int sSavedMinimapDisabledValue = -1;

  if (CFG_CYCLING_MODE != CYCLING_MODE_L) {
    return;
  }

  if (Player_CanCycleArrows(player)) {
    if (sSavedMinimapDisabledValue == -1) {
      sSavedMinimapDisabledValue = R_MINIMAP_DISABLED;
    }
    R_MINIMAP_DISABLED = sSavedMinimapDisabledValue;
    AudioSfx_StopById(NA_SE_SY_CAMERA_ZOOM_UP);
    AudioSfx_StopById(NA_SE_SY_CAMERA_ZOOM_DOWN);
    return;
  }

  if (sSavedMinimapDisabledValue != -1) {
    R_MINIMAP_DISABLED = sSavedMinimapDisabledValue;
    sSavedMinimapDisabledValue = -1;
  }
}

RECOMP_HOOK("Player_UpdateCommon")
void pre_Player_UpdateCommon(Player* player, PlayState* play, Input* input) {
  // This hook also runs for Kafei's actor, which must not be modified.
  if (!IsMainPlayer(player)) {
    return;
  }

  ArrowCycling cyclingMode = CFG_CYCLING_MODE;
  bool canCycleArrows = Player_CanCycleArrows(player);

  // Prevent shielding while aiming when cycling with R.
  if (cyclingMode == CYCLING_MODE_R) {
    if (canCycleArrows) {
      player->stateFlags1 &= ~PLAYER_STATE1_400000;
      input->cur.button &= ~BTN_R;
    } else {
      player->stateFlags1 |= PLAYER_STATE1_400000;
    }
  }

  // Wait briefly after an arrow is destroyed before allowing a switch.
  if (sArrowMagicChange.arrowDeathTimer > 0) {
    sArrowMagicChange.arrowDeathTimer--;
  }

  if (canCycleArrows) {
    if (cyclingMode == CYCLING_MODE_L) {
      CycleArrows(player, play, input, BTN_L);
    } else if (cyclingMode == CYCLING_MODE_R) {
      CycleArrows(player, play, input, BTN_R);
    }
  }

  UpdateArrowMagicChange(play);
}

RECOMP_EXPORT int AddArrowEntry(CyclingArrowEntry entry) {
  // Reuse removed entries so other mods can retain their stable indices.
  for (int i = CYCLING_ARROW_BUILTIN_COUNT; i < sCyclingArrowCount; i++) {
    if (IsEmptyArrowEntry(&sCyclingArrows[i])) {
      sCyclingArrows[i] = entry;
      return i;
    }
  }

  if (sCyclingArrowCount >= CYCLING_ARROW_TYPES_MAX) {
    recomp_printf("Arrow type capacity exceeded!");
    return -1;
  }

  int entryIndex = sCyclingArrowCount;
  sCyclingArrows[entryIndex] = entry;
  sCyclingArrowCount++;
  return entryIndex;
}

RECOMP_EXPORT int RemoveArrowEntry(int index) {
  // Built-ins guarantee a valid base arrow, so only external entries can be
  // removed.
  if (index < CYCLING_ARROW_BUILTIN_COUNT || index >= sCyclingArrowCount) {
    return -1;
  }

  // Keep the entry's slot intact; indices returned to other mods stay valid.
  sCyclingArrows[index].is_available = NULL;

  // Empty entries after the final live one no longer belong to the list.
  while (sCyclingArrowCount > CYCLING_ARROW_BUILTIN_COUNT &&
         IsEmptyArrowEntry(&sCyclingArrows[sCyclingArrowCount - 1])) {
    sCyclingArrowCount--;
  }

  if (sCurrentArrowIndex == index) {
    sCurrentArrowIndex = 0;
  }

  return 0;
}
