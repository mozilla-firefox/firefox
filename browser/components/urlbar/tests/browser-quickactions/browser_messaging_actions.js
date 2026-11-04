/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test quick actions loaded from `urlbar_action` messaging system messages.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ActionsProviderQuickActions:
    "moz-src:///browser/components/urlbar/ActionsProviderQuickActions.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

function makeMessage(key, content = {}) {
  return {
    id: `TEST_URLBAR_ACTION_${key.toUpperCase()}`,
    template: "urlbar_action",
    content: {
      key,
      keywords: [`${key}keyword`],
      label: `${key} label`,
      icon: "chrome://global/skin/icons/info.svg",
      action: {
        type: "OPEN_ABOUT_PAGE",
        data: { args: "robots", where: "tab" },
      },
      ...content,
    },
    targeting: "true",
  };
}

/**
 * Enrolls in a messaging experiment delivering the given messages.
 *
 * @param {object[]} messages The messages delivered by the experiment.
 * @param {string} [featureId] The messaging feature to enroll in.
 * @returns {Promise<() => Promise<void>>} A function that unenrolls, which can
 *   safely be called more than once.
 */
async function enroll(messages, featureId = "fxms-message-1") {
  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId,
    value: { template: "multi", messages },
  });
  // Enrolling directly doesn't send the notification sent after a Remote
  // Settings sync, which makes ASRouter reload its messages.
  Services.obs.notifyObservers(null, "nimbus:enrollments-updated");
  let unenrolled = false;
  return async () => {
    if (unenrolled) {
      return;
    }
    unenrolled = true;
    await cleanup();
    Services.obs.notifyObservers(null, "nimbus:enrollments-updated");
  };
}

function getButton(key) {
  return document.querySelector(`.urlbarView-action-btn[data-action=${key}]`);
}

async function search(value) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({ window, value });
}

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      [
        "browser.newtabpage.activity-stream.asrouter.providers.messaging-experiments",
        `{"id":"messaging-experiments","enabled":true,"type":"remote-experiments","updateCycleInMs":0}`,
      ],
    ],
  });
  await ASRouter._updateMessageProviders();

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    await ASRouter._updateMessageProviders();
  });
});

add_task(async function test_register_and_remove() {
  const sandbox = sinon.createSandbox();
  let unenroll;
  try {
    const strings = {
      "test-keywords": "Localized One, localized TWO",
      "test-label": "Localized label",
    };
    sandbox
      .stub(RemoteL10n, "formatLocalizableText")
      .callsFake(async text =>
        typeof text === "string" ? text : strings[text.string_id]
      );

    unenroll = await enroll([
      makeMessage("raw", { keywords: ["Raw Keyword, OTHER"], label: "Raw" }),
      makeMessage("localized", {
        keywords: [{ string_id: "test-keywords" }],
        label: { string_id: "test-label" },
      }),
      makeMessage("disallowed", {
        action: { type: "OPEN_URL", data: { args: "https://example.com/" } },
      }),
    ]);
    await TestUtils.waitForCondition(
      () => ActionsProviderQuickActions.getAction("fxms_localized"),
      "Actions are registered after enrolling"
    );

    const raw = ActionsProviderQuickActions.getAction("fxms_raw");
    Assert.deepEqual(raw.commands, ["raw keyword", "other"]);
    Assert.equal(raw.labelText, "Raw");
    Assert.ok(!ActionsProviderQuickActions.getAction("raw"), "Key is prefixed");

    const localized = ActionsProviderQuickActions.getAction("fxms_localized");
    Assert.deepEqual(localized.commands, ["localized one", "localized two"]);
    Assert.equal(localized.labelText, "Localized label");

    Assert.ok(
      !ActionsProviderQuickActions.getAction("fxms_disallowed"),
      "An action that isn't allowed isn't registered"
    );

    await search("raw keyword");
    const button = await TestUtils.waitForCondition(() =>
      getButton("fxms_raw")
    );
    Assert.equal(button.textContent, "Raw", "The button shows the label");
    await UrlbarTestUtils.promisePopupClose(window);

    await unenroll();
    await TestUtils.waitForCondition(
      () => !ActionsProviderQuickActions.getAction("fxms_raw"),
      "Actions are removed after unenrolling"
    );
    Assert.ok(!ActionsProviderQuickActions.getAction("fxms_localized"));
    const matches = await ActionsProviderQuickActions.getActions({
      input: "other",
      includesExactMatch: true,
    });
    Assert.ok(!matches.has("fxms_raw"), "The keywords are removed too");
  } finally {
    sandbox.restore();
    await unenroll?.();
  }
});

add_task(async function test_pick() {
  const sandbox = sinon.createSandbox();
  let unenroll;
  try {
    const handleAction = sandbox
      .stub(SpecialMessageActions, "handleAction")
      .resolves();
    const message = makeMessage("pick");
    unenroll = await enroll([message]);
    await TestUtils.waitForCondition(() =>
      ActionsProviderQuickActions.getAction("fxms_pick")
    );

    await search("pickkeyword");
    const button = await TestUtils.waitForCondition(() =>
      getButton("fxms_pick")
    );
    let engaged = UrlbarTestUtils.promiseProviderEngagement(window);
    EventUtils.synthesizeKey("KEY_Tab", {}, window);
    Assert.equal(
      gURLBar.inputField.getAttribute("aria-activedescendant"),
      button.id,
      "The action button is selected"
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, window);
    await UrlbarTestUtils.promisePopupClose(window);
    await engaged;

    Assert.ok(handleAction.calledOnce, "The message's action is run");
    Assert.deepEqual(handleAction.firstCall.args[0], message.content.action);
    Assert.equal(handleAction.firstCall.args[1], gBrowser.selectedBrowser);
  } finally {
    sandbox.restore();
    await unenroll?.();
  }
  await TestUtils.waitForCondition(
    () => !ActionsProviderQuickActions.getAction("fxms_pick")
  );
});

add_task(async function test_reload_replaces_actions() {
  let unenrollFirst;
  let unenrollSecond;
  try {
    unenrollFirst = await enroll([makeMessage("first")]);
    unenrollSecond = await enroll([makeMessage("second")], "fxms-message-2");
    await TestUtils.waitForCondition(
      () =>
        ActionsProviderQuickActions.getAction("fxms_first") &&
        ActionsProviderQuickActions.getAction("fxms_second"),
      "Actions from both experiments are registered"
    );

    await unenrollFirst();
    await TestUtils.waitForCondition(
      () => !ActionsProviderQuickActions.getAction("fxms_first"),
      "The unenrolled experiment's action is removed"
    );
    Assert.ok(
      ActionsProviderQuickActions.getAction("fxms_second"),
      "The other experiment's action is kept"
    );
  } finally {
    await unenrollFirst?.();
    await unenrollSecond?.();
  }
  await TestUtils.waitForCondition(
    () => !ActionsProviderQuickActions.getAction("fxms_second")
  );
});
