/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGNOMEShellSearchProvider.h"

#include "nsToolkitCompsCID.h"
#include "base/message_loop.h"  // for MessageLoop
#include "base/task.h"          // for NewRunnableMethod, etc
#include "mozilla/gfx/2D.h"
#include "nsComponentManagerUtils.h"
#include "nsIIOService.h"
#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "nsImportModule.h"
#include "nsIOpenTabsProvider.h"
#include "imgIContainer.h"
#include "imgITools.h"
#include "mozilla/places/nsFaviconService.h"

using namespace mozilla;
using namespace mozilla::gfx;

// Mozilla has old GIO version in build roots
#define G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE GBusNameOwnerFlags(1 << 2)

static const char* introspect_template =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection "
    "1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.gnome.Shell.SearchProvider2\">\n"
    "   <method name=\"GetInitialResultSet\">\n"
    "     <arg type=\"as\" name=\"terms\" direction=\"in\" />\n"
    "     <arg type=\"as\" name=\"results\" direction=\"out\" />\n"
    "   </method>\n"
    "   <method name=\"GetSubsearchResultSet\">\n"
    "     <arg type=\"as\" name=\"previous_results\" direction=\"in\" />\n"
    "     <arg type=\"as\" name=\"terms\" direction=\"in\" />\n"
    "     <arg type=\"as\" name=\"results\" direction=\"out\" />\n"
    "   </method>\n"
    "   <method name=\"GetResultMetas\">\n"
    "     <arg type=\"as\" name=\"identifiers\" direction=\"in\" />\n"
    "     <arg type=\"aa{sv}\" name=\"metas\" direction=\"out\" />\n"
    "   </method>\n"
    "   <method name=\"ActivateResult\">\n"
    "     <arg type=\"s\" name=\"identifier\" direction=\"in\" />\n"
    "     <arg type=\"as\" name=\"terms\" direction=\"in\" />\n"
    "     <arg type=\"u\" name=\"timestamp\" direction=\"in\" />\n"
    "   </method>\n"
    "   <method name=\"LaunchSearch\">\n"
    "     <arg type=\"as\" name=\"terms\" direction=\"in\" />\n"
    "     <arg type=\"u\" name=\"timestamp\" direction=\"in\" />\n"
    "   </method>\n"
    "</interface>\n"
    "</node>\n";

// Inspired by SurfaceToPackedBGRA
static UniquePtr<uint8_t[]> SurfaceToPackedRGBA(DataSourceSurface* aSurface) {
  IntSize size = aSurface->GetSize();
  CheckedInt<size_t> bufferSize =
      CheckedInt<size_t>(size.width * 4) * CheckedInt<size_t>(size.height);
  if (!bufferSize.isValid()) {
    return nullptr;
  }
  UniquePtr<uint8_t[]> imageBuffer(new (std::nothrow)
                                       uint8_t[bufferSize.value()]);
  if (!imageBuffer) {
    return nullptr;
  }

  DataSourceSurface::MappedSurface map;
  if (!aSurface->Map(DataSourceSurface::MapType::READ, &map)) {
    return nullptr;
  }

  // Convert BGRA to RGBA
  uint32_t* aSrc = (uint32_t*)map.mData;
  uint32_t* aDst = (uint32_t*)imageBuffer.get();
  for (int i = 0; i < size.width * size.height; i++, aDst++, aSrc++) {
    *aDst = *aSrc & 0xff00ff00;
    *aDst |= (*aSrc & 0xff) << 16;
    *aDst |= (*aSrc & 0xff0000) >> 16;
  }

  aSurface->Unmap();

  return imageBuffer;
}

