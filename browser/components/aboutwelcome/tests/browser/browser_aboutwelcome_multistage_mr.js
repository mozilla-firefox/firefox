"use strict";

const { AboutWelcomeParent } = ChromeUtils.importESModule(
  "resource:///actors/AboutWelcomeParent.sys.mjs"
);

const { AboutWelcomeTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeTelemetry.sys.mjs"
);
const { ASRouterScreenUtils } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterScreenUtils.sys.mjs"
);
const { InternalTestingProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/InternalTestingProfileMigrator.sys.mjs"
);
const {
  EASY_SETUP_TARGETING,
  PIN_CHECKBOX_TARGETING,
  SET_DEFAULT_CHECKBOX_TARGETING,
} = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeDefaults.sys.mjs"
);

async function clickVisibleButton(browser, selector) {
  // eslint-disable-next-line no-shadow
  await SpecialPowers.spawn(browser, [{ selector }], async ({ selector }) => {
    function getVisibleElement() {
      for (const el of content.document.querySelectorAll(selector)) {
        if (el.offsetParent !== null) {
          return el;
        }
      }
      return null;
    }
    await ContentTaskUtils.waitForCondition(
      getVisibleElement,
      selector,
      200, // interval
      100 // maxTries
    );
    getVisibleElement().click();
  });
}

add_setup(async function () {
  SpecialPowers.pushPrefEnv({
    set: [
      ["ui.prefersReducedMotion", 1],
      ["browser.aboutwelcome.transitions", false],
      ["browser.shell.checkDefaultBrowser", true],
      ["browser.aboutwelcome.didSeeFinalScreen", false],
    ],
  });
});

add_task(function () {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(
      "messaging-system-action.showRestoreFromBackup"
    );
  });
});

/**
 * Test MR message telemetry
 */
add_task(async function test_aboutwelcome_mr_template_telemetry() {
  const sandbox = sinon.createSandbox();
  let { browser, cleanup } = await openMRAboutWelcome();
  let aboutWelcomeActor = await getAboutWelcomeParent(browser);
  // Stub AboutWelcomeParent's Content Message Handler
  const messageStub = sandbox.spy(aboutWelcomeActor, "onContentMessage");
  await clickVisibleButton(browser, ".action-buttons button.secondary");

  const { callCount } = messageStub;
  Assert.greaterOrEqual(callCount, 1, `${callCount} Stub was called`);
  let clickCall;
  for (let i = 0; i < callCount; i++) {
    const call = messageStub.getCall(i);
    info(`Call #${i}: ${call.args[0]} ${JSON.stringify(call.args[1])}`);
    if (call.calledWithMatch("", { event: "CLICK_BUTTON" })) {
      clickCall = call;
    }
  }

  Assert.ok(
    clickCall.args[1].message_id.startsWith("MR_WELCOME_DEFAULT"),
    "Telemetry includes MR message id"
  );

  await cleanup();
  sandbox.restore();
});

/**
 * Telemetry Impression with Easy Setup Need Default and Pin as First Screen
 */
