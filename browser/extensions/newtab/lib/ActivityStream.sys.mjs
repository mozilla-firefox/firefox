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

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutPreferences: "resource://newtab/lib/AboutPreferences.sys.mjs",
  AdsFeed: "resource://newtab/lib/AdsFeed.sys.mjs",
  DEFAULT_SITES: "resource://newtab/lib/DefaultSites.sys.mjs",
  DefaultPrefs: "resource://newtab/lib/ActivityStreamPrefs.sys.mjs",
  DiscoveryStreamFeed: "resource://newtab/lib/DiscoveryStreamFeed.sys.mjs",
  FaviconFeed: "resource://newtab/lib/FaviconFeed.sys.mjs",
  HighlightsFeed: "resource://newtab/lib/HighlightsFeed.sys.mjs",
  NewTabInit: "resource://newtab/lib/NewTabInit.sys.mjs",
  NewTabMessaging: "resource://newtab/lib/NewTabMessaging.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefsFeed: "resource://newtab/lib/PrefsFeed.sys.mjs",
  PlacesFeed: "resource://newtab/lib/PlacesFeed.sys.mjs",
  RecommendationProvider:
    "resource://newtab/lib/RecommendationProvider.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SectionsFeed: "resource://newtab/lib/SectionsManager.sys.mjs",
  Store: "resource://newtab/lib/Store.sys.mjs",
  SystemTickFeed: "resource://newtab/lib/SystemTickFeed.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TopSitesFeed: "resource://newtab/lib/TopSitesFeed.sys.mjs",
  TopStoriesFeed: "resource://newtab/lib/TopStoriesFeed.sys.mjs",
  WallpaperFeed: "resource://newtab/lib/WallpaperFeed.sys.mjs",
  WeatherFeed: "resource://newtab/lib/WeatherFeed.sys.mjs",
});

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

const REGION_THUMBS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.region-thumbs-config";
const LOCALE_THUMBS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.locale-thumbs-config";

const REGION_CONTEXTUAL_CONTENT_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.contextualContent.region-content-config";
const LOCALE_CONTEXTUAL_CONTENT_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.contextualContent.locale-content-config";

const REGION_SECTIONS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.region-content-config";
const LOCALE_SECTIONS_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.locale-content-config";

export function csvPrefHasValue(stringPrefName, value) {
  if (typeof stringPrefName !== "string") {
    throw new Error(`The stringPrefName argument is not a string`);
  }

  const pref = Services.prefs.getStringPref(stringPrefName) || "";
  const prefValues = pref
    .split(",")
    .map(s => s.trim())
    .filter(item => item);

  return prefValues.includes(value);
}

function useInferredPersonalization({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_INFERRED_PERSONALIZATION_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_INFERRED_PERSONALIZATION_CONFIG, locale)
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

function showThumbsUpDown({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_THUMBS_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_THUMBS_CONFIG, locale)
  );
}

