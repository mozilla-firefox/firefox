/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
const { UrlbarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlbarTestUtils.sys.mjs"
);

const TEST_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

const TEST_PAGE = TEST_ROOT + "test-page.html";
const SHORT_TEST_PAGE = TEST_ROOT + "short-test-page.html";
const LARGE_TEST_PAGE = TEST_ROOT + "large-test-page.html";
const IFRAME_TEST_PAGE = TEST_ROOT + "iframe-test-page.html";
const RESIZE_TEST_PAGE = TEST_ROOT + "test-page-resize.html";
const SELECTION_TEST_PAGE = TEST_ROOT + "test-selectionAPI-page.html";
const RTL_TEST_PAGE = TEST_ROOT + "rtl-test-page.html";
const SHADOWROOT_TEST_PAGE = TEST_ROOT + "test-page-shadowRoot.html";
const MISSING_TITLE_TEST_PAGE = TEST_ROOT + "missing-title-test-page.html";

const { MAX_CAPTURE_DIMENSION, MAX_CAPTURE_AREA } = ChromeUtils.importESModule(
  "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs"
);

const gScreenshotUISelectors = {
  panel: ".screenshotsPagePanel",
  fullPageButton: "button#full-page",
  visiblePageButton: "button#visible-page",
  copyButton: "button.#copy",
};

// AnonymousContentEvents is for the mouse, keyboard, and touch events on the Anonymous content
const AnonymousContentEvents = {
  mouse: new Proxy(
    {},
    {
      get: (target, name) =>
        async function (x, y, options = {}, browser) {
          if (name.includes("click")) {
            this.down(x, y, options, browser);
            this.up(x, y, options, browser);
            if (name.includes("dbl")) {
              this.down(x, y, options, browser);
              this.up(x, y, options, browser);
            }
          } else if (name === "contextmenu") {
            await safeSynthesizeMouseEventInContentPage(
              ":root",
              x,
              y,
              {
                type: name,
                ...options,
              },
              browser
            );
          } else {
            await safeSynthesizeMouseEventInContentPage(
              ":root",
              x,
              y,
              {
                type: "mouse" + name,
                ...options,
              },
              browser
            );
          }
        },
    }
  ),
  key: new Proxy(
    {},
    {
      get: (target, name) =>
        async function (key, options = {}, browser) {
          await safeSynthesizeKeyEventInContentPage(
            key,
            { type: "key" + name, ...options },
            browser
          );
        },
    }
  ),
  touch: new Proxy(
    {},
    {
      get: (target, name) =>
        async function (x, y, options = {}, browser) {
          await safeSynthesizeTouchEventInContentPage(
            ":root",
            x,
            y,
            {
              type: "touch" + name,
              ...options,
            },
            browser
          );
        },
    }
  ),
};

const { mouse, key, touch } = AnonymousContentEvents;

class ScreenshotsHelper {
  constructor(browser) {
    this.browser = browser;
    this.selector = gScreenshotUISelectors;
  }

  get toolbarButton() {
    return this.browser.ownerDocument.getElementById("screenshot-button");
  }

  get panel() {
    return this.browser.ownerDocument.querySelector(this.selector.panel);
  }

  /**
   * Click the screenshots button in the toolbar
   */
  triggerUIFromToolbar() {
    let button = this.toolbarButton;
    ok(
      BrowserTestUtils.isVisible(button),
      "The screenshot toolbar button is visible"
    );
    button.click();
  }

  async getPanelButton(selector) {
    let panel = await this.waitForPanel();
    let screenshotsButtons = panel.querySelector("screenshots-buttons");
    ok(screenshotsButtons, "Found the screenshots-buttons");
    let button = screenshotsButtons.shadowRoot.querySelector(selector);
    ok(button, `Found ${selector} button`);
    return button;
  }

  /**
   * Get the button from screenshots preview dialog
   *
   * @param {Sting} name The id of the button to query
   * @returns The button or null
   */
  getDialogButton(name) {
    let dialog = this.getDialog();
    let screenshotsPreviewEl = dialog._frame.contentDocument.querySelector(
      "screenshots-preview"
    );

    switch (name) {
      case "retry":
        return screenshotsPreviewEl.retryButtonEl;
      case "cancel":
        return screenshotsPreviewEl.cancelButtonEl;
      case "copy":
        return screenshotsPreviewEl.copyButtonEl;
      case "download":
        return screenshotsPreviewEl.downloadButtonEl;
    }

    return null;
  }