add_task(async function test_aboutwelcome_easy_setup_screen_impression() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    .withArgs(EASY_SETUP_TARGETING)
    .resolves(true)
    .withArgs(PIN_CHECKBOX_TARGETING)
    .resolves(true)
    .withArgs(SET_DEFAULT_CHECKBOX_TARGETING)
    .resolves(true)
    .withArgs("isDeviceMigration")
    .resolves(false)
    .withArgs("!isFxASignedIn")
    .resolves(true)
    .withArgs("backupRestoreEnabled")
    .resolves(true);

  let impressionSpy = sandbox.spy(
    AboutWelcomeTelemetry.prototype,
    "sendTelemetry"
  );

  let { browser, cleanup } = await openMRAboutWelcome();
  // Wait for screen elements to render before checking impression pings
  await test_screen_content(
    browser,
    "Onboarding screen elements rendered",
    // Expected selectors:
    [
      `main.screen[pos="split"]`,
      "div.secondary-cta.top",
      "button[value='secondary_button_top_0']", //backup restore button
      "button[value='secondary_button_top_1']", //sign in button
    ]
  );

  const { callCount } = impressionSpy;
  Assert.greaterOrEqual(callCount, 1, `${callCount} impressionSpy was called`);
  let impressionCall;
  for (let i = 0; i < callCount; i++) {
    const call = impressionSpy.getCall(i);
    info(`Call #${i}:  ${JSON.stringify(call.args[0])}`);
    if (
      call.calledWithMatch({ event: "IMPRESSION" }) &&
      !call.calledWithMatch({ message_id: "MR_WELCOME_DEFAULT" })
    ) {
      info(`Screen Impression Call #${i}:  ${JSON.stringify(call.args[0])}`);
      impressionCall = call;
    }
  }

  Assert.ok(
    impressionCall.args[0].message_id.startsWith(
      "MR_WELCOME_DEFAULT_0_AW_EASY_SETUP"
    ),
    "Impression telemetry includes correct message id"
  );
  await cleanup();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_gratitude() {
  const TEST_CONTENT = [
    {
      id: "AW_GRATITUDE",
      content: {
        position: "split",
        split_narrow_bkg_position: "-228px",
        background:
          "url('chrome://activity-stream/content/data/content/assets/br-gratitude-fox-rock.svg') var(--mr-secondary-position) no-repeat, var(--mr-screen-background-color)",
        progress_bar: true,
        logo: {},
        title: {
          string_id: "mr2022-onboarding-gratitude-title",
        },
        subtitle: {
          string_id: "mr2022-onboarding-gratitude-subtitle",
        },
        primary_button: {
          label: {
            string_id: "mr2022-onboarding-gratitude-primary-button-label",
          },
          action: {
            navigate: true,
          },
        },
      },
    },
  ];
  await setAboutWelcomeMultiStage(JSON.stringify(TEST_CONTENT)); // NB: calls SpecialPowers.pushPrefEnv
  let { cleanup, browser } = await openMRAboutWelcome();

  // execution
  await test_screen_content(
    browser,
    "doesn't render secondary button on gratitude screen",
    //Expected selectors
    ["main.AW_GRATITUDE", "button[value='primary_button']"],

    //Unexpected selectors:
    ["button[value='secondary_button']"]
  );
  await clickVisibleButton(browser, ".action-buttons button.primary");

  // make sure the button navigates to newtab
  await BrowserTestUtils.browserLoaded(browser, false, "about:home");
  await test_screen_content(
    browser,
    "home",
    //Expected selectors
    ["body.activity-stream"],

    //Unexpected selectors:
    ["main.AW_GRATITUDE"]
  );

  // cleanup
  await SpecialPowers.popPrefEnv(); // for setAboutWelcomeMultiStage
  await cleanup();
});