static nsresult UpdateHistoryIcon(
    const places::FaviconPromise::ResolveOrRejectValue& aPromiseResult,
    const RefPtr<nsGNOMEShellHistorySearchResult>& aSearchResult,
    int aIconIndex, int aTimeStamp) {
  // This is a callback from some previous search so we don't want it
  if (aTimeStamp != aSearchResult->GetTimeStamp()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIFavicon> favicon =
      aPromiseResult.IsResolve() ? aPromiseResult.ResolveValue() : nullptr;
  if (!favicon) {
    return NS_ERROR_FAILURE;
  }

  // Get favicon content.
  nsTArray<uint8_t> rawData;
  nsresult rv = favicon->GetRawData(rawData);
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString mimeType;
  rv = favicon->GetMimeType(mimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  // Decode the image from the format it was returned to us in (probably PNG)
  nsCOMPtr<imgIContainer> container;
  nsCOMPtr<imgITools> imgtool = do_CreateInstance("@mozilla.org/image/tools;1");
  rv = imgtool->DecodeImageFromBuffer(
      reinterpret_cast<const char*>(rawData.Elements()), rawData.Length(),
      mimeType, getter_AddRefs(container));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<SourceSurface> surface = container->GetFrame(
      imgIContainer::FRAME_FIRST,
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY);

  if (!surface || surface->GetFormat() != SurfaceFormat::B8G8R8A8) {
    return NS_ERROR_FAILURE;
  }

  // Allocate a new buffer that we own.
  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  UniquePtr<uint8_t[]> data = SurfaceToPackedRGBA(dataSurface);
  if (!data) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aSearchResult->SetHistoryIcon(aTimeStamp, std::move(data),
                                surface->GetSize().width,
                                surface->GetSize().height, aIconIndex);
  return NS_OK;
}

void nsGNOMEShellSearchProvider::HandleSearchResultSet(
    GVariant* aParameters, GDBusMethodInvocation* aInvocation,
    bool aInitialSearch) {
  // Discard any existing search results.
  mSearchResult = nullptr;

  RefPtr<nsGNOMEShellHistorySearchResult> newSearch =
      new nsGNOMEShellHistorySearchResult(this, mConnection,
                                          mSearchResultTimeStamp);
  mSearchResultTimeStamp++;
  newSearch->SetTimeStamp(mSearchResultTimeStamp);

  // Send the search request over DBus. We'll get reply over DBus it will be
  // set to mSearchResult by nsGNOMEShellSearchProvider::SetSearchResult().
  DBusHandleResultSet(newSearch.forget(), aParameters, aInitialSearch,
                      aInvocation);
}

void nsGNOMEShellSearchProvider::HandleResultMetas(
    GVariant* aParameters, GDBusMethodInvocation* aInvocation) {
  if (mSearchResult) {
    DBusHandleResultMetas(mSearchResult, aParameters, aInvocation);
  }
}

void nsGNOMEShellSearchProvider::ActivateResult(
    GVariant* aParameters, GDBusMethodInvocation* aInvocation) {
  if (mSearchResult) {
    DBusActivateResult(mSearchResult, aParameters, aInvocation);
  }
}

void nsGNOMEShellSearchProvider::LaunchSearch(
    GVariant* aParameters, GDBusMethodInvocation* aInvocation) {
  if (mSearchResult) {
    DBusLaunchSearch(mSearchResult, aParameters, aInvocation);
  }
}

static void HandleMethodCall(GDBusConnection* aConnection, const gchar* aSender,
                             const gchar* aObjectPath,
                             const gchar* aInterfaceName,
                             const gchar* aMethodName, GVariant* aParameters,
                             GDBusMethodInvocation* aInvocation,
                             gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());

  if (strcmp("org.gnome.Shell.SearchProvider2", aInterfaceName) == 0) {
    if (strcmp("GetInitialResultSet", aMethodName) == 0) {
      static_cast<nsGNOMEShellSearchProvider*>(aUserData)
          ->HandleSearchResultSet(aParameters, aInvocation,
                                  /* aInitialSearch */ true);
    } else if (strcmp("GetSubsearchResultSet", aMethodName) == 0) {
      static_cast<nsGNOMEShellSearchProvider*>(aUserData)
          ->HandleSearchResultSet(aParameters, aInvocation,
                                  /* aInitialSearch */ false);
    } else if (strcmp("GetResultMetas", aMethodName) == 0) {
      static_cast<nsGNOMEShellSearchProvider*>(aUserData)->HandleResultMetas(
          aParameters, aInvocation);
    } else if (strcmp("ActivateResult", aMethodName) == 0) {
      static_cast<nsGNOMEShellSearchProvider*>(aUserData)->ActivateResult(
          aParameters, aInvocation);
    } else if (strcmp("LaunchSearch", aMethodName) == 0) {
      static_cast<nsGNOMEShellSearchProvider*>(aUserData)->LaunchSearch(
          aParameters, aInvocation);
    } else {
      g_warning(
          "nsGNOMEShellSearchProvider: HandleMethodCall() wrong method %s",
          aMethodName);
    }
  }
}

static GVariant* HandleGetProperty(GDBusConnection* aConnection,
                                   const gchar* aSender,
                                   const gchar* aObjectPath,
                                   const gchar* aInterfaceName,
                                   const gchar* aPropertyName, GError** aError,
                                   gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());
  g_set_error(aError, G_IO_ERROR, G_IO_ERROR_FAILED,
              "%s:%s setting is not supported", aInterfaceName, aPropertyName);
  return nullptr;
}

