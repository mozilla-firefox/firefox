/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { ASRouterTriggerListeners } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTriggerListeners.sys.mjs"
);
const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);
const { DefaultBrowserCheck } = ChromeUtils.importESModule(
  "moz-src:///browser/components/DefaultBrowserCheck.sys.mjs"
);

const PDF_TEST_URL =
  "https://example.com/browser/browser/extensions/newtab/test/browser/file_pdf.PDF";

async function openURLInWindow(window, url) {
  const { selectedBrowser } = window.gBrowser;
  BrowserTestUtils.startLoadingURIString(selectedBrowser, url);
  await BrowserTestUtils.browserLoaded(selectedBrowser, false, url);
  return selectedBrowser;
}

async function openURLInNewTab(window, ...args) {
  return BrowserTestUtils.openNewForegroundTab(window.gBrowser, ...args);
}

const pdfMatch = sinon.match(val => {
  return (
    val?.id === "pdfJsFeatureCalloutCheck" && val?.context?.source === "open"
  );
});

const validateCalloutCustomPosition = (element, absolutePosition, doc) => {
  const browserBox = doc.querySelector("hbox#browser");
  for (let position in absolutePosition) {
    if (Object.prototype.hasOwnProperty.call(absolutePosition, position)) {
      // remove the `px` at the end of our absolute position strings
      const relativePos = parseFloat(absolutePosition[position]);
      const elPos = element.getBoundingClientRect()[position];
      const browserPos = browserBox.getBoundingClientRect()[position];

      if (position in ["top", "left"]) {
        if (elPos !== browserPos + relativePos) {
          return false;
        }
      } else if (position in ["right", "bottom"]) {
        if (elPos !== browserPos - relativePos) {
          return false;
        }
      }
    }
  }
  return true;
};

const validateCalloutRTLPosition = (element, absolutePosition) => {
  for (let position in absolutePosition) {
    if (Object.prototype.hasOwnProperty.call(absolutePosition, position)) {
      const pixels = parseFloat(absolutePosition[position]);
      if (position === "left") {
        if (element.getBoundingClientRect().right !== pixels) {
          return false;
        }
      } else if (position === "right") {
        if (element.getBoundingClientRect().left !== pixels) {
          return false;
        }
      }
    }
  }
  return true;
};

const testMessage = {
  message: {
    id: "TEST_MESSAGE",
    template: "feature_callout",
    content: {
      id: "TEST_MESSAGE",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      screens: [
        {
          id: "TEST_MESSAGE_1",
          anchors: [
            { selector: "#PanelUI-menu-button", arrow_position: "top-end" },
          ],
          content: {
            position: "callout",
            title: {
              raw: "Test title",
            },
            subtitle: {
              raw: "Test subtitle",
            },
            primary_button: {
              label: {
                raw: "Done",
              },
              action: {
                navigate: true,
              },
            },
          },
        },
      ],
    },
    priority: 1,
    targeting: "true",
    trigger: { id: "pdfJsFeatureCalloutCheck" },
  },
};

const newtabTestMessage = {
  id: "TEST_MESSAGE",
  template: "feature_callout",
  content: {
    id: "TEST_MESSAGE",
    template: "multistage",
    backdrop: "transparent",
    transitions: false,
    disableHistoryUpdates: true,
    tour_pref_name: "browser.newtab.feature-tour",
    tour_pref_default_value: JSON.stringify({
      screen: "TEST_MESSAGE_1",
      complete: false,
    }),
    screens: [
      {
        id: "TEST_MESSAGE_1",
        anchors: [
          {
            selector: "hbox#browser",
            arrow_position: "top-end",
            absolute_position: { top: "45px", right: "55px" },
          },
        ],
        content: {
          position: "callout",
          title: "Test callout title",
          subtitle: "Test callout subtitle",
          primary_button: {
            label: "Test callout button",
          },
        },
      },
    ],
  },
  priority: 1,
  targeting: "true",
  trigger: { id: "newtabFeatureCalloutCheck" },
};

const testMessageScreenId = testMessage.message.content.screens[0].id;
const newtabTestMessageScreenId = newtabTestMessage.content.screens[0].id;

const inChaosMode = !!parseInt(Services.env.get("MOZ_CHAOSMODE"), 16);

add_setup(async function () {
  let timeoutFactor = 3;
  // Runtime increases in chaos mode on Mac.
  if (inChaosMode && AppConstants.platform === "macosx") {
    timeoutFactor = 5;
  }
  requestLongerTimeout(timeoutFactor);
});

