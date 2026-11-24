/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WorkerLocation.h"

#include "mozilla/dom/WorkerLocationBinding.h"
#include "nsContentUtils.h"
#include "nsIURI.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WorkerLocation)

/* static */
already_AddRefed<WorkerLocation> WorkerLocation::Create(nsIURI* aURI) {
  MOZ_ASSERT(aURI);
  return do_AddRef(new WorkerLocation(aURI));
}

void WorkerLocation::GetHref(nsACString& aHref) const {
  if (NS_FAILED(mURI->GetSpec(aHref))) {
    aHref.Truncate();
  }
}

void WorkerLocation::GetProtocol(nsACString& aProtocol) const {
  if (NS_FAILED(mURI->GetScheme(aProtocol))) {
    aProtocol.Truncate();
    return;
  }
  aProtocol.Append(':');
}

void WorkerLocation::GetHost(nsACString& aHost) const {
  if (NS_FAILED(mURI->GetHostPort(aHost))) {
    aHost.Truncate();
  }
}

void WorkerLocation::GetHostname(nsACString& aHostname) const {
  nsContentUtils::GetHostOrIPv6WithBrackets(mURI, aHostname);
}

void WorkerLocation::GetPort(nsACString& aPort) const {
  aPort.Truncate();
  int32_t port;
  if (NS_SUCCEEDED(mURI->GetPort(&port)) && port != -1) {
    aPort.AppendInt(port);
  }
}

void WorkerLocation::GetPathname(nsACString& aPathname) const {
  if (NS_FAILED(mURI->GetFilePath(aPathname))) {
    aPathname.Truncate();
  }
}

void WorkerLocation::GetSearch(nsACString& aSearch) const {
  aSearch.Truncate();
  nsAutoCString query;
  if (NS_SUCCEEDED(mURI->GetQuery(query)) && !query.IsEmpty()) {
    aSearch.Assign('?');
    aSearch.Append(query);
  }
}

void WorkerLocation::GetHash(nsACString& aHash) const {
  aHash.Truncate();
  nsAutoCString ref;
  if (NS_SUCCEEDED(mURI->GetRef(ref)) && !ref.IsEmpty()) {
    aHash.Assign('#');
    aHash.Append(ref);
  }
}

void WorkerLocation::GetOrigin(nsACString& aOrigin) const {
  if (NS_FAILED(
          nsContentUtils::GetWebExposedOriginSerialization(mURI, aOrigin))) {
    aOrigin.Truncate();
  }
}

JSObject* WorkerLocation::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return WorkerLocation_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
