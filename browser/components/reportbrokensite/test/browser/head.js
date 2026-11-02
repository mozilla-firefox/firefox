/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const { ReportBrokenSite, ViewState } = ChromeUtils.importESModule(
  "moz-src:///browser/components/reportbrokensite/ReportBrokenSite.sys.mjs"
);

const BASE_URL =
  "https://example.com/browser/browser/components/reportbrokensite/test/browser/";

const REPORTABLE_PAGE_URL = "https://example.com";

const REPORTABLE_PAGE_URL2 = REPORTABLE_PAGE_URL.replace(".com", ".org");

const REPORTABLE_PAGE_URL3 = `${BASE_URL}example_report_page.html`;

const SUMO_BASE_URL = Services.urlFormatter.formatURLPref(
  "app.support.baseURL"
);
const LEARN_MORE_TEST_URL = `${SUMO_BASE_URL}report-broken-site`;

const NEW_REPORT_ENDPOINT_TEST_URL = `${BASE_URL}sendMoreInfoTestEndpoint.html`;

// The test-framework will crash if we try to open these URLs outside of private/strict mode
// and wait for the given number of content-blocking events.
const URLS_NEEDING_CONTENT_BLOCKING = {
  [REPORTABLE_PAGE_URL3]: 3,
};

const PREFS = {
  DATAREPORTING_ENABLED: "datareporting.healthreport.uploadEnabled",
  REPORTER_ENABLED: "ui.new-webcompat-reporter.enabled",
  REASON: "ui.new-webcompat-reporter.reason-dropdown",
  SCREENSHOTS: "ui.new-webcompat-reporter.screenshots.enabled",
  SHOW_SEND_MORE_INFO: "ui.new-webcompat-reporter.show-send-more-info-link",
  NEW_REPORT_ENDPOINT: "ui.new-webcompat-reporter.new-report-endpoint",
  TOUCH_EVENTS: "dom.w3c_touch_events.enabled",
  USE_ACCESSIBILITY_THEME: "ui.useAccessibilityTheme",
  PHISHING_REPORT_URL: "browser.safebrowsing.reportPhishURL",
};

function add_common_setup() {
  add_setup(async function () {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.urlbar.trustPanel.featureGate", false],
        [PREFS.NEW_REPORT_ENDPOINT, NEW_REPORT_ENDPOINT_TEST_URL],

        // set touch events to auto-detect, as the pref gets set to 1 somewhere
        // while tests are running, making hasTouchScreen checks unreliable.
        [PREFS.TOUCH_EVENTS, 2],
      ],
    });
    registerCleanupFunction(function () {
      for (const prefName of Object.values(PREFS)) {
        Services.prefs.clearUserPref(prefName);
      }
      Services.telemetry.clearEvents();
      Services.fog.testResetFOG();
    });
  });
}

function areObjectsEqual(actual, expected, path = "") {
  if (typeof expected == "function") {
    try {
      const passes = expected(actual);
      if (!passes) {
        info(`${path} not pass check function: ${actual}`);
      }
      return passes;
    } catch (e) {
      info(`${path} threw exception:
        got: ${typeof actual}, ${actual}
        expected: ${typeof expected}, ${expected}
        exception: ${e.message}
          ${e.stack}`);
      return false;
    }
  }

  if (typeof actual != typeof expected) {
    info(`${path} types do not match:
      got: ${typeof actual}, ${actual}
      expected: ${typeof expected}, ${expected}`);
    return false;
  }
  if (typeof actual != "object" || actual === null || expected === null) {
    if (actual !== expected) {
      info(`${path} does not match
        got: ${typeof actual}, ${actual}
        expected: ${typeof expected}, ${expected}`);
      return false;
    }
    return true;
  }
  const prefix = path ? `${path}.` : path;
  for (const [key, val] of Object.entries(actual)) {
    if (!(key in expected)) {
      info(`Extra ${prefix}${key}: ${val}`);
      return false;
    }
  }
  let result = true;
  for (const [key, expectedVal] of Object.entries(expected)) {
    if (key in actual) {
      if (!areObjectsEqual(actual[key], expectedVal, `${prefix}${key}`)) {
        result = false;
      }
    } else {
      info(`Missing ${prefix}${key} (${expectedVal})`);
      result = false;
    }
  }
  return result;
}

