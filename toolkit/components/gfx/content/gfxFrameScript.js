/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const { testVideoEncode, CodecTestResult } = ChromeUtils.importESModule(
  "resource://gre/modules/SanityTestChecks.sys.mjs"
);

const gfxFrameScript = {
  domUtils: null,

  init() {
    let webNav = docShell.QueryInterface(Ci.nsIWebNavigation);
    let webProgress = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
    webProgress.addProgressListener(
      this,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
    );

    this.domUtils = content.windowUtils;

    addMessageListener("gfxSanity:RunEncoderTest", this);

    let loadURIOptions = {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    };
    webNav.loadURI(
      Services.io.newURI("resource://gfxsanity/sanitytest.html"),
      loadURIOptions
    );
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "MozAfterPaint":
        this.sendContentLoaded();
        removeEventListener("MozAfterPaint", this);
        break;
    }
  },

  receiveMessage(aMessage) {
    switch (aMessage.name) {
      case "gfxSanity:RunEncoderTest":
        testVideoEncode(content).then(
          result => sendAsyncMessage("gfxSanity:EncoderResult", result),
          e =>
            sendAsyncMessage("gfxSanity:EncoderResult", {
              status: CodecTestResult.Failed,
              reason: `encode check threw: ${e}`,
            })
        );
        break;
    }
  },

  sendContentLoaded() {
    ChromeUtils.addProfilerMarker(
      "GfxSanityTest",
      { category: "Graphics" },
      `content loaded, video readyState ${content.document.querySelector("video").readyState}`
    );
    sendAsyncMessage("gfxSanity:ContentLoaded");
  },

  isSanityTest(aUri) {
    if (!aUri) {
      return false;
    }

    return aUri.endsWith("/sanitytest.html");
  },

  onStateChange(webProgress, req, flags) {
    if (
      webProgress.isTopLevel &&
      flags & Ci.nsIWebProgressListener.STATE_STOP &&
      this.isSanityTest(req.name)
    ) {
      webProgress.removeProgressListener(this);

      // If no paint is pending, then the test already painted
      if (this.domUtils.isMozAfterPaintPending) {
        addEventListener("MozAfterPaint", this);
      } else {
        this.sendContentLoaded();
      }
    }
  },

  // Needed to support web progress listener
  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
    "nsIObserver",
  ]),
};

gfxFrameScript.init();
