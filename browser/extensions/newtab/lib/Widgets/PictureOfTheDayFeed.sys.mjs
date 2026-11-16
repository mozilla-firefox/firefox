/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

/**
 * Picture of the Day widget - data flow overview
 *
 * Fetch / render:
 *   Once per day this feed fetches the picture from Merino (via
 *   TemporaryMerinoClientShim), stores it in PersistentCache, maps the payload
 *   onto our internal shape with normalize(), and broadcasts it into the Redux
 *   store, where PictureOfTheDay.jsx renders it. The Merino backend refreshes the
 *   picture on a daily cron (around midnight PST); when a fresh image is not
 *   available it serves the previous day's picture rather than nothing, so the
 *   client can legitimately receive the same picture across days. Whether the
 *   widget runs at all is gated by isEnabled() (isWidgetEnabled: widgets container
 *   on, the widget addable / trainhop-enabled, and the user's enabled pref).
 *
 * Set as wallpaper:
 *   The "Set wallpaper" CTA - shown only when newtabWallpapers.enabled +
 *   customWallpaper.enabled are on - dispatches WIDGETS_PICTURE_SET_WALLPAPER.
 *   setWallpaper() then fetches the image, derives a theme, applies it as the
 *   custom wallpaper (newtabWallpapers.*), and records the picture's published
 *   date in widgets.pictureOfTheDay.wallpaperActive.
 *
 * State reset:
 *   The JSX shows the "already set" checkmark only while wallpaperActive equals the
 *   current picture's published date, so a new day's picture re-offers the CTA on
 *   its own. wallpaperActive is also cleared when the user switches to a non-custom
 *   wallpaper (onPrefChangedAction) or replaces the custom wallpaper with a
 *   different image (onWallpaperUpload). Toggling wallpapers off in the Content
 *   section does not clear it (the picture stays selected) - the JSX just hides the
 *   checkmark while newtabWallpapers.user.enabled is off and shows it again on.
 *
 * Previous picture:
 *   Merino also sends yesterday's picture as `previous`. While today's is the
 *   wallpaper the widget shows that one instead, and setWallpaper() takes its date.
 *
 * wallpaperActive stores the published date of the picture that is the active
 * wallpaper ("" = none).
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  TemporaryMerinoClientShim:
    "resource://newtab/lib/TemporaryMerinoClientShim.sys.mjs",
  calculateTheme: "resource://newtab/lib/Wallpapers/WallpaperThemeUtils.mjs",
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  isWidgetEnabled,
} from "resource://newtab/common/WidgetsRegistry.mjs";
import { WALLPAPER_TYPES } from "resource://newtab/lib/Wallpapers/WallpaperFileNames.mjs";

const CACHE_KEY = "picture_of_the_day_feed";
const MERINO_CLIENT_KEY = "HNT_PICTURE_OF_THE_DAY_FEED";

const PICTURE_OF_THE_DAY_ENTRY = WIDGET_REGISTRY.find(
  w => w.id === "pictureOfTheDay"
);

const PREF_WIDGETS_ENABLED = "widgets.enabled";
const PREF_ENABLED = "widgets.pictureOfTheDay.enabled";
const PREF_SYSTEM_ENABLED = "widgets.system.pictureOfTheDay.enabled";
const PREF_ENDPOINT = "widgets.pictureOfTheDay.endpoint";
const PREF_ENDPOINTS_ALLOWLIST = "discoverystream.endpoints";

/**
 * Fetches the daily "Picture of the day" from Merino (scraped from Wikimedia
 * Commons) once per day, caches it, and broadcasts it to the New Tab widget.
 * The Merino payload carries { title, thumbnail_image_url, high_res_image_url,
 * published_date, description, file_page, author, license_label, license_link };
 * normalize() maps it onto our internal shape. The attribution fields
 * (file_page/author/license_label/license_link) may be absent while the backend
 * source lacks them, so callers must treat them as optional.
 */
