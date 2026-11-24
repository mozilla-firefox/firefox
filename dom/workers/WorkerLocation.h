/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_location_h_
#define mozilla_dom_location_h_

#include "WorkerCommon.h"
#include "WorkerPrivate.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class WorkerLocation final : public nsWrapperCache {
  nsCOMPtr<nsIURI> mURI;

  explicit WorkerLocation(nsIURI* aURI) : mURI(aURI) {
    MOZ_COUNT_CTOR(WorkerLocation);
  }

  MOZ_COUNTED_DTOR(WorkerLocation)

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WorkerLocation)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WorkerLocation)

  static already_AddRefed<WorkerLocation> Create(nsIURI* aURI);

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  nsISupports* GetParentObject() const { return nullptr; }

  void Stringify(nsACString& aHref) const { GetHref(aHref); }
  void GetHref(nsACString& aHref) const;
  void GetProtocol(nsACString& aProtocol) const;
  void GetHost(nsACString& aHost) const;
  void GetHostname(nsACString& aHostname) const;
  void GetPort(nsACString& aPort) const;
  void GetPathname(nsACString& aPathname) const;
  void GetSearch(nsACString& aSearch) const;
  void GetHash(nsACString& aHash) const;
  void GetOrigin(nsACString& aOrigin) const;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_location_h_