add_task(async function test_aboutwelcome_embedded_migration() {
  // Let's make sure at least one migrator is available and enabled - the
  // InternalTestingProfileMigrator.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.migrate.internal-testing.enabled", true]],
  });

  const sandbox = sinon.createSandbox();
  sandbox
    .stub(InternalTestingProfileMigrator.prototype, "getResources")
    .callsFake(() =>
      Promise.resolve([
        {
          type: MigrationUtils.resourceTypes.BOOKMARKS,
          migrate: () => {},
        },
      ])
    );
  sandbox.stub(MigrationUtils, "_importQuantities").value({
    bookmarks: 123,
    history: 123,
    logins: 123,
  });
  const migrated = new Promise(resolve => {
    sandbox
      .stub(InternalTestingProfileMigrator.prototype, "migrate")
      .callsFake((aResourceTypes, aStartup, aProfile, aProgressCallback) => {
        aProgressCallback(MigrationUtils.resourceTypes.BOOKMARKS);
        Services.obs.notifyObservers(null, "Migration:Ended");
        resolve();
      });
  });

  let telemetrySpy = sandbox.spy(
    AboutWelcomeTelemetry.prototype,
    "sendTelemetry"
  );

  const TEST_CONTENT = [
    {
      id: "AW_IMPORT_SETTINGS_EMBEDDED",
      content: {
        tiles: { type: "migration-wizard" },
        position: "split",
        split_narrow_bkg_position: "-42px",
        image_alt_text: {
          string_id: "mr2022-onboarding-import-image-alt",
        },
        background:
          "url('chrome://activity-stream/content/data/content/assets/br-migration-fox-juggle.svg') var(--mr-secondary-position) no-repeat var(--mr-screen-background-color)",
        progress_bar: true,
        migrate_start: {
          action: {},
        },
        migrate_close: {
          action: { navigate: true },
        },
        secondary_button: {
          label: {
            string_id: "mr2022-onboarding-secondary-skip-button-label",
          },
          action: {
            navigate: true,
          },
          has_arrow_icon: true,
        },
      },
    },
    {
      id: "AW_STEP2",
      content: {
        position: "split",
        split_narrow_bkg_position: "-228px",
        background:
          "url('chrome://activity-stream/content/data/content/assets/br-gratitude-fox-rock.svg') var(--mr-secondary-position) no-repeat, var(--mr-screen-background-color)",
        progress_bar: true,
        logo: {},
        title: {
          string_id: "mr2022-onboarding-gratitude-title",
        },
        subtitle: {
          string_id: "mr2022-onboarding-gratitude-subtitle",
        },
        primary_button: {
          label: {
            string_id: "mr2022-onboarding-gratitude-primary-button-label",
          },
          action: {
            navigate: true,
          },
        },
      },
    },
  ];

  await setAboutWelcomeMultiStage(JSON.stringify(TEST_CONTENT)); // NB: calls SpecialPowers.pushPrefEnv
  let { cleanup, browser } = await openMRAboutWelcome();

  // execution
  await test_screen_content(
    browser,
    "Renders a <migration-wizard> custom element",
    // We expect <migration-wizard> to automatically request the set of migrators
    // upon binding to the DOM, and to not be in dialog mode.
    [
      "main.AW_IMPORT_SETTINGS_EMBEDDED",
      "migration-wizard[auto-request-state]:not([dialog-mode])",
    ]
  );

  // Do a basic test to make sure that the <migration-wizard> is on the right
  // page and the <panel-list> can open.
  await SpecialPowers.spawn(
    browser,
    [`panel-item[key="${InternalTestingProfileMigrator.key}"]`],
    async menuitemSelector => {
      const { MigrationWizardConstants } = ChromeUtils.importESModule(
        "chrome://browser/content/migration/migration-wizard-constants.mjs"
      );

      let wizard = content.document.querySelector("migration-wizard");
      await new Promise(resolve => content.requestAnimationFrame(resolve));
      let shadow = wizard.openOrClosedShadowRoot;
      let deck = shadow.querySelector("#wizard-deck");

      // It's unlikely but possible that the deck might not yet be showing the
      // selection page yet, in which case we wait for that page to appear.
      if (deck.selectedViewName !== MigrationWizardConstants.PAGES.SELECTION) {
        await ContentTaskUtils.waitForMutationCondition(
          deck,
          { attributeFilter: ["selected-view"] },
          () => {
            return (
              deck.getAttribute("selected-view") ===
              `page-${MigrationWizardConstants.PAGES.SELECTION}`
            );
          }
        );
      }

      Assert.ok(true, "Selection page is being shown in the migration wizard.");

      // Now let's make sure that the <panel-list> can appear.
      let panelList = shadow.querySelector("panel-list");
      Assert.ok(panelList, "Found the <panel-list>.");

      // The "shown" event from the panel-list is coming from a lower level
      // of privilege than where we're executing this SpecialPowers.spawn
      // task. In order to properly listen for it, we have to ask
      // ContentTaskUtils.waitForEvent to listen for untrusted events.
      let shown = ContentTaskUtils.waitForEvent(
        panelList,
        "shown",
        false /* capture */,
        null /* checkFn */,
        true /* wantsUntrusted */
      );
      let selector = shadow.querySelector("#browser-profile-selector");

      // The migration wizard programmatically focuses the selector after
      // the selection page is shown using an rAF. If we click the button
      // before that occurs, then the focus can shift after the panel opens
      // which will cause it to immediately close again. So we wait for the
      // selection button to gain focus before continuing.
      if (!selector.matches(":focus")) {
        await ContentTaskUtils.waitForEvent(selector, "focus");
      }

      EventUtils.synthesizeMouseAtCenter(selector, {}, wizard.documentGlobal);
      await shown;

      let panelRect = panelList.getBoundingClientRect();
      let selectorRect = selector.getBoundingClientRect();

      // Recalculate the <panel-list> rect top value relative to the top-left
      // of the selectorRect. We expect the <panel-list> to be tightly anchored
      // to the bottom of the <button>, so we expect this new value to be close to 0,
      // to account for subpixel rounding
      let panelTopLeftRelativeToAnchorTopLeft =
        panelRect.top - selectorRect.top - selectorRect.height;

      function isfuzzy(actual, expected, epsilon, msg) {
        if (actual >= expected - epsilon && actual <= expected + epsilon) {
          ok(true, msg);
        } else {
          is(actual, expected, msg);
        }
      }

      isfuzzy(
        panelTopLeftRelativeToAnchorTopLeft,
        0,
        1,
        "Panel should be tightly anchored to the bottom of the button shadow node."
      );

      let panelItem = shadow.querySelector(menuitemSelector);
      panelItem.click();

      let importButton = shadow.querySelector("#import");
      importButton.click();
    }
  );

  await migrated;
  Assert.ok(
    telemetrySpy.calledWithMatch({
      event: "CLICK_BUTTON",
      event_context: { source: "primary_button", page: "about:welcome" },
      message_id: sinon.match.string,
    }),
    "Should have sent telemetry for clicking the 'Import' button."
  );

  await SpecialPowers.spawn(browser, [], async () => {
    let wizard = content.document.querySelector("migration-wizard");
    let shadow = wizard.openOrClosedShadowRoot;
    let continueButton = shadow.querySelector(
      "div[name='page-progress'] .continue-button"
    );
    continueButton.click();
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("main.AW_STEP2"),
      "Waiting for step 2 to render"
    );
  });

  Assert.ok(
    telemetrySpy.calledWithMatch({
      event: "CLICK_BUTTON",
      event_context: { source: "migrate_close", page: "about:welcome" },
      message_id: sinon.match.string,
    }),
    "Should have sent telemetry for clicking the 'Continue' button."
  );

  // Ensure that we can go back and get the migration wizard to appear
  // again.
  await SpecialPowers.spawn(browser, [], async () => {
    const { MigrationWizardConstants } = ChromeUtils.importESModule(
      "chrome://browser/content/migration/migration-wizard-constants.mjs"
    );

    let migrationWizardReady = ContentTaskUtils.waitForEvent(
      content,
      "MigrationWizard:Ready"
    );

    // Waiting for the history length to update seems to allow us to avoid
    // an intermittent test failure.
    await ContentTaskUtils.waitForCondition(() => {
      return content.history.length === 2;
    });

    content.history.back();
    await migrationWizardReady;

    let wizard = content.document.querySelector("migration-wizard");
    let shadow = wizard.openOrClosedShadowRoot;
    let deck = shadow.querySelector("#wizard-deck");

    Assert.equal(
      deck.getAttribute("selected-view"),
      `page-${MigrationWizardConstants.PAGES.SELECTION}`
    );
  });

  // cleanup
  await SpecialPowers.popPrefEnv(); // for the InternalTestingProfileMigrator.
  await SpecialPowers.popPrefEnv(); // for setAboutWelcomeMultiStage
  await cleanup();
  sandbox.restore();
  let migrator = await MigrationUtils.getMigrator(
    InternalTestingProfileMigrator.key
  );
  migrator.flushResourceCache();
});

