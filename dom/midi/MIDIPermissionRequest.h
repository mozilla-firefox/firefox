/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MIDIPermissionRequest_h
#define mozilla_dom_MIDIPermissionRequest_h

#include "mozilla/dom/Promise.h"
#include "nsContentPermissionHelper.h"

namespace mozilla::dom {

struct MIDIOptions;

/**
 * Handles permission dialog management when requesting MIDI permissions.
 */
class MIDIPermissionRequest final : public ContentPermissionRequestBase,
                                    public nsIRunnable {
 public:
  MIDIPermissionRequest(nsPIDOMWindowInner* aWindow, Promise* aPromise,
                        const MIDIOptions& aOptions);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MIDIPermissionRequest,
                                           ContentPermissionRequestBase)
  // nsIContentPermissionRequest
  NS_IMETHOD Cancel(void) override;
  NS_IMETHOD Allow(JS::Handle<JS::Value> choices) override;
  void GetPermissionOptions(nsTArray<nsString>& aOptions) const override;

 private:
  ~MIDIPermissionRequest() = default;
  nsresult DoPrompt();
  void CancelWithRandomizedDelay();

  // If we're canceling on a timer, we need to hold a strong ref while it's
  // outstanding.
  nsCOMPtr<nsITimer> mCancelTimer;

  // Promise for returning MIDIAccess on request success
  RefPtr<Promise> mPromise;
  // True if sysex permissions should be requested
  bool mNeedsSysex;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_MIDIPermissionRequest_h
