"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const { AboutWelcomeTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeTelemetry.sys.mjs"
);

add_setup(async function () {
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("browser.aboutwelcome.didSeeFinalScreen");
  });
});

/**
 * Test rendering a screen with a dismiss button
 */
add_task(async function test_aboutwelcome_dismiss_button() {
  let browser = await openAboutWelcome(
    JSON.stringify(
      // Use 2 screens to test that the message is dismissed, not navigated
      [1, 2].map(i =>
        makeTestContent(`TEST_DISMISS_STEP_${i}`, {
          dismiss_button: { action: { dismiss: true }, size: "small" },
        })
      )
    )
  );

  await test_screen_content(
    browser,
    "renders screen with dismiss button",
    // Expected selectors:
    ['div.section-main button.dismiss-button[button-size="small"]']
  );

  // Click dismiss button
  await onButtonClick(browser, "button.dismiss-button");

  // Wait for about:home to load
  await BrowserTestUtils.browserLoaded(browser, false, "about:home");
  is(browser.currentURI.spec, "about:home", "about:home loaded");

  browser.closeBrowser();
});

/**
 * Test rendering a screen with a "progress bar" style step indicator
 */
add_task(async function test_aboutwelcome_with_progress_bar() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["ui.systemUsesDarkTheme", 0],
      ["ui.prefersReducedMotion", 0],
      ["ui.useAccessibilityTheme", 0],
    ],
  });
  let screens = [];
  // we need at least three screens to test the progress bar styling
  for (let i = 0; i < 3; i++) {
    screens.push(
      makeTestContent(`TEST_MR_PROGRESS_BAR_${i + 1}`, {
        position: "split",
        progress_bar: true,
        primary_button: {
          label: "next",
          action: {
            navigate: true,
          },
        },
      })
    );
  }

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: {
      enabled: true,
      screens,
    },
  });
  let browser = await openAboutWelcome(JSON.stringify(screens));

  await SpecialPowers.spawn(browser, [], async () => {
    const progressBar = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".progress-bar")
    );
    const indicator = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".indicator")
    );
    // Progress bar should have a gray background.
    is(
      content.window.getComputedStyle(progressBar)["background-color"],
      "color(srgb 0.0862745 0.0784314 0.137255 / 0.25)",
      "Correct progress bar background"
    );

    const indicatorStyles = content.window.getComputedStyle(indicator);
    for (let [key, val] of Object.entries({
      // The filled "completed" element should have
      // `background-color: var(--button-background-color-primary);`
      "background-color": "rgb(118, 78, 221)",
      // Base progress bar step styles.
      height: "6px",
      "margin-inline": "-1px",
      "padding-block": "0px",
    })) {
      is(indicatorStyles[key], val, `Correct indicator ${key} style`);
    }
    const indicatorX = indicator.getBoundingClientRect().x;
    content.document.querySelector("button.primary").click();
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(".indicator")?.getBoundingClientRect()
          .x > indicatorX,
      "Indicator should have grown"
    );
  });

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a message with session history updates disabled
 */
add_task(async function test_aboutwelcome_history_updates_disabled() {
  let screens = [];
  // we need at least two screens to test the history state
  for (let i = 1; i < 3; i++) {
    screens.push(makeTestContent(`TEST_PUSH_STATE_STEP_${i}`));
  }
  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: {
      enabled: true,
      disableHistoryUpdates: true,
      screens,
    },
  });
  let browser = await openAboutWelcome(JSON.stringify(screens));

  let startHistoryLength = await SpecialPowers.spawn(browser, [], () => {
    return content.window.history.length;
  });
  // Advance to second screen
  await onButtonClick(browser, "button.primary");
  let endHistoryLength = await SpecialPowers.spawn(browser, [], async () => {
    // Ensure next screen has rendered
    await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".TEST_PUSH_STATE_STEP_2")
    );
    return content.window.history.length;
  });

  Assert.strictEqual(
    startHistoryLength,
    endHistoryLength,
    "No entries added to the session's history stack with history updates disabled"
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a message that starts on a specific screen
 */
add_task(async function test_aboutwelcome_start_screen_configured() {
  let startScreen = 1;
  let screens = [];
  // we need at least two screens to test
  for (let i = 1; i < 3; i++) {
    screens.push(makeTestContent(`TEST_START_STEP_${i}`));
  }
  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: {
      enabled: true,
      startScreen,
      screens,
    },
  });

  let sandbox = sinon.createSandbox();
  let spy = sandbox.spy(AboutWelcomeTelemetry.prototype, "sendTelemetry");

  let browser = await openAboutWelcome(JSON.stringify(screens));

  let secondScreenShown = await SpecialPowers.spawn(browser, [], async () => {
    // Ensure screen has rendered
    await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".TEST_START_STEP_2")
    );
    return true;
  });

  ok(
    secondScreenShown,
    `Starts on second screen when configured with startScreen index equal to ${startScreen}`
  );
  // Wait for screen elements to render before checking impression pings
  await test_screen_content(
    browser,
    "renders second screen elements",
    // Expected selectors:
    [`main.screen`, "div.secondary-cta"]
  );

  let expectedTelemetry = sinon.match({
    event: "IMPRESSION",
    message_id: `MR_WELCOME_DEFAULT_${startScreen}_TEST_START_STEP_${
      startScreen + 1
    }_${screens.map(({ id }) => id?.split("_")[1]?.[0]).join("")}`,
  });
  if (spy.calledWith(expectedTelemetry)) {
    ok(
      true,
      "Impression events have the correct message id with start screen configured"
    );
  } else if (spy.called) {
    ok(
      false,
      `Wrong telemetry sent: ${JSON.stringify(
        spy.getCalls().map(c => c.args[0]),
        null,
        2
      )}`
    );
  } else {
    ok(false, "No telemetry sent");
  }

  await doExperimentCleanup();
  browser.closeBrowser();
  sandbox.restore();
});

/**
 * Test rendering the dismiss button on a reversed split layout screen
 */
add_task(async function test_aboutwelcome_reverse_dismiss() {
  let screens = [
    makeTestContent(`TEST_REVERSE_DISMISS`, {
      reverse_split: true,
      position: "split",
      dismiss_button: { action: { dismiss: true } },
    }),
  ];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render screen with 'reverse_split' attribute",
    // Expected selectors:
    ["main.TEST_REVERSE_DISMISS[reverse-split]"]
  );

  await test_screen_content(
    browser,
    "renders screen with dismiss button on secondary section",
    // Expected selectors:
    [".section-secondary .dismiss-button"]
  );

  // Click dismiss button
  await onButtonClick(browser, "button.dismiss-button");

  // Wait for about:home to load
  await BrowserTestUtils.browserLoaded(browser, false, "about:home");
  is(browser.currentURI.spec, "about:home", "about:home loaded");

  await doExperimentCleanup();
  browser.closeBrowser();
});