add_task(async function test_aboutwelcome_multiselect() {
  const TEST_SCREENS = [
    {
      id: "AW_EASY_SETUP_X",
      content: {
        position: "split",
        split_narrow_bkg_position: "-60px",
        progress_bar: true,
        logo: {},
        title: { string_id: "mr2022-onboarding-set-default-title" },
        tiles: {
          type: "multiselect",
          style: { flexDirection: "column", alignItems: "flex-start" },
          data: [
            {
              id: "radio-1",
              type: "radio",
              group: "radios",
              defaultValue: true,
              label: {
                raw: "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
              },
              action: { type: "OPEN_PROTECTION_REPORT" },
            },
            {
              id: "radio-2",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: {
                raw: "Nulla facilisi nullam vehicula ipsum a arcu cursus vitae.",
              },
              action: { type: "OPEN_FIREFOX_VIEW" },
            },
            {
              id: "radio-3",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: { raw: "Natoque penatibus et magnis dis." },
              action: { type: "OPEN_PRIVATE_BROWSER_WINDOW" },
            },
          ],
        },
        secondary_button: {
          label: {
            string_id: "mr2022-onboarding-secondary-skip-button-label",
          },
          action: { navigate: true },
          has_arrow_icon: true,
        },
      },
    },
    {
      id: "AW_EASY_SETUP_Y",
      content: {
        position: "split",
        split_narrow_bkg_position: "-60px",
        progress_bar: true,
        logo: {},
        title: { string_id: "mr2022-onboarding-set-default-title" },
        tiles: {
          type: "multiselect",
          style: { flexDirection: "row", gap: "24px" },
          data: [
            {
              id: "checkbox-1",
              defaultValue: true,
              label: { raw: "Test1" },
              action: { type: "OPEN_PROTECTION_REPORT" },
            },
            {
              id: "checkbox-2",
              defaultValue: true,
              label: { raw: "Test2" },
              action: { type: "OPEN_FIREFOX_VIEW" },
            },
            {
              id: "radio-1",
              type: "radio",
              group: "radios",
              defaultValue: true,
              label: { raw: "Test3" },
              action: { type: "OPEN_PROTECTION_REPORT" },
            },
            {
              id: "radio-2",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: { raw: "Test4" },
              action: { type: "OPEN_FIREFOX_VIEW" },
            },
            {
              id: "radio-3",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: { raw: "Test5" },
              action: { type: "OPEN_PRIVATE_BROWSER_WINDOW" },
            },
          ],
        },
        secondary_button: {
          label: {
            string_id: "mr2022-onboarding-secondary-skip-button-label",
          },
          action: { navigate: true },
          has_arrow_icon: true,
        },
      },
    },
    {
      id: "AW_EASY_SETUP_Z",
      content: {
        position: "split",
        split_narrow_bkg_position: "-60px",
        progress_bar: true,
        logo: {},
        title: { string_id: "mr2022-onboarding-set-default-title" },
        tiles: {
          type: "multiselect",
          style: {
            flexDirection: "column",
            flexShrink: 1,
            justifyContent: "flex-start",
          },
          data: [
            {
              id: "checkbox-1",
              defaultValue: true,
              label: { raw: "Test1" },
              action: { type: "OPEN_PROTECTION_REPORT" },
            },
            {
              id: "checkbox-2",
              defaultValue: true,
              label: { raw: "Test2" },
              action: { type: "OPEN_FIREFOX_VIEW" },
            },
            {
              id: "radio-1",
              type: "radio",
              group: "radios",
              defaultValue: true,
              label: { raw: "Test3" },
              action: { type: "OPEN_PROTECTION_REPORT" },
            },
            {
              id: "radio-2",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: { raw: "Test4" },
              action: { type: "OPEN_FIREFOX_VIEW" },
            },
            {
              id: "radio-3",
              type: "radio",
              group: "radios",
              defaultValue: false,
              label: { raw: "Test5" },
              action: { type: "OPEN_PRIVATE_BROWSER_WINDOW" },
            },
          ],
        },
        secondary_button: {
          label: {
            string_id: "mr2022-onboarding-secondary-skip-button-label",
          },
          action: { navigate: true },
          has_arrow_icon: true,
        },
      },
    },
  ];

  const sandbox = sinon.createSandbox();
  sandbox.stub(ASRouterScreenUtils, "addScreenImpression").resolves();

  await setAboutWelcomeMultiStage(JSON.stringify(TEST_SCREENS));
  let { cleanup, browser } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "renders default screen",
    ["main.AW_EASY_SETUP_X", "#radio-1:checked"],
    ["#radio-2:checked", "#radio-3:checked"]
  );

  await clickVisibleButton(browser, "#radio-3");

  await test_screen_content(
    browser,
    "renders radio button selection",
    ["main.AW_EASY_SETUP_X", "#radio-3:checked"],
    ["#radio-1:checked", "#radio-2:checked"]
  );

  await test_element_styles(
    browser,
    ".multi-select-container",
    { flexDirection: "column", alignItems: "flex-start" },
    {}
  );

  await clickVisibleButton(browser, ".action-buttons button.secondary");

  await test_screen_content(
    browser,
    "renders screen 2",
    [
      "main.AW_EASY_SETUP_Y",
      "#checkbox-1:checked",
      "#checkbox-2:checked",
      "#radio-1:checked",
    ],
    ["#radio-2:checked", "#radio-3:checked"]
  );

  await clickVisibleButton(browser, "#checkbox-1");
  await clickVisibleButton(browser, "#checkbox-2");
  await clickVisibleButton(browser, "#radio-2");

  await test_screen_content(
    browser,
    "renders checkbox and radio button selection",
    ["main.AW_EASY_SETUP_Y", "#radio-2:checked"],
    ["#checkbox-1:checked", "#checkbox-2:checked", "#radio-1:checked"]
  );

  await test_element_styles(
    browser,
    ".multi-select-container",
    { flexDirection: "row", gap: "24px" },
    {}
  );

  browser.goBack();

  await test_screen_content(
    browser,
    "renders screen 1 and remembers selection",
    ["main.AW_EASY_SETUP_X", "#radio-3:checked"],
    ["#radio-1:checked", "#radio-2:checked"]
  );

  browser.goForward();

  await test_screen_content(
    browser,
    "renders screen 2 and remembers selection",
    ["main.AW_EASY_SETUP_Y", "#radio-2:checked"],
    ["#checkbox-1:checked", "#checkbox-2:checked", "#radio-1:checked"]
  );

  await clickVisibleButton(browser, ".action-buttons button.secondary");

  await test_screen_content(
    browser,
    "renders screen 3",
    [
      "main.AW_EASY_SETUP_Z",
      "#checkbox-1:checked",
      "#checkbox-2:checked",
      "#radio-1:checked",
    ],
    ["#radio-2:checked", "#radio-3:checked"]
  );

  await clickVisibleButton(browser, "#radio-3");

  await test_screen_content(
    browser,
    "renders radio button selection without removing checkbox selection",
    [
      "main.AW_EASY_SETUP_Z",
      "#checkbox-1:checked",
      "#checkbox-2:checked",
      "#radio-3:checked",
    ],
    ["#radio-1:checked", "#radio-2:checked"]
  );

  await test_element_styles(
    browser,
    ".multi-select-container",
    { flexDirection: "column", flexShrink: 1, justifyContent: "flex-start" },
    {}
  );

  await SpecialPowers.popPrefEnv();
  await cleanup();

  sandbox.restore();
});