function prettyElements(elems) {
  elems = Array.isArray(elems) ? elems : [elems];
  return elems
    .map(e => {
      let { id, className, nodeName } = e;
      if (!nodeName) {
        return e;
      }
      id = id ? `#${id}` : "";
      className = className ? `.${className.split(" ").join(".")}` : "";
      return `${nodeName}${id}${className}`;
    })
    .join(" and ");
}

async function waitForAllElems(elems, what, condition, msg) {
  elems = Array.isArray(elems) ? elems : [elems];
  const prettyElems = prettyElements(elems);
  msg = msg ? `${msg}: ` : "";
  msg = `${msg}waiting for ${prettyElems} to ${what}`;
  info(msg);
  try {
    return await TestUtils.waitForCondition(
      () => !elems.some(e => !condition(e)),
      msg
    );
  } catch (err) {
    ok(false, `${msg}: ${err}`);
    throw err;
  }
}

function getStyle(elem, style) {
  return elem.documentGlobal.getComputedStyle(elem)[style];
}

function isOpaque(elems, msg) {
  return waitForAllElems(
    elems,
    "have opacity:1",
    e => getStyle(e, "opacity") == 1,
    msg
  );
}

function isTransparent(elems, msg) {
  return waitForAllElems(
    elems,
    "have opacity:0",
    e => getStyle(e, "opacity") == 0,
    msg
  );
}

function isDisplayed(elems, msg) {
  return waitForAllElems(
    elems,
    "not have display:none",
    e => getStyle(e, "display") != "none",
    msg
  );
}

function isNotDisplayed(elems, msg) {
  return waitForAllElems(
    elems,
    "have display:none",
    e => getStyle(e, "display") == "none",
    msg
  );
}

function isVisible(elems, msg) {
  return waitForAllElems(
    elems,
    "be visible",
    e => BrowserTestUtils.isVisible(e),
    msg
  );
}

function isNotVisible(elems, msg) {
  return waitForAllElems(
    elems,
    "not be visible",
    e => !BrowserTestUtils.isVisible(e),
    msg
  );
}

function isPressed(elems, msg) {
  return waitForAllElems(elems, "be pressed", e => e.pressed, msg);
}

function isNotPressed(elems, msg) {
  return waitForAllElems(elems, "not be pressed", e => !e.pressed, msg);
}

function isHidden(elems, msg) {
  return waitForAllElems(elems, "be hidden", e => e.hidden, msg);
}

function isNotHidden(elems, msg) {
  return waitForAllElems(elems, "not be hidden", e => !e.hidden, msg);
}

function isDisabled(elems, msg) {
  return waitForAllElems(elems, "be disabled", e => e.disabled, msg);
}

function isNotDisabled(elems, msg) {
  return waitForAllElems(elems, "not be disabled", e => !e.disabled, msg);
}

async function withNewTab(options, taskFn) {
  if (typeof options == "string") {
    options = {
      url: options,
      window: Services.wm.getMostRecentWindow("navigator:browser"),
    };
  }

  let { url, window, private } = options;

  const expectedContentBlockingEvents = URLS_NEEDING_CONTENT_BLOCKING[url];
  private = expectedContentBlockingEvents ?? Boolean(private);

  let closeWindowWhenDone = false;
  if (!window || private !== window.browsingContext.usePrivateBrowsing) {
    closeWindowWhenDone = true;
    window = await BrowserTestUtils.openNewBrowserWindow({
      private,
    });
  }

  const expectedContentBlockedPromise = waitForContentBlockingEvent(
    expectedContentBlockingEvents,
    window
  );
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser: window.gBrowser,
    url,
  });

  await expectedContentBlockedPromise;

  await taskFn(window, tab);

  BrowserTestUtils.removeTab(tab);
  if (closeWindowWhenDone) {
    await BrowserTestUtils.closeWindow(window);
  }
}

async function navigateOnTab(tab, url) {
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
}

