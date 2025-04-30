"use strict";

const { AboutMessagePreviewParent } = ChromeUtils.importESModule(
  "resource:///actors/AboutWelcomeParent.sys.mjs"
);

// A feature callout that needs no modification
const TEST_HAPPY_FEATURE_CALLOUT_MESSAGE = {
  id: "TEST_HAPPY_FEATURE_CALLOUT",
  template: "feature_callout",
  content: {
    id: "TEST_HAPPY_FEATURE_CALLOUT",
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
            panel_position: {
              anchor_attachment: "bottomcenter",
              callout_attachment: "topright",
            },
          },
        ],
        content: {
          position: "callout",
          title: {
            raw: "Panel Feature Callout",
          },
          subtitle: {
            raw: "Hello!",
          },
          secondary_button: {
            label: {
              raw: "Advance",
            },
            action: {
              navigate: true,
            },
          },
          submenu_button: {
            submenu: [
              {
                type: "action",
                label: {
                  raw: "Item 1",
                },
                action: {
                  navigate: true,
                },
                id: "item1",
              },
            ],
            attached_to: "secondary_button",
          },
          dismiss_button: {
            action: {
              dismiss: true,
            },
          },
        },
      },
    ],
  },
  trigger: { id: "nthTabClosed" },
  targeting: true,
  provider: "panel_local_testing",
};

// A feature callout where the targeting and trigger need adjusting
const TEST_FEATURE_CALLOUT_NO_TRIGGER = {
  id: "TEST_FEATURE_CALLOUT_NO_TRIGGER",
  template: "feature_callout",
  content: {
    id: "TEST_FEATURE_CALLOUT_NO_TRIGGER",
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
            panel_position: {
              anchor_attachment: "bottomcenter",
              callout_attachment: "topright",
            },
          },
        ],
        content: {
          position: "callout",
          title: {
            raw: "Panel Feature Callout",
          },
          subtitle: {
            raw: "Hello!",
          },
          secondary_button: {
            label: {
              raw: "Advance",
            },
            action: {
              navigate: true,
            },
          },
          submenu_button: {
            submenu: [
              {
                type: "action",
                label: {
                  raw: "Item 1",
                },
                action: {
                  navigate: true,
                },
                id: "item1",
              },
            ],
            attached_to: "secondary_button",
          },
          dismiss_button: {
            action: {
              dismiss: true,
            },
          },
        },
      },
    ],
  },
  targeting: "userPrefs.cfrFeatures && visitsCount >= 3",
  provider: "panel_local_testing",
};

// A feature callout with bad anchors
const TEST_FEATURE_CALLOUT_ANCHORS = {
  id: "TEST_FEATURE_CALLOUT_ANCHORS",
  template: "feature_callout",
  content: {
    id: "TEST_FEATURE_CALLOUT_ANCHORS",
    template: "multistage",
    backdrop: "transparent",
    transitions: false,
    disableHistoryUpdates: true,
    screens: [
      {
        id: "FEATURE_CALLOUT_1",
        anchors: [
          {
            selector:
              "#tabbrowser-tabs:not([overflow]):not([haspinnedtabs]) %triggerTab%[visuallyselected]",
            arrow_width: "33.94",
            panel_position: {
              anchor_attachment: "bottomcenter",
              callout_attachment: "topcenter",
              panel_position_string: "bottomcenter topcenter",
            },
          },
        ],
        content: {
          position: "callout",
          title: {
            raw: "Panel Feature Callout",
          },
          subtitle: {
            raw: "Hello!",
          },
          secondary_button: {
            label: {
              raw: "Advance",
            },
            action: {
              navigate: true,
            },
          },
          submenu_button: {
            submenu: [
              {
                type: "action",
                label: {
                  raw: "Item 1",
                },
                action: {
                  navigate: true,
                },
                id: "item1",
              },
            ],
            attached_to: "secondary_button",
          },
          dismiss_button: {
            action: {
              dismiss: true,
            },
          },
        },
      },
    ],
  },
  trigger: { id: "nthTabClosed" },
  targeting: true,
  provider: "panel_local_testing",
};

// A bad feature callout. Everything is wrong but it should still show
const TEST_VERY_BAD_FEATURE_CALLOUT = {
  id: "TEST_VERY_BAD_FEATURE_CALLOUT",
  template: "feature_callout",
  content: {
    id: "TEST_VERY_BAD_FEATURE_CALLOUT",
    template: "multistage",
    backdrop: "transparent",
    transitions: false,
    disableHistoryUpdates: true,
    screens: [
      {
        id: "FEATURE_CALLOUT_1",
        anchors: [
          {
            selector:
              "#tabbrowser-tabs:not([overflow]):not([haspinnedtabs]) %triggerTab%[visuallyselected]",
            arrow_width: "33.94",
            panel_position: {
              anchor_attachment: "bottomcenter",
              callout_attachment: "topcenter",
              panel_position_string: "bottomcenter topcenter",
            },
          },
        ],
        content: {
          position: "callout",
          title: {
            raw: "Panel Feature Callout",
          },
          subtitle: {
            raw: "Hello!",
          },
          secondary_button: {
            label: {
              raw: "Advance",
            },
            action: {
              navigate: true,
            },
          },
          submenu_button: {
            submenu: [
              {
                type: "action",
                label: {
                  raw: "Item 1",
                },
                action: {
                  navigate: true,
                },
                id: "item1",
              },
            ],
            attached_to: "secondary_button",
          },
          dismiss_button: {
            action: {
              dismiss: true,
            },
          },
        },
      },
    ],
  },
  targeting: "userPrefs.cfrFeatures && visitsCount >= 3",
  provider: "panel_local_testing",
};

