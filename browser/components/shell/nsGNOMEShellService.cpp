/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Preferences.h"
#include "mozilla/widget/GSettings.h"

#include "nsAppRunner.h"
#include "nsCOMPtr.h"
#include "nsGNOMEShellService.h"
#include "nsShellService.h"
#include "nsIFile.h"
#include "nsIProperties.h"
#include "nsDirectoryServiceDefs.h"
#include "prenv.h"
#include "nsString.h"
#include "nsIGIOService.h"
#include "nsIStringBundle.h"
#include "nsServiceManagerUtils.h"
#include "nsIImageLoadingContent.h"
#include "imgIRequest.h"
#include "imgIContainer.h"
#include "mozilla/Components.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/dom/Element.h"
#include "nsImageToPixbuf.h"
#include "nsXULAppAPI.h"
#include "gfxPlatform.h"

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdlib.h>

using namespace mozilla;

struct ProtocolAssociation {
  const char* name;
  bool essential;
};

struct MimeTypeAssociation {
  const char* mimeType;
  const char* extensions;
};

static const ProtocolAssociation appProtocols[] = {
    // clang-format off
  { "http",   true     },
  { "https",  true     },
  { "chrome", false }
    // clang-format on
};

static const MimeTypeAssociation appTypes[] = {
    // clang-format off
  { "text/html",             "htm html shtml" },
  { "application/xhtml+xml", "xhtml xht"      }
    // clang-format on
};

#define kDesktopBGSchema "org.gnome.desktop.background"_ns
#define kDesktopColorGSKey "primary-color"_ns

