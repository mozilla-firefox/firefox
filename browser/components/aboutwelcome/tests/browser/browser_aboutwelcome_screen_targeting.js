"use strict";

const { ASRouterScreenUtils } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterScreenUtils.sys.mjs"
);

const { ASRouterTargeting } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTargeting.sys.mjs"
);

const { OnboardingMessageProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/OnboardingMessageProvider.sys.mjs"
);

function makeSplashScreen() {
  const message = OnboardingMessageProvider.getPreonboardingMessages().find(
    m => m.id === "NEW_USER_TOU_ONBOARDING"
  );
  return message.screens.find(s => s.id === "TOU_ONBOARDING_LOADING");
}

const TEST_DEFAULT_CONTENT = [
  {
    id: "AW_STEP1",
    content: {
      title: "Step 1",
      primary_button: {
        label: "Next",
        action: {
          navigate: true,
        },
      },
      secondary_button: {
        label: "Secondary",
      },
    },
  },
  {
    id: "AW_STEP2",
    targeting: "false",
    content: {
      title: "Step 2",
      primary_button: {
        label: "Next",
        action: {
          navigate: true,
        },
      },
      secondary_button: {
        label: "Secondary",
      },
    },
  },
  {
    id: "AW_STEP3",
    content: {
      title: "Step 3",
      primary_button: {
        label: "Next",
        action: {
          navigate: true,
        },
      },
      secondary_button: {
        label: "Secondary",
      },
    },
  },
];

const TEST_DEFAULT_JSON = JSON.stringify(TEST_DEFAULT_CONTENT);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.backup.restore.enabled", false]],
  });
});

add_task(async function second_screen_filtered_by_targeting() {
  const sandbox = sinon.createSandbox();
  let browser = await openAboutWelcome(TEST_DEFAULT_JSON);

  await test_screen_content(
    browser,
    "multistage step 1",
    // Expected selectors:
    ["main.AW_STEP1"],
    // Unexpected selectors:
    ["main.AW_STEP2", "main.AW_STEP3"]
  );

  await onButtonClick(browser, "button.primary");

  await test_screen_content(
    browser,
    "multistage step 3",
    // Expected selectors:
    ["main.AW_STEP3"],
    // Unexpected selectors:
    ["main.AW_STEP2", "main.AW_STEP1"]
  );

  sandbox.restore();
  await popPrefs();
});

/**
 * Test MR template easy setup default content - Browser is not pinned
 * and not set as default
 */
add_task(async function test_aboutwelcome_mr_template_easy_setup_default() {
  const sandbox = sinon.createSandbox();
  await pushPrefs(
    ["browser.shell.checkDefaultBrowser", true],
    ["messaging-system-action.showEmbeddedImport", false]
  );
  sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
  sandbox.stub(ShellService, "isDefaultBrowser").returns(false);

  await clearHistoryAndBookmarks();

  const { browser, cleanup } = await openMRAboutWelcome();

  //should render easy setup with all checkboxes (default, pin, import)
  await test_screen_content(
    browser,
    "doesn't render only pin, default, or import easy setup",
    //Expected selectors:
    ["main.AW_EASY_SETUP_NEEDS_DEFAULT_AND_PIN"],
    //Unexpected selectors:
    [
      "main.AW_EASY_SETUP_NEEDS_DEFAULT",
      "main.AW_EASY_SETUP_NEEDS_PIN",
      "main.AW_ONLY_IMPORT",
    ]
  );

  await cleanup();
  await popPrefs();
  sandbox.restore();
});

/**
 * Test MR template easy setup content - Browser is not pinned
 * and set as default
 */
add_task(async function test_aboutwelcome_mr_template_easy_setup_needs_pin() {
  const sandbox = sinon.createSandbox();
  await pushPrefs(
    ["browser.shell.checkDefaultBrowser", true],
    ["messaging-system-action.showEmbeddedImport", false]
  );
  sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
  sandbox.stub(ShellService, "isDefaultBrowser").returns(true);

  await clearHistoryAndBookmarks();

  const { browser, cleanup } = await openMRAboutWelcome();

  //should render easy setup needs pin
  await test_screen_content(
    browser,
    "doesn't render default and pin, only default or import easy setup",
    //Expected selectors:
    ["main.AW_EASY_SETUP_NEEDS_PIN"],
    //Unexpected selectors:
    [
      "main.AW_EASY_SETUP_NEEDS_DEFAULT",
      "main.AW_EASY_SETUP_NEEDS_DEFAULT_AND_PIN",
      "main.AW_ONLY_IMPORT",
    ]
  );

  await cleanup();
  await popPrefs();
  sandbox.restore();
});

