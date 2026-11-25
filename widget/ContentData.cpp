/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentData.h"

#include "TextEvents.h"

namespace mozilla {

/******************************************************************************
 * ContentSelection
 ******************************************************************************/

ContentSelection::ContentSelection(
    const WidgetQueryContentEvent& aSelectedTextEvent)
    : mWritingMode(aSelectedTextEvent.mReply->WritingModeRef()) {
  MOZ_ASSERT(aSelectedTextEvent.mMessage == eQuerySelectedText);
  MOZ_ASSERT(aSelectedTextEvent.Succeeded());
  if (aSelectedTextEvent.mReply->mOffsetAndData.isSome()) {
    mOffsetAndData =
        Some(OffsetAndData<uint32_t>(aSelectedTextEvent.mReply->StartOffset(),
                                     aSelectedTextEvent.mReply->DataRef(),
                                     OffsetAndDataFor::SelectedString));
  }
}

}  // namespace mozilla
