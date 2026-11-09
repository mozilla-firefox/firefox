/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ModemManagerImeiProvider.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/widget/AsyncDBus.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

static LazyLogModule gModemManagerImeiLog("ModemManagerImei");

#define MMI_LOG(level, ...) \
  MOZ_LOG(gModemManagerImeiLog, mozilla::LogLevel::level, (__VA_ARGS__))

static StaticRefPtr<ModemManagerImeiProvider> sInstance;

/* static */
already_AddRefed<ModemManagerImeiProvider>
ModemManagerImeiProvider::GetInstance() {
  if (!sInstance) {
    sInstance = new ModemManagerImeiProvider();
    ClearOnShutdown(&sInstance);
    // Pending D-Bus calls hold a reference to the provider, so they have to be
    // cancelled before the last reference goes away.
    RunOnShutdown(
        [] {
          if (sInstance) {
            g_cancellable_cancel(sInstance->mCancellable);
          }
        },
        ShutdownPhase::XPCOMWillShutdown);
  }

  RefPtr<ModemManagerImeiProvider> service = sInstance.get();
  return service.forget();
}

static const char* const kModemManagerBusName = "org.freedesktop.ModemManager1";
static const char* const kModemRootPath =
    "/org/freedesktop/ModemManager1/Modem";
static const char* const kModemInterface =
    "org.freedesktop.ModemManager1.Modem";
static const char* const kDbusIntrospectable =
    "org.freedesktop.DBus.Introspectable";

// Keeps only the first and last three characters, enough to check the value
// without logging the whole identifier.
static nsAutoCString MaskImei(const nsACString& aImei) {
  if (aImei.Length() <= 6) {
    return nsAutoCString("***");
  }
  nsAutoCString masked(Substring(aImei, 0, 3));
  masked.AppendLiteral("...");
  masked.Append(Substring(aImei, aImei.Length() - 3));
  return masked;
}

NS_IMPL_ISUPPORTS(ModemManagerImeiProvider, nsIImeiProvider)

NS_IMETHODIMP
ModemManagerImeiProvider::GetImei(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> promise = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  CollectImei(promise);

  promise.forget(aResult);
  return NS_OK;
}

void ModemManagerImeiProvider::QueryModemImei(RefPtr<Promise> aPromise,
                                              const nsCString& aRealModemPath) {
  MMI_LOG(Debug, "%s query modem %s", __PRETTY_FUNCTION__,
          aRealModemPath.get());

  widget::CreateDBusProxyForBus(
      G_BUS_TYPE_SYSTEM, GDBusProxyFlags(G_DBUS_PROXY_FLAGS_NONE), nullptr,
      kModemManagerBusName, aRealModemPath.get(), kModemInterface, mCancellable)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aPromise](RefPtr<GDBusProxy>&& aProxy) {
            RefPtr<GVariant> variant =
                dont_AddRef(g_dbus_proxy_get_cached_property(
                    aProxy, "EquipmentIdentifier"));

            if (!variant) {
              MMI_LOG(Error, "Failure getting cached EquipmentIdentifier");
              aPromise->MaybeReject(NS_ERROR_ILLEGAL_VALUE);
              return;
            }

            if (!g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING)) {
              MMI_LOG(Error, "Unexpected EquipmentIdentifier type: %s",
                      g_variant_get_type_string(variant));
              aPromise->MaybeReject(NS_ERROR_ILLEGAL_VALUE);
              return;
            }

            nsDependentCString imei(g_variant_get_string(variant, nullptr));
            MMI_LOG(Debug, "IMEI: %s", MaskImei(imei).get());

            aPromise->MaybeResolve(imei);
          },
          [aPromise](GUniquePtr<GError>&& aError) {
            MMI_LOG(Error, "Failed to get modem: %s", aError->message);
            aPromise->MaybeReject(NS_ERROR_INVALID_ARG);
          });
}

void ModemManagerImeiProvider::OnIntrospectFinish(RefPtr<Promise> aPromise,
                                                  RefPtr<GVariant> aResult) {
  const gchar* introspectXml;
  g_variant_get(aResult, "(&s)", &introspectXml);

  GUniquePtr<GError> error;
  RefPtr<GDBusNodeInfo> nodeInfos = dont_AddRef(
      g_dbus_node_info_new_for_xml(introspectXml, getter_Transfers(error)));
  if (!nodeInfos) {
    MMI_LOG(Error, "g_dbus_node_info_new_for_xml() failed! %s", error->message);
    aPromise->MaybeReject(NS_ERROR_FAILURE);
    return;
  }

  if (!nodeInfos->nodes) {
    MMI_LOG(Error, "nodeInfos->nodes failed! %s",
            error ? error->message : "(noerror)");
    aPromise->MaybeReject(NS_ERROR_FAILURE);
    return;
  }

  GDBusNodeInfo* modem = nodeInfos->nodes[0];
  if (!modem) {
    aPromise->MaybeReject(NS_ERROR_NOT_IMPLEMENTED);
    return;
  }

  MMI_LOG(Debug, "Query IMEI: %s", modem->path);
  QueryModemImei(aPromise,
                 nsPrintfCString("%s/%s", kModemRootPath, modem->path));
}

void ModemManagerImeiProvider::CollectImei(RefPtr<Promise> aPromise) {
  widget::CreateDBusProxyForBus(
      G_BUS_TYPE_SYSTEM, GDBusProxyFlags(G_DBUS_PROXY_FLAGS_NONE), nullptr,
      kModemManagerBusName, kModemRootPath, kDbusIntrospectable, mCancellable)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aPromise, self = RefPtr{this}](RefPtr<GDBusProxy>&& aProxy) {
            widget::DBusProxyCall(aProxy, "Introspect", nullptr,
                                  G_DBUS_CALL_FLAGS_NONE, -1,
                                  self->mCancellable)
                ->Then(
                    GetCurrentSerialEventTarget(), __func__,
                    [aPromise, self](RefPtr<GVariant>&& aResult) {
                      self->OnIntrospectFinish(std::move(aPromise),
                                               std::move(aResult));
                    },
                    [aPromise](GUniquePtr<GError>&& aError) {
                      MMI_LOG(Error, "Failed to introspect modems: %s",
                              aError->message);
                      aPromise->MaybeReject(NS_ERROR_FAILURE);
                    });
          },
          [aPromise](GUniquePtr<GError>&& aError) {
            MMI_LOG(Error, "Failed to get modems: %s", aError->message);
            aPromise->MaybeReject(NS_ERROR_NOT_AVAILABLE);
          });
}

}  // namespace mozilla::dom
