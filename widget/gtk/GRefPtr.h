/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GRefPtr_h_
#define GRefPtr_h_

// Allows to use RefPtr<T> with various kinds of GObjects

#include <gdk/gdk.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "mozilla/RefPtr.h"

typedef struct _DbusmenuMenuitem DbusmenuMenuitem;
typedef struct _DbusmenuServer DbusmenuServer;

namespace mozilla {

template <typename T>
struct GObjectRefPtrTraits {
  static void AddRef(T* aObject) { g_object_ref(aObject); }
  static void Release(T* aObject) { g_object_unref(aObject); }
};

#define GOBJECT_TRAITS(type_) \
  template <>                 \
  struct RefPtrTraits<type_> : public GObjectRefPtrTraits<type_> {};

GOBJECT_TRAITS(DbusmenuMenuitem)
GOBJECT_TRAITS(DbusmenuServer)
GOBJECT_TRAITS(GtkWidget)
GOBJECT_TRAITS(GFile)
GOBJECT_TRAITS(GFileMonitor)
GOBJECT_TRAITS(GMenu)
GOBJECT_TRAITS(GMenuItem)
GOBJECT_TRAITS(GSimpleAction)
GOBJECT_TRAITS(GSimpleActionGroup)
GOBJECT_TRAITS(GDBusProxy)
GOBJECT_TRAITS(GAppInfo)
GOBJECT_TRAITS(GDesktopAppInfo)
GOBJECT_TRAITS(GAppLaunchContext)
GOBJECT_TRAITS(GdkDragContext)
GOBJECT_TRAITS(GDBusMessage)
GOBJECT_TRAITS(GdkPixbuf)
GOBJECT_TRAITS(GCancellable)
GOBJECT_TRAITS(GtkIMContext)
GOBJECT_TRAITS(GUnixFDList)
GOBJECT_TRAITS(GtkCssProvider)
GOBJECT_TRAITS(GDBusMethodInvocation)
GOBJECT_TRAITS(GdkWindow)
GOBJECT_TRAITS(GtkIconInfo)
GOBJECT_TRAITS(GIcon)
GOBJECT_TRAITS(::GSettings)
GOBJECT_TRAITS(GDBusConnection)

#undef GOBJECT_TRAITS

template <>
struct RefPtrTraits<GVariant> {
  static void AddRef(GVariant* aVariant) { g_variant_ref(aVariant); }
  static void Release(GVariant* aVariant) { g_variant_unref(aVariant); }
};

template <>
struct RefPtrTraits<GHashTable> {
  static void AddRef(GHashTable* aObject) { g_hash_table_ref(aObject); }
  static void Release(GHashTable* aObject) { g_hash_table_unref(aObject); }
};

template <>
struct RefPtrTraits<GDBusNodeInfo> {
  static void AddRef(GDBusNodeInfo* aObject) { g_dbus_node_info_ref(aObject); }
  static void Release(GDBusNodeInfo* aObject) {
    g_dbus_node_info_unref(aObject);
  }
};

template <>
struct RefPtrTraits<GKeyFile> {
  static void AddRef(GKeyFile* aObject) { g_key_file_ref(aObject); }
  static void Release(GKeyFile* aObject) { g_key_file_unref(aObject); }
};

}  // namespace mozilla

#endif