static gboolean HandleSetProperty(GDBusConnection* aConnection,
                                  const gchar* aSender,
                                  const gchar* aObjectPath,
                                  const gchar* aInterfaceName,
                                  const gchar* aPropertyName, GVariant* aValue,
                                  GError** aError, gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());
  g_set_error(aError, G_IO_ERROR, G_IO_ERROR_FAILED,
              "%s:%s setting is not supported", aInterfaceName, aPropertyName);
  return false;
}

static const GDBusInterfaceVTable gInterfaceVTable = {
    HandleMethodCall, HandleGetProperty, HandleSetProperty};

void nsGNOMEShellSearchProvider::OnBusAcquired(GDBusConnection* aConnection) {
  GUniquePtr<GError> error;
  mIntrospectionData = dont_AddRef(g_dbus_node_info_new_for_xml(
      introspect_template, getter_Transfers(error)));
  if (!mIntrospectionData) {
    g_warning(
        "nsGNOMEShellSearchProvider: g_dbus_node_info_new_for_xml() failed! %s",
        error->message);
    return;
  }

  mRegistrationId = g_dbus_connection_register_object(
      aConnection, GetDBusObjectPath(), mIntrospectionData->interfaces[0],
      &gInterfaceVTable, this,  /* user_data */
      nullptr,                  /* user_data_free_func */
      getter_Transfers(error)); /* GError** */

  if (mRegistrationId == 0) {
    g_warning(
        "nsGNOMEShellSearchProvider: g_dbus_connection_register_object() "
        "failed! %s",
        error->message);
    return;
  }
}

void nsGNOMEShellSearchProvider::OnNameAcquired(GDBusConnection* aConnection) {
  mConnection = aConnection;
}

void nsGNOMEShellSearchProvider::OnNameLost(GDBusConnection* aConnection) {
  mConnection = nullptr;
  if (!mRegistrationId) {
    return;
  }
  if (g_dbus_connection_unregister_object(aConnection, mRegistrationId)) {
    mRegistrationId = 0;
  }
}