// This is a clone of waitForEvent which times out and throws an exception
// to keep things moving along in our tests.
async function waitForEvent(
  subject,
  eventName,
  capture,
  checkFn,
  wantsUntrusted,
  msg
) {
  msg ??= `waiting for ${eventName} event on ${prettyElements(subject)}`;
  info(msg);

  const startTime = ChromeUtils.now();
  const innerWindowId = subject.documentGlobal?.windowGlobalChild.innerWindowId;

  // await the promise here so we see the actual stack trace, rather
  // than the vague one a "promise rejection not handled" error gives,
  // without having to try/catch everywhere in our tests.
  return await new Promise((resolve, reject) => {
    let timeout;
    let removed = false;
    function cleanup() {
      if (timeout) {
        (subject.documentGlobal ?? subject).clearTimeout(timeout);
        timeout = null;
      }
      removed = true;
      // Avoid keeping references to objects after the promise resolves.
      subject = null;
      checkFn = null;
    }

    function listener(event) {
      try {
        if (checkFn && !checkFn(event)) {
          return;
        }
        subject.removeEventListener(eventName, listener, capture);
        cleanup();
        TestUtils.executeSoon(() => {
          ChromeUtils.addProfilerMarker(
            "BrowserTestUtils",
            { startTime, category: "Test", innerWindowId },
            "waitForEvent: " + eventName
          );
          resolve(event);
        });
      } catch (ex) {
        try {
          subject.removeEventListener(eventName, listener, capture);
        } catch (ex2) {
          // Maybe the provided object does not support removeEventListener.
        }
        cleanup();
        TestUtils.executeSoon(() => reject(ex));
      }
    }

    subject.addEventListener(eventName, listener, capture, wantsUntrusted);

    timeout = (subject.documentGlobal ?? subject).setTimeout(() => {
      subject.removeEventListener(eventName, listener, capture);
      cleanup();
      TestUtils.executeSoon(() => reject("timed out"));
    }, 5000);

    TestUtils.promiseTestFinished?.then(() => {
      if (removed) {
        return;
      }

      subject.removeEventListener(eventName, listener, capture);
      let text = eventName + " listener";
      if (subject.id) {
        text += ` on #${subject.id}`;
      }
      text += " not removed before the end of test";
      reject(text);
      ChromeUtils.addProfilerMarker(
        "BrowserTestUtils",
        { startTime, category: "Test", innerWindowId },
        "waitForEvent: " + text
      );
    });
  });
}

function switchToWindow(win) {
  const promises = [waitForEvent(win, "focus"), waitForEvent(win, "activate")];
  win.focus();
  return Promise.all(promises);
}

function isSelectedTab(win, tab) {
  const selectedTab = win.document.querySelector(".tabbrowser-tab[selected]");
  is(selectedTab, tab);
}

function ensureReportBrokenSitePreffedOn() {
  Services.prefs.setBoolPref(PREFS.DATAREPORTING_ENABLED, true);
  Services.prefs.setBoolPref(PREFS.REPORTER_ENABLED, true);
}

function ensureReportBrokenSitePreffedOff() {
  Services.prefs.setBoolPref(PREFS.REPORTER_ENABLED, false);
}

function enableSendMoreInfo() {
  Services.prefs.setBoolPref(PREFS.SHOW_SEND_MORE_INFO, true);
}

function disableSendMoreInfo() {
  Services.prefs.setBoolPref(PREFS.SHOW_SEND_MORE_INFO, false);
}

function enableScreenshots() {
  Services.prefs.setBoolPref(PREFS.SCREENSHOTS, true);
}

function disableScreenshots() {
  Services.prefs.setBoolPref(PREFS.SCREENSHOTS, false);
}

function ensureProtectionsPanelHidden(test) {
  const { hidden } = document.getElementById(
    "tracking-protection-icon-container"
  );
  ok(hidden, `Protections panel disabled ${test}`);
}

function isMenuItemEnabled(menuItem, itemDesc) {
  ok(!menuItem.hidden, `${itemDesc} menu item is shown`);
  ok(!menuItem.disabled, `${itemDesc} menu item is enabled`);
}

function isMenuItemHidden(menuItem, itemDesc) {
  ok(
    !menuItem || menuItem.hidden || !BrowserTestUtils.isVisible(menuItem),
    `${itemDesc} menu item is hidden`
  );
}

