/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test we don't re-enter record() (and record both an engagement and an
// abandonment) when handling an engagement blurs the input field.

const TEST_URL = "https://example.com/";

add_task(async function () {
  await setup();
  let onEngagementDeferred = Promise.withResolvers();
  const provider = new UrlbarTestUtils.TestProvider({
    results: [
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        payload: {
          url: TEST_URL,
          helpUrl: "https://example.com/help",
          helpL10n: {
            id: "urlbar-result-menu-tip-get-help2",
          },
        },
      }),
    ],
    priority: 999,
    onEngagement: () => {
      info("Blur the address bar during the onEngagement notification");
      gURLBar.blur();
      // Run at the next tick to be sure spurious events would have happened.
      TestUtils.waitForTick().then(() => {
        onEngagementDeferred.resolve();
      });
    },
  });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);
  // This should cover at least engagement and abandonment.
  let engagementSpy = sinon.spy(provider, "onEngagement");

  let beforeRecordCall = false,
    recordReentered = false;
  let recordStub = sinon
    .stub(gURLBar.controller.session, "record")
    .callsFake((...args) => {
      recordReentered = beforeRecordCall;
      beforeRecordCall = true;
      recordStub.wrappedMethod.apply(gURLBar.controller.session, args);
      beforeRecordCall = false;
    });

  registerCleanupFunction(() => {
    sinon.restore();
    providersManager.unregisterProvider(provider);
  });

  await doTest(async () => {
    await openPopup("example");
    await selectRowByURL(TEST_URL);
    EventUtils.synthesizeKey("VK_RETURN");
    await Promise.all([onEngagementDeferred]);

    await assertEngagementTelemetry([{ engagement_type: "enter" }]);
    await assertAbandonmentTelemetry([]);

    // Re-entry only happens on the direct path, where onEngagement (and the
    // blur it triggers) runs synchronously inside record(). On the message path
    // onEngagement fires after the pick's engagement has shipped and cleared the
    // session's start info, so the blur's abandonment record() runs un-nested
    // and is a no-op; either way no abandonment is recorded.
    if (!UrlbarPrefs.get("ipc.chromeMessagePassing")) {
      Assert.ok(recordReentered, "`record()` was re-entered");
    }
    Assert.equal(engagementSpy.callCount, 1, "`onEngagement` was invoked once");
  });
});
