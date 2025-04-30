/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  LinkPreviewModel:
    "moz-src:///browser/components/genai/LinkPreviewModel.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
});
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "allowedLanguages",
  "browser.ml.linkPreview.allowedLanguages"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enabled",
  "browser.ml.linkPreview.enabled",
  false,
  (_pref, _old, val) => LinkPreview.onEnabledPref(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "prefetchOnEnable",
  "browser.ml.linkPreview.prefetchOnEnable",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "noKeyPointsRegions",
  "browser.ml.linkPreview.noKeyPointsRegions"
);

export const LinkPreview = {
  // Shared downloading state to use across multiple previews
  progress: -1, // -1 = off, 0-100 = download progress

  keyboardComboActive: false,
  _windowStates: new Map(),
  linkPreviewPanelId: "link-preview-panel",

  /**
   * Handles the preference change for enabling/disabling Link Preview.
   * It adds or removes event listeners for all tracked windows based on the new preference value.
   *
   * @param {boolean} enabled - The new state of the Link Preview preference.
   */
  onEnabledPref(enabled) {
    const method = enabled ? "_addEventListeners" : "_removeEventListeners";
    for (const win of this._windowStates.keys()) {
      this[method](win);
    }

    // Prefetch the model when enabling by simulating a request.
    if (enabled && lazy.prefetchOnEnable && this._isRegionSupported()) {
      this.generateKeyPoints();
    }

    Glean.genaiLinkpreview.enabled.set(enabled);
    Glean.genaiLinkpreview.labsCheckbox.record({ enabled });
  },

  /**
   * Handles startup tasks such as telemetry and adding listeners.
   *
   * @param {Window} win - The window context used to add event listeners.
   */
  init(win) {
    this._windowStates.set(win, {});
    if (!win.customElements.get("link-preview-card")) {
      win.ChromeUtils.importESModule(
        "chrome://browser/content/genai/content/link-preview-card.mjs",
        { global: "current" }
      );
    }

    if (lazy.enabled) {
      this._addEventListeners(win);
    }

    Glean.genaiLinkpreview.enabled.set(lazy.enabled);
  },

  /**
   * Teardown the Link Preview feature for the given window.
   * Removes event listeners from the specified window and removes it from the window map.
   *
   * @param {Window} win - The window context to uninitialize.
   */
  teardown(win) {
    // Remove event listeners from the specified window
    if (lazy.enabled) {
      this._removeEventListeners(win);
    }

    // Remove the panel if it exists
    const doc = win.document;
    doc.getElementById(this.linkPreviewPanelId)?.remove();

    // Remove the window from the map
    this._windowStates.delete(win);
  },

  /**
   * Adds all needed event listeners and updates the state.
   *
   * @param {Window} win - The window to which event listeners are added.
   */
  _addEventListeners(win) {
    win.addEventListener("OverLink", this, true);
    win.addEventListener("keydown", this, true);
    win.addEventListener("keyup", this, true);
  },

  /**
   * Removes all event listeners and updates the state.
   *
   * @param {Window} win - The window from which event listeners are removed.
   */
  _removeEventListeners(win) {
    win.removeEventListener("OverLink", this, true);
    win.removeEventListener("keydown", this, true);
    win.removeEventListener("keyup", this, true);
  },

  /**
   * Handles keyboard events ("keydown" and "keyup") for the Link Preview feature.
   * Adjusts the state of keyboardComboActive based on modifier keys.
   *
   * @param {KeyboardEvent} event - The keyboard event to be processed.
   */
  handleEvent(event) {
    switch (event.type) {
      case "keydown":
      case "keyup":
        this._onKeyEvent(event);
        break;
      case "OverLink":
        this._onLinkPreview(event);
        break;
      default:
        break;
    }
  },

  /**
   * Handles "keydown" and "keyup" events.
   *
   * @param {KeyboardEvent} event - The keyboard event to be processed.
   */
  _onKeyEvent(event) {
    const win = event.currentTarget;
    // Save the last state of the keyboard with both alt and shift pressed
    // without other modifiers.
    this.keyboardComboActive =
      event.altKey && event.shiftKey && !event.ctrlKey && !event.metaKey;
    // New presses or releases can result in desired combo for previewing.
    this._maybeLinkPreview(win);
  },

  /**
   * Handles "OverLink" events.
   * Stores the hovered link URL in the per-window state object and processes the
   * link preview if the keyboard combination is active.
   *
   * @param {CustomEvent} event - The event object containing details about the link preview.
   */
  _onLinkPreview(event) {
    const win = event.currentTarget;
    const url = event.detail.url;

    // Store the current overLink in the per-window state object.
    const stateObject = this._windowStates.get(win);
    stateObject.overLink = url;

    if (this.keyboardComboActive) {
      this._maybeLinkPreview(win);
    }
  },

  /**
   * Checks if the user's region is supported for key points generation.
   *
   * @returns {boolean} True if the region is supported, false otherwise.
   */
  _isRegionSupported() {
    const disallowedRegions = lazy.noKeyPointsRegions
      .split(",")
      .map(region => region.trim().toUpperCase());

    const userRegion = lazy.Region.home?.toUpperCase();
    return !disallowedRegions.includes(userRegion);
  },

  /**
   * Creates an Open Graph (OG) card using meta information from the page.
   *
   * @param {Document} doc - The document object where the OG card will be
   * created.
   * @param {object} pageData - An object containing page data, including meta
   * tags and article information.
   * @param {object} [pageData.article] - Optional article-specific data.
   * @param {object} [pageData.metaInfo] - Optional meta tag key-value pairs.
   * @returns {Element} A DOM element representing the OG card.
   */
  createOGCard(doc, pageData) {
    const ogCard = doc.createElement("link-preview-card");
    ogCard.style.width = "100%";
    ogCard.pageData = pageData;

    // Reflect the shared download progress to this preview.
    const updateProgress = () => {
      ogCard.progress = this.progress;
      // If we are still downloading, update the progress again.
      if (this.progress >= 0) {
        doc.ownerGlobal.setTimeout(
          () => ogCard.isConnected && updateProgress(),
          250
        );
      }
    };
    updateProgress();

    // Generate key points if we have content, language and configured for any
    // language or restricted.
    if (
      this._isRegionSupported() &&
      pageData.article.textContent &&
      pageData.article.detectedLanguage &&
      (!lazy.allowedLanguages ||
        lazy.allowedLanguages
          .split(",")
          .includes(pageData.article.detectedLanguage))
    ) {
      this.generateKeyPoints(ogCard);
    }
    return ogCard;
  },

  /**
   * Generate AI key points for card.
   *
   * @param {LinkPreviewCard} ogCard to add key points
   */
  async generateKeyPoints(ogCard) {
    // Support prefetching without a card by mocking expected properties.
    let outcome = ogCard ? "success" : "prefetch";
    if (!ogCard) {
      ogCard = { addKeyPoint() {}, isConnected: true, keyPoints: [] };
    }

    const startTime = Date.now();
    ogCard.generating = true;

    // Ensure sequential AI processing to reduce memory usage by passing our
    // promise to the next request before waiting on the previous.
    const previous = this.lastRequest;
    const { promise, resolve } = Promise.withResolvers();
    this.lastRequest = promise;
    await previous;
    const delay = Date.now() - startTime;

    // No need to generate if already removed.
    if (!ogCard.isConnected) {
      resolve();
      Glean.genaiLinkpreview.generate.record({
        delay,
        outcome: "removed",
      });
      return;
    }

    let download, latency;
    try {
      await lazy.LinkPreviewModel.generateTextAI(
        ogCard.pageData?.article.textContent ?? "",
        {
          onDownload: (downloading, percentage) => {
            // Initial percentage is NaN, so set to 0.
            percentage = isNaN(percentage) ? 0 : percentage;
            // Use the percentage while downloading, otherwise disable with -1.
            this.progress = downloading ? percentage : -1;
            ogCard.progress = this.progress;
            download = Date.now() - startTime;
          },
          onError: error => {
            console.error(error);
            outcome = error;
          },
          onText: text => {
            // Clear waiting in case a different generate handled download.
            ogCard.showWait = false;
            ogCard.addKeyPoint(text);
            latency = latency ?? Date.now() - startTime;
          },
        }
      );
    } finally {
      resolve();
      ogCard.generating = false;
      Glean.genaiLinkpreview.generate.record({
        delay,
        download,
        latency,
        outcome,
        sentences: ogCard.keyPoints.length,
        time: Date.now() - startTime,
      });
    }
  },

  /**
   * Renders the link preview panel at the specified coordinates.
   *
   * @param {Window} win - The browser window context.
   * @param {string} url - The URL of the link to be previewed.
   * @param {string} source - Optional trigging behavior.
   */
  async renderLinkPreviewPanel(win, url, source = "shortcut") {
    const doc = win.document;
    let panel = doc.getElementById(this.linkPreviewPanelId);
    const openPopup = () => {
      const { _x: x, _y: y } = win.MousePosTracker;
      // Open near the mouse offsetting so link in the card can be clicked.
      panel.openPopup(doc.documentElement, "overlap", x - 20, y - 160);
      panel.openTime = Date.now();
    };

    // Reuse the existing panel if the url is the same.
    if (panel) {
      if (panel.previewUrl == url) {
        if (panel.state == "closed") {
          openPopup();
          Glean.genaiLinkpreview.start.record({ cached: true, source });
        }
        return;
      }

      // Hide and remove previous in preparation for new url data.
      panel.hidePopup();
      panel.replaceChildren();
    } else {
      panel = doc
        .getElementById("mainPopupSet")
        .appendChild(doc.createXULElement("panel"));
      panel.className = "panel-no-padding";
      panel.id = this.linkPreviewPanelId;
      panel.setAttribute("noautofocus", true);
      panel.setAttribute("type", "arrow");
      panel.style.width = "362px";
      panel.style.setProperty("--og-padding", "var(--space-xlarge)");
      // Match the radius of the image extended out by the padding.
      panel.style.setProperty(
        "--panel-border-radius",
        "calc(var(--border-radius-small) + var(--og-padding))"
      );
      panel.addEventListener("popuphidden", () => {
        Glean.genaiLinkpreview.cardClose.record({
          duration: Date.now() - panel.openTime,
        });
      });
    }
    panel.previewUrl = url;
    Glean.genaiLinkpreview.start.record({ cached: false, source });

    // TODO we want to immediately add a card as a placeholder to have UI be
    // more responsive while we wait on fetching page data.
    const browsingContext = win.browsingContext;
    const actor = browsingContext.currentWindowGlobal.getActor("LinkPreview");
    const fetchTime = Date.now();
    const pageData = await actor.fetchPageData(url);
    // Skip updating content if we've moved on to showing something else.
    const skipped = pageData.url != panel.previewUrl;
    Glean.genaiLinkpreview.fetch.record({
      description: !!pageData.meta.description,
      image: !!pageData.meta.imageUrl,
      length:
        Math.round((pageData.article.textContent?.length ?? 0) * 0.01) * 100,
      outcome: pageData.error?.result ?? "success",
      sitename: !!pageData.article.siteName,
      skipped,
      time: Date.now() - fetchTime,
      title: !!pageData.meta.title,
    });
    if (skipped) {
      return;
    }
    const ogCard = this.createOGCard(doc, pageData);
    panel.append(ogCard);
    ogCard.addEventListener("LinkPreviewCard:dismiss", event => {
      panel.hidePopup();
      Glean.genaiLinkpreview.cardLink.record({ source: event.detail });
    });

    openPopup();
  },

  /**
   * Determines whether to process or cancel the link preview based on the current state.
   * If a URL is available and the keyboard combination is active, it processes the link preview.
   * Otherwise, it cancels the link preview.
   *
   * @param {Window} win - The window context in which the link preview may occur.
   */
  _maybeLinkPreview(win) {
    const stateObject = this._windowStates.get(win);
    const url = stateObject.overLink;
    if (url && this.keyboardComboActive) {
      this.renderLinkPreviewPanel(win, url);
    }
  },
};
