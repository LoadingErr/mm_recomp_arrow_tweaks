#include "./arrow_cycling.h"

#include "recomputils.h"

RECOMP_CALLBACK("*", recomp_on_init) void on_startup() {
  magic_arrow_info.type_change_timer = 0;
  magic_arrow_info.arrow_death_timer = 0;
}

static bool normal_arrow_is_available() {
  return INV_CONTENT(ITEM_BOW) == ITEM_BOW;
}
static bool fire_arrow_is_available() {
  return INV_CONTENT(ITEM_ARROW_FIRE) == ITEM_ARROW_FIRE;
}
static bool ice_arrow_is_available() {
  return INV_CONTENT(ITEM_ARROW_ICE) == ITEM_ARROW_ICE;
}
static bool light_arrow_is_available() {
  return INV_CONTENT(ITEM_ARROW_LIGHT) == ITEM_ARROW_LIGHT;
}

CyclingArrowEntry cyclingArrows[CYCLING_ARROW_TYPES_MAX] = {
    {ITEM_BOW, SLOT_BOW, normal_arrow_is_available},
    {ITEM_BOW_FIRE, SLOT_BOW, fire_arrow_is_available},
    {ITEM_BOW_ICE, SLOT_BOW, ice_arrow_is_available},
    {ITEM_BOW_LIGHT, SLOT_BOW, light_arrow_is_available},
};

static bool Player_isHoldingBow(Player* this, PlayState* play) {
  return (this->heldItemAction == PLAYER_IA_BOW ||
          this->heldItemAction == PLAYER_IA_BOW_FIRE ||
          this->heldItemAction == PLAYER_IA_BOW_ICE ||
          this->heldItemAction == PLAYER_IA_BOW_LIGHT);
}

static bool Player_IsAiming(Player* this, PlayState* play) {
  return (Player_isHoldingBow(this, play)) &&
         (this->upperActionFunc == Player_UpperAction_8 ||
          this->upperActionFunc == Player_UpperAction_7);
}

static bool Player_IsArrowNocked(Player* this, PlayState* play) {
  return ((Player_isHoldingBow(this, play)) &&
          (this->upperActionFunc == Player_UpperAction_7));
}

