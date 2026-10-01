"use strict";

add_setup(async function () {
  registerCleanupFunction(() => ASRouter.resetMessageState());
});
function getArrowPosition(doc) {
  let callout = doc.querySelector(calloutSelector);
  return callout.getAttribute("arrow-position");
}
const squareWidth = 24;
const arrowWidth = Math.hypot(squareWidth, squareWidth);
const arrowHeight = arrowWidth / 2;
let overlap = 5 - arrowHeight;

function getTestMessage() {
  return {
    id: "TEST_CALLOUT",
    template: "feature_callout",
    groups: [],
    content: {
      id: "TEST_CALLOUT",
      template: "multistage",
      backdrop: "transparent",
      transitions: false,
      disableHistoryUpdates: true,
      screens: [
        {
          id: "TEST_CALLOUT_1",
          anchors: [
            {
              selector: "#PanelUI-menu-button",
              arrow_position: "top",
            },
          ],
          content: {
            position: "callout",
            title: { raw: "Test Callout" },
            primary_button: {
              label: {
                raw: "TEST",
              },
              action: {
                dismiss: true,
              },
            },
            dismiss_button: {
              label: {
                raw: "TEST 2",
              },
              action: {
                dismiss: true,
              },
            },
            page_event_listeners: [
              {
                params: {
                  type: "toggle",
                  selectors: "#PanelUI-menu-button",
                },
                action: {
                  reposition: true,
                },
              },
            ],
          },
        },
      ],
    },
  };
}

async function showFeatureCallout(browser, message) {
  let resolveClosed;
  let closed = new Promise(resolve => {
    resolveClosed = resolve;
  });
  const config = {
    win: browser.documentGlobal,
    location: "chrome",
    context: "chrome",
    browser,
    theme: { preset: "chrome" },
    listener: (_, event) => {
      if (event === "end") {
        resolveClosed();
      }
    },
  };
  const featureCallout = new FeatureCallout(config);
  let showing = await featureCallout.showFeatureCallout(message);
  return { featureCallout, showing, closed };
}

//Functionality tests
add_task(async function feature_callout_closes_on_dismiss() {
  const testMessage = getTestMessage();
  const sandbox = sinon.createSandbox();
  const spy = new TelemetrySpy(sandbox);
  const messageID = getTestMessage().content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  const { closed } = await showFeatureCallout(browser, testMessage);

  await waitForCalloutScreen(doc, messageID);

  doc.querySelector(".dismiss-button").click();
  await closed;
  await waitForCalloutRemoved(doc);

  ok(
    !doc.querySelector(calloutSelector),
    "Callout is removed from screen on dismiss"
  );

  // Test that appropriate telemetry is sent
  spy.assertCalledWith({
    event: "CLICK_BUTTON",
    event_context: {
      source: "dismiss_button",
      page: "chrome",
    },
    message_id: sinon.match(messageID),
  });
  spy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: "dismiss_button",
      page: "chrome",
    },
    message_id: sinon.match(messageID),
  });
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_dismiss_on_timeout() {
  const testMessage = getTestMessage();
  const sandbox = sinon.createSandbox();

  const messageID = getTestMessage().content.screens[0].id;

  // Configure message with a dismiss action on tab container click
  testMessage.content.screens[0].content.page_event_listeners = [
    {
      params: { type: "timeout", options: { once: true, interval: 5000 } },
      action: { dismiss: true, type: "CANCEL" },
    },
  ];

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  const telemetrySpy = new TelemetrySpy(sandbox);

  let onInterval;
  let startedInterval = new Promise(resolve => {
    sandbox.stub(win, "setInterval").callsFake((fn, ms) => {
      Assert.strictEqual(ms, 5000, "setInterval called with 5 second interval");
      onInterval = fn;
      resolve();
      return 1;
    });
  });

  await showFeatureCallout(browser, testMessage);

  info("Waiting for callout to render");

  await startedInterval;
  await waitForCalloutScreen(doc, messageID);

  info("Ending timeout");
  onInterval();
  await waitForCalloutRemoved(document);

  // Test that appropriate telemetry is sent
  telemetrySpy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: "PAGE_EVENT:timeout",
      page: "chrome",
    },
    message_id: "TEST_CALLOUT",
  });
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_page_event_listener() {
  const testMessage = getTestMessage();
  testMessage.content.screens[0].content.page_event_listeners = [
    {
      params: { type: "click", selectors: "#PanelUI-menu-button" },
      action: JSON.parse(
        JSON.stringify(
          testMessage.content.screens[0].content.primary_button.action
        )
      ),
    },
  ];

  const sandbox = sinon.createSandbox();
  const spy = new TelemetrySpy(sandbox);
  const messageID = testMessage.content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  const { closed } = await showFeatureCallout(browser, testMessage);

  info("Waiting for callout to render");
  await waitForCalloutScreen(doc, messageID);

  info("Clicking on button");
  doc.querySelector("#PanelUI-menu-button").click();
  await closed;

  await waitForCalloutRemoved(doc);
  // Test that appropriate telemetry is sent
  spy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: sinon.match("PAGE_EVENT"),
      page: "chrome",
    },
    message_id: "TEST_CALLOUT",
  });
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_dismiss_on_escape() {
  const testMessage = getTestMessage();
  const sandbox = sinon.createSandbox();
  const spy = new TelemetrySpy(sandbox);
  const messageID = getTestMessage().content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;
  // Ensure the browser is focused
  win.gBrowser.selectedBrowser.focus();

  const { closed } = await showFeatureCallout(browser, testMessage);

  info("Waiting for callout to render");
  await waitForCalloutScreen(doc, messageID);

  info("Pressing escape");
  // Press Escape to close
  EventUtils.synthesizeKey("KEY_Escape", {}, win);
  await closed;
  await waitForCalloutRemoved(doc);
  // Test that appropriate telemetry is sent
  spy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: "KEY_Escape",
      page: "chrome",
    },
    message_id: "TEST_CALLOUT",
  });
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_returns_default_focus_to_top() {
  const testMessage = getTestMessage();

  const messageID = getTestMessage().content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;
  // Ensure the browser is focused
  win.gBrowser.selectedBrowser.focus();
  let focusedElement = doc.activeElement;

  const { closed } = await showFeatureCallout(browser, testMessage);

  await waitForCalloutScreen(doc, messageID);

  ok(doc.querySelector(calloutSelector), "Feature Callout element exists");

  doc.querySelector(".dismiss-button").click();
  await closed;
  await waitForCalloutRemoved(document);

  Assert.strictEqual(
    doc.activeElement.localName,
    focusedElement.localName,
    `by default focus returns to the browser after callout closes`
  );
  await BrowserTestUtils.closeWindow(win);
});

