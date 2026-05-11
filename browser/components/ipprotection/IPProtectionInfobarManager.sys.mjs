/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { formatRemainingBandwidth } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

const BANDWIDTH_WARNING_DISMISSED_PREF =
  "browser.ipProtection.bandwidthWarningDismissedThreshold";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPUsageHelper:
    "moz-src:///browser/components/ipprotection/IPPUsageHelper.sys.mjs",
});

/**
 * Manages displaying bandwidth warning infobars when usage reaches
 * 75% or 90% thresholds based on remaining bandwidth percentage.
 */
class IPProtectionInfobarManagerClass {
  #initialized = false;
  #lastThreshold = null;
  #lastUsage = null;
  #windowListener = null;
  #prefObserver = null;

  get initialized() {
    return this.#initialized;
  }

  init() {
    if (this.#initialized) {
      return;
    }

    lazy.IPPProxyManager.addEventListener("IPPProxyManager:UsageChanged", this);
    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this
    );

    this.#windowListener = {
      onOpenWindow: xulWindow => {
        const win = xulWindow.docShell.domWindow;
        win.addEventListener(
          "load",
          () => {
            if (
              win.document.documentElement.getAttribute("windowtype") !==
              "navigator:browser"
            ) {
              return;
            }
            if (this.#lastThreshold && this.#lastUsage) {
              this.#showInfobar(this.#lastThreshold, this.#lastUsage, win);
            }
          },
          { once: true }
        );
      },
    };
    Services.wm.addListener(this.#windowListener);

    this.#prefObserver = this.#handlePrefChange.bind(this);
    Services.prefs.addObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.#prefObserver
    );

    this.#initialized = true;
  }

  uninit() {
    if (!this.#initialized) {
      return;
    }

    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:UsageChanged",
      this
    );
    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this
    );

    Services.wm.removeListener(this.#windowListener);
    this.#windowListener = null;
    this.#lastThreshold = null;
    this.#lastUsage = null;

    Services.prefs.removeObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.#prefObserver
    );
    this.#prefObserver = null;

    this.#initialized = false;
  }

  #handlePrefChange(_subject, _topic, data) {
    if (data !== BANDWIDTH_WARNING_DISMISSED_PREF) {
      return;
    }
    const { infobar } = lazy.IPPUsageHelper.getDismissedThresholds();
    if (infobar >= 75) {
      this.#hideInfobar(75);
    }
    if (infobar >= 90) {
      this.#hideInfobar(90);
    }
  }

  handleEvent(event) {
    if (
      event.type === "IPProtectionService:StateChanged" &&
      lazy.IPProtectionService.state !== lazy.IPProtectionStates.READY
    ) {
      // Eg. hide warnings when signed out
      this.#lastThreshold = null;
      this.#lastUsage = null;
      this.#hideInfobar(75);
      this.#hideInfobar(90);
      return;
    }

    if (event.type === "IPPProxyManager:UsageChanged") {
      const usage = event.detail.usage;
      if (
        !usage ||
        usage.max == null ||
        usage.remaining == null ||
        !usage.reset
      ) {
        return;
      }

      // Calculate what percentage of bandwidth remains
      const remainingPercent = Number(usage.remaining) / Number(usage.max);

      /* Covers the cases where bandwidth hits max or is reset
         Could check for remainingPercent = 1, but there is a chance
         of not having exactly 100% left when this is called, and we
         want to clear the infobar if it's showing and there is less than
         75% usage */
      if (remainingPercent === 0 || remainingPercent > 0.25) {
        lazy.IPPUsageHelper.setDismissedThresholds({ infobar: 0, panel: 0 });
        this.#lastThreshold = null;
        this.#lastUsage = null;
        this.#hideInfobar(75);
        this.#hideInfobar(90);
        return;
      }

      // Show 90% warning when 10% or less bandwidth remains
      if (remainingPercent <= 0.1) {
        this.#lastThreshold = 90;
        this.#lastUsage = usage;
        this.#showInfobar(90, usage);
        // Show 75% warning when bandwidth remaining is between 10% and 25%
      } else if (remainingPercent > 0.1 && remainingPercent <= 0.25) {
        this.#lastThreshold = 75;
        this.#lastUsage = usage;
        this.#showInfobar(75, usage);
      }
    }
  }

  /**
   * Hide all bandwidth warning infobars from all browser windows.
   */
  hideInfobars() {
    this.#hideInfobar(75);
    this.#hideInfobar(90);
  }

  /**
   * Hide the bandwidth warning infobar if displayed in a browser window.
   *
   * @param {number} threshold - The threshold level (75 or 90)
   */
  #hideInfobar(threshold) {
    const notificationId = `ip-protection-bandwidth-warning-${threshold}`;
    // It's possible for the notification to be in another browser window
    // after navigating away. Clear the notification from all windows if applicable.
    for (const win of Services.wm.getEnumerator("navigator:browser")) {
      if (win.closed) {
        continue;
      }
      const notification =
        win.gNotificationBox.getNotificationWithValue(notificationId);
      if (notification) {
        win.gNotificationBox.removeNotification(notification);
      }
    }
  }

  /**
   * Display bandwidth warning infobar in the given browser window, or the
   * most recently used browser window if none is provided.
   *
   * @param {number} threshold - The threshold level (75 or 90)
   * @param {object} usage - Usage object containing remaining bandwidth
   * @param {Window} [win] - The browser window to show the infobar in
   */
  #showInfobar(
    threshold,
    usage,
    win = Services.wm.getMostRecentWindow("navigator:browser")
  ) {
    const notificationId = `ip-protection-bandwidth-warning-${threshold}`;

    if (!win || win.closed) {
      return;
    }

    if (lazy.IPPUsageHelper.getDismissedThresholds().infobar >= threshold) {
      return;
    }

    // Skip if this window already has the notification
    const existing =
      win.gNotificationBox.getNotificationWithValue(notificationId);
    if (existing) {
      return;
    }

    const { value: remainingFormatted, useGB } = formatRemainingBandwidth(
      Number(usage.remaining)
    );

    // Skip if any window has the panel open with a message bar
    for (const openWin of Services.wm.getEnumerator("navigator:browser")) {
      if (openWin.closed) {
        continue;
      }
      const panel = lazy.IPProtection.getPanel(openWin);
      if (panel?.active && panel.state.bandwidthWarning) {
        return;
      }
    }

    let usageLeft;
    let l10nId;

    if (!useGB && threshold === 90) {
      usageLeft = String(remainingFormatted);
      l10nId = "ip-protection-bandwidth-warning-infobar-message-90-mb";
    } else {
      usageLeft = String(remainingFormatted);
      l10nId =
        threshold === 90
          ? "ip-protection-bandwidth-warning-infobar-message-90"
          : "ip-protection-bandwidth-warning-infobar-message-75";
    }

    // Show the infobar with localized message
    win.gNotificationBox.appendNotification(
      notificationId,
      {
        label: {
          "l10n-id": l10nId,
          "l10n-args": {
            usageLeft,
          },
        },
        priority: win.gNotificationBox.PRIORITY_WARNING_HIGH,
        eventCallback: event => {
          if (event === "dismissed") {
            const current = lazy.IPPUsageHelper.getDismissedThresholds();
            if (threshold > current.infobar) {
              lazy.IPPUsageHelper.setDismissedThresholds({
                ...current,
                infobar: threshold,
              });
            }
          }
        },
      },
      [],
      false,
      true
    );
  }
}

const IPProtectionInfobarManager = new IPProtectionInfobarManagerClass();
export { IPProtectionInfobarManager };