/**
 * Test MR template easy setup content - Browser is pinned and
 * not set as default
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_needs_default() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["messaging-system-action.showEmbeddedImport", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(false);
    sandbox.stub(ShellService, "doesAppNeedStartMenuPin").returns(false);
    sandbox.stub(ShellService, "isDefaultBrowser").returns(false);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    //should render easy setup needs default
    await test_screen_content(
      browser,
      "doesn't render pin, import and set to default",
      //Expected selectors:
      ["main.AW_EASY_SETUP_NEEDS_DEFAULT"],
      //Unexpected selectors:
      [
        "main.AW_EASY_SETUP_NEEDS_PIN",
        "main.AW_EASY_SETUP_NEEDS_DEFAULT_AND_PIN",
        "main.AW_ONLY_IMPORT",
      ]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is pinned and
 * set as default
 */
add_task(async function test_aboutwelcome_mr_template_easy_setup_only_import() {
  const sandbox = sinon.createSandbox();
  await pushPrefs(
    ["browser.shell.checkDefaultBrowser", true],
    ["messaging-system-action.showEmbeddedImport", false]
  );
  sandbox.stub(ShellService, "doesAppNeedPin").returns(false);
  sandbox.stub(ShellService, "doesAppNeedStartMenuPin").returns(false);
  sandbox.stub(ShellService, "isDefaultBrowser").returns(true);

  await clearHistoryAndBookmarks();

  const { browser, cleanup } = await openMRAboutWelcome();

  //should render easy setup - only import
  await test_screen_content(
    browser,
    "doesn't render any combination of pin and default",
    //Expected selectors:
    ["main.AW_EASY_SETUP_ONLY_IMPORT"],
    //Unexpected selectors:
    [
      "main.AW_EASY_SETUP_NEEDS_PIN",
      "main.AW_EASY_SETUP_NEEDS_DEFAULT_AND_PIN",
      "main.AW_EASY_SETUP_NEEDS_DEFAULT",
    ]
  );

  await cleanup();
  await popPrefs();
  sandbox.restore();
});

add_task(
  async function test_splash_screen_removed_when_experiments_gate_disabled() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.aboutwelcome.experimentsGate.enabled", false]],
    });

    const result = await ASRouterScreenUtils.evaluateTargetingAndRemoveScreens([
      makeSplashScreen(),
    ]);
    Assert.equal(
      result.length,
      0,
      "Splash screen removed when experimentsGate.enabled is false"
    );

    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_splash_screen_kept_when_experiments_gate_enabled() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.aboutwelcome.experimentsGate.enabled", true],
        ["browser.aboutwelcome.experimentsGate.skipSplashIfLoaded", false],
      ],
    });

    const result = await ASRouterScreenUtils.evaluateTargetingAndRemoveScreens([
      makeSplashScreen(),
    ]);
    Assert.equal(
      result.length,
      1,
      "Splash screen kept when experimentsGate.enabled is true"
    );

    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_splash_screen_removed_when_nimbus_already_loaded() {
    const sandbox = sinon.createSandbox();
    sandbox
      .stub(ASRouterTargeting.Environment, "experimentsLoaded")
      .get(() => true);

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.aboutwelcome.experimentsGate.enabled", true],
        ["browser.aboutwelcome.experimentsGate.skipSplashIfLoaded", true],
      ],
    });

    const result = await ASRouterScreenUtils.evaluateTargetingAndRemoveScreens([
      makeSplashScreen(),
    ]);
    Assert.equal(
      result.length,
      0,
      "Splash screen removed when skipSplashIfLoaded is true and Nimbus is already loaded"
    );

    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_splash_screen_kept_when_nimbus_not_yet_loaded() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ASRouterTargeting.Environment, "experimentsLoaded")
    .get(() => false);

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.aboutwelcome.experimentsGate.enabled", true],
      ["browser.aboutwelcome.experimentsGate.skipSplashIfLoaded", true],
    ],
  });

  const result = await ASRouterScreenUtils.evaluateTargetingAndRemoveScreens([
    makeSplashScreen(),
  ]);
  Assert.equal(
    result.length,
    1,
    "Splash screen kept when skipSplashIfLoaded is true but Nimbus has not loaded yet"
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});