// Test that a feature callout message can be loaded into ASRouter and displayed
// via a standard trigger. Also test that the callout can be a feature tour,
// even if its tour pref doesn't exist in Firefox. The tour pref will be created
// and cleaned up automatically. This allows a feature callout to be implemented
// entirely off-train in an experiment, without landing anything in tree.
add_task(async function triggered_feature_tour_with_custom_pref() {
  let sandbox = sinon.createSandbox();
  const TEST_MESSAGES = [
    {
      id: "TEST_FEATURE_TOUR",
      template: "feature_callout",
      content: {
        id: "TEST_FEATURE_TOUR",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        tour_pref_name: "messaging-system-action.browser.test.feature-tour",
        tour_pref_default_value: JSON.stringify({
          screen: "FEATURE_CALLOUT_1",
          complete: false,
        }),
        screens: [
          {
            id: "FEATURE_CALLOUT_1",
            anchors: [
              {
                selector: "#PanelUI-menu-button",
                arrow_position: "top-center-arrow-end",
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "callout-pdfjs-edit-title" },
              subtitle: { string_id: "callout-pdfjs-edit-body-b" },
              primary_button: {
                label: { string_id: "callout-pdfjs-edit-button" },
                action: {
                  type: "SET_PREF",
                  data: {
                    pref: {
                      name: "messaging-system-action.browser.test.feature-tour",
                      value: JSON.stringify({
                        screen: "FEATURE_CALLOUT_2",
                        complete: false,
                      }),
                    },
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "MULTI_ACTION",
                  data: {
                    actions: [
                      {
                        type: "BLOCK_MESSAGE",
                        data: { id: "TEST_FEATURE_TOUR" },
                      },
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "messaging-system-action.browser.test.feature-tour",
                          },
                        },
                      },
                    ],
                  },
                },
              },
            },
          },
          {
            id: "FEATURE_CALLOUT_2",
            anchors: [
              {
                selector: "#back-button",
                arrow_position: "top-center-arrow-start",
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "callout-pdfjs-draw-title" },
              subtitle: { string_id: "callout-pdfjs-draw-body-b" },
              primary_button: {
                label: { string_id: "callout-pdfjs-draw-button" },
                action: {
                  type: "MULTI_ACTION",
                  data: {
                    actions: [
                      {
                        type: "BLOCK_MESSAGE",
                        data: { id: "TEST_FEATURE_TOUR" },
                      },
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "messaging-system-action.browser.test.feature-tour",
                          },
                        },
                      },
                    ],
                  },
                },
              },
              dismiss_button: {
                action: {
                  type: "MULTI_ACTION",
                  data: {
                    actions: [
                      {
                        type: "BLOCK_MESSAGE",
                        data: { id: "TEST_FEATURE_TOUR" },
                      },
                      {
                        type: "SET_PREF",
                        data: {
                          pref: {
                            name: "messaging-system-action.browser.test.feature-tour",
                          },
                        },
                      },
                    ],
                  },
                },
              },
            },
          },
        ],
      },
      priority: 2,
      targeting: `(('messaging-system-action.browser.test.feature-tour' | preferenceValue) ? (('messaging-system-action.browser.test.feature-tour' | preferenceValue | regExpMatch('(?<=complete":)(.*)(?=})')) ? ('messaging-system-action.browser.test.feature-tour' | preferenceValue | regExpMatch('(?<=complete":)(.*)(?=})')[1] != "true") : true) : true)`,
      trigger: { id: "nthTabClosed" },
    },
    {
      id: "TEST_FEATURE_TOUR_2",
      template: "feature_callout",
      content: {
        id: "TEST_FEATURE_TOUR_2",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FEATURE_CALLOUT_TEST",
            anchors: [
              {
                selector: "#PanelUI-menu-button",
                arrow_position: "top-center-arrow-end",
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "callout-pdfjs-edit-title" },
              subtitle: { string_id: "callout-pdfjs-edit-body-b" },
              primary_button: {
                label: { string_id: "callout-pdfjs-edit-button" },
                action: { dismiss: true },
              },
            },
          },
        ],
      },
      priority: 1,
      targeting: "true",
      trigger: { id: "nthTabClosed" },
    },
  ];
  const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
  getMessagesStub.returns(TEST_MESSAGES);
  await ASRouter._updateMessageProviders();
  await ASRouter.loadMessagesFromAllProviders(
    ASRouter.state.providers.filter(p => p.id === "onboarding")
  );

  // Test that callout is triggered and shown in browser chrome
  const win1 = await BrowserTestUtils.openNewBrowserWindow();
  win1.focus();
  const tab1 = await BrowserTestUtils.openNewForegroundTab(win1.gBrowser);
  await TestUtils.waitForTick();
  win1.gBrowser.removeTab(tab1);
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[0].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout is rendered in the browser chrome when a message is available"
  );

  // Test that a callout does NOT appear if another is already shown in any window.
  const showFeatureCalloutSpy = sandbox.spy(
    FeatureCalloutBroker,
    "showFeatureCallout"
  );
  const win2 = await BrowserTestUtils.openNewBrowserWindow();
  win2.focus();
  const tab2 = await BrowserTestUtils.openNewForegroundTab(win2.gBrowser);
  await TestUtils.waitForTick();
  win2.gBrowser.removeTab(tab2);
  await BrowserTestUtils.waitForCondition(async () => {
    const rvs = await Promise.all(showFeatureCalloutSpy.returnValues);
    return (
      showFeatureCalloutSpy.calledWith(
        win2.gBrowser.selectedBrowser,
        sinon.match(TEST_MESSAGES[0])
      ) && rvs.every(rv => !rv)
    );
  }, "Waiting for showFeatureCallout to be called");
  ok(
    !win2.document.querySelector(calloutSelector),
    "Feature Callout is not rendered when a callout is already shown in any window"
  );
  await BrowserTestUtils.closeWindow(win2);
  win1.focus();
  await BrowserTestUtils.waitForCondition(
    async () => Services.focus.activeWindow === win1,
    "Waiting for window 1 to be active"
  );

  // Test that the tour pref doesn't exist yet
  ok(
    !Services.prefs.prefHasUserValue(TEST_MESSAGES[0].content.tour_pref_name),
    "Tour pref does not exist yet"
  );

  // Test that the callout advances screen and sets the tour pref
  win1.document.querySelector(calloutCTASelector).click();
  await BrowserTestUtils.waitForCondition(
    () =>
      Services.prefs.prefHasUserValue(TEST_MESSAGES[0].content.tour_pref_name),
    "Waiting for tour pref to be set"
  );
  SimpleTest.isDeeply(
    JSON.parse(
      Services.prefs.getStringPref(
        TEST_MESSAGES[0].content.tour_pref_name,
        "{}"
      )
    ),
    { screen: "FEATURE_CALLOUT_2", complete: false },
    "Tour pref is set correctly"
  );
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[1].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout screen 2 is rendered"
  );

  // Test that the callout is dismissed and cleans up the tour pref
  win1.document.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(win1.document);
  ok(
    !win1.document.querySelector(calloutSelector),
    "Feature Callout is not rendered after being dismissed"
  );
  ok(
    !Services.prefs.prefHasUserValue(TEST_MESSAGES[0].content.tour_pref_name),
    "Tour pref is cleaned up correctly"
  );
  await BrowserTestUtils.waitForCondition(
    () => !FeatureCalloutBroker.isCalloutShowing,
    "Waiting for all callouts to empty from the callout broker"
  );

  // Test that the message was blocked so a different callout is shown
  const tab3 = await BrowserTestUtils.openNewForegroundTab(win1.gBrowser);
  await TestUtils.waitForTick();
  win1.gBrowser.removeTab(tab3);
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[1].content.screens[0].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "A different Feature Callout is rendered"
  );
  win1.document.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(win1.document);
  ok(!FeatureCalloutBroker.isCalloutShowing, "No Feature Callout is shown");

  BrowserTestUtils.closeWindow(win1);

  sandbox.restore();
  await ASRouter.unblockMessageById(TEST_MESSAGES[0].id);
  await ASRouter.resetMessageState();
  await ASRouter._updateMessageProviders();
  await ASRouter.loadMessagesFromAllProviders(
    ASRouter.state.providers.filter(p => p.id === "onboarding")
  );
});

