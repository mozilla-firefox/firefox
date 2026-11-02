/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SerialPermissionRequest_h
#define mozilla_dom_SerialPermissionRequest_h

#include "mozilla/MozPromise.h"
#include "mozilla/dom/SerialPortIPCTypes.h"
#include "mozilla/dom/SerialTypes.h"
#include "nsCOMPtr.h"
#include "nsContentPermissionHelper.h"
#include "nsITimer.h"

class nsIPrincipal;

namespace mozilla::dom {

class Element;
class WindowGlobalParent;

// Promise returned by SerialPermissionRequest::Run(). Resolves with the
// IPCSerialPortInfo of the port the user picked, or rejects with a
// RequestPortReason describing why the chooser did not grant a port.
using SerialChooserPromise =
    MozPromise<IPCSerialPortInfo, RequestPortReason, /* IsExclusive */ true>;

// Parent-process implementation of nsIContentPermissionRequest used to drive
// the WebSerial chooser. The chrome prompt service routes "serial" requests
// to the SerialPermissionPrompt JS class which reads the available ports out
// of the request's options array and posts a doorhanger to the user.
//
// On user grant the request resolves its chooser promise with the picked
// port; on cancel/error it rejects with a RequestPortReason.
class SerialPermissionRequest final
    : public ContentPermissionRequestBaseNoWindow {
 public:
  SerialPermissionRequest(WindowGlobalParent* aWindowGlobalParent,
                          bool aAutoselect,
                          nsTArray<IPCSerialPortInfo>&& aPorts);

  NS_DECL_ISUPPORTS
  NS_IMETHOD GetElement(mozilla::dom::Element** aElement) override;
  void GetPermissionOptions(nsTArray<nsString>& aOptions) const override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD Cancel() override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD Allow(JS::Handle<JS::Value> choices) override;

  // Begin the request: runs sitepermsaddon checks, then either auto-resolves
  // or invokes nsContentPermissionUtils::AskPermission to show the chooser.
  // Returns the promise that will be settled when the chooser completes.
  RefPtr<SerialChooserPromise> Run();

 private:
  ~SerialPermissionRequest();

  bool IsSitePermAllow() const;
  bool IsSitePermDeny() const;
  bool ShouldShowAddonGate() const;
  void CancelWithRandomizedDelay(RequestPortReason aReason);
  nsresult DoPrompt();
  void ResolveWithPort(const IPCSerialPortInfo& aPort);
  void ResolveCancelled(RequestPortReason aReason);

  RefPtr<WindowGlobalParent> mWindowGlobalParent;
  bool mAutoselect;
  nsTArray<IPCSerialPortInfo> mPorts;
  MozPromiseHolder<SerialChooserPromise> mPromiseHolder;
  nsCOMPtr<nsITimer> mCancelTimer;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SerialPermissionRequest_h
