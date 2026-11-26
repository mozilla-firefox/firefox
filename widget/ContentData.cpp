/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentData.h"

#include <fmt/format.h>

#include "TextEvents.h"
#include "mozilla/ToString.h"

namespace mozilla {

/******************************************************************************
 * ContentSelection
 ******************************************************************************/

ContentSelection::ContentSelection(
    const QueryContentResult& aQuerySelectionResult)
    : mWritingMode(aQuerySelectionResult.WritingModeRef()) {
  MOZ_ASSERT(aQuerySelectionResult.mEventMessage == eQuerySelectedText);
  if (aQuerySelectionResult.FoundSelectionRange()) {
    mOffsetAndData = Some(OffsetAndData<uint32_t>(
        aQuerySelectionResult.StartOffset(), aQuerySelectionResult.DataRef(),
        OffsetAndDataFor::SelectedString));
  }
}

std::string format_as(const ContentSelection& aContentSelection) {
  if (!aContentSelection.HasRange()) {
    return "{ HasRange()=false }";
  }
  return fmt::format("{{ mOffsetAndData={}, mWritingMode={} }}",
                     aContentSelection.mOffsetAndData,
                     ToString(aContentSelection.mWritingMode));
}

}  // namespace mozilla