static u8 getArrowMagic(u8 bowItem) {
  u8 magicArrowIndex = bowItem - ITEM_BOW_FIRE;
  u8 arrowType;
  if (magicArrowIndex >= 0 && magicArrowIndex <= 2) {
    arrowType = ARROW_TYPE_FIRE + magicArrowIndex;
  } else {
    arrowType = ARROW_TYPE_NORMAL;
  }

  ArrowMagic magicArrowType = ARROW_GET_MAGIC_FROM_TYPE(arrowType);

  if ((ARROW_GET_MAGIC_FROM_TYPE(arrowType) >= ARROW_MAGIC_FIRE) &&
      (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {
    return sMagicArrowCosts[magicArrowType];
  }

  return 0;
}

static void SetArrowMagicInfoHandler(Player* this, PlayState* play,
                                     u8 lastArrow, u8 currentArrow) {
  magic_arrow_info.lastArrow = lastArrow;
  magic_arrow_info.currentArrow = currentArrow;
  if (Player_IsArrowNocked(this, play)) {
    magic_arrow_info.type_change_timer = 3;
  }
}

static void UpdateArrowMagicHandler(Player* this, PlayState* play) {
  switch (magic_arrow_info.type_change_timer) {
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
        Magic_Consume(play, getArrowMagic(magic_arrow_info.currentArrow),
                      MAGIC_CONSUME_WAIT_PREVIEW);
      }
      break;
    default:
      return;
  }

  magic_arrow_info.type_change_timer--;
}

bool deferBowMagicAudio = false;

int cyclingArrowCount = CYCLING_ARROW_BUILTIN_COUNT;
int currentArrowIndex = 0;

RECOMP_EXPORT int AddArrowEntry(CyclingArrowEntry entry) {
  // Removed external entries leave empty slots so their indices stay valid for
  // other mods. Reuse one before extending the queue.
  for (int i = CYCLING_ARROW_BUILTIN_COUNT; i < cyclingArrowCount; i++) {
    if (cyclingArrows[i].is_available == NULL) {
      cyclingArrows[i] = entry;
      return i;
    }
  }

  if (cyclingArrowCount < CYCLING_ARROW_TYPES_MAX) {
    cyclingArrows[cyclingArrowCount] = entry;
    return cyclingArrowCount++;
  }

  recomp_printf("Arrow type capacity exceeded!");
  return -1;
}

RECOMP_EXPORT int RemoveArrowEntry(int index) {
  // Built-in entries guarantee that there is always a valid base arrow to
  // cycle to, so only externally registered entries can be removed.
  if (index < CYCLING_ARROW_BUILTIN_COUNT || index >= cyclingArrowCount) {
    return -1;
  }

  // Preserve the indices of other external entries. CycleArrows ignores empty
  // slots, and AddArrowEntry will reuse them.
  cyclingArrows[index].is_available = NULL;

  // Entries after the last live one are not part of the queue anymore.
  while (cyclingArrowCount > CYCLING_ARROW_BUILTIN_COUNT &&
         cyclingArrows[cyclingArrowCount - 1].is_available == NULL) {
    cyclingArrowCount--;
  }

  if (currentArrowIndex == index) {
    currentArrowIndex = 0;
  }

  return 0;
}

static u16 sPlayerItemButtons[] = {
    BTN_B,
    BTN_CLEFT,
    BTN_CDOWN,
    BTN_CRIGHT,
};

static void CycleArrows(Player* this, PlayState* play, Input* input,
                        bool using_r) {
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

  // Update currentArrowIndex based on the currently equipped bow item
  u8 equippedItem = gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];
  u8 equippedSlot = C_SLOT_EQUIP(0, bowButton) & 0xFF;

  for (int i = 0; i < cyclingArrowCount; i++) {
    if ((cyclingArrows[i].is_available != NULL) &&
        (cyclingArrows[i].item == equippedItem) &&
        (cyclingArrows[i].slot == equippedSlot)) {
      currentArrowIndex = i;
      break;
    }
  }

  bool bowButtonPressed =
      CHECK_BTN_ALL(input->press.button, sPlayerItemButtons[bowButton]);

  // Store the current value of the equipped bow button
  u8 previousBowItem =
      gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];

  // Check for shoulder press
  if (CHECK_BTN_ALL(input->press.button, using_r ? BTN_R : BTN_L)) {
    if ((magic_arrow_info.arrow_death_timer > 0) &&
        (cyclingArrows[currentArrowIndex].item != ITEM_BOW)) {
      Audio_PlaySfx(NA_SE_SY_ERROR);
      return;
    }

    do {
      currentArrowIndex++;

      if (currentArrowIndex >= cyclingArrowCount) {
        currentArrowIndex = 0;
      }
    } while ((cyclingArrows[currentArrowIndex].is_available == NULL) ||
             !cyclingArrows[currentArrowIndex].is_available());

    // Set the current item
    ItemId currentItem = cyclingArrows[currentArrowIndex].item;
    EquipSlot currentSlot = cyclingArrows[currentArrowIndex].slot;
    gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton] = currentItem;
    C_SLOT_EQUIP(0, bowButton) = currentSlot;
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

  if (Player_IsAiming(this, play) && !Player_IsHoldingHookshot(this)) {
    input->press.button &= ~BTN_R;
    input->press.button &= ~BTN_L;
  }

  // Kill current arrow and spawn new one upon cycling
  u8 bowItem = gSaveContext.save.saveInfo.equips.buttonItems[0][bowButton];
  if ((bowButtonPressed && deferBowMagicAudio) ||
      (bowItem != previousBowItem)) {
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
        s32 var_v1 = ABS_ALT(this->unk_B28);
        ItemId item;
        ArrowMagic magicArrowType;

        if (this->unk_B28 >= 0) {
          magicArrowType = ARROW_GET_MAGIC_FROM_TYPE(arrowType);

          if ((ARROW_GET_MAGIC_FROM_TYPE(arrowType) >= ARROW_MAGIC_FIRE) &&
              (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {
            if (((void)0, gSaveContext.save.saveInfo.playerData.magic) <
                sMagicArrowCosts[magicArrowType]) {
              arrowType = ARROW_TYPE_NORMAL;
              magicArrowType = ARROW_MAGIC_INVALID;
            }
          }
        }

        this->heldActor = Actor_SpawnAsChild(
            &play->actorCtx, &this->actor, play, ACTOR_EN_ARROW,
            this->actor.world.pos.x, this->actor.world.pos.y,
            this->actor.world.pos.z, 0, this->actor.shape.rot.y, 0, arrowType);
      }
      SetArrowMagicInfoHandler(this, play, previousBowItem, bowItem);
    }

    if (magicArrowIndex >= 0 && magicArrowIndex <= 2) {
      if (cyclingArrows[currentArrowIndex].item != previousBowItem) {
        // Play the sound effect for the newly selected magic arrow
        Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + magicArrowIndex);
      }
      deferBowMagicAudio = false;
    } else {
      if (cyclingArrows[currentArrowIndex].item != previousBowItem) {
        // Play the sound effect for switching to non-magic arrows
        Audio_PlaySfx(NA_SE_PL_CHANGE_ARMS);
      }
      deferBowMagicAudio = true;
    }
  }
}

