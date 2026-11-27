/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_DisplayListArenaObjectId_h
#define mozilla_DisplayListArenaObjectId_h

#include <cstdint>

namespace mozilla {

enum class DisplayListArenaObjectId : uint8_t {
#define DISPLAY_LIST_ARENA_OBJECT(name_) name_,
#include "nsDisplayListArenaTypes.inc"
#undef DISPLAY_LIST_ARENA_OBJECT
  COUNT
};

}  // namespace mozilla

#endif
