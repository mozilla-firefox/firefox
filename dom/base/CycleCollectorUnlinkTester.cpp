/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CycleCollectorUnlinkTester.h"

#include "mozilla/Assertions.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CycleCollectorUnlinkTesterBinding.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(CycleCollectorUnlinkTester)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(CycleCollectorUnlinkTester)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CycleCollectorUnlinkTester)
  tmp->mUnlinked = true;
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(CycleCollectorUnlinkTester)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CycleCollectorUnlinkTester)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CycleCollectorUnlinkTester)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

CycleCollectorUnlinkTester::CycleCollectorUnlinkTester(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal) {}

/* static */
already_AddRefed<CycleCollectorUnlinkTester>
CycleCollectorUnlinkTester::Constructor(const GlobalObject& aGlobal) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  return do_AddRef(new CycleCollectorUnlinkTester(global));
}

JSObject* CycleCollectorUnlinkTester::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return CycleCollectorUnlinkTester_Binding::Wrap(aCx, this, aGivenProto);
}

void CycleCollectorUnlinkTester::CrashIfUnlinked() const {
  if (mUnlinked) {
    MOZ_CRASH("CycleCollectorUnlinkTester used after unlink");
  }
}

}  // namespace mozilla::dom
