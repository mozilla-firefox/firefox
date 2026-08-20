"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const { AboutWelcomeTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeTelemetry.sys.mjs"
);

const BASE_SCREEN_CONTENT = {
  title: "Step 1",
  primary_button: {
    label: "Next",
    action: {
      navigate: true,
    },
  },
  secondary_button: {
    label: "link",
  },
};

const makeTestContent = (id, contentAdditions) => {
  return {
    id,
    content: Object.assign({}, BASE_SCREEN_CONTENT, contentAdditions),
  };
};

async function testAboutWelcomeLogoFor(logo = {}) {
  info(`Testing logo: ${JSON.stringify(logo)}`);

  let screens = [makeTestContent("TEST_LOGO_SELECTION_STEP", { logo })];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome(JSON.stringify(screens));

  let expected = [
    `.brand-logo[src="${
      logo.imageURL ?? "chrome://branding/content/about-logo.svg"
    }"][alt=""]`,
  ];
  let unexpected = [];
  (logo.alt ? unexpected : expected).push('.brand-logo[role="presentation"]');
  (logo.width ? expected : unexpected).push(`.brand-logo[style*="width"]`);
  (logo.height ? expected : unexpected).push(`.brand-logo[style*="height"]`);
  (logo.marginBlock ? expected : unexpected).push(
    `.logo-container[style*="margin-block"]`
  );
  (logo.marginInline ? expected : unexpected).push(
    `.logo-container[style*="margin-inline"]`
  );
  (logo.darkModeImageURL ? expected : unexpected).push(
    `.logo-container source[media="(prefers-color-scheme: dark)"]${
      logo.darkModeImageURL ? `[srcset="${logo.darkModeImageURL}"]` : ""
    }`
  );
  (logo.reducedMotionImageURL ? expected : unexpected).push(
    `.logo-container source[media="(prefers-reduced-motion: reduce)"]${
      logo.reducedMotionImageURL
        ? `[srcset="${logo.reducedMotionImageURL}"]`
        : ""
    }`
  );
  (logo.darkModeReducedMotionImageURL ? expected : unexpected).push(
    `.logo-container source[media="(prefers-color-scheme: dark) and (prefers-reduced-motion: reduce)"]${
      logo.darkModeReducedMotionImageURL
        ? `[srcset="${logo.darkModeReducedMotionImageURL}"]`
        : ""
    }`
  );
  await test_screen_content(
    browser,
    "renders screen with passed logo",
    expected,
    unexpected
  );

  await doExperimentCleanup();
  browser.closeBrowser();
}

add_setup(async function () {
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("browser.aboutwelcome.didSeeFinalScreen");
  });
});

/**
 * Test rendering a screen in about welcome with decorative noodles
 */
add_task(async function test_aboutwelcome_with_noodles() {
  const TEST_NOODLE_CONTENT = makeTestContent("TEST_NOODLE_STEP", {
    has_noodles: true,
  });
  const TEST_NOODLE_JSON = JSON.stringify([TEST_NOODLE_CONTENT]);
  let browser = await openAboutWelcome(TEST_NOODLE_JSON);

  await test_screen_content(
    browser,
    "renders screen with noodles",
    // Expected selectors:
    [
      "main.TEST_NOODLE_STEP[pos='center']",
      "div.noodle.purple-C",
      "div.noodle.orange-L",
      "div.noodle.outline-L",
      "div.noodle.yellow-circle",
    ]
  );
  browser.closeBrowser();
});

/**
 * Test rendering a screen with a customized logo
 */
add_task(async function test_aboutwelcome_with_customized_logo() {
  const TEST_LOGO_URL = "chrome://branding/content/icon64.png";
  const TEST_LOGO_HEIGHT = "50px";
  const TEST_LOGO_CONTENT = makeTestContent("TEST_LOGO_STEP", {
    logo: {
      height: TEST_LOGO_HEIGHT,
      imageURL: TEST_LOGO_URL,
    },
  });
  const TEST_LOGO_JSON = JSON.stringify([TEST_LOGO_CONTENT]);
  let browser = await openAboutWelcome(TEST_LOGO_JSON);

  await test_screen_content(
    browser,
    "renders screen with customized logo",
    // Expected selectors:
    ["main.TEST_LOGO_STEP[pos='center']", `.brand-logo[src="${TEST_LOGO_URL}"]`]
  );

  // Ensure logo has custom height
  await test_element_styles(
    browser,
    ".brand-logo",
    // Expected styles:
    {
      // Override default text-link styles
      height: TEST_LOGO_HEIGHT,
    }
  );
  browser.closeBrowser();
});

