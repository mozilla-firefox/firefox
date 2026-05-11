#filter substitution
#include @TOPOBJDIR@/source-repo.h
#include @TOPOBJDIR@/buildid.h
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * AppConstants is a set of immutable constants that are defined at build time.
 * These should not depend on any other JavaScript module.
 */
export var AppConstants = Object.freeze({
  // See this wiki page for more details about channel specific build
  // defines: https://wiki.mozilla.org/Platform/Channel-specific_build_defines
  NIGHTLY_BUILD: @NIGHTLY_BUILD_BOOL@,

  ENABLE_EXPLICIT_RESOURCE_MANAGEMENT: @ENABLE_EXPLICIT_RESOURCE_MANAGEMENT_BOOL@,

  RELEASE_OR_BETA: @RELEASE_OR_BETA_BOOL@,

  EARLY_BETA_OR_EARLIER: @EARLY_BETA_OR_EARLIER_BOOL@,

  IS_ESR: @MOZ_ESR_BOOL@,

  ACCESSIBILITY: @ACCESSIBILITY_BOOL@,

  // Official corresponds, roughly, to whether this build is performed
  // on Mozilla's continuous integration infrastructure. You should
  // disable developer-only functionality when this flag is set.
  MOZILLA_OFFICIAL: @MOZILLA_OFFICIAL_BOOL@,

  MOZ_OFFICIAL_BRANDING: @MOZ_OFFICIAL_BRANDING_BOOL@,

  BUILT_BY_MOZILLA:
#ifdef BUILT_BY_MOZILLA
  true,
#else
  false,
#endif

  MOZ_DEV_EDITION: @MOZ_DEV_EDITION_BOOL@,

  MOZ_SERVICES_SYNC: @MOZ_SERVICES_SYNC_BOOL@,

  MOZ_DATA_REPORTING: @MOZ_DATA_REPORTING_BOOL@,

  MOZ_SANDBOX: @MOZ_SANDBOX_BOOL@,

  MOZ_TELEMETRY_REPORTING:
#ifdef MOZ_TELEMETRY_REPORTING
  true,
#else
  false,
#endif

  MOZ_UPDATER: @MOZ_UPDATER_BOOL@,

  MOZ_WEBRTC: @MOZ_WEBRTC_BOOL@,

  MOZ_WIDGET_GTK:
#ifdef MOZ_WIDGET_GTK
  true,
#else
  false,
#endif

  MOZ_WMF_CDM: @MOZ_WMF_CDM_BOOL@,

  XP_UNIX:
#ifdef XP_UNIX
  true,
#else
  false,
#endif

// NOTE! XP_LINUX has to go after MOZ_WIDGET_ANDROID otherwise Android
// builds will be misidentified as linux.
  platform:
#ifdef MOZ_WIDGET_GTK
  "linux",
#elif XP_WIN
  "win",
#elif XP_MACOSX
  "macosx",
#elif XP_IOS
  "ios",
#elif MOZ_WIDGET_ANDROID
  "android",
#elif XP_LINUX
  "linux",
#else
  "other",
#endif

// Most of our frontend code assumes that any desktop Unix platform
// is "linux". Add the distinction for code that needs it.
  unixstyle:
#ifdef XP_LINUX
    "linux",
#elif XP_OPENBSD
    "openbsd",
#elif XP_NETBSD
    "netbsd",
#elif XP_FREEBSD
    "freebsd",
#elif XP_SOLARIS
    "solaris",
#else
    "other",
#endif

  isPlatformAndVersionAtLeast(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) >= 0;
  },

  isPlatformAndVersionAtMost(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) <= 0;
  },

  MOZ_CRASHREPORTER: @MOZ_CRASHREPORTER_BOOL@,

  MOZ_NORMANDY: @MOZ_NORMANDY_BOOL@,

  MOZ_MAINTENANCE_SERVICE: @MOZ_MAINTENANCE_SERVICE_BOOL@,

  MOZ_BACKGROUNDTASKS: @MOZ_BACKGROUNDTASKS_BOOL@,

  MOZ_UPDATE_AGENT: @MOZ_UPDATE_AGENT_BOOL@,

  MOZ_BITS_DOWNLOAD: @MOZ_BITS_DOWNLOAD_BOOL@,

  DEBUG: @MOZ_DEBUG_BOOL@,

  ASAN: @MOZ_ASAN_BOOL@,

  ASAN_REPORTER: @MOZ_ASAN_REPORTER_BOOL@,

  TSAN: @MOZ_TSAN_BOOL@,

  MOZ_SYSTEM_NSS: @MOZ_SYSTEM_NSS_BOOL@,

  MOZ_PLACES: @MOZ_PLACES_BOOL@,

  MOZ_REQUIRE_SIGNING: @MOZ_REQUIRE_SIGNING_BOOL@,

  MOZ_UNSIGNED_APP_SCOPE: @MOZ_UNSIGNED_APP_SCOPE_BOOL@,

  MOZ_UNSIGNED_SYSTEM_SCOPE: @MOZ_UNSIGNED_SYSTEM_SCOPE_BOOL@,

  MOZ_ALLOW_ADDON_SIDELOAD: @MOZ_ALLOW_ADDON_SIDELOAD_BOOL@,

  MOZ_WEBEXT_WEBIDL_ENABLED: @MOZ_WEBEXT_WEBIDL_ENABLED_BOOL@,

  MOZ_GECKOVIEW_HISTORY: @MOZ_GECKOVIEW_HISTORY_BOOL@,

  DLL_PREFIX: "@DLL_PREFIX@",
  DLL_SUFFIX: "@DLL_SUFFIX@",

  MOZ_APP_NAME: "@MOZ_APP_NAME@",
  MOZ_APP_BASENAME: "@MOZ_APP_BASENAME@",
  // N.b.: you almost certainly want brandShortName/brand-short-name:
  // MOZ_APP_DISPLAYNAME should only be used for static user-visible
  // fields (e.g., DLL properties, Mac Bundle name, or similar).
  MOZ_APP_DISPLAYNAME_DO_NOT_USE: "@MOZ_APP_DISPLAYNAME@",
  MOZ_APP_VERSION: "@MOZ_APP_VERSION@",
  MOZ_APP_VERSION_DISPLAY: "@MOZ_APP_VERSION_DISPLAY@",
  MOZ_BUILDID: "@MOZ_BUILDID@",
  MOZ_BUILD_APP: "@MOZ_BUILD_APP@",
  MOZ_MACBUNDLE_ID: "@MOZ_MACBUNDLE_ID@",
  MOZ_MACBUNDLE_NAME: "@MOZ_MACBUNDLE_NAME@",
  MOZ_UPDATE_CHANNEL: "@MOZ_UPDATE_CHANNEL@",
  MOZ_WIDGET_TOOLKIT: "@MOZ_WIDGET_TOOLKIT@",

  DEBUG_JS_MODULES: "@DEBUG_JS_MODULES@",

  MOZ_BING_API_CLIENTID: "@MOZ_BING_API_CLIENTID@",
  MOZ_BING_API_KEY: "@MOZ_BING_API_KEY@",