function isMenuItemDisabled(menuItem, itemDesc) {
  ok(!menuItem.hidden, `${itemDesc} menu item is shown`);
  ok(menuItem.disabled, `${itemDesc} menu item is disabled`);
}

function waitForWebcompatComTab(gBrowser) {
  info("waiting for a new tab to open to webcompat.com");
  return BrowserTestUtils.waitForNewTab(gBrowser, NEW_REPORT_ENDPOINT_TEST_URL);
}

class ReportBrokenSiteHelper {
  sourceMenu = undefined;
  win = undefined;

  constructor(sourceMenu) {
    this.sourceMenu = sourceMenu;
    this.win = sourceMenu.win;
  }

  getViewNode(id) {
    return PanelMultiView.getViewNode(this.win.document, id);
  }

  get mainView() {
    return this.getViewNode("report-broken-site-popup-mainView");
  }

  get detailsView() {
    return this.getViewNode("report-broken-site-popup-detailsView");
  }

  get previewView() {
    return this.getViewNode("report-broken-site-popup-previewView");
  }

  get sentView() {
    return this.getViewNode("report-broken-site-popup-reportSentView");
  }

  get openPanel() {
    return this.mainView?.closest("panel");
  }

  get opened() {
    return this.openPanel?.hasAttribute("panelopen");
  }

  click(elem, options = {}) {
    return new Promise(r => {
      elem.scrollIntoView({ behavior: "instant" });
      // An inline element wrapped across lines has a bounding box spanning
      // every line it touches, whose center can land in the empty space
      // beside the text. Aim at the first line box in that case.
      const rects = elem.getClientRects();
      if (rects.length > 1) {
        const bounds = elem.getBoundingClientRect();
        return EventUtils.synthesizeMouse(
          elem,
          rects[0].x - bounds.x + rects[0].width / 2,
          rects[0].y - bounds.y + rects[0].height / 2,
          options,
          this.win,
          r
        );
      }
      return EventUtils.synthesizeMouseAtCenter(elem, options, this.win, r);
    });
  }