  async waitForPanel() {
    let panel = this.panel;
    await TestUtils.waitForCondition(async () => {
      if (!panel) {
        panel = this.panel;
      }
      return panel && BrowserTestUtils.isVisible(panel);
    });
    return panel;
  }

  async waitForOverlay() {
    const panel = await this.waitForPanel();
    ok(BrowserTestUtils.isVisible(panel), "Panel buttons are visible");

    await TestUtils.waitForCondition(async () => {
      let init = await this.isOverlayInitialized();
      return init;
    });

    await new Promise(r => window.requestAnimationFrame(r));
    info("Overlay is visible");
  }

  async waitForPanelClosed() {
    let panel = this.panel;
    if (!panel) {
      info("waitForPanelClosed: Panel doesnt exist");
      return;
    }
    if (panel.hidden) {
      info("waitForPanelClosed: panel is already hidden");
      return;
    }
    info("waitForPanelClosed: waiting for the panel to become hidden");
    await BrowserTestUtils.waitForMutationCondition(
      panel,
      { attributes: true },
      () => {
        return BrowserTestUtils.isHidden(panel);
      }
    );
    ok(BrowserTestUtils.isHidden(panel), "Panel buttons are hidden");
    info("waitForPanelClosed, panel is hidden: " + panel.hidden);
  }

  async waitForOverlayClosed() {
    await this.waitForPanelClosed();
    await TestUtils.waitForCondition(async () => {
      let init = !(await this.isOverlayInitialized());
      info("Is overlay initialized: " + !init);
      return init;
    });

    await new Promise(r => window.requestAnimationFrame(r));
    info("Overlay is not visible");
  }

