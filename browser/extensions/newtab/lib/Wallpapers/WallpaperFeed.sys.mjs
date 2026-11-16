/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "gNewTabStrings", () => {
  return new Localization(["browser/newtab/newtab.ftl"], true);
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

import {
  LIBRARY_DIRECTORY_NAME,
  WALLPAPER_TYPES,
  buildSavedWallpaperFilename,
  getDetailsFilename,
  getWallpaperURL,
  sanitizeSavedName,
  getThumbnailFilename,
  parseWallpaperFilename,
} from "resource://newtab/lib/Wallpapers/WallpaperFileNames.mjs";
import { isWallpaperLibraryEnabled } from "resource://newtab/lib/Wallpapers/WallpaperLibraryPref.mjs";

const PREF_WALLPAPERS_ENABLED =
  "browser.newtabpage.activity-stream.newtabWallpapers.enabled";

const PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER =
  "browser.newtabpage.activity-stream.newtabWallpapers.highlightSeenCounter";

const WALLPAPER_REMOTE_SETTINGS_COLLECTION_V2 = "newtab-wallpapers-v2";

const PREF_WALLPAPERS_CUSTOM_WALLPAPER_ENABLED =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.enabled";

// Holds the filename of the applied wallpaper. Named "uuid" because that is
// what it used to hold, and older New Tab versions still read it.
const PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid";

// The number the next saved image gets. Counts up and is never reused, so an
// image keeps its number even when an earlier one is removed.
const PREF_WALLPAPERS_CUSTOM_WALLPAPER_NEXT_NUMBER =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.nextNumber";

const PREF_WALLPAPERS_CUSTOM_WALLPAPER_THEME =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.theme";
const PREF_WALLPAPERS_CUSTOM_WALLPAPER_POSITION =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.position";

const PREF_SELECTED_WALLPAPER =
  "browser.newtabpage.activity-stream.newtabWallpapers.wallpaper";

const PREF_INITIAL_WALLPAPER =
  "browser.newtabpage.activity-stream.newtabWallpapers.initialWallpaper";

const PREF_WALLPAPERS_USER_ENABLED_MIGRATED =
  "browser.newtabpage.activity-stream.newtabWallpapers.user.enabled.migrated";

// Held by everything that writes under <profile>/wallpaper/ or moves the uuid
// pref: upload, delete, apply, migrate, rescue and cleanup.
const WALLPAPER_FILE_LOCK = "newtab-wallpaper-file";

export class WallpaperFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this.loaded = false;
    // Applying moves several prefs, and this feed reacts to two of them. Held
    // while they move, so the page is told once at the end instead of once per
    // pref, each time with a directory sweep behind it. Counted, because an
    // upload holds it across the whole write and an apply can land inside that.
    this.applyingWallpaper = 0;
    this.wallpaperClient = null;
    this._onSync = this.onSync.bind(this);
  }

  get wallpaperDirectory() {
    return PathUtils.join(PathUtils.profileDir, "wallpaper");
  }

  get libraryEnabled() {
    return isWallpaperLibraryEnabled(this.store?.getState()?.Prefs?.values);
  }

  // Where every saved image lives. The page loads them from here, except on
  // a host too old to resolve a path, which reads a copy one level up.
  get libraryDirectory() {
    return PathUtils.join(this.wallpaperDirectory, LIBRARY_DIRECTORY_NAME);
  }

  /**
   * This thin wrapper around global.fetch makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   *
   * @param {Parameters<typeof fetch>} args
   */
  fetch(...args) {
    return fetch(...args);
  }

  /**
   * Thin wrapper around the New Tab strings, so tests can stand in for them.
   */
  formatString(id) {
    return lazy.gNewTabStrings.formatValue(id);
  }

  /**
   * This thin wrapper around lazy.RemoteSettings makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   *
   * @param {Parameters<typeof lazy.RemoteSettings>} args
   */
  RemoteSettings(...args) {
    return lazy.RemoteSettings(...args);
  }

  /**
   * This thin wrapper around IOUtils.write lets tests simulate write failures.
   *
   * @param {Parameters<typeof IOUtils.write>} args
   */
  writeFile(...args) {
    return IOUtils.write(...args);
  }

  /**
   * This thin wrapper around IOUtils.writeJSON lets tests simulate a failure
   * writing an image's details.
   *
   * @param {Parameters<typeof IOUtils.writeJSON>} args
   */
  writeJSON(...args) {
    return IOUtils.writeJSON(...args);
  }

  /**
   * This thin wrapper around IOUtils.remove lets tests interleave with a
   * deletion that is already in flight.
   *
   * @param {Parameters<typeof IOUtils.remove>} args
   */
  removeFile(...args) {
    return IOUtils.remove(...args);
  }

  /**
   * Thin wrapper around IOUtils.copy, so tests can make a copy fail.
   *
   * @param {Parameters<typeof IOUtils.copy>} args
   */
  copyFile(...args) {
    return IOUtils.copy(...args);
  }

  async wallpaperSetup(isStartup = false) {
    const wallpapersEnabled = Services.prefs.getBoolPref(
      PREF_WALLPAPERS_ENABLED
    );

    if (wallpapersEnabled) {
      // newtabWallpapers.user.enabled defaults to false, but users who already
      // had a wallpaper selected before this pref was introduced should have
      // it set to true so their wallpaper remains visible after updating.
      //
      // PREF_WALLPAPERS_USER_ENABLED_MIGRATED tracks whether this one-time
      // check has already run. Without it, wallpaperSetup (which is called on
      // startup and on Remote Settings sync) would re-run the check every time,
      // undoing any explicit toggle-off the user makes.
      //
      // This is a one-time data migration, not a train-hop compatibility shim,
      // so it is intentionally not gated on a release version: a profile can
      // update directly from a pre-migration version to a much later one
      // (Firefox updates may skip intermediate major versions), and must still
      // run this check the first time the new code executes.
      if (
        !Services.prefs.getBoolPref(
          PREF_WALLPAPERS_USER_ENABLED_MIGRATED,
          false
        )
      ) {
        // Mark as done immediately so subsequent wallpaperSetup calls skip this.
        Services.prefs.setBoolPref(PREF_WALLPAPERS_USER_ENABLED_MIGRATED, true);
        const selectedWallpaper = Services.prefs.getStringPref(
          PREF_SELECTED_WALLPAPER,
          ""
        );
        if (selectedWallpaper) {
          this.store.dispatch(
            ac.SetPref("newtabWallpapers.user.enabled", true)
          );
        }
      }

      if (!this.wallpaperClient) {
        // getting collection
        this.wallpaperClient = this.RemoteSettings(
          WALLPAPER_REMOTE_SETTINGS_COLLECTION_V2
        );
      }

      this.wallpaperClient.on("sync", this._onSync);
      // Awaited so callers can tell when the library is ready. The applied
      // wallpaper goes out on the first line of it either way.
      await this.updateWallpapers(isStartup);
    }
  }

  async wallpaperTeardown() {
    if (this._onSync) {
      this.wallpaperClient?.off("sync", this._onSync);
    }
    this.loaded = false;
    this.wallpaperClient = null;
  }

  async onSync(event) {
    await this.rescueRetiredWallpaper(
      event?.data?.deleted,
      event?.data?.current
    );
    this.wallpaperTeardown();
    await this.wallpaperSetup(false /* isStartup */);
  }

  /**
   * Keeps a shipped wallpaper someone is using when its record is retired, by
   * copying it into their own folder. Takes this sync's deleted and current records.
   */
  async rescueRetiredWallpaper(deletedRecords, currentRecords) {
    if (!deletedRecords?.length) {
      return;
    }

    const selectedWallpaper = this.#shownWallpaperTitle();
    if (!selectedWallpaper || selectedWallpaper === "custom") {
      return;
    }

    // Republishing a collection deletes and recreates its records, so only
    // treat this as a retirement when nothing with that title came back.
    if (currentRecords?.some(record => record.title === selectedWallpaper)) {
      return;
    }

    // Only an image can be copied into the folder. A retired solid color has
    // no file to keep.
    const record = deletedRecords.find(
      deleted => deleted.title === selectedWallpaper && deleted.attachment
    );
    if (!record) {
      return;
    }

    // Download and read the name before taking the lock, so an upload or a
    // delete never waits on the network or on string loading.
    const image = await this.#downloadRetiredWallpaper(record);
    if (!image) {
      return;
    }
    const name = await this.#retiredWallpaperName(record);

    try {
      await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#saveRetiredWallpaper(record, image, name)
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
    }
  }

  async #downloadRetiredWallpaper(record) {
    try {
      const baseAttachmentURL = await lazy.Utils.baseAttachmentsURL();
      const response = await this.fetch(
        `${baseAttachmentURL}${record.attachment.location}`
      );
      const contentType = response.headers?.get?.("content-type") || "";
      if (!response.ok || !contentType.startsWith("image/")) {
        console.error(
          `Retired wallpaper fetch not usable ` +
            `(status ${response.status}, type "${contentType}")`
        );
        return null;
      }
      return {
        bytes: new Uint8Array(await response.arrayBuffer()),
        mimeType: contentType.split(";")[0].trim(),
      };
    } catch (error) {
      console.error("Failed to download a retired wallpaper:", error);
      return null;
    }
  }

  /**
   * The name in the current locale, read now because the string can leave
   * Firefox long before the image does.
   *
   * @param {object} record - The retired Remote Settings record.
   * @returns {Promise<string>} The localized name, or the record title when
   *   there is no string or it cannot be read.
   */
  async #retiredWallpaperName(record) {
    if (record.fluent_id) {
      try {
        const name = await this.formatString(record.fluent_id);
        if (name) {
          return name;
        }
      } catch (error) {
        console.error("Could not read the retired wallpaper's name:", error);
      }
    }
    return record.title || "";
  }

  /**
   * Writes a retired wallpaper into the library and, if it is still the one
   * applied, applies the copy. Callers hold the file lock.
   *
   * @param {object} record - The retired Remote Settings record.
   * @param {{ bytes: Uint8Array, mimeType: string }} image - Its downloaded
   *   image.
   * @param {string} name - What to call it, already localized.
   */
  async #saveRetiredWallpaper(record, image, name) {
    try {
      const theme = record.theme === "dark" ? "dark" : "light";
      const uuid = Services.uuid.generateUUID().toString().slice(1, -1);
      const filename = buildSavedWallpaperFilename({
        type: WALLPAPER_TYPES.Builtin,
        theme,
        position: record.background_position,
        // A rescued wallpaper is one of their images now, so it is numbered
        // like the rest.
        number: this.#takeNextWallpaperNumber(),
        uuid,
      });

      const libraryDir = this.libraryDirectory;
      await IOUtils.makeDirectory(libraryDir, { ignoreExisting: true });

      // Keep the wallpaper's own name so screen readers can tell one rescued
      // image from another. Written first, so a break leaves a clearable file.
      const details = { ...(record.attribution ?? {}) };
      if (name) {
        details.fallbackName = name;
      }
      if (Object.keys(details).length) {
        const detailsPath = PathUtils.join(
          libraryDir,
          getDetailsFilename(filename)
        );
        await this.writeJSON(detailsPath, details, {
          tmpPath: `${detailsPath}.tmp`,
        });
      }

      const filePath = PathUtils.join(libraryDir, filename);
      await this.writeFile(filePath, image.bytes, {
        tmpPath: `${filePath}.tmp`,
      });

      // Send the library first: it carries the crop and the credit, so applying
      // the wallpaper before it arrives paints centered and uncredited.
      await this.broadcastWallpaperLibrary();

      // The download took time. Leave the image saved either way, but only take
      // over the selection if it is still the wallpaper that was retired.
      if (this.#shownWallpaperTitle() !== record.title) {
        return;
      }

      if (!(await this.#copyAppliedWallpaper(filename))) {
        return;
      }

      Services.prefs.setStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        filename
      );
      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.theme", theme)
      );
      this.store.dispatch(ac.SetPref("newtabWallpapers.wallpaper", "custom"));
      this.store.dispatch(ac.SetPref("newtabWallpapers.initialWallpaper", ""));
    } catch (error) {
      console.error("Failed to keep a retired wallpaper:", error);
    }
  }

  // What the page shows: the chosen wallpaper, or the one an experiment set.
  #shownWallpaperTitle() {
    return (
      Services.prefs.getStringPref(PREF_SELECTED_WALLPAPER, "") ||
      Services.prefs.getStringPref(PREF_INITIAL_WALLPAPER, "")
    );
  }

  async updateWallpapers(isStartup = false) {
    // Send the applied wallpaper before touching the file system, so the page
    // has it as early as it did before the library existed. Not mid-upload,
    // where the applied URL is still the picture being replaced.
    if (!this.applyingWallpaper) {
      this.broadcastAppliedWallpaper();
    }

    if ((await this.migrateWallpaperLibrary()) && !this.applyingWallpaper) {
      this.broadcastAppliedWallpaper();
    }

    // retrieving all records in collection
    let records;
    try {
      records = await this.wallpaperClient.get();
    } catch (error) {
      // Fall through so the custom-wallpaper upload entry is still surfaced.
      console.error(
        "Error fetching wallpaper records from remote settings",
        error
      );
      records = [];
    }

    const customWallpaperEnabled = Services.prefs.getBoolPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_ENABLED
    );

    let baseAttachmentURL = "";
    if (records.length) {
      try {
        baseAttachmentURL = await lazy.Utils.baseAttachmentsURL();
      } catch (error) {
        // Same as a failed record fetch: carry on so the categories and the
        // saved images below still reach the page.
        console.error("Error fetching the wallpaper attachments URL", error);
      }
    }

    const wallpapers = [
      ...records.map(record => {
        return {
          ...record,
          ...(record.attachment
            ? {
                wallpaperUrl: `${baseAttachmentURL}${record.attachment.location}`,
              }
            : {}),
          background_position: record.background_position || "center",
          category: record.category || "",
          order: record.order || 0,
          thumbnail: record.thumbnail || null,
        };
      }),
    ];

    const CATEGORY_ORDER = [
      "custom-wallpaper",
      "firefox",
      "abstracts",
      "celestial",
      "predecessor",
      "photographs",
      "solid-colors",
    ];

    const categories = [
      ...new Set(
        wallpapers.map(wallpaper => wallpaper.category).filter(Boolean)
      ),
      ...(customWallpaperEnabled ? ["custom-wallpaper"] : []), // Conditionally add custom wallpaper input
    ].sort((a, b) => {
      const aIndex = CATEGORY_ORDER.indexOf(a);
      const bIndex = CATEGORY_ORDER.indexOf(b);
      const aOrder = aIndex === -1 ? CATEGORY_ORDER.length : aIndex;
      const bOrder = bIndex === -1 ? CATEGORY_ORDER.length : bIndex;
      return aOrder - bOrder;
    });

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_SET,
        data: wallpapers,
        meta: {
          isStartup,
        },
      })
    );

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_CATEGORY_SET,
        data: categories,
        meta: {
          isStartup,
        },
      })
    );

    await this.broadcastWallpaperLibrary(isStartup);

    if (isStartup) {
      // Nothing else sweeps unless an upload happens, so a .tmp file or a
      // credit whose image is gone would sit there forever.
      await this.cleanWallpaperDirectory();
    }
  }

  initHighlightCounter() {
    let counter = Services.prefs.getIntPref(
      PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER
    );

    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT,
        data: {
          value: counter,
        },
      })
    );
  }

  wallpaperSeenEvent() {
    let counter = Services.prefs.getIntPref(
      PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER
    );

    const newCount = counter + 1;

    this.store.dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: {
          name: "newtabWallpapers.highlightSeenCounter",
          value: newCount,
        },
      })
    );

    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT,
        data: {
          value: newCount,
        },
      })
    );
  }

  /**
   * Saves an image into the library and applies it, under the file lock.
   *
   * @param {Blob} file - The image.
   * @param {string} wallpaperTheme - "light" or "dark", whichever the image
   *   reads as. Stored in the filename so the page can set its text color.
   * @param {string} [type] - One of WALLPAPER_TYPES. Defaults to Custom.
   * @param {object} [info] - Extra details about the image.
   * @param {string} [info.name] - A name to show in the picker, for a kept
   *   Picture of the Day. Uploads have none and are known by their number.
   * @param {string} [info.publishedDate] - The day a Picture of the Day was
   *   published, so setting the same picture twice applies the saved copy.
   * @param {string|null} [target] - The id of the content port that asked, so
   *   the save can be reported against it.
   * @returns {Promise<string|null>} The path of the image now applied, or null
   *   when the save failed.
   */
  async wallpaperUpload(
    file,
    wallpaperTheme,
    type = WALLPAPER_TYPES.Custom,
    info = {},
    target = null
  ) {
    if (!Blob.isInstance(file)) {
      console.error("wallpaperUpload: file is not a Blob");
      return null;
    }
    if (wallpaperTheme !== "dark" && wallpaperTheme !== "light") {
      console.error("wallpaperUpload: invalid theme");
      return null;
    }
    if (
      type !== WALLPAPER_TYPES.Custom &&
      type !== WALLPAPER_TYPES.PictureOfTheDay
    ) {
      console.error("wallpaperUpload: invalid type");
      return null;
    }
    // The picker selects "custom" while this is still writing, but the new URL
    // only exists at the end. Drop the old one, then hold so the pref answer
    // below cannot put it straight back.
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_CUSTOM_SET,
        data: null,
      })
    );
    this.applyingWallpaper++;

    let savedPath = null;
    try {
      savedPath = await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#writeWallpaper(file, wallpaperTheme, type, info, target)
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
    } finally {
      this.applyingWallpaper--;
    }

    // Nothing saved, so put back what is still applied. Only once the last
    // upload is done, or that URL is one another write is about to replace.
    if (!savedPath && !this.applyingWallpaper) {
      this.broadcastAppliedWallpaper();
    }
    return savedPath;
  }

  /**
   * Does the work of wallpaperUpload. The caller holds the file lock.
   *
   * @param {Blob} file - The image.
   * @param {string} wallpaperTheme - "light" or "dark".
   * @param {string} type - One of WALLPAPER_TYPES.
   * @param {object} info - The name and publishedDate described on
   *   wallpaperUpload, either of which may be missing.
   * @param {string|null} [target] - The id of the content port that asked, so
   *   the save can be reported against it.
   * @returns {Promise<string|null>} The path of the image now applied, or null
   *   when the save failed.
   */
  async #writeWallpaper(file, wallpaperTheme, type, info, target = null) {
    try {
      const wallpaperDir = this.wallpaperDirectory;

      // create wallpaper directory if it does not exist
      await IOUtils.makeDirectory(wallpaperDir, { ignoreExisting: true });

      // Clear out anything left by an earlier version or an interrupted write.
      await this.#sweepWallpaperDirectory();

      // One picture a day, and "Set wallpaper" can be pressed more than once
      // on it. Applying the copy already saved beats keeping two of the same.
      if (type === WALLPAPER_TYPES.PictureOfTheDay && info.publishedDate) {
        const existing = (await this.getSavedWallpapers()).find(
          wallpaper =>
            wallpaper.type === WALLPAPER_TYPES.PictureOfTheDay &&
            wallpaper.publishedDate === info.publishedDate
        );
        // Nothing is saved here, the copy already in the folder is applied,
        // so this reports an apply rather than another save. A copy that
        // cannot be applied is saved fresh instead.
        if (
          existing &&
          (await this.#applySavedWallpaper(existing.filename, target))
        ) {
          return PathUtils.join(this.libraryDirectory, existing.filename);
        }
      }

      const uuid = Services.uuid.generateUUID().toString().slice(1, -1);
      const number = this.#takeNextWallpaperNumber();
      const filename = buildSavedWallpaperFilename({
        type,
        number,
        theme: wallpaperTheme,
        position: "center",
        uuid,
      });

      // convert to Uint8Array for IOUtils
      const arrayBuffer = await file.arrayBuffer();
      const uint8Array = new Uint8Array(arrayBuffer);

      const libraryDir = this.libraryDirectory;
      await IOUtils.makeDirectory(libraryDir, { ignoreExisting: true });

      // What screen readers read for this image. Kept even while the library is
      // off, because turning it on later cannot recover a name nothing saved.
      // A Picture of the Day has no file of its own, so it carries its
      // description.
      const details =
        type === WALLPAPER_TYPES.PictureOfTheDay
          ? {
              fallbackName: sanitizeSavedName(info.name),
              // Which day's picture this is. Applying it again restores the
              // widget's "already set" state instead of clearing it.
              publishedDate: info.publishedDate || "",
            }
          : {};
      if (Object.values(details).some(Boolean)) {
        const detailsPath = PathUtils.join(
          libraryDir,
          getDetailsFilename(filename)
        );
        await this.writeJSON(detailsPath, details, {
          tmpPath: `${detailsPath}.tmp`,
        });
      }

      const filePath = PathUtils.join(libraryDir, filename);
      await this.writeFile(filePath, uint8Array, {
        tmpPath: `${filePath}.tmp`,
      });

      const replaced = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );

      if (!(await this.#copyAppliedWallpaper(filename))) {
        return null;
      }

      // Point the pref at the new file only once it exists, so a failed write
      // leaves the previous image referenced and on disk.
      Services.prefs.setStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        filename
      );

      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: getWallpaperURL(filename, this.libraryEnabled),
        })
      );

      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.theme", wallpaperTheme)
      );

      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.position", "center")
      );

      // Without the library there is nowhere to see or remove an old image, so
      // keeping it would just grow the folder.
      if (
        replaced &&
        replaced !== filename &&
        parseWallpaperFilename(replaced).kind === "saved" &&
        !this.libraryEnabled
      ) {
        await this.#removeLibraryEntry(replaced);
      }

      // The pref now names the new file, so the sweep spares it and clears the
      // stale Picture of the Day image along with anything else left over.
      await this.#sweepWallpaperDirectory();

      this.#recordSavedWallpaperEvent(at.WALLPAPER_SAVED_ADDED, target, {
        wallpaper_source: type,
        saved_wallpaper_count: (await this.getSavedWallpapers()).length,
      });
      await this.broadcastWallpaperLibrary();

      return filePath;
    } catch (error) {
      console.error("Error saving wallpaper:", error);
      return null;
    }
  }

  /**
   * Reserves the next number and moves the counter on straight away, so two
   * saves can never be given the same one. A save that fails afterwards leaves
   * a gap, which is fine: the number only has to be unique, not consecutive.
   *
   * @returns {number} The number the next saved image gets.
   */
  #takeNextWallpaperNumber() {
    const next = Services.prefs.getIntPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_NEXT_NUMBER,
      1
    );
    Services.prefs.setIntPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_NEXT_NUMBER,
      next + 1
    );
    return next;
  }

  /**
   * @backward-compat { version 158 }
   *
   * With the library off, a host-only address is all an older handler
   * resolves, so the applied image is copied to the top of the wallpaper
   * folder for the page to read. Goes once 158 reaches Release.
   *
   * @param {string} filename - The library image to copy up.
   * @returns {Promise<boolean>} Whether the page can load the image.
   */
  async #copyAppliedWallpaper(filename) {
    if (this.libraryEnabled) {
      return true;
    }
    try {
      await this.copyFile(
        PathUtils.join(this.libraryDirectory, filename),
        PathUtils.join(this.wallpaperDirectory, filename)
      );
      return true;
    } catch (error) {
      console.error("Failed to place the applied wallpaper:", error);
      return false;
    }
  }

  /**
   * Drops one saved image and the extra files that belong to it. Resolves to
   * true only if this call removed the image, so a second tab confirming the
   * same dialog does not clear the applied state or report a removal twice.
   *
   * @param {string} filename - The saved image to remove.
   * @returns {Promise<boolean>} Whether this call removed the image.
   */
  async #removeLibraryEntry(filename) {
    const libraryDir = this.libraryDirectory;

    const imagePath = PathUtils.join(libraryDir, filename);
    if (!(await IOUtils.exists(imagePath))) {
      return false;
    }

    // The image itself decides whether the removal happened. Carrying on after
    // it fails would clear the applied state and drop the details, leaving the
    // picture on disk to come back nameless on the next read.
    try {
      await this.removeFile(imagePath, { ignoreAbsent: true });
    } catch (error) {
      console.error("Failed to remove a saved wallpaper:", error);
      return false;
    }

    // The thumbnail is from before the picker loaded images by address. A
    // profile saved back then still has one beside every image.
    for (const name of [
      getDetailsFilename(filename),
      getThumbnailFilename(filename),
    ]) {
      try {
        await this.removeFile(PathUtils.join(libraryDir, name), {
          ignoreAbsent: true,
        });
      } catch (error) {
        // The image is gone, so a leftover extra file is the sweep's problem.
        console.error("Failed to remove a library file:", error);
      }
    }

    return true;
  }

  /**
   * Applies a saved image, under the file lock. The filename comes from
   * content, so it is checked against the library rather than trusted.
   *
   * @param {string} filename - The library image to apply.
   * @param {string|null} [target] - The id of the content port that asked, so
   *   the apply can be reported against it.
   * @returns {Promise<boolean>} Whether the image is now applied.
   */
  async applySavedWallpaper(filename, target) {
    try {
      return await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#applySavedWallpaper(filename, target)
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
      return false;
    }
  }

  /**
   * Points every pref at the image and tells content. Callers hold the file
   * lock. With the library off it also copies the image up, which is the only
   * place an older handler can address it.
   *
   * @param {string} filename - The library image to apply.
   * @param {string|null} [target] - The id of the content port that asked, so
   *   the apply can be reported against it.
   * @returns {Promise<boolean>} False when the file is not a saved image, is
   *   not in the library or could not be copied.
   */
  async #applySavedWallpaper(filename, target) {
    const parsed = parseWallpaperFilename(filename);
    if (parsed.kind !== "saved") {
      console.error("Refusing to apply a file that is not a saved wallpaper");
      return false;
    }

    if (
      !(await IOUtils.exists(PathUtils.join(this.libraryDirectory, filename)))
    ) {
      console.error("Refusing to apply a wallpaper that is not in the library");
      return false;
    }

    if (!(await this.#copyAppliedWallpaper(filename))) {
      return false;
    }

    // Read before the flag goes up: while it is up, wallpaper pref changes are
    // not sent to content, so nothing may await in between.
    // Re-applying the picture that is showing puts the widget's "already set"
    // state back. Anything else is not that picture, so the state goes.
    const detailsFilename = getDetailsFilename(filename);
    let publishedDate = "";
    if (parsed.type === WALLPAPER_TYPES.PictureOfTheDay) {
      const details = (await IOUtils.exists(
        PathUtils.join(this.libraryDirectory, detailsFilename)
      ))
        ? await this.#readDetails(detailsFilename)
        : null;
      publishedDate = details?.publishedDate || "";
    }

    this.applyingWallpaper++;

    // In a finally: if any of these throws, leaving the hold up would stop the
    // page being told about a wallpaper change for the rest of the session.
    try {
      Services.prefs.setStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        filename
      );
      // Before the prefs below, which reach the page as they change. Turning
      // wallpapers back on would otherwise paint the URL it still holds.
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: getWallpaperURL(filename, this.libraryEnabled),
        })
      );
      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.theme", parsed.theme)
      );
      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.position", parsed.position)
      );

      // Set here rather than in the picker, so a refusal above leaves the page
      // on the wallpaper it already had instead of naming one it cannot show.
      this.store.dispatch(ac.SetPref("newtabWallpapers.wallpaper", "custom"));
      this.store.dispatch(ac.SetPref("newtabWallpapers.initialWallpaper", ""));
      this.store.dispatch(ac.SetPref("newtabWallpapers.user.enabled", true));

      this.store.dispatch(
        ac.SetPref("widgets.pictureOfTheDay.wallpaperActive", publishedDate)
      );
    } finally {
      this.applyingWallpaper--;
    }

    await this.#sweepWallpaperDirectory();

    const saved = await this.getSavedWallpapers();
    this.#recordSavedWallpaperEvent(at.WALLPAPER_SAVED_APPLIED, target, {
      saved_wallpaper_count: saved.length,
      wallpaper_source: parsed.type,
    });

    return true;
  }

  /**
   * Tells the page which saved wallpaper to show, worked out from the prefs
   * rather than from anything held in memory, so a restart restores it.
   */
  broadcastAppliedWallpaper() {
    const filename = Services.prefs.getStringPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
      ""
    );
    const applied = parseWallpaperFilename(filename);
    const selectedWallpaper = Services.prefs.getStringPref(
      PREF_SELECTED_WALLPAPER,
      ""
    );

    // "legacy" is a wallpaper waiting to be migrated. Its file is still there
    // under the old name, so it can be shown while that happens.
    const isApplied = applied.kind === "saved" || applied.kind === "legacy";

    if (selectedWallpaper !== "custom" || !isApplied) {
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: null,
        })
      );
      return;
    }

    // The filename is what the file actually is, so it wins over a stale theme
    // pref. A legacy name carries no theme, so the pref stands until migration.
    if (
      applied.theme &&
      applied.theme !==
        Services.prefs.getStringPref(PREF_WALLPAPERS_CUSTOM_WALLPAPER_THEME, "")
    ) {
      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.theme", applied.theme)
      );
    }

    // The library carries this too, but that waits on a network fetch. A
    // rescued wallpaper would show centered until it arrived.
    if (
      applied.position &&
      applied.position !==
        Services.prefs.getStringPref(
          PREF_WALLPAPERS_CUSTOM_WALLPAPER_POSITION,
          ""
        )
    ) {
      this.store.dispatch(
        ac.SetPref(
          "newtabWallpapers.customWallpaper.position",
          applied.position
        )
      );
    }

    // A legacy name is still at the top of the wallpaper folder, so address it
    // there until the migration moves it and sends the new name.
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_CUSTOM_SET,
        data: getWallpaperURL(
          filename,
          this.libraryEnabled && applied.kind === "saved"
        ),
      })
    );
  }

  /**
   * Sends the list of saved images to every open page, or to one that asked.
   *
   * @param {boolean} [isStartup] - Whether this is the broadcast made at
   *   startup.
   * @param {string|null} [target] - The page to reply to, instead of
   *   broadcasting.
   */
  async broadcastWallpaperLibrary(isStartup = false, target = null) {
    const saved = await this.getSavedWallpapers();

    // A page restored from the startup cache never receives a broadcast, only
    // a direct reply, so it asks and is answered on its own.
    if (target) {
      this.store.dispatch(
        ac.OnlyToOneContent(
          { type: at.WALLPAPERS_CUSTOM_LIBRARY_SET, data: saved },
          target
        )
      );
      return;
    }

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_CUSTOM_LIBRARY_SET,
        data: saved,
        meta: {
          isStartup,
        },
      })
    );
  }

  /**
   * Lists the saved wallpapers, newest first, with the filename breaking ties.
   * A kept Picture of the Day is one of them.
   *
   * @returns {Promise<object[]>} One entry per image, each carrying the parsed
   *   filename fields plus whatever its details file held.
   */
  async getSavedWallpapers() {
    try {
      const children = await IOUtils.getChildren(this.libraryDirectory, {
        ignoreAbsent: true,
      });

      const filenames = new Set(children.map(path => PathUtils.filename(path)));

      const wallpapers = [];
      for (const path of children) {
        const filename = PathUtils.filename(path);
        const parsed = parseWallpaperFilename(filename);
        if (parsed.kind !== "saved") {
          continue;
        }
        // Per file, so one unreadable entry does not lose the whole library.
        try {
          const { lastModified, type } = await IOUtils.stat(path);
          if (type !== "regular") {
            continue;
          }
          const detailsFilename = getDetailsFilename(filename);
          const details = filenames.has(detailsFilename)
            ? await this.#readDetails(detailsFilename)
            : null;
          // An image's extra file can hold what it is called and, for a
          // rescued shipped wallpaper, its photographer credit as well.
          const {
            fallbackName = "",
            publishedDate = "",
            ...credit
          } = details ?? {};
          wallpapers.push({
            filename,
            number: parsed.number,
            type: parsed.type,
            theme: parsed.theme,
            position: parsed.position,
            lastModified,
            fallbackName,
            publishedDate,
            attribution: Object.keys(credit).length ? credit : null,
          });
        } catch (error) {
          console.error("Failed to read a saved wallpaper:", error);
        }
      }

      return wallpapers.sort(
        (a, b) =>
          b.lastModified - a.lastModified ||
          a.filename.localeCompare(b.filename)
      );
    } catch (error) {
      console.error("Failed to list the saved wallpapers:", error);
      return [];
    }
  }

  /**
   * Reads the details file kept beside a saved image.
   *
   * @param {string} detailsFilename - The .txt file next to the image.
   * @returns {Promise<object|null>} Its contents, or null when the file is
   *   missing or unreadable. The image is still listed either way, just
   *   without a name or credit.
   */
  async #readDetails(detailsFilename) {
    try {
      return await IOUtils.readJSON(
        PathUtils.join(this.libraryDirectory, detailsFilename)
      );
    } catch (error) {
      console.error("Failed to read a wallpaper's details:", error);
      return null;
    }
  }

  /**
   * Moves anything saved in the wallpaper folder into the library, renaming
   * the one written before it existed. Takes the file lock.
   *
   * @returns {Promise<boolean>} Whether the applied wallpaper's name changed,
   *   in which case the page needs telling again.
   */
  async migrateWallpaperLibrary() {
    try {
      return await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#migrateWallpaperLibrary()
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
      return false;
    }
  }

  /**
   * Brings a folder written by an earlier New Tab up to date: moves loose v1-
   * images and their .txt files down into library/, and adopts the single
   * pre-library image under a v1- name. Resolves to true if the applied image was
   * renamed, so the page can be told its new address.
   */
  async #migrateWallpaperLibrary() {
    let appliedRenamed = false;

    try {
      const wallpaperDir = this.wallpaperDirectory;
      const libraryDir = this.libraryDirectory;
      const children = await IOUtils.getChildren(wallpaperDir, {
        ignoreAbsent: true,
      });

      const applied = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );

      const moved = [];

      for (const path of children) {
        const filename = PathUtils.filename(path);
        const parsed = parseWallpaperFilename(filename);

        // A saved image or its credit, left in the wallpaper folder by a version
        // that kept the library there.
        if (parsed.kind === "saved" || parsed.kind === "details") {
          // A name the library already holds is the pre-158 copy of it, not a
          // stray. Moving it would overwrite the original on every startup.
          // The sweep clears a flat file that is genuinely left over.
          if (await IOUtils.exists(PathUtils.join(libraryDir, filename))) {
            continue;
          }
          await IOUtils.makeDirectory(libraryDir, { ignoreExisting: true });
          try {
            await IOUtils.move(path, PathUtils.join(libraryDir, filename));
            if (parsed.kind === "saved") {
              moved.push(filename);
            }
          } catch (error) {
            console.error(
              "Failed to move a wallpaper into the library:",
              error
            );
          }
          continue;
        }

        // The single bare-UUID upload from before the library. Only the applied
        // one, since the rest were already leftovers.
        if (parsed.kind === "legacy" && filename === applied) {
          const renamed = await this.#adoptLooseWallpaper(filename, parsed);
          if (renamed) {
            moved.push(renamed);
            appliedRenamed = true;
          }
        }
      }
      // @backward-compat { version 158 } On a pre-158 host the page reads the
      // top-level copy, so put it back when it is missing.
      // #copyAppliedWallpaper no-ops otherwise.
      const appliedNow = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );
      if (
        parseWallpaperFilename(appliedNow).kind === "saved" &&
        !(await IOUtils.exists(PathUtils.join(wallpaperDir, appliedNow))) &&
        (await IOUtils.exists(PathUtils.join(libraryDir, appliedNow)))
      ) {
        await this.#copyAppliedWallpaper(appliedNow);
      }

      if (moved.length) {
        await this.#sweepWallpaperDirectory();
      }
    } catch (error) {
      console.error("Failed to migrate the wallpaper library:", error);
    }

    return appliedRenamed;
  }

  /**
   * Gives a wallpaper written outside the library a library name and moves it
   * in.
   *
   * @param {string} looseFilename - The file as it sits in the wallpaper folder.
   * @param {object} parsed - What parseWallpaperFilename made of that name.
   * @returns {Promise<string|null>} The new filename, or null if it could not
   *   be moved.
   */
  async #adoptLooseWallpaper(looseFilename, parsed) {
    try {
      // A bare UUID carries no theme, so the pref is all there is.
      const theme =
        Services.prefs.getStringPref(
          PREF_WALLPAPERS_CUSTOM_WALLPAPER_THEME,
          ""
        ) === "dark"
          ? "dark"
          : "light";

      const filename = buildSavedWallpaperFilename({
        type: WALLPAPER_TYPES.Custom,
        theme,
        position: "center",
        // An image carried over from before the library still gets a number,
        // so everything in the folder is named the same way.
        number: this.#takeNextWallpaperNumber(),
        uuid: parsed.uuid,
      });

      await IOUtils.makeDirectory(this.libraryDirectory, {
        ignoreExisting: true,
      });

      // The old file stays until the new name is in both places, so the page
      // always has something it can load. The pref moves last.
      const loosePath = PathUtils.join(this.wallpaperDirectory, looseFilename);
      const libraryPath = PathUtils.join(this.libraryDirectory, filename);
      await IOUtils.copy(loosePath, libraryPath);
      if (!(await this.#copyAppliedWallpaper(filename))) {
        await IOUtils.remove(libraryPath, { ignoreAbsent: true });
        return null;
      }
      Services.prefs.setStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        filename
      );
      await IOUtils.remove(loosePath, { ignoreAbsent: true });

      return filename;
    } catch (error) {
      console.error("Failed to migrate a loose wallpaper:", error);
      return null;
    }
  }

  /**
   * Removes .tmp files, leftover copies and orphans from the wallpaper folder.
   * The library is a folder so it survives. Callers hold the file lock.
   */
  async #sweepWallpaperDirectory() {
    try {
      const wallpaperDir = this.wallpaperDirectory;
      const libraryDir = this.libraryDirectory;
      const children = await IOUtils.getChildren(wallpaperDir, {
        ignoreAbsent: true,
      });

      const appliedFilename = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );
      for (const path of children) {
        const filename = PathUtils.filename(path);
        const parsed = parseWallpaperFilename(filename);

        // Every saved image lives in the library now, so anything recognized
        // up here is a leftover, including the copy older versions kept. The
        // exception is an applied image with no library counterpart, which is
        // waiting for the migration to adopt it.
        if (parsed.kind === "unknown") {
          continue;
        }
        if (
          filename === appliedFilename &&
          (!this.libraryEnabled ||
            !(await IOUtils.exists(PathUtils.join(libraryDir, filename))))
        ) {
          continue;
        }

        // Per file, so one bad entry does not stop the rest of the sweep.
        try {
          const { type } = await IOUtils.stat(path);
          if (type === "regular") {
            await IOUtils.remove(path, { ignoreAbsent: true });
          }
        } catch (error) {
          console.error("Failed to remove an orphaned wallpaper:", error);
        }
      }
    } catch (error) {
      console.error("Failed to clean up the wallpaper directory:", error);
    }

    await this.#sweepLibraryDirectory();
  }

  /**
   * Tidies the library. Only ever removes a .tmp, a thumbnail, or a details
   * file whose image is gone, never an image: a name this fails to recognize
   * is someone's picture. Thumbnails all go, since the picker loads the image
   * itself now and profiles saved before that still have them.
   */
  async #sweepLibraryDirectory() {
    try {
      const children = await IOUtils.getChildren(this.libraryDirectory, {
        ignoreAbsent: true,
      });
      const filenames = new Set(children.map(path => PathUtils.filename(path)));

      for (const path of children) {
        const parsed = parseWallpaperFilename(PathUtils.filename(path));
        const isStrandedDetails =
          parsed.kind === "details" && !filenames.has(parsed.imageFilename);

        if (
          parsed.kind !== "temporary" &&
          parsed.kind !== "thumbnail" &&
          !isStrandedDetails
        ) {
          continue;
        }

        try {
          const { type } = await IOUtils.stat(path);
          if (type === "regular") {
            await IOUtils.remove(path, { ignoreAbsent: true });
          }
        } catch (error) {
          console.error("Failed to tidy the wallpaper library:", error);
        }
      }
    } catch (error) {
      console.error("Failed to clean up the wallpaper library:", error);
    }
  }

  /**
   * Runs the cleanup on its own, under the file lock. Used at startup and
   * whenever the applied wallpaper changes and leaves a copy behind.
   */
  async cleanWallpaperDirectory() {
    try {
      await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#sweepWallpaperDirectory()
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
    }
  }

  // Removes one saved image. The only thing that deletes one, and only once
  // someone has confirmed it.
  async removeCustomWallpaper(filename, target) {
    try {
      await locks.request(WALLPAPER_FILE_LOCK, () =>
        this.#deleteCustomWallpaper(filename, target)
      );
    } catch (error) {
      console.error("Could not take the wallpaper file lock:", error);
    }
  }

  async #deleteCustomWallpaper(requestedFilename, target) {
    try {
      const appliedFilename = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );
      const filename = requestedFilename || appliedFilename;

      if (!filename) {
        return;
      }

      // The filename comes from content, so only a saved image can be deleted
      // this way. Everything else in the folder belongs to the sweep.
      const parsed = parseWallpaperFilename(filename);
      if (parsed.kind !== "saved") {
        console.error(
          "Refusing to remove a file that is not a saved wallpaper"
        );
        return;
      }

      if (!(await this.#removeLibraryEntry(filename))) {
        return;
      }

      // Read again rather than trusting the value from before the removal: a
      // selection can land while it is in flight and must not be cleared.
      const appliedNow = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );

      if (filename === appliedNow) {
        // Firefox before 158 cannot load library files, so the applied image
        // is also copied to the top of the wallpaper folder. Remove that copy.
        // If that fails, still clear the selection. The sweep takes it later.
        try {
          await this.removeFile(
            PathUtils.join(this.wallpaperDirectory, filename),
            { ignoreAbsent: true }
          );
        } catch (error) {
          console.error("Failed to remove the applied copy:", error);
        }

        Services.prefs.clearUserPref(PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID);

        // Both, so New Tab goes back to its default background. Clearing only
        // the selection would fall through to an experiment's wallpaper.
        this.store.dispatch(ac.SetPref("newtabWallpapers.wallpaper", ""));
        this.store.dispatch(
          ac.SetPref("newtabWallpapers.initialWallpaper", "")
        );

        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.WALLPAPERS_CUSTOM_SET,
            data: null,
          })
        );
      }

      await this.broadcastWallpaperLibrary();

      this.#recordSavedWallpaperEvent(at.WALLPAPER_SAVED_REMOVED, target, {
        saved_wallpaper_count: (await this.getSavedWallpapers()).length,
        was_applied: filename === appliedNow,
        // Off the name, the same as the save and the selection, so all three
        // can be read together by where the image came from.
        wallpaper_source: parsed.type,
      });
    } catch (error) {
      console.error("Failed to remove custom wallpaper:", error);
    }
  }

  // Telemetry for something that has already happened. Recorded here rather
  // than where it was asked for, because either can be refused. The tab is
  // passed through so the event still belongs to that page's session.
  #recordSavedWallpaperEvent(type, target, data) {
    if (!target) {
      return;
    }
    this.store.dispatch({ type, data, meta: { fromTarget: target } });
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        await this.wallpaperSetup(true /* isStartup */);
        this.initHighlightCounter();
        break;
      case at.UNINIT:
        break;
      case at.SYSTEM_TICK:
        break;
      case at.PREF_CHANGED:
        if (
          action.data.name === "newtabWallpapers.customColor.enabled" ||
          action.data.name === "newtabWallpapers.customWallpaper.enabled" ||
          action.data.name === "newtabWallpapers.enabled" ||
          // @nova-cleanup(remove-conditional): Drop this case; the wallpaper
          // setup no longer depends on the pref.
          action.data.name === "nova.enabled"
        ) {
          this.wallpaperTeardown();
          await this.wallpaperSetup(false /* isStartup */);
        }
        if (
          !this.applyingWallpaper &&
          (action.data.name === "newtabWallpapers.customWallpaper.uuid" ||
            action.data.name === "newtabWallpapers.wallpaper")
        ) {
          // Picking a saved image only changes prefs, so this is where content
          // finds out which file to show.
          this.broadcastAppliedWallpaper();
          // Whatever is applied now, a Picture of the Day image that is not it
          // is no longer needed.
          await this.cleanWallpaperDirectory();
        }
        if (action.data.name === "newtabWallpapers.highlightSeenCounter") {
          // Reset redux highlight counter to pref
          this.initHighlightCounter();
        }
        break;
      case at.WALLPAPERS_SET:
        break;
      case at.WALLPAPERS_FEATURE_HIGHLIGHT_SEEN:
        this.wallpaperSeenEvent();
        break;
      case at.WALLPAPER_UPLOAD:
        {
          const target = action.meta?.fromTarget;
          const savedPath = await this.wallpaperUpload(
            action.data.file,
            action.data.theme,
            action.data.type,
            {
              name: action.data.name,
              publishedDate: action.data.publishedDate,
            },
            target
          );
          // The reply goes only to the tab that asked. The id is for that tab:
          // it ignores an earlier save's result if a newer save has started.
          if (target && action.data.requestId) {
            this.store.dispatch(
              ac.OnlyToOneContent(
                {
                  type: at.WALLPAPER_UPLOAD_RESULT,
                  data: {
                    requestId: action.data.requestId,
                    filename: savedPath ? PathUtils.filename(savedPath) : null,
                  },
                },
                target
              )
            );
          }
        }
        break;
      case at.WALLPAPER_REMOVE_UPLOAD:
        await this.removeCustomWallpaper(
          action.data?.filename,
          action.meta?.fromTarget
        );
        break;
      case at.WALLPAPERS_CUSTOM_APPLY:
        await this.applySavedWallpaper(
          action.data?.filename,
          action.meta?.fromTarget
        );
        break;
      case at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST:
        // Behind the file lock, so an upload in flight cannot be answered
        // with the list from before it.
        await locks.request(WALLPAPER_FILE_LOCK, () =>
          this.broadcastWallpaperLibrary(false, action.meta?.fromTarget)
        );
        break;
    }
  }
}