  open(triggerMenuItem) {
    return this.#clickAndWaitForViewToShowNoAsserts(
      triggerMenuItem,
      this.mainView
    );
  }

  async clickAndWaitForEvent(toClick, event, actualEventTarget) {
    const wait = waitForEvent(
      actualEventTarget ?? toClick,
      event,
      undefined,
      undefined,
      `clicking ${prettyElements(toClick)} and waiting for ${event} event on ${prettyElements(actualEventTarget ?? toClick)}`
    );
    await this.click(toClick);
    await wait;
  }

  #assertCanClick(toClick) {
    if (!this.visibleView) {
      throw new Error(`can't click ${toClick}: no panel view is open/visible`);
    }
    if (!BrowserTestUtils.isVisible(toClick)) {
      throw new Error(`can't click ${toClick}: is not visible`);
    }
    if (toClick.disabled) {
      throw new Error(`can't click ${toClick}: is disabled`);
    }
  }

  clickAndWaitForViewToHide(toClick, targetView = this.visibleView) {
    info(
      `clicking ${prettyElements(toClick)} and waiting for ${targetView.id} to hide`
    );
    this.#assertCanClick(toClick);
    return this.clickAndWaitForEvent(toClick, "ViewHiding", targetView);
  }

  async clickAndWaitForViewToShow(toClick, targetView, targetFocus) {
    info(
      `clicking ${prettyElements(toClick)} and waiting for ${targetView.id} to show`
    );
    this.#assertCanClick(toClick);
    return this.#clickAndWaitForViewToShowNoAsserts(
      toClick,
      targetView,
      targetFocus
    );
  }

  async #clickAndWaitForViewToShowNoAsserts(toClick, targetView, targetFocus) {
    const promises = [];
    if (targetView) {
      if (targetView.nodeName == "panel") {
        promises.push(waitForEvent(targetView, "popupshown"));
      } else {
        promises.push(this.waitForViewToShow(targetView));
      }
    } else {
      promises.push(waitForEvent(this.visibleView, "ViewHiding"));
    }
    if (targetFocus) {
      promises.push(waitForEvent(targetFocus, "focus"));
    }
    await this.click(toClick);
    await Promise.all(promises);
  }

  async clickSend() {
    const { sendButton, sentView } = this;
    const promise = this.waitForViewToShow(sentView);
    sendButton.click();
    await promise;
    await isNotDisabled(sendButton, "send button should not be disabled");
  }

  clickCancel() {
    const promise = this.waitForViewToHide(this.visibleView);
    this.cancelButton.click();
    return promise;
  }

  clickOkay() {
    return this.clickAndWaitForViewToHide(this.okayButton, this.sentView);
  }

  clickPreview() {
    const promise = this.waitForViewToShow(this.previewView);
    this.previewButton.click();
    return promise;
  }

  clickReason(reason) {
    const selector = `#report-broken-site-popup-reason-${reason || "load"}`;
    const button = this.mainView.querySelector(selector);
    if (!button) {
      throw new Error(
        `No ${selector} button to click on in ${this.visibleView.id}`
      );
    }
    if (!BrowserTestUtils.isVisible(button)) {
      throw new Error(`${reason} button is not visible`);
    }
    return this.clickAndWaitForViewToShow(button, this.detailsView);
  }

  waitForSendMoreInfoTab() {
    return BrowserTestUtils.waitForNewTab(
      this.win.gBrowser,
      NEW_REPORT_ENDPOINT_TEST_URL
    );
  }

  async clickSendMoreInfo() {
    const { sendMoreInfoButton, win } = this;
    const newTabPromise = waitForWebcompatComTab(win.gBrowser);
    await isVisible(sendMoreInfoButton);
    this.click(sendMoreInfoButton);
    const newTab = await newTabPromise;
    const receivedData = await SpecialPowers.spawn(
      newTab.linkedBrowser,
      [],
      async function () {
        await content.wrappedJSObject.messageArrived;
        return content.wrappedJSObject.message;
      }
    );
    this.win.gBrowser.removeCurrentTab();
    return receivedData;
  }

  async clickDeceptiveSiteReport() {
    // change the target report URL to a local one so the test framework won't crash.
    const originalURL = Services.prefs.getStringPref(PREFS.PHISHING_REPORT_URL);
    const expectedURL = NEW_REPORT_ENDPOINT_TEST_URL + "?";
    Services.prefs.setStringPref(PREFS.PHISHING_REPORT_URL, expectedURL);

    const newTabPromise = BrowserTestUtils.waitForNewTab(this.win.gBrowser, u =>
      u.startsWith(expectedURL)
    );
    const button = this.visibleView.querySelector(
      "#report-broken-site-popup-reason-deceptive"
    );
    this.click(button);
    await newTabPromise;
    this.win.gBrowser.removeCurrentTab();

    Services.prefs.setStringPref(PREFS.PHISHING_REPORT_URL, originalURL);
  }

  get visibleView() {
    return [
      this.mainView,
      this.detailsView,
      this.previewView,
      this.sentView,
    ].filter(view => BrowserTestUtils.isVisible(view))[0];
  }

  get backButton() {
    return this.visibleView.querySelector(".subviewbutton-back");
  }

  async clickBack() {
    const { visibleView } = this;
    let targetView;
    if (visibleView == this.detailsView) {
      targetView = this.mainView;
    } else if (visibleView == this.previewView) {
      targetView = this.detailsView;
    } else if (visibleView == this.mainView) {
      await this.clickAndWaitForViewToHide(this.backButton);
      return;
    } else {
      throw new Error(
        `Can't click back; not on a view with a back button: ${visibleView}`
      );
    }
    await this.clickAndWaitForViewToShow(this.backButton, targetView);
  }

  close() {
    if (this.opened) {
      this.openPanel?.hidePopup(false);
    }
    ViewState.get(this.win.document).reset();
    this.sourceMenu?.close();
  }

  // Data getters
  get url() {
    return ViewState.get(this.win.document).url;
  }

  get reason() {
    return ViewState.get(this.win.document).reason;
  }

  get description() {
    return this.descriptionTextarea.value;
  }

  // UI element getters
  get urlInputs() {
    return [
      this.mainView.querySelector("url-input"),
      this.detailsView.querySelector("url-input"),
    ];
  }

  get urlComponent() {
    return this.visibleView.querySelector("url-input");
  }

  get descriptionInvalidMessage() {
    return this.visibleView.querySelector(
      "#report-broken-site-details-description-error"
    );
  }

  get progressionButtons() {
    return [...this.visibleView.querySelectorAll(".progression")];
  }

  get reasonButtons() {
    return [...this.mainView.querySelectorAll(".reason-button")];
  }

  get previewSummaries() {
    return [...this.previewView.querySelectorAll("summary")];
  }

  get descriptionTextarea() {
    return this.getViewNode("report-broken-site-popup-description");
  }

  get learnMoreLink() {
    return this.getViewNode("report-broken-site-popup-learn-more-link");
  }

  get screenshotToggle() {
    return this.getViewNode("report-broken-site-popup-screenshot-toggle");
  }

  get sendMoreInfoButton() {
    return this.getViewNode("report-broken-site-popup-send-more-info-button");
  }

  get blockedTrackersToggle() {
    return this.getViewNode("report-broken-site-popup-blocked-trackers-toggle");
  }

  get sendButton() {
    return this.visibleView.querySelector(".send-button");
  }

  get cancelButton() {
    return this.visibleView.querySelector(".cancel-button");
  }

  get okayButton() {
    return this.getViewNode("report-broken-site-popup-okay-button");
  }

  get previewButton() {
    return this.getViewNode("report-broken-site-popup-preview-button");
  }

  get previewItems() {
    return this.getViewNode("report-broken-site-panel-preview-items");
  }

  // Test helpers

  #setInput(input, value) {
    input.value = value;
    input.dispatchEvent(
      new UIEvent("input", { bubbles: true, view: this.win })
    );
    input.dispatchEvent(
      new UIEvent("change", { bubbles: true, view: this.win })
    );
  }

  setURL(value) {
    this.#setInput(this.urlComponent.input, value);
  }

  setDescription(value) {
    this.#setInput(this.descriptionTextarea, value);
  }

  get hasScreenshot() {
    return ViewState.get(this.win.document).noScreenshot;
  }

  get hasBlockedTrackers() {
    const state = ViewState.get(this.win.document);
    return URLS_NEEDING_CONTENT_BLOCKING[this.url] && !state.noBlockedTrackers;
  }

  get availableTabSpecificPreviewItems() {
    const { hasBlockedTrackers, screenshot } = this;
    return [...this.previewItems.querySelectorAll(".tab-specific-data")].filter(
      i =>
        (hasBlockedTrackers ||
          !i.classList.contains("preview-blockedOrigins")) &&
        (screenshot || !i.classList.contains("preview-screenshot"))
    );
  }

  async reportData() {
    const state = ViewState.get(this.win.document);
    return await state.currentTabWebcompatDetailsPromise;
  }

  waitForViewToShow(view) {
    return waitForEvent(view, "ViewShown");
  }

  waitForViewToHide(view) {
    return waitForEvent(view, "ViewHiding");
  }

  set screenshot(dataURI) {
    const state = ViewState.get(this.win.document);
    state.screenshot = dataURI;
  }

  isProperlyReset() {
    const { spec } = this.win.gBrowser.selectedBrowser.currentURI;
    ok(
      !this.urlInputs.some(i => i.input && i.input.value != spec),
      "URL inputs were properly reset"
    );
    is(this.reason, "", "Reason was properly reset");
    is(this.description, "", "Description was properly reset");
    ok(!this.blockedTrackersToggle.pressed, "blocked trackers toggle is reset");
  }

  async pressKeyAndAwait(event, key, options = {}) {
    if (!event.then) {
      event = waitForEvent(
        this.win,
        event,
        true,
        undefined,
        `pressing ${key} and waiting for ${event} event on window`
      );
    }
    await EventUtils.synthesizeKey(key, options, this.win);
    return event;
  }

  async pressKeyAndGetFocus(key, options) {
    return (await this.pressKeyAndAwait("focus", key, options)).target;
  }

  async tabTo(match) {
    const window = this.win;
    const config = { window };
    info(`Tabbing to ${match}`);
    let initial = window.document.activeElement;
    let candidate = initial;
    do {
      if (candidate.matches(match)) {
        return candidate;
      }
      candidate = await this.pressKeyAndGetFocus("VK_TAB", config);
    } while (candidate && candidate !== initial);
    return undefined;
  }
}

