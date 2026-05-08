/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PageExtractorParent: "resource://gre/actors/PageExtractorParent.sys.mjs",
});

/**
 * @returns {object} The chrome window the active browser tab lives in.
 */
function getChromeWindow(chromeWindow) {
  const win = chromeWindow || Services.wm.getMostRecentBrowserWindow();
  if (!win || !win.gBrowser) {
    throw new Error(
      "PageExtractor runner needs an active browser window with gBrowser"
    );
  }
  return win;
}

/**
 * Resolve the PageExtractor JSWindowActor for the foreground tab.
 *
 * @param {object} [chromeWindow]
 * @returns {object}
 */
export function getPageExtractor(chromeWindow) {
  const win = getChromeWindow(chromeWindow);
  const actor =
    win.gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
      "PageExtractor"
    );
  if (!actor) {
    throw new Error(
      "PageExtractor actor is not available for the selected tab"
    );
  }
  return actor;
}

/**
 * @param {{ chromeWindow?: object }} [options]
 */
export function getPageText({ chromeWindow, ...rest } = {}) {
  return getPageExtractor(chromeWindow).getText(rest);
}

/**
 * @param {boolean} force
 * @param {object} [chromeWindow]
 */
export function getReaderModeContent(force, chromeWindow) {
  return getPageExtractor(chromeWindow).getReaderModeContent(Boolean(force));
}

/**
 * @param {{ chromeWindow?: object }} [options]
 */
export function getPageInfo({ chromeWindow, ...rest } = {}) {
  return getPageExtractor(chromeWindow).getPageInfo(rest);
}

/**
 * @param {{ chromeWindow?: object }} [options]
 */
export function getSelectionText({ chromeWindow, ...rest } = {}) {
  return getPageExtractor(chromeWindow).getSelectionText(rest);
}

/**
 * Run extraction in a hidden browser without disturbing the visible tab.
 *
 * @param {string} url
 * @param {object} [options]
 */
export async function runHeadlessExtractor(url, options = {}) {
  if (!url) {
    throw new Error("A URL is required to run the headless extractor");
  }
  return lazy.PageExtractorParent.getHeadlessExtractor(url, actor =>
    actor.getText(options)
  );
}
