/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGIOService.h"
#include "nsString.h"
#include "nsIURI.h"
#include "nsIFile.h"
#include "nsTArray.h"
#include "nsStringEnumerator.h"
#include "nsIMIMEInfo.h"
#include "nsComponentManagerUtils.h"
#include "nsArray.h"
#include "nsPrintfCString.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/net/DNS.h"
#include "prenv.h"

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#ifdef MOZ_ENABLE_DBUS
#  include <fcntl.h>
#  include <dlfcn.h>
#  include "mozilla/widget/AsyncDBus.h"
#  include "mozilla/WidgetUtilsGtk.h"
#  include "nsAppShell.h"
#endif

using namespace mozilla;

#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"
LazyLogModule gGIOServiceLog("GIOService");
#  define LOG(...) \
    MOZ_LOG(gGIOServiceLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOG(...)
#endif /* MOZ_LOGGING */

#define MOZ_TYPE_APP_LAUNCH_CONTEXT (moz_app_launch_context_get_type())
#define MOZ_APP_LAUNCH_CONTEXT(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), MOZ_TYPE_APP_LAUNCH_CONTEXT, \
                              MozAppLaunchContext))
#define MOZ_APP_LAUNCH_CONTEXT_CLASS(klass)                      \
  (G_TYPE_CHECK_CLASS_CAST((klass), MOZ_TYPE_APP_LAUNCH_CONTEXT, \
                           MozAppLaunchContextClass))
#define MOZ_IS_APP_LAUNCH_CONTEXT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), MOZ_TYPE_APP_LAUNCH_CONTEXT))
#define MOZ_IS_APP_LAUNCH_CONTEXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), MOZ_TYPE_APP_LAUNCH_CONTEXT))
#define MOZ_APP_LAUNCH_CONTEXT_GET_CLASS(obj)                    \
  (G_TYPE_INSTANCE_GET_CLASS((obj), MOZ_TYPE_APP_LAUNCH_CONTEXT, \
                             MozAppLaunchContextClass))

typedef struct {
  GAppLaunchContext parent_instance;
  char* activation_token;
} MozAppLaunchContext;

typedef struct {
  GAppLaunchContextClass parent_class;
} MozAppLaunchContextClass;

// GLib's G_DEFINE_TYPE triggers warnings about volatile types. These warnings
// were fixed in GLib 2.67.1: https://gitlab.gnome.org/GNOME/glib/-/issues/600
// We can remove the diagnostic pragmas below after our minimum supported GLib
// version is >= 2.67.1.
#if !GLIB_CHECK_VERSION(2, 67, 1)
#  if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-volatile"
#  elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wvolatile"
#  endif
#endif

G_DEFINE_TYPE(MozAppLaunchContext, moz_app_launch_context,
              G_TYPE_APP_LAUNCH_CONTEXT)

#if !GLIB_CHECK_VERSION(2, 67, 1)
#  if defined(__clang__)
#    pragma clang diagnostic pop
#  elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#  endif
#endif

static char* moz_app_launch_context_get_startup_notify_id(
    GAppLaunchContext* context, GAppInfo* /*info*/, GList* /*files*/) {
  MozAppLaunchContext* self = MOZ_APP_LAUNCH_CONTEXT(context);

  // https://docs.gtk.org/gio/method.AppLaunchContext.get_startup_notify_id.html
  // We have already obtained an activation token, so simply return it now.
  return g_strdup(self->activation_token);
}

static void moz_app_launch_context_finalize(GObject* object) {
  MozAppLaunchContext* self = MOZ_APP_LAUNCH_CONTEXT(object);
  g_clear_pointer(&self->activation_token, g_free);
  G_OBJECT_CLASS(moz_app_launch_context_parent_class)->finalize(object);
}

static void moz_app_launch_context_class_init(MozAppLaunchContextClass* klass) {
  GAppLaunchContextClass* launch_class = G_APP_LAUNCH_CONTEXT_CLASS(klass);
  GObjectClass* object_class = G_OBJECT_CLASS(klass);

  launch_class->get_startup_notify_id =
      moz_app_launch_context_get_startup_notify_id;
  object_class->finalize = moz_app_launch_context_finalize;
}

static void moz_app_launch_context_init(MozAppLaunchContext* self) {
  GAppLaunchContext* context = G_APP_LAUNCH_CONTEXT(self);

  // GLib does not support xdg-activation before version 2.76,
  // so set the environment variable manually.
  // This only works when launching the proces directly, and does
  // not work when going through the xdg-desktop-portal.
  // For activation to work when using the portal, you need to use
  // GLib 2.76 or newer.
  if (self->activation_token && glib_check_version(2, 76, 0)) {
    g_app_launch_context_setenv(context, "XDG_ACTIVATION_TOKEN",
                                self->activation_token);
  }

  // Unset this before launching third-party MIME handlers. Otherwise, if
  // Thunderbird sets this in its startup script (as it does in Debian and
  // Fedora), and Firefox does not set this in its startup script (it doesn't in
  // Debian), then Firefox will think it is part of Thunderbird and try to make
  // Thunderbird the default browser. See bug 1494436.
  g_app_launch_context_unsetenv(context, "MOZ_APP_LAUNCHER");
}

MozAppLaunchContext* moz_app_launch_context_new(const char* activation_token) {
  MozAppLaunchContext* self = MOZ_APP_LAUNCH_CONTEXT(
      g_object_new(MOZ_TYPE_APP_LAUNCH_CONTEXT, nullptr));
  self->activation_token = g_strdup(activation_token);
  return self;
}

class nsFlatpakHandlerApp : public nsIHandlerApp {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIHANDLERAPP
  nsFlatpakHandlerApp() = default;

 private:
  virtual ~nsFlatpakHandlerApp() = default;
};

NS_IMPL_ISUPPORTS(nsFlatpakHandlerApp, nsIHandlerApp)

NS_IMETHODIMP
nsFlatpakHandlerApp::GetName(nsAString& aName) {
  aName.AssignLiteral("System Handler");
  return NS_OK;
}

NS_IMETHODIMP
nsFlatpakHandlerApp::SetName(const nsAString& /*aName*/) {
  // We don't implement SetName because flatpak system handler name is fixed
  return NS_OK;
}