nsresult nsGNOMEShellService::Init() {
  nsresult rv;

  if (gfxPlatform::IsHeadless()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // GSettings or GIO _must_ be available, or we do not allow
  // CreateInstance to succeed.

#ifdef MOZ_ENABLE_DBUS
  if (widget::IsGnomeDesktopEnvironment() &&
      Preferences::GetBool("browser.gnome-search-provider.enabled", false)) {
    mSearchProvider.Startup();
  }
#endif

  // Check G_BROKEN_FILENAMES.  If it's set, then filenames in glib use
  // the locale encoding.  If it's not set, they use UTF-8.
  mUseLocaleFilenames = PR_GetEnv("G_BROKEN_FILENAMES") != nullptr;

  if (GetAppPathFromLauncher()) return NS_OK;

  nsCOMPtr<nsIProperties> dirSvc(
      do_GetService("@mozilla.org/file/directory_service;1"));
  NS_ENSURE_TRUE(dirSvc, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIFile> appPath;
  rv = dirSvc->Get(XRE_EXECUTABLE_FILE, NS_GET_IID(nsIFile),
                   getter_AddRefs(appPath));
  NS_ENSURE_SUCCESS(rv, rv);

  return appPath->GetNativePath(mAppPath);
}

NS_IMPL_ISUPPORTS(nsGNOMEShellService, nsIGNOMEShellService, nsIShellService,
                  nsIToolkitShellService)

bool nsGNOMEShellService::GetAppPathFromLauncher() {
  gchar* tmp;

  const char* launcher = PR_GetEnv("MOZ_APP_LAUNCHER");
  if (!launcher) return false;

  if (g_path_is_absolute(launcher)) {
    mAppPath = launcher;
    tmp = g_path_get_basename(launcher);
    gchar* fullpath = g_find_program_in_path(tmp);
    if (fullpath && mAppPath.Equals(fullpath)) mAppIsInPath = true;
    g_free(fullpath);
  } else {
    tmp = g_find_program_in_path(launcher);
    if (!tmp) return false;
    mAppPath = tmp;
    mAppIsInPath = true;
  }

  g_free(tmp);
  return true;
}

bool nsGNOMEShellService::KeyMatchesAppName(const char* aKeyValue) const {
  gchar* commandPath;
  if (mUseLocaleFilenames) {
    gchar* nativePath =
        g_filename_from_utf8(aKeyValue, -1, nullptr, nullptr, nullptr);
    if (!nativePath) {
      NS_ERROR("Error converting path to filesystem encoding");
      return false;
    }

    commandPath = g_find_program_in_path(nativePath);
    g_free(nativePath);
  } else {
    commandPath = g_find_program_in_path(aKeyValue);
  }

  if (!commandPath) return false;

  bool matches = mAppPath.Equals(commandPath);
  g_free(commandPath);
  return matches;
}

bool nsGNOMEShellService::CheckHandlerMatchesAppName(
    const nsACString& handler) const {
  gint argc;
  gchar** argv;
  nsAutoCString command(handler);

  // The string will be something of the form: [/path/to/]browser "%s"
  // We want to remove all of the parameters and get just the binary name.

  if (g_shell_parse_argv(command.get(), &argc, &argv, nullptr) && argc > 0) {
    command.Assign(argv[0]);
    g_strfreev(argv);
  }

  if (!KeyMatchesAppName(command.get()))
    return false;  // the handler is set to another app

  return true;
}

NS_IMETHODIMP
nsGNOMEShellService::IsDefaultBrowser(bool aForAllTypes,
                                      bool* aIsDefaultBrowser) {
  *aIsDefaultBrowser = false;

  if (widget::IsRunningUnderSnap()) {
    const gchar* argv[] = {"xdg-settings", "check", "default-web-browser",
                           (MOZ_APP_NAME ".desktop"), nullptr};
    GSpawnFlags flags = static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                                 G_SPAWN_STDERR_TO_DEV_NULL);
    gchar* output = nullptr;
    gint exit_status = 0;
    if (!g_spawn_sync(nullptr, (gchar**)argv, nullptr, flags, nullptr, nullptr,
                      &output, nullptr, &exit_status, nullptr)) {
      return NS_OK;
    }
    if (exit_status != 0) {
      g_free(output);
      return NS_OK;
    }
    if (strcmp(output, "yes\n") == 0) {
      *aIsDefaultBrowser = true;
    }
    g_free(output);
    return NS_OK;
  }

  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  nsAutoCString handler;
  nsCOMPtr<nsIGIOMimeApp> gioApp;

  for (auto appProtocol : appProtocols) {
    if (!appProtocol.essential) continue;

    if (!IsDefaultForSchemeHelper(nsDependentCString(appProtocol.name),
                                  giovfs)) {
      return NS_OK;
    }
  }

  *aIsDefaultBrowser = true;

  return NS_OK;
}

bool nsGNOMEShellService::IsDefaultForSchemeHelper(
    const nsACString& aScheme, nsIGIOService* giovfs) const {
  nsCOMPtr<nsIGIOService> gioService;
  if (!giovfs) {
    gioService = do_GetService(NS_GIOSERVICE_CONTRACTID);
    giovfs = gioService.get();
  }

  if (!giovfs) {
    return false;
  }

  nsCOMPtr<nsIGIOMimeApp> gioApp;
  nsCOMPtr<nsIHandlerApp> handlerApp;
  giovfs->GetAppForURIScheme(aScheme, getter_AddRefs(handlerApp));
  gioApp = do_QueryInterface(handlerApp);
  if (!gioApp) {
    return false;
  }

  nsAutoCString handler;
  gioApp->GetCommand(handler);
  return CheckHandlerMatchesAppName(handler);
}

NS_IMETHODIMP
nsGNOMEShellService::IsDefaultForScheme(const nsACString& aScheme,
                                        bool* aIsDefaultBrowser) {
  *aIsDefaultBrowser = IsDefaultForSchemeHelper(aScheme, nullptr);
  return NS_OK;
}

NS_IMETHODIMP
nsGNOMEShellService::SetDefaultBrowser(bool aForAllUsers) {
#ifdef DEBUG
  if (aForAllUsers)
    NS_WARNING(
        "Setting the default browser for all users is not yet supported");
#endif

  if (widget::IsRunningUnderSnap()) {
    const gchar* argv[] = {"xdg-settings", "set", "default-web-browser",
                           (MOZ_APP_NAME ".desktop"), nullptr};
    GSpawnFlags flags = static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                                 G_SPAWN_STDOUT_TO_DEV_NULL |
                                                 G_SPAWN_STDERR_TO_DEV_NULL);
    g_spawn_sync(nullptr, (gchar**)argv, nullptr, flags, nullptr, nullptr,
                 nullptr, nullptr, nullptr, nullptr);
    return NS_OK;
  }

  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (giovfs) {
    nsresult rv;
    nsCOMPtr<nsIStringBundleService> bundleService =
        components::StringBundle::Service(&rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIStringBundle> brandBundle;
    rv = bundleService->CreateBundle(BRAND_PROPERTIES,
                                     getter_AddRefs(brandBundle));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString brandShortName;
    brandBundle->GetStringFromName("brandShortName", brandShortName);

    // use brandShortName as the application id.
    NS_ConvertUTF16toUTF8 id(brandShortName);
    nsCOMPtr<nsIGIOMimeApp> appInfo;
    rv = giovfs->FindAppFromCommand(mAppPath, getter_AddRefs(appInfo));
    if (NS_FAILED(rv)) {
      // Application was not found in the list of installed applications
      // provided by OS. Fallback to create appInfo from command and name.
      rv = giovfs->CreateAppFromCommand(mAppPath, id, getter_AddRefs(appInfo));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // set handler for the protocols
    for (auto appProtocol : appProtocols) {
      appInfo->SetAsDefaultForURIScheme(nsDependentCString(appProtocol.name));
    }

    // set handler for .html and xhtml files and MIME types:
    // Add mime types for html, xhtml extension and set app to just created
    // appinfo.
    for (auto appType : appTypes) {
      appInfo->SetAsDefaultForMimeType(nsDependentCString(appType.mimeType));
      appInfo->SetAsDefaultForFileExtensions(
          nsDependentCString(appType.extensions));
    }
  }

  nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefs) {
    (void)prefs->SetBoolPref(PREF_CHECKDEFAULTBROWSER, true);
    // Reset the number of times the dialog should be shown
    // before it is silenced.
    (void)prefs->SetIntPref(PREF_DEFAULTBROWSERCHECKCOUNT, 0);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsGNOMEShellService::GetCanSetDesktopBackground(bool* aResult) {
  // setting desktop background is currently only supported
  // for Gnome or desktops using the same GSettings keys
  if (widget::IsGnomeDesktopEnvironment()) {
    *aResult = true;
    return NS_OK;
  }

  *aResult = !!getenv("GNOME_DESKTOP_SESSION_ID");
  return NS_OK;
}

static nsresult WriteImage(const nsCString& aPath, imgIContainer* aImage) {
  RefPtr<GdkPixbuf> pixbuf = nsImageToPixbuf::ImageToPixbuf(aImage);
  if (!pixbuf) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  gboolean res = gdk_pixbuf_save(pixbuf, aPath.get(), "png", nullptr, nullptr);
  return res ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsGNOMEShellService::SetDesktopBackground(dom::Element* aElement,
                                          int32_t aPosition,
                                          const nsACString& aImageName) {
  nsCOMPtr<nsIImageLoadingContent> imageContent = do_QueryInterface(aElement);
  if (!imageContent) {
    return NS_ERROR_FAILURE;
  }

  // get the image container
  nsCOMPtr<imgIRequest> request;
  imageContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                           getter_AddRefs(request));
  if (!request) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<imgIContainer> container;
  request->GetImage(getter_AddRefs(container));
  if (!container) {
    return NS_ERROR_FAILURE;
  }

  // Set desktop wallpaper filling style
  nsAutoCString options;
  if (aPosition == BACKGROUND_TILE)
    options.AssignLiteral("wallpaper");
  else if (aPosition == BACKGROUND_STRETCH)
    options.AssignLiteral("stretched");
  else if (aPosition == BACKGROUND_FILL)
    options.AssignLiteral("zoom");
  else if (aPosition == BACKGROUND_FIT)
    options.AssignLiteral("scaled");
  else if (aPosition == BACKGROUND_SPAN)
    options.AssignLiteral("spanned");
  else
    options.AssignLiteral("centered");

  // Write the background file to the home directory.
  nsAutoCString filePath(PR_GetEnv("HOME"));
  nsAutoString brandName;

  // get the product brand name from localized strings
  if (nsCOMPtr<nsIStringBundleService> bundleService =
          components::StringBundle::Service()) {
    nsCOMPtr<nsIStringBundle> brandBundle;
    bundleService->CreateBundle(BRAND_PROPERTIES, getter_AddRefs(brandBundle));
    if (bundleService) {
      brandBundle->GetStringFromName("brandShortName", brandName);
    }
  }

  // build the file name
  filePath.Append('/');
  filePath.Append(NS_ConvertUTF16toUTF8(brandName));
  filePath.AppendLiteral("_wallpaper.png");

  // write the image to a file in the home dir
  MOZ_TRY(WriteImage(filePath, container));

  widget::GSettings::Collection bgSettings(kDesktopBGSchema);
  if (!bgSettings) {
    return NS_ERROR_FAILURE;
  }

  GUniquePtr<gchar> fileURI(
      g_filename_to_uri(filePath.get(), nullptr, nullptr));
  if (!fileURI) {
    return NS_ERROR_FAILURE;
  }

  bgSettings.SetString("picture-options"_ns, options);
  bgSettings.SetString("picture-uri"_ns, nsDependentCString(fileURI.get()));
  bgSettings.SetString("picture-uri-dark"_ns,
                       nsDependentCString(fileURI.get()));
  return NS_OK;
}

#define COLOR_16_TO_8_BIT(_c) ((_c) >> 8)
#define COLOR_8_TO_16_BIT(_c) ((_c) << 8 | (_c))

NS_IMETHODIMP
nsGNOMEShellService::GetDesktopBackgroundColor(uint32_t* aColor) {
  nsAutoCString background;
  widget::GSettings::GetString(kDesktopBGSchema, kDesktopColorGSKey,
                               background);
  if (background.IsEmpty()) {
    *aColor = 0;
    return NS_OK;
  }

  GdkColor color;
  gboolean success = gdk_color_parse(background.get(), &color);

  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  *aColor = COLOR_16_TO_8_BIT(color.red) << 16 |
            COLOR_16_TO_8_BIT(color.green) << 8 | COLOR_16_TO_8_BIT(color.blue);
  return NS_OK;
}

static void ColorToCString(uint32_t aColor, nsCString& aResult) {
  // The #rrrrggggbbbb format is used to match gdk_color_to_string()
  aResult.SetLength(13);
  char* buf = aResult.BeginWriting();
  if (!buf) return;

  uint16_t red = COLOR_8_TO_16_BIT((aColor >> 16) & 0xff);
  uint16_t green = COLOR_8_TO_16_BIT((aColor >> 8) & 0xff);
  uint16_t blue = COLOR_8_TO_16_BIT(aColor & 0xff);

  snprintf(buf, 14, "#%04x%04x%04x", red, green, blue);
}

NS_IMETHODIMP
nsGNOMEShellService::SetDesktopBackgroundColor(uint32_t aColor) {
  NS_ASSERTION(aColor <= 0xffffff, "aColor has extra bits");
  nsAutoCString colorString;
  ColorToCString(aColor, colorString);

  widget::GSettings::Collection bgSettings(kDesktopBGSchema);
  if (bgSettings) {
    bgSettings.SetString(kDesktopColorGSKey, colorString);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsGNOMEShellService::GetGSettingsString(const nsACString& aSchema,
                                        const nsACString& aKey,
                                        nsACString& aResult) {
  widget::GSettings::GetString(PromiseFlatCString(aSchema),
                               PromiseFlatCString(aKey), aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsGNOMEShellService::SetGSettingsString(const nsACString& aSchema,
                                        const nsACString& aKey,
                                        const nsACString& aValue) {
  widget::GSettings::Collection settings(PromiseFlatCString(aSchema));
  if (!settings) {
    return NS_ERROR_FAILURE;
  }
  if (!settings.SetString(PromiseFlatCString(aKey),
                          PromiseFlatCString(aValue))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP nsGNOMEShellService::GetArgv0(nsACString& output) {
  output.Assign(gArgc <= 0 ? "" : gArgv[0]);
  return NS_OK;
}

NS_IMETHODIMP nsGNOMEShellService::GetDesktopEntryStatus(
    const nsACString& aDesktopId,
    nsIGNOMEShellService::DesktopEntryStatus* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  RefPtr<GDesktopAppInfo> appinfo =
      dont_AddRef(g_desktop_app_info_new(PromiseFlatCString(aDesktopId).get()));
  if (!appinfo) {
    *aResult = nsIGNOMEShellService::DESKTOP_ENTRY_ABSENT;
  } else if (g_app_info_should_show(G_APP_INFO(appinfo.get()))) {
    *aResult = nsIGNOMEShellService::DESKTOP_ENTRY_VISIBLE;
  } else {
    *aResult = nsIGNOMEShellService::DESKTOP_ENTRY_INVISIBLE;
  }

  return NS_OK;
}
