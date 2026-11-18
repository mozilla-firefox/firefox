/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for engagement telemetry for tips using Glean.

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-tips/head.js",
  this
);

add_setup(async function () {
  await makeProfileResettable();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  registerCleanupFunction(async function () {
    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function selected_result_tip() {
  const testData = [
    {
      type: "searchTip_onboard",
      expected: "tip_onboard",
    },
    {
      type: "searchTip_redirect",
      expected: "tip_redirect",
    },
    {
      type: "test",
      expected: "tip_unknown",
    },
  ];

  for (const { type, expected } of testData) {
    const deferred = Promise.withResolvers();
    const provider = new UrlbarTestUtils.TestProvider({
      results: [
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.TIP,
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          payload: {
            type,
            helpUrl: "https://example.com/",
            titleL10n: { id: "urlbar-search-tips-confirm" },
            buttons: [
              {
                url: "https://example.com/",
                l10n: { id: "urlbar-search-tips-confirm" },
              },
            ],
          },
        }),
      ],
      priority: 1,
      onEngagement: () => {
        deferred.resolve();
      },
    });
    let providersManager = ProvidersManager.getInstanceForSap("urlbar");
    providersManager.registerProvider(provider);

    await doTest(async () => {
      await openPopup("example");
      await selectRowByType(type);
      let newTabOpened = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "https://example.com/"
      );
      EventUtils.synthesizeKey("VK_RETURN");
      await deferred.promise;

      await assertEngagementTelemetry([
        {
          selected_result: expected,
          results: expected,
        },
      ]);

      let newTab = await newTabOpened;
      await BrowserTestUtils.removeTab(newTab);
    }).finally(() => providersManager.unregisterProvider(provider));
  }
});

add_task(async function selected_result_intervention_clear() {
  await doInterventionTest(
    SEARCH_STRINGS.CLEAR,
    "intervention_clear",
    "chrome://browser/content/sanitize_v2.xhtml",
    [
      {
        selected_result: "intervention_clear",
        results: "search_engine,intervention_clear",
      },
    ]
  );
});

add_task(async function selected_result_intervention_refresh() {
  await doInterventionTest(
    SEARCH_STRINGS.REFRESH,
    "intervention_refresh",
    "chrome://global/content/resetProfile.xhtml",
    [
      {
        selected_result: "intervention_refresh",
        results: "search_engine,intervention_refresh",
      },
    ]
  );
});

add_task(async function selected_result_intervention_update() {
  // Updates are disabled for MSIX packages, this test is irrelevant for them.
  if (
    AppConstants.platform === "win" &&
    Services.sysinfo.getProperty("hasWinPackageId")
  ) {
    return;
  }
  await UpdateUtils.setAppUpdateAutoEnabled(false);
  await initUpdate({ queryString: "&noUpdates=1" });
  UrlbarProviderInterventions.checkForBrowserUpdate(true);
  await processUpdateSteps([
    {
      panelId: "checkingForUpdates",
      checkActiveUpdate: null,
      continueFile: CONTINUE_CHECK,
    },
    {
      panelId: "noUpdatesFound",
      checkActiveUpdate: null,
      continueFile: null,
    },
  ]);

  await doInterventionTest(
    SEARCH_STRINGS.UPDATE,
    "intervention_update_refresh",
    "chrome://global/content/resetProfile.xhtml",
    [
      {
        selected_result: "intervention_update",
        results: "search_engine,intervention_update",
      },
    ]
  );
});

add_task(async function learn_more_link() {
  const provider = new UrlbarTestUtils.TestProvider({
    results: [
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.TIP,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        payload: {
          type: "test",
          titleL10n: { id: "urlbar-search-tips-confirm" },
          descriptionL10n: {
            id: "firefox-suggest-onboarding-main-accept-option-label",
            parseMarkup: true,
          },
          descriptionLearnMoreTopic: "learn_more_link",
        },
      }),
    ],
    priority: 1,
  });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);

  await doTest(async () => {
    await openPopup("any");
    let expectedURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "learn_more_link";
    let newTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, expectedURL);
    EventUtils.synthesizeKey("KEY_Tab");
    Assert.equal(
      gURLBar.view.selectedElement.dataset.l10nName,
      "learn-more-link"
    );
    EventUtils.synthesizeKey("KEY_Enter");
    info("Wait until expected url is loaded in the current tab");
    let newTab = await newTabOpened;

    await assertEngagementTelemetry([
      {
        selected_result: "tip_unknown",
        engagement_type: "help",
      },
    ]);

    await BrowserTestUtils.removeTab(newTab);
  }).finally(() => providersManager.unregisterProvider(provider));
});

async function doInterventionTest(keyword, type, dialog, expectedTelemetry) {
  await doTest(async () => {
    await openPopup(keyword);
    await selectRowByType(type);
    const onDialog = BrowserTestUtils.promiseAlertDialog("cancel", dialog, {
      isSubDialog: true,
    });
    EventUtils.synthesizeKey("VK_RETURN");
    await onDialog;

    await assertEngagementTelemetry(expectedTelemetry);
  });
}
