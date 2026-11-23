/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AsyncDBus.h"

#include "gio/gio.h"
#include "mozilla/XREAppData.h"
#include "nsAppShell.h"

namespace mozilla::widget {

static void CreateProxyCallback(GObject*, GAsyncResult* aResult,
                                gpointer aUserData) {
  RefPtr<DBusProxyPromise::Private> promise =
      dont_AddRef(static_cast<DBusProxyPromise::Private*>(aUserData));
  GUniquePtr<GError> error;
  RefPtr<GDBusProxy> proxy = dont_AddRef(
      g_dbus_proxy_new_for_bus_finish(aResult, getter_Transfers(error)));
  if (proxy) {
    promise->Resolve(std::move(proxy), __func__);
  } else {
    promise->Reject(std::move(error), __func__);
  }
  nsAppShell::DBusConnectionCheck();
}

RefPtr<DBusProxyPromise> CreateDBusProxyForBus(
    GBusType aBusType, GDBusProxyFlags aFlags,
    GDBusInterfaceInfo* aInterfaceInfo, const char* aName,
    const char* aObjectPath, const char* aInterfaceName,
    GCancellable* aCancellable) {
  nsAppShell::DBusConnectionCheck();
  auto promise = MakeRefPtr<DBusProxyPromise::Private>(__func__);
  g_dbus_proxy_new_for_bus(aBusType, aFlags, aInterfaceInfo, aName, aObjectPath,
                           aInterfaceName, aCancellable, CreateProxyCallback,
                           do_AddRef(promise).take());
  return promise.forget();
}

static void ProxyCallCallback(GObject* aSourceObject, GAsyncResult* aResult,
                              gpointer aUserData) {
  RefPtr<DBusCallPromise::Private> promise =
      dont_AddRef(static_cast<DBusCallPromise::Private*>(aUserData));
  GUniquePtr<GError> error;
  RefPtr<GVariant> result = dont_AddRef(g_dbus_proxy_call_finish(
      G_DBUS_PROXY(aSourceObject), aResult, getter_Transfers(error)));
  if (result) {
    promise->Resolve(std::move(result), __func__);
  } else {
    promise->Reject(std::move(error), __func__);
  }
  nsAppShell::DBusConnectionCheck();
}

RefPtr<DBusCallPromise> DBusProxyCall(GDBusProxy* aProxy, const char* aMethod,
                                      GVariant* aArgs, GDBusCallFlags aFlags,
                                      gint aTimeout,
                                      GCancellable* aCancellable) {
  auto promise = MakeRefPtr<DBusCallPromise::Private>(__func__);
  nsAppShell::DBusConnectionCheck();
  g_dbus_proxy_call(aProxy, aMethod, aArgs, aFlags, aTimeout, aCancellable,
                    ProxyCallCallback, do_AddRef(promise).take());
  return promise.forget();
}

static void ProxyCallWithUnixFDListCallback(GObject* aSourceObject,
                                            GAsyncResult* aResult,
                                            gpointer aUserData) {
  RefPtr<DBusCallFDListPromise::Private> promise =
      dont_AddRef(static_cast<DBusCallFDListPromise::Private*>(aUserData));
  GUniquePtr<GError> error;
  RefPtr<GUnixFDList> fdList;
  RefPtr<GVariant> result =
      dont_AddRef(g_dbus_proxy_call_with_unix_fd_list_finish(
          G_DBUS_PROXY(aSourceObject), getter_AddRefs(fdList), aResult,
          getter_Transfers(error)));
  if (result) {
    auto pair = std::make_pair<RefPtr<GVariant>, RefPtr<GUnixFDList>>(
        std::move(result), std::move(fdList));
    promise->Resolve(std::move(pair), __func__);
  } else {
    promise->Reject(std::move(error), __func__);
  }
  nsAppShell::DBusConnectionCheck();
}

RefPtr<DBusCallFDListPromise> DBusProxyCallWithUnixFDList(
    GDBusProxy* aProxy, const char* aMethod, GVariant* aArgs,
    GDBusCallFlags aFlags, gint aTimeout, GUnixFDList* aFDList,
    GCancellable* aCancellable) {
  auto promise = MakeRefPtr<DBusCallFDListPromise::Private>(__func__);
  nsAppShell::DBusConnectionCheck();
  g_dbus_proxy_call_with_unix_fd_list(
      aProxy, aMethod, aArgs, aFlags, aTimeout, aFDList, aCancellable,
      ProxyCallWithUnixFDListCallback, do_AddRef(promise).take());
  return promise.forget();
}

// Tokens should be unique and not guessable. To avoid clashes with calls
// made from unrelated libraries, it is a good idea to use a per-library
// prefix combined with a random number.
// Here, we build the token by concatenating MOZ_APP_NAME (e.g. "firefox"),
// with some unique name from the caller, plus a serial and random number.
void MakePortalRequestToken(const nsCString& aType, nsACString& aToken) {
  static Atomic<unsigned, MemoryOrdering::Relaxed> sTokenSerial;
  aToken.Truncate();
  aToken.AppendPrintf(MOZ_APP_NAME "_%s_%u_%u", aType.get(), sTokenSerial++,
                      g_random_int());
  XREAppData::SanitizeNameForDBus(aToken);
}

// The portal request paths are well known:
//
//   Since version 0.9 of xdg-desktop-portal, the handle will be of the form
//   /org/freedesktop/portal/desktop/request/SENDER/TOKEN
//   where SENDER is the callers unique name, with the initial ':' removed and
//   all '.' replaced by '_', and TOKEN is a unique token that the caller
//   provided with the handle_token key in the options vardict.
//
// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html#org-freedesktop-portal-request
void GetPortalRequestPath(GDBusProxy* aProxy, const nsCString& aRequestToken,
                          nsACString& aOutPath) {
  aOutPath.Truncate();
  GDBusConnection* connection = g_dbus_proxy_get_connection(aProxy);
  nsAutoCString senderName(g_dbus_connection_get_unique_name(connection));
  senderName.ReplaceChar('.', '_');
  aOutPath.AppendPrintf("/org/freedesktop/portal/desktop/request/%s/%s",
                        senderName.get() + 1, aRequestToken.get());
}

struct PortalResponseData {
  explicit PortalResponseData(PortalResponseListener aCallback)
      : mCallback(std::move(aCallback)) {
    MOZ_COUNT_CTOR(PortalResponseData);
  }
  MOZ_COUNTED_DTOR(PortalResponseData);