/**
 * Test each version of the feature callout
 */
add_task(async function test_show_happy_feature_callout_message() {
  const messageSandbox = sinon.createSandbox();
  // FeatureCallout needs a new window
  const win = await BrowserTestUtils.openNewBrowserWindow();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:messagepreview",
    true
  );

  let aboutMessagePreviewActor = await getAboutMessagePreviewParent(
    tab.linkedBrowser
  );
  messageSandbox.spy(aboutMessagePreviewActor, "showMessage");
  registerCleanupFunction(() => {
    messageSandbox.restore();
  });

  await aboutMessagePreviewActor.receiveMessage({
    name: "MessagePreview:SHOW_MESSAGE",
    data: JSON.stringify(TEST_HAPPY_FEATURE_CALLOUT_MESSAGE),
    validationEnabled: false,
  });

  const { callCount } = aboutMessagePreviewActor.showMessage;
  Assert.greaterOrEqual(callCount, 1, "showMessage was called");

  await test_window_message_content(
    win,
    "renders the test feature callout",
    "FEATURE_CALLOUT_1",
    //Expected selectors
    [
      "main.FEATURE_CALLOUT_1", // screen element
      "h1#mainContentHeader", // main title
      "div.secondary-cta.split-button-container", // split button
    ]
  );

  await waitForClick("button.dismiss-button", win);
  await dialogClosed(win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_show_feature_callout_without_trigger() {
  const messageSandbox = sinon.createSandbox();
  // FeatureCallout needs a new window
  const win = await BrowserTestUtils.openNewBrowserWindow();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:messagepreview",
    true
  );

  let aboutMessagePreviewActor = await getAboutMessagePreviewParent(
    tab.linkedBrowser
  );
  messageSandbox.spy(aboutMessagePreviewActor, "showMessage");
  registerCleanupFunction(() => {
    messageSandbox.restore();
  });

  await aboutMessagePreviewActor.receiveMessage({
    name: "MessagePreview:SHOW_MESSAGE",
    data: JSON.stringify(TEST_FEATURE_CALLOUT_NO_TRIGGER),
    validationEnabled: false,
  });

  const { callCount } = aboutMessagePreviewActor.showMessage;
  Assert.greaterOrEqual(callCount, 1, "showMessage was called");

  await test_window_message_content(
    win,
    "renders the test feature callout",
    "FEATURE_CALLOUT_1",
    //Expected selectors
    [
      "main.FEATURE_CALLOUT_1", // screen element
      "h1#mainContentHeader", // main title
      "div.secondary-cta.split-button-container", // split button
    ]
  );

  await waitForClick("button.dismiss-button", win);
  await dialogClosed(win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_show_feature_callout_anchors() {
  const messageSandbox = sinon.createSandbox();
  // FeatureCallout needs a new window
  const win = await BrowserTestUtils.openNewBrowserWindow();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:messagepreview",
    true
  );

  let aboutMessagePreviewActor = await getAboutMessagePreviewParent(
    tab.linkedBrowser
  );
  messageSandbox.spy(aboutMessagePreviewActor, "showMessage");
  registerCleanupFunction(() => {
    messageSandbox.restore();
  });

  await aboutMessagePreviewActor.receiveMessage({
    name: "MessagePreview:SHOW_MESSAGE",
    data: JSON.stringify(TEST_FEATURE_CALLOUT_ANCHORS),
    validationEnabled: false,
  });

  const { callCount } = aboutMessagePreviewActor.showMessage;
  Assert.greaterOrEqual(callCount, 1, "showMessage was called");

  await test_window_message_content(
    win,
    "renders the test feature callout",
    "FEATURE_CALLOUT_1",
    //Expected selectors
    [
      "main.FEATURE_CALLOUT_1", // screen element
      "h1#mainContentHeader", // main title
      "div.secondary-cta.split-button-container", // split button
    ]
  );

  await waitForClick("button.dismiss-button", win);
  await dialogClosed(win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_show_bad_feature_callout_message() {
  const messageSandbox = sinon.createSandbox();
  // FeatureCallout needs a new window
  const win = await BrowserTestUtils.openNewBrowserWindow();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:messagepreview",
    true
  );

  let aboutMessagePreviewActor = await getAboutMessagePreviewParent(
    tab.linkedBrowser
  );
  messageSandbox.spy(aboutMessagePreviewActor, "showMessage");
  registerCleanupFunction(() => {
    messageSandbox.restore();
  });

  await aboutMessagePreviewActor.receiveMessage({
    name: "MessagePreview:SHOW_MESSAGE",
    data: JSON.stringify(TEST_VERY_BAD_FEATURE_CALLOUT),
    validationEnabled: false,
  });

  const { callCount } = aboutMessagePreviewActor.showMessage;
  Assert.greaterOrEqual(callCount, 1, "showMessage was called");

  await test_window_message_content(
    win,
    "renders the test feature callout",
    "FEATURE_CALLOUT_1",
    //Expected selectors
    [
      "main.FEATURE_CALLOUT_1", // screen element
      "h1#mainContentHeader", // main title
      "div.secondary-cta.split-button-container", // split button
    ]
  );

  await waitForClick("button.dismiss-button", win);
  await dialogClosed(win);
  await BrowserTestUtils.closeWindow(win);
});
