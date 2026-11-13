/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CoalescedMouseData_h
#define mozilla_dom_CoalescedMouseData_h

#include "CoalescedInputData.h"
#include "mozilla/MouseEvents.h"
#include "nsRefreshObservers.h"

class nsRefreshDriver;

namespace mozilla::dom {

class CoalescedMouseData final : public CoalescedInputData<WidgetMouseEvent> {
 public:
  CoalescedMouseData() { MOZ_COUNT_CTOR(mozilla::dom::CoalescedMouseData); }

  ~CoalescedMouseData() { MOZ_COUNT_DTOR(mozilla::dom::CoalescedMouseData); }

  void Coalesce(const WidgetMouseEvent& aMouseOrPointerEvent,
                const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
                const nsID& aUUID);

  bool CanCoalesce(const WidgetMouseEvent& aMouseMoveEvent,
                   const ScrollableLayerGuid& aGuid,
                   const uint64_t& aInputBlockId,
                   const nsRefreshDriver* aRefreshDriver);

  nsID GetUUID() const { return mUUID; }

 private:
  // UUID used for parent process to track X1/X2 clicks, which we send back to
  // the parent process through SendReplyMouseEvent so that it can do the
  // default behavior.
  nsID mUUID = {};
};

class CoalescedMouseMoveFlusher final : public CoalescedInputFlusher {
 public:
  explicit CoalescedMouseMoveFlusher(BrowserChild* aBrowserChild);

  MOZ_CAN_RUN_SCRIPT void WillRefresh(mozilla::TimeStamp aTime) override;

 private:
  ~CoalescedMouseMoveFlusher() override;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_CoalescedMouseData_h