function showContextualContent({ geo, locale }) {
  return (
    csvPrefHasValue(REGION_CONTEXTUAL_CONTENT_CONFIG, geo) &&
    csvPrefHasValue(LOCALE_CONTEXTUAL_CONTENT_CONFIG, locale)
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
    "feeds.section.topstories.options",
    {
      title: "Configuration options for top stories feed",
      // This is a dynamic pref as it depends on the feed being shown or not
      getValue: args =>
        JSON.stringify({
          api_key_pref: "extensions.pocket.oAuthConsumerKey",
          // Use the opposite value as what default value the feed would have used
          hidden: !PREFS_CONFIG.get("feeds.system.topstories").getValue(args),
          provider_icon: "chrome://global/skin/icons/pocket.svg",
          provider_name: "Pocket",
          read_more_endpoint:
            "https://getpocket.com/explore/trending?src=fx_new_tab",
          stories_endpoint: `https://getpocket.cdn.mozilla.net/v3/firefox/global-recs?version=3&consumer_key=$apiKey&locale_lang=${
            args.locale
          }&feed_variant=${
            showSpocs(args) ? "default_spocs_on" : "default_spocs_off"
          }`,
          stories_referrer: "https://getpocket.com/recommendations",
          topics_endpoint: `https://getpocket.cdn.mozilla.net/v3/firefox/trending-topics?version=2&consumer_key=$apiKey&locale_lang=${args.locale}`,
          show_spocs: showSpocs(args),
        }),
    },
  ],
  [
    "feeds.topsites",
    {
      title: "Displays Top Sites on the New Tab Page",
      value: true,
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
    "unifiedAds.adsFeed.enabled",
    {
      title:
        "Use AdsFeed.sys.mjs to fetch/cache/serve Mozilla Ad Routing Service (MARS) unified ads ",
      value: false,
    },
  ],
  [
    "unifiedAds.adsFeed.tiles.enabled",
    {
      title:
        "Use AdsFeed.sys.mjs to fetch/cache/serve sponsored top sites tiles",
      value: false,
    },
  ],
  [
    "unifiedAds.adsFeed.spocs.enabled",
    {
      title:
        "Use AdsFeed.sys.mjs to fetch/cache/serve sponsored content in recommended stories",
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
      value: "simple",
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
    "section.highlights.includePocket",
    {
      title:
        "Boolean flag that decides whether or not to show saved Pocket stories in highlights.",
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
    "newtabWallpapers.v2.enabled",
    {
      title: "Boolean flag to turn wallpaper v2 functionality on and off",
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
    "newtabAdSize.variant-a",
    {
      title: "Boolean flag to turn ad size variant A on and off",
      value: false,
    },
  ],
  [
    "newtabAdSize.variant-b",
    {
      title: "Boolean flag to turn ad size variant B on and off",
      value: false,
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
    "newtabLayouts.variant-a",
    {
      title: "Boolean flag to turn layout variant A on and off",
      value: false,
    },
  ],
  [
    "newtabLayouts.variant-b",
    {
      title: "Boolean flag to turn layout variant B on and off",
      value: false,
    },
  ],
  [
    "newtabShortcuts.refresh",
    {
      title: "Boolean flag to change sizes and spacing of new tab shortcuts",
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
      value: false,
    },
  ],
  [
    "discoverystream.sections.customizeMenuPanel.enabled",
    {
      title:
        "Boolean flag to enable the setions management panel in Customize menu",
      value: false,
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
    "discoverystream.sections.personalization.inferred.enabled",
    {
      title: "Boolean flag to enable inferred personalizaton",
      // pref is dynamic
      getValue: useInferredPersonalization,
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.position",
    {
      title:
        "Position of inferred personalizaton card. Should correlate to a row in Card Sections",
      value: "1",
    },
  ],
  [
    "discoverystream.sections.personalization.inferred.blocked",
    {
      title:
        "Boolean determining if personalized card is dismissed or visible on the page",
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
    "discoverystream.sections.cards.thumbsUpDown.enabled",
    {
      title:
        "Boolean flag to enable thumbs up/down buttons in the new card UI in recommended stories",
      value: true,
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
          api_key_pref: "extensions.pocket.oAuthConsumerKey",
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
    "discoverystream.isCollectionDismissible",
    {
      title: "Allows Pocket story collections to be dismissed",
      value: false,
    },
  ],
  [
    "discoverystream.onboardingExperience.dismissed",
    {
      title: "Allows the user to dismiss the new Pocket onboarding experience",
      skipBroadcast: true,
      alsoToPreloaded: true,
      value: false,
    },
  ],
  [
    "discoverystream.thumbsUpDown.enabled",
    {
      title: "Allow users to give thumbs up/down on recommended stories",
      // pref is dynamic
      getValue: showThumbsUpDown,
    },
  ],
  [
    "discoverystream.thumbsUpDown.searchTopsitesCompact",
    {
      title:
        "A compact layout of the search/topsites/stories sections to account for new height from thumbs up/down icons ",
      value: false,
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
    "showRecentSaves",
    {
      title: "Control whether a user wants recent saves visible on Newtab",
      value: true,
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
    "discoverystream.contextualContent.enabled",
    {
      title: "Controls if contextual content (List feed) is displayed",
      getValue: showContextualContent,
    },
  ],
  [
    "discoverystream.contextualContent.feeds",
    {
      title: "CSV list of possible topics for the contextual content feed",
      value: "need_to_know, fakespot",
    },
  ],
  [
    "discoverystream.contextualContent.selectedFeed",
    {
      title:
        "currently selected feed (one of discoverystream.contextualContent.feeds) to display in listfeed",
      value: "need_to_know",
    },
  ],
  [
    "discoverystream.contextualContent.listFeedTitle",
    {
      title: "Title for currently selected feed",
      value: "",
    },
  ],
  [
    "discoverystream.contextualContent.fakespot.defaultCategoryTitle",
    {
      title: "Title default category from fakespot endpoint",
      value: "",
    },
  ],
  [
    "discoverystream.contextualContent.fakespot.footerCopy",
    {
      title: "footer copy for fakespot feed",
      value: "",
    },
  ],
  [
    "discoverystream.contextualContent.fakespot.enabled",
    {
      title: "User controlled pref that displays fakespot feed",
      value: true,
    },
  ],
  [
    "discoverystream.contextualContent.fakespot.ctaCopy",
    {
      title: "cta copy for fakespot feed",
      value: "",
    },
  ],
  [
    "discoverystream.contextualContent.fakespot.ctaUrl",
    {
      title: "cta link for fakespot feed",
      value: "",
    },
  ],
  [
    "discoverystream.publisherFavicon.enabled",
    {
      title: "Enables publihser favicons on recommended stories",
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
]);

// Array of each feed's FEEDS_CONFIG factory and values to add to PREFS_CONFIG
const FEEDS_DATA = [
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
    name: "recommendationprovider",
    factory: () => new lazy.RecommendationProvider(),
    title: "Handles setup and interaction for the personality provider",
    value: true,
  },
  {
    name: "discoverystreamfeed",
    factory: () => new lazy.DiscoveryStreamFeed(),
    title: "Handles new pocket ui for the new tab page",
    value: true,
  },
  {
    name: "wallpaperfeed",
    factory: () => new lazy.WallpaperFeed(),
    title: "Handles fetching and managing wallpaper data from RemoteSettings",
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
    name: "newtabmessaging",
    factory: () => new lazy.NewTabMessaging(),
    title: "Handles fetching and triggering ASRouter messages in newtab",
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
  /**
   * constructor - Initializes an instance of ActivityStream
   */
  constructor() {
    this.initialized = false;
    this.store = new lazy.Store();
    this.feeds = FEEDS_CONFIG;
    this._defaultPrefs = new lazy.DefaultPrefs(PREFS_CONFIG);
  }

  init() {
    this._updateDynamicPrefs();
    this._defaultPrefs.init();
    Services.obs.addObserver(this, "intl:app-locales-changed");

    // Look for outdated user pref values that might have been accidentally
    // persisted when restoring the original pref value at the end of an
    // experiment across versions with a different default value.
    const DS_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.config";
    if (
      Services.prefs.prefHasUserValue(DS_CONFIG) &&
      [
        // Firefox 66
        `{"api_key_pref":"extensions.pocket.oAuthConsumerKey","enabled":false,"show_spocs":true,"layout_endpoint":"https://getpocket.com/v3/newtab/layout?version=1&consumer_key=$apiKey&layout_variant=basic"}`,
        // Firefox 67
        `{"api_key_pref":"extensions.pocket.oAuthConsumerKey","enabled":false,"show_spocs":true,"layout_endpoint":"https://getpocket.cdn.mozilla.net/v3/newtab/layout?version=1&consumer_key=$apiKey&layout_variant=basic"}`,
        // Firefox 68
        `{"api_key_pref":"extensions.pocket.oAuthConsumerKey","collapsible":true,"enabled":false,"show_spocs":true,"hardcoded_layout":true,"personalized":false,"layout_endpoint":"https://getpocket.cdn.mozilla.net/v3/newtab/layout?version=1&consumer_key=$apiKey&layout_variant=basic"}`,
      ].includes(Services.prefs.getStringPref(DS_CONFIG))
    ) {
      Services.prefs.clearUserPref(DS_CONFIG);
    }

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
  }

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

    Services.obs.removeObserver(this, "intl:app-locales-changed");

    this.store.uninit();
    this.initialized = false;
  }

  _updateDynamicPrefs() {
    // Save the geo pref if we have it
    if (lazy.Region.home) {
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

  observe(subject, topic) {
    switch (topic) {
      case "intl:app-locales-changed":
      case lazy.Region.REGION_TOPIC:
        this._updateDynamicPrefs();
        break;
    }
  }
}