// Test that a feature callout message can be loaded into ASRouter and displayed
// via a standard trigger. Also test that the callout can be a feature tour
// without requiring a tour pref to be used.
add_task(async function triggered_feature_tour_with_advance_screens() {
  let sandbox = sinon.createSandbox();
  const TEST_MESSAGES = [
    {
      id: "TEST_FEATURE_TOUR",
      template: "feature_callout",
      content: {
        id: "TEST_FEATURE_TOUR",
        template: "multistage",
        backdrop: "transparent",
        transitions: false,
        disableHistoryUpdates: true,
        screens: [
          {
            id: "FEATURE_CALLOUT_1",
            anchors: [
              {
                selector: "#PanelUI-menu-button",
                arrow_position: "top-center-arrow-end",
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "callout-pdfjs-edit-title" },
              subtitle: { string_id: "callout-pdfjs-edit-body-b" },
              primary_button: {
                label: { string_id: "callout-pdfjs-edit-button" },
                action: { advance_screens: { direction: 1 } },
              },
            },
          },
          {
            id: "FEATURE_CALLOUT_2",
            anchors: [
              {
                selector: "#back-button",
                arrow_position: "top-center-arrow-start",
              },
            ],
            content: {
              position: "callout",
              title: { string_id: "callout-pdfjs-draw-title" },
              subtitle: { string_id: "callout-pdfjs-draw-body-b" },
              primary_button: {
                label: { raw: "Go forward" },
                action: {
                  advance_screens: { direction: 1 },
                },
              },
              secondary_button: {
                label: { raw: "Go back" },
                action: {
                  advance_screens: { direction: -1 },
                },
              },
            },
          },
        ],
      },
      priority: 2,
      targeting: "true",
      trigger: { id: "nthTabClosed" },
    },
  ];
  const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
  getMessagesStub.returns(TEST_MESSAGES);
  await ASRouter._updateMessageProviders();
  await ASRouter.loadMessagesFromAllProviders(
    ASRouter.state.providers.filter(p => p.id === "onboarding")
  );

  // Test that callout is triggered and shown in browser chrome
  const win1 = await BrowserTestUtils.openNewBrowserWindow();
  win1.focus();
  const tab1 = await BrowserTestUtils.openNewForegroundTab(win1.gBrowser);
  await TestUtils.waitForTick();
  win1.gBrowser.removeTab(tab1);
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[0].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout is rendered in the browser chrome when a message is available"
  );

  // Test that the callout advances screen
  win1.document.querySelector(`#${calloutId} .primary`).click();
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[1].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout screen 2 is rendered"
  );

  // Test that the callout goes backward
  win1.document.querySelector(`#${calloutId} .secondary`).click();
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[0].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout screen 1 is rendered again"
  );

  // Go forward again
  win1.document.querySelector(`#${calloutId} .primary`).click();
  await waitForCalloutScreen(
    win1.document,
    TEST_MESSAGES[0].content.screens[1].id
  );
  ok(
    win1.document.querySelector(calloutSelector),
    "Feature Callout screen 2 is rendered again"
  );

  // Test that the tour ends when advancing past the last screen
  win1.document.querySelector(`#${calloutId} .primary`).click();
  await waitForCalloutRemoved(win1.document);
  ok(
    !win1.document.querySelector(calloutSelector),
    "Feature Callout is not rendered after the tour ends"
  );
  await BrowserTestUtils.waitForCondition(
    () => !FeatureCalloutBroker.isCalloutShowing,
    "Waiting for all callouts to empty from the callout broker"
  );

  BrowserTestUtils.closeWindow(win1);

  sandbox.restore();
  await ASRouter.resetMessageState();
  await ASRouter._updateMessageProviders();
  await ASRouter.loadMessagesFromAllProviders(
    ASRouter.state.providers.filter(p => p.id === "onboarding")
  );
});