  async isOverlayInitialized() {
    return SpecialPowers.spawn(this.browser, [], () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      return screenshotsChild?.overlay?.initialized;
    });
  }

  waitForStateChange(newStateArr) {
    return SpecialPowers.spawn(this.browser, [newStateArr], async stateArr => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );

      await ContentTaskUtils.waitForCondition(() => {
        info(`got ${screenshotsChild.overlay.state}. expected ${stateArr}`);
        return stateArr.includes(screenshotsChild.overlay.state);
      }, `Wait for overlay state to be ${stateArr}`);

      return screenshotsChild.overlay.state;
    });
  }

  async assertStateChange(newState) {
    let currentState = await this.waitForStateChange([newState]);

    is(
      currentState,
      newState,
      `The current state is ${currentState}, expected ${newState}`
    );
  }

  getHoverElementRect() {
    return SpecialPowers.spawn(this.browser, [], async () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      return screenshotsChild.overlay.hoverElementRegion.dimensions;
    });
  }

  isHoverElementRegionValid() {
    return SpecialPowers.spawn(this.browser, [], async () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      return screenshotsChild.overlay.hoverElementRegion.isRegionValid;
    });
  }

  async waitForHoverElementRect(expectedWidth, expectedHeight) {
    return SpecialPowers.spawn(
      this.browser,
      [expectedWidth, expectedHeight],
      async (width, height) => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        let dimensions;
        await ContentTaskUtils.waitForCondition(() => {
          dimensions = screenshotsChild.overlay.hoverElementRegion.dimensions;
          if (dimensions.width === width && dimensions.height === height) {
            return true;
          }
          info(`Got: ${JSON.stringify(dimensions)}`);
          return false;
        }, "The hover element region is the expected width and height");
        return dimensions;
      }
    );
  }

  async waitForSelectionRegionSizeChange(currentWidth) {
    await SpecialPowers.spawn(this.browser, [currentWidth], async currWidth => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );

      let dimensions = screenshotsChild.overlay.selectionRegion.dimensions;
      await ContentTaskUtils.waitForCondition(() => {
        dimensions = screenshotsChild.overlay.selectionRegion.dimensions;
        return dimensions.width !== currWidth;
      }, "Wait for selection box width change");
    });
  }

  /**
   * This will drag an overlay starting at the given startX and startY coordinates and ending
   * at the given endX and endY coordinates.
   *
   * endY should be at least 70px from the bottom of window and endX should be at least
   * 265px from the left of the window. If these requirements are not met then the
   * overlay buttons (cancel, copy, download) will be positioned different from the default
   * and the methods to click the overlay buttons will not work unless the updated
   * position coordinates are supplied.
   * See https://searchfox.org/mozilla-central/rev/af78418c4b5f2c8721d1a06486cf4cf0b33e1e8d/browser/components/screenshots/ScreenshotsOverlayChild.sys.mjs#1789,1798
   * for how the overlay buttons are positioned when the overlay rect is near the bottom or
   * left edge of the window.
   *
   * Note: The distance of the rect should be greater than 40 to enter in the "dragging" state.
   * See https://searchfox.org/mozilla-central/rev/af78418c4b5f2c8721d1a06486cf4cf0b33e1e8d/browser/components/screenshots/ScreenshotsOverlayChild.sys.mjs#809
   *
   * @param {number} startX The starting X coordinate. The left edge of the overlay rect.
   * @param {number} startY The starting Y coordinate. The top edge of the overlay rect.
   * @param {number} endX The end X coordinate. The right edge of the overlay rect.
   * @param {number} endY The end Y coordinate. The bottom edge of the overlay rect.
   */
  async dragOverlay(
    startX,
    startY,
    endX,
    endY,
    expectedStartingState = "crosshairs"
  ) {
    await this.assertStateChange(expectedStartingState);

    mouse.down(startX, startY);

    await this.waitForStateChange(["draggingReady", "resizing"]);
    Assert.ok(true, "The overlay is in the draggingReady or resizing state");

    mouse.move(endX, endY);

    await this.waitForStateChange(["dragging", "resizing"]);

    Assert.ok(true, "The overlay is in the dragging or resizing state");
    // We intentionally turn off this a11y check, because the following mouse
    // event is emitted at the end of the dragging event. Its keyboard
    // accessible alternative is provided and tested elsewhere, therefore
    // this rule check shall be ignored by a11y_checks suite.
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    mouse.up(endX, endY);

    await this.assertStateChange("selected");
    AccessibilityUtils.resetEnv();

    this.endX = endX;
    this.endY = endY;
  }

  /**
   * The touch equivalent of dragOverlay. See dragOverlay for the constraints
   * on the coordinates.
   */
  async touchDragOverlay(
    startX,
    startY,
    endX,
    endY,
    expectedStartingState = "crosshairs"
  ) {
    await this.assertStateChange(expectedStartingState);

    await touch.start(startX, startY, {}, this.browser);
    await this.waitForStateChange(["draggingReady", "resizing"]);

    await touch.move(endX, endY, {}, this.browser);
    await this.waitForStateChange(["dragging", "resizing"]);

    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    await touch.end(endX, endY, {}, this.browser);
    await this.assertStateChange("selected");
    AccessibilityUtils.resetEnv();
  }

  /**
   * Tap a button in the overlay with a synthesized touchstart and touchend.
   *
   * Synthesized touch events produce pointer events but no click, unlike a
   * tap on a real touchscreen, so this only exercises the overlay's
   * touchend handling of buttons.
   *
   * @param {string} id The id of the button in the overlay
   */
  async tapOverlayButton(id) {
    let { x, y } = await SpecialPowers.spawn(this.browser, [id], buttonId => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      let { left, top, width, height } = screenshotsChild.overlay
        .getElementById(buttonId)
        .getBoundingClientRect();
      return { x: left + width / 2, y: top + height / 2 };
    });

    info(`tapping ${id} at ${x}, ${y}`);
    await touch.start(x, y, {}, this.browser);
    await touch.end(x, y, {}, this.browser);
  }

  async moveOverlayViaKeyboard(mover, events) {
    await SpecialPowers.spawn(
      this.browser,
      [mover, events],
      async (moverToFocus, eventsArr) => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );

        let overlay = screenshotsChild.overlay;

        switch (moverToFocus) {
          case "highlight":
            overlay.highlightEl.focus({ focusVisible: true });
            break;
          case "mover-bottomLeft":
            overlay.bottomLeftMover.focus({ focusVisible: true });
            break;
          case "mover-bottomRight":
            overlay.bottomRightMover.focus({ focusVisible: true });
            break;
          case "mover-topLeft":
            overlay.topLeftMover.focus({ focusVisible: true });
            break;
          case "mover-topRight":
            overlay.topRightMover.focus({ focusVisible: true });
            break;
        }

        for (let event of eventsArr) {
          EventUtils.synthesizeKey(
            event.key,
            { type: "keydown", ...event.options },
            content
          );

          await ContentTaskUtils.waitForCondition(
            () => overlay.state === "resizing",
            "Wait for overlay state to be resizing"
          );

          EventUtils.synthesizeKey(
            event.key,
            { type: "keyup", ...event.options },
            content
          );

          await ContentTaskUtils.waitForCondition(
            () => overlay.state === "selected",
            "Wait for overlay state to be selected"
          );
        }
      }
    );
  }

  async scrollContentWindow(x, y) {
    let contentDims = await this.getContentDimensions();
    await SpecialPowers.spawn(
      this.browser,
      [x, y, contentDims],
      async (xPos, yPos, cDims) => {
        content.window.scroll(xPos, yPos);

        info(JSON.stringify(cDims, null, 2));
        const scrollbarHeight = {};
        const scrollbarWidth = {};
        content.window.windowUtils.getScrollbarSize(
          false,
          scrollbarWidth,
          scrollbarHeight
        );

        await ContentTaskUtils.waitForCondition(() => {
          function isCloseEnough(a, b, diff) {
            return Math.abs(a - b) <= diff;
          }

          info(
            `scrollbarWidth: ${scrollbarWidth.value}, scrollbarHeight: ${scrollbarHeight.value}`
          );
          info(
            `scrollX: ${content.window.scrollX}, scrollY: ${content.window.scrollY}, scrollMaxX: ${content.window.scrollMaxX}, scrollMaxY: ${content.window.scrollMaxY}`
          );

          // Sometimes (read intermittently) the scroll width/height will be
          // off by the width/height of the scrollbar when we are expecting the
          // page to be scrolled to the very end. To mitigate this, we check
          // that the below differences are within the scrollbar width/height.
          return (
            (content.window.scrollX === xPos ||
              isCloseEnough(
                cDims.clientWidth + content.window.scrollX,
                cDims.scrollWidth,
                scrollbarWidth.value + 1
              )) &&
            (content.window.scrollY === yPos ||
              isCloseEnough(
                cDims.clientHeight + content.window.scrollY,
                cDims.scrollHeight,
                scrollbarHeight.value + 1
              ))
          );
        }, `Waiting for window to scroll to ${xPos}, ${yPos}`);
      }
    );
  }

  async waitForScrollTo(x, y) {
    await SpecialPowers.spawn(this.browser, [x, y], async (xPos, yPos) => {
      await ContentTaskUtils.waitForCondition(() => {
        info(
          `Got scrollX: ${content.window.scrollX}. scrollY: ${content.window.scrollY}`
        );
        return (
          content.window.scrollX === xPos && content.window.scrollY === yPos
        );
      }, `Waiting for window to scroll to ${xPos}, ${yPos}`);
    });
  }

  async resizeContentWindow(width, height) {
    info(
      `Resizing window to ${width}x${height} from ${innerWidth}x${innerHeight} (outer ${outerWidth}x${outerHeight})`
    );
    let targetW = outerWidth - innerWidth + width;
    let targetH = outerHeight - innerHeight + height;
    window.resizeTo(targetW, targetH);
    await TestUtils.waitForCondition(() => {
      info(
        `Current: ${innerWidth}x${innerHeight} (outer ${outerWidth}x${outerHeight})`
      );
      return outerHeight === targetH && outerWidth === targetW;
    }, "Waiting for window to resize");
  }

  waitForContentMousePosition(left, top) {
    return SpecialPowers.spawn(this.browser, [left, top], async (x, y) => {
      function isCloseEnough(a, b, diff) {
        return Math.abs(a - b) <= diff;
      }

      let cursorX = {};
      let cursorY = {};

      await ContentTaskUtils.waitForCondition(() => {
        content.window.windowUtils.getLastOverWindowPointerLocationInCSSPixels(
          cursorX,
          cursorY
        );
        if (
          isCloseEnough(cursorX.value, x, 1) &&
          isCloseEnough(cursorY.value, y, 1)
        ) {
          return true;
        }
        info(`Got: ${JSON.stringify({ cursorX, cursorY, x, y })}`);
        return false;
      }, `Wait for cursor to be ${x}, ${y}`);
    });
  }

  async clickDownloadButton() {
    let { centerX: x, centerY: y } = await SpecialPowers.spawn(
      this.browser,
      [],
      async () => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        let { left, top, width, height } =
          screenshotsChild.overlay.downloadButton.getBoundingClientRect();
        let centerX = left + width / 2;
        let centerY = top + height / 2;
        return { centerX, centerY };
      }
    );

    info(`clicking download button at ${x}, ${y}`);
    mouse.click(x, y);
  }

  async clickCopyButton() {
    let { centerX: x, centerY: y } = await SpecialPowers.spawn(
      this.browser,
      [],
      async () => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        let { left, top, width, height } =
          screenshotsChild.overlay.copyButton.getBoundingClientRect();
        let centerX = left + width / 2;
        let centerY = top + height / 2;
        return { centerX, centerY };
      }
    );

    info(`clicking copy button at ${x}, ${y}`);
    mouse.click(x, y);
  }

  async clickCancelButton() {
    let { centerX: x, centerY: y } = await SpecialPowers.spawn(
      this.browser,
      [],
      async () => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        let { left, top, width, height } =
          screenshotsChild.overlay.cancelButton.getBoundingClientRect();
        let centerX = left + width / 2;
        let centerY = top + height / 2;
        return { centerX, centerY };
      }
    );

    info(`clicking cancel button at ${x}, ${y}`);
    mouse.click(x, y, {}, this.browser);
  }

  async clickPreviewCancelButton() {
    let { centerX: x, centerY: y } = await SpecialPowers.spawn(
      this.browser,
      [],
      async () => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        let { left, top, width, height } =
          screenshotsChild.overlay.previewCancelButton.getBoundingClientRect();
        let centerX = left + width / 2;
        let centerY = top + height / 2;
        return { centerX, centerY };
      }
    );

    info(`clicking cancel button at ${x}, ${y}`);
    mouse.click(x, y);
  }

  escapeKeyInContent() {
    return SpecialPowers.spawn(this.browser, [], () => {
      EventUtils.synthesizeKey("KEY_Escape", {}, content);
    });
  }

  getTestPageElementRect(elementId = "testPageElement") {
    return SpecialPowers.spawn(this.browser, [elementId], async id => {
      let ele = content.document.getElementById(id);
      return ele.getBoundingClientRect();
    });
  }

  getOverlaySelectionSizeText(elementId = "testPageElement") {
    return SpecialPowers.spawn(this.browser, [elementId], async () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      return screenshotsChild.overlay.selectionSize.textContent;
    });
  }

  async hoverTestPageElement(elementId = "testPageElement") {
    let rect = await this.getTestPageElementRect(elementId);
    let dims = await this.getContentDimensions();

    let x = Math.floor(rect.x + dims.scrollX + rect.width / 2);
    let y = Math.floor(rect.y + dims.scrollY + rect.height / 2);

    mouse.move(x, y);
    await this.waitForHoverElementRect(rect.width, rect.height);
  }

  async clickTestPageElement(elementId = "testPageElement") {
    let rect = await this.getTestPageElementRect(elementId);
    let dims = await this.getContentDimensions();

    let x = Math.floor(rect.x + dims.scrollX + rect.width / 2);
    let y = Math.floor(rect.y + dims.scrollY + rect.height / 2);

    mouse.move(x, y);
    await this.waitForHoverElementRect(rect.width, rect.height);
    mouse.down(x, y);
    await this.assertStateChange("draggingReady");
    mouse.up(x, y);
    await this.assertStateChange("selected");
  }

  async zoomBrowser(zoom) {
    let promise = BrowserTestUtils.waitForContentEvent(this.browser, "resize");
    await SpecialPowers.spawn(this.browser, [zoom], zoomLevel => {
      const { Layout } = ChromeUtils.importESModule(
        "chrome://mochitests/content/browser/accessible/tests/browser/Layout.sys.mjs"
      );
      Layout.zoomDocument(content.document, zoomLevel);
    });
    await promise;
  }

  /**
   * Gets the dialog box
   *
   * @returns The dialog box
   */
  getDialog() {
    let currDialogBox = this.browser.tabDialogBox;
    let manager = currDialogBox.getTabDialogManager();
    let dialogs = manager.hasDialogs && manager.dialogs;
    return dialogs[0];
  }

  assertPanelVisible() {
    info("assertPanelVisible, panel.hidden:" + this.panel?.hidden);
    Assert.ok(
      BrowserTestUtils.isVisible(this.panel),
      "Screenshots panel is visible"
    );
  }

  assertPanelNotVisible() {
    info("assertPanelNotVisible, panel.hidden:" + this.panel?.hidden);
    Assert.ok(
      !this.panel || BrowserTestUtils.isHidden(this.panel),
      "Screenshots panel is not visible"
    );
  }

  /**
   * Copied from screenshots extension
   * Returns a promise that resolves when the clipboard data has changed
   * Otherwise rejects
   */
  waitForRawClipboardChange(epectedWidth, expectedHeight, options = {}) {
    const initialClipboardData = Date.now().toString();
    SpecialPowers.clipboardCopyString(initialClipboardData);

    return TestUtils.waitForCondition(
      async () => {
        let data;
        try {
          data = await this.getImageSizeAndColorFromClipboard(options);
        } catch (e) {
          console.log("Failed to get image/png clipboard data:", e);
          return false;
        }
        if (
          data &&
          initialClipboardData !== data &&
          data.height === expectedHeight &&
          data.width === epectedWidth
        ) {
          return data;
        }
        info(`Got from clipboard: ${JSON.stringify(data, null, 2)}`);
        return false;
      },
      "Waiting for screenshot to copy to clipboard",
      200
    );
  }

  /**
   * Gets the client and scroll demensions on the window
   *
   * @returns {object}
   *   clientHeight The visible height
   *   clientWidth The visible width
   *   scrollHeight The scrollable height
   *   scrollWidth The scrollable width
   *   scrollX The scroll x position
   *   scrollY The scroll y position
   */
  getContentDimensions() {
    return SpecialPowers.spawn(this.browser, [], async function () {
      let {
        innerWidth,
        innerHeight,
        scrollX,
        scrollY,
        scrollMinX,
        scrollMinY,
      } = content.window;

      const scrollbarHeight = {};
      const scrollbarWidth = {};
      content.window.windowUtils.getScrollbarSize(
        false,
        scrollbarWidth,
        scrollbarHeight
      );

      let clientHeight = innerHeight - scrollbarHeight.value;
      let clientWidth = innerWidth - scrollbarWidth.value;

      let docEl = content.document.documentElement;
      let scrollWidth = Math.max(docEl.scrollWidth, clientWidth);
      let scrollHeight = Math.max(docEl.scrollHeight, clientHeight);

      return {
        clientWidth,
        clientHeight,
        scrollWidth,
        scrollHeight,
        scrollX,
        scrollY,
        scrollbarWidth: scrollbarWidth.value,
        scrollbarHeight: scrollbarHeight.value,
        scrollMinX,
        scrollMinY,
      };
    });
  }

  async getScreenshotsOverlayDimensions() {
    return SpecialPowers.spawn(this.browser, [], async () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      Assert.ok(screenshotsChild.overlay.initialized, "The overlay exists");

      let screenshotsContainer = screenshotsChild.overlay.screenshotsContainer;

      await ContentTaskUtils.waitForCondition(() => {
        return !screenshotsContainer.hasAttribute("resizing");
      }, "Waiting for overlay to be done resizing");

      info(
        `${screenshotsContainer.style.width} ${
          screenshotsContainer.style.height
        } ${screenshotsContainer.hasAttribute("resizing")}`
      );

      return {
        scrollWidth: screenshotsContainer.scrollWidth,
        scrollHeight: screenshotsContainer.scrollHeight,
      };
    });
  }

  async waitForSelectionLayerDimensionChange(oldWidth, oldHeight) {
    await SpecialPowers.spawn(
      this.browser,
      [oldWidth, oldHeight],
      async (prevWidth, prevHeight) => {
        let screenshotsChild = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );

        await ContentTaskUtils.waitForCondition(() => {
          let screenshotsContainer =
            screenshotsChild.overlay.screenshotsContainer;
          info(
            `old height: ${prevHeight}. new height: ${screenshotsContainer.scrollHeight}.\nold width: ${prevWidth}. new width: ${screenshotsContainer.scrollWidth}`
          );
          return (
            screenshotsContainer.scrollHeight !== prevHeight &&
            screenshotsContainer.scrollWidth !== prevWidth
          );
        }, "Wait for selection box width change");
      }
    );
  }

  waitForOverlaySizeChangeTo(width, height) {
    return SpecialPowers.spawn(
      this.browser,
      [width, height],
      async (newWidth, newHeight) => {
        await ContentTaskUtils.waitForCondition(() => {
          let { innerHeight, innerWidth } = content.window;
          const scrollbarHeight = {};
          const scrollbarWidth = {};
          content.window.windowUtils.getScrollbarSize(
            false,
            scrollbarWidth,
            scrollbarHeight
          );
          let clientWidth = innerWidth - scrollbarWidth.value;
          let clientHeight = innerHeight - scrollbarHeight.value;
          let docEl = content.document.documentElement;
          let scrollWidth = Math.max(docEl.scrollWidth, clientWidth);
          let scrollHeight = Math.max(docEl.scrollHeight, clientHeight);
          info(
            `${scrollHeight}, ${newHeight}, ${scrollWidth}, ${newWidth}, ${content.window.scrollMaxX}`
          );
          return scrollHeight === newHeight && scrollWidth === newWidth;
        }, "Wait for document size change");
      }
    );
  }

  getSelectionRegionDimensions() {
    return SpecialPowers.spawn(this.browser, [], async () => {
      let screenshotsChild = content.windowGlobalChild.getActor(
        "ScreenshotsComponent"
      );
      Assert.ok(screenshotsChild.overlay.initialized, "The overlay exists");

      return screenshotsChild.overlay.selectionRegion.dimensions;
    });
  }

  waitForContentEventOnce(event) {
    return BrowserTestUtils.waitForContentEvent(this.browser, event, true);
  }

  /**
   * Copied from screenshots extension
   * A helper that returns the size of the image that was just put into the clipboard by the
   * :screenshot command.
   *
   * @return The {width, height, color} dimension and color object.
   */
  async getImageSizeAndColorFromClipboard(options = {}) {
    let flavor = "image/png";
    let image = getRawClipboardData(flavor);
    if (!image) {
      return false;
    }

    // Due to the differences in how images could be stored in the clipboard the
    // checks below are needed. The clipboard could already provide the image as
    // byte streams or as image container. If it's not possible obtain a
    // byte stream, the function throws.

    if (image instanceof Ci.imgIContainer) {
      image = Cc["@mozilla.org/image/tools;1"]
        .getService(Ci.imgITools)
        .encodeImage(image, flavor);
    }

    if (!(image instanceof Ci.nsIInputStream)) {
      throw new Error("Unable to read image data");
    }

    const binaryStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
    binaryStream.setInputStream(image);
    const available = binaryStream.available();
    const buffer = new ArrayBuffer(available);
    info(
      `${binaryStream.readArrayBuffer(
        available,
        buffer
      )} read, ${available} available`
    );

    // We are going to load the image in the content page to measure its size.
    // We don't want to insert the image directly in the browser's document
    // which could mess all sorts of things up
    return SpecialPowers.spawn(
      this.browser,
      [buffer, options],
      async function (_buffer, _options) {
        const img = content.document.createElement("img");
        const loaded = new Promise(r => {
          img.addEventListener("load", r, { once: true });
        });
        const url = content.URL.createObjectURL(
          new Blob([_buffer], { type: "image/png" })
        );

        img.src = url;
        content.document.documentElement.appendChild(img);

        info("Waiting for the clipboard image to load in the content page");
        await loaded;

        let canvas = content.document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "html:canvas"
        );
        let context = canvas.getContext("2d");
        canvas.width = img.width;
        canvas.height = img.height;
        context.drawImage(img, 0, 0);
        let topLeft = context.getImageData(0, 0, 1, 1);
        let topRight = context.getImageData(img.width - 1, 0, 1, 1);
        let bottomLeft = context.getImageData(0, img.height - 1, 1, 1);
        let bottomRight = context.getImageData(
          img.width - 1,
          img.height - 1,
          1,
          1
        );

        let allPixels = null;
        if (_options.allPixels) {
          allPixels = context.getImageData(0, 0, img.width, img.height);
        }

        img.remove();
        content.URL.revokeObjectURL(url);

        return {
          width: img.width,
          height: img.height,
          color: {
            topLeft: topLeft.data,
            topRight: topRight.data,
            bottomLeft: bottomLeft.data,
            bottomRight: bottomRight.data,
          },
          allPixels: allPixels?.data,
        };
      }
    );
  }
}

