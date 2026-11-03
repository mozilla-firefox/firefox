/* Public Domain — http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DiscoveryStreamFeed } = ChromeUtils.importESModule(
  "resource://newtab/lib/DiscoveryStreamFeed.sys.mjs"
);
const { PREFS_CONFIG } = ChromeUtils.importESModule(
  "resource://newtab/lib/ActivityStream.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
});

const PREF_PREFIX = "browser.newtabpage.activity-stream.";
const TOP_SITES_PREF = `${PREF_PREFIX}feeds.topsites`;
const TOP_STORIES_PREF = `${PREF_PREFIX}feeds.section.topstories`;
const VARIANT_PREF = `${PREF_PREFIX}activationWindow.variant`;
const TOP_SITES_TEMP_PREF = `${PREF_PREFIX}activationWindow.temp.topSitesUserValue`;
const TOP_STORIES_TEMP_PREF = `${PREF_PREFIX}activationWindow.temp.topStoriesUserValue`;
const WIDGETS_PREF = `${PREF_PREFIX}widgets.enabled`;
const WIDGETS_TEMP_PREF = `${PREF_PREFIX}activationWindow.temp.widgetsUserValue`;

const TEST_NOW = Temporal.Instant.from("2024-01-15T12:00:00Z");
const TEST_PROFILE_24H_AGO = Temporal.Instant.from("2024-01-14T12:00:00Z");
const TEST_PROFILE_50H_AGO = Temporal.Instant.from("2024-01-13T10:00:00Z");

/**
 * Gets the PrefsFeed instance and ensures it's not in activation window state.
 *
 * @returns {Promise<object>} The PrefsFeed instance.
 */
async function getPrefsFeed() {
  const prefsFeed = AboutNewTab.activityStream.store.feeds.get("feeds.prefs");
  if (prefsFeed.inActivationWindowState) {
    prefsFeed.exitActivationWindowState();
  }

  return prefsFeed;
}

/**
 * Enrolls in an activation window experiment with the given configuration and
 * waits for the enrollment to be fully applied.
 *
 * @param {PrefsFeed} prefsFeed - The PrefsFeed instance.
 * @param {object} activationWindowConfig - The activation window configuration object.
 * @returns {Promise<Function>} A cleanup function to unenroll from the experiment.
 */
async function enrollWithActivationWindow(prefsFeed, activationWindowConfig) {
  const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "newtabTrainhop",
    value: {
      type: "activationWindowBehavior",
      payload: activationWindowConfig,
    },
  });

  await NimbusTestUtils.flushStore();

  await TestUtils.waitForCondition(() => {
    const allEnrollments = NimbusFeatures.newtabTrainhop.getAllEnrollments();

    if (!allEnrollments || allEnrollments.length !== 1) {
      return false;
    }

    const trainhopConfig = prefsFeed._getTrainhopConfig();
    return ObjectUtils.deepEqual(
      trainhopConfig.activationWindowBehavior,
      activationWindowConfig
    );
  }, "Wait for expected activation window config");

  return doExperimentCleanup;
}

/**
 * Navigates to about:newtab and runs an activation window check.
 *
 * @param {Tab} tab - The browser tab to navigate.
 * @param {PrefsFeed} prefsFeed - The PrefsFeed instance.
 * @param {Temporal.Instant} testTime - The simulated current time for the activation window check.
 * @param {boolean} [isStartup=true] - Whether to simulate a browser startup.
 * @param {boolean} [expectTopSites=true] - Whether top sites are expected to be visible.
 */
async function navigateToNewTabAndRunActivationWindowCheck(
  tab,
  prefsFeed,
  testTime,
  isStartup = true,
  expectTopSites = true
) {
  let stopped = BrowserTestUtils.browserStopped(
    tab.linkedBrowser,
    "about:newtab"
  );
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, "about:newtab");
  await stopped;

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [expectTopSites],
    async checkForTopSites => {
      if (checkForTopSites) {
        await ContentTaskUtils.waitForCondition(
          () => content.document.querySelector(".top-sites-list"),
          "Wait for top-sites to be visible"
        );
      } else {
        await ContentTaskUtils.waitForCondition(
          () => content.document.querySelector(".top-sites-list") === null,
          "Wait for top-sites to be invisible"
        );
      }
    }
  );

  prefsFeed.checkForActivationWindow(testTime, isStartup);

  await TestUtils.waitForTick();
}