add_task(async function callout_not_shown_if_dialog_open() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  let dialogPromise = BrowserTestUtils.promiseAlertDialog(null, undefined, {
    callback: async dialogWin => {
      let rv = await FeatureCalloutBroker.showFeatureCallout(
        win.gBrowser.selectedBrowser,
        testMessage.message
      );
      ok(
        !rv,
        "Feature callout not shown when a dialog is open in the same window"
      );
      dialogWin.document.querySelector("dialog").getButton("cancel").click();
    },
    isSubDialog: true,
  });
  await DefaultBrowserCheck.prompt(win);
  await dialogPromise;

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function callout_not_shown_if_panel_open() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const gCUITestUtils = new CustomizableUITestUtils(win);
  await gCUITestUtils.openMainMenu();

  let rv = await FeatureCalloutBroker.showFeatureCallout(
    win.gBrowser.selectedBrowser,
    testMessage.message
  );
  ok(!rv, "Feature callout not shown when a panel is open in the same window");

  await gCUITestUtils.hideMainMenu();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_renders_in_browser_chrome_for_pdf() {
  const sandbox = sinon.createSandbox();
  const sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
  sendTriggerStub.withArgs(pdfMatch).resolves(testMessage);
  sendTriggerStub.callThrough();

  const win = await BrowserTestUtils.openNewBrowserWindow();
  await openURLInWindow(win, PDF_TEST_URL);
  const doc = win.document;
  await waitForCalloutScreen(doc, testMessageScreenId);
  const container = doc.querySelector(calloutSelector);
  ok(
    container,
    "Feature Callout is rendered in the browser chrome with a new window when a message is available"
  );

  // click CTA to close
  doc.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(doc);
  ok(
    true,
    "Feature callout removed from browser chrome after clicking button configured to navigate"
  );

  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});

