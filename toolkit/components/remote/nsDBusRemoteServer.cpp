/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDBusRemoteServer.h"

#include "nsCOMPtr.h"
#include "nsAppRunner.h"
#include "mozilla/XREAppData.h"
#include "mozilla/Base64.h"
#include "mozilla/GUniquePtr.h"
#include "MainThreadUtils.h"
#include "nsPrintfCString.h"
#include "nsGTKToolkit.h"

#include <dbus/dbus.h>

using namespace mozilla;

// Mozilla has old GIO version in build roots
#define G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE GBusNameOwnerFlags(1 << 2)

static const char* introspect_template =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection "
    "1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.mozilla.%s\">\n"
    "   <method name=\"OpenURL\">\n"
    "     <arg name=\"url\" direction=\"in\" type=\"ay\"/>\n"
    "   </method>\n"
    " </interface>\n"
    "</node>\n";

bool nsDBusRemoteServer::HandleOpenURL(const gchar* aInterfaceName,
                                       const gchar* aMethodName,
                                       Span<const gchar> aParam) {
  nsPrintfCString ourInterfaceName("org.mozilla.%s", mAppName.get());

  if ((strcmp("OpenURL", aMethodName) != 0) ||
      (strcmp(ourInterfaceName.get(), aInterfaceName) != 0)) {
    g_warning("nsDBusRemoteServer: HandleOpenURL() called with wrong params!");
    return false;
  }

  HandleCommandLine(aParam, gtk_get_current_event_time());
  return true;
}

static void HandleMethodCall(GDBusConnection* /*aConnection*/,
                             const gchar* /*aSender*/, const gchar* aObjectPath,
                             const gchar* aInterfaceName,
                             const gchar* aMethodName, GVariant* aParameters,
                             GDBusMethodInvocation* aInvocation,
                             gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());

  if (!g_variant_is_of_type(aParameters, G_VARIANT_TYPE_TUPLE) ||
      g_variant_n_children(aParameters) != 1) {
    g_warning(
        "nsDBusRemoteServer: HandleMethodCall: aParameters is not a tuple!");
    g_dbus_method_invocation_return_error(
        aInvocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
        "Method %s.%s.%s has wrong params!", aObjectPath, aInterfaceName,
        aMethodName);
    return;
  }

  gsize len = 0;
  const char* commandLine = nullptr;

  RefPtr<GVariant> variant =
      dont_AddRef(g_variant_get_child_value(aParameters, 0));
  if (variant) {
    commandLine =
        (const char*)g_variant_get_fixed_array(variant, &len, sizeof(char));
  }
  if (!commandLine || !len) {
    g_warning(
        "nsDBusRemoteServer: HandleMethodCall: failed to get url string!");
    g_dbus_method_invocation_return_error(
        aInvocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
        "Method %s.%s.%s has wrong params!", aObjectPath, aInterfaceName,
        aMethodName);
    return;
  }

  int ret = static_cast<nsDBusRemoteServer*>(aUserData)->HandleOpenURL(
      aInterfaceName, aMethodName, mozilla::Span(commandLine, len));
  if (!ret) {
    g_dbus_method_invocation_return_error(
        aInvocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
        "Method %s.%s.%s doesn't match OpenURL()", aObjectPath, aInterfaceName,
        aMethodName);
    return;
  }
  g_dbus_method_invocation_return_value(aInvocation, nullptr);
}

static GVariant* HandleGetProperty(GDBusConnection* /*aConnection*/,
                                   const gchar* /*aSender*/,
                                   const gchar* /*aObjectPath*/,
                                   const gchar* aInterfaceName,
                                   const gchar* aPropertyName, GError** aError,
                                   gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());
  g_set_error(aError, G_IO_ERROR, G_IO_ERROR_FAILED,
              "%s:%s setting is not supported", aInterfaceName, aPropertyName);
  return nullptr;
}

static gboolean HandleSetProperty(GDBusConnection* /*aConnection*/,
                                  const gchar* /*aSender*/,
                                  const gchar* /*aObjectPath*/,
                                  const gchar* aInterfaceName,
                                  const gchar* aPropertyName,
                                  GVariant* /*aValue*/, GError** aError,
                                  gpointer aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(NS_IsMainThread());
  g_set_error(aError, G_IO_ERROR, G_IO_ERROR_FAILED,
              "%s:%s setting is not supported", aInterfaceName, aPropertyName);
  return false;
}

