const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

const CC_NUM_USES_HISTOGRAM = "CREDITCARD_NUM_USES";

function ccFormArgsv2(method, extra) {
  return ["creditcard", method, "cc_form_v2", undefined, extra];
}

function buildccFormv2Extra(extra, defaultValue) {
  let defaults = {};
  for (const field of [
    "cc_name",
    "cc_number",
    "cc_type",
    "cc_exp",
    "cc_exp_month",
    "cc_exp_year",
  ]) {
    defaults[field] = defaultValue;
  }

  return { ...defaults, ...extra };
}

function assertDetectedCcNumberFieldsCountInGlean(expectedLabeledCounts) {
  expectedLabeledCounts.forEach(expected => {
    const actualCount =
      Glean.creditcard.detectedCcNumberFieldsCount[
        expected.label
      ].testGetValue();
    Assert.equal(
      actualCount,
      expected.count,
      `Expected counter to be ${expected.count} for label ${expected.label} - but got ${actualCount}`
    );
  });
}

function assertFormInteractionEventsInGlean(events) {
  const eventCount = 1;
  let flowIds = new Set();
  events.forEach(event => {
    const expectedName = event[1];
    const expectedExtra = event[4];
    const eventMethod = expectedName.replace(/(_[a-z])/g, c =>
      c[1].toUpperCase()
    );
    const actualEvents =
      Glean.creditcard[eventMethod + "CcFormV2"].testGetValue() ?? [];

    Assert.equal(
      actualEvents.length,
      eventCount,
      `Expected to have ${eventCount} event/s with the name "${expectedName}"`
    );

    if (expectedExtra) {
      let actualExtra = actualEvents[0].extra;
      // We don't want to test the flow_id of the form interaction session just yet
      flowIds.add(actualExtra.value);
      delete actualExtra.value;

      Assert.deepEqual(actualEvents[0].extra, expectedExtra);
    }
  });

  Assert.equal(
    flowIds.size,
    1,
    `All events from the same user interaction session have the same flow id`
  );
}

async function openTabAndUseCreditCard(
  idx,
  creditCard,
  { closeTab = true, submitForm = true } = {}
) {
  let osKeyStoreLoginShown = null;

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    CREDITCARD_FORM_URL
  );
  if (OSKeyStore.canReauth()) {
    osKeyStoreLoginShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
  }
  let browser = tab.linkedBrowser;

  await openPopupOn(browser, "form #cc-name");
  for (let i = 0; i <= idx; i++) {
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  }
  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
  if (osKeyStoreLoginShown) {
    await osKeyStoreLoginShown;
  }
  await waitForAutofill(browser, "#cc-number", creditCard["cc-number"]);

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => {
    setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
  });

  await focusUpdateSubmitForm(
    browser,
    {
      focusSelector: "#cc-name",
      newValues: {},
    },
    submitForm
  );

  // flushing Glean data before tab removal (see Bug 1843178)
  await Services.fog.testFlushAllChildren();

  if (!closeTab) {
    return tab;
  }

  await BrowserTestUtils.removeTab(tab);
  return null;
}

/**
 * Sets up a telemetry task and returns an async cleanup function
 */
async function setupTask(prefEnv, ...itemsToStore) {
  const itemCount = itemsToStore.length;

  if (prefEnv) {
    await SpecialPowers.pushPrefEnv(prefEnv);
  }

  await clearGleanTelemetry();

  if (itemCount) {
    await setStorage(...itemsToStore);
  }

  return async function () {
    if (prefEnv) {
      await SpecialPowers.popPrefEnv();
    }

    if (itemCount) {
      await removeAllRecords();
    }
  };
}