add_task(
  async function feature_callout_renders_and_hides_in_chrome_when_switching_tabs() {
    const sandbox = sinon.createSandbox();
    const sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
    sendTriggerStub.withArgs(pdfMatch).resolves(testMessage);
    sendTriggerStub.callThrough();

    const win = await BrowserTestUtils.openNewBrowserWindow();

    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      PDF_TEST_URL
    );
    tab1.focus();
    await waitForCalloutScreen(doc, testMessageScreenId);
    ok(
      doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout rendered when opening a new tab with PDF url"
    );

    const tab2 = await openURLInNewTab(win, "about:preferences");
    tab2.focus();
    await BrowserTestUtils.waitForCondition(() => {
      return !doc.body.querySelector(
        "#multi-stage-message-root.featureCallout"
      );
    });

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout removed when tab without PDF URL is navigated to"
    );

    const tab3 = await openURLInNewTab(win, PDF_TEST_URL);
    tab3.focus();
    await waitForCalloutScreen(doc, testMessageScreenId);
    ok(
      doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout still renders when opening a new tab with PDF url after being initially rendered on another tab"
    );

    tab1.focus();
    await waitForCalloutScreen(doc, testMessageScreenId);
    ok(
      doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout rendered on original tab after switching tabs multiple times"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
  }
);

add_task(
  async function feature_callout_disappears_when_navigating_to_non_pdf_url_in_same_tab() {
    const sandbox = sinon.createSandbox();
    const sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
    sendTriggerStub.withArgs(pdfMatch).resolves(testMessage);
    sendTriggerStub.callThrough();

    const win = await BrowserTestUtils.openNewBrowserWindow();

    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      PDF_TEST_URL
    );
    tab1.focus();
    await waitForCalloutScreen(doc, testMessageScreenId);
    ok(
      doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout rendered when opening a new tab with PDF url"
    );

    BrowserTestUtils.startLoadingURIString(win.gBrowser, "about:preferences");
    await BrowserTestUtils.waitForLocationChange(
      win.gBrowser,
      "about:preferences"
    );
    await waitForCalloutRemoved(doc);

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout not rendered on original tab after navigating to non pdf URL"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
  }
);

add_task(
  async function feature_callout_disappears_when_closing_foreground_pdf_tab() {
    const sandbox = sinon.createSandbox();
    const sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
    sendTriggerStub.withArgs(pdfMatch).resolves(testMessage);
    sendTriggerStub.callThrough();

    const win = await BrowserTestUtils.openNewBrowserWindow();

    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      PDF_TEST_URL
    );
    tab1.focus();
    await waitForCalloutScreen(doc, testMessageScreenId);
    ok(
      doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout rendered when opening a new tab with PDF url"
    );

    BrowserTestUtils.removeTab(tab1);
    await waitForCalloutRemoved(doc);

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout disappears after closing foreground tab"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
  }
);

