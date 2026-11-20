/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { BANDWIDTH } from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";
import { getSitePrincipal } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "ipProtectionLocalization", () => {
  return new Localization(["browser/ipProtection.ftl"], true);
});

ChromeUtils.defineESModuleGetters(lazy, {
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  IPPMode:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  IPPPrincipalRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPSiteRuleManager:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

/**
 * Manages showing alerts for different VPN states
 */
class IPProtectionAlertManagerClass {
  #localizationMessages = null;
  #promptsOpen = false;
  #initialized = false;

  get initialized() {
    return this.#initialized;
  }

  init() {
    if (this.#initialized) {
      return;
    }

    lazy.IPPProxyManager.addEventListener("IPPProxyManager:StateChanged", this);

    this.#initialized = true;
  }

  uninit() {
    if (!this.#initialized) {
      return;
    }

    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:StateChanged",
      this
    );

    this.#closeAllPrompts();

    this.#initialized = false;
  }

  get localizationMessages() {
    if (!this.#localizationMessages) {
      const [pausedTitle, pausedBody, closeTabsButton, continueButton] =
        lazy.ipProtectionLocalization.formatMessagesSync([
          { id: "vpn-paused-alert-title" },
          {
            id: "vpn-paused-alert-body",
            args: { maxUsage: this.#getMaxBandwidthUsage() },
          },
          { id: "vpn-paused-alert-close-tabs-button" },
          { id: "vpn-paused-alert-continue-wo-vpn-button" },
        ]);

      this.#localizationMessages = {
        pausedTitle: pausedTitle.value,
        pausedBody: pausedBody.value,
        closeTabsButton: closeTabsButton.value,
        continueButton: continueButton.value,
      };
    }

    return this.#localizationMessages;
  }

  /**
   * Check usage info for the max usage, then entitlement, then default to
   * BANDWIDTH.MAX_IN_GB.
   *
   * @returns {object} An object with max and remaining as numbers
   */
  #getMaxBandwidthUsage() {
    if (lazy.IPPProxyManager.usageInfo?.max != null) {
      return Number(lazy.IPPProxyManager.usageInfo.max) / BANDWIDTH.BYTES_IN_GB;
    } else if (lazy.IPProtectionService.authProvider.maxBytes != null) {
      // Usage info doesn't exist yet. Check the entitlement
      return (
        Number(lazy.IPProtectionService.authProvider.maxBytes) /
        BANDWIDTH.BYTES_IN_GB
      );
    }

    return BANDWIDTH.MAX_IN_GB;
  }

  handleEvent(event) {
    if (event.type !== "IPPProxyManager:StateChanged") {
      return;
    }

    switch (event.detail.state) {
      case lazy.IPPProxyStates.ACTIVE:
      case lazy.IPPProxyStates.NOT_READY:
      case lazy.IPPProxyStates.READY: {
        this.#closeAllPrompts();
        break;
      }
      case lazy.IPPProxyStates.PAUSED: {
        this.showPausedPrompts();
        break;
      }
    }
  }

  /**
   * Whether the proxy error should be shown for the selected tab of a window.
   * A failed connection that only carries included sites is not shown on a
   * tab it does not carry.
   *
   * @param {object} gBrowser
   * @returns {boolean}
   */
  isErrorShown(gBrowser) {
    if (lazy.IPPProxyManager.state !== lazy.IPPProxyStates.ERROR) {
      return false;
    }
    return (
      lazy.IPPProxyManager.mode !== lazy.IPPMode.MODE_INCLUSION ||
      this.#isIncludedSiteSelected(gBrowser)
    );
  }

  #isIncludedSiteSelected(gBrowser) {
    const principal = getSitePrincipal(gBrowser);
    return (
      !!principal &&
      lazy.IPPSiteRuleManager.getRule(
        principal,
        gBrowser.selectedBrowser.browserId
      ) === lazy.IPPPrincipalRules.INCLUDED
    );
  }

  /**
   * A helper to create a blocking prompt in all existing windows.
   *
   * @param {string} title The title of the prompt
   * @param {string} body The body text of the prompt
   * @param {string} button0 The text of the primary button
   * @param {string} button1 The text of the secondary button
   * @returns {Array} An array of promies. A promise will resole once one of
   * buttons in the prompt are clicked.
   */
  #createAllPrompts(title, body, button0, button1) {
    if (this.#promptsOpen) {
      return [];
    }

    const promises = [];
    for (let window of lazy.EveryWindow.readyWindows) {
      promises.push(
        Services.prompt.asyncConfirmEx(
          window.browsingContext,
          Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
          title,
          body,
          Ci.nsIPromptService.BUTTON_POS_0_DEFAULT |
            (Ci.nsIPromptService.BUTTON_TITLE_IS_STRING *
              Ci.nsIPromptService.BUTTON_POS_0) |
            (Ci.nsIPromptService.BUTTON_TITLE_IS_STRING *
              Ci.nsIPromptService.BUTTON_POS_1),
          button0,
          button1,
          null,
          null,
          false,
          { useTitle: true }
        )
      );
    }
    this.#promptsOpen = !!promises.length;
    return promises;
  }

  /**
   * Close all of the open prompts
   */
  #closeAllPrompts() {
    if (!this.#promptsOpen) {
      return;
    }

    this.#promptsOpen = false;

    for (let window of lazy.EveryWindow.readyWindows) {
      window.gDialogBox.dialog?.close();
    }
  }

  /**
   * Show the VPN paused alert. This notifies the user that they hit the max
   * bandwidth.
   *
   * @returns {number} The button clicked. 0 meaning continue without vpn and
   *  1 meaning close all tabs.
   */
  async showPausedPrompts() {
    if (!lazy.IPPProxyManager.active) {
      // If the proxy isn't already active, it will fail to start.
      return;
    }
    let { pausedTitle, pausedBody, continueButton, closeTabsButton } =
      this.localizationMessages;

    const promises = this.#createAllPrompts(
      pausedTitle,
      pausedBody,
      continueButton,
      closeTabsButton
    );

    if (promises.length === 0) {
      return;
    }

    let result = await Promise.any(promises);
    if (!this.#promptsOpen) {
      return;
    }
    let buttonClicked = result.getProperty("buttonNumClicked");

    this.#handlePromptAction(buttonClicked, "paused");
  }

  /**
   * Handles the action taken by the user from the prompt.
   *
   * @param {number} buttonClicked Either 0 or 1.
   *  0 means continue without vpn
   *  1 means close all tabs
   * @param {"paused"} reason Reason why the alert was triggered.
   */
  #handlePromptAction(buttonClicked, reason) {
    this.#closeAllPrompts();

    if (buttonClicked === 0) {
      lazy.IPPProxyManager.stop(true);
    } else if (buttonClicked === 1) {
      this.#closeAllTabs();
    }

    Glean.ipprotection.alertButtonClicked.record({
      buttonType: buttonClicked,
      reason,
    });
  }

  async #closeAllTabs() {
    const mostRecentWindow = Services.wm.getMostRecentBrowserWindow();
    const tabs = mostRecentWindow.gBrowser.tabs;
    mostRecentWindow.openTrustedLinkIn("about:home", "tab");
    mostRecentWindow.gBrowser.removeTabs(tabs);

    for (let window of lazy.EveryWindow.readyWindows) {
      if (window === mostRecentWindow) {
        continue;
      }

      window.close();
    }

    // Stop the VPN after closing everything to prevent any requests from
    // completing.
    lazy.IPPProxyManager.stop(true);
  }
}

const IPProtectionAlertManager = new IPProtectionAlertManagerClass();
export { IPProtectionAlertManager };
