#ifndef ARROW_CYCLING_H_
#define ARROW_CYCLING_H_

#include <stdbool.h>

#include "PR/ultratypes.h"
#include "z64item.h"

typedef bool (*ArrowAvailabilityFunc)(void);

typedef struct {
  ItemId item;
  u8 slot;
  ArrowAvailabilityFunc is_available;
} CyclingArrowEntry;

#endif  // ARROW_CYCLING_H_