#if !defined(MOZ_WIDGET_ANDROID) && !defined(XP_WIN)
  MOZ_GOOGLE_LOCATION_SERVICE_API_KEY: "@MOZ_GOOGLE_LOCATION_SERVICE_API_KEY@",
#endif
  MOZ_GOOGLE_SAFEBROWSING_API_KEY: "@MOZ_GOOGLE_SAFEBROWSING_API_KEY@",
  MOZ_MOZILLA_API_KEY: "@MOZ_MOZILLA_API_KEY@",

  BROWSER_CHROME_URL: "@BROWSER_CHROME_URL@",

  OMNIJAR_NAME: "@OMNIJAR_NAME@",

  // URL to the hg revision this was built from (e.g.
  // "https://hg.mozilla.org/mozilla-central/rev/6256ec9113c1")
  // On unofficial builds, this is an empty string.
#ifndef MOZ_SOURCE_URL
#define MOZ_SOURCE_URL
#endif
  SOURCE_REVISION_URL: "@MOZ_SOURCE_URL@",

  HAVE_USR_LIB64_DIR:
#ifdef HAVE_USR_LIB64_DIR
    true,
#else
    false,
#endif

  HAVE_SHELL_SERVICE: @HAVE_SHELL_SERVICE_BOOL@,

  MOZ_CODE_COVERAGE: @MOZ_CODE_COVERAGE_BOOL@,

  TELEMETRY_PING_FORMAT_VERSION: @TELEMETRY_PING_FORMAT_VERSION@,

  ENABLE_WEBDRIVER: @ENABLE_WEBDRIVER_BOOL@,

  REMOTE_SETTINGS_SERVER_URLS:
#ifdef MOZ_THUNDERBIRD
    [ "https://thunderbird-settings.thunderbird.net/v1" ],
#else
    [ "https://firefox.settings.services.mozilla.com/v1", "https://firefox.settings.services.mozilla.com/v2" ],
#endif

  REMOTE_SETTINGS_VERIFY_SIGNATURE:
#ifdef MOZ_THUNDERBIRD
    false,
#else
    true,
#endif

  REMOTE_SETTINGS_DEFAULT_BUCKET:
#ifdef MOZ_THUNDERBIRD
    "thunderbird",
#else
    "main",
#endif

  MOZ_GLEAN_ANDROID: @MOZ_GLEAN_ANDROID_BOOL@,

  MOZ_JXL: @MOZ_JXL_BOOL@,

#if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
  MOZ_CAN_FOLLOW_SYSTEM_TIME:
#ifdef XP_WIN
    true,
#elif XP_MACOSX
    true,
#elif MOZ_WIDGET_GTK
  #ifdef MOZ_ENABLE_DBUS
    true,
  #else
    false,
  #endif
#else
    false,
#endif
#endif

  MOZ_SYSTEM_POLICIES: @MOZ_SYSTEM_POLICIES_BOOL@,

  MOZ_SELECTABLE_PROFILES: @MOZ_SELECTABLE_PROFILES_BOOL@,

  SQLITE_LIBRARY_FILENAME:
#ifdef MOZ_FOLD_LIBS
  "@DLL_PREFIX@nss3@DLL_SUFFIX@",
#else
  "@DLL_PREFIX@mozsqlite3@DLL_SUFFIX@",
#endif

  MOZ_GECKOVIEW:
#ifdef MOZ_GECKOVIEW
    true,
#else
    false,
#endif

#ifdef MOZ_WIDGET_ANDROID
  MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_PROCESS: @MOZ_ANDROID_CONTENT_SERVICE_ISOLATED_PROCESS_BOOL@,
#endif

  USE_LIBZ_RS: @USE_LIBZ_RS_BOOL@,
});
