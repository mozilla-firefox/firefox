const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

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

async function assertTelemetry(expected_content, expected_parent) {
  let snapshots;

  info(
    `Waiting for ${expected_content?.length ?? 0} content events and ` +
      `${expected_parent?.length ?? 0} parent events`
  );

  await TestUtils.waitForCondition(
    () => {
      snapshots = Services.telemetry.snapshotEvents(
        Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
        false
      );

      return (
        (snapshots.parent?.length ?? 0) >= (expected_parent?.length ?? 0) &&
        (snapshots.content?.length ?? 0) >= (expected_content?.length ?? 0)
      );
    },
    "Wait for telemetry to be collected",
    100,
    100
  );

  info(JSON.stringify(snapshots, null, 2));

  if (expected_content !== undefined) {
    expected_content = expected_content.map(
      ([category, method, object, value, extra]) => {
        return { category, method, object, value, extra };
      }
    );

    let clear = expected_parent === undefined;

    TelemetryTestUtils.assertEvents(
      expected_content,
      {
        category: "creditcard",
      },
      { clear, process: "content" }
    );
  }

  if (expected_parent !== undefined) {
    expected_parent = expected_parent.map(
      ([category, method, object, value, extra]) => {
        return { category, method, object, value, extra };
      }
    );
    TelemetryTestUtils.assertEvents(
      expected_parent,
      {
        category: "creditcard",
      },
      { process: "parent" }
    );
  }
}

async function assertHistogram(histogramId, expectedNonZeroRanges) {
  let actualNonZeroRanges = {};
  await TestUtils.waitForCondition(
    () => {
      const snapshot = Services.telemetry
        .getHistogramById(histogramId)
        .snapshot();
      // Compute the actual ranges in the format { range1: value1, range2: value2 }.
      for (let [range, value] of Object.entries(snapshot.values)) {
        if (value > 0) {
          actualNonZeroRanges[range] = value;
        }
      }

      return (
        JSON.stringify(actualNonZeroRanges) ==
        JSON.stringify(expectedNonZeroRanges)
      );
    },
    "Wait for telemetry to be collected",
    100,
    100
  );

  Assert.equal(
    JSON.stringify(actualNonZeroRanges),
    JSON.stringify(expectedNonZeroRanges)
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
      focusSelector: "#cc-number",
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

async function clearTelemetry(histogramId) {
  Services.telemetry.clearEvents();
  if (histogramId) {
    Services.telemetry.getHistogramById(histogramId).clear();
  }
  await clearGleanTelemetry();
}

/**
 * Sets up a telemetry task and returns an async cleanup function
 */
async function setupTask(prefEnv, ...itemsToStore) {
  const itemCount = itemsToStore.length;

  if (prefEnv) {
    await SpecialPowers.pushPrefEnv(prefEnv);
  }

  clearTelemetry();

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
