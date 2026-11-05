/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/** @import { CanonicalURLParent } from "./CanonicalURLParent.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "TabNotesController",
    maxLogLevel: Services.prefs.getBoolPref("browser.tabs.notes.debug", false)
      ? "Debug"
      : "Warn",
  });
});

const EVENTS = [
  "CanonicalURL:Identified",
  "TabNote:Created",
  "TabNote:Edited",
  "TabNote:Removed",
  "TabNote:Expand",
];

/**
 * Orchestrates the tab notes life cycle.
 *
 * Singleton class within Firefox that observes tab notes-related topics,
 * listens for tab notes-related events, and ensures that the state of the
 * tabbrowser stays in sync with the TabNotes repository.
 *
 * Registers with the category manager in order to initialize on Firefox
 * startup and be notified when windows are opened/closed.
 *
 * @see https://firefox-source-docs.mozilla.org/browser/CategoryManagerIndirection.html
 *
 * Expected life cycle from Firefox startup:
 * - One call to `browserWindowDelayedStartup` from the first browser window created. Since
 *   we haven't gotten far enough along in startup yet, we do nothing.
 * - One call to `browserFirstWindowReady` when the first window has loaded. If tab notes
 *   is enabled, then we initialize TabNotes and the first window.
 * - As windows are opened (`browserWindowDelayedStartup`) or closed (`browserWindowUnload`),
 *   we register/unregister from canonical URL and tab notes events.
 * - As the user interacts with the browser, this class mediates between TabNotes storage and
 *   Tabbrowser state to make sure everything stays consistent.
 * - If the tab notes preference is enabled/disabled, the DesktopActorRegistry will start/stop
 *   the CanonicalURL actor and this class will observe those messages. This class is responsible
 *   for stopping/starting the TabNotes storage connection.
 * - When the user quits, `browserQuitApplicationGranted` will stop the TabNotes storage
 *   connection.
 */
class TabNotesControllerClass {
  /** @type {boolean} */
  TAB_NOTES_ENABLED;

  /** Whether `browser-first-window-ready` has been received on startup yet. */
  #isStartupComplete = false;
  /** Whether the tab notes machinery is currently wired up and running. */
  #isInitialized = false;

