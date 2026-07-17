# Majora's Mask: Recompiled - Arrow Cycling 🏹🔥❄️☀️

Cycle between available arrow types while aiming the Hero's Bow. Configure the
cycling button to **L** or **R** in the mod settings, then hold the bow out and
press that button to select the next available arrow.

Arrow Cycling supports the normal, Fire, Ice, and Light Arrows out of the box.
Mods such as Combo Arrows can add their own selectable arrow entries.

## Usage

1. Equip the Hero's Bow to a C-button.
2. Hold the button to aim.
3. Press the configured cycling button to select the next arrow type.

Only arrow types currently available in the inventory are selected. The normal
Hero's Bow remains the fallback entry.

## Extending Arrow Cycling

Mods can add an item/slot pair to Arrow Cycling through the public
[`src/arrow_cycling.h`](src/arrow_cycling.h) descriptor. Declare Arrow Cycling
as a required dependency, then import its registration functions:

```c
#include "arrow_cycling.h"
#include "modding.h"

RECOMP_IMPORT("mm_recomp_arrow_cycling",
              int AddArrowEntry(CyclingArrowEntry entry));
RECOMP_IMPORT("mm_recomp_arrow_cycling", int RemoveArrowEntry(int index));
```

```c
static bool MyArrowIsAvailable(void) {
  return true;
}

static CyclingArrowEntry sMyArrowEntry = {
    ITEM_BOW,
    SLOT_BOMBCHU,
    MyArrowIsAvailable,
};

static int sMyArrowEntryIndex = -1;

RECOMP_CALLBACK("*", recomp_on_init)
void my_mod_init(void) {
  sMyArrowEntryIndex = AddArrowEntry(sMyArrowEntry);
}
```

`AddArrowEntry` returns a stable index, or `-1` when the 16-entry registry is
full. Use an item/slot pair that is unique among cycling entries. `is_available`
is called while cycling and must be non-null, fast, and safe to call repeatedly.
The entry and callback must remain valid until it is removed.

`RemoveArrowEntry` accepts only externally-added entries, returns `0` on
success, and does not shift other entries. A removed slot may later be reused.
The built-in normal and elemental arrow entries cannot be removed.

Registering an entry makes it selectable; it does not itself create a new
projectile type. A custom item still needs its own equipment and arrow-spawn
behavior, as [Combo Arrows](https://github.com/a-priestley/MMRecompComboArrows) provides.
