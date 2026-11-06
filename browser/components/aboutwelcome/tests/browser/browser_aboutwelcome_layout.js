"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

add_setup(async function () {
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("browser.aboutwelcome.didSeeFinalScreen");
  });
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
  await test_element_styles(
    browser,
    ".action-buttons .secondary-cta .secondary",
    // Expected styles:
    {
      // Override default text-link styles
      "background-color": "rgba(0, 0, 0, 0)",
      color: "rgb(22, 20, 35)",
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
  await test_element_styles(
    browser,
    "main.screen .section-main .main-content",
    // Expected styles:
    {
      "backdrop-filter": "none",
      "background-color": "rgba(0, 0, 0, 0)",
      "border-left-style": "none",
      "border-left-width": "0px",
    }
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const mainContentInner = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(
        "main.screen .section-main .main-content .main-content-inner"
      )
    );
    const glowStyles = content.window.getComputedStyle(
      mainContentInner,
      "::before"
    );
    is(glowStyles.filter, "blur(70px)", "center-large glow should be blurred");
    isnot(
      glowStyles.backgroundColor,
      "rgba(0, 0, 0, 0)",
      "center-large glow should have a visible fill color"
    );
  });

  // Ensure secondary action has button styling
  await test_element_styles(
    browser,
    ".action-buttons .secondary-cta .secondary",
    // Expected styles:
    {
      // Override default text-link styles
      "background-color": "rgba(0, 0, 0, 0)",
      color: "rgb(22, 20, 35)",
    }
  );
  await SpecialPowers.popPrefEnv();
  browser.closeBrowser();
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