add_task(
  async function feature_callout_does_not_appear_when_opening_background_pdf_tab() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders(
      ASRouter.state.providers.filter(p => p.id === "onboarding")
    );

    const win = await BrowserTestUtils.openNewBrowserWindow();
    const doc = win.document;

    const tab1 = await BrowserTestUtils.addTab(win.gBrowser, PDF_TEST_URL);
    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout not rendered when opening a background tab with PDF url"
    );

    BrowserTestUtils.removeTab(tab1);

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout still not rendered after closing background tab with PDF url"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function newtab_feature_callout_appears_in_browser_chrome_on_newtab() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:newtab"
    );
    tab1.focus();
    await waitForCalloutScreen(doc, newtabTestMessageScreenId);
    ok(
      doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Newtab feature callout rendered when opening a focused newtab"
    );

    BrowserTestUtils.removeTab(tab1);
    await waitForCalloutRemoved(doc);
    ok(
      !doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Feature callout disappears after closing new tab"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function newtab_feature_callout_does_not_appear_when_opening_background_newtab_tab() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    const doc = win.document;

    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:preferences"
    );
    const tab2 = await BrowserTestUtils.addTab(win.gBrowser, "about:newtab");
    ok(
      !doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Newtab feature callout not rendered when opening a background newtab"
    );

    BrowserTestUtils.removeTab(tab2);
    await waitForCalloutRemoved(doc);
    ok(
      !doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Feature callout still not rendered after closing background tab"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function newtab_feature_callout_does_not_appear_if_newtab_disabled() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.newtabpage.enabled", false]],
    });
    const sandbox = sinon.createSandbox();
    const trigger = ASRouterTriggerListeners.get("newtabFeatureCalloutCheck");
    const showFeatureCalloutTourSpy = sandbox.spy(
      trigger,
      "showFeatureCalloutTour"
    );
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:newtab"
    );
    tab1.focus();
    ok(
      showFeatureCalloutTourSpy.notCalled,
      "Newtab feature callout not shown when newtab pref is disabled"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function newtab_feature_callout_does_not_appear_in_browser_chrome_on_new_window() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    await openURLInWindow(win, "about:newtab");
    const doc = win.document;

    await waitForCalloutScreen(doc, newtabTestMessageScreenId);
    ok(
      doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Newtab Feature Callout is in the browser chrome of first window when a message is available"
    );

    const win2 = await BrowserTestUtils.openNewBrowserWindow();
    await openURLInWindow(win2, "about:newtab");
    const doc2 = win2.document;
    ok(
      !doc2.querySelector(`.${newtabTestMessageScreenId}`),
      "Newtab Feature Callout is not in the browser chrome new window when a message is available"
    );

    await BrowserTestUtils.closeWindow(win);
    await BrowserTestUtils.closeWindow(win2);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function feature_callout_disappears_when_navigating_from_newtab_to_pdf_url_in_same_tab() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();

    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:newtab"
    );
    tab1.focus();
    await waitForCalloutScreen(doc, newtabTestMessageScreenId);
    ok(
      doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Feature callout rendered when opening a newtab"
    );

    BrowserTestUtils.startLoadingURIString(win.gBrowser, PDF_TEST_URL);
    await BrowserTestUtils.waitForLocationChange(win.gBrowser, PDF_TEST_URL);
    await waitForCalloutRemoved(doc);

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Feature callout not rendered on original tab after navigating to PDF"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function feature_callout_disappears_when_navigating_from_newtab_to_pdf_url_in_different_tab() {
    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [newtabTestMessage];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();

    const doc = win.document;
    const tab1 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "about:newtab"
    );
    tab1.focus();
    await waitForCalloutScreen(doc, newtabTestMessageScreenId);
    ok(
      doc.querySelector(`.${newtabTestMessageScreenId}`),
      "Feature callout rendered when opening a newtab"
    );

    const tab2 = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      PDF_TEST_URL
    );
    tab2.focus();
    await waitForCalloutRemoved(doc);

    ok(
      !doc.querySelector(`.${testMessageScreenId}`),
      "Newtab feature callout not rendered after navigating to PDF"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function feature_callout_is_positioned_relative_to_browser_window() {
    // Deep copying our test message so we can alter it without disrupting future tests
    const pdfTestMessage = JSON.parse(JSON.stringify(testMessage));
    const pdfTestMessageCalloutSelector =
      pdfTestMessage.message.content.screens[0].id;

    pdfTestMessage.message.content.screens[0].anchors[0] = {
      selector: "hbox#browser",
      absolute_position: { top: "45px", right: "25px" },
    };

    const sandbox = sinon.createSandbox();
    const getMessagesStub = sandbox.stub(FeatureCalloutMessages, "getMessages");
    const TEST_MESSAGES = [pdfTestMessage.message];
    getMessagesStub.returns(TEST_MESSAGES);
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    await openURLInWindow(win, PDF_TEST_URL);
    const doc = win.document;
    await waitForCalloutScreen(doc, pdfTestMessageCalloutSelector);

    // Verify that callout renders in appropriate position (without infobar element)
    const callout = doc.querySelector(`.${pdfTestMessageCalloutSelector}`);
    ok(callout, "Callout is rendered when navigating to PDF file");
    ok(
      validateCalloutCustomPosition(
        callout,
        pdfTestMessage.message.content.screens[0].anchors[0].absolute_position,
        doc
      ),
      "Callout custom position is as expected"
    );

    // Add height to the top of the browser to simulate an infobar or other element
    const navigatorToolBox = doc.querySelector("#navigator-toolbox");
    navigatorToolBox.style.height = "150px";
    // We test in a new tab because the callout does not adjust itself
    // when size of the navigator-toolbox-background box changes.
    const tab = await openURLInNewTab(win, "https://example.com/some2.pdf");
    // Verify that callout renders in appropriate position (with infobar element displayed)
    ok(
      validateCalloutCustomPosition(
        callout,
        pdfTestMessage.message.content.screens[0].anchors[0].absolute_position,
        doc
      ),
      "Callout custom position is as expected while navigator toolbox height is extended"
    );
    BrowserTestUtils.removeTab(tab);
    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
    await ASRouter._updateMessageProviders();
    await ASRouter.loadMessagesFromAllProviders();
  }
);

add_task(
  async function custom_position_callout_is_horizontally_reversed_in_rtl_layouts() {
    // Deep copying our test message so we can alter it without disrupting future tests
    const pdfTestMessage = JSON.parse(JSON.stringify(testMessage));
    const pdfTestMessageCalloutSelector =
      pdfTestMessage.message.content.screens[0].id;

    pdfTestMessage.message.content.screens[0].anchors[0] = {
      selector: "hbox#browser",
      absolute_position: { top: "45px", right: "25px" },
    };

    const sandbox = sinon.createSandbox();
    const sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
    sendTriggerStub.withArgs(pdfMatch).resolves(pdfTestMessage);
    sendTriggerStub.callThrough();

    const win = await BrowserTestUtils.openNewBrowserWindow();
    win.document.dir = "rtl";
    Assert.strictEqual(
      win.document.documentElement.getAttribute("dir"),
      "rtl",
      "browser window is in RTL"
    );

    await openURLInWindow(win, PDF_TEST_URL);
    const doc = win.document;
    await waitForCalloutScreen(doc, pdfTestMessageCalloutSelector);

    const callout = doc.querySelector(`.${pdfTestMessageCalloutSelector}`);
    ok(callout, "Callout is rendered when navigating to PDF file");
    ok(
      validateCalloutRTLPosition(
        callout,
        pdfTestMessage.message.content.screens[0].anchors[0].absolute_position
      ),
      "Callout custom position is rendered appropriately in RTL mode"
    );

    await BrowserTestUtils.closeWindow(win);
    sandbox.restore();
  }
);