class MenuHelper {
  menuDescription = undefined;

  win = undefined;

  constructor(win = window) {
    this.win = win;
  }

  getViewNode(id) {
    return PanelMultiView.getViewNode(this.win.document, id);
  }

  get showsBackButton() {
    return true;
  }

  get reportBrokenSite() {
    throw new Error("Should be defined in derived class");
  }

  get popup() {
    throw new Error("Should be defined in derived class");
  }

  get opened() {
    return this.popup?.hasAttribute("panelopen");
  }

  async open() {}

  async close() {}

  isReportBrokenSiteDisabled() {
    return isMenuItemDisabled(this.reportBrokenSite, this.menuDescription);
  }

  isReportBrokenSiteEnabled() {
    return isMenuItemEnabled(this.reportBrokenSite, this.menuDescription);
  }

  isReportBrokenSiteHidden() {
    return isMenuItemHidden(this.reportBrokenSite, this.menuDescription);
  }

  async clickReportBrokenSiteAndAwaitWebCompatTabData() {
    const newTabPromise = waitForWebcompatComTab(this.win.gBrowser);
    await this.clickReportBrokenSite();
    const newTab = await newTabPromise;
    const receivedData = await SpecialPowers.spawn(
      newTab.linkedBrowser,
      [],
      async function () {
        await content.wrappedJSObject.messageArrived;
        return content.wrappedJSObject.message;
      }
    );

    this.win.gBrowser.removeCurrentTab();
    return receivedData;
  }