nsresult nsGNOMEShellSearchProvider::Startup() {
  if (mDBusID) {
    // We're already connected so we don't need to reconnect
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  mDBusID = g_bus_own_name(
      G_BUS_TYPE_SESSION, GetDBusBusName(), G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
      [](GDBusConnection* aConnection, const gchar*,
         gpointer aUserData) -> void {
        static_cast<nsGNOMEShellSearchProvider*>(aUserData)->OnBusAcquired(
            aConnection);
      },
      [](GDBusConnection* aConnection, const gchar*,
         gpointer aUserData) -> void {
        static_cast<nsGNOMEShellSearchProvider*>(aUserData)->OnNameAcquired(
            aConnection);
      },
      [](GDBusConnection* aConnection, const gchar*,
         gpointer aUserData) -> void {
        static_cast<nsGNOMEShellSearchProvider*>(aUserData)->OnNameLost(
            aConnection);
      },
      this, nullptr);

  if (!mDBusID) {
    g_warning("nsGNOMEShellSearchProvider: g_bus_own_name() failed!");
    return NS_ERROR_FAILURE;
  }

  mSearchResultTimeStamp = 0;
  return NS_OK;
}

void nsGNOMEShellSearchProvider::Shutdown() {
  OnNameLost(mConnection);
  if (mDBusID) {
    g_bus_unown_name(mDBusID);
    mDBusID = 0;
  }
  mIntrospectionData = nullptr;
}

bool nsGNOMEShellSearchProvider::SetSearchResult(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult) {
  MOZ_ASSERT(!mSearchResult);

  if (mSearchResultTimeStamp != aSearchResult->GetTimeStamp()) {
    NS_WARNING("Time stamp mismatch.");
    return false;
  }
  mSearchResult = aSearchResult;
  return true;
}

static void DispatchSearchResults(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
    nsCOMPtr<nsINavHistoryContainerResultNode> aHistResultContainer) {
  aSearchResult->ReceiveSearchResultContainer(aHistResultContainer);
}

nsresult nsGNOMEShellHistoryService::QueryHistory(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult) {
  if (!mHistoryService) {
    mHistoryService = do_GetService(NS_NAVHISTORYSERVICE_CONTRACTID);
    if (!mHistoryService) {
      return NS_ERROR_FAILURE;
    }
  }

  nsresult rv;
  nsCOMPtr<nsINavHistoryQuery> histQuery;
  rv = mHistoryService->GetNewQuery(getter_AddRefs(histQuery));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = histQuery->SetSearchTerms(
      NS_ConvertUTF8toUTF16(aSearchResult->GetSearchTerm()));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsINavHistoryQueryOptions> histQueryOpts;
  rv = mHistoryService->GetNewQueryOptions(getter_AddRefs(histQueryOpts));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = histQueryOpts->SetSortingMode(
      nsINavHistoryQueryOptions::SORT_BY_FRECENCY_DESCENDING);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = histQueryOpts->SetMaxResults(MAX_SEARCH_RESULTS_NUM);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsINavHistoryResult> histResult;
  rv = mHistoryService->ExecuteQuery(histQuery, histQueryOpts,
                                     getter_AddRefs(histResult));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsINavHistoryContainerResultNode> resultContainer;

  rv = histResult->GetRoot(getter_AddRefs(resultContainer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = resultContainer->SetContainerOpen(true);
  NS_ENSURE_SUCCESS(rv, rv);

  // Simulate async searching by delayed reply. This search API will
  // likely become async in the future and we want to be sure to not rely on
  // its current synchronous behavior.
  MOZ_ASSERT(MessageLoop::current());
  MessageLoop::current()->PostTask(
      NewRunnableFunction("Gnome shell search results", &DispatchSearchResults,
                          aSearchResult, resultContainer));

  return NS_OK;
}

static void DBusGetIDKeyForURI(int aIndex, bool aIsOpen, nsAutoCString& aUri,
                               nsAutoCString& aIDKey) {
  // Compose ID as NN:S:URL where NN is index to our current history
  // result container and S is the state, which can be 'o'pen or 'h'istory
  aIDKey =
      nsPrintfCString("%.2d:%c:%s", aIndex, aIsOpen ? 'o' : 'h', aUri.get());
}

// Send (as) rearch result reply
void nsGNOMEShellHistorySearchResult::HandleSearchResultReply() {
  MOZ_ASSERT(mReply);
  MOZ_ASSERT(mHistResultContainer);

  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("as"));

  uint32_t childCount = 0;
  nsresult rv = mHistResultContainer->GetChildCount(&childCount);
  if (NS_SUCCEEDED(rv) && childCount > 0) {
    // Obtain the favicon service and get the favicon for the specified page
    auto* favIconSvc = nsFaviconService::GetFaviconService();
    nsCOMPtr<nsIIOService> ios(do_GetService(NS_IOSERVICE_CONTRACTID));

    if (childCount > MAX_SEARCH_RESULTS_NUM) {
      childCount = MAX_SEARCH_RESULTS_NUM;
    }

    for (uint32_t i = 0; i < childCount; i++) {
      nsCOMPtr<nsINavHistoryResultNode> child;
      rv = mHistResultContainer->GetChild(i, getter_AddRefs(child));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }
      if (!IsHistoryResultNodeURI(child)) {
        continue;
      }

      nsAutoCString uri;
      child->GetUri(uri);

      RefPtr<nsGNOMEShellHistorySearchResult> self = this;
      nsCOMPtr<nsIURI> iconIri;
      ios->NewURI(uri, nullptr, nullptr, getter_AddRefs(iconIri));
      favIconSvc->AsyncGetFaviconForPage(iconIri)->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, iconIndex = i, timeStamp = mTimeStamp](
              const places::FaviconPromise::ResolveOrRejectValue& aResult) {
            UpdateHistoryIcon(aResult, self, iconIndex, timeStamp);
          });

      bool isOpen = false;
      for (const auto& openuri : mOpenTabs) {
        if (openuri.Equals(uri)) {
          isOpen = true;
          break;
        }
      }
      nsAutoCString idKey;
      DBusGetIDKeyForURI(i, isOpen, uri, idKey);

      g_variant_builder_add(&b, "s", idKey.get());
    }
  }

  nsPrintfCString searchString("%s:%s", KEYWORD_SEARCH_STRING,
                               mSearchTerm.get());
  g_variant_builder_add(&b, "s", searchString.get());

  GVariant* v = g_variant_builder_end(&b);
  g_dbus_method_invocation_return_value(mReply, g_variant_new_tuple(&v, 1));
  mReply = nullptr;
}