/**
 * Create a new tab in the current window and wait for its browser to be loaded
 *
 * @param {string} [url] Optional url to load, defaults to value of TEST_PAGE
 * @returns The tab
 */
async function addTabAndLoadBrowser(url = TEST_PAGE) {
  const tab = BrowserTestUtils.addTab(gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  return tab;
}

/**
 * Get the raw clipboard data
 *
 * @param flavor Type of data to get from clipboard
 * @returns The data from the clipboard
 */
function getRawClipboardData(flavor) {
  const whichClipboard = Services.clipboard.kGlobalClipboard;
  const xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  xferable.init(null);
  xferable.addDataFlavor(flavor);
  Services.clipboard.getData(
    xferable,
    whichClipboard,
    SpecialPowers.wrap(window).browsingContext.currentWindowContext
  );
  let data = {};
  try {
    // xferable.getTransferData(flavor, data);
    xferable.getAnyTransferData({}, data);
    info(JSON.stringify(data, null, 2));
  } catch (e) {
    info(e);
  }
  data = data.value || null;
  return data;
}

/**
 * Synthesize a mouse event on an element
 * in the viewport.
 *
 * @param {string} selector: The node selector to get the node target for the event.
 * @param {number} x
 * @param {number} y
 * @param {object} options: Options that will be passed to BrowserTestUtils.synthesizeMouse
 */
async function safeSynthesizeMouseEventInContentPage(
  selector,
  x,
  y,
  options = {},
  browser
) {
  let context;
  if (!browser) {
    context = gBrowser.selectedBrowser.browsingContext;
  } else {
    context = browser.browsingContext;
  }
  await BrowserTestUtils.synthesizeMouse(selector, x, y, options, context);
}

/**
 * Synthesize a key event on an element
 * in the viewport.
 *
 * @param {string} key The key
 * @param {object} options: Options that will be passed to BrowserTestUtils.synthesizeKey
 */
async function safeSynthesizeKeyEventInContentPage(aKey, options, browser) {
  let context;
  if (!browser) {
    context = gBrowser.selectedBrowser.browsingContext;
  } else {
    context = browser.browsingContext;
  }
  await BrowserTestUtils.synthesizeKey(aKey, options, context);
}

/**
 * Synthesize a touch event on an element
 * in the viewport.
 *
 * @param {string} selector: The node selector to get the node target for the event.
 * @param {number} x
 * @param {number} y
 * @param {object} options: Options that will be passed to BrowserTestUtils.synthesizeTouch
 */
async function safeSynthesizeTouchEventInContentPage(
  selector,
  x,
  y,
  options = {},
  browser
) {
  let context;
  if (!browser) {
    context = gBrowser.selectedBrowser.browsingContext;
  } else {
    context = browser.browsingContext;
  }
  await BrowserTestUtils.synthesizeTouch(selector, x, y, options, context);
}

// Should the test include a setup step to ensure the screenshots button is placed on the
// nav-bar toolbar? A lot of our tests use this as the entry point, so default true.
var gPlaceButtonOnToolbar = true;

function ensureButtonOnToolbar() {
  CustomizableUI.addWidgetToArea(
    "screenshot-button",
    CustomizableUI.AREA_NAVBAR,
    0
  );
  let screenshotBtn = document.getElementById("screenshot-button");
  Assert.ok(screenshotBtn, "The screenshots button exists in the document");
  Assert.equal(
    CustomizableUI.getPlacementOfWidget("screenshot-button")?.area,
    "nav-bar",
    "The screenshots button is on the nav-bar"
  );
}

async function cleanup() {
  CustomizableUI.reset();
  Services.prefs.clearUserPref(
    "screenshots.browser.component.last-screenshot-method"
  );
  Services.prefs.clearUserPref(
    "screenshots.browser.component.last-saved-method"
  );

  if (DownloadsPanel.isPanelShowing) {
    let hiddenPromise = BrowserTestUtils.waitForEvent(
      DownloadsPanel.panel,
      "popuphidden"
    );
    DownloadsPanel.hidePanel();
    await hiddenPromise;
    info(
      `downloads panel should not be visible: ${DownloadsPanel.isPanelShowing}`
    );
  }
}

add_setup(async () => {
  if (gPlaceButtonOnToolbar) {
    ensureButtonOnToolbar();
  }
  registerCleanupFunction(cleanup);
});

function getContentDevicePixelRatio(browser) {
  return SpecialPowers.spawn(browser, [], async function () {
    return content.window.devicePixelRatio;
  });
}

async function clearAllTelemetryEvents() {
  // Clear everything.
  info("Clearing all telemetry events");
  await TestUtils.waitForCondition(() => {
    Services.telemetry.clearEvents();
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    );
    let content = events.content;
    let parent = events.parent;

    return (!content && !parent) || (!content.length && !parent.length);
  });
}

async function waitForScreenshotsEventCount(count, process = "parent") {
  await TestUtils.waitForCondition(
    () => {
      let events = TelemetryTestUtils.getEvents(
        { category: "screenshots" },
        { process }
      );

      info(`Got ${events?.length} event(s)`);
      info(`Actual events: ${JSON.stringify(events, null, 2)}`);
      return events.length === count ? events : null;
    },
    `Waiting for ${count} ${process} event(s).`,
    200,
    100
  );
}

async function assertScreenshotsEvents(
  expectedEvents,
  process = "parent",
  clearEvents = true
) {
  info(`Expected events: ${JSON.stringify(expectedEvents, null, 2)}`);
  // Make sure we have recorded the correct number of events
  await waitForScreenshotsEventCount(expectedEvents.length, process);

  TelemetryTestUtils.assertEvents(
    expectedEvents,
    { category: "screenshots" },
    { clear: clearEvents, process }
  );
}
