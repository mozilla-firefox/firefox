/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_PointerLockManager_h
#define mozilla_PointerLockManager_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/RefPtr.h"
#include "nsIWeakReferenceUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {
enum class StyleCursorKind : uint8_t;

namespace dom {
class BrowsingContext;
class BrowserParent;
enum class CallerType : uint32_t;
class Document;
class Element;
class Promise;
struct PointerLockOptions;
}  // namespace dom

class PointerLockManager final {
 public:
  // https://w3c.github.io/pointerlock/#dom-element-requestpointerlock
  // |aPromise| is the Promise returned to script; it is resolved when the
  // lock is acquired and rejected with a DOMException matching the spec
  // when any check fails.
  static void RequestLock(dom::Element* aElement,
                          const dom::PointerLockOptions& aOptions,
                          dom::CallerType aCallerType, dom::Promise* aPromise);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static void Unlock(const char* aReason, dom::Document* aDoc = nullptr);

  static bool IsLocked() { return sIsLocked; }

  static already_AddRefed<dom::Element> GetLockedElement();

  static already_AddRefed<dom::Document> GetLockedDocument();

  static dom::BrowserParent* GetLockedRemoteTarget();

  /**
   * Returns true if aContext and the current pointer locked document
   * have common top BrowsingContext.
   * Note that this method returns true only if caller is in the same process
   * as pointer locked document.
   */
  static bool IsInLockContext(mozilla::dom::BrowsingContext* aContext);

  // Set/release pointer lock remote target. Should only be called in parent
  // process.
  static void SetLockedRemoteTarget(dom::BrowserParent* aBrowserParent,
                                    nsACString& aError);
  static void ReleaseLockedRemoteTarget(dom::BrowserParent* aBrowserParent);

 private:
  class PointerLockRequest final : public Runnable {
   public:
    PointerLockRequest(dom::Element* aElement, bool aUserInputOrChromeCaller,
                       bool aUnadjustedMovement, dom::Promise* aPromise);
    MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() final;

   private:
    nsWeakPtr mElement;
    nsWeakPtr mDocument;
    bool mUserInputOrChromeCaller;
    bool mUnadjustedMovement;
    // Strong reference: the script-side lifetime of the returned Promise is
    // independent of the requested element's lifetime, so we keep it alive
    // until we resolve or reject it.
    RefPtr<dom::Promise> mPromise;
  };

  static void ChangePointerLockedElement(dom::Element* aElement,
                                         dom::Document* aDocument,
                                         dom::Element* aPointerLockedElement);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static bool StartSetPointerLock(dom::Element* aElement,
                                  dom::Document* aDocument,
                                  bool aUnadjustedMovement);

  MOZ_CAN_RUN_SCRIPT
  static bool SetPointerLock(dom::Element* aElement, dom::Document* aDocument,
                             StyleCursorKind, bool aUnadjustedMovement);

  static bool sIsLocked;
  static bool sIsLockUnadjustedMovement;
};

}  // namespace mozilla

#endif  // mozilla_PointerLockManager_h