NS_IMETHODIMP
nsFlatpakHandlerApp::GetDetailedDescription(
    nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsFlatpakHandlerApp::SetDetailedDescription(
    const nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsFlatpakHandlerApp::Equals(nsIHandlerApp* /*aHandlerApp*/, bool* /*_retval*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsFlatpakHandlerApp::LaunchWithURI(
    nsIURI* aUri, mozilla::dom::BrowsingContext* /*aBrowsingContext*/) {
  nsCString spec;
  aUri->GetSpec(spec);
  GUniquePtr<GError> error;

  // The TMPDIR where files are downloaded when user choose to open them
  // needs to be accessible from sandbox and host. The default settings
  // TMPDIR=/tmp is accessible only to the sandbox. That can be the reason
  // why the gtk_show_uri fails there.
  // The workaround is to set TMPDIR environment variable in sandbox to
  // $XDG_CACHE_HOME/tmp before executing Firefox.
  gtk_show_uri(nullptr, spec.get(), GDK_CURRENT_TIME, getter_Transfers(error));
  if (error) {
    NS_WARNING(
        nsPrintfCString("Cannot launch flatpak handler: %s", error->message)
            .get());
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

/**
 * Get command without any additional arguments
 * @param aCommandWithArguments full commandline input string
 * @param aCommand string for storing command without arguments
 * @return NS_ERROR_FAILURE when unable to parse commandline
 */
static nsresult GetCommandFromCommandline(
    nsACString const& aCommandWithArguments, nsACString& aCommand) {
  GUniquePtr<GError> error;
  gchar** argv = nullptr;
  if (!g_shell_parse_argv(PromiseFlatCString(aCommandWithArguments).get(),
                          nullptr, &argv, getter_Transfers(error)) ||
      !argv[0]) {
    g_warning("Cannot parse command with arguments: %s", error->message);
    g_strfreev(argv);
    return NS_ERROR_FAILURE;
  }
  aCommand.Assign(argv[0]);
  g_strfreev(argv);
  return NS_OK;
}

class nsGIOHandlerApp final : public nsIGIOHandlerApp {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIHANDLERAPP
  NS_DECL_NSIGIOHANDLERAPP

  explicit nsGIOHandlerApp(already_AddRefed<GAppInfo> aApp) : mApp(aApp) {}

 private:
  ~nsGIOHandlerApp() = default;
  RefPtr<GAppInfo> mApp;
};

NS_IMPL_ISUPPORTS(nsGIOHandlerApp, nsIGIOHandlerApp, nsIHandlerApp)

NS_IMETHODIMP
nsGIOHandlerApp::GetId(nsACString& aId) {
  aId.Assign(g_app_info_get_id(mApp));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOHandlerApp::GetName(nsAString& aName) {
  aName.Assign(NS_ConvertUTF8toUTF16(g_app_info_get_name(mApp)));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOHandlerApp::SetName(const nsAString& /*aName*/) {
  // We don't implement SetName because we're using mGIOMimeApp instance for
  // obtaining application name
  return NS_OK;
}

NS_IMETHODIMP
nsGIOHandlerApp::GetDetailedDescription(nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsGIOHandlerApp::SetDetailedDescription(
    const nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsGIOHandlerApp::Equals(nsIHandlerApp* aHandlerApp, bool* _retval) {
  // Compare with nsIGIOMimeApp instance by command with stripped arguments
  nsCOMPtr<nsIGIOHandlerApp> gioMimeApp = do_QueryInterface(aHandlerApp);
  *_retval = false;
  if (!gioMimeApp) {
    return NS_OK;
  }

  nsAutoCString thisId;
  nsresult rv = GetId(thisId);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString theirId;
  gioMimeApp->GetId(theirId);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = thisId.Equals(theirId);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOHandlerApp::LaunchFile(const nsACString& aFileName) {
  GFile* gfile = g_file_new_for_path(PromiseFlatCString(aFileName).get());
  GList* fileList = nullptr;
  fileList = g_list_append(fileList, gfile);
  bool retval = g_app_info_launch(mApp, fileList, nullptr, nullptr);
  g_list_foreach(fileList, (GFunc)g_object_unref, nullptr);
  g_list_free(fileList);
  return retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsGIOHandlerApp::LaunchWithURI(
    nsIURI* /*aUri*/, mozilla::dom::BrowsingContext* /*aBrowsingContext*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsGIOHandlerApp::GetMozIconURL(nsACString& _retval) {
  GIcon* icon = g_app_info_get_icon(mApp);
  _retval.Assign(g_icon_to_string(icon));
  return NS_OK;
}

class nsGIOMimeApp final : public nsIGIOMimeApp {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIHANDLERAPP
  NS_DECL_NSIGIOMIMEAPP

  explicit nsGIOMimeApp(already_AddRefed<GAppInfo> aApp) : mApp(aApp) {}

 private:
  ~nsGIOMimeApp() = default;

  RefPtr<GAppInfo> mApp;
};

NS_IMPL_ISUPPORTS(nsGIOMimeApp, nsIGIOMimeApp, nsIHandlerApp)

NS_IMETHODIMP
nsGIOMimeApp::GetId(nsACString& aId) {
  aId.Assign(g_app_info_get_id(mApp));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetName(nsAString& aName) {
  aName.Assign(NS_ConvertUTF8toUTF16(g_app_info_get_name(mApp)));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::SetName(const nsAString& /*aName*/) {
  // We don't implement SetName because we're using mGIOMimeApp instance for
  // obtaining application name
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetCommand(nsACString& aCommand) {
  const char* cmd = g_app_info_get_commandline(mApp);
  if (!cmd) return NS_ERROR_FAILURE;
  aCommand.Assign(cmd);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetExpectsURIs(int32_t* aExpects) {
  *aExpects = g_app_info_supports_uris(mApp);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetDetailedDescription(nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsGIOMimeApp::SetDetailedDescription(
    const nsAString& /*aDetailedDescription*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsGIOMimeApp::Equals(nsIHandlerApp* aHandlerApp, bool* _retval) {
  if (!aHandlerApp) return NS_ERROR_FAILURE;

  // Compare with nsILocalHandlerApp instance by name
  nsCOMPtr<nsILocalHandlerApp> localHandlerApp = do_QueryInterface(aHandlerApp);
  if (localHandlerApp) {
    nsAutoString theirName;
    nsAutoString thisName;
    GetName(thisName);
    localHandlerApp->GetName(theirName);
    *_retval = thisName.Equals(theirName);
    return NS_OK;
  }

  // Compare with nsIGIOMimeApp instance by command with stripped arguments
  nsCOMPtr<nsIGIOMimeApp> gioMimeApp = do_QueryInterface(aHandlerApp);
  if (gioMimeApp) {
    nsAutoCString thisCommandline, thisCommand;
    nsresult rv = GetCommand(thisCommandline);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetCommandFromCommandline(thisCommandline, thisCommand);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString theirCommandline, theirCommand;
    gioMimeApp->GetCommand(theirCommandline);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetCommandFromCommandline(theirCommandline, theirCommand);
    NS_ENSURE_SUCCESS(rv, rv);

    *_retval = thisCommand.Equals(theirCommand);
    return NS_OK;
  }

  // We can only compare with nsILocalHandlerApp and nsGIOMimeApp
  *_retval = false;
  return NS_OK;
}

static RefPtr<GAppLaunchContext> GetLaunchContext(
    const char* aXDGToken = nullptr) {
  RefPtr<GAppLaunchContext> context =
      dont_AddRef(G_APP_LAUNCH_CONTEXT(moz_app_launch_context_new(aXDGToken)));
  return context;
}

#ifdef __OpenBSD__
// wrappers required for OpenBSD sandboxing with unveil()
gboolean g_app_info_launch_uris_openbsd(GAppInfo* mApp, const char* uri,
                                        GAppLaunchContext* context,
                                        GError** error) {
  gchar* path = g_filename_from_uri(uri, NULL, NULL);
  auto releasePath = MakeScopeExit([&] { g_free(path); });
  const gchar* bin = g_app_info_get_executable(mApp);
  if (!bin) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "no executable found for %s, maybe not unveiled ?",
                g_app_info_get_name(mApp));
    return FALSE;
  }
  g_debug("spawning %s %s for %s", bin, path, uri);
  const gchar* const argv[] = {bin, path, NULL};

  GSpawnFlags flags =
      static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD);
  gboolean result =
      g_spawn_async(NULL, (char**)argv, NULL, flags, NULL, NULL, NULL, error);

  if (!result) {
    g_warning("Cannot launch application %s with arg %s: %s", bin, path,
              (*error)->message);
    return FALSE;
  }
  return TRUE;
}

gboolean g_app_info_launch_default_for_uri_openbsd(const char* uri,
                                                   GAppLaunchContext* context,
                                                   GError** error) {
  gboolean result_uncertain;
  gchar* path = g_filename_from_uri(uri, NULL, NULL);
  gchar* content_type = g_content_type_guess(path, NULL, 0, &result_uncertain);
  gchar* scheme = g_uri_parse_scheme(uri);
  auto release = MakeScopeExit([&] {
    g_free(path);
    g_free(content_type);
    g_free(scheme);
  });
  if (g_strcmp0(scheme, "http") == 0 || g_strcmp0(scheme, "https") == 0)
    return g_app_info_launch_default_for_uri(uri, context, error);

  if (content_type != NULL && !result_uncertain) {
    g_debug("content type for %s: %s", uri, content_type);
    GAppInfo* app_info = g_app_info_get_default_for_type(content_type, false);
    auto releaseAppInfo = MakeScopeExit([&] {
      if (app_info) g_object_unref(app_info);
    });
    if (!app_info) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                  "Could not find default handler for content type %s",
                  content_type);
      return FALSE;
    } else {
      return g_app_info_launch_uris_openbsd(app_info, uri, context, error);
    }
  } else {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Could not find content type for URI: %s", uri);
    return FALSE;
  }
}
#endif

static NS_IMETHODIMP LaunchWithURIImpl(RefPtr<GAppInfo> aInfo, nsIURI* aUri,
                                       const char* aXDGToken = nullptr) {
  GList uris = {nullptr};
  nsCString spec;
  aUri->GetSpec(spec);
  // nsPromiseFlatCString flatUri(aUri);
  uris.data = const_cast<char*>(spec.get());

  GUniquePtr<GError> error;
#ifdef __OpenBSD__
  gboolean result = g_app_info_launch_uris_openbsd(
      aInfo, spec.get(), GetLaunchContext(aXDGToken).get(),
      getter_Transfers(error));
#else
  gboolean result = g_app_info_launch_uris(
      aInfo, &uris, GetLaunchContext(aXDGToken).get(), getter_Transfers(error));
#endif
  if (!result) {
    g_warning("Cannot launch application: %s", error->message);
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::LaunchWithURI(
    nsIURI* aUri, mozilla::dom::BrowsingContext* /*aBrowsingContext*/) {
  auto promise = mozilla::widget::RequestWaylandFocusPromise();
  if (!promise) {
    return LaunchWithURIImpl(mApp, aUri);
  }
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      /* resolve */
      [app = RefPtr{mApp}, uri = RefPtr{aUri}](nsCString token) {
        LaunchWithURIImpl(app, uri, token.get());
      },
      /* reject */
      [app = RefPtr{mApp}, uri = RefPtr{aUri}](bool /*state*/) {
        LaunchWithURIImpl(app, uri);
      });
  return NS_OK;
}

class GIOUTF8StringEnumerator final : public nsStringEnumeratorBase {
  ~GIOUTF8StringEnumerator() = default;

 public:
  GIOUTF8StringEnumerator() : mIndex(0) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIUTF8STRINGENUMERATOR

  using nsStringEnumeratorBase::GetNext;

  nsTArray<nsCString> mStrings;
  uint32_t mIndex;
};

NS_IMPL_ISUPPORTS(GIOUTF8StringEnumerator, nsIUTF8StringEnumerator,
                  nsIStringEnumerator)

NS_IMETHODIMP
GIOUTF8StringEnumerator::HasMore(bool* aResult) {
  *aResult = mIndex < mStrings.Length();
  return NS_OK;
}

NS_IMETHODIMP
GIOUTF8StringEnumerator::GetNext(nsACString& aResult) {
  if (mIndex >= mStrings.Length()) return NS_ERROR_UNEXPECTED;

  aResult.Assign(mStrings[mIndex]);
  ++mIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetSupportedURISchemes(nsIUTF8StringEnumerator** aSchemes) {
  *aSchemes = nullptr;

  RefPtr<GIOUTF8StringEnumerator> array = new GIOUTF8StringEnumerator();

  GVfs* gvfs = g_vfs_get_default();

  if (!gvfs) {
    g_warning("Cannot get GVfs object.");
    return NS_ERROR_OUT_OF_MEMORY;
  }

  const gchar* const* uri_schemes = g_vfs_get_supported_uri_schemes(gvfs);

  while (*uri_schemes != nullptr) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    array->mStrings.AppendElement(*uri_schemes);
    uri_schemes++;
  }

  array.forget(aSchemes);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::SetAsDefaultForMimeType(nsACString const& aMimeType) {
  GUniquePtr<char> content_type(
      g_content_type_from_mime_type(PromiseFlatCString(aMimeType).get()));
  if (!content_type) return NS_ERROR_FAILURE;
  GUniquePtr<GError> error;
  g_app_info_set_as_default_for_type(mApp, content_type.get(),
                                     getter_Transfers(error));
  if (error) {
    g_warning("Cannot set application as default for MIME type (%s): %s",
              PromiseFlatCString(aMimeType).get(), error->message);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}
/**
 * Set default application for files with given extensions
 * @param fileExts string of space separated extensions
 * @return NS_OK when application was set as default for given extensions,
 * NS_ERROR_FAILURE otherwise
 */
NS_IMETHODIMP
nsGIOMimeApp::SetAsDefaultForFileExtensions(nsACString const& fileExts) {
  GUniquePtr<GError> error;
  GUniquePtr<char> extensions(g_strdup(PromiseFlatCString(fileExts).get()));
  char* ext_pos = extensions.get();
  char* space_pos;

  while ((space_pos = strchr(ext_pos, ' ')) || (*ext_pos != '\0')) {
    if (space_pos) {
      *space_pos = '\0';
    }
    g_app_info_set_as_default_for_extension(mApp, ext_pos,
                                            getter_Transfers(error));
    if (error) {
      g_warning("Cannot set application as default for extension (%s): %s",
                ext_pos, error->message);
      return NS_ERROR_FAILURE;
    }
    if (space_pos) {
      ext_pos = space_pos + 1;
    } else {
      *ext_pos = '\0';
    }
  }
  return NS_OK;
}

/**
 * Set default application for URI's of a particular scheme
 * @param aURIScheme string containing the URI scheme
 * @return NS_OK when application was set as default for URI scheme,
 * NS_ERROR_FAILURE otherwise
 */
NS_IMETHODIMP
nsGIOMimeApp::SetAsDefaultForURIScheme(nsACString const& aURIScheme) {
  GUniquePtr<GError> error;
  nsAutoCString contentType("x-scheme-handler/");
  contentType.Append(aURIScheme);

  g_app_info_set_as_default_for_type(mApp, contentType.get(),
                                     getter_Transfers(error));
  if (error) {
    g_warning("Cannot set application as default for URI scheme (%s): %s",
              PromiseFlatCString(aURIScheme).get(), error->message);
    return NS_ERROR_FAILURE;
  }

  // Its consistent with the way `gio mime x-scheme-handler/contentType
  // some.desktop` registers mime types to also add the registered application
  // to the list of supported applications for the handler.
  g_app_info_set_as_last_used_for_type(mApp, contentType.get(),
                                       getter_Transfers(error));
  if (error) {
    g_warning("Cannot register as compatible URI scheme handler for %s: %s",
              PromiseFlatCString(aURIScheme).get(), error->message);
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsGIOService, nsIGIOService)

NS_IMETHODIMP
nsGIOService::GetMimeTypeFromExtension(const nsACString& aExtension,
                                       nsACString& aMimeType) {
  nsAutoCString fileExtToUse("file.");
  fileExtToUse.Append(aExtension);

  gboolean result_uncertain;
  GUniquePtr<char> content_type(
      g_content_type_guess(fileExtToUse.get(), nullptr, 0, &result_uncertain));
  if (!content_type) {
    return NS_ERROR_FAILURE;
  }

  GUniquePtr<char> mime_type(g_content_type_get_mime_type(content_type.get()));
  if (!mime_type) {
    return NS_ERROR_FAILURE;
  }

  aMimeType.Assign(mime_type.get());
  return NS_OK;
}
// used in nsGNOMERegistry
// -----------------------------------------------------------------------------
#define OPENURI_BUS_NAME "org.freedesktop.portal.Desktop"
#define OPENURI_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define OPENURI_INTERFACE_NAME "org.freedesktop.portal.OpenURI"
#define SCHEME_SUPPORTED_METHOD "SchemeSupported"

NS_IMETHODIMP
nsGIOService::GetAppForURIScheme(const nsACString& aURIScheme,
                                 nsIHandlerApp** aApp) {
  *aApp = nullptr;

  LOG("nsGIOService::GetAppForURIScheme() %s",
      PromiseFlatCString(aURIScheme).get());

  // Portals works with DBus only.
#ifdef MOZ_ENABLE_DBUS
  // Application in flatpak sandbox does not have access to the list
  // of installed applications on the system. We use SchemeSupported
  // method to check if the URI scheme is supported and then use
  // generic nsFlatpakHandlerApp which forwards launch call to the system.
  if (widget::ShouldUsePortal(widget::PortalKind::MimeHandler)) {
    LOG("  using portal");
    if (mozilla::net::IsLoopbackHostname(aURIScheme)) {
      LOG("  quit, IsLoopbackHostname");
      // When the user writes foo:1234, we try to handle it natively using
      // GetAppForURIScheme, and if that fails, we carry on. On flatpak there's
      // no way to know if an app has handlers or not. Some things like
      // localhost:1234 are really unlikely to be handled by native
      // apps, and we're much better off returning an error here instead.
      return NS_ERROR_FAILURE;
    }
    GUniquePtr<GError> error;

    nsAppShell::DBusConnectionCheck();
    RefPtr<GDBusProxy> proxy = dont_AddRef(g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr, OPENURI_BUS_NAME,
        OPENURI_OBJECT_PATH, OPENURI_INTERFACE_NAME,
        nullptr,  // cancellable
        getter_Transfers(error)));
    if (error) {
      g_warning("Failed to create proxy: %s\n", error->message);
      return NS_ERROR_FAILURE;
    }
    // Construct the dictionary of options (empty in current implementaiton)
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    RefPtr<GVariant> result = dont_AddRef(g_dbus_proxy_call_sync(
        proxy, SCHEME_SUPPORTED_METHOD,
        g_variant_new("(sa{sv})", PromiseFlatCString(aURIScheme).get(),
                      &builder),
        G_DBUS_CALL_FLAGS_NONE,
        -1,       // timeout
        nullptr,  // cancellable
        getter_Transfers(error)));
    if (error) {
      if (error->code == G_DBUS_ERROR_UNKNOWN_METHOD) {
        // The method "SchemeSupported" not available, seems like we're running
        // older portal. Be optimistic about supported scheme handler
        LOG("  SchemeSupported method not found, fallback to flatpak handler");
        RefPtr<nsFlatpakHandlerApp> mozApp = new nsFlatpakHandlerApp();
        mozApp.forget(aApp);
        return NS_OK;
      }
      g_warning("Failed to call SchemeSupported method: %s\n", error->message);
      return NS_ERROR_FAILURE;
    }

    gboolean supported;
    g_variant_get(result, "(b)", &supported);
    if (!supported) {
      LOG("  Scheme '%s' is NOT supported.",
          PromiseFlatCString(aURIScheme).get());
      return NS_ERROR_FAILURE;
    }
    LOG("  Scheme '%s' is supported.", PromiseFlatCString(aURIScheme).get());
    RefPtr<nsFlatpakHandlerApp> mozApp = new nsFlatpakHandlerApp();
    mozApp.forget(aApp);
    return NS_OK;
  }
#endif

  RefPtr<GAppInfo> app_info = dont_AddRef(g_app_info_get_default_for_uri_scheme(
      PromiseFlatCString(aURIScheme).get()));
  if (!app_info) {
    LOG("  Failed to get info from g_app_info_get_default_for_uri_scheme()");
    return NS_ERROR_FAILURE;
  }
  LOG("  g_app_info_get_default_for_uri_scheme() provides %s : %s",
      g_app_info_get_name(app_info), g_app_info_get_executable(app_info));
  RefPtr<nsGIOMimeApp> mozApp = new nsGIOMimeApp(app_info.forget());
  mozApp.forget(aApp);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::GetAppsForURIScheme(const nsACString& aURIScheme,
                                  nsIMutableArray** aResult) {
  // We don't need to return the nsFlatpakHandlerApp here because
  // it would be skipped by the callers anyway.
  // The preferred handler is provided by GetAppForURIScheme.
  // This method returns all possible application handlers
  // including preferred one. The callers skips the preferred
  // handler in this list to avoid duplicate records in the list
  // they create.
  nsCOMPtr<nsIMutableArray> handlersArray =
      do_CreateInstance(NS_ARRAY_CONTRACTID);

  nsAutoCString contentType("x-scheme-handler/");
  contentType.Append(aURIScheme);

  LOG("nsGIOService::GetAppsForURIScheme() %s", contentType.get());

  GList* appInfoList = g_app_info_get_all_for_type(contentType.get());
  // g_app_info_get_all_for_type returns NULL when no appinfo is found
  // or error occurs (contentType is NULL). We are fine with empty app list
  // and we're sure that contentType is not NULL, so we won't return failure.
  if (appInfoList) {
    GList* appInfo = appInfoList;
    while (appInfo) {
      nsCOMPtr<nsIGIOMimeApp> mimeApp =
          new nsGIOMimeApp(dont_AddRef(G_APP_INFO(appInfo->data)));
      handlersArray->AppendElement(mimeApp);
      LOG("  adding %s : %s", g_app_info_get_name(G_APP_INFO(appInfo->data)),
          g_app_info_get_executable(G_APP_INFO(appInfo->data)));
      appInfo = appInfo->next;
    }
    g_list_free(appInfoList);
  } else {
    LOG("  g_app_info_get_all_for_type() is empty");
  }
  handlersArray.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::GetAppForMimeType(const nsACString& aMimeType,
                                nsIHandlerApp** aApp) {
  *aApp = nullptr;

  LOG("nsGIOService::GetAppForMimeType() %s",
      PromiseFlatCString(aMimeType).get());

  // Flatpak does not reveal installed application to the sandbox,
  // we need to create generic system handler.
  if (widget::ShouldUsePortal(widget::PortalKind::MimeHandler)) {
    LOG("  using portal");
    RefPtr<nsFlatpakHandlerApp> mozApp = new nsFlatpakHandlerApp();
    mozApp.forget(aApp);
    return NS_OK;
  }

  LOG("  g_content_type_from_mime_type(%s)",
      PromiseFlatCString(aMimeType).get());
  GUniquePtr<char> content_type(
      g_content_type_from_mime_type(PromiseFlatCString(aMimeType).get()));
  if (!content_type) {
    LOG("  g_content_type_from_mime_type() failed.");
    return NS_ERROR_FAILURE;
  }

  // GIO returns "unknown" appinfo for the application/octet-stream, which is
  // useless. It's better to fallback to create appinfo from file extension
  // later.
  if (g_content_type_is_unknown(content_type.get())) {
    LOG("  g_content_type_from_mime_type() return unknown app.");
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<GAppInfo> app_info =
      dont_AddRef(g_app_info_get_default_for_type(content_type.get(), false));
  if (!app_info) {
    LOG("  g_app_info_get_default_for_type() failed.");
    return NS_ERROR_FAILURE;
  }
  LOG("  g_app_info_get_default_for_type() gives us %s : %s",
      g_app_info_get_name(app_info), g_app_info_get_executable(app_info));
#ifdef __OpenBSD__
  char* t;
  t = g_find_program_in_path(g_app_info_get_executable(app_info));
  if (t != nullptr) {
    g_debug("%s is registered as handler for %s, binary available as %s",
            g_app_info_get_executable(app_info), content_type.get(), t);
  } else {
    g_warning(
        "%s is registered as handler for %s but not available in PATH "
        "(missing unveil ?)",
        g_app_info_get_executable(app_info), content_type.get());
  }
#endif
  RefPtr<nsGIOMimeApp> mozApp = new nsGIOMimeApp(app_info.forget());
  mozApp.forget(aApp);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::GetDescriptionForMimeType(const nsACString& aMimeType,
                                        nsACString& aDescription) {
  GUniquePtr<char> content_type(
      g_content_type_from_mime_type(PromiseFlatCString(aMimeType).get()));
  if (!content_type) {
    return NS_ERROR_FAILURE;
  }

  GUniquePtr<char> desc(g_content_type_get_description(content_type.get()));
  if (!desc) {
    return NS_ERROR_FAILURE;
  }

  aDescription.Assign(desc.get());
  return NS_OK;
}

static nsresult ShowURIImpl(nsIURI* aURI, const char* aXDGToken = nullptr) {
  nsAutoCString spec;
  MOZ_TRY(aURI->GetSpec(spec));
  GUniquePtr<GError> error;
  LOG("ShowURIImpl() spec %s : aXDGToken %s", spec.get(),
      aXDGToken ? aXDGToken : "none");
#ifdef __OpenBSD__
  if (!g_app_info_launch_default_for_uri_openbsd(
          spec.get(), GetLaunchContext(aXDGToken).get(),
#else
  if (!g_app_info_launch_default_for_uri(spec.get(),
                                         GetLaunchContext(aXDGToken).get(),
#endif
          getter_Transfers(error))) {
    g_warning("Could not launch default application for URI: %s",
              error->message);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult nsGIOService::ShowURI(nsIURI* aURI) {
  auto promise = mozilla::widget::RequestWaylandFocusPromise();
  if (!promise) {
    LOG("nsGIOService::ShowURI() failed to get focus promise, fallback to "
        "ShowURIImpl()");
    return ShowURIImpl(aURI);
  }
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      /* resolve */
      [uri = RefPtr{aURI}](nsCString token) {
        LOG("nsGIOService::ShowURI() received XDG focus token");
        ShowURIImpl(uri, token.get());
      },
      /* reject */
      [uri = RefPtr{aURI}](bool /*state*/) {
        LOG("nsGIOService::ShowURI() without XDG focus token");
        ShowURIImpl(uri);
      });
  return NS_OK;
}

static nsresult LaunchPathImpl(const nsACString& aPath,
                               const char* aXDGToken = nullptr) {
  RefPtr<GFile> file = dont_AddRef(
      g_file_new_for_commandline_arg(PromiseFlatCString(aPath).get()));
  GUniquePtr<char> spec(g_file_get_uri(file));
  LOG("LaunchPathImpl() file %s : aXDGToken %s",
      PromiseFlatCString(aPath).get(), aXDGToken ? aXDGToken : "none");
  GUniquePtr<GError> error;
#ifdef __OpenBSD__
  g_app_info_launch_default_for_uri_openbsd(spec.get(),
                                            GetLaunchContext(aXDGToken).get(),
#else
  g_app_info_launch_default_for_uri(spec.get(),
                                    GetLaunchContext(aXDGToken).get(),
#endif
                                            getter_Transfers(error));
  if (error) {
    g_warning("Cannot launch default application: %s", error->message);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

static nsresult LaunchPath(const nsACString& aPath) {
  auto promise = mozilla::widget::RequestWaylandFocusPromise();
  if (!promise) {
    LOG("LaunchPath() failed to get focus promise, fallback to "
        "LaunchPathImpl()");
    return LaunchPathImpl(aPath);
  }
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      /* resolve */
      [path = nsCString{aPath}](nsCString token) {
        LOG("LaunchPath() received XDG focus token");
        LaunchPathImpl(path, token.get());
      },
      /* reject */
      [path = nsCString{aPath}](bool /*state*/) {
        LOG("LaunchPath() without XDG focus token");
        LaunchPathImpl(path);
      });
  return NS_OK;
}

nsresult nsGIOService::LaunchFile(const nsACString& aPath) {
  return LaunchPath(aPath);
}

nsresult nsGIOService::GetIsRunningUnderFlatpak(bool* aResult) {
  *aResult = mozilla::widget::IsRunningUnderFlatpak();
  LOG("nsGIOService::GetIsRunningUnderFlatpak() %d", *aResult);
  return NS_OK;
}

nsresult nsGIOService::GetIsRunningUnderSnap(bool* aResult) {
  *aResult = mozilla::widget::IsRunningUnderSnap();
  LOG("nsGIOService::IsRunningUnderSnap() %d", *aResult);
  return NS_OK;
}

static nsresult RevealDirectory(nsIFile* aFile, bool aForce) {
  nsAutoCString path;
  if (bool isDir; NS_SUCCEEDED(aFile->IsDirectory(&isDir)) && isDir) {
    MOZ_TRY(aFile->GetNativePath(path));
    return LaunchPath(path);
  }

  if (!aForce) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIFile> parentDir;
  MOZ_TRY(aFile->GetParent(getter_AddRefs(parentDir)));
  MOZ_TRY(parentDir->GetNativePath(path));
  return LaunchPath(path);
}

#ifdef MOZ_ENABLE_DBUS
// Classic DBus
const char kFreedesktopFileManagerName[] = "org.freedesktop.FileManager1";
const char kFreedesktopFileManagerPath[] = "/org/freedesktop/FileManager1";
const char kMethodShowItems[] = "ShowItems";

// Portal for Snap, Flatpak
const char kFreedesktopPortalName[] = "org.freedesktop.portal.Desktop";
const char kFreedesktopPortalPath[] = "/org/freedesktop/portal/desktop";
const char kFreedesktopPortalOpenURI[] = "org.freedesktop.portal.OpenURI";
const char kMethodOpenDirectory[] = "OpenDirectory";

static nsresult RevealFileViaDBusWithProxy(GDBusProxy* aProxy, nsIFile* aFile,
                                           const char* aMethod) {
  nsAutoCString path;
  MOZ_TRY(aFile->GetNativePath(path));

  char* activationToken = nullptr;
  auto releaseActivationToken = MakeScopeExit([&] { g_free(activationToken); });

  // Try to get activation token from GdkDisplay
  if (GdkDisplay* display = gdk_display_get_default()) {
    if (GdkAppLaunchContext* context =
            gdk_display_get_app_launch_context(display)) {
      activationToken = g_app_launch_context_get_startup_notify_id(
          G_APP_LAUNCH_CONTEXT(context), nullptr, nullptr);
      g_object_unref(context);
    }
  }

  const int32_t timeout =
      StaticPrefs::widget_gtk_file_manager_show_items_timeout_ms();

  nsAppShell::DBusConnectionCheck();
  if (!(strcmp(aMethod, kMethodOpenDirectory) == 0)) {
    GUniquePtr<gchar> uri(g_filename_to_uri(path.get(), nullptr, nullptr));
    if (!uri) {
      RevealDirectory(aFile, /* aForce = */ true);
      return NS_ERROR_FAILURE;
    }

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);
    g_variant_builder_add(&builder, "s", uri.get());

    RefPtr<GVariant> variant = dont_AddRef(g_variant_ref_sink(g_variant_new(
        "(ass)", &builder, activationToken ? activationToken : "")));
    g_variant_builder_clear(&builder);

    widget::DBusProxyCall(aProxy, aMethod, variant, G_DBUS_CALL_FLAGS_NONE,
                          timeout)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [](RefPtr<GVariant>&& aResult) {
              // Do nothing, file is shown, we're done.
            },
            [file = RefPtr{aFile}, aMethod](GUniquePtr<GError>&& aError) {
              g_printerr("Failed to query file manager via %s: %s\n", aMethod,
                         aError->message);
              RevealDirectory(file, /* aForce = */ true);
            });
  } else {
    int fd = open(path.get(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      g_printerr("Failed to open file: %s returned %d\n", path.get(), errno);
      RevealDirectory(aFile, /* aForce = */ true);
      return NS_ERROR_FAILURE;
    }

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);

    static auto g_unix_fd_list_new_from_array =
        (GUnixFDList * (*)(const gint* fds, gint n_fds))
            dlsym(RTLD_DEFAULT, "g_unix_fd_list_new_from_array");

    // Will take ownership of the fd, so we dont have to care about it anymore
    RefPtr<GUnixFDList> fd_list =
        dont_AddRef(g_unix_fd_list_new_from_array(&fd, 1));

    RefPtr<GVariant> variant = dont_AddRef(g_variant_ref_sink(g_variant_new(
        "(sha{sv})", activationToken ? activationToken : "", 0, &options)));
    g_variant_builder_clear(&options);

    widget::DBusProxyCallWithUnixFDList(
        aProxy, aMethod, variant, G_DBUS_CALL_FLAGS_NONE, timeout, fd_list)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [](std::pair<RefPtr<GVariant>, RefPtr<GUnixFDList>>&& aResult) {
              // Do nothing, file is shown, we're done.
            },
            [file = RefPtr{aFile}, aMethod](GUniquePtr<GError>&& aError) {
              g_printerr("Failed to query file manager via %s: %s\n", aMethod,
                         aError->message);
              RevealDirectory(file, /* aForce = */ true);
            });
  }
  return NS_OK;
}

static void RevealFileViaDBus(nsIFile* aFile, const char* aName,
                              const char* aPath, const char* aCall,
                              const char* aMethod) {
  widget::CreateDBusProxyForBus(
      G_BUS_TYPE_SESSION,
      GDBusProxyFlags(G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES),
      /* aInterfaceInfo = */ nullptr, aName, aPath, aCall)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [file = RefPtr{aFile}, aMethod](RefPtr<GDBusProxy>&& aProxy) {
            RevealFileViaDBusWithProxy(aProxy.get(), file, aMethod);
          },
          [file = RefPtr{aFile}, aName](GUniquePtr<GError>&& aError) {
            g_printerr("Failed to create DBUS proxy for %s: %s\n", aName,
                       aError->message);
            RevealDirectory(file, /* aForce = */ true);
          });
}

static void RevealFileViaDBusClassic(nsIFile* aFile) {
  RevealFileViaDBus(aFile, kFreedesktopFileManagerName,
                    kFreedesktopFileManagerPath, kFreedesktopFileManagerName,
                    kMethodShowItems);
}

static void RevealFileViaDBusPortal(nsIFile* aFile) {
  RevealFileViaDBus(aFile, kFreedesktopPortalName, kFreedesktopPortalPath,
                    kFreedesktopPortalOpenURI, kMethodOpenDirectory);
}
#endif

nsresult nsGIOService::RevealFile(nsIFile* aFile) {
#ifdef MOZ_ENABLE_DBUS
  if (ShouldUsePortal(widget::PortalKind::OpenUri)) {
    RevealFileViaDBusPortal(aFile);
  } else if (NS_SUCCEEDED(RevealDirectory(aFile, /* aForce = */ false))) {
    return NS_OK;
  } else {
    RevealFileViaDBusClassic(aFile);
  }
  return NS_OK;
#else
  return RevealDirectory(aFile, /* aForce = */ true);
#endif
}

/**
 * Find GIO Mime App from given commandline.
 * This is different from CreateAppFromCommand because instead of creating the
 * GIO Mime App in case it's not found in the GIO application list, the method
 * returns error.
 * @param aCmd command with parameters used to start the application
 * @return NS_OK when application is found, NS_ERROR_NOT_AVAILABLE otherwise
 */
NS_IMETHODIMP
nsGIOService::FindAppFromCommand(nsACString const& aCmd,
                                 nsIGIOMimeApp** aAppInfo) {
  RefPtr<GAppInfo> app_info;

  LOG("nsGIOService::FindAppFromCommand() %s", PromiseFlatCString(aCmd).get());

  GList* apps = g_app_info_get_all();

  // Try to find relevant and existing GAppInfo in all installed application
  // We do this by comparing each GAppInfo's executable with out own
  for (GList* node = apps; node; node = node->next) {
    RefPtr<GAppInfo> app_info_from_list = dont_AddRef((GAppInfo*)node->data);
    LOG("  checking %s : %s", g_app_info_get_name(app_info_from_list),
        g_app_info_get_executable(app_info_from_list));
    node->data = nullptr;
    if (!app_info) {
      // If the executable is not absolute, get it's full path
      GUniquePtr<char> executable(g_find_program_in_path(
          g_app_info_get_executable(app_info_from_list)));

      if (executable &&
          strcmp(executable.get(), PromiseFlatCString(aCmd).get()) == 0) {
        app_info = std::move(app_info_from_list);
        LOG("  found %s : %s", g_app_info_get_name(app_info),
            g_app_info_get_executable(app_info));
        // Can't break here because we need to keep iterating to unref the other
        // nodes.
      }
    }
  }

  g_list_free(apps);
  if (!app_info) {
    LOG("  failed to get application");
    *aAppInfo = nullptr;
    return NS_ERROR_NOT_AVAILABLE;
  }
  RefPtr<nsGIOMimeApp> app = new nsGIOMimeApp(app_info.forget());
  app.forget(aAppInfo);
  return NS_OK;
}

/**
 * Create GIOHandlerApp specified by application id. The id is the basename
 * of the desktop file including the .desktop extension. For example:
 * org.mozilla.firefox.desktop
 */
NS_IMETHODIMP
nsGIOService::CreateHandlerAppFromAppId(const char* aAppId,
                                        nsIGIOHandlerApp** aResult) {
  LOG("nsGIOService::CreateHandlerAppFromAppId() %s", aAppId);

  RefPtr<GAppInfo> appInfo =
      dont_AddRef((GAppInfo*)g_desktop_app_info_new(aAppId));
  if (!appInfo) {
    LOG("  Appinfo not found");
    g_warning("Appinfo not found for: %s", aAppId);
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIGIOHandlerApp> mozApp = new nsGIOHandlerApp(appInfo.forget());

  mozApp.forget(aResult);
  return NS_OK;
}

/**
 * Create application info for specified command and application name.
 * Command arguments are ignored and the "%u" is always added.
 * @param cmd command to execute
 * @param appName application name
 * @param appInfo location where created GAppInfo is stored
 * @return NS_OK when object is created, NS_ERROR_FILE_NOT_FOUND when executable
 * is not found in the system path or NS_ERROR_FAILURE otherwise.
 */
NS_IMETHODIMP
nsGIOService::CreateAppFromCommand(nsACString const& cmd,
                                   nsACString const& appName,
                                   nsIGIOMimeApp** appInfo) {
  *appInfo = nullptr;

  LOG("nsGIOService::CreateAppFromCommand() %s : %s",
      PromiseFlatCString(appName).get(), PromiseFlatCString(cmd).get());

  // Using G_APP_INFO_CREATE_SUPPORTS_URIS calling
  // g_app_info_create_from_commandline appends %u to the cmd even when cmd
  // already contains this parameter. To avoid that we're going to remove
  // arguments before passing to it.
  nsAutoCString commandWithoutArgs;
  nsresult rv = GetCommandFromCommandline(cmd, commandWithoutArgs);
  NS_ENSURE_SUCCESS(rv, rv);

  GUniquePtr<GError> error;
  RefPtr<GAppInfo> app_info = dont_AddRef(g_app_info_create_from_commandline(
      commandWithoutArgs.get(), PromiseFlatCString(appName).get(),
      G_APP_INFO_CREATE_SUPPORTS_URIS, getter_Transfers(error)));
  if (!app_info) {
    g_warning("Cannot create application info from command: %s",
              error->message);
    return NS_ERROR_FAILURE;
  }

  // Check if executable exist in path
  GUniquePtr<gchar> executableWithFullPath(
      g_find_program_in_path(commandWithoutArgs.get()));
  if (!executableWithFullPath) {
    LOG("  quit, program not found in path");
    return NS_ERROR_FILE_NOT_FOUND;
  }
  RefPtr<nsGIOMimeApp> mozApp = new nsGIOMimeApp(app_info.forget());
  mozApp.forget(appInfo);
  return NS_OK;
}

/**
 * Create application info which runs an executable with one argument, the URI
 * the application is launched with, or the path of the local file it names.
 * @param aExecutable executable to run
 * @param aAppName application name
 * @param aPassURI true to pass the URI unchanged, false to pass the path
 * @param aAppInfo location where created GAppInfo is stored
 * @return NS_OK when object is created, NS_ERROR_FILE_NOT_FOUND when
 * aExecutable is not an executable file or its path contains %, which GLib
 * does not accept, NS_ERROR_NOT_IMPLEMENTED on OpenBSD, or NS_ERROR_FAILURE
 * otherwise.
 */
NS_IMETHODIMP
nsGIOService::CreateAppFromExecutable(nsIFile* aExecutable,
                                      nsACString const& aAppName, bool aPassURI,
                                      nsIGIOMimeApp** aAppInfo) {
  NS_ENSURE_ARG(aExecutable);
  *aAppInfo = nullptr;

#ifdef __OpenBSD__
  // g_app_info_launch_uris_openbsd() runs g_app_info_get_executable(), the Exec
  // key up to its first space, with the path of a file URI only.
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  nsAutoCString path;
  nsresult rv = aExecutable->GetNativePath(path);
  NS_ENSURE_SUCCESS(rv, rv);

  LOG("nsGIOService::CreateAppFromExecutable() %s : %s",
      PromiseFlatCString(aAppName).get(), path.get());

  GUniquePtr<gchar> quoted(g_shell_quote(path.get()));
  nsAutoCString exec(quoted.get());
  // In the Exec key, % starts a field code. GLib checks that the Exec key names
  // an executable file before it expands field codes, so it refuses a path
  // with % (https://gitlab.gnome.org/GNOME/glib/-/work_items/4111).
  exec.ReplaceSubstring("%"_ns, "%%"_ns);
  // %u passes the URI as it is, as X-GIO-NoFuse below keeps GLib from passing
  // the path of a file URI instead, and %f passes the path of the local file.
  exec.Append(aPassURI ? " %u" : " %f");

  RefPtr<GKeyFile> keyFile = dont_AddRef(g_key_file_new());
  g_key_file_set_string(keyFile, G_KEY_FILE_DESKTOP_GROUP,
                        G_KEY_FILE_DESKTOP_KEY_TYPE,
                        G_KEY_FILE_DESKTOP_TYPE_APPLICATION);
  g_key_file_set_string(keyFile, G_KEY_FILE_DESKTOP_GROUP,
                        G_KEY_FILE_DESKTOP_KEY_NAME,
                        PromiseFlatCString(aAppName).get());
  g_key_file_set_string(keyFile, G_KEY_FILE_DESKTOP_GROUP,
                        G_KEY_FILE_DESKTOP_KEY_EXEC, exec.get());
  g_key_file_set_boolean(keyFile, G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, TRUE);
  // The activation token, which nsGIOMimeApp::LaunchWithURI() requests on
  // Wayland, is passed only to an application with startup notification.
  g_key_file_set_boolean(keyFile, G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_STARTUP_NOTIFY, TRUE);
  g_key_file_set_boolean(keyFile, G_KEY_FILE_DESKTOP_GROUP, "X-GIO-NoFuse",
                         TRUE);

  // GLib refuses an Exec key which does not name an executable file.
  RefPtr<GAppInfo> app_info =
      dont_AddRef(G_APP_INFO(g_desktop_app_info_new_from_keyfile(keyFile)));
  if (!app_info) {
    LOG("  quit, not an executable file");
    return NS_ERROR_FILE_NOT_FOUND;
  }
  RefPtr<nsGIOMimeApp> mozApp = new nsGIOMimeApp(app_info.forget());
  mozApp.forget(aAppInfo);
  return NS_OK;
#endif
}
