/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// AppConstants, and overrides importESModule to be a no-op (which
// can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTabParent: "resource:///actors/AboutNewTabParent.sys.mjs",
  AboutPreferences: "resource://newtab/lib/AboutPreferences.sys.mjs",
  AdsFeed: "resource://newtab/lib/AdsFeed.sys.mjs",
  InferredPersonalizationFeed:
    "resource://newtab/lib/InferredPersonalizationFeed.sys.mjs",
  SmartShortcutsFeed: "resource://newtab/lib/SmartShortcutsFeed.sys.mjs",
  DEFAULT_SITES: "resource://newtab/lib/DefaultSites.sys.mjs",
  DefaultPrefs: "resource://newtab/lib/ActivityStreamPrefs.sys.mjs",
  DiscoveryStreamFeed: "resource://newtab/lib/DiscoveryStreamFeed.sys.mjs",
  ExternalComponentsFeed:
    "resource://newtab/lib/ExternalComponentsFeed.sys.mjs",
  FaviconFeed: "resource://newtab/lib/FaviconFeed.sys.mjs",
  HighlightsFeed: "resource://newtab/lib/HighlightsFeed.sys.mjs",
  ListsFeed: "resource://newtab/lib/Widgets/ListsFeed.sys.mjs",
  NewTabAttributionFeed: "resource://newtab/lib/NewTabAttributionFeed.sys.mjs",
  NewTabActorRegistry: "resource://newtab/lib/NewTabActorRegistry.sys.mjs",
  NewTabInit: "resource://newtab/lib/NewTabInit.sys.mjs",
  NewTabMessaging: "resource://newtab/lib/NewTabMessaging.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefsFeed: "resource://newtab/lib/PrefsFeed.sys.mjs",
  PlacesFeed: "resource://newtab/lib/PlacesFeed.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteRenderer: "resource://newtab/lib/RemoteRenderer.sys.mjs",
  SectionsFeed: "resource://newtab/lib/SectionsManager.sys.mjs",
  SectionsLayoutFeed: "resource://newtab/lib/SectionsLayoutFeed.sys.mjs",
  SportsFeed: "resource://newtab/lib/Widgets/SportsFeed.sys.mjs",
  StartupCacheInit: "resource://newtab/lib/StartupCacheInit.sys.mjs",
  Store: "resource://newtab/lib/Store.sys.mjs",
  SystemTickFeed: "resource://newtab/lib/SystemTickFeed.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TimerFeed: "resource://newtab/lib/Widgets/TimerFeed.sys.mjs",
  TopSitesFeed: "resource://newtab/lib/TopSitesFeed.sys.mjs",
  TopStoriesFeed: "resource://newtab/lib/TopStoriesFeed.sys.mjs",
  WallpaperFeed: "resource://newtab/lib/Wallpapers/WallpaperFeed.sys.mjs",
  WeatherFeed: "resource://newtab/lib/WeatherFeed.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ProxyService",
  "@mozilla.org/network/protocol-proxy-service;1",
  Ci.nsIProtocolProxyService
);

// NB: Eagerly load modules that will be loaded/constructed/initialized in the
// common case to avoid the overhead of wrapping and detecting lazy loading.
import {
  actionCreators as ac,
  actionTypes as at,
} from "resource://newtab/common/Actions.mjs";

const REGION_INFERRED_PERSONALIZATION_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.region-config";
const LOCALE_INFERRED_PERSONALIZATION_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.locale-config";
const REGION_SOV_CONFIG =
  "browser.newtabpage.activity-stream.sov.region-config";
const LOCALE_SOV_CONFIG =
  "browser.newtabpage.activity-stream.sov.locale-config";

const REGION_WEATHER_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.region-weather-config";
const LOCALE_WEATHER_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.locale-weather-config";

const REGION_TOPICS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.topicSelection.region-topics-config";
const LOCALE_TOPICS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.topicSelection.locale-topics-config";

const REGION_TOPIC_LABEL_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.topicLabels.region-topic-label-config";
const LOCALE_TOPIC_LABEL_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.topicLabels.locale-topic-label-config";
const REGION_BASIC_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.region-basic-config";

const REGION_CONTEXTUAL_AD_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.contextualAds.region-config";
const LOCALE_CONTEXTUAL_AD_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.contextualAds.locale-config";

const REGION_SECTIONS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.region-content-config";
const LOCALE_SECTIONS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.locale-content-config";

const PREF_SHOULD_AS_INITIALIZE_FEEDS =
  "browser.newtabpage.activity-stream.testing.shouldInitializeFeeds";

const PREF_INFERRED_ENABLED =
  "discoverystream.sections.personalization.inferred.enabled";

const PREF_IMAGE_PROXY_ENABLED =
  "browser.newtabpage.activity-stream.discoverystream.imageProxy.enabled";

const PREF_IMAGE_PROXY_ENABLED_STORE = "discoverystream.imageProxy.enabled";

const PREF_NEWTAB_REMOTE_RENDERER_ENABLED =
  "browser.newtabpage.activity-stream.remote-renderer.enabled";

export const PREF_DEFAULT_VALUE_TOPSITES_ENABLED = true;
export const PREF_DEFAULT_VALUE_TOPSTORIES_ENABLED = true;

export const WEATHER_OPTIN_REGIONS = [
  "AT", // Austria
  "BE", // Belgium
  "BG", // Bulgaria
  "HR", // Croatia
  "CY", // Cyprus
  "CZ", // Czechia
  "DK", // Denmark
  "EE", // Estonia
  "FI", // Finland
  "FR", // France
  "DE", // Germany
  "GB", // United Kingdom
  "GR", // Greece
  "HU", // Hungary
  "IS", // Iceland
  "IE", // Ireland
  "IT", // Italy
  "LV", // Latvia
  "LI", // Liechtenstein
  "LT", // Lithuania
  "MT", // Malta
  "NL", // Netherlands
  "NO", // Norway
  "PL", // Poland
  "PT", // Portugal
  "RO", // Romania
  "SG", // Singapore
  "SK", // Slovakia
  "SI", // Slovenia
  "ES", // Spain
  "SE", // Sweden
  "CH", // Switzerland
];

export function csvPrefHasValue(stringPrefName, value) {
  if (typeof stringPrefName !== "string") {
    throw new Error(`The stringPrefName argument is not a string`);
  }

  const pref = Services.prefs.getStringPref(stringPrefName, "") || "";
  const prefValues = pref
    .split(",")
    .map(s => s.trim())
    .filter(item => item);

  return prefValues.includes(value);
}

export function shouldInitializeFeeds(defaultValue = true) {
  // For tests/automation: when false, newtab won't initialize
  // select feeds in this session.
  // Flipping after initialization has no effect on the current session.
  const shouldInitialize = Services.prefs.getBoolPref(
    PREF_SHOULD_AS_INITIALIZE_FEEDS,
    defaultValue
  );
  return shouldInitialize;
}

function useInferredPersonalization({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_INFERRED_PERSONALIZATION_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_INFERRED_PERSONALIZATION_CONFIG, locale)
  );
}

function useSov({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_SOV_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_SOV_CONFIG, locale)
  );
}

function useContextualAds({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_CONTEXTUAL_AD_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_CONTEXTUAL_AD_CONFIG, locale)
  );
}

