/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { BrowserTestUtils } from "resource://testing-common/BrowserTestUtils.sys.mjs";
import { Assert } from "resource://testing-common/Assert.sys.mjs";
import { TestUtils } from "resource://testing-common/TestUtils.sys.mjs";

const initialStates = new WeakMap();
let gTestScope;

/**
 * Helpers for working with the sidebar in browser chrome tests.
 */
class _SidebarTestUtils {
  /**
   * Initializes the utils.
   *
   * @param {object} scope
   *   The global JS scope where tests are being run. This allows the instance
   *   to access test helpers like `info` and `registerCleanupFunction` that are available in the scope.
   */
  init(scope) {
    if (!scope) {
      throw new Error("SidebarTestUtils.init() must be called with a scope");
    }
    gTestScope = scope;
    scope.registerCleanupFunction?.(() => {
      gTestScope = null;
    });
  }

  /**
   * Capture the initial sidebar state for later restoration.
   * Call this while setting up any test which interacts with the sidebar, so that
   * we can restore it to its original state in a cleanup function when
   * the test completes.
   *
   * @param {ChromeWindow} win
   */
  restoreStateAtCleanup(win) {
    if (initialStates.has(win)) {
      throw new Error(
        "SidebarTestUtils.restoreStateAtCleanup already called for this window"
      );
    }
    initialStates.set(win, {
      ...win.SidebarController.getUIState(),
      command: "",
    });
    const testScopeInfo = gTestScope.info;
    gTestScope.registerCleanupFunction(async () => {
      // Close any sidebar panel which got left open
      if (!win.document.getElementById("sidebar-box").hidden) {
        testScopeInfo(
          `Sidebar ${win.SidebarController.currentID} was left open, closing it in cleanup function`
        );
        this.closePanel(win);
      }
      await this.restoreToInitialState(win);
    });
  }

  /**
   * Clean up and restore any sidebar state captured by restoreStateAtCleanup.
   *
   * @param {ChromeWindow} win
   */
  async restoreToInitialState(win) {
    let state = initialStates.get(win);
    if (state) {
      // When a sidebar panel is toggled with `hide()` the lastOpenedId is left populated
      // and that panel will re-open when next toggled. In tests, that's not normally
      // expected so we reset here.
      win.SidebarController.lastOpenedId = null;
      // Restore sidebar launcher back to whatever state it was in initially.
      await win.SidebarController.updateUIState(state);
      initialStates.delete(win);
    }
  }

  /**
   * Show a sidebar panel and wait for it to be focused.
   *
   * @param {ChromeWindow} win
   * @param {string} commandID
   */
  async showPanel(win, commandID) {
    let promiseFocused = BrowserTestUtils.waitForEvent(win, "SidebarFocused");
    await win.SidebarController.show(commandID);
    await promiseFocused;
  }

  /**
   * Close sidebar panel
   *
   * @param {ChromeWindow} win
   */
  closePanel(win) {
    let sidebarBox = win.document.getElementById("sidebar-box");
    if (sidebarBox.hidden) {
      return;
    }
    // This is equivalent to the user clicking the "X" to close a panel.
    // That's not the same as toggling the sidebar with `hide()` which leaves
    // lastOpenedId populated.
    win.SidebarController.hide({ dismissPanel: true });
  }

  async _ensureLauncherShowing(win, visible = true) {
    let { promiseInitialized, sidebarContainer } = win.SidebarController;
    await promiseInitialized;
    let hidden = !visible;
    if (sidebarContainer.hidden !== hidden) {
      // The command handler for the sidebar-button sets up the sidebar and button
      // state we want. But we can't always guarantee the sidebar-button is present,
      // so we call the handler directly.
      win.SidebarController.handleToolbarButtonClick();
      await BrowserTestUtils.waitForMutationCondition(
        sidebarContainer,
        { attributes: true, attributeFilter: ["hidden"] },
        () => sidebarContainer.hidden === hidden
      );
      await win.SidebarController.waitUntilStable();
    }
  }

  /**
   * Ensure the sidebar launcher is visible, opening it if necessary.
   *
   * @param {ChromeWindow} win
   */
  async ensureLauncherVisible(win, message = "Sidebar launcher is visible") {
    await this._ensureLauncherShowing(win, true);
    Assert.ok(
      BrowserTestUtils.isVisible(win.SidebarController.sidebarContainer),
      message
    );
  }

  /**
   * Ensure the sidebar launcher is hidden, closing it if necessary.
   *
   * @param {ChromeWindow} win
   */
  async ensureLauncherHidden(win, message = "Sidebar launcher is hidden") {
    await this._ensureLauncherShowing(win, false);
    Assert.ok(
      BrowserTestUtils.isHidden(win.SidebarController.sidebarContainer),
      message
    );
  }

  /**
   * Wait for the sidebar to be initialized in a window.
   *
   * @param {ChromeWindow} win
   */
  async waitForInitialized(win) {
    if (!win.SidebarController) {
      await TestUtils.topicObserved(
        "browser-delayed-startup-finished",
        subject => subject == win
      );
    }
    await win.SidebarController.promiseInitialized;
  }

  /**
   * Wait for the tabstrip orientation to settle.
   *
   * @param {ChromeWindow} win
   * @param {string} [toOrientation="vertical"]
   */
  async waitForTabstripOrientation(win, toOrientation = "vertical") {
    await win.SidebarController.promiseInitialized;
    await BrowserTestUtils.waitForMutationCondition(
      win.gBrowser.tabContainer,
      { attributes: true, attributeFilter: ["orient"] },
      () => win.gBrowser.tabContainer.getAttribute("orient") == toOrientation
    );
    await win.SidebarController.sidebarMain?.updateComplete;
    // Same wait as waitForRepaint() in head.js. The theme change that carries
    // -moz-pref() invalidation runs at the top of the next rendering update,
    // and the dispatched runnable resumes only once that update has finished.
    await new Promise(resolve =>
      win.requestAnimationFrame(() => Services.tm.dispatchToMainThread(resolve))
    );
  }
}

export const SidebarTestUtils = new _SidebarTestUtils();