static const GDBusInterfaceVTable gInterfaceVTable = {
    HandleMethodCall, HandleGetProperty, HandleSetProperty};

void nsDBusRemoteServer::OnBusAcquired(GDBusConnection* aConnection) {
  mPathName = nsPrintfCString("/org/mozilla/%s/Remote", mAppName.get());
  if (!g_variant_is_object_path(mPathName.get())) {
    g_warning("nsDBusRemoteServer: object path is not valid");
    return;
  }

  GUniquePtr<GError> error;
  mIntrospectionData = dont_AddRef(g_dbus_node_info_new_for_xml(
      nsPrintfCString(introspect_template, mAppName.get()).get(),
      getter_Transfers(error)));
  if (!mIntrospectionData) {
    g_warning("nsDBusRemoteServer: g_dbus_node_info_new_for_xml() failed! %s",
              error->message);
    return;
  }

  mRegistrationId = g_dbus_connection_register_object(
      aConnection, mPathName.get(), mIntrospectionData->interfaces[0],
      &gInterfaceVTable, this,  /* user_data */
      nullptr,                  /* user_data_free_func */
      getter_Transfers(error)); /* GError** */

  if (mRegistrationId == 0) {
    g_warning(
        "nsDBusRemoteServer: g_dbus_connection_register_object() failed! %s",
        error->message);
    return;
  }

  // The object can receive calls from now on, whether or not we get the bus
  // name, so keep the connection until Shutdown() unregisters it.
  mConnection = aConnection;
}

nsresult nsDBusRemoteServer::Startup(const char* /*aAppName*/,
                                     const char* aProfileName) {
  MOZ_DIAGNOSTIC_ASSERT(!mDBusID);

  // Don't even try to start without any profile name
  if (!aProfileName || aProfileName[0] == '\0') return NS_ERROR_INVALID_ARG;

  // aAppName is remoting name which can be something like org.mozilla.appname
  // or so.
  // For DBus service we rather use general application DBus identifier
  // which is shared by all DBus services.
  gAppData->GetDBusAppName(mAppName);

  nsAutoCString profileName;
  MOZ_TRY(
      mozilla::Base64Encode(aProfileName, strlen(aProfileName), profileName));

  mozilla::XREAppData::SanitizeNameForDBus(profileName);

  nsPrintfCString busName("org.mozilla.%s.%s", mAppName.get(),
                          profileName.get());
  if (busName.Length() > DBUS_MAXIMUM_NAME_LENGTH) {
    busName.Truncate(DBUS_MAXIMUM_NAME_LENGTH);
  }

  // We don't have a valid busName yet - try to create a default one.
  if (!g_dbus_is_name(busName.get())) {
    busName = nsPrintfCString("org.mozilla.%s.%s", mAppName.get(), "default");
    if (!g_dbus_is_name(busName.get())) {
      // We failed completely to get a valid bus name - just quit.
      g_warning("nsDBusRemoteServer: couldn't get a valid bus name!");
      return NS_ERROR_FAILURE;
    }
  }

  mDBusID = g_bus_own_name(
      G_BUS_TYPE_SESSION, busName.get(), G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
      [](GDBusConnection* aConnection, const gchar*,
         gpointer aUserData) -> void {
        static_cast<nsDBusRemoteServer*>(aUserData)->OnBusAcquired(aConnection);
      },
      nullptr, /* name_acquired_handler */
      nullptr, /* name_lost_handler */
      this, nullptr);
  if (!mDBusID) {
    g_warning("nsDBusRemoteServer: g_bus_own_name() failed!");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void nsDBusRemoteServer::Shutdown() {
  if (mRegistrationId) {
    if (g_dbus_connection_unregister_object(mConnection, mRegistrationId)) {
      mRegistrationId = 0;
    } else {
      // Note: Most code examples in the internet probably don't even check
      // the result here, but according to the spec it _can_ return false.
      g_warning("nsDBusRemoteServer: Unable to unregister root object!");
    }
  }
  mConnection = nullptr;
  if (mDBusID) {
    g_bus_unown_name(mDBusID);
  }
  mIntrospectionData = nullptr;
}