// Determine if spocs should be shown for a geo/locale
function showSpocs({ geo }) {
  const spocsGeoString =
    lazy.NimbusFeatures.pocketNewtab.getVariable("regionSpocsConfig") || "";
  const spocsGeo = spocsGeoString.split(",").map(s => s.trim());
  return spocsGeo.includes(geo);
}

function showWeather({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_WEATHER_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_WEATHER_CONFIG, locale)
  );
}

/**
 * Returns the default size for widgets that support large/medium sizes.
 * This sets a default pref, not a user pref — if the user has explicitly
 * resized a widget via the UI, their choice takes precedence.
 *
 * In the future this will follow the same regional logic as showWeather,
 * returning different defaults based on the user's region.
 */
function getDefaultWidgetSize() {
  return Services.prefs.getStringPref(
    "browser.newtabpage.activity-stream.widgets.defaultSize",
    "large"
  );
}

/**
 * Determines the default size for the consolidated weather widget by inferring
 * what the user previously had visible before Nova was enabled. This runs at
 * startup so that existing users are migrated to the correct size without any
 * explicit one-time migration step.
 *
 * This sets a default pref, not a user pref. Users who change their size via
 * the UI are fully migrated (their choice becomes a user pref — see the
 * sentinel approach documented in WidgetsRegistry.mjs). Users who never touch
 * the UI remain dependent on this function at every startup.
 *
 * widgets.weather.size uses getValue here instead of value: "" (the approach
 * used by other widget size prefs) because the correct initial value depends
 * on the user's prior weather configuration and cannot be a static default.
 *
 * - No forecast system pref → user had the classic weather widget → "small" (sidebar)
 * - Forecast enabled + display !== "detailed" → user switched to simple weather → "small"
 * - Forecast enabled + maximized → user had the large forecast widget → "large"
 * - Forecast enabled + not maximized → user had the medium forecast widget → "medium"
 */
// @nova-cleanup(remove-pref): Replace this function with a _migratePref call
// that writes the computed size as a user pref for widgets.weather.size, then
// change widgets.weather.size in PREFS_CONFIG to value: "" (consistent with
// other widget size prefs; new users fall through to defaultSize in the registry).
function getWeatherWidgetSize() {
  const forecastSystemEnabled = Services.prefs.getBoolPref(
    "browser.newtabpage.activity-stream.widgets.system.weatherForecast.enabled",
    false
  );
  if (!forecastSystemEnabled) {
    return "small";
  }
  const weatherDisplay = Services.prefs.getStringPref(
    "browser.newtabpage.activity-stream.weather.display",
    "detailed"
  );
  if (weatherDisplay !== "detailed") {
    return "small";
  }
  const maximized = Services.prefs.getBoolPref(
    "browser.newtabpage.activity-stream.widgets.maximized",
    true
  );
  return maximized ? getDefaultWidgetSize() : "medium";
}

function showWeatherOptIn({ geo }) {
  return WEATHER_OPTIN_REGIONS.includes(geo);
}

function showTopicsSelection({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_TOPICS_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_TOPICS_CONFIG, locale)
  );
}

function showTopicLabels({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_TOPIC_LABEL_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_TOPIC_LABEL_CONFIG, locale)
  );
}

function showSectionLayout({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_SECTIONS_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_SECTIONS_CONFIG, locale)
  );
}