/**
 * Test rendering a screen with empty logo used for padding
 */
add_task(async function test_aboutwelcome_with_empty_logo_spacing() {
  const TEST_LOGO_HEIGHT = "50px";
  const TEST_LOGO_CONTENT = makeTestContent("TEST_LOGO_STEP", {
    logo: {
      height: TEST_LOGO_HEIGHT,
      imageURL: "none",
    },
  });
  const TEST_LOGO_JSON = JSON.stringify([TEST_LOGO_CONTENT]);
  let browser = await openAboutWelcome(TEST_LOGO_JSON);

  await test_screen_content(
    browser,
    "renders screen with empty logo element",
    // Expected selectors:
    ["main.TEST_LOGO_STEP[pos='center']", ".brand-logo[src='none']"]
  );

  // Ensure logo has custom height
  await test_element_styles(
    browser,
    ".brand-logo",
    // Expected styles:
    {
      // Override default text-link styles
      height: TEST_LOGO_HEIGHT,
    }
  );
  browser.closeBrowser();
});

/**
 * Test rendering a screen with a "Title Logo"
 */
add_task(async function test_title_logo() {
  const TEST_TITLE_LOGO_URL = "chrome://branding/content/icon64.png";
  const TEST_CONTENT = makeTestContent("TITLE_LOGO", {
    title_logo: {
      imageURL: TEST_TITLE_LOGO_URL,
    },
  });
  const TEST_JSON = JSON.stringify([TEST_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders screen with title_logo",
    // Expected selectors:
    [".inline-icon-container"]
  );

  browser.closeBrowser();
});

/**
 * Test rendering a screen with a title with custom styles.
 */
add_task(async function test_aboutwelcome_with_title_styles() {
  const TEST_TITLE_STYLE_CONTENT = makeTestContent("TEST_TITLE_STYLE_STEP", {
    title: {
      fontSize: "36px",
      fontWeight: 276,
      letterSpacing: 0,
      raw: "test",
    },
    title_style: "fancy shine",
  });

  const TEST_TITLE_STYLE_JSON = JSON.stringify([TEST_TITLE_STYLE_CONTENT]);
  let browser = await openAboutWelcome(TEST_TITLE_STYLE_JSON);

  await test_screen_content(
    browser,
    "renders screen with customized title style",
    // Expected selectors:
    [`div.welcome-text.fancy.shine`]
  );

  await test_element_styles(
    browser,
    "#mainContentHeader",
    // Expected styles:
    {
      "font-weight": "276",
      "font-size": "36px",
      animation: "50s linear infinite shine",
      "letter-spacing": "normal",
    }
  );
  browser.closeBrowser();
});

/**
 * Test rendering a screen with an image for the dialog window's background
 */
add_task(async function test_aboutwelcome_with_background() {
  const BACKGROUND_URL =
    "chrome://activity-stream/content/data/content/assets/confetti.svg";
  const TEST_BACKGROUND_CONTENT = makeTestContent("TEST_BACKGROUND_STEP", {
    background: `url(${BACKGROUND_URL}) no-repeat center/cover`,
  });

  const TEST_BACKGROUND_JSON = JSON.stringify([TEST_BACKGROUND_CONTENT]);
  let browser = await openAboutWelcome(TEST_BACKGROUND_JSON);

  await test_screen_content(
    browser,
    "renders screen with dialog background image",
    // Expected selectors:
    [`div.main-content[style*='${BACKGROUND_URL}'`]
  );
  browser.closeBrowser();
});

/**
 * Test rendering a screen with defined dimensions
 */
add_task(async function test_aboutwelcome_with_dimensions() {
  const TEST_DIMENSIONS_CONTENT = makeTestContent("TEST_DIMENSIONS_STEP", {
    width: "100px",
    position: "center",
  });

  const TEST_DIMENSIONS_JSON = JSON.stringify([TEST_DIMENSIONS_CONTENT]);
  let browser = await openAboutWelcome(TEST_DIMENSIONS_JSON);

  await test_screen_content(
    browser,
    "renders screen with defined dimensions",
    // Expected selectors:
    [`div.main-content[style*='width: 100px;']`]
  );
  browser.closeBrowser();
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
 * Test rendering a screen with the "split" position
 */
add_task(async function test_aboutwelcome_split_position() {
  // Forcing light-mode prevents the test from failing locally if your OS is in dark-mode
  await SpecialPowers.pushPrefEnv({
    set: [["ui.systemUsesDarkTheme", 0]],
  });

  const TEST_SPLIT_STEP = makeTestContent("TEST_SPLIT_STEP", {
    position: "split",
    hero_text: "hero test",
  });

  const TEST_SPLIT_JSON = JSON.stringify([TEST_SPLIT_STEP]);
  let browser = await openAboutWelcome(TEST_SPLIT_JSON);

  await test_screen_content(
    browser,
    "renders screen secondary section containing hero text",
    // Expected selectors:
    [`main.screen[pos="split"]`, `.section-secondary`, `.message-text h1`]
  );

  // Ensure secondary section has split template styling
  await test_element_styles(
    browser,
    "main.screen .section-secondary",
    // Expected styles:
    {
      display: "flex",
      margin: "auto 0px auto auto",
    }
  );

  // Ensure secondary action has button styling
  const novaEnabled = Services.prefs.getBoolPref("browser.nova.enabled", false);
  await test_element_styles(
    browser,
    ".action-buttons .secondary-cta .secondary",
    // Expected styles:
    {
      // Override default text-link styles
      "background-color": novaEnabled
        ? "rgba(0, 0, 0, 0)"
        : "color(srgb 0.0823529 0.0784314 0.101961 / 0.07)",
      color: novaEnabled ? "rgb(22, 20, 35)" : "rgb(21, 20, 26)",
    }
  );
  await SpecialPowers.popPrefEnv();
  browser.closeBrowser();
});

/**
 * Test rendering a screen with the "center-large" position
 */
add_task(async function test_aboutwelcome_center_large_position() {
  // Forcing light-mode prevents the test from failing locally if your OS is in dark-mode
  await SpecialPowers.pushPrefEnv({
    set: [["ui.systemUsesDarkTheme", 0]],
  });

  const TEST_CENTER_LARGE_STEP = makeTestContent("TEST_CENTER_LARGE_STEP", {
    position: "center-large",
    fullscreen: true,
    title: "Test title",
    subtitle: "Test subtitle",
    corner_image: {
      position: "bottom-right",
      imageURL:
        "chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif",
      height: "200px",
    },
  });

  const TEST_CENTER_LARGE_JSON = JSON.stringify([TEST_CENTER_LARGE_STEP]);
  let browser = await openAboutWelcome(TEST_CENTER_LARGE_JSON);

  await test_screen_content(
    browser,
    "renders screen main section with the container for the corner image",
    // Expected selectors:
    [
      `main.screen[pos="center-large"]`,
      `.section-main`,
      `.corner-image-container`,
    ]
  );

  // Ensure main section has center-large template styling.
  const novaEnabled = Services.prefs.getBoolPref("browser.nova.enabled", false);
  await test_element_styles(
    browser,
    "main.screen .section-main .main-content",
    // Expected styles:
    {
      "backdrop-filter": "blur(8px)",
      "background-color": "color(srgb 0 0 0 / 0.05)",
      "border-left-color": novaEnabled
        ? "rgb(214, 213, 218)"
        : "rgb(186, 194, 202)",
      "border-left-style": "solid",
      "border-left-width": "1px",
    }
  );

  // Ensure secondary action has button styling
  await test_element_styles(
    browser,
    ".action-buttons .secondary-cta .secondary",
    // Expected styles:
    {
      // Override default text-link styles
      "background-color": novaEnabled
        ? "rgba(0, 0, 0, 0)"
        : "color(srgb 0.0823529 0.0784314 0.101961 / 0.07)",
      color: novaEnabled ? "rgb(22, 20, 35)" : "rgb(21, 20, 26)",
    }
  );
  await SpecialPowers.popPrefEnv();
  browser.closeBrowser();
});

const CORNER_IMAGE_URL =
  "chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif";

const makeCornerImageScreen = (id, cornerImageAdditions, contentAdditions) =>
  makeTestContent(id, {
    position: "center-large",
    fullscreen: true,
    corner_image: {
      imageURL: CORNER_IMAGE_URL,
      height: "200px",
      ...cornerImageAdditions,
    },
    ...contentAdditions,
  });

/**
 * Test that the corner image is anchored to each supported corner
 */
add_task(async function test_aboutwelcome_corner_image_positions() {
  const CORNER_OFFSETS = {
    "bottom-left": { bottom: "-10px", left: "20px" },
    "bottom-right": { bottom: "-10px", right: "20px" },
    "top-left": { top: "-10px", left: "20px" },
    "top-right": { top: "-10px", right: "20px" },
  };

  for (const [position, offsets] of Object.entries(CORNER_OFFSETS)) {
    info(`Testing corner image position: ${position}`);
    const screens = [
      makeCornerImageScreen(`TEST_CORNER_IMAGE_${position}`, { position }),
    ];
    let browser = await openAboutWelcome(JSON.stringify(screens));

    await test_screen_content(
      browser,
      `renders the corner image in the ${position} corner, outside section-main`,
      // Expected selectors:
      [
        `main.screen > .corner-image-container picture.corner-image.${position}`,
      ],
      // Unexpected selectors:
      [".section-main .corner-image-container"]
    );

    await test_element_styles(browser, "picture.corner-image", {
      position: "absolute",
      ...offsets,
    });

    browser.closeBrowser();
  }
});

/**
 * Test that a corner image position outside the allowlist, or omitted entirely,
 * falls back to bottom-right
 */
add_task(async function test_aboutwelcome_corner_image_fallback_position() {
  const FALLBACK_CASES = [
    {
      label: "an unsupported position",
      cornerImage: { position: "not-a-corner" },
    },
    { label: "an omitted position", cornerImage: {} },
  ];

  for (const { label, cornerImage } of FALLBACK_CASES) {
    info(`Testing corner image fallback for ${label}`);
    const screens = [
      makeCornerImageScreen("TEST_CORNER_IMAGE_FALLBACK", cornerImage),
    ];
    let browser = await openAboutWelcome(JSON.stringify(screens));

    await test_screen_content(
      browser,
      `falls back to bottom-right for ${label}`,
      // Expected selectors:
      ["picture.corner-image.bottom-right"],
      // Unexpected selectors:
      ["picture.corner-image.not-a-corner", "picture.corner-image.undefined"]
    );

    browser.closeBrowser();
  }
});

/**
 * Test that the corner image only renders in the layout that styles it
 */
add_task(async function test_aboutwelcome_corner_image_layout_gating() {
  const UNSTYLED_LAYOUTS = [
    {
      label: "center-large without fullscreen",
      content: { fullscreen: false },
    },
    { label: "split", content: { position: "split" } },
  ];

  for (const { label, content } of UNSTYLED_LAYOUTS) {
    info(`Testing that the corner image is not rendered for ${label}`);
    const screens = [
      makeCornerImageScreen(
        "TEST_CORNER_IMAGE_GATING",
        { position: "bottom-right" },
        content
      ),
    ];
    let browser = await openAboutWelcome(JSON.stringify(screens));

    await test_screen_content(
      browser,
      `does not render the corner image for ${label}`,
      // Expected selectors:
      [".section-main"],
      // Unexpected selectors:
      [".corner-image-container", "picture.corner-image"]
    );

    browser.closeBrowser();
  }
});

/**
 * Test that the top buttons get their own row inside the card in the
 * "center-large" layout, rather than being overlaid on the content
 */
add_task(async function test_aboutwelcome_center_large_top_buttons_row() {
  const screens = [
    makeTestContent("TEST_CENTER_LARGE_TOP_BUTTONS", {
      position: "center-large",
      fullscreen: true,
      secondary_button_top: [
        { label: { raw: "test button 1" }, action: { navigate: true } },
        { label: { raw: "test button 2" }, action: { navigate: true } },
      ],
    }),
  ];
  let browser = await openAboutWelcome(JSON.stringify(screens));

  await test_screen_content(
    browser,
    "renders the top buttons as a row inside the card",
    // Expected selectors:
    [
      ".main-content > .secondary-buttons-top-container",
      "#secondary_button_0",
      "#secondary_button_1",
    ],
    // Unexpected selectors:
    [".section-main > .secondary-buttons-top-container"]
  );

  await test_element_styles(
    browser,
    ".main-content > .secondary-buttons-top-container",
    // Expected styles:
    {
      position: "static",
      "align-self": "flex-end",
    }
  );

  browser.closeBrowser();
});

/**
 * Test rendering a screen with a URL value and default color for backdrop
 */
add_task(async function test_aboutwelcome_with_url_backdrop() {
  const TEST_BACKDROP_URL = `url("chrome://activity-stream/content/data/content/assets/confetti.svg")`;
  const TEST_BACKDROP_VALUE = `#212121 ${TEST_BACKDROP_URL} center/cover no-repeat fixed`;
  const TEST_URL_BACKDROP_CONTENT = makeTestContent("TEST_URL_BACKDROP_STEP");

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: {
      enabled: true,
      backdrop: TEST_BACKDROP_VALUE,
      screens: [TEST_URL_BACKDROP_CONTENT],
    },
  });
  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "renders screen with background image",
    // Expected selectors:
    [`div.outer-wrapper.onboardingContainer[style*='${TEST_BACKDROP_URL}']`]
  );
  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a screen with a color name for backdrop
 */
add_task(async function test_aboutwelcome_with_color_backdrop() {
  const TEST_BACKDROP_COLOR = "transparent";
  const TEST_BACKDROP_COLOR_CONTENT = makeTestContent(
    "TEST_COLOR_NAME_BACKDROP_STEP"
  );

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: {
      enabled: true,
      backdrop: TEST_BACKDROP_COLOR,
      screens: [TEST_BACKDROP_COLOR_CONTENT],
    },
  });
  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "renders screen with background color",
    // Expected selectors:
    [`div.outer-wrapper.onboardingContainer[style*='${TEST_BACKDROP_COLOR}']`]
  );
  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a screen with a text color override
 */
add_task(async function test_aboutwelcome_with_text_color_override() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Override the system color scheme to dark
      ["ui.systemUsesDarkTheme", 1],
    ],
  });

  let screens = [];
  // we need at least two screens to test the step indicator
  for (let i = 0; i < 2; i++) {
    screens.push(
      makeTestContent("TEST_TEXT_COLOR_OVERRIDE_STEP", {
        text_color: "dark",
        background: "white",
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

  await test_screen_content(
    browser,
    "renders screen with dark text",
    // Expected selectors:
    [`main.screen.dark-text`, `.indicator.current`, `.indicator:not(.current)`],
    // Unexpected selectors:
    [`main.screen.light-text`]
  );

  // Ensure title inherits light text color
  const novaEnabled = Services.prefs.getBoolPref("browser.nova.enabled", false);
  await test_element_styles(
    browser,
    "#mainContentHeader",
    // Expected styles:
    {
      color: novaEnabled ? "rgb(24, 14, 48)" : "rgb(21, 20, 26)",
    }
  );

  // Ensure next step indicator inherits light color
  await test_element_styles(
    browser,
    ".indicator:not(.current)",
    // Expected styles:
    {
      color: novaEnabled ? "rgb(242, 240, 248)" : "rgb(251, 251, 254)",
    }
  );

  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
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

  const novaEnabled = Services.prefs.getBoolPref("browser.nova.enabled", false);
  await SpecialPowers.spawn(browser, [novaEnabled], async isNova => {
    const progressBar = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".progress-bar")
    );
    const indicator = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(".indicator")
    );
    // Progress bar should have a gray background.
    is(
      content.window.getComputedStyle(progressBar)["background-color"],
      isNova
        ? "color(srgb 0.0862745 0.0784314 0.137255 / 0.25)"
        : "color(srgb 0.0823529 0.0784314 0.101961 / 0.25)",
      "Correct progress bar background"
    );

    const indicatorStyles = content.window.getComputedStyle(indicator);
    for (let [key, val] of Object.entries({
      // The filled "completed" element should have
      // `background-color: var(--button-background-color-primary);`
      "background-color": isNova ? "rgb(118, 78, 221)" : "oklch(0.55 0.24 260)",
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
 * Test rendering a screen with different logos depending on reduced motion and
 * color scheme preferences
 */
add_task(async function test_aboutwelcome_logo_selection() {
  // Test a screen config that includes every logo parameter
  await testAboutWelcomeLogoFor({
    imageURL: "chrome://branding/content/icon16.png",
    darkModeImageURL: "chrome://branding/content/icon32.png",
    reducedMotionImageURL: "chrome://branding/content/icon64.png",
    darkModeReducedMotionImageURL: "chrome://branding/content/icon128.png",
    alt: "TEST_LOGO_SELECTION_ALT",
    width: "16px",
    height: "16px",
    marginBlock: "0px",
    marginInline: "0px",
  });
  // Test a screen config with no animated/static logos
  await testAboutWelcomeLogoFor({
    imageURL: "chrome://branding/content/icon16.png",
    darkModeImageURL: "chrome://branding/content/icon32.png",
  });
  // Test a screen config with no dark mode logos
  await testAboutWelcomeLogoFor({
    imageURL: "chrome://branding/content/icon16.png",
    reducedMotionImageURL: "chrome://branding/content/icon64.png",
  });
  // Test a screen config that includes only the default logo
  await testAboutWelcomeLogoFor({
    imageURL: "chrome://branding/content/icon16.png",
  });
  // Test a screen config with no logos
  await testAboutWelcomeLogoFor();
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
 * Test rendering a screen with that doesn't use responsive design
 */
add_task(async function test_aboutwelcome_rdm_property() {
  let screens = [makeTestContent(`TEST_NO_RDM`, { no_rdm: true })];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render screen with 'no-rdm' attribute",
    // Expected selectors:
    ["main.TEST_NO_RDM[no-rdm]"]
  );

  await doExperimentCleanup();
  browser.closeBrowser();
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

/**
 * Test rendering a screen with that uses fullscreen mode
 */
add_task(async function test_aboutwelcome_fullscreen_property() {
  let screens = [makeTestContent(`TEST_FULLSCREEN`, { fullscreen: true })];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render screen with 'fullscreen' attribute",
    // Expected selectors:
    ["main.TEST_FULLSCREEN[fullscreen]"]
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a split screen with that uses narrow mode
 */
add_task(async function test_aboutwelcome_narrow_property() {
  const logo = JSON.stringify([
    makeTestContent("TEST_LOGO_STEP", {
      logo: {
        imageURL: "chrome://branding/content/icon64.png",
        height: "50px",
      },
    }),
  ]);
  let screens = [
    makeTestContent(`TEST_FULLSCREEN`, {
      narrow: true,
      position: "split",
      logo,
    }),
  ];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render #multi-stage-message-root container with 'narrow' attribute",
    // Expected selectors:
    ["#multi-stage-message-root[narrow]"]
  );

  // Ensure elements get narrow styles
  await test_element_styles(
    browser,
    ".section-main",
    // Expected styles:
    {
      "margin-top": "0px",
      width: "400px", // $split-section-width
    }
  );

  await test_element_styles(
    browser,
    ".section-secondary",
    // Expected styles:
    {
      height: "100px", // $small-secondary-section-height
    }
  );

  await test_element_styles(
    browser,
    ".logo-container",
    // Expected styles:
    {
      "text-align": "center",
    }
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test configurability of single select picker icons styles
 */
add_task(async function test_aboutwelcome_single_select_icon_styles() {
  let screens = [
    makeTestContent(`TEST_SINGLE_SELECT_ICONS`, {
      tiles: {
        type: "single-select",
        selected: "horizontal",
        action: {
          picker: "<event>",
        },
        data: [
          {
            icon: {
              background: `center / contain no-repeat url("chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif")`,
              width: "150px",
              height: "100px",
              marginInline: "10px",
              borderRadius: "5px",
            },
            id: "test1",
            label: {
              raw: "test1 label",
            },
            action: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "test1.pref",
                  value: true,
                },
              },
            },
          },
          {
            defaultValue: true,
            icon: {
              background: `center / contain no-repeat url("chrome://activity-stream/content/data/content/assets/heart.webp")`,
              width: "150px",
              height: "100px",
              marginInline: "10px",
              borderRadius: "5px",
            },
            id: "test2",
            label: {
              raw: "test2 label",
            },
            action: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "test2.pref",
                  value: false,
                },
              },
            },
          },
        ],
      },
    }),
  ];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_element_styles(
    browser,
    ".icon.test1",
    // Expected styles:
    {
      "background-image":
        'url("chrome://activity-stream/content/data/content/assets/fox-doodle-waving.gif")',
      "background-repeat": "no-repeat",
      "background-size": "contain",
      width: "150px",
      height: "100px",
      "margin-inline": "10px",
      "border-radius": "5px",
    }
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test configurability of secondary_button_top
 */
add_task(async function test_secondary_button_top_configuration() {
  const secondaryTopContent = makeTestContent(`TEST_SECONDARY_TOP_CONTENT`, {
    secondary_button_top: [
      {
        label: {
          raw: "test button 1",
        },
        action: {
          navigate: true,
        },
      },
      {
        label: {
          raw: "test button 2",
        },
        action: {
          navigate: true,
        },
      },
    ],
  });

  let screens = [secondaryTopContent];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render the secondary top buttons in a container",
    // Expected selectors:
    [
      ".secondary-buttons-top-container",
      "#secondary_button_0",
      "#secondary_button_1",
    ]
  );

  // Ensure container has appropriate styles
  await test_element_styles(
    browser,
    ".secondary-buttons-top-container",
    // Expected styles:
    {
      display: "flex",
      "flex-direction": "row",
      position: "fixed",
      top: "10px",
    }
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

/**
 * Test rendering a fullscreen split screen that supports lengthy content
 */
add_task(async function test_aboutwelcome_fullscreen_split_layout_styles() {
  let screens = [
    makeTestContent("TEST_FULLSCREEN_SPLIT", {
      fullscreen: true,
      position: "split",
      background:
        "var(--mr-secondary-position) var(--mr-screen-background-color)",
      secondary_button_top: [
        {
          label: {
            raw: "Sign in",
          },
          action: {
            navigate: true,
          },
        },
      ],
    }),
  ];

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "aboutwelcome",
    value: { enabled: true, screens },
  });

  let browser = await openAboutWelcome();

  await test_screen_content(
    browser,
    "render fullscreen split screen",
    // Expected selectors:
    ["main.TEST_FULLSCREEN_SPLIT[pos='split'][fullscreen]"]
  );

  await test_element_styles(
    browser,
    ".onboardingContainer",
    // Expected styles:
    {
      display: "flex",
      flexDirection: "column",
    }
  );

  await test_element_styles(
    browser,
    ".section-main",
    // Expected styles:
    {
      margin: "0px",
      display: "flex",
    }
  );

  await test_element_styles(
    browser,
    ".section-main .main-content",
    // Expected styles:
    {
      flex: "1 1 0%",
      borderRadius: "0px",
      padding: "0px",
    }
  );

  await test_element_styles(
    browser,
    ".section-main .main-content .main-content-inner",
    // Expected styles:
    {
      paddingTop: "40px",
      paddingBottom: "40px",
    }
  );

  await test_element_styles(
    browser,
    ".secondary-buttons-top-container",
    // Expected styles:
    {
      zIndex: "2",
    }
  );

  await doExperimentCleanup();
  browser.closeBrowser();
});

add_task(async function test_aboutwelcome_link_paragraph_inline_image() {
  const TEST_ICON_URL =
    "chrome://global/skin/media/picture-in-picture-open.svg";
  const TEST_CONTENT = makeTestContent("TEST_LINK_PARAGRAPH_IMAGE_STEP", {
    above_button_content: [
      {
        type: "text",
        text: [
          "Learn more ",
          { imageURL: TEST_ICON_URL, alt: "icon" },
          " here.",
        ],
      },
    ],
  });

  let browser = await openAboutWelcome(JSON.stringify([TEST_CONTENT]));

  await test_screen_content(
    browser,
    "renders an image segment in a LinkParagraph",
    [`.link-paragraph img.inline-icon[src="${TEST_ICON_URL}"][alt="icon"]`]
  );

  browser.closeBrowser();
});