  async clickReportBrokenSite() {
    if (!this.opened) {
      await this.open();
    }
    isMenuItemEnabled(this.reportBrokenSite, this.menuDescription);
    const rbs = new ReportBrokenSiteHelper(this);
    rbs.click(this.reportBrokenSite);
    return rbs;
  }

  async openReportBrokenSite() {
    if (!this.opened) {
      await this.open();
    }
    isMenuItemEnabled(this.reportBrokenSite, this.menuDescription);
    const rbs = new ReportBrokenSiteHelper(this);
    const promise = rbs.waitForViewToShow(rbs.mainView);
    await rbs.open(this.reportBrokenSite);
    await promise;
    return rbs;
  }

  async openReportBrokenSiteToDetailsPanel({
    url,
    reason = "load",
    description,
  } = {}) {
    let rbs = await this.openReportBrokenSite();
    rbs.isProperlyReset();
    if (url !== undefined) {
      rbs.setURL(url);
    }
    await rbs.clickReason(reason);
    if (description !== undefined) {
      rbs.setDescription(description);
    }
    return rbs;
  }
}

class AppMenuHelper extends MenuHelper {
  menuDescription = "AppMenu";

  get reportBrokenSite() {
    return this.getViewNode("appMenu_help_reportBrokenSite");
  }

  get popup() {
    return this.win.document.getElementById("appMenu-popup");
  }

  async open() {
    await new CustomizableUITestUtils(this.win).openMainMenu();
    // Report Broken Site lives in the Help and Report subview.
    const helpView = this.getViewNode("PanelUI-helpView");
    const shownPromise = BrowserTestUtils.waitForEvent(helpView, "ViewShown");
    this.getViewNode("appMenu-help-button2").click();
    await shownPromise;
  }

  async close() {
    if (this.opened) {
      await new CustomizableUITestUtils(this.win).hideMainMenu();
    }
  }
}

class HelpMenuHelper extends MenuHelper {
  menuDescription = "Help Menu";

  get showsBackButton() {
    return false;
  }

  get reportBrokenSite() {
    return this.win.document.getElementById("help_reportBrokenSite");
  }

  get popup() {
    return this.getViewNode("PanelUI-helpView");
  }

  get helpMenu() {
    return this.win.document.getElementById("menu_HelpPopup");
  }

  async openReportBrokenSite() {
    // We can't actually open the Help menu properly in testing, so the best
    // we can do to open its Report Broken Site panel is to force its DOM to be
    // prepared, and then soft-click the Report Broken Site menuitem to open it.
    await this.open();
    const shownPromise = waitForEvent(
      this.win,
      "ViewShown",
      true,
      e => e.target.classList.contains("report-broken-site-view"),
      `clicking Report Broken Site on ${this.menuDescription} and waiting for it to show`
    );
    await this.reportBrokenSite.click();
    await shownPromise;
    return new ReportBrokenSiteHelper(this);
  }