// Configure default Activity Stream prefs with a plain `value` or a `getValue`
// that computes a value. A `value_local_dev` is used for development defaults.
export const PREFS_CONFIG = new Map([
  [
    "default.sites",
    {
      title:
        "Comma-separated list of default top sites to fill in behind visited sites",
      getValue: ({ geo }) =>
        lazy.DEFAULT_SITES.get(lazy.DEFAULT_SITES.has(geo) ? geo : ""),
    },
  ],
  [
    "feeds.topsites",
    {
      title: "Displays Top Sites on the New Tab Page",
      value: PREF_DEFAULT_VALUE_TOPSITES_ENABLED,
    },
  ],
  [
    "hideTopSitesTitle",
    {
      title:
        "Hide the top sites section's title, including the section and collapse icons",
      value: false,
    },
  ],
  [
    "showSponsored",
    {
      title: "User pref for sponsored Pocket content",
      value: true,
    },
  ],
  [
    "system.showSponsored",
    {
      title: "System pref for sponsored Pocket content",
      // This pref is dynamic as the sponsored content depends on the region
      getValue: showSpocs,
    },
  ],
  [
    "showSponsoredTopSites",
    {
      title: "Show sponsored top sites",
      value: true,
    },
  ],
  [
    "mobileDownloadModal.enabled",
    {
      title: "Boolean flag to show download Firefox for mobile QR code modal",
      value: false,
    },
  ],
  [
    "mobileDownloadModal.variant-a",
    {
      title:
        "Boolean flag to turn download Firefox for mobile promo variant A on and off",
      value: false,
    },
  ],
  [
    "mobileDownloadModal.variant-b",
    {
      title:
        "Boolean flag to turn download Firefox for mobile promo variant B on and off",
      value: false,
    },
  ],
  [
    "mobileDownloadModal.variant-c",
    {
      title:
        "Boolean flag to turn download Firefox for mobile promo variant C on and off",
      value: false,
    },
  ],
  [
    "discoverystream.merino-provider.ohttp.enabled",
    {
      title: "Enables the Merino requests and images sent over OHTTP",
      value: false,
    },
  ],
  [
    "unifiedAds.ohttp.enabled",
    {
      title: "Enables the MARS requests and images sent over OHTTP",
      value: false,
    },
  ],
  [
    "unifiedAds.adsFeed.enabled",
    {
      title:
        "Use AdsFeed.sys.mjs to fetch/cache/serve Mozilla Ad Routing Service (MARS) unified ads ",
      value: false,
    },
  ],
  [
    "unifiedAds.tiles.enabled",
    {
      title:
        "Use Mozilla Ad Routing Service (MARS) unified ads API for sponsored top sites tiles",
      value: false,
    },
  ],
  [
    "unifiedAds.spocs.enabled",
    {
      title:
        "Use Mozilla Ad Routing Service (MARS) unified ads API for sponsored content in recommended stories",
      value: false,
    },
  ],
  [
    "unifiedAds.endpoint",
    {
      title: "Mozilla Ad Routing Service (MARS) unified ads API endpoint URL",
      value: "https://ads.mozilla.org/",
    },
  ],
  [
    "unifiedAds.blockedAds",
    {
      title:
        "CSV list of blocked (dismissed) MARS ads. This payload is sent back every time new ads are fetched.",
      value: "",
    },
  ],
  [
    "system.showWeather",
    {
      title: "system.showWeather",
      // pref is dynamic
      getValue: showWeather,
    },
  ],
  [
    "showWeather",
    {
      title: "showWeather",
      value: true,
    },
  ],
  [
    "system.showWeatherOptIn",
    {
      title: "system.showWeatherOptIn",
      // pref is dynamic
      getValue: showWeatherOptIn,
    },
  ],
  [
    "discoverystream.optIn-region-weather-config",
    {
      title: "Regions for weather opt-in.",
      value: "DE,GB,FR,ES,IT,CH,AT,BE,IE,NL,PL,CZ,SE,SG,HU,SK,FI,DK,NO,PT",
    },
  ],
  [
    "weather.optInDisplayed",
    {
      title:
        "Enable opt-in dialog to display for weather widget in GDPR regions.",
      value: true,
    },
  ],
  [
    "weather.optInAccepted",
    {
      title:
        "User choice made when prompted with the opt-in dialog for weather.",
      value: false,
    },
  ],
  [
    "weather.staticData.enabled",
    {
      title:
        "Static weather data shown when user has not set/enabled location from opt-in.",
      value: true,
    },
  ],
  [
    "weather.query",
    {
      title: "weather.query",
      value: "",
    },
  ],
  [
    "weather.locationSearchEnabled",
    {
      title: "Enable the option to search for a specific city",
      value: false,
    },
  ],
  [
    "weather.temperatureUnits",
    {
      title: "Switch the temperature between Celsius and Fahrenheit",
      getValue: ({ geo }) => (geo === "US" ? "f" : "c"),
    },
  ],
  [
    "weather.display",
    {
      title:
        "Toggle the weather widget to include a text summary of the current conditions",
      value: "detailed",
    },
  ],
  [
    "weather.reportEndpoint",
    {
      title:
        "Temporary measure for trainhopping. This adds the Merino endpoint for the weather report",
      value: "https://merino.services.mozilla.com/api/v1/suggest",
    },
  ],
  [
    "weather.hourlyEndpoint",
    {
      title:
        "Temporary measure for trainhopping. This adds the Merino endpoint for the hourly forecasts to display in Weather Forecast widget",
      value:
        "https://merino.services.mozilla.com/api/v1/weather/hourly-forecasts",
    },
  ],
  [
    "images.smart",
    {
      title: "Smart crop images on newtab",
      value: false,
    },
  ],
  [
    "pocketCta",
    {
      title: "Pocket cta and button for logged out users.",
      value: JSON.stringify({
        cta_button: "",
        cta_text: "",
        cta_url: "",
        use_cta: false,
      }),
    },
  ],
  [
    "showSearch",
    {
      title: "Show the Search bar",
      value: true,
    },
  ],
  [
    "logowordmark.alwaysVisible",
    {
      title: "Show the logo and wordmark",
      value: true,
    },
  ],
  [
    "topSitesRows",
    {
      title: "Number of rows of Top Sites to display",
      value: 1,
    },
  ],
  [
    "topSitesMaxSitesPerRow",
    {
      title: "Max number of Top Sites to display per row",
      value: 8,
    },
  ],
  [
    "telemetry",
    {
      title: "Enable system error and usage data collection",
      value: true,
      value_local_dev: false,
    },
  ],
  [
    "telemetry.ut.events",
    {
      title: "Enable Unified Telemetry event data collection",
      value: AppConstants.EARLY_BETA_OR_EARLIER,
      value_local_dev: false,
    },
  ],
  [
    "telemetry.structuredIngestion.endpoint",
    {
      title: "Structured Ingestion telemetry server endpoint",
      value: "https://incoming.telemetry.mozilla.org/submit",
    },
  ],
  [
    "telemetry.privatePing.enabled",
    {
      title: "Enables the private ping sent over OHTTP through Glean",
      value: false,
    },
  ],
  [
    "telemetry.surfaceId",
    {
      title: "surface id",
    },
  ],
  [
    "telemetry.privatePing.redactNewtabPing.enabled",
    {
      title: "Redacts content interaction ids from original New Tab ping",
      value: false,
    },
  ],
  [
    "telemetry.privatePing.inferredInterests.enabled",
    {
      title:
        "Includes interest vector with private ping when user has enabeled inferred personalization",
      value: false,
    },
  ],
  [
    "section.highlights.includeVisited",
    {
      title:
        "Boolean flag that decides whether or not to show visited pages in highlights.",
      value: true,
    },
  ],
  [
    "section.highlights.includeBookmarks",
    {
      title:
        "Boolean flag that decides whether or not to show bookmarks in highlights.",
      value: true,
    },
  ],
  [
    "section.highlights.includeDownloads",
    {
      title:
        "Boolean flag that decides whether or not to show saved recent Downloads in highlights.",
      value: true,
    },
  ],
  [
    "section.highlights.rows",
    {
      title: "Number of rows of Highlights to display",
      value: 1,
    },
  ],
  [
    "feeds.section.topstories",
    {
      title: "Whether top stories are enabled by default.",
      value: PREF_DEFAULT_VALUE_TOPSTORIES_ENABLED,
    },
  ],
  [
    "section.topstories.rows",
    {
      title: "Number of rows of Top Stories to display",
      value: 1,
    },
  ],
  [
    "sectionOrder",
    {
      title: "The rendering order for the sections",
      value: "topsites,topstories,highlights",
    },
  ],
  [
    "newtabWallpapers.enabled",
    {
      title: "Boolean flag to turn wallpaper functionality on and off",
      value: false,
    },
  ],
  [
    "newtabWallpapers.user.enabled",
    {
      title:
        "Boolean flag controlling wallpaper visibility -- if true the user's selected wallpaper is shown, if false it is hidden",
      value: false,
    },
  ],
  [
    "newtabWallpapers.customColor.enabled",
    {
      title: "Boolean flag to turn show custom color select box",
      value: false,
    },
  ],
  [
    "newtabWallpapers.customWallpaper.enabled",
    {
      title:
        "Boolean flag to enable custom/user-uploaded wallpaper functionality",
      value: false,
    },
  ],
  [
    "newtabWallpapers.customWallpaper.uuid",
    {
      title: "uuid for uploaded custom wallpaper",
      value: "",
    },
  ],
  [
    "newtabWallpapers.customWallpaper.uploadedPreviously",
    {
      title:
        "Boolean flag used for telemetry to track if a user has previously uploaded a custom wallpaper",
      value: false,
    },
  ],
  [
    "newtabWallpapers.customWallpaper.fileSize.enabled",
    {
      title: "Boolean flag to enforce a maximum file size for uploaded images",
      value: false,
    },
  ],
  [
    "newtabWallpapers.customWallpaper.fileSize",
    {
      title: "Number pref of maximum file size (in MB) a user can upload",
      value: 0,
    },
  ],
  [
    "newtabWallpapers.customWallpaper.theme",
    {
      title: "theme ('light' | 'dark') of user uploaded wallpaper",
      value: "",
    },
  ],
  [
    "newtabAdSize.leaderboard",
    {
      title: "Boolean flag to turn the leaderboard ad size on and off",
      value: false,
    },
  ],
  [
    "newtabAdSize.leaderboard.position",
    {
      title:
        "position for leaderboard spoc - should correlate to a row in DS grid",
      value: "3",
    },
  ],
  [
    "newtabAdSize.billboard",
    {
      title: "Boolean flag to turn the billboard ad size on and off",
      value: false,
    },
  ],
  [
    "newtabAdSize.billboard.position",
    {
      title:
        "position for billboard spoc - should correlate to a row in DS grid",
      value: "3",
    },
  ],
  [
    "newtabAdSize.mediumRectangle",
    {
      title: "Boolean flag to turn the medium (MREC) ad size on and off",
      value: false,
    },
  ],
  [
    "discoverystream.promoCard.enabled",
    {
      title: "Boolean flag to turn the promo card on and off",
      value: false,
    },
  ],
  [
    "discoverystream.promoCard.visible",
    {
      title: "Boolean flag whether the promo card is visible or not",
      value: false,
    },
  ],
  [
    "discoverystream.sections.enabled",
    {
      title: "Boolean flag to enable section layout UI in recommended stories",
      getValue: showSectionLayout,
    },
  ],
  [
    "discoverystream.sections.personalization.enabled",
    {
      title:
        "Boolean flag to enable personalized sections layout. Allows users to follow/unfollow topic sections.",
      value: true,
    },
  ],
  [
    "discoverystream.sections.customizeMenuPanel.enabled",
    {
      title:
        "Boolean flag to enable the setions management panel in Customize menu",
      value: true,
    },
  ],
  [
    "discoverystream.sections.cards.enabled",
    {
      title:
        "Boolean flag to enable revised pocket story card UI in recommended stories",
      value: false,
    },
  ],
  [
    "discoverystream.sections.contextualAds.enabled",
    {
      title: "Boolean flag to enable contextual ads",
      getValue: useContextualAds,
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.enabled",
    {
      title: "Boolean flag to enable inferred personalizaton",
      // pref is dynamic
      getValue: useInferredPersonalization,
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.interests.override",
    {
      title:
        "Testing feature to allow specification of specific user interests",
    },
  ],
  [
    "discoverystream.dailyBrief.sectionId",
    {
      title: "sectionId for the Daily brief section",
      value: "top_stories_section",
    },
  ],
  [
    "discoverystream.dailyBrief.enabled",
    {
      title: "Boolean flag to enable daily briefing",
      value: false,
    },
  ],
  [
    "discoverystream.sections.layout",
    {
      title: "CSV string of section layouts configs",
      value: "7-double-row-2-ad",
    },
  ],
  [
    "discoverystream.shortcuts.personalization.enabled",
    {
      title: "Boolean flag to enable shortcuts personalization",
      value: true,
    },
  ],
  [
    "discoverystream.shortcuts.force_log.enabled",
    {
      title:
        "Boolean flag to enable logging shortcuts interactions even if enabled is off",
      value: false,
    },
  ],
  [
    "discoverystream.attribution.enabled",
    {
      title: "Boolean flag to enable newtab attribution",
      value: false,
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.user.enabled",
    {
      title: "User pref to toggle inferred personalizaton",
      value: false,
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.model.override",
    {
      title:
        "Override inferred personalization model JSON string that typically comes from rec API. Or 'TEST' for a test model",
    },
  ],
  [
    "discoverystream.reportAds.enabled",
    {
      title: "Boolean flag to enable reporting ads from the context menu",
      value: false,
    },
  ],
  [
    "discoverystream.sections.following",
    {
      title: "A comma-separated list of strings of followed section topics",
      value: "",
    },
  ],
  [
    "discoverystream.sections.blocked",
    {
      title: "A comma-separated list of strings of blocked section topics",
      value: "",
    },
  ],
  [
    "discoverystream.sections.interestPicker.enabled",
    {
      title: "Boolean flag to enable the inline interest picker",
      value: false,
    },
  ],
  [
    "discoverystream.sections.interestPicker.visibleSections",
    {
      title: "comma separated string of sections that are visible",
      value: "",
    },
  ],
  [
    "discoverystream.spoc-positions",
    {
      title: "CSV string of spoc position indexes on newtab Pocket grid",
      value: "1,5,7,11,18,20",
    },
  ],
  [
    "discoverystream.placements.contextualSpocs",
    {
      title:
        "CSV string of spoc placement ids on newtab Pocket grid. A placement id tells our ad server where the ads are intended to be displayed.",
    },
  ],
  [
    "discoverystream.placements.contextualSpocs.counts",
    {
      title:
        "CSV string of spoc placement counts on newtab Pocket grid. The count tells the ad server how many ads to return for this position and placement.",
    },
  ],
  [
    "discoverystream.placements.contextualBanners",
    {
      title:
        "CSV string of the banner placement ids on newtab Pocket grid. This placement id tells us which banner is visible when contexual ads are on",
      value: "",
    },
  ],
  [
    "discoverystream.placements.contextualBanners.counts",
    {
      title:
        "CSV string of AdBanner placement counts on newtab Pocket grid. The count tells the ad server how many banners to return for this position and placement.",
      value: "",
    },
  ],
  [
    "discoverystream.placements.spocs",
    {
      title:
        "CSV string of spoc placement ids on newtab Pocket grid. A placement id tells our ad server where the ads are intended to be displayed.",
    },
  ],
  [
    "discoverystream.placements.spocs.counts",
    {
      title:
        "CSV string of spoc placement counts on newtab Pocket grid. The count tells the ad server how many ads to return for this position and placement.",
    },
  ],
  [
    "discoverystream.placements.tiles",
    {
      title:
        "CSV string of tiles placement ids on newtab tiles section. A placement id tells our ad server where the ads are intended to be displayed.",
    },
  ],
  [
    "discoverystream.placements.tiles.counts",
    {
      title:
        "CSV string of tiles placement counts on newtab tiles section. The count tells the ad server how many ads to return for this position and placement.",
    },
  ],
  [
    "discoverystream.imageProxy.enabled",
    {
      title: "Boolean flag to enable image proxying for images on newtab",
      value: false,
    },
  ],
  [
    "newtabWallpapers.highlightEnabled",
    {
      title: "Boolean flag to show the highlight about the Wallpaper feature",
      value: false,
    },
  ],
  [
    "newtabWallpapers.highlightDismissed",
    {
      title:
        "Boolean flag to remember if the user has seen the feature highlight",
      value: false,
    },
  ],
  [
    "newtabWallpapers.highlightSeenCounter",
    {
      title: "Count the number of times a user has seen the feature highlight",
      value: 0,
    },
  ],
  [
    "newtabWallpapers.highlightHeaderText",
    {
      title: "Changes the wallpaper feature highlight header text",
      value: "",
    },
  ],
  [
    "newtabWallpapers.highlightContentText",
    {
      title: "Changes the wallpaper feature highlight content text",
      value: "",
    },
  ],
  [
    "newtabWallpapers.highlightCtaText",
    {
      title: "Changes the wallpaper feature highlight cta text",
      value: "",
    },
  ],
  [
    "newtabWallpapers.wallpaper",
    {
      title: "Currently set wallpaper",
      value: "",
    },
  ],
  [
    "newtabWallpapers.initialWallpaper",
    {
      title:
        "Initial wallpaper set by a Nimbus experiment. Persists after experiment ends.",
      value: "",
    },
  ],
  [
    "sov.enabled",
    {
      title: "Enables share of voice (SOV)",
      getValue: useSov,
    },
  ],
  [
    "sov.name",
    {
      title:
        "A unique id, usually this is a timestamp for the day it was generated",
      value: "SOV-20251122215625",
    },
  ],
  [
    "sov.frecency.exposure",
    {
      title:
        "Is or was the user eligible for frecency ranked sponsored shortcuts",
      value: false,
    },
  ],
  [
    "sov.amp.allocation",
    {
      title: "How many positions can be filled from amp",
      value: "100, 100, 100",
    },
  ],
  [
    "sov.frecency.allocation",
    {
      title: "How many positions can be filled by frecency",
      value: "0, 0, 0",
    },
  ],
  [
    "widgets.system.enabled",
    {
      title: "Enables visibility of all widgets and controls to enable them",
      value: false,
    },
  ],
  [
    "widgets.enabled",
    {
      title: "Allows users to toggle all widgets on and off at once",
      value: true,
    },
  ],
  [
    "widgets.lists.enabled",
    {
      title: "Enables the to-do lists widget",
      value: true,
    },
  ],
  [
    "widgets.lists.maxLists",
    {
      title: "Maximum number of lists that can be created",
      value: 10,
    },
  ],
  [
    "widgets.lists.maxListItems",
    {
      title:
        "Maximum number of items that can be created on an individual list",
      value: 100,
    },
  ],
  [
    "widgets.system.lists.enabled",
    {
      title: "Enables the to-do lists widget experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.lists.interaction",
    {
      title:
        "Boolean flag for determining if a user has interacted with the lists widget",
      value: false,
    },
  ],
  [
    "widgets.lists.badge.enabled",
    {
      title: "Show badge on lists widget to indicate new/beta feature",
      value: false,
    },
  ],
  [
    "widgets.lists.badge.label",
    {
      title: "Label type for lists widget badge (New or Beta)",
      value: "",
    },
  ],
  [
    "widgets.maximized",
    {
      title:
        "Toggles maximized state for all widgets in the widgets section. It defaults to true as the default widget size is large",
      value: true,
    },
  ],
  [
    "widgets.system.maximized",
    {
      title: "Enables the maximize widget feature experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.focusTimer.enabled",
    {
      title: "Enables the focus timer widget",
      value: true,
    },
  ],
  [
    "widgets.system.focusTimer.enabled",
    {
      title: "Enables the focus timer widget experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.focusTimer.interaction",
    {
      title:
        "Boolean flag for determining if a user has interacted with the timer widget",
      value: false,
    },
  ],
  [
    "widgets.focusTimer.showSystemNotifications",
    {
      title: "Enables the focus timer widget to show system notifications",
      value: false,
    },
  ],
  [
    "widgets.weatherForecast.enabled",
    {
      title: "Enables the weather forecast widget",
      value: true,
    },
  ],
  [
    "widgets.weather.enabled",
    {
      title: "Enables the weather widget",
      value: true,
    },
  ],
  [
    "widgets.system.weather.enabled",
    {
      title: "Enables the weather widget experiment in Nimbus",
      getValue: showWeather,
    },
  ],
  [
    "widgets.system.weatherForecast.enabled",
    {
      title: "Enables the weather forecast widget experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.weatherForecast.interaction",
    {
      title:
        "Boolean flag for determining if a user has interacted with the weather forecast widget",
      value: false,
    },
  ],
  [
    "widgets.weather.size",
    {
      title: "Size of the weather forecast widget (small, medium, or large)",
      getValue: getWeatherWidgetSize,
    },
  ],
  [
    "widgets.clocks.enabled",
    {
      title: "Enables the clock widget",
      value: true,
    },
  ],
  [
    "widgets.system.clocks.enabled",
    {
      title: "Enables the clock widget experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.defaultSize",
    {
      title: "Default size for widgets (medium or large)",
      value: "large",
    },
  ],
  [
    "widgets.lists.size",
    {
      title: "Size of the lists widget (medium or large)",
      value: "",
    },
  ],
  [
    "widgets.focusTimer.size",
    {
      title: "Size of the focus timer widget (medium or large)",
      value: "",
    },
  ],
  [
    "widgets.sportsWidget.enabled",
    {
      title: "Enables the sports widget",
      value: true,
    },
  ],
  [
    "widgets.system.sportsWidget.enabled",
    {
      title: "Enables the sports widget experiment in Nimbus",
      value: false,
    },
  ],
  [
    "widgets.sportsWidget.size",
    {
      title: "Size of the sports widget (medium or large)",
      value: "",
    },
  ],
  [
    "widgets.sportsWidget.live.enabled",
    {
      title: "Enables live scores in the sports widget",
      value: false,
    },
  ],
  [
    "widgets.sportsWidget.interaction",
    {
      title:
        "Boolean flag for determining if a user has interacted with the sports widget",
      value: false,
    },
  ],
  [
    "widgets.clocks.size",
    {
      title: "Size of the clock widget (small, medium, or large)",
      getValue: getDefaultWidgetSize,
    },
  ],
  [
    "widgets.clocks.hourFormat",
    {
      title:
        "User override for clock widget hour format ('12', '24', or empty string to use locale default)",
      value: "",
    },
  ],
  [
    "widgets.clocks.zones",
    {
      title: "Saved clock widget time zones",
      value: "",
    },
  ],
  [
    "widgets.feedback.enabled",
    {
      title: "Enables the feedback link in the widgets container",
      value: false,
    },
  ],
  [
    "widgets.hideAllToast.enabled",
    {
      title: "Shows a toast when all widgets are hidden via the X button",
      value: false,
    },
  ],
  [
    "widgets.order",
    {
      title:
        "Widget display order as a comma-separated list of widget IDs. Empty string means use the default registry order.",
      value: "",
    },
  ],
  [
    "improvesearch.noDefaultSearchTile",
    {
      title: "Remove tiles that are the same as the default search",
      value: true,
    },
  ],
  [
    "improvesearch.topSiteSearchShortcuts.searchEngines",
    {
      title:
        "An ordered, comma-delimited list of search shortcuts that we should try and pin",
      // This pref is dynamic as the shortcuts vary depending on the region
      getValue: ({ geo }) => {
        if (!geo) {
          return "";
        }
        const searchShortcuts = [];
        if (geo === "CN") {
          searchShortcuts.push("baidu");
        } else if (["BY", "KZ", "RU", "TR"].includes(geo)) {
          searchShortcuts.push("yandex");
        } else {
          searchShortcuts.push("google");
        }
        if (["DE", "FR", "GB", "IT", "JP", "US"].includes(geo)) {
          searchShortcuts.push("amazon");
        }
        return searchShortcuts.join(",");
      },
    },
  ],
  [
    "improvesearch.topSiteSearchShortcuts.havePinned",
    {
      title:
        "A comma-delimited list of search shortcuts that have previously been pinned",
      value: "",
    },
  ],
  [
    "asrouter.devtoolsEnabled",
    {
      title: "Are the asrouter devtools enabled?",
      value: false,
    },
  ],
  [
    "discoverystream.flight.blocks",
    {
      title: "Track flight blocks",
      skipBroadcast: true,
      value: "{}",
    },
  ],
  [
    "discoverystream.config",
    {
      title: "Configuration for the new pocket new tab",
      getValue: () => {
        return JSON.stringify({
          collapsible: true,
          enabled: true,
        });
      },
    },
  ],
  [
    "discoverystream.endpoints",
    {
      title:
        "Endpoint prefixes (comma-separated) that are allowed to be requested",
      value:
        "https://getpocket.cdn.mozilla.net/,https://firefox-api-proxy.cdn.mozilla.net/,https://spocs.getpocket.com/,https://merino.services.mozilla.com/,https://ads.mozilla.org/",
    },
  ],
  [
    "discoverystream.region-basic-layout",
    {
      title: "Decision to use basic layout based on region.",
      getValue: ({ geo }) => {
        const preffedRegionsString =
          Services.prefs.getStringPref(REGION_BASIC_CONFIG) || "";
        // If no regions are set to basic,
        // we don't need to bother checking against the region.
        // We are also not concerned if geo is not set,
        // because stories are going to be empty until we have geo.
        if (!preffedRegionsString) {
          return false;
        }
        const preffedRegions = preffedRegionsString
          .split(",")
          .map(s => s.trim());

        return preffedRegions.includes(geo);
      },
    },
  ],
  [
    "discoverystream.spoc.impressions",
    {
      title: "Track spoc impressions",
      skipBroadcast: true,
      value: "{}",
    },
  ],
  [
    "discoverystream.endpointSpocsClear",
    {
      title:
        "Endpoint for when a user opts-out of sponsored content to delete the user's data from the ad server.",
      value: "https://spocs.getpocket.com/user",
    },
  ],
  [
    "discoverystream.rec.impressions",
    {
      title: "Track rec impressions",
      skipBroadcast: true,
      value: "{}",
    },
  ],
  [
    "discoverystream.topicSelection.enabled",
    {
      title: "Enables topic selection for discovery stream",
      // pref is dynamic
      getValue: showTopicsSelection,
    },
  ],
  [
    "discoverystream.topicSelection.topics",
    {
      title: "Topics available",
      value:
        "business, arts, food, health, finance, government, sports, tech, travel, education-science, society",
    },
  ],
  [
    "discoverystream.topicSelection.selectedTopics",
    {
      title: "Selected topics",
      value: "",
    },
  ],
  [
    "discoverystream.topicSelection.suggestedTopics",
    {
      title: "Suggested topics to choose during onboarding for topic selection",
      value: "business, arts, government",
    },
  ],
  [
    "discoverystream.topicSelection.hasBeenUpdatedPreviously",
    {
      title: "Returns true only if the user has previously selected topics",
      value: false,
    },
  ],
  [
    "discoverystream.topicSelection.onboarding.displayCount",
    {
      title: "amount of times that topic selection onboarding has been shown",
      value: 0,
    },
  ],
  [
    "discoverystream.topicSelection.onboarding.maybeDisplay",
    {
      title:
        "Whether the onboarding should be shown, based on previous interactions",
      value: true,
    },
  ],
  [
    "discoverystream.topicSelection.onboarding.lastDisplayed",
    {
      title:
        "time in ms that onboarding was last shown (stored as string due to contraits of prefs)",
      value: "",
    },
  ],
  [
    "discoverystream.topicSelection.onboarding.displayTimeout",
    {
      title: "time in ms that the onboarding show be shown next",
      value: 0,
    },
  ],
  [
    "discoverystream.topicSelection.onboarding.enabled",
    {
      title: "enabled onboarding experience for topic selection onboarding",
      value: false,
    },
  ],
  [
    "discoverystream.topicLabels.enabled",
    {
      title: "Enables topic labels for discovery stream",
      // pref is dynamic
      getValue: showTopicLabels,
    },
  ],
  [
    "discoverystream.spocs.onDemand",
    {
      title: "Set sponsored content to only update cache when requested.",
      value: false,
    },
  ],
  [
    "discoverystream.spocs.cacheTimeout",
    {
      title: "Set sponsored content cache timeout in minutes.",
    },
  ],
  [
    "discoverystream.spocs.startupCache.enabled",
    {
      title: "Controls if spocs should be included in startup cache.",
      value: false,
    },
  ],
  [
    "discoverystream.publisherFavicon.enabled",
    {
      title: "Enables publisher favicons on recommended stories",
      value: true,
    },
  ],
  [
    "discoverystream.sections.clientLayout.enabled",
    {
      title:
        "Enables client side and remote settings layout for recommended stories",
      value: false,
    },
  ],
  [
    "support.url",
    {
      title: "Link to HNT's support page",
      getValue: () => {
        // Services.urlFormatter completes the in-product SUMO page URL:
        // https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/new-tab
        const baseUrl = Services.urlFormatter.formatURLPref(
          "app.support.baseURL"
        );
        return `${baseUrl}new-tab`;
      },
    },
  ],
  [
    "privacyInfo.url",
    {
      title: "Link to HNT's sponsor privacy page",
      getValue: () => {
        // Services.urlFormatter completes the in-product SUMO page URL:
        // https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/sponsor-privacy
        const baseUrl = Services.urlFormatter.formatURLPref(
          "app.support.baseURL"
        );
        return `${baseUrl}sponsor-privacy`;
      },
    },
  ],
  [
    "caretBlinkCount",
    {
      title:
        "The amount of times the caret blinks. This pref copies the value from the system settings",
      getValue: () => {
        return Services.appinfo.caretBlinkCount;
      },
    },
  ],
  [
    "caretBlinkTime",
    {
      title:
        "Rate at which the caret blinks. This pref copies the value from the system settings",
      getValue: () => {
        return Services.appinfo.caretBlinkTime;
      },
    },
  ],
  [
    "showSponsoredCheckboxes",
    {
      title:
        "'Support Firefox' pref on 'about:settings#home' page. Toggles all sponsored results on and off at the same time",
      value: true,
    },
  ],
  [
    "activationWindow.variant",
    {
      title:
        "Set to the activation window variant if in activation window mode, otherwise the empty string.",
      value: "",
    },
  ],
  [
    "nova.enabled",
    {
      title: "Boolean flag to enable Nova",
      value: false,
    },
  ],
  [
    "selfLoading.enabled",
    {
      title:
        "Communicates to AboutNewTabChild whether or not it should load the classic scripts or do nothing.",
      value: true,
    },
  ],
  [
    "remote-renderer.enabled",
    {
      title:
        "Set to true to enable the RemoteSettings backed renderer for newtab. See RemoteRenderer.sys.mjs for more details.",
      value: false,
    },
  ],
]);

// Array of each feed's FEEDS_CONFIG factory and values to add to PREFS_CONFIG
const FEEDS_DATA = [
  {
    name: "startupcacheinit",
    factory: () => new lazy.StartupCacheInit(),
    title: "Sends a copy of the state to the startup cache newtab",
    value: true,
  },
  {
    name: "aboutpreferences",
    factory: () => new lazy.AboutPreferences(),
    title: "about:preferences rendering",
    value: true,
  },
  {
    name: "newtabinit",
    factory: () => new lazy.NewTabInit(),
    title: "Sends a copy of the state to each new tab that is opened",
    value: true,
  },
  {
    name: "places",
    factory: () => new lazy.PlacesFeed(),
    title: "Listens for and relays various Places-related events",
    value: true,
  },
  {
    name: "prefs",
    factory: () => new lazy.PrefsFeed(PREFS_CONFIG),
    title: "Preferences",
    value: true,
  },
  {
    name: "sections",
    factory: () => new lazy.SectionsFeed(),
    title: "Manages sections",
    value: true,
  },
  {
    name: "section.highlights",
    factory: () => new lazy.HighlightsFeed(),
    title: "Fetches content recommendations from places db",
    value: false,
  },
  {
    name: "system.topstories",
    factory: () =>
      new lazy.TopStoriesFeed(PREFS_CONFIG.get("discoverystream.config")),
    title:
      "System pref that fetches content recommendations from a configurable content provider",
    // Dynamically determine if Pocket should be shown for a geo / locale
    getValue: ({ geo, locale }) => {
      // If we don't have geo, we don't want to flash the screen with stories while geo loads.
      // Best to display nothing until geo is ready.
      if (!geo) {
        return false;
      }
      const preffedRegionsBlockString =
        lazy.NimbusFeatures.pocketNewtab.getVariable("regionStoriesBlock") ||
        "";
      const preffedRegionsString =
        lazy.NimbusFeatures.pocketNewtab.getVariable("regionStoriesConfig") ||
        "";
      const preffedLocaleListString =
        lazy.NimbusFeatures.pocketNewtab.getVariable("localeListConfig") || "";
      const preffedBlockRegions = preffedRegionsBlockString
        .split(",")
        .map(s => s.trim());
      const preffedRegions = preffedRegionsString.split(",").map(s => s.trim());
      const preffedLocales = preffedLocaleListString
        .split(",")
        .map(s => s.trim());
      const locales = {
        US: ["en-CA", "en-GB", "en-US"],
        CA: ["en-CA", "en-GB", "en-US"],
        GB: ["en-CA", "en-GB", "en-US"],
        AU: ["en-CA", "en-GB", "en-US"],
        NZ: ["en-CA", "en-GB", "en-US"],
        IN: ["en-CA", "en-GB", "en-US"],
        IE: ["en-CA", "en-GB", "en-US"],
        ZA: ["en-CA", "en-GB", "en-US"],
        CH: ["de"],
        BE: ["de"],
        DE: ["de"],
        AT: ["de"],
        IT: ["it"],
        FR: ["fr"],
        ES: ["es-ES"],
        PL: ["pl"],
        JP: ["ja", "ja-JP-mac"],
      }[geo];

      const regionBlocked = preffedBlockRegions.includes(geo);
      const localeEnabled = locale && preffedLocales.includes(locale);
      const regionEnabled =
        preffedRegions.includes(geo) && !!locales && locales.includes(locale);
      return !regionBlocked && (localeEnabled || regionEnabled);
    },
  },
  {
    name: "systemtick",
    factory: () => new lazy.SystemTickFeed(),
    title: "Produces system tick events to periodically check for data expiry",
    value: true,
  },
  {
    name: "telemetry",
    factory: () => new lazy.TelemetryFeed(),
    title: "Relays telemetry-related actions to PingCentre",
    value: true,
  },
  {
    name: "favicon",
    factory: () => new lazy.FaviconFeed(),
    title: "Fetches tippy top manifests from remote service",
    value: true,
  },
  {
    name: "system.topsites",
    factory: () => new lazy.TopSitesFeed(),
    title: "Queries places and gets metadata for Top Sites section",
    value: true,
  },
  {
    name: "discoverystreamfeed",
    factory: () => new lazy.DiscoveryStreamFeed(),
    title: "Handles new pocket ui for the new tab page",
    value: true,
  },
  {
    name: "sectionslayoutfeed",
    factory: () => new lazy.SectionsLayoutFeed(),
    title: "Fetches section layout configurations from Remote Settings",
    value: true,
  },
  {
    name: "wallpaperfeed",
    factory: () => new lazy.WallpaperFeed(),
    title: "Handles fetching and managing wallpaper data from Remote Settings",
    value: true,
  },
  {
    name: "weatherfeed",
    factory: () => new lazy.WeatherFeed(),
    title: "Handles fetching and caching weather data",
    value: true,
  },
  {
    name: "adsfeed",
    factory: () => new lazy.AdsFeed(),
    title: "Handles fetching and caching ads data",
    value: true,
  },
  {
    name: "inferredpersonalizationfeed",
    factory: () => new lazy.InferredPersonalizationFeed(),
    title:
      "Handles generating and caching an interest vector for inferred personalization",
    value: true,
  },
  {
    name: "smartshortcutsfeed",
    factory: () => new lazy.SmartShortcutsFeed(),
    title:
      "Handles generating and caching an interest vector for shortcuts personalization",
    value: true,
  },
  {
    name: "newtabattributionfeed",
    factory: () => new lazy.NewTabAttributionFeed(),
    title: "Handles a local DB for story and shortcuts clicks and impressions",
    value: true,
  },
  {
    name: "newtabmessaging",
    factory: () => new lazy.NewTabMessaging(),
    title: "Handles fetching and triggering ASRouter messages in newtab",
    value: true,
  },
  {
    name: "listsfeed",
    factory: () => new lazy.ListsFeed(),
    title: "Handles the data for the Todo list widget",
    value: true,
  },
  {
    name: "sportsfeed",
    factory: () => new lazy.SportsFeed(),
    title: "Handles persistent state for the Sports widget",
    value: true,
  },
  {
    name: "timerfeed",
    factory: () => new lazy.TimerFeed(),
    title: "Handles the data for the Timer widget",
    value: true,
  },
  {
    name: "externalcomponentsfeed",
    factory: () => new lazy.ExternalComponentsFeed(),
    title: "Handles updating the registry of external components",
    value: true,
  },
];

const FEEDS_CONFIG = new Map();

for (const config of FEEDS_DATA) {
  const pref = `feeds.${config.name}`;
  FEEDS_CONFIG.set(pref, config.factory);
  PREFS_CONFIG.set(pref, config);
}

export class ActivityStream {
  #createdInstant = null;

  /**
   * constructor - Initializes an instance of ActivityStream
   *
   * @param {Temporal.Instant} [createdInstant=null]
   *   The creation time of the current user profile.
   */
  constructor(createdInstant) {
    this.initialized = false;
    this.store = new lazy.Store();
    this._defaultPrefs = new lazy.DefaultPrefs(PREFS_CONFIG);
    this._proxyRegistered = false;
    this.#createdInstant = createdInstant ?? null;

    if (
      Services.prefs.getBoolPref(PREF_NEWTAB_REMOTE_RENDERER_ENABLED, false)
    ) {
      this.remoteRenderer = new lazy.RemoteRenderer();
    }
  }

  /**
   * Returns a Temporal.Instant for when the user profile was created, or null
   * if that value was never passed to us in the constructor.
   *
   * @type {Temporal.Instant}
   */
  get createdInstant() {
    return this.#createdInstant;
  }

  get feeds() {
    if (shouldInitializeFeeds()) {
      return FEEDS_CONFIG;
    }

    // We currently make excpetions for topsites, and prefs feeds
    // because they currently impacts tests timing for places initialization.
    // See bug 1999166.
    const feeds = new Map([
      ["feeds.system.topsites", FEEDS_CONFIG.get("feeds.system.topsites")],
      ["feeds.prefs", FEEDS_CONFIG.get("feeds.prefs")],
    ]);
    return feeds;
  }

  init() {
    this._updateDynamicPrefs();
    this._defaultPrefs.init();
    Services.obs.addObserver(this, "intl:app-locales-changed");
    Services.prefs.addObserver(PREF_IMAGE_PROXY_ENABLED, this);
    lazy.NewTabActorRegistry.init();

    // Hook up the store and let all feeds and pages initialize
    this.store.init(
      this.feeds,
      ac.BroadcastToContent({
        type: at.INIT,
        data: {
          locale: this.locale,
        },
        meta: {
          isStartup: true,
        },
      }),
      { type: at.UNINIT }
    );

    this.initialized = true;

    this.registerNetworkProxy();
  }

  /**
   * Registers network proxy channel filter for image requests.
   * This enables privacy-preserving image proxy for newtab when
   * inferred personalization is enabled.
   */
  registerNetworkProxy() {
    const enabled = Services.prefs.getBoolPref(PREF_IMAGE_PROXY_ENABLED, false);
    if (!this._proxyRegistered && enabled) {
      lazy.ProxyService.registerChannelFilter(this, 0);
      this._proxyRegistered = true;
    }
  }

  /**
   * Unregisters network proxy channel filter.
   */
  unregisterNetworkProxy() {
    if (this._proxyRegistered) {
      lazy.ProxyService.unregisterChannelFilter(this);
      this._proxyRegistered = false;
    }
  }

  /**
   * Retrieves and validates image proxy configuration from prefs/nimbus.
   *
   * @returns {object|null} Image proxy config object, or null if disabled/invalid.
   */
  getImageProxyConfig() {
    try {
      if (!this.store || !this.initialized) {
        return null;
      }

      const state = this.store.getState();
      if (!state || !state.Prefs) {
        return null;
      }

      const { values } = state.Prefs;

      const config = values?.trainhopConfig?.imageProxy;
      if (
        !config ||
        !config.enabled ||
        !config.proxyHost ||
        !config.proxyPort ||
        !config.proxyAuthHeader ||
        !values?.[PREF_INFERRED_ENABLED] ||
        !values?.[PREF_IMAGE_PROXY_ENABLED_STORE]
      ) {
        return null;
      }
      return {
        proxyHost: config.proxyHost,
        proxyPort: config.proxyPort,
        proxyAuthHeader: config.proxyAuthHeader,
        connectionIsolationKey: config.connectionIsolationKey || "",
        failoverProxy: config.failoverProxy,
        imageProxyHosts: (config.imageProxyHosts || "")
          .split(",")
          .map(host => host.trim()),
      };
    } catch (e) {
      return null;
    }
  }

  /**
   * nsIProtocolProxyChannelFilter implementation. Applies MASQUE proxy
   * to image requests from newtab when configured.
   *
   * @param {nsIChannel} channel
   * @param {nsIProxyInfo} proxyInfo
   * @param {nsIProtocolProxyChannelFilter} callback
   */
  applyFilter(channel, proxyInfo, callback) {
    const { browsingContext } = channel.loadInfo;
    let browser = browsingContext?.top?.embedderElement;

    if (!browser || !lazy.AboutNewTabParent.loadedTabs.has(browser)) {
      callback.onProxyFilterResult(proxyInfo);
      return;
    }

    const config = this.getImageProxyConfig();

    if (!config) {
      callback.onProxyFilterResult(proxyInfo);
      return;
    }

    if (
      config.imageProxyHosts.includes(channel.URI.host) &&
      channel.URI?.scheme === "https"
    ) {
      callback.onProxyFilterResult(
        lazy.ProxyService.newProxyInfo(
          "https" /* aType */,
          config.proxyHost /* aHost */,
          config.proxyPort /* aPort */,
          config.proxyAuthHeader /* aProxyAuthorizationHeader */,
          config.connectionIsolationKey /* aConnectionIsolationKey */,
          0 /* aFlags */,
          5000 /* aFailoverTimeout */,
          config.failoverProxy /* aFailoverProxy */
        )
      );
    } else {
      callback.onProxyFilterResult(proxyInfo);
    }
  }

  QueryInterface = ChromeUtils.generateQI([Ci.nsIProtocolProxyChannelFilter]);

  /**
   * Check if an old pref has a custom value to migrate. Clears the pref so that
   * it's the default after migrating (to avoid future need to migrate).
   *
   * @param oldPrefName {string} Pref to check and migrate
   * @param cbIfNotDefault {function} Callback that gets the current pref value
   */
  _migratePref(oldPrefName, cbIfNotDefault) {
    // Nothing to do if the user doesn't have a custom value
    if (!Services.prefs.prefHasUserValue(oldPrefName)) {
      return;
    }

    // Figure out what kind of pref getter to use
    let prefGetter;
    switch (Services.prefs.getPrefType(oldPrefName)) {
      case Services.prefs.PREF_BOOL:
        prefGetter = "getBoolPref";
        break;
      case Services.prefs.PREF_INT:
        prefGetter = "getIntPref";
        break;
      case Services.prefs.PREF_STRING:
        prefGetter = "getStringPref";
        break;
    }

    // Give the callback the current value then clear the pref
    cbIfNotDefault(Services.prefs[prefGetter](oldPrefName));
    Services.prefs.clearUserPref(oldPrefName);
  }

  uninit() {
    if (this.geo === "") {
      Services.obs.removeObserver(this, lazy.Region.REGION_TOPIC);
    }
    delete this.geo;

    Services.obs.removeObserver(this, "intl:app-locales-changed");
    Services.prefs.removeObserver(PREF_IMAGE_PROXY_ENABLED, this);

    this.store.uninit();
    this.unregisterNetworkProxy();
    lazy.NewTabActorRegistry.uninit();
    this.initialized = false;
  }

  _updateDynamicPrefs() {
    // Save the geo pref if we have it
    if (lazy.Region.home) {
      if (this.geo === "") {
        // The observer has become obsolete.
        Services.obs.removeObserver(this, lazy.Region.REGION_TOPIC);
      }
      this.geo = lazy.Region.home;
    } else if (this.geo !== "") {
      // Watch for geo changes and use a dummy value for now
      Services.obs.addObserver(this, lazy.Region.REGION_TOPIC);
      this.geo = "";
    }

    this.locale = Services.locale.appLocaleAsBCP47;

    // Update the pref config of those with dynamic values
    for (const pref of PREFS_CONFIG.keys()) {
      // Only need to process dynamic prefs
      const prefConfig = PREFS_CONFIG.get(pref);
      if (!prefConfig.getValue) {
        continue;
      }

      // Have the dynamic pref just reuse using existing default, e.g., those
      // set via Autoconfig or policy
      try {
        const existingDefault = this._defaultPrefs.get(pref);
        if (existingDefault !== undefined && prefConfig.value === undefined) {
          prefConfig.getValue = () => existingDefault;
        }
      } catch (ex) {
        // We get NS_ERROR_UNEXPECTED for prefs that have a user value (causing
        // default branch to believe there's a type) but no actual default value
      }

      // Compute the dynamic value (potentially generic based on dummy geo)
      const newValue = prefConfig.getValue({
        geo: this.geo,
        locale: this.locale,
      });

      // If there's an existing value and it has changed, that means we need to
      // overwrite the default with the new value.
      if (prefConfig.value !== undefined && prefConfig.value !== newValue) {
        this._defaultPrefs.set(pref, newValue);
      }

      prefConfig.value = newValue;
    }
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "intl:app-locales-changed":
      case lazy.Region.REGION_TOPIC:
        this._updateDynamicPrefs();
        break;
      case "nsPref:changed":
        if (data === PREF_IMAGE_PROXY_ENABLED) {
          const enabled = Services.prefs.getBoolPref(
            PREF_IMAGE_PROXY_ENABLED,
            false
          );
          if (enabled) {
            this.registerNetworkProxy();
          } else {
            this.unregisterNetworkProxy();
          }
        }
        break;
    }
  }
}
