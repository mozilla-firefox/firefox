/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CycleCollectorUnlinkTester_h
#define mozilla_dom_CycleCollectorUnlinkTester_h

#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla::dom {

class GlobalObject;

// Test-only object that records whether the cycle collector has unlinked it,
// so tests can detect use-after-unlink bugs.
class CycleCollectorUnlinkTester final : public nsISupports,
                                         public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(CycleCollectorUnlinkTester)

  static already_AddRefed<CycleCollectorUnlinkTester> Constructor(
      const GlobalObject& aGlobal);

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  bool Unlinked() const { return mUnlinked; }

  void CrashIfUnlinked() const;

 private:
  explicit CycleCollectorUnlinkTester(nsIGlobalObject* aGlobal);
  ~CycleCollectorUnlinkTester() = default;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  bool mUnlinked = false;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_CycleCollectorUnlinkTester_h