  async clickReportBrokenSite() {
    await this.open();
    this.reportBrokenSite.click();
    return new ReportBrokenSiteHelper(this);
  }

  async open() {
    const { helpMenu } = this;
    const promise = waitForEvent(
      helpMenu,
      "popupshown",
      undefined,
      undefined,
      `opening Report Broken Site on ${this.menuDescription} and waiting for it to show`
    );

    // This event-faking method was copied from browser_title_case_menus.js.
    // We can't actually open the Help menu in testing, but this lets us
    // force its DOM to be properly built.
    helpMenu.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    helpMenu.dispatchEvent(new MouseEvent("popupshown", { bubbles: true }));

    await promise;
  }

  async close() {
    const { helpMenu } = this;
    const promise = BrowserTestUtils.waitForPopupEvent(helpMenu, "hidden");

    // (Also copied from browser_title_case_menus.js)
    // Just for good measure, we'll fire the popuphiding/popuphidden events
    // after we close the menupopups.
    helpMenu.dispatchEvent(new MouseEvent("popuphiding", { bubbles: true }));
    helpMenu.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));

    await promise;
  }
}

class ProtectionsPanelHelper extends MenuHelper {
  menuDescription = "Protections Panel";

  get reportBrokenSite() {
    this.win.gProtectionsHandler._initializePopup();
    return this.getViewNode("protections-popup-report-broken-site-button");
  }

  get popup() {
    this.win.gProtectionsHandler._initializePopup();
    return this.win.document.getElementById("protections-popup");
  }

  async open() {
    const promise = waitForEvent(
      this.win,
      "popupshown",
      true,
      e => e.target.id == "protections-popup",
      `opening Report Broken Site on ${this.menuDescription} and waiting for it to show`
    );
    this.win.gProtectionsHandler.showProtectionsPopup();
    await promise;
  }

  async close() {
    if (this.opened) {
      const popup = this.popup;
      const promise = BrowserTestUtils.waitForPopupEvent(popup, "hidden");
      PanelMultiView.hidePopup(popup, false);
      await promise;
    }
  }
}

function AppMenu(win = window) {
  return new AppMenuHelper(win);
}

function HelpMenu(win = window) {
  return new HelpMenuHelper(win);
}

function ProtectionsPanel(win = window) {
  return new ProtectionsPanelHelper(win);
}

function filterFrameworkDetectorFails(ping, expected) {
  // the framework detector's frame-script may fail to run in low memory or other
  // weird corner-cases, so we ignore the results in that case if they don't match.
  if (!areObjectsEqual(ping.frameworks, expected.frameworks)) {
    const { fastclick, mobify, marfeel } = ping.frameworks;
    if (!fastclick && !mobify && !marfeel) {
      console.info("Ignoring failure to get framework data");
      expected.frameworks = ping.frameworks;
    }
  }
}

async function setupBuiltInTestTrackers() {
  await UrlClassifierTestUtils.addBuiltInTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
}

async function setupStrictETP() {
  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["security.mixed_content.block_active_content", true],
      ["security.mixed_content.block_display_content", true],
      ["security.mixed_content.upgrade_display_content", false],
      [
        "urlclassifier.trackingTable",
        "content-track-digest256,mochitest2-track-simple",
      ],
      ["browser.contentblocking.category", "strict"],
    ],
  });
}

// copied from browser/base/content/test/browser-protectionsUI/head.js
function waitForContentBlockingEvent(numChanges, win = null) {
  if (!numChanges) {
    return Promise.resolve();
  }
  if (!win) {
    win = window;
  }
  info(`Waiting for ${numChanges} content-blocking events`);
  return new Promise(resolve => {
    let n = 0;
    let listener = {
      onContentBlockingEvent(webProgress, request, event) {
        n = n + 1;
        info(
          `Received onContentBlockingEvent event: ${event} (${n} of ${numChanges})`
        );
        if (n >= numChanges) {
          win.gBrowser.removeProgressListener(listener);
          resolve(n);
        }
      },
    };
    win.gBrowser.addProgressListener(listener);
  });
}