add_setup(async () => {
  NewTabPagePreloading.removePreloadedBrowser(window);

  await AboutNewTab.activityStreamPromise;
  let prefsFeed = AboutNewTab.activityStream.store.feeds.get("feeds.prefs");
  let sandbox = sinon.createSandbox();
  let originalOnAction = prefsFeed.onAction;
  sandbox.stub(prefsFeed, "onAction").callsFake(action => {
    if (action.type !== actionTypes.NEW_TAB_STATE_REQUEST) {
      originalOnAction.apply(prefsFeed, [action]);
    }
  });

  sandbox
    .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
    .returns(
      "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
    );

  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        PREFS_CONFIG.get("discoverystream.config").getValue({
          geo: "US",
          locale: "en-US",
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
      [WIDGETS_PREF, false],
    ],
  });

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.activationWindow.enterMessageID"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.activationWindow.exitMessageID"
    );
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
  });
});

/**
 * Tests that the createdInstant getter is being populated with the current
 * profile creation date.
 */
add_task(async function test_createdInstant_getter() {
  let profileAccessor = await ProfileAge();
  let createdInstant = Temporal.Instant.fromEpochMilliseconds(
    await profileAccessor.created
  );
  Assert.ok(
    AboutNewTab.activityStream.createdInstant,
    "Should have been constructed with a createdInstant"
  );
  Assert.ok(
    AboutNewTab.activityStream.createdInstant.equals(createdInstant),
    "ActivityStream.createdInstant should equal the profile creation instant."
  );
});

/**
 * Tests that entering the activation window correctly hides top sites and
 * top stories when configured to do so via Nimbus.
 */
add_task(async function test_activation_window_entry() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: true,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(() => {
      return !content.document.querySelector(".top-sites-list");
    }, "Top sites should be hidden during activation window");

    const topStoriesSection = content.document.querySelector(
      "[data-section-id='topstories']"
    );
    Assert.ok(
      !topStoriesSection,
      "Top stories should be hidden during activation window"
    );
  });

  BrowserTestUtils.removeTab(tab);

  if (prefsFeed.inActivationWindowState) {
    prefsFeed.exitActivationWindowState();
  }
  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user explicitly enables top sites during the activation
 * window (by setting the pref to true), their choice persists after the
 * activation window expires.
 */
add_task(async function test_user_enabling_persists_topsites() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: true,
    disableTopStories: false,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  Services.prefs.setBoolPref(TOP_SITES_PREF, true);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites-list"),
      "Top sites should appear after user enables them"
    );
  });

  BrowserTestUtils.removeTab(tab);

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  const topSitesEnabled = Services.prefs.getBoolPref(TOP_SITES_PREF);
  Assert.equal(
    topSitesEnabled,
    true,
    "User's choice to enable top sites should persist"
  );

  Assert.ok(
    !Services.prefs.prefHasUserValue(TOP_SITES_TEMP_PREF),
    "Temp pref should be cleared after exit"
  );

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user explicitly enables top stories during the activation
 * window (by setting the pref to true), their choice persists after the
 * activation window expires.
 */
add_task(async function test_user_enabling_persists_topstories() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  Services.prefs.setBoolPref(TOP_STORIES_PREF, true);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("[data-section-id='topstories']"),
      "Top stories should appear after user enables them"
    );
  });

  BrowserTestUtils.removeTab(tab);

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  const topStoriesEnabled = Services.prefs.getBoolPref(TOP_STORIES_PREF);
  Assert.equal(
    topStoriesEnabled,
    true,
    "User's choice to enable top stories should persist"
  );

  Assert.ok(
    !Services.prefs.prefHasUserValue(TOP_STORIES_TEMP_PREF),
    "Temp pref should be cleared after exit"
  );

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user explicitly disables top sites during the activation
 * window (by setting the pref to false), their choice persists after the
 * activation window expires.
 */