  PortalResponseListener mCallback;
  guint mSubscriptionId = 0;

  static void Release(gpointer aData) {
    delete static_cast<PortalResponseData*>(aData);
  }

  static void Fired(GDBusConnection* connection, const gchar* sender_name,
                    const gchar* object_path, const gchar* interface_name,
                    const gchar* signal_name, GVariant* parameters,
                    gpointer user_data) {
    nsAppShell::DBusConnectionCheck();
    auto* data = static_cast<PortalResponseData*>(user_data);
    auto callback = std::move(data->mCallback);
    g_dbus_connection_signal_unsubscribe(connection, data->mSubscriptionId);
    data = nullptr;  // Dangling now!
    callback(parameters);
  }
};

guint OnDBusPortalResponse(GDBusProxy* aProxy, const nsCString& aRequestToken,
                           PortalResponseListener aCallback) {
  nsAppShell::DBusConnectionCheck();
  auto boxedData = std::make_unique<PortalResponseData>(std::move(aCallback));

  nsAutoCString requestPath;
  GetPortalRequestPath(aProxy, aRequestToken, requestPath);

  auto* data = boxedData.get();
  guint subscriptionId = g_dbus_connection_signal_subscribe(
      g_dbus_proxy_get_connection(aProxy), "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.Request", "Response", requestPath.get(), nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE, PortalResponseData::Fired, boxedData.release(),
      PortalResponseData::Release);
  data->mSubscriptionId = subscriptionId;
  return subscriptionId;
}

}  // namespace mozilla::widget
