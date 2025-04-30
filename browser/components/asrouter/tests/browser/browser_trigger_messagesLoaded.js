/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
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

const client = RemoteSettings("nimbus-desktop-experiments");

const TEST_MESSAGE_CONTENT = {
  id: "ON_LOAD_TEST_MESSAGE",
  template: "cfr_doorhanger",
  content: {
    bucket_id: "ON_LOAD_TEST_MESSAGE",
    anchor_id: "PanelUI-menu-button",
    layout: "icon_and_message",
    icon: "chrome://activity-stream/content/data/content/assets/glyph-webextension-16.svg",
    icon_dark_theme:
      "chrome://activity-stream/content/data/content/assets/glyph-webextension-16.svg",
    icon_class: "cfr-doorhanger-small-icon",
    heading_text: "Heading",
    text: "Text",
    buttons: {
      primary: {
        label: { value: "Primary CTA", attributes: { accesskey: "P" } },
        action: { navigate: true },
      },
      secondary: [
        {
          label: { value: "Secondary CTA", attributes: { accesskey: "S" } },
          action: { type: "CANCEL" },
        },
      ],
    },
    skip_address_bar_notifier: true,
  },
  targeting: "true",
  trigger: { id: "messagesLoaded" },
};

add_task(async function test_messagesLoaded_reach_experiment() {
  const sandbox = sinon.createSandbox();
  const sendTriggerSpy = sandbox.spy(ASRouter, "sendTriggerMessage");
  const routeSpy = sandbox.spy(ASRouter, "routeCFRMessage");
  const reachSpy = sandbox.spy(ASRouter, "_recordReachEvent");
  const triggerMatch = sandbox.match({ id: "messagesLoaded" });
  const featureId = "cfr";
  const recipe = ExperimentFakes.recipe(
    `messages_loaded_test_${Services.uuid
      .generateUUID()
      .toString()
      .slice(1, -1)}`,
    {
      id: `messages-loaded-test`,
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
          features: [
            {
              featureId,
              value: { ...TEST_MESSAGE_CONTENT, id: "messages-loaded-test-1" },
            },
          ],
        },
        {
          slug: "treatment",
          ratio: 1,
          features: [
            {
              featureId,
              value: { ...TEST_MESSAGE_CONTENT, id: "messages-loaded-test-2" },
            },
          ],
        },
      ],
    }
  );
  await ExperimentTestUtils.validateExperiment(recipe);

  await client.db.importChanges({}, Date.now(), [recipe], { clear: true });
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
  await RemoteSettingsExperimentLoader.updateRecipes();
  await BrowserTestUtils.waitForCondition(
    () => ExperimentAPI.getExperimentMetaData({ featureId }),
    "ExperimentAPI should return an experiment"
  );

  await ASRouter._updateMessageProviders();
  await ASRouter.loadMessagesFromAllProviders();

  const filterFn = m =>
    ["messages-loaded-test-1", "messages-loaded-test-2"].includes(m?.id);
  await BrowserTestUtils.waitForCondition(
    () => ASRouter.state.messages.filter(filterFn).length > 1,
    "Should load the test messages"
  );
  Assert.ok(sendTriggerSpy.calledWith(triggerMatch, true), "Trigger fired");
  Assert.ok(
    routeSpy.calledWith(
      sandbox.match(filterFn),
      gBrowser.selectedBrowser,
      triggerMatch
    ),
    "Trigger routed to the correct message"
  );
  Assert.ok(
    reachSpy.calledWith(sandbox.match(filterFn)),
    "Trigger recorded a reach event"
  );
  Assert.ok(
    ASRouter.state.messages.find(m => filterFn(m) && m.forReachEvent)
      ?.forReachEvent.sent,
    "Reach message will not be sent again"
  );

  sandbox.restore();
  await client.db.clear();
  await SpecialPowers.popPrefEnv();
  await ASRouter._updateMessageProviders();
});
