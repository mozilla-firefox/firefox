const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { RemoteSettingsExperimentLoader } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);
const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { ExperimentFakes, ExperimentTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { ExperimentManager } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);
const { NimbusTelemetry } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Telemetry.sys.mjs"
);
const { ASRouterTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTelemetry.sys.mjs"
);
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const MESSAGE_CONTENT = {
  id: "xman_test_message",
  groups: [],
  content: {
    text: "This is a test CFR",
    addon: {
      id: "954390",
      icon: "chrome://activity-stream/content/data/content/assets/cfr_fb_container.png",
      title: "Facebook Container",
      users: "1455872",
      author: "Mozilla",
      rating: "4.5",
      amo_url: "https://addons.mozilla.org/firefox/addon/facebook-container/",
    },
    buttons: {
      primary: {
        label: {
          string_id: "cfr-doorhanger-extension-ok-button",
        },
        action: {
          data: {
            url: "about:blank",
          },
          type: "INSTALL_ADDON_FROM_URL",
        },
      },
      secondary: [
        {
          label: {
            string_id: "cfr-doorhanger-extension-cancel-button",
          },
          action: {
            type: "CANCEL",
          },
        },
        {
          label: {
            string_id: "cfr-doorhanger-extension-never-show-recommendation",
          },
        },
        {
          label: {
            string_id: "cfr-doorhanger-extension-manage-settings-button",
          },
          action: {
            data: {
              origin: "CFR",
              category: "general-cfraddons",
            },
            type: "OPEN_PREFERENCES_PAGE",
          },
        },
      ],
    },
    category: "cfrAddons",
    layout: "short_message",
    bucket_id: "CFR_M1",
    info_icon: {
      label: {
        string_id: "cfr-doorhanger-extension-sumo-link",
      },
      sumo_path: "extensionrecommendations",
    },
    heading_text: "Welcome to the experiment",
    notification_text: {
      string_id: "cfr-doorhanger-extension-notification2",
    },
  },
  trigger: {
    id: "openURL",
    params: [
      "www.facebook.com",
      "facebook.com",
      "www.instagram.com",
      "instagram.com",
      "www.whatsapp.com",
      "whatsapp.com",
      "web.whatsapp.com",
      "www.messenger.com",
      "messenger.com",
    ],
  },
  template: "cfr_doorhanger",
  frequency: {
    lifetime: 3,
  },
  targeting: "true",
};

const getExperiment = async feature => {
  let recipe = ExperimentFakes.recipe(
    // In tests by default studies/experiments are turned off. We turn them on
    // to run the test and rollback at the end. Cleanup causes unenrollment so
    // for cases where the test runs multiple times we need unique ids.
    `test_xman_${feature}_${Date.now()}`,
    {
      id: "xman_test_message",
      bucketConfig: {
        count: 100,
        start: 0,
        total: 100,
        namespace: "mochitest",
        randomizationUnit: "normandy_id",
      },
    }
  );
  recipe.branches[0].features[0].featureId = feature;
  recipe.branches[0].features[0].value = MESSAGE_CONTENT;
  recipe.branches[1].features[0].featureId = feature;
  recipe.branches[1].features[0].value = MESSAGE_CONTENT;
  recipe.featureIds = [feature];
  await ExperimentTestUtils.validateExperiment(recipe);
  return recipe;
};

const getCFRExperiment = async () => {
  return getExperiment("cfr");
};

const client = RemoteSettings("nimbus-desktop-experiments");

// no `add_task` because we want to run this setup before each test not before
// the entire test suite.
async function setup(experiment) {
  // Store the experiment in RS local db to bypass synchronization.
  await client.db.importChanges({}, Date.now(), [experiment], { clear: true });
  await SpecialPowers.pushPrefEnv({
    set: [
      ["app.shield.optoutstudies.enabled", true],
      ["datareporting.healthreport.uploadEnabled", true],
      [
        "browser.newtabpage.activity-stream.asrouter.providers.messaging-experiments",
        `{"id":"messaging-experiments","enabled":true,"type":"remote-experiments","updateCycleInMs":0}`,
      ],
    ],
  });
}

async function cleanup() {
  await client.db.clear();
  await SpecialPowers.popPrefEnv();
  // Reload the provider
  await ASRouter._updateMessageProviders();
}

/**
 * Assert that a message is (or optionally is not) present in the ASRouter
 * messages list, optionally waiting for it to be present/not present.
 * @param {string} id message id
 * @param {boolean} [found=true] expect the message to be found
 * @param {boolean} [wait=true] check for the message until found/not found
 * @returns {Promise<Message|null>} resolves with the message, if found
 */
async function assertMessageInState(id, found = true, wait = true) {
  if (wait) {
    await BrowserTestUtils.waitForCondition(
      () => !!ASRouter.state.messages.find(m => m.id === id) === found,
      `Message ${id} should ${found ? "" : "not"} be found in ASRouter state`
    );
  }
  const message = ASRouter.state.messages.find(m => m.id === id);
  Assert.equal(
    !!message,
    found,
    `Message ${id} should ${found ? "" : "not"} be found`
  );
  return message || null;
}

add_task(async function test_loading_experimentsAPI() {
  const experiment = await getCFRExperiment();
  await setup(experiment);
  // Fetch the new recipe from RS
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId: "cfr" }),
    "ExperimentAPI should return an experiment"
  );

  const telemetryInstance = new ASRouterTelemetry();
  Assert.ok(telemetryInstance.isInCFRCohort, "Telemetry should return true");

  await assertMessageInState("xman_test_message");

  await cleanup();
});

