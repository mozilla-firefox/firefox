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

const { ClientEnvironmentBase } = ChromeUtils.importESModule(
  "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs"
);

// All `os.*` targeting attributes resolve to ClientEnvironmentBase.os,
// regardless of the machine actually running the test. We stub that getter's
// result with the whole `os` object used during targeting evaluation rather
// than relying on the test runner's real OS.
const OS_WITHOUT_PIN_PROMPT = { isWindows: false };
const OS_WITH_WIN_PIN_PROMPT = {
  isWindows: true,
  windowsBuildNumber: 22621,
  windowsUBR: 2400,
};
const OS_MAC = { isMac: true, isWindows: false };
const OS_WINDOWS_ONE_CLICK_DEFAULT = { isMac: false, isWindows: true };
const OS_LINUX = { isLinux: true, isMac: false, isWindows: false };

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
  await pushPrefs(["browser.shell.checkDefaultBrowser", true]);
  sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
  sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
  sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_WITHOUT_PIN_PROMPT);

  await clearHistoryAndBookmarks();

  const { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "renders easy setup with pin and default checkbox",
    //Expected selectors:
    ["main.AW_EASY_SETUP", "#checkbox-1", "#checkbox-2"]
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
  await pushPrefs(["browser.shell.checkDefaultBrowser", true]);
  sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
  sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(true);
  sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_WITHOUT_PIN_PROMPT);

  await clearHistoryAndBookmarks();

  const { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "renders easy setup with only pin checkbox",
    //Expected selectors:
    ["main.AW_EASY_SETUP", "#checkbox-1"],
    //Unexpected selectors:
    ["#checkbox-2"]
  );

  await cleanup();
  await popPrefs();
  sandbox.restore();
});

/**
 * Test MR template easy setup content - Browser is not pinned, not set as
 * default, and Windows will show its own OS-level pin consent prompt. The
 * pin checkbox should be suppressed (PIN_FIREFOX_TASKBAR_WIN_OS_PROMPT
 * silently pins instead) but the default-browser checkbox should still show
 * (one-click set default is enabled here so nothing auto-triggers for
 * default; this test is only exercising the pin-prompt half of targeting).
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_win_os_pin_prompt() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(true);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_WITH_WIN_PIN_PROMPT);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with only default checkbox when Windows will show its own pin prompt",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-2"],
      //Unexpected selectors:
      ["#checkbox-1"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is pinned and
 * not set as default
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_needs_default() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(["browser.shell.checkDefaultBrowser", true]);
    sandbox.stub(ShellService, "doesAppNeedPin").returns(false);
    sandbox.stub(ShellService, "doesAppNeedStartMenuPin").returns(false);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_WITHOUT_PIN_PROMPT);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with only set to default checkbox",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-2"],
      //Unexpected selectors:
      ["#checkbox-1"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is not pinned, not set as
 * default, and on macOS, where setting default silently triggers the OS's
 * own consent prompt (SET_DEFAULT_MAC_AND_WINDOWS_OS_PROMPT) in lieu of the
 * AW_EASY_SETUP default-browser checkbox. The pin checkbox should still show.
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_mac_auto_default() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(false);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_MAC);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with only pin checkbox on macOS",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-1"],
      //Unexpected selectors:
      ["#checkbox-2"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is not pinned, not set as
 * default, and on Windows where one-click set default is enabled. One-click set
 * default is a silent UserChoice registry write with no consent surface of its
 * own, so SET_DEFAULT_MAC_AND_WINDOWS_OS_PROMPT deliberately does not
 * auto-trigger in this case. The default-browser checkbox is the only consent
 * surface and should still show, alongside the pin checkbox.
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_windows_one_click_shows_checkbox() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(true);
    sandbox
      .stub(ClientEnvironmentBase, "os")
      .get(() => OS_WINDOWS_ONE_CLICK_DEFAULT);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with both checkboxes when Windows one-click set default is enabled",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-1", "#checkbox-2"],
      //Unexpected selectors:
      []
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is not pinned, not set as
 * default, and on Windows where one-click set default is NOT enabled, so
 * setting default falls back to Windows' own "Choose default apps" settings
 * UI (SET_DEFAULT_MAC_AND_WINDOWS_OS_PROMPT) in lieu of the AW_EASY_SETUP
 * default-browser checkbox. The pin checkbox should still show.
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_windows_no_one_click_auto_default() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(false);
    sandbox
      .stub(ClientEnvironmentBase, "os")
      .get(() => OS_WINDOWS_ONE_CLICK_DEFAULT);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with only pin checkbox when Windows one-click set default is not enabled",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-1"],
      //Unexpected selectors:
      ["#checkbox-2"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is pinned, not set as
 * default, and on macOS, where setting default is silently auto-handled
 * (SET_DEFAULT_MAC_AND_WINDOWS_OS_PROMPT). With pin not needed and the
 * default checkbox suppressed, the AW_EASY_SETUP screen has nothing left to
 * show and should not render at all.
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_hidden_when_default_auto_handled() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(false);
    sandbox.stub(ShellService, "doesAppNeedStartMenuPin").returns(false);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(false);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_MAC);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "does not render easy setup when default is auto-handled and pin isn't needed",
      //Expected selectors: falls through to the next screen instead
      ["main.AW_IMPORT_SETTINGS_EMBEDDED"],
      //Unexpected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-1", "#checkbox-2"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template easy setup content - Browser is not pinned, not set as
 * default, and on macOS, but browser.bypassAutoTriggerActions is set (as it
 * is by default on local/non-official builds), so the OS-level consent
 * prompt won't be silently triggered for either pin or default. Both
 * checkboxes should show, same as if the OS couldn't handle either prompt.
 */