/**
 * Test end of multistage url bar focus on new tab
 */
add_task(async function test_aboutwelcome_newtab_urlbar_focus() {
  const TEST_CONTENT = [
    {
      id: "TEST_SCREEN",
      content: {
        position: "split",
        logo: {},
        title: "Test newtab url focus",
        primary_button: {
          label: "Next",
          action: {
            navigate: true,
          },
        },
      },
    },
  ];

  const TEST_CONTENT_JSON = JSON.stringify(TEST_CONTENT);

  await setAboutWelcomePref(true);
  await setAboutWelcomeMultiStage(TEST_CONTENT_JSON);
  const sandbox = sinon.createSandbox();

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:welcome",
    true
  );
  const browser = tab.linkedBrowser;
  let focused = BrowserTestUtils.waitForEvent(gURLBar.inputField, "focus");
  await onButtonClick(browser, "button.primary");
  await focused;
  Assert.ok(gURLBar.focused, "focus should be on url bar");

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_gratitude() {
  const TEST_CONTENT = [
    {
      id: "AW_ACCOUNT_LOGIN",
      content: {
        fullscreen: true,
        position: "split",
        split_narrow_bkg_position: "-228px",
        background:
          "url('chrome://activity-stream/content/data/content/assets/br-fxa-fox-mirror.svg') var(--mr-secondary-position) no-repeat light-dark(rgba(252, 245, 240, 1), rgba(33, 3, 64, 1))",
        progress_bar: true,
        logo: {},
        title: {
          string_id: "onboarding-sign-up-title",
        },
        subtitle: {
          string_id: "onboarding-sign-up-description",
        },
        secondary_button: {
          label: {
            string_id: "mr2-onboarding-start-browsing-button-label",
          },
          style: "secondary",
          action: {
            navigate: true,
          },
        },
        primary_button: {
          label: {
            string_id: "onboarding-sign-up-button",
          },
          action: {
            navigate: true,
          },
        },
      },
    },
  ];
  await setAboutWelcomeMultiStage(JSON.stringify(TEST_CONTENT));
  let { cleanup, browser } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "renders action buttons",
    //Expected selectors
    [
      "main.AW_ACCOUNT_LOGIN",
      "button[value='primary_button']",
      "button[value='secondary_button']",
    ],
    //Unexpected selectors:
    []
  );

  // make sure the secondary button navigates to newtab
  await clickVisibleButton(browser, ".action-buttons button.secondary");

  await BrowserTestUtils.browserLoaded(browser, false, "about:home");
  await test_screen_content(
    browser,
    "home",
    //Expected selectors
    ["body.activity-stream"],
    //Unexpected selectors:
    ["main.AW_ACCOUNT_LOGIN"]
  );

  // cleanup
  await SpecialPowers.popPrefEnv();
  await cleanup();
});