// Layout/UI tests
add_task(async function feature_callout_arrow_position_attribute_exists() {
  const testMessage = getTestMessage();
  const sandbox = sinon.createSandbox();
  const messageID = getTestMessage().content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  await showFeatureCallout(browser, testMessage);

  await waitForCalloutScreen(doc, messageID);

  let callout = doc.querySelector(`${calloutSelector}`);
  is(
    callout.getAttribute("arrow-position"),
    testMessage.content.screens[0].anchors[0].arrow_position,
    "Arrow position attribute exists on parent container"
  );
  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_does_not_display_arrow_if_hidden() {
  const testMessage = getTestMessage();
  testMessage.content.screens[0].anchors[0].hide_arrow = true;

  const messageID = getTestMessage().content.screens[0].id;

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  await showFeatureCallout(browser, testMessage);

  await waitForCalloutScreen(doc, messageID);

  is(
    getComputedStyle(
      doc.querySelector(`${calloutSelector} .arrow-box`)
    ).getPropertyValue("display"),
    "none",
    "callout arrow is not visible"
  );
  await BrowserTestUtils.closeWindow(win);
});

//Feature callout Theme tests
function testStyles({ win, theme }) {
  const calloutEl = win.document.querySelector(calloutSelector);
  const calloutStyle = win.getComputedStyle(calloutEl);
  for (const type of ["light", "dark", "hcm"]) {
    const appliedTheme = Object.assign(
      {},
      FeatureCallout.themePresets[theme.preset],
      theme
    );
    const scheme = appliedTheme[type];
    for (const name of FeatureCallout.themePropNames) {
      Assert.equal(
        !!calloutStyle.getPropertyValue(`--fc-${name}-${type}`),
        !!(scheme?.[name] || appliedTheme.all?.[name]),
        `Theme property --fc-${name}-${type} is set for theme ${theme.preset}`
      );
    }
  }
}

async function testCallout(config) {
  const testMessage = getTestMessage();
  const featureCallout = new FeatureCallout(config);
  const [screen] = testMessage.content.screens;
  screen.anchors[0].selector = "body";
  testMessage.content.screens = [screen];
  featureCallout.showFeatureCallout(testMessage);
  await waitForCalloutScreen(config.win.document, screen.id);
  testStyles(config);
  return { featureCallout };
}

add_task(async function feature_callout_chrome_theme() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await testCallout({
    win,
    location: "chrome",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "chrome" },
  });
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function feature_callout_link_colors_and_focus() {
  for (const nova of [false, true]) {
    await pushPrefs(["browser.nova.enabled", nova]);
    const win = await BrowserTestUtils.openNewBrowserWindow();
    try {
      const message = getTestMessage();
      message.content.screens[0].content.additional_button = {
        label: { raw: "Privacy notice" },
        style: "link",
        action: { type: "OPEN_URL", data: { args: "https://example.com/" } },
      };
      await showFeatureCallout(win.gBrowser.selectedBrowser, message);
      await waitForCalloutScreen(win.document, "TEST_CALLOUT_1");

      const callout = win.document.querySelector(calloutSelector);
      const link = callout.querySelector(".cta-link");
      const reference = win.document.createElement("span");
      reference.style.cssText =
        "outline: var(--focus-outline); outline-offset: var(--focus-outline-offset); border-radius: var(--button-border-radius)";
      callout.append(reference);

      for (const scheme of ["light", "dark"]) {
        win.document.documentElement.setAttribute("lwt-popup", scheme);
        for (const [state, token] of [
          [null, "--link-color"],
          [":hover", "--link-color-hover"],
          [":active", "--link-color-active"],
        ]) {
          if (state) {
            InspectorUtils.addPseudoClassLock(link, state);
          }
          reference.style.color = `var(${token})`;
          is(
            win.getComputedStyle(link).color,
            win.getComputedStyle(reference).color,
            `Link uses ${token} in ${scheme} mode with Nova ${nova}`
          );
          if (state) {
            InspectorUtils.removePseudoClassLock(link, state);
          }
        }

        InspectorUtils.addPseudoClassLock(link, ":focus-visible");
        for (const property of [
          "outlineColor",
          "outlineStyle",
          "outlineWidth",
          "outlineOffset",
          "borderRadius",
        ]) {
          is(
            win.getComputedStyle(link)[property],
            win.getComputedStyle(reference)[property],
            `Link uses themed ${property} in ${scheme} mode with Nova ${nova}`
          );
        }
        InspectorUtils.removePseudoClassLock(link, ":focus-visible");
      }
    } finally {
      await BrowserTestUtils.closeWindow(win);
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function feature_callout_pdfjs_theme() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await testCallout({
    win,
    location: "pdfjs",
    context: "chrome",
    browser: win.gBrowser.selectedBrowser,
    theme: { preset: "pdfjs", simulateContent: true },
  });
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function page_event_listeners_every_window() {
  const testMessage2 = getTestMessage();
  testMessage2.content.screens[0].content.page_event_listeners = [
    {
      params: {
        type: "TabPinned",
        selectors: "#main-window",
        options: {
          every_window: true,
        },
      },
      action: {
        dismiss: true,
      },
    },
  ];
  const sandbox = sinon.createSandbox();
  const spy = new TelemetrySpy(sandbox);

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  await showFeatureCallout(browser, testMessage2);
  info("Waiting for callout to render");

  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  const newTab = await BrowserTestUtils.openNewForegroundTab(
    newWin.gBrowser,
    "about:mozilla"
  );

  info("Waiting to pin tab in new window");
  let newTabPinned = BrowserTestUtils.waitForEvent(newTab, "TabPinned");
  newWin.gBrowser.pinTab(newTab);
  await newTabPinned;

  ok(newTab.pinned, "Tab should be pinned");

  info("Waiting to dismiss callout");
  await waitForCalloutRemoved(doc);
  ok(
    !doc.querySelector(calloutSelector),
    "Callout is removed from screen on page_event"
  );

  //Check dismiss telemetry
  spy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: sinon.match("tab.tabbrowser-tab"),
      page: "chrome",
    },
    message_id: "TEST_CALLOUT",
  });
  await BrowserTestUtils.closeWindow(newWin);
  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});

