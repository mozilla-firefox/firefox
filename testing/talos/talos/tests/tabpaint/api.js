/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This test will measure the performance of opening a tab and displaying
 * the simplest content (a blank page). This test is concerned primarily
 * with the time it takes to show the content area to the user, rather than
 * the performance of the tab opening or closing animations (which are
 * measured in the TART test). This is why this test disables tab animations.
 *
 * When opening tabs and showing content, there are two cases to consider:
 *
 * 1) The tab has been opened via a command from the parent process. For example,
 *    the user has clicked on a link in a separate application which is opening
 *    in this browser, so the message to open the tab and display the content
 *    comes from the operating system, to the parent.
 *
 * 2) The tab has been opened by clicking on a link in content. It is possible
 *    for certain types of links (_blank links for example) to open new tabs.
 */

ChromeUtils.defineESModuleGetters(this, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  TalosParentProfiler: "resource://talos-powers/TalosParentProfiler.sys.mjs",
});

const REDUCE_MOTION_PREF = "ui.prefersReducedMotion";
const PROCESS_COUNT_PREF = "dom.ipc.processCount";

const MESSAGES = ["TabPaint:Go", "TabPaint:Painted"];

const BROWSER_FLUSH_TOPIC = "sessionstore-browser-shutdown-flush";

/* globals ExtensionAPI */
this.tabpaint = class extends ExtensionAPI {
  onStartup() {
    // We don't have a window in this scope, and in fact, there might
    // not be any browser windows around. Since pageloader is loading
    // this add-on, along with the tabpaint.html content, what we'll do
    // is wait for the tabpaint.html content to send us a message to
    // get us moving.
    for (let msgName of MESSAGES) {
      Services.mm.addMessageListener(msgName, this);
    }

    this.framescriptURL = this.extension.baseURI.resolve("/framescript.js");
    Services.mm.loadFrameScript(this.framescriptURL, true);

    Services.prefs.setIntPref(REDUCE_MOTION_PREF, 1);
    Services.prefs.setIntPref(PROCESS_COUNT_PREF, 1);

    /**
     * We'll store a callback here to be fired once the target page
     * reports that it has painted.
     */
    this.paintCallback = null;
  }

  onShutdown() {
    for (let msgName of MESSAGES) {
      Services.mm.removeMessageListener(msgName, this);
    }

    Services.mm.removeDelayedFrameScript(this.framescriptURL);

    Services.prefs.clearUserPref(REDUCE_MOTION_PREF);
    Services.prefs.clearUserPref(PROCESS_COUNT_PREF);
  }

  receiveMessage(msg) {
    let browser = msg.target;

    let gBrowser = browser.documentGlobal.gBrowser;

    switch (msg.name) {
      case "TabPaint:Go": {
        // Our document has loaded, and we're off to the races!
        this.go(browser.messageManager, gBrowser, msg.data.target);
        break;
      }

      case "TabPaint:Painted": {
        // Content has reported that it has painted.
        if (!this.paintCallback) {
          throw new Error("TabPaint:Painted fired without a paintCallback set");
        }

        let tab = gBrowser.getTabForBrowser(browser);
        let delta = msg.data.delta;
        this.paintCallback({ tab, delta });
        break;
      }
    }
  }

  /**
   * Start a single run of the test. This will measure the time
   * to open a new tab from the parent, and then measure the time
   * to open a new tab from content.
   *
   * @param gBrowser (<xul:tabbrowser>)
   *        The tabbrowser of the window to use.
   */
  async go(mm, gBrowser, target) {
    let fromParent = await this.openTabFromParent(gBrowser, target);
    let fromContent = await this.openTabFromContent(gBrowser);

    mm.sendAsyncMessage("TabPaint:FinalResults", { fromParent, fromContent });
  }

  /**
   * Opens a tab from the parent, waits until it is displayed, then
   * removes the tab.
   *
   * @param gBrowser (<xul:tabbrowser>)
   *        The tabbrowser of the window to use.
   *
   * @return Promise
   *         Resolves once the tab has been fully removed. Resolves
   *         with the time (in ms) it took to open the tab from the parent.
   */
  async openTabFromParent(gBrowser, target) {
    let win = BrowserWindowTracker.getTopWindow();

    TalosParentProfiler.subtestStart("TabPaint Parent Start");
    let startTime = ChromeUtils.now();

    gBrowser.selectedTab = gBrowser.addTab(
      //win.performance.now() + win.performance.timing.navigationStart gives the UNIX timestamp.
      `${target}?${
        win.performance.now() + win.performance.timing.navigationStart
      }`,
      {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      }
    );

    let { tab, delta } = await this.whenTabShown();
    TalosParentProfiler.subtestEnd(
      "Talos - Tabpaint: Open Tab from Parent",
      startTime
    );
    await this.removeTab(tab);
    return delta;
  }

  /**
   * Opens a tab from content, waits until it is displayed, then
   * removes the tab.
   *
   * @param gBrowser (<xul:tabbrowser>)
   *        The tabbrowser of the window to use.
   *
   * @return Promise
   *         Resolves once the tab has been fully removed. Resolves
   *         with the time (in ms) it took to open the tab from content.
   */
  async openTabFromContent() {
    TalosParentProfiler.subtestStart("TabPaint Content Start");
    let start_time = ChromeUtils.now();

    Services.mm.broadcastAsyncMessage("TabPaint:OpenFromContent");

    let { tab, delta } = await this.whenTabShown();
    TalosParentProfiler.subtestEnd(
      "Talos - Tabpaint: Open Tab from Content",
      start_time
    );
    await this.removeTab(tab);
    return delta;
  }

  /**
   * Returns a Promise that will resolve once the next tab reports
   * it has shown.
   *
   * @return Promise
   */
  whenTabShown() {
    return new Promise(resolve => {
      this.paintCallback = resolve;
    });
  }

  /**
   * Returns a Promise that removes a tab, and resolves once the final
   * message from that tab has come up.
   *
   * @param tab (<xul:tab>)
   *        The tab to remove.
   *
   * @return Promise
   */
  removeTab(tab) {
    TalosParentProfiler.mark("Tabpaint: Remove Tab");
    return new Promise(resolve => {
      let browser = tab.linkedBrowser;
      let observer = subject => {
        if (subject === browser) {
          Services.obs.removeObserver(observer, BROWSER_FLUSH_TOPIC);
          resolve();
        }
      };
      Services.obs.addObserver(observer, BROWSER_FLUSH_TOPIC);
      tab.documentGlobal.gBrowser.removeTab(tab);
    });
  }
};
