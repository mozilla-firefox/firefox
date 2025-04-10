/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionTypes as at } from "resource://newtab/common/Actions.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  DownloadsCommon: "resource:///modules/DownloadsCommon.sys.mjs",
  DownloadsViewUI: "resource:///modules/DownloadsViewUI.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
});

const DOWNLOAD_CHANGED_DELAY_TIME = 1000; // time in ms to delay timer for downloads changed events

export class DownloadsManager {
  constructor() {
    this._downloadData = null;
    this._store = null;
    this._downloadItems = new Map();
    this._downloadTimer = null;
  }

  setTimeout(callback, delay) {
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(callback, delay, Ci.nsITimer.TYPE_ONE_SHOT);
    return timer;
  }

  formatDownload(download) {
    let referrer = download.source.referrerInfo?.originalReferrer?.spec || null;
    return {
      hostname: new URL(download.source.url).hostname,
      url: download.source.url,
      path: download.target.path,
      title: lazy.DownloadsViewUI.getDisplayName(download),
      description:
        lazy.DownloadsViewUI.getSizeWithUnits(download) ||
        lazy.DownloadsCommon.strings.sizeUnknown,
      referrer,
      date_added: download.endTime,
    };
  }

  init(store) {
    this._store = store;
    this._downloadData = lazy.DownloadsCommon.getData(
      null /* null for non-private downloads */,
      true,
      false,
      true
    );
    this._downloadData.addView(this);
  }

  onDownloadAdded(download) {
    if (!this._downloadItems.has(download.source.url)) {
      this._downloadItems.set(download.source.url, download);

      // On startup, all existing downloads fire this notification, so debounce them
      if (this._downloadTimer) {
        this._downloadTimer.delay = DOWNLOAD_CHANGED_DELAY_TIME;
      } else {
        this._downloadTimer = this.setTimeout(() => {
          this._downloadTimer = null;
          this._store.dispatch({ type: at.DOWNLOAD_CHANGED });
        }, DOWNLOAD_CHANGED_DELAY_TIME);
      }
    }
  }

  onDownloadRemoved(download) {
    if (this._downloadItems.has(download.source.url)) {
      this._downloadItems.delete(download.source.url);
      this._store.dispatch({ type: at.DOWNLOAD_CHANGED });
    }
  }

  async getDownloads(
    threshold,
    {
      numItems = this._downloadItems.size,
      onlySucceeded = false,
      onlyExists = false,
    }
  ) {
    if (!threshold) {
      return [];
    }
    let results = [];

    // Only get downloads within the time threshold specified and sort by recency
    const downloadThreshold = Date.now() - threshold;
    let downloads = [...this._downloadItems.values()]
      .filter(download => download.endTime > downloadThreshold)
      .sort((download1, download2) => download1.endTime < download2.endTime);

    for (const download of downloads) {
      // Ignore blocked links, but allow long (data:) uris to avoid high CPU
      if (
        download.source.url.length < 10000 &&
        lazy.NewTabUtils.blockedLinks.isBlocked(download.source)
      ) {
        continue;
      }

      // Only include downloads where the file still exists
      if (onlyExists) {
        // Refresh download to ensure the 'exists' attribute is up to date
        await download.refresh();
        if (!download.target.exists) {
          continue;
        }
      }
      // Only include downloads that were completed successfully
      if (onlySucceeded) {
        if (!download.succeeded) {
          continue;
        }
      }
      const formattedDownloadForHighlights = this.formatDownload(download);
      results.push(formattedDownloadForHighlights);
      if (results.length === numItems) {
        break;
      }
    }
    return results;
  }

  uninit() {
    if (this._downloadData) {
      this._downloadData.removeView(this);
      this._downloadData = null;
    }
    if (this._downloadTimer) {
      this._downloadTimer.cancel();
      this._downloadTimer = null;
    }
  }

  onAction(action) {
    let doDownloadAction = callback => {
      let download = this._downloadItems.get(action.data.url);
      if (download) {
        callback(download);
      }
    };

    switch (action.type) {
      case at.COPY_DOWNLOAD_LINK:
        doDownloadAction(download => {
          lazy.DownloadsCommon.copyDownloadLink(download);
        });
        break;
      case at.REMOVE_DOWNLOAD_FILE:
        doDownloadAction(download => {
          lazy.DownloadsCommon.deleteDownload(download).catch(console.error);
        });
        break;
      case at.SHOW_DOWNLOAD_FILE:
        doDownloadAction(download => {
          lazy.DownloadsCommon.showDownloadedFile(
            new lazy.FileUtils.File(download.target.path)
          );
        });
        break;
      case at.OPEN_DOWNLOAD_FILE: {
        const openWhere = lazy.BrowserUtils.whereToOpenLink(action.data.event);
        doDownloadAction(download => {
          lazy.DownloadsCommon.openDownload(download, {
            // Replace "current" or unknown value with "tab" as the default behavior
            // for opening downloads when handled internally
            openWhere: ["window", "tab", "tabshifted"].includes(openWhere)
              ? openWhere
              : "tab",
          });
        });
        break;
      }
      case at.UNINIT:
        this.uninit();
        break;
    }
  }
}