add_task(async function test_loading_fxms_message_1_feature() {
  const experiment = await getExperiment("fxms-message-1");
  await setup(experiment);
  // Fetch the new recipe from RS
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId: "fxms-message-1" }),
    "ExperimentAPI should return an experiment"
  );

  await assertMessageInState("xman_test_message");

  await cleanup();
});

add_task(async function test_loading_experimentsAPI_rollout() {
  const rollout = await getCFRExperiment();
  rollout.isRollout = true;
  rollout.branches.pop();

  await setup(rollout);
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(() =>
    ExperimentAPI.getRolloutMetaData({ featureId: "cfr" })
  );

  await assertMessageInState("xman_test_message");

  await cleanup();
});

add_task(async function test_exposure_ping() {
  // Reset this check to allow sending multiple exposure pings in tests
  NimbusFeatures.cfr._didSendExposureEvent = false;
  const experiment = await getCFRExperiment();
  await setup(experiment);
  Services.telemetry.clearScalars();
  // Fetch the new recipe from RS
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId: "cfr" }),
    "ExperimentAPI should return an experiment"
  );

  await assertMessageInState("xman_test_message");

  const exposureSpy = sinon.spy(NimbusTelemetry, "recordExposure");

  await ASRouter.sendTriggerMessage({
    browser: gBrowser.selectedBrowser,
    id: "openURL",
    param: { host: "messenger.com" },
  });

  Assert.ok(exposureSpy.callCount === 1, "Should send exposure ping");
  const scalars = TelemetryTestUtils.getProcessScalars("parent", true, true);
  TelemetryTestUtils.assertKeyedScalar(
    scalars,
    "telemetry.event_counts",
    "normandy#expose#nimbus_experiment",
    1
  );

  exposureSpy.restore();
  await cleanup();
});

add_task(async function test_forceEnrollUpdatesMessages() {
  const experiment = await getCFRExperiment();

  await setup(experiment);
  await SpecialPowers.pushPrefEnv({
    set: [["nimbus.debug", true]],
  });

  await assertMessageInState("xman_test_message", false, false);

  await RemoteSettingsExperimentLoader.optInToExperiment({
    slug: experiment.slug,
    branch: experiment.branches[0].slug,
  });

  await assertMessageInState("xman_test_message");

  await ExperimentManager.unenroll(`optin-${experiment.slug}`);
  await SpecialPowers.popPrefEnv();
  await cleanup();
});

add_task(async function test_update_on_enrollments_changed() {
  // Check that the message is not already present
  await assertMessageInState("xman_test_message", false, false);

  const experiment = await getCFRExperiment();
  let enrollmentChanged = TestUtils.topicObserved("nimbus:enrollments-updated");
  await setup(experiment);
  await RemoteSettingsExperimentLoader.updateRecipes();

  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId: "cfr" }),
    "ExperimentAPI should return an experiment"
  );
  await enrollmentChanged;

  await assertMessageInState("xman_test_message");

  await cleanup();
});

add_task(async function test_emptyMessage() {
  const experiment = ExperimentFakes.recipe(`empty_${Date.now()}`, {
    id: "empty",
    branches: [
      {
        slug: "a",
        ratio: 1,
        features: [
          {
            featureId: "cfr",
            value: {},
          },
        ],
      },
    ],
    bucketConfig: {
      start: 0,
      count: 100,
      total: 100,
      namespace: "mochitest",
      randomizationUnit: "normandy_id",
    },
  });

  await setup(experiment);
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId: "cfr" }),
    "ExperimentAPI should return an experiment"
  );

  await ASRouter._updateMessageProviders();

  const experimentsProvider = ASRouter.state.providers.find(
    p => p.id === "messaging-experiments"
  );

  // Clear all messages
  ASRouter.setState(() => ({
    messages: [],
  }));

  await ASRouter.loadMessagesFromAllProviders([experimentsProvider]);

  Assert.deepEqual(
    ASRouter.state.messages,
    [],
    "ASRouter should have loaded zero messages"
  );

  await cleanup();
});

add_task(async function test_multiMessageTreatment() {
  const featureId = "cfr";
  // Add an array of two messages to the first branch
  const messages = [
    { ...MESSAGE_CONTENT, id: "multi-message-1" },
    { ...MESSAGE_CONTENT, id: "multi-message-2" },
  ];
  const recipe = ExperimentFakes.recipe(`multi-message_${Date.now()}`, {
    id: `multi-message`,
    bucketConfig: {
      count: 100,
      start: 0,
      total: 100,
      namespace: "mochitest",
      randomizationUnit: "normandy_id",
    },
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [{ featureId, value: { template: "multi", messages } }],
      },
    ],
  });
  await ExperimentTestUtils.validateExperiment(recipe);

  await setup(recipe);
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId }),
    "ExperimentAPI should return an experiment"
  );

  await BrowserTestUtils.waitForCondition(
    () =>
      messages
        .map(m => ASRouter.state.messages.find(n => n.id === m.id))
        .every(Boolean),
    "Experiment message found in ASRouter state"
  );
  Assert.ok(true, "Experiment message found in ASRouter state");

  await cleanup();
});
