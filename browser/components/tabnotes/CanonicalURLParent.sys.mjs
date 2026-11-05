/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "CanonicalURL",
    maxLogLevel: Services.prefs.getBoolPref("browser.tabs.notes.debug", false)
      ? "Debug"
      : "Warn",
  });
});

/**
 * Receives canonical URL identifications from CanonicalURLChild and dispatches
 * event notifications on the <browser>.
 */
export class CanonicalURLParent extends JSWindowActorParent {
  /**
   * Called when a message is received from the content process.
   *
   * @param {ReceiveMessageArgument} msg
   */
  receiveMessage(msg) {
    switch (msg.name) {
      case "CanonicalURL:Identified":
        {
          const browser = this.browsingContext?.embedderElement;

          // If we don't have a browser then it went away before we could record,
          // so we don't know where the data came from.
          if (!browser) {
            lazy.logConsole.debug(
              "CanonicalURL:Identified: reject due to missing browser"
            );
            return;
          }

          if (!browser.documentGlobal.gBrowser?.getTabForBrowser(browser)) {
            lazy.logConsole.debug(
              "CanonicalURL:Identified: reject due to the browser not being a tab browser"
            );
            return;
          }

          const { canonicalUrl, canonicalUrlSources } = msg.data;

          let event = new browser.documentGlobal.CustomEvent(
            "CanonicalURL:Identified",
            {
              bubbles: true,
              cancelable: false,
              detail: {
                canonicalUrl,
                canonicalUrlSources,
              },
            }
          );
          browser.dispatchEvent(event);
          lazy.logConsole.info("CanonicalURL:Identified", {
            canonicalUrl,
            canonicalUrlSources,
          });
        }
        break;
    }
  }
}