add_task(async function first_anchor_selected_is_valid() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const config = {
    win,
    location: "chrome",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "chrome" },
  };

  const message = JSON.parse(JSON.stringify(testMessage.message));
  const sandbox = sinon.createSandbox();

  const doc = win.document;
  const featureCallout = new FeatureCallout(config);
  const getAnchorSpy = sandbox.spy(featureCallout, "_getAnchor");
  featureCallout.showFeatureCallout(message);
  await waitForCalloutScreen(doc, message.content.screens[0].id);
  ok(
    getAnchorSpy.alwaysReturned(
      sandbox.match(message.content.screens[0].anchors[0])
    ),
    "The first anchor is selected"
  );

  win.document.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(win.document);
  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});

add_task(async function first_anchor_selected_is_invalid() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const config = {
    win,
    location: "chrome",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "chrome" },
  };

  let stopReloadButton = doc.getElementById("stop-reload-button");

  await gCustomizeMode.addToPanel(stopReloadButton);

  const message = JSON.parse(JSON.stringify(testMessage.message));
  message.content.screens[0].anchors = [
    // element that does not exist
    { selector: "#some-fake-id.some-fake-class", arrow_position: "top" },
    // element that exists but has no height/width
    { selector: "#a11y-announcement", arrow_position: "top" },
    // element that exists but is hidden by CSS
    { selector: "#window-modal-dialog", arrow_position: "top" },
    // customizable widget that's in the overflow panel
    { selector: "#stop-reload-button", arrow_position: "top" },
    // element that is fully visible
    { selector: "#PanelUI-menu-button", arrow_position: "top" },
  ];
  const sandbox = sinon.createSandbox();

  const featureCallout = new FeatureCallout(config);
  const getAnchorSpy = sandbox.spy(featureCallout, "_getAnchor");
  featureCallout.showFeatureCallout(message);
  await waitForCalloutScreen(doc, message.content.screens[0].id);
  is(
    getAnchorSpy.lastCall.returnValue.selector,
    message.content.screens[0].anchors[4].selector,
    "The first valid anchor (anchor 5) is selected"
  );

  win.document.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(win.document);
  CustomizableUI.reset();
  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});

add_task(async function test_triggerTab_selector() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const config = {
    win,
    location: "chrome",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "chrome" },
  };

  const message = JSON.parse(JSON.stringify(testMessage.message));
  message.content.screens[0].anchors[0].selector =
    "#tabbrowser-tabs %triggerTab%[visuallyselected]";
  const sandbox = sinon.createSandbox();

  const doc = win.document;
  const featureCallout = new FeatureCallout(config);
  const getAnchorSpy = sandbox.spy(featureCallout, "_getAnchor");
  featureCallout.showFeatureCallout(message);
  await waitForCalloutScreen(doc, message.content.screens[0].id);
  ok(
    getAnchorSpy.alwaysReturned(
      sandbox.match(message.content.screens[0].anchors[0])
    ),
    "The first anchor is selected"
  );

  win.document.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(win.document);
  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});

