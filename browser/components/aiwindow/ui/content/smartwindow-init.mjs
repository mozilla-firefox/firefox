/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
});

const { topChromeWindow } = window.browsingContext;

/**
 * Initializes ASRouter and call appropriate trigger functions
 */
async function init() {
  // This init is only for the full-page Smart Window. Redirecting the hidden
  // sidebar would leave about:newtab alive in the parent process.
  if (window.browsingContext.embedderElement?.id === "ai-window-browser") {
    return;
  }

  if (!lazy.AIWindow.isAIWindowActive(topChromeWindow)) {
    window.location.href = topChromeWindow.BROWSER_NEW_TAB_URL;
    return;
  }

  await lazy.ASRouter.waitForInitialized;
  triggerSwitcherButtonCallout();
}

/**
 * Triggers the onboarding switcher button callout using the messaging system.
 */
function triggerSwitcherButtonCallout() {
  lazy.ASRouter.sendTriggerMessage({
    browser: topChromeWindow.gBrowser.selectedBrowser,
    id: "smartWindowNewTab",
  });
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init, {
    once: true,
  });
} else {
  await init();
}