add_task(async function multiple_page_event_listeners_every_window() {
  const testMessage2 = getTestMessage();
  testMessage2.content.screens[0].content.page_event_listeners = [
    {
      params: {
        type: "click",
        selectors: "#PanelUI-menu-button",
        options: {
          every_window: true,
        },
      },
      action: {
        dismiss: true,
      },
    },
    {
      params: {
        type: "click",
        selectors: "#unified-extensions-button",
        options: {
          every_window: true,
        },
      },
      action: {
        dismiss: true,
      },
    },
    {
      params: {
        type: "click",
        selectors: "#fxa-toolbar-menu-button",
        options: {
          every_window: true,
        },
      },
      action: {
        dismiss: true,
      },
    },
  ];
  const sandbox = sinon.createSandbox();
  const spy = new TelemetrySpy(sandbox);

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const doc = win.document;
  const browser = win.gBrowser.selectedBrowser;

  await showFeatureCallout(browser, testMessage2);
  info("Waiting for callout to render");

  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  const doc2 = newWin.document;

  info("Clicking on button - second page event listener");
  doc2.querySelector("#unified-extensions-button").click();

  info("Waiting to dismiss callout");
  await waitForCalloutRemoved(doc);
  ok(
    !doc.querySelector(calloutSelector),
    "Callout is removed from screen on page_event"
  );

  //Check dismiss telemetry
  spy.assertCalledWith({
    event: "DISMISS",
    event_context: {
      source: sinon.match("#unified-extensions-button"),
      page: "chrome",
    },
    message_id: "TEST_CALLOUT",
  });

  await BrowserTestUtils.closeWindow(newWin);
  await BrowserTestUtils.closeWindow(win);
  sandbox.restore();
});