add_task(async function test_triggeredTabBookmark_selector() {
  // Currently not supported on Linux, see Bug 1927472
  if (AppConstants.platform === "linux") {
    return;
  }

  await SpecialPowers.pushPrefEnv({
    set: [["browser.toolbars.bookmarks.visibility", "always"]],
  });
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    await SpecialPowers.popPrefEnv();
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const browser = win.gBrowser.selectedBrowser;
  const testURL = "https://example.com/";
  BrowserTestUtils.startLoadingURIString(browser, testURL);
  await BrowserTestUtils.browserLoaded(browser);

  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Test",
    url: testURL,
  });

  // Wait for the toolbar to rebuild and its visibility update to settle, then the item.
  await TestUtils.waitForCondition(
    async () => (await win.PlacesToolbarHelper.getIsEmpty()) === false,
    "Waiting for the Bookmarks toolbar to be rebuilt and not empty"
  );
  if (
    win.PlacesToolbarHelper._viewElt._placesView._updateNodesVisibilityTimer
  ) {
    await BrowserTestUtils.waitForEvent(
      win,
      "BookmarksToolbarVisibilityUpdated"
    );
  }
  await TestUtils.waitForCondition(() => {
    const item = [
      ...win.document.querySelectorAll("#PlacesToolbarItems .bookmark-item"),
    ].find(el => el._placesNode?.uri === testURL);
    if (!item) {
      return false;
    }
    const style = win.getComputedStyle(item);
    const rect = item.getBoundingClientRect();
    return (
      style.visibility === "visible" &&
      style.display !== "none" &&
      rect.width > 0 &&
      rect.height > 0
    );
  }, "Waiting for the triggered-tab bookmark item to render in the toolbar");

  const config = {
    win,
    location: "chrome",
    context: "chrome",
    browser,
    theme: { preset: "chrome" },
  };

  const message = JSON.parse(JSON.stringify(testMessage.message));
  message.content.screens[0].anchors[0].selector = "%triggeredTabBookmark%";

  const sandbox = sinon.createSandbox();
  const doc = win.document;
  const featureCallout = new FeatureCallout(config);
  const getAnchorSpy = sandbox.spy(featureCallout, "_getAnchor");

  await featureCallout.showFeatureCallout(message);
  await waitForCalloutScreen(doc, message.content.screens[0].id);

  const [anchor] = getAnchorSpy.returnValues;

  Assert.strictEqual(
    anchor?.element?.classList.contains("bookmark-item"),
    true,
    "Resolved anchor element is a bookmark item"
  );

  Assert.strictEqual(
    anchor.element?._placesNode?.uri,
    testURL,
    "Resolved bookmark matches triggered tab URI"
  );

  doc.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(doc);

  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_triggeredTabBookmark_selector_fallback_overflow() {
  // Currently not supported on Linux, see Bug 1927472
  if (AppConstants.platform === "linux") {
    return;
  }

  await SpecialPowers.pushPrefEnv({
    set: [["browser.toolbars.bookmarks.visibility", "always"]],
  });
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
    await SpecialPowers.popPrefEnv();
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const browser = win.gBrowser.selectedBrowser;
  const testURL = "https://example.com/";
  BrowserTestUtils.startLoadingURIString(browser, testURL);
  await BrowserTestUtils.browserLoaded(browser);

  // Add filler bookmark first
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Filler Bookmark",
    url: testURL,
  });

  const placesToolbarItems = win.document.getElementById("PlacesToolbarItems");
  const placesChevron = win.document.getElementById("PlacesChevron");

  // Resize the toolbar to force overflow
  placesToolbarItems.style.maxWidth = "10px";
  await TestUtils.waitForCondition(
    () => !placesChevron.collapsed,
    "Waiting for bookmarks toolbar to overflow"
  );

  await TestUtils.waitForCondition(
    async () => (await win.PlacesToolbarHelper.getIsEmpty()) === false,
    "Waiting for the Bookmarks toolbar to have been rebuilt and not be empty"
  );
  if (
    win.PlacesToolbarHelper._viewElt._placesView._updateNodesVisibilityTimer
  ) {
    await BrowserTestUtils.waitForEvent(
      win,
      "BookmarksToolbarVisibilityUpdated"
    );
  }

  // Add the target bookmark so it overflows
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Overflow Bookmark",
    url: testURL,
  });

  await new Promise(resolve => win.requestAnimationFrame(resolve));

  await TestUtils.waitForCondition(() => {
    const match = [
      ...placesToolbarItems.querySelectorAll(".bookmark-item"),
    ].find(el => el._placesNode?.uri === testURL);
    if (!match) {
      return false;
    }
    const style = win.getComputedStyle(match);
    return style.visibility === "hidden" || style.display === "none";
  }, "Waiting for test bookmark to be hidden due to overflow");

  const config = {
    win,
    location: "chrome",
    context: "chrome",
    browser,
    theme: { preset: "chrome" },
  };

  const message = JSON.parse(JSON.stringify(testMessage.message));
  message.content.screens[0].anchors = [
    { selector: "%triggeredTabBookmark%", arrow_position: "top" },
    { selector: "#PlacesToolbarItems", arrow_position: "top" },
  ];

  const sandbox = sinon.createSandbox();
  const doc = win.document;
  const featureCallout = new FeatureCallout(config);
  const getAnchorSpy = sandbox.spy(featureCallout, "_getAnchor");

  await featureCallout.showFeatureCallout(message);
  await waitForCalloutScreen(doc, message.content.screens[0].id);

  const [anchor] = getAnchorSpy.returnValues;

  Assert.strictEqual(
    anchor?.element?.id,
    "PlacesToolbarItems",
    "Anchor resolved to fallback when triggeredTabBookmark was overflowed"
  );

  doc.querySelector(calloutCTASelector).click();
  await waitForCalloutRemoved(doc);
  placesToolbarItems.style.removeProperty("max-width");
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});
