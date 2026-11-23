/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_AsyncDBus_h
#define mozilla_widget_AsyncDBus_h

#include <utility>

#include "mozilla/GRefPtr.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/MozPromise.h"

namespace mozilla::widget {

using DBusProxyPromise = MozPromise<RefPtr<GDBusProxy>, GUniquePtr<GError>,
                                    /* IsExclusive = */ true>;

using DBusCallPromise = MozPromise<RefPtr<GVariant>, GUniquePtr<GError>,
                                   /* IsExclusive = */ true>;

using DBusCallFDListPromise =
    MozPromise<std::pair<RefPtr<GVariant>, RefPtr<GUnixFDList>>,
               GUniquePtr<GError>,
               /* IsExclusive = */ true>;

RefPtr<DBusProxyPromise> CreateDBusProxyForBus(
    GBusType aBusType, GDBusProxyFlags aFlags,
    GDBusInterfaceInfo* aInterfaceInfo, const char* aName,
    const char* aObjectPath, const char* aInterfaceName,
    GCancellable* aCancellable = nullptr);

RefPtr<DBusCallPromise> DBusProxyCall(GDBusProxy*, const char* aMethod,
                                      GVariant* aArgs, GDBusCallFlags,
                                      gint aTimeout = -1,
                                      GCancellable* = nullptr);

RefPtr<DBusCallFDListPromise> DBusProxyCallWithUnixFDList(
    GDBusProxy*, const char* aMethod, GVariant* aArgs, GDBusCallFlags,
    gint aTimeout = -1, GUnixFDList* = nullptr, GCancellable* = nullptr);

// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html#org-freedesktop-portal-request
void MakePortalRequestToken(const nsCString& aType, nsACString& aOutToken);
void GetPortalRequestPath(GDBusProxy*, const nsCString& aRequestToken,
                          nsACString& aOutPath);
using PortalResponseListener = std::function<void(GVariant*)>;
guint OnDBusPortalResponse(GDBusProxy* aProxy, const nsCString& aRequestToken,
                           PortalResponseListener);

}  // namespace mozilla::widget

#endif
