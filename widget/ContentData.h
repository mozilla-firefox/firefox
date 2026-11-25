/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ContentData_h
#define mozilla_ContentData_h

#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/WritingModes.h"
#include "mozilla/widget/IMEData.h"

/**
 * This file is intended for declaring classes which store DOM content data for
 * widget classes.  Those data should be retrived by `WidgetQueryContentEvent`,
 * notified with `IMENotification`, or set assumed data as result of dispatching
 * widget events such as `WidgetKeyboardEvent`, `WidgetCompositionEvent`,
 * `WidgetSelectionEvent` etc.
 */

namespace mozilla {

/**
 * ContentSelection stores DOM selection in flattend text by
 * ContentEventHandler.  It should be retrieved with `eQuerySelectedText` event,
 * notified with `NOTIFY_IME_OF_SELECTION_CHANGE` or set by widget itself.
 */
class ContentSelection {
 public:
  using SelectionChangeDataBase =
      widget::IMENotification::SelectionChangeDataBase;
  ContentSelection() = default;
  explicit ContentSelection(const SelectionChangeDataBase& aSelectionChangeData)
      : mOffsetAndData(Some(aSelectionChangeData.ToUint32OffsetAndData())),
        mWritingMode(aSelectionChangeData.GetWritingMode()) {}
  explicit ContentSelection(const WidgetQueryContentEvent& aSelectedTextEvent);
  ContentSelection(uint32_t aOffset, const WritingMode& aWritingMode)
      : mOffsetAndData(Some(OffsetAndData<uint32_t>(
            aOffset, EmptyString(), OffsetAndDataFor::SelectedString))),
        mWritingMode(aWritingMode) {}

  const OffsetAndData<uint32_t>& OffsetAndDataRef() const {
    return mOffsetAndData.ref();
  }

  void Collapse(uint32_t aOffset) {
    if (mOffsetAndData.isSome()) {
      mOffsetAndData->Collapse(aOffset);
    } else {
      mOffsetAndData.emplace(aOffset, EmptyString(),
                             OffsetAndDataFor::SelectedString);
    }
  }
  void Clear() {
    mOffsetAndData.reset();
    mWritingMode = WritingMode();
  }

  bool HasRange() const { return mOffsetAndData.isSome(); }
  const WritingMode& WritingModeRef() const { return mWritingMode; }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const ContentSelection& aContentSelection) {
    if (aContentSelection.HasRange()) {
      return aStream << "{ HasRange()=false }";
    }
    aStream << "{ mOffsetAndData=" << aContentSelection.mOffsetAndData
            << ", mWritingMode=" << aContentSelection.mWritingMode << " }";
    return aStream;
  }

 private:
  Maybe<OffsetAndData<uint32_t>> mOffsetAndData;
  WritingMode mWritingMode;
};

}  // namespace mozilla

#endif  // #ifndef mozilla_ContentData_h