add_task(async function test_user_disabling_persists_topsites() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  // Simulate the user enabling top sites...
  Services.prefs.setBoolPref(TOP_SITES_PREF, true);
  // and then deciding to disable them again.
  Services.prefs.setBoolPref(TOP_SITES_PREF, false);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".top-sites-list"),
      "Top sites should be hidden after user disables them"
    );
  });

  BrowserTestUtils.removeTab(tab);

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  const topSitesEnabled = Services.prefs.getBoolPref(TOP_SITES_PREF);
  Assert.equal(
    topSitesEnabled,
    false,
    "User's choice to disable top sites should persist"
  );

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user explicitly disables top stories during the activation
 * window (by setting the pref to false), their choice persists after the
 * activation window expires.
 */
add_task(async function test_user_disabling_persists_topstories() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  // Simulate the user enabling top stories...
  Services.prefs.setBoolPref(TOP_STORIES_PREF, true);
  // and then deciding to disable them again.
  Services.prefs.setBoolPref(TOP_STORIES_PREF, false);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const topStoriesSection = content.document.querySelector(
      "[data-section-id='topstories']"
    );
    Assert.ok(
      !topStoriesSection,
      "Top stories should be hidden during activation window"
    );
  });

  BrowserTestUtils.removeTab(tab);

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  const topStoriesEnabled = Services.prefs.getBoolPref(TOP_STORIES_PREF);
  Assert.equal(
    topStoriesEnabled,
    false,
    "User's choice to disable top stories should persist"
  );

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that entering the activation window disables widgets by default when
 * configured to do so via Nimbus, and that the original default is restored
 * after the activation window expires.
 */
add_task(async function test_activation_window_widgets() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(WIDGETS_PREF);
  const originalDefault = Services.prefs
    .getDefaultBranch("")
    .getBoolPref(WIDGETS_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: false,
    disableWidgets: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  prefsFeed.checkForActivationWindow(TEST_NOW, /* isStartup */ true);

  Assert.equal(
    Services.prefs.getBoolPref(WIDGETS_PREF),
    false,
    "Widgets should be hidden during activation window"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(WIDGETS_TEMP_PREF),
    "Temp pref should not be set by the activation window default"
  );

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  Assert.equal(
    Services.prefs.getBoolPref(WIDGETS_PREF),
    originalDefault,
    "Widgets default should be restored after exit"
  );

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user explicitly enables widgets during the activation
 * window, their choice persists after the activation window expires.
 */
add_task(async function test_user_enabling_persists_widgets() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(WIDGETS_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: false,
    disableWidgets: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  prefsFeed.checkForActivationWindow(TEST_NOW, /* isStartup */ true);

  Services.prefs.setBoolPref(WIDGETS_PREF, true);

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  Assert.equal(
    Services.prefs.getBoolPref(WIDGETS_PREF),
    true,
    "User's choice to enable widgets should persist"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(WIDGETS_TEMP_PREF),
    "Temp pref should be cleared after exit"
  );

  Services.prefs.clearUserPref(WIDGETS_PREF);
  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when a user enables and then disables widgets during the
 * activation window, their choice to disable them persists after the
 * activation window expires.
 */
add_task(async function test_user_disabling_persists_widgets() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(WIDGETS_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: false,
    disableTopStories: false,
    disableWidgets: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  prefsFeed.checkForActivationWindow(TEST_NOW, /* isStartup */ true);

  // Simulate the user enabling widgets...
  Services.prefs.setBoolPref(WIDGETS_PREF, true);
  // and then deciding to disable them again.
  Services.prefs.setBoolPref(WIDGETS_PREF, false);

  Assert.equal(
    Services.prefs.getBoolPref(WIDGETS_TEMP_PREF),
    false,
    "Temp pref should hold the user's choice before exit"
  );

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  Assert.equal(
    Services.prefs.getBoolPref(WIDGETS_PREF),
    false,
    "User's choice to disable widgets should persist"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(WIDGETS_TEMP_PREF),
    "Temp pref should be cleared after exit"
  );

  Services.prefs.clearUserPref(WIDGETS_PREF);
  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that simulating a browser restart (by calling checkForActivationWindow
 * with isStartup=true) correctly reapplies the activation window defaults,
 * ensuring that the modified default prefs are restored on each startup during
 * the activation window period.
 */
add_task(async function test_restart_reapplies_defaults() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: true,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".top-sites-list"),
      "Top sites should be hidden initially"
    );
  });

  BrowserTestUtils.removeTab(tab);

  tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true,
    /* expectTopSites */ false
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".top-sites-list"),
      "Top sites should still be hidden after restart"
    );
  });

  BrowserTestUtils.removeTab(tab);

  if (prefsFeed.inActivationWindowState) {
    prefsFeed.exitActivationWindowState();
  }
  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Tests that when the activation window expires (profile age exceeds the
 * configured maximum), the default prefs are restored to their normal values
 * and the variant pref is cleared.
 */