RECOMP_PATCH s32 func_808306F8(Player* this, PlayState* play) {
  // Kafei prevention.
  if (this->actor.id != ACTOR_PLAYER) {
    return false;
  }

  if ((this->heldItemAction >= PLAYER_IA_BOW_FIRE) &&
      (this->heldItemAction <= PLAYER_IA_BOW_LIGHT) &&
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

      if (!Player_IsHoldingHookshot(this) &&
          (func_808305BC(play, this, &item, &arrowType) > 0)) {
        if (this->unk_B28 >= 0) {
          magicArrowType = ARROW_GET_MAGIC_FROM_TYPE(arrowType);

          if ((ARROW_GET_MAGIC_FROM_TYPE(arrowType) >= ARROW_MAGIC_FIRE) &&
              (ARROW_GET_MAGIC_FROM_TYPE(arrowType) <= ARROW_MAGIC_LIGHT)) {
            if (((void)0, gSaveContext.save.saveInfo.playerData.magic) <
                sMagicArrowCosts[magicArrowType]) {
              arrowType = ARROW_TYPE_NORMAL;
              magicArrowType = ARROW_MAGIC_INVALID;
            }
          } else if ((arrowType == ARROW_TYPE_DEKU_BUBBLE) &&
                     (!CHECK_WEEKEVENTREG(WEEKEVENTREG_08_01) ||
                      (play->sceneId != SCENE_BOWLING))) {
            magicArrowType = ARROW_MAGIC_DEKU_BUBBLE;
          } else {
            magicArrowType = ARROW_MAGIC_INVALID;
          }

          this->heldActor = Actor_SpawnAsChild(
              &play->actorCtx, &this->actor, play, ACTOR_EN_ARROW,
              this->actor.world.pos.x, this->actor.world.pos.y,
              this->actor.world.pos.z, 0, this->actor.shape.rot.y, 0,
              arrowType);

          if ((this->heldActor != NULL) &&
              (magicArrowType > ARROW_MAGIC_INVALID)) {
            Magic_Consume(play, sMagicArrowCosts[magicArrowType],
                          (this->transformation == PLAYER_FORM_DEKU
                               ? MAGIC_CONSUME_NOW
                               : MAGIC_CONSUME_WAIT_PREVIEW));
          }
        }
      }
    }
    return true;
  }
  return false;
}

// Handles draining magic when fired:
RECOMP_HOOK("func_80831194")
void pre_func_80831194(PlayState* play, Player* this) {
  // Kafei prevention.
  if (this->actor.id != ACTOR_PLAYER) {
    return;
  }

  magic_arrow_info.arrow_death_timer = ARROW_DEATH_TIMER_MAX;
  if (gSaveContext.minigameStatus == MINIGAME_STATUS_ACTIVE ||
      play->bButtonAmmoPlusOne != 0) {
    return;
  }

  if (this->heldActor != NULL) {
    if (!Player_IsHoldingHookshot(this)) {
      if (gSaveContext.magicState == MAGIC_CONSUME_WAIT_PREVIEW) {
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

RECOMP_HOOK("Player_UpdateCommon")
void pre_Player_UpdateCommon(Player* this, PlayState* play, Input* input) {
  // Kafei prevention.
  if (this->actor.id != ACTOR_PLAYER) {
    return;
  }

  // Prevent shielding while aiming if R config selected
  if (CFG_CYCLING_MODE == CYCLING_MODE_R) {
    if (Player_IsAiming(this, play) && !Player_IsHoldingHookshot(this)) {
      this->stateFlags1 &= ~PLAYER_STATE1_400000;
      input->cur.button &= ~BTN_R;
    } else {
      this->stateFlags1 |= PLAYER_STATE1_400000;
    }
  }

  // If an arrow is destroyed, delay a few frames to make before switching is
  // allowed. Prevents crashes.
  if (magic_arrow_info.arrow_death_timer > 0) {
    magic_arrow_info.arrow_death_timer--;
  }

  // Cycle arrows upon button press
  if (Player_IsAiming(this, play) && !Player_IsHoldingHookshot(this) &&
      (CFG_CYCLING_MODE == CYCLING_MODE_L)) {
    CycleArrows(this, play, input, false);
  } else if (Player_IsAiming(this, play) && !Player_IsHoldingHookshot(this) &&
             (CFG_CYCLING_MODE == CYCLING_MODE_R)) {
    CycleArrows(this, play, input, true);
  }

  UpdateArrowMagicHandler(this, play);
}
