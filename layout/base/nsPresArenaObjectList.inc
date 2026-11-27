/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a list of all types that can be allocated in an nsPresArena, for
   preprocessing */

// These are objects that can be stored in the pres arena
#define FRAME_ID(classname, ...) PRES_ARENA_OBJECT(classname)
#define ABSTRACT_FRAME_ID(classname)
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
PRES_ARENA_OBJECT(nsLineBox)
PRES_ARENA_OBJECT(nsFrameList)
PRES_ARENA_OBJECT(CustomCounterStyle)
PRES_ARENA_OBJECT(DependentBuiltinCounterStyle)
PRES_ARENA_OBJECT(nsCallbackEventRequest)
PRES_ARENA_OBJECT(nsIntervalSet_Interval)
PRES_ARENA_OBJECT(CellData)
PRES_ARENA_OBJECT(BCCellData)