add_task(async function test_aboutwelcome_backup_found() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    .withArgs(
      "backupRestoreEnabled && !hasSelectableProfiles && (backupsInfo.found && !backupsInfo.multipleBackupsFound)"
    )
    .resolves(true)
    .withArgs("isDeviceMigration")
    .resolves(false);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Should render backups found screen as first screen when backupRestoreEnabled is true and backup is found",
    [
      "main.AW_BACKUP_RESTORE_EMBEDDED_BACKUP_FOUND",
      "[data-l10n-id='restore-from-backup-title']",
      "[data-l10n-id='restore-from-backup-subtitle']",
    ],
    // Unexpected selectors
    [
      "main.AW_BACKUP_RESTORE_EMBEDDED_MULTIPLE_BACKUPS_FOUND",
      "main.AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND",
    ]
  );

  await cleanup();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_multiple_backups_found() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    .withArgs(
      "backupRestoreEnabled && !hasSelectableProfiles && backupsInfo.multipleBackupsFound"
    )
    .resolves(true)
    .withArgs("isDeviceMigration")
    .resolves(false);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Should render multiple backups found screen as first screen when backupRestoreEnabled is true and multiple backups are found",
    [
      "main.AW_BACKUP_RESTORE_EMBEDDED_MULTIPLE_BACKUPS_FOUND",
      "[data-l10n-id='restore-from-backup-title']",
      "[data-l10n-id='restore-from-backup-subtitle']",
      "[data-l10n-id='multiple-backups-info-tile']",
    ],
    // Unexpected selectors
    [
      "main.AW_BACKUP_RESTORE_EMBEDDED_BACKUP_FOUND",
      "main.AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND",
    ]
  );

  await cleanup();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_no_backups() {
  const sandbox = sinon.createSandbox();

  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    .withArgs(
      "backupRestoreEnabled && (backupsInfo.found || backupsInfo.multipleBackupsFound)"
    )
    .resolves(false)
    // Easy setup for secondary top button
    .withArgs(EASY_SETUP_TARGETING)
    .resolves(true)
    // Restore from backup pref gating
    .withArgs("backupRestoreEnabled")
    .resolves(true)
    // No backups found
    .withArgs(
      "backupRestoreEnabled && 'messaging-system-action.showRestoreFromBackup' |preferenceValue == true"
    )
    .resolves(true);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Easy setup renders with restore secondary top button",
    ["main.AW_EASY_SETUP", "div.secondary-cta.top"],
    //Unexpected selectors:
    ["main.AW_BACKUP_RESTORE_EMBEDDED_BACKUP_FOUND"]
  );

  await clickVisibleButton(
    browser,
    "button[data-l10n-id='restore-from-backup-secondary-top-button']"
  );

  await test_screen_content(
    browser,
    "NO_BACKUP_FOUND screen renders afrer clicking restore from backup top cta",
    [
      "main.AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND",
      "[data-l10n-id='restore-from-backup-title']",
      "[data-l10n-id='restore-from-backup-subtitle']",
    ]
  );

  await cleanup();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_secondary_top_signin_only() {
  const sandbox = sinon.createSandbox();

  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    // Mock Easy Setup for secondary button top testing
    .withArgs(EASY_SETUP_TARGETING)
    .resolves(true)
    // Sign in button targeting
    .withArgs("!isFxASignedIn")
    .resolves(true);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Easy setup renders with secondary top button",
    [
      "main.AW_EASY_SETUP",
      ".secondary-buttons-top-container, div.secondary-cta.top",
    ]
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const count = content.document.querySelectorAll(
      "button[value^='secondary_button_top_']"
    ).length;
    Assert.equal(count, 1, "One top CTA is shown");
    Assert.ok(
      content.document.querySelector(
        "button[data-l10n-id='mr1-onboarding-sign-in-button-label']"
      ),
      "'Sign in' top button is present"
    );
    Assert.ok(
      !content.document.querySelector(
        "button[data-l10n-id='restore-from-backup-secondary-top-button']"
      ),
      "'Restore Backup' top button not present"
    );
  });

  await cleanup();
  sandbox.restore();
});