add_task(async function test_defaults_restored_after_exit() {
  const sandbox = sinon.createSandbox();
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
    enabled: true,
    maxProfileAgeInHours: 48,
    disableTopSites: true,
    disableTopStories: true,
    variant: "a",
  });

  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await navigateToNewTabAndRunActivationWindowCheck(
    tab,
    prefsFeed,
    TEST_NOW,
    /* isStartup */ true
  );

  profileCreatedInstant = TEST_PROFILE_50H_AGO;
  prefsFeed.checkForActivationWindow(TEST_NOW);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites-list"),
      "Top sites should reappear after exiting activation window"
    );
  });

  BrowserTestUtils.removeTab(tab);

  const variantPref = Services.prefs.getStringPref(VARIANT_PREF, "");
  Assert.equal(variantPref, "", "Variant pref should be cleared after exit");

  await doExperimentCleanup();
  sandbox.restore();
});

/**
 * Test that by setting the activationWindow.variant pref to "a" and "b", we can
 * apply the appropriate classes to the customization menu button.
 */
add_task(async function test_activation_window_variants() {
  let profileCreatedInstant = TEST_PROFILE_24H_AGO;

  const prefsFeed = await getPrefsFeed();

  Services.prefs.clearUserPref(TOP_SITES_PREF);
  Services.prefs.clearUserPref(TOP_STORIES_PREF);

  await ExperimentAPI.ready();
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(AboutNewTab.activityStream, "createdInstant")
    .get(() => profileCreatedInstant);

  for (const variantToTest of ["a", "b"]) {
    const doExperimentCleanup = await enrollWithActivationWindow(prefsFeed, {
      enabled: true,
      maxProfileAgeInHours: 48,
      disableTopSites: true,
      disableTopStories: true,
      variant: variantToTest,
    });

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );

    await navigateToNewTabAndRunActivationWindowCheck(
      tab,
      prefsFeed,
      TEST_NOW,
      /* isStartup */ true
    );

    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [variantToTest],
      async variantName => {
        await ContentTaskUtils.waitForCondition(() => {
          return !content.document.querySelector(".top-sites-list");
        }, "Top sites should be hidden during activation window");

        const customizeButton = content.document.querySelector(
          "button.personalize-button, moz-button.open-customization-button"
        );
        Assert.ok(
          customizeButton.classList.contains(
            `activation-window-variant-${variantName}`
          ),
          `Found the activation-window-variant-${variantName} class on the customize button`
        );
      }
    );

    BrowserTestUtils.removeTab(tab);

    if (prefsFeed.inActivationWindowState) {
      prefsFeed.exitActivationWindowState();
    }
    await doExperimentCleanup();
  }

  sandbox.restore();
});