export class PictureOfTheDayFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this.loaded = false;
    this.merino = null;
    this.currentImageUrl = "";
    // Set while this feed is applying its own wallpaper so the WALLPAPER_UPLOAD
    // it triggers isn't mistaken for the user replacing the wallpaper.
    this.settingWallpaper = false;
    this.cache = this.PersistentCache(CACHE_KEY, true);
  }

  // Share the registry enablement logic the UI uses (widgets container on, the
  // widget addable, and the user's enabled pref set) so trainhop rollouts and
  // the global widgets toggle are honored consistently (mirrors PrivacyFeed).
  isEnabled() {
    const { values } = this.store.getState().Prefs;
    return isWidgetEnabled(
      PICTURE_OF_THE_DAY_ENTRY,
      values,
      values[PREF_WIDGETS_ENABLED]
    );
  }

  // Resolve the Merino endpoint, guarding it against the shared endpoint
  // allowlist so a mis-set pref can't point the fetch at an arbitrary host.
  getEndpoint() {
    const { values } = this.store.getState().Prefs;
    const endpoint = values[PREF_ENDPOINT];
    if (!endpoint) {
      return null;
    }
    const allowed = (values[PREF_ENDPOINTS_ALLOWLIST] ?? "")
      .split(",")
      .map(item => item.trim())
      .filter(item => item);
    if (!allowed.some(prefix => endpoint.startsWith(prefix))) {
      console.error(
        `Picture of the day endpoint not in allowlist: ${endpoint}`
      );
      return null;
    }
    return endpoint;
  }

  // The picture changes once per (local) day; refetch when we have nothing
  // cached or the cached entry is from a previous local day.
  isStale(lastUpdated) {
    if (!lastUpdated) {
      return true;
    }
    const D = this.Date();
    return new D(lastUpdated).toDateString() !== new D(D.now()).toDateString();
  }

  async init() {
    await this.loadPicture();
  }

  async resetCache() {
    if (this.cache) {
      await this.cache.set("picture", {});
    }
  }

  async reset() {
    await this.resetCache();
    this.loaded = false;
  }

  // Map a raw Merino payload onto our internal shape. Returns null when there's
  // no usable image so the caller can fall back to the empty state.
  normalize(raw) {
    if (!raw) {
      return null;
    }
    const imageUrl = raw.high_res_image_url || raw.thumbnail_image_url;
    if (!imageUrl) {
      return null;
    }
    return {
      imageUrl,
      thumbnailUrl: raw.thumbnail_image_url || "",
      title: raw.title || "",
      description: raw.description || "",
      publishedDate: raw.published_date || "",
      sourceUrl: raw.file_page || "",
      author: raw.author || "",
      licenseLabel: raw.license_label || "",
      licenseUrl: raw.license_link || "",
    };
  }

  async fetch() {
    if (!this.merino) {
      this.merino = this.MerinoClient(MERINO_CLIENT_KEY);
    }
    const endpointUrl = this.getEndpoint();
    if (!endpointUrl) {
      this.update({ error: "no_endpoint" });
      return;
    }
    const { data, error } = await this.merino.fetchPictureOfTheDay({
      source: "newtab",
      endpointUrl,
      acceptLanguage: Services.locale.appLocaleAsBCP47,
    });
    const picture = this.normalize(data);
    if (error || !picture) {
      // Silent fallback: no picture means the widget shows the sunrise
      // gradient. The error is broadcast for telemetry only.
      this.update({ error: error || "empty_response" });
      return;
    }
    // Shown instead of today's picture while today's is the wallpaper. Needs its
    // own date, since setWallpaper() reads an empty date as today's picture.
    let previous = this.normalize(data.previous);
    if (
      !previous?.publishedDate ||
      previous.publishedDate === picture.publishedDate
    ) {
      previous = null;
    }
    const entry = { ...picture, previous, lastUpdated: this.Date().now() };
    await this.cache.set("picture", entry);
    this.update(entry);
  }

  async loadPicture() {
    const cachedData = (await this.cache.get()) || {};
    const { picture } = cachedData;
    if (this.isStale(picture?.lastUpdated)) {
      await this.fetch();
    } else {
      this.update(picture);
    }
    this.loaded = true;
  }

  update(data = {}) {
    this.currentImageUrl = data.imageUrl ?? "";
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PICTURE_OF_THE_DAY_UPDATE,
        data: {
          imageUrl: data.imageUrl ?? "",
          thumbnailUrl: data.thumbnailUrl ?? "",
          title: data.title ?? "",
          description: data.description ?? "",
          publishedDate: data.publishedDate ?? "",
          sourceUrl: data.sourceUrl ?? "",
          author: data.author ?? "",
          licenseLabel: data.licenseLabel ?? "",
          licenseUrl: data.licenseUrl ?? "",
          previous: data.previous ?? null,
          lastUpdated: data.lastUpdated ?? null,
          error: data.error ?? null,
        },
      })
    );
  }

  // The Merino image host doesn't send CORS headers, so the picture bytes can
  // only be read from the privileged main process. Fetch the current picture,
  // verify it's an image, derive a light/dark theme, upload it as a custom
  // wallpaper, and select it — the same end state as a manual Customize upload.
  // Takes the page that asked, so the save can be reported against it. Without
  // one the parent has nobody to report to and the event is dropped.
  // `requestedDate` is the date of the picture the widget showed; "" means
  // today's. A date matching neither picture is stale, so nothing is set.
  async setWallpaper(target = null, requestedDate = "", win = null) {
    // The CTA is only shown when wallpapers and custom wallpapers are enabled,
    // so reaching here with either disabled means something dispatched the
    // action out of band.
    const { values } = this.store.getState().Prefs;
    if (
      !values["newtabWallpapers.enabled"] ||
      !values["newtabWallpapers.customWallpaper.enabled"]
    ) {
      throw new Error(
        "PictureOfTheDayFeed.setWallpaper called while custom wallpapers are disabled"
      );
    }
    const state = this.store.getState().PictureOfTheDay ?? {};
    let picture = { ...state, imageUrl: this.currentImageUrl };
    if (requestedDate && requestedDate === state.previous?.publishedDate) {
      picture = state.previous;
    } else if (requestedDate && requestedDate !== state.publishedDate) {
      return;
    }
    const { imageUrl } = picture;
    if (!imageUrl) {
      return;
    }
    // With the URL, not after the download: a refresh landing mid-fetch would
    // otherwise file yesterday's picture under today's name and date, and the
    // saved name is the only one that picture ever gets.
    // The description rather than the title, matching the widget's alt text:
    // the title is the same generic line every day, and Bug 2050975 rejected
    // it as unreliable. Merino localizes the description, so it is a real name
    // in the person's own language. Without one the picture falls back to
    // being numbered like any other saved image.
    const { description = "", publishedDate = "" } = picture;
    try {
      const response = await this.fetchImage(imageUrl);
      const contentType = response.headers?.get?.("content-type") || "";
      if (!response.ok || !contentType.startsWith("image/")) {
        console.error(
          `PictureOfTheDayFeed: wallpaper fetch not usable ` +
            `(status ${response.status}, type "${contentType}")`
        );
        return;
      }
      const blob = await response.blob();
      let theme = "light";
      try {
        // sys.mjs has no createImageBitmap or OffscreenCanvas, and the hidden
        // window is macOS only, so decode with the window that asked.
        theme = await lazy.calculateTheme(win, blob);
      } catch (e) {
        console.error("PictureOfTheDayFeed: theme calculation failed", e);
      }
      this.settingWallpaper = true;
      // Saved into "Your images" like an upload, so picking a different
      // wallpaper later does not throw this picture away.
      this.store.dispatch({
        type: at.WALLPAPER_UPLOAD,
        data: {
          file: blob,
          theme,
          type: WALLPAPER_TYPES.PictureOfTheDay,
          name: description,
          publishedDate,
        },
        meta: { fromTarget: target },
      });
      // Select the uploaded image as the active custom wallpaper and turn on
      // the user's wallpaper display. The wallpaper feature pref
      // (newtabWallpapers.enabled) is deliberately left untouched: the "Set
      // wallpaper" CTA is only shown when it's already enabled, so we never
      // force the feature on (product decision). One transaction, because
      // content re-renders on every pref broadcast: turning the display on
      // before "custom" is selected paints whatever wallpaper was chosen last.
      this.store.dispatch(
        ac.SetMultiplePrefs({
          "newtabWallpapers.wallpaper": "custom",
          "newtabWallpapers.initialWallpaper": "",
          "newtabWallpapers.user.enabled": true,
          "widgets.pictureOfTheDay.wallpaperActive": publishedDate,
        })
      );
    } catch (e) {
      console.error("PictureOfTheDayFeed: failed to set wallpaper", e);
    }
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      case PREF_WIDGETS_ENABLED:
      case PREF_ENABLED:
      case PREF_SYSTEM_ENABLED:
      case "region":
      case "trainhopConfig": {
        const enabled = this.isEnabled();
        if (enabled && !this.loaded) {
          await this.loadPicture();
        } else if (!enabled && this.loaded) {
          await this.reset();
        }
        break;
      }
      case PREF_ENDPOINT:
        if (this.isEnabled()) {
          await this.fetch();
        }
        break;
      case "newtabWallpapers.wallpaper": {
        // Un-set the "set as wallpaper" state when the user switches to a
        // different (non-custom) wallpaper.
        const { values } = this.store.getState().Prefs;
        if (
          values["widgets.pictureOfTheDay.wallpaperActive"] &&
          values["newtabWallpapers.wallpaper"] !== "custom"
        ) {
          this.store.dispatch(
            ac.SetPref("widgets.pictureOfTheDay.wallpaperActive", "")
          );
        }
        break;
      }
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (this.isEnabled() && !this.loaded) {
          await this.init();
        }
        break;
      case at.SYSTEM_TICK:
        if (this.isEnabled()) {
          await this.loadPicture();
        }
        break;
      case at.PREF_CHANGED:
        await this.onPrefChangedAction(action);
        break;
      case at.WIDGETS_PICTURE_SET_WALLPAPER:
        await this.setWallpaper(
          action.meta?.fromTarget,
          action.data?.publishedDate,
          action._target?.window
        );
        break;
      case at.WALLPAPER_UPLOAD:
        this.onWallpaperUpload();
        break;
    }
  }

  // A WALLPAPER_UPLOAD this feed didn't initiate means the user replaced the
  // custom wallpaper with a different image, so the current picture is no longer
  // the active wallpaper - clear the "already set" state. Our own
  // setWallpaper sets `settingWallpaper` and re-records the date right after, so
  // it must not clear here.
  onWallpaperUpload() {
    if (this.settingWallpaper) {
      this.settingWallpaper = false;
      return;
    }
    const { values } = this.store.getState().Prefs;
    if (values["widgets.pictureOfTheDay.wallpaperActive"]) {
      this.store.dispatch(
        ac.SetPref("widgets.pictureOfTheDay.wallpaperActive", "")
      );
    }
  }
}

/**
 * Thin wrappers around external tools so tests can stub them.
 */
PictureOfTheDayFeed.prototype.MerinoClient = name => {
  return new lazy.TemporaryMerinoClientShim(name);
};
PictureOfTheDayFeed.prototype.PersistentCache = (...args) => {
  return new lazy.PersistentCache(...args);
};
PictureOfTheDayFeed.prototype.Date = () => {
  return Date;
};
PictureOfTheDayFeed.prototype.fetchImage = url => {
  return fetch(url);
};