add_task(
  async function test_aboutwelcome_mr_template_easy_setup_bypass_auto_trigger_actions() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", true]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(false);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(false);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_MAC);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders easy setup with both checkboxes when auto-triggered actions are bypassed",
      //Expected selectors:
      ["main.AW_EASY_SETUP", "#checkbox-1", "#checkbox-2"],
      //Unexpected selectors:
      []
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template restore-from-backup CTA - Browser is not pinned but is on
 * Windows, where the OS-level prompt handles pinning, and is already the
 * default browser, so AW_EASY_SETUP has no checkbox left to offer and is
 * skipped. The restore-from-backup CTA it normally includes should fall
 * through to AW_IMPORT_SETTINGS_EMBEDDED so it stays reachable in the flow.
 */
add_task(
  async function test_aboutwelcome_mr_template_restore_cta_when_easy_setup_skipped() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.bypassAutoTriggerActions", false],
      ["browser.backup.restore.enabled", true]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(true);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(true);
    sandbox.stub(ShellService, "isOneClickSetDefaultEnabled").returns(true);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_WITH_WIN_PIN_PROMPT);
    sandbox
      .stub(ASRouterTargeting.Environment, "backupRestoreEnabled")
      .get(() => true);
    // Keep the AW_BACKUP_RESTORE_EMBEDDED_* screens out of the flow
    sandbox
      .stub(ASRouterTargeting.Environment, "backupsInfo")
      .get(() => Promise.resolve({ found: false }));

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "renders the restore from backup CTA on the import screen when easy setup is skipped",
      //Expected selectors:
      [
        "main.AW_IMPORT_SETTINGS_EMBEDDED",
        "button[data-l10n-id='restore-from-backup-secondary-top-button']",
      ],
      //Unexpected selectors:
      ["main.AW_EASY_SETUP"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

/**
 * Test MR template import screen on Linux - the embedded migration wizard is
 * enabled, but the import screen is skipped there. Browser is pinned and
 * already the default, so AW_EASY_SETUP is skipped too and the flow should
 * start on a later screen.
 */
add_task(
  async function test_aboutwelcome_mr_template_import_skipped_on_linux() {
    const sandbox = sinon.createSandbox();
    await pushPrefs(
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.migrate.content-modal.about-welcome-behavior", "embedded"]
    );
    sandbox.stub(ShellService, "doesAppNeedPin").returns(false);
    sandbox.stub(ShellService, "doesAppNeedStartMenuPin").returns(false);
    sandbox.stub(ShellService, "isDefaultBrowserAsync").resolves(true);
    sandbox.stub(ClientEnvironmentBase, "os").get(() => OS_LINUX);

    await clearHistoryAndBookmarks();

    const { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "does not render the import screen on Linux",
      //Expected selectors:
      ["main.screen"],
      //Unexpected selectors:
      ["main.AW_EASY_SETUP", "main.AW_IMPORT_SETTINGS_EMBEDDED"]
    );

    await cleanup();
    await popPrefs();
    sandbox.restore();
  }
);

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