void nsGNOMEShellHistorySearchResult::ReceiveSearchResultContainer(
    nsCOMPtr<nsINavHistoryContainerResultNode> aHistResultContainer) {
  // Propagate search results to nsGNOMEShellSearchProvider.
  // SetSearchResult() checks this is up-to-date search (our time stamp matches
  // latest requested search timestamp).
  if (!mSearchProvider->SetSearchResult(this)) {
    return;
  }

  mHistResultContainer = aHistResultContainer;

  // Getting the currently open tabs to mark them accordingly
  nsresult rv;
  nsCOMPtr<nsIOpenTabsProvider> provider =
      do_ImportESModule("resource:///modules/OpenTabsProvider.sys.mjs", &rv);
  if (NS_FAILED(rv)) {
    // Don't fail, just log an error message
    NS_WARNING("Failed to determine currently open tabs. Using history only.");
  }

  nsTArray<nsCString> openTabs;
  if (provider) {
    rv = provider->GetOpenTabs(openTabs);
    if (NS_FAILED(rv)) {
      // Don't fail, just log an error message
      NS_WARNING(
          "Failed to determine currently open tabs. Using history only.");
    }
  }
  // In case of error, we just clear out mOpenTabs with an empty new array
  mOpenTabs = std::move(openTabs);

  HandleSearchResultReply();
}

void nsGNOMEShellHistorySearchResult::SetHistoryIcon(int aTimeStamp,
                                                     UniquePtr<uint8_t[]> aData,
                                                     int aWidth, int aHeight,
                                                     int aIconIndex) {
  MOZ_ASSERT(mTimeStamp == aTimeStamp);
  MOZ_RELEASE_ASSERT(aIconIndex < MAX_SEARCH_RESULTS_NUM);
  mHistoryIcons[aIconIndex].Set(mTimeStamp, std::move(aData), aWidth, aHeight);
}

GnomeHistoryIcon* nsGNOMEShellHistorySearchResult::GetHistoryIcon(
    int aIconIndex) {
  MOZ_RELEASE_ASSERT(aIconIndex < MAX_SEARCH_RESULTS_NUM);
  if (mHistoryIcons[aIconIndex].GetTimeStamp() == mTimeStamp &&
      mHistoryIcons[aIconIndex].IsLoaded()) {
    return mHistoryIcons + aIconIndex;
  }
  return nullptr;
}

nsGNOMEShellHistoryService* GetGNOMEShellHistoryService() {
  static nsGNOMEShellHistoryService gGNOMEShellHistoryService;
  return &gGNOMEShellHistoryService;
}