add_task(async function test_aboutwelcome_secondary_top_backup_restore_only() {
  const sandbox = sinon.createSandbox();

  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    .resolves(false)
    // Mock Easy Setup for secondary button top testing
    .withArgs(EASY_SETUP_TARGETING)
    .resolves(true)
    // Show Restore Backup top button
    .withArgs("backupRestoreEnabled")
    .resolves(true);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Easy setup renders with secondary top button",
    [
      "main.AW_EASY_SETUP",
      ".secondary-buttons-top-container, div.secondary-cta.top",
    ]
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const count = content.document.querySelectorAll(
      "button[value^='secondary_button_top_']"
    ).length;
    Assert.equal(count, 1, "One top CTA is shown");
    Assert.ok(
      !content.document.querySelector(
        "button[data-l10n-id='mr1-onboarding-sign-in-button-label']"
      ),
      "'Sign in' top button is present"
    );
    Assert.ok(
      content.document.querySelector(
        "button[data-l10n-id='restore-from-backup-secondary-top-button']"
      ),
      "'Restore Backup' top button present"
    );
  });

  await cleanup();
  sandbox.restore();
});

add_task(
  async function test_aboutwelcome_import_embedded_backup_restore_button() {
    const sandbox = sinon.createSandbox();

    sandbox
      .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
      .resolves(false)
      .withArgs("useEmbeddedMigrationWizard && !os.isLinux")
      .resolves(true)
      .withArgs(`backupRestoreEnabled && !(${EASY_SETUP_TARGETING})`)
      .resolves(true);

    let { browser, cleanup } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "Import embedded screen renders with backup restore top button",
      [
        "main.AW_IMPORT_SETTINGS_EMBEDDED",
        "button[data-l10n-id='restore-from-backup-secondary-top-button']",
      ]
    );

    await cleanup();
    sandbox.restore();
  }
);