  /**
   * Registered with `browser-first-window-ready` to be notified of
   * app startup.
   *
   * @see tabnotes.manifest
   */
  browserFirstWindowReady() {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "TAB_NOTES_ENABLED",
      "browser.tabs.notes.enabled",
      false
    );
    this.#isStartupComplete = true;
    Services.obs.addObserver(this, "CanonicalURL:ActorRegistered");
    Services.obs.addObserver(this, "CanonicalURL:ActorUnregistered");
    if (this.TAB_NOTES_ENABLED) {
      lazy.logConsole.debug("browserFirstWindowReady", "Tab notes enabled");
      return this.#init().then(() => {
        for (const win of lazy.BrowserWindowTracker.orderedWindows) {
          this.#initWindow(win);
        }
      });
    }
    lazy.logConsole.debug("browserFirstWindowReady", "Tab notes disabled");
    return Promise.resolve();
  }

  /**
   * Starts up application-level systems in order to make sure tab notes are usable.
   *
   * @returns {Promise<void>}
   */
  async #init() {
    if (!this.#isInitialized) {
      this.#isInitialized = true;
      lazy.logConsole.debug("TabNotes initialized");
      return lazy.TabNotes.init();
    }
    return Promise.resolve();
  }

  /**
   * Registered with `browser-window-delayed-startup` to be notified of new
   * windows.
   *
   * @param {Window} win
   * @see tabnotes.manifest
   */
  browserWindowDelayedStartup(win) {
    lazy.logConsole.debug("browserWindowDelayedStartup", win);
    if (!this.#isStartupComplete) {
      lazy.logConsole.debug(
        "browserWindowDelayedStartup",
        "initialization deferred until startup complete"
      );
      return;
    }
    if (this.TAB_NOTES_ENABLED) {
      this.#initWindow(win);
    }
  }

  /**
   * Initializes a specific window in order to hook into the life cycle of tabs
   * and tab notes.
   *
   * @param {Window} win
   */
  #initWindow(win) {
    EVENTS.forEach(eventName => win.addEventListener(eventName, this));
    win.gBrowser.addTabsProgressListener(this);

    // check tabs that may have had canonicalUrl restored from session data
    for (const tab of win.gBrowser.tabs) {
      if (tab.canonicalUrl && lazy.TabNotes.isEligible(tab)) {
        lazy.TabNotes.has(tab).then(hasTabNote => {
          tab.hasTabNote = hasTabNote;
        });
      }
    }

    lazy.logConsole.debug("initWindow", win, EVENTS);
  }

  /**
   * Registered with `browser-window-unload` to be notified of unloaded windows.
   *
   * @param {Window} win
   * @see tabnotes.manifest
   */
  browserWindowUnload(win) {
    if (this.TAB_NOTES_ENABLED) {
      this.#unloadWindow(win);
      lazy.logConsole.debug("browserWindowUnload", EVENTS, win);
    }
  }

  /**
   * Stops listening to the life cycle of tabs and tab notes for a particular
   * window.
   *
   * @param {Window} win
   */
  #unloadWindow(win) {
    EVENTS.forEach(eventName => win.removeEventListener(eventName, this));
    win.gBrowser.removeTabsProgressListener(this);
    lazy.logConsole.debug("unloadWindow", win, EVENTS);
  }

  /**
   * Registered with `browser-quit-application-granted` to be notified of
   * app shutdown.
   *
   * @see tabnotes.manifest
   */
  browserQuitApplicationGranted() {
    return this.#deinit();
  }

  /**
   * Shuts down application-level systems in order to make sure tab notes are
   * no longer using system resources.
   *
   * @returns {Promise<void>}
   */
  async #deinit() {
    if (this.#isInitialized) {
      this.#isInitialized = false;
      lazy.logConsole.debug("TabNotes deinitialized");
    }
    return lazy.TabNotes.deinit();
  }

  /**
   * @param {CanonicalURLIdentifiedEvent|TabNoteCreatedEvent|TabNoteEditedEvent|TabNoteRemovedEvent|TabNoteExpandEvent} event
   */
  handleEvent(event) {
    switch (event.type) {
      case "CanonicalURL:Identified":
        {
          // A browser identified its canonical URL, so we can determine whether
          // the tab has an associated note and should therefore display a tab
          // notes icon.
          const browser = event.target;
          const { canonicalUrl } = event.detail;
          const gBrowser = browser.getTabBrowser();
          /** @type {MozTabbrowserTab} */
          const tab = gBrowser.getTabForBrowser(browser);
          tab.canonicalUrl = canonicalUrl;
          lazy.TabNotes.has(tab).then(hasTabNote => {
            tab.hasTabNote = hasTabNote;
            lazy.logConsole.debug("TabNote:Determined", tab, hasTabNote);
            tab.dispatchEvent(
              new CustomEvent("TabNote:Determined", {
                detail: { hasTabNote },
              })
            );
          });

          lazy.logConsole.debug("CanonicalURL:Identified", tab, canonicalUrl);
        }
        break;
      case "TabNote:Created":
        {
          const { note, telemetrySource } = event.detail;
          if (telemetrySource) {
            Glean.tabNotes.added.record({
              source: telemetrySource,
              note_length: note.text.length,
            });
          }
          // A new tab note was created for a specific canonical URL. Ensure that
          // all tabs with the same canonical URL also indicate that there is a
          // tab note.
          const { canonicalUrl } = event.target;
          for (const win of lazy.BrowserWindowTracker.orderedWindows) {
            for (const tab of win.gBrowser.tabs) {
              if (tab.canonicalUrl == canonicalUrl) {
                tab.hasTabNote = true;
              }
            }
          }
          lazy.logConsole.debug("TabNote:Created", canonicalUrl);
        }
        break;
      case "TabNote:Edited":
        {
          const { canonicalUrl } = event.target;
          const { note, telemetrySource } = event.detail;
          if (telemetrySource) {
            Glean.tabNotes.edited.record({
              source: telemetrySource,
              note_length: note.text.length,
            });
          }
          lazy.logConsole.debug("TabNote:Edited", canonicalUrl);
        }
        break;
      case "TabNote:Removed":
        {
          const { telemetrySource, note } = event.detail;
          const now = Temporal.Now.instant();
          const noteAgeHours = Math.round(
            now.since(note.created).total("hours")
          );
          if (telemetrySource) {
            Glean.tabNotes.deleted.record({
              source: telemetrySource,
              note_age_hours: noteAgeHours,
            });
          }

          // A tab note was removed from a specific canonical URL. Ensure that
          // all tabs with the same canonical URL also indicate that there is no
          // longer a tab note.
          const { canonicalUrl } = event.target;
          for (const win of lazy.BrowserWindowTracker.orderedWindows) {
            for (const tab of win.gBrowser.tabs) {
              if (tab.canonicalUrl == canonicalUrl) {
                tab.hasTabNote = false;
              }
            }
          }
          lazy.logConsole.debug("TabNote:Removed", canonicalUrl);
        }
        break;

      case "TabNote:Expand": {
        const tab = event.target;
        lazy.TabNotes.get(tab).then(note => {
          if (note) {
            lazy.logConsole.debug("TabNote:Expand", note);
            Glean.tabNotes.expanded.record({ note_length: note.text.length });
          }
        });
      }
    }
  }

  /**
   * Observes messages from the DesktopActorRegistry about the CanonicalURL actor
   * being registered and unregistered. Instead of observing the tab notes preference
   * directly, we instead have the DesktopActorRegistry observe the tab notes pref,
   * manage the CanonicalURL actor, and only then tell us when the CanonicalURL actor
   * is in a state that we can work with.
   *
   * @type {Extract<nsIObserver, Function>}
   */
  observe(_aSubject, aTopic) {
    switch (aTopic) {
      case "CanonicalURL:ActorRegistered":
        // Tab notes pref was flipped from disabled to enabled while the
        // browser was running, and now the CanonicalURL actor has been
        // registered with the browser.
        // Ask all browsers to report their canonical URLs, if possible.
        lazy.logConsole.debug(
          "CanonicalURL actor registered, requesting canonical URLs"
        );
        this.#init()
          .then(() => {
            for (const win of lazy.BrowserWindowTracker.orderedWindows) {
              this.#initWindow(win);
              for (const tab of win.gBrowser.tabs) {
                try {
                  /** @type {CanonicalURLParent|undefined} */
                  let parent =
                    tab.linkedBrowser.browsingContext?.currentWindowGlobal.getActor(
                      "CanonicalURL"
                    );

                  parent?.sendAsyncMessage("CanonicalURL:Detect");
                } catch (e) {
                  if (
                    DOMException.isInstance(e) &&
                    e.message.includes("Window protocol")
                  ) {
                    // Tab is on a URL that doesn't support the CanonicalURL
                    // actor (e.g. about:config) and that's OK.
                  } else {
                    lazy.logConsole.error(e);
                  }
                }
              }
            }
          })
          .then(() => {
            Services.obs.notifyObservers(null, "TabNote:Enabled");
          });

        break;
      case "CanonicalURL:ActorUnregistered":
        // Tab notes pref was flipped from enabled to disabled while the
        // browser was running, and now the CanonicalURL actor has been
        // unregistered with the browser.
        // Reset the canonical URL and tab notes state for all tabs.
        lazy.logConsole.debug(
          "CanonicalURL actor unregistered, clearing all tabs"
        );
        this.#deinit()
          .then(() => {
            for (const win of lazy.BrowserWindowTracker.orderedWindows) {
              this.#unloadWindow(win);
              for (const tab of win.gBrowser.tabs) {
                this.#resetTab(tab);
              }
            }
          })
          .then(() => {
            Services.obs.notifyObservers(null, "TabNote:Disabled");
          });
        break;
    }
  }

  /**
   * Invoked by Tabbrowser after we register with `Tabbrowser.addProgressListener`.
   *
   * Clears the tab note icon and canonical URL from a tab when navigation starts
   * within a tab.
   *
   * The CanonicalURL actor running in this tab's browser will report the canonical
   * URL of the new destination in the browser, which will determine whether the
   * new destination has an associated tab note.
   *
   * @type {TabbrowserWebProgressListener<"onLocationChange">}
   */
  onLocationChange(aBrowser, aWebProgress, aRequest, aLocation, aFlags) {
    // Tab notes only apply to the top-level frames loaded in tabs.
    if (!aWebProgress.isTopLevel) {
      return;
    }

    if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      if (
        aWebProgress.loadType & Ci.nsIDocShell.LOAD_CMD_RELOAD ||
        aWebProgress.loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY
      ) {
        // User is reloading or returning to the same document via history. We
        // can count on CanonicalURLChild to listen for `pageshow` (for traditional
        // web sites) or `popstate` (for single-page applications) and tell us
        // about the canonical URL at the new location.
        lazy.logConsole.debug(
          "reload/history navigation, waiting for pageshow or popstate",
          aLocation.spec
        );
        return;
      }

      if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_HASHCHANGE) {
        // The web site modified the hash/fragment identifier part of the URL
        // directly. TODO: determine how and whether to handle `hashchange`.
        lazy.logConsole.debug("fragment identifier changed", aLocation.spec);
        return;
      }

      if (aWebProgress.loadType & Ci.nsIDocShell.LOAD_CMD_PUSHSTATE) {
        // Web page is using `history.pushState()` to change URLs. There isn't
        // a way for CanonicalURLChild to detect this in the content process,
        // so we need to ask it to recalculate canonical URLs to see if they
        // changed.
        /** @type {CanonicalURLParent|undefined} */
        let parent =
          aBrowser.browsingContext?.currentWindowGlobal.getExistingActor(
            "CanonicalURL"
          );

        if (parent) {
          parent.sendAsyncMessage("CanonicalURL:DetectFromPushState", {
            pushStateUrl: aLocation.spec,
          });
          lazy.logConsole.debug(
            "requesting CanonicalURL:DetectFromPushState due to history.pushState",
            aLocation.spec
          );
        }

        return;
      }

      // General same document case: we are navigating in the same document,
      // so the tab note indicator does not need to change.
      return;
    }

    if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SESSION_STORE) {
      // Location was changed as part of a session restoration. In this case
      // the canonical URL was already retrieved from session data.
      lazy.logConsole.debug("preserving tab note state during session restore");
      return;
    }

    // General case: we are doing normal navigation to another URL, so we
    // clear the canonical URL/tab note state on the tab and wait for
    // `CanonicalURL:Identified` to tell us whether the new location has
    // a tab note.
    const tab = aBrowser.documentGlobal.gBrowser.getTabForBrowser(aBrowser);
    this.#resetTab(tab);
    lazy.logConsole.debug("clear tab note due to location change", tab);
  }

  /**
   * @param {MozTabbrowserTab} tab
   */
  #resetTab(tab) {
    tab.canonicalUrl = null;
    tab.hasTabNote = false;
  }
}

export const TabNotesController = new TabNotesControllerClass();