add_task(async function test_aboutwelcome_both_secondary_top_buttons() {
  const sandbox = sinon.createSandbox();

  sandbox
    .stub(ASRouterScreenUtils, "evaluateScreenTargeting")
    // Mock Easy Setup for secondary button top testing
    .withArgs(EASY_SETUP_TARGETING)
    .resolves(true)
    // Show Sign-in top button
    .withArgs("!isFxASignedIn")
    .resolves(true)
    // Show Restore top button
    .withArgs("backupRestoreEnabled")
    .resolves(true);

  let { browser, cleanup } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "Easy setup renders with secondary top button",
    [
      "main.AW_EASY_SETUP",
      ".secondary-buttons-top-container, div.secondary-cta.top",
    ]
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const buttons = content.document.querySelectorAll(
      "button[value^='secondary_button_top_']"
    );
    Assert.equal(buttons.length, 2, "Two secondary top buttons are present");
    Assert.ok(
      content.document.querySelector(
        "button[data-l10n-id='mr1-onboarding-sign-in-button-label']"
      ),
      "'Sign in' top button present"
    );
    Assert.ok(
      content.document.querySelector(
        "button[data-l10n-id='restore-from-backup-secondary-top-button']"
      ),
      "'Restore Backup' top button present"
    );
  });

  await cleanup();
  sandbox.restore();
});

add_task(
  async function test_aboutwelcome_no_backup_skip_returns_to_easy_setup() {
    const TEST_SCREENS = [
      {
        id: "AW_EASY_SETUP_TEST",
        targeting: "true",
        content: {
          position: "split",
          progress_bar: true,
          logo: {},
          title: "Easy setup test",
          secondary_button_top: [
            {
              label: { string_id: "restore-from-backup-secondary-top-button" },
              action: {
                type: "SET_PREF",
                data: { pref: { name: "showRestoreFromBackup", value: true } },
                navigate: true,
              },
              targeting: "true",
            },
          ],
          primary_button: {
            label: { raw: "Continue" },
            action: { navigate: true },
          },
        },
      },
      {
        id: "AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND_TEST",
        targeting:
          "'messaging-system-action.showRestoreFromBackup'|preferenceValue == true",
        content: {
          position: "split",
          progress_bar: true,
          logo: {},
          title: { string_id: "restore-from-backup-title" },
          subtitle: { string_id: "restore-from-backup-subtitle" },
          tiles: { type: "backup_restore" },
          skip_button: {
            label: { string_id: "restore-from-backup-secondary-button" },
            has_arrow_icon: true,
            action: {
              type: "SET_PREF",
              data: { pref: { name: "showRestoreFromBackup", value: false } },
              navigate: true,
              goBack: true,
            },
          },
        },
      },
      {
        id: "AW_TEST_FOLLOWUP",
        content: {
          position: "split",
          progress_bar: true,
          logo: {},
          title: "Test backup skipped",
          primary_button: {
            label: { raw: "Done" },
            action: { navigate: true },
          },
        },
      },
    ];

    await setAboutWelcomeMultiStage(JSON.stringify(TEST_SCREENS));
    let { cleanup, browser } = await openMRAboutWelcome();

    await test_screen_content(
      browser,
      "Easy Setup test screen is visible with restore from backup CTA",
      [
        "main.AW_EASY_SETUP_TEST",
        "div.secondary-cta.top",
        "button[data-l10n-id='restore-from-backup-secondary-top-button']",
        "button[value='primary_button']",
      ]
    );

    // Clicking the CTA should set the pref to true and navigate to backup restore screen
    await clickVisibleButton(
      browser,
      "button[data-l10n-id='restore-from-backup-secondary-top-button']"
    );

    await test_screen_content(
      browser,
      "No backup found screen rendered after clicking the top CTA",
      [
        "main.AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND_TEST",
        "[data-l10n-id='restore-from-backup-title']",
        "[data-l10n-id='restore-from-backup-subtitle']",
        "button[data-l10n-id='restore-from-backup-secondary-button']",
      ]
    );

    // Click skip on backup restore screen
    await clickVisibleButton(
      browser,
      "button[data-l10n-id='restore-from-backup-secondary-button']"
    );

    await test_screen_content(
      browser,
      "Pressing skip returns to Easy Setup screen",
      ["main.AW_EASY_SETUP_TEST"]
    );

    await clickVisibleButton(browser, ".action-buttons button.primary");

    await test_screen_content(
      browser,
      "Pressing the primary button should proceed to follow up screen and not backup restore again",
      ["main.AW_TEST_FOLLOWUP"],
      //unexpected
      ["AW_BACKUP_RESTORE_EMBEDDED_NO_BACKUP_FOUND_TEST"]
    );

    // The screens rendered above leave a detached DOM subtree behind in the
    // content process. Gecko defers unbinding a detached subtree that nothing
    // outside the DOM tree references, and only unbinds it at the start of the
    // next cycle collection, so closing the tab immediately afterwards needs
    // one more GC/CC round than the shutdown leak checker runs and the window
    // gets reported as leaked. Collect it here instead of relying on the
    // browser happening to go idle first.
    await SpecialPowers.spawn(
      browser,
      [],
      () => new Promise(resolve => SpecialPowers.exactGC(resolve))
    );

    await SpecialPowers.popPrefEnv();
    await cleanup();
  }
);
