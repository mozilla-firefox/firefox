/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HOMEPAGE_PREF = "browser.startup.homepage";
const PRIVACY_SEGMENTATION_PREF = "browser.dataFeatureRecommendations.enabled";
const MESSAGING_ACTION_PREF = "special-message-testpref";
const TIMESTAMP_PREF = "termsofuse.acceptedDate";
const FIRST_ACCEPTED_TIMESTAMP_PREF = "termsofuse.firstAcceptedDate";

const PREFS_TO_CLEAR = [
  HOMEPAGE_PREF,
  PRIVACY_SEGMENTATION_PREF,
  TIMESTAMP_PREF,
  FIRST_ACCEPTED_TIMESTAMP_PREF,
  `messaging-system-action.${MESSAGING_ACTION_PREF}`,
];

add_setup(async function () {
  registerCleanupFunction(async () => {
    PREFS_TO_CLEAR.forEach(pref => Services.prefs.clearUserPref(pref));
  });
});

add_task(async function test_set_privacy_segmentation_pref() {
  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: PRIVACY_SEGMENTATION_PREF,
        value: true,
      },
    },
  };

  Assert.ok(
    !Services.prefs.prefHasUserValue(PRIVACY_SEGMENTATION_PREF),
    "Test setup ok"
  );

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    Services.prefs.getBoolPref(PRIVACY_SEGMENTATION_PREF),
    `${PRIVACY_SEGMENTATION_PREF} pref successfully updated`
  );
});

add_task(async function test_clear_privacy_segmentation_pref() {
  Services.prefs.setBoolPref(PRIVACY_SEGMENTATION_PREF, true);
  Assert.ok(
    Services.prefs.prefHasUserValue(PRIVACY_SEGMENTATION_PREF),
    "Test setup ok"
  );

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: PRIVACY_SEGMENTATION_PREF,
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(PRIVACY_SEGMENTATION_PREF),
    `${PRIVACY_SEGMENTATION_PREF} pref successfully cleared`
  );
});

add_task(async function test_set_homepage_pref() {
  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: HOMEPAGE_PREF,
        value: "https://foo.example.com",
      },
    },
  };

  Assert.ok(!Services.prefs.prefHasUserValue(HOMEPAGE_PREF), "Test setup ok");

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getStringPref(HOMEPAGE_PREF),
    "https://foo.example.com",
    `${HOMEPAGE_PREF} pref successfully updated`
  );
});

add_task(async function test_clear_homepage_pref() {
  Services.prefs.setStringPref(HOMEPAGE_PREF, "https://www.example.com");
  Assert.ok(Services.prefs.prefHasUserValue(HOMEPAGE_PREF), "Test setup ok");

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: HOMEPAGE_PREF,
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(HOMEPAGE_PREF),
    `${HOMEPAGE_PREF} pref successfully updated`
  );
});

// Set a pref not listed in "allowed prefs"
add_task(async function test_set_messaging_system_pref() {
  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: MESSAGING_ACTION_PREF,
        value: true,
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getBoolPref(
      `messaging-system-action.${MESSAGING_ACTION_PREF}`
    ),
    true,
    `messaging-system-action.${MESSAGING_ACTION_PREF} pref successfully updated to correct value`
  );
});

// Clear a pref not listed in "allowed prefs" that was initially set by
// the SET_PREF special messaging action
add_task(async function test_clear_messaging_system_pref() {
  Services.prefs.setBoolPref(
    `messaging-system-action.${MESSAGING_ACTION_PREF}`,
    true
  );
  Assert.ok(
    Services.prefs.prefHasUserValue(
      `messaging-system-action.${MESSAGING_ACTION_PREF}`
    ),
    "Test setup ok"
  );

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: MESSAGING_ACTION_PREF,
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(
      `messaging-system-action.${MESSAGING_ACTION_PREF}`
    ),
    `messaging-system-action.${MESSAGING_ACTION_PREF} pref successfully cleared`
  );
});

add_task(async function test_set_pref_to_timestamp_string() {
  Assert.ok(!Services.prefs.prefHasUserValue(TIMESTAMP_PREF), "Test setup ok");

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: TIMESTAMP_PREF,
        value: { timestamp: true },
      },
    },
  };

  const before = Date.now();
  await SMATestUtils.executeAndValidateAction(action);
  const after = Date.now();

  Assert.ok(
    Services.prefs.prefHasUserValue(TIMESTAMP_PREF),
    "Timestamp pref successfully set"
  );

  const value = Services.prefs.getStringPref(TIMESTAMP_PREF);
  const timestamp = parseInt(value, 10);
  Assert.ok(!isNaN(timestamp), "Timestamp value is a valid number");
  Assert.ok(
    timestamp >= before && timestamp <= after,
    "Timestamp is within the expected time range"
  );
});

add_task(async function test_set_messaging_system_pref_onImpression() {
  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: MESSAGING_ACTION_PREF,
        value: true,
      },
      onImpression: true,
    },
  };

  Assert.ok(
    !Services.prefs.prefHasUserValue(
      `messaging-system-action.${MESSAGING_ACTION_PREF}`
    ),
    "Test setup ok"
  );

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getBoolPref(
      `messaging-system-action.${MESSAGING_ACTION_PREF}`
    ),
    true,
    "messaging-system-action.<name> pref successfully set on impression"
  );

  Services.prefs.clearUserPref(
    `messaging-system-action.${MESSAGING_ACTION_PREF}`
  );
});

add_task(async function test_set_unallowed_pref_onImpression_namespaced() {
  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: PRIVACY_SEGMENTATION_PREF,
        value: true,
      },
      onImpression: true,
    },
  };

  Assert.ok(
    !Services.prefs.prefHasUserValue(PRIVACY_SEGMENTATION_PREF),
    "Original pref not set"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(
      `messaging-system-action.${PRIVACY_SEGMENTATION_PREF}`
    ),
    "Namespaced pref not set"
  );

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(PRIVACY_SEGMENTATION_PREF),
    "Original pref still not set"
  );

  Assert.ok(
    Services.prefs.getBoolPref(
      `messaging-system-action.${PRIVACY_SEGMENTATION_PREF}`
    ),
    "Non-allowed existing pref name was namespaced and set on impression"
  );

  Services.prefs.clearUserPref(
    `messaging-system-action.${PRIVACY_SEGMENTATION_PREF}`
  );
});

add_task(async function test_clear_namespaced_pref_onImpression() {
  const namespaced = `messaging-system-action.${MESSAGING_ACTION_PREF}`;

  Services.prefs.setBoolPref(namespaced, true);
  Assert.ok(Services.prefs.prefHasUserValue(namespaced), "Test setup ok");

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: namespaced,
      },
      onImpression: true,
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(namespaced),
    "Namespace pref successfully cleared on impression"
  );
});

add_task(async function test_update_namespaced_timestamp_pref_onImpression() {
  const namespaced = `messaging-system-action.${MESSAGING_ACTION_PREF}`;
  Services.prefs.clearUserPref(namespaced);

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: namespaced,
        value: { timestamp: true },
      },
      onImpression: true,
    },
  };

  await SMATestUtils.executeAndValidateAction(action);
  const first = Services.prefs.getStringPref(namespaced);
  Assert.ok(first, "First timestamp was written");

  // Small delay so the next timestamp will be strictly greater
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(r => setTimeout(r, 5));

  await SMATestUtils.executeAndValidateAction(action);
  const second = Services.prefs.getStringPref(namespaced);
  Assert.ok(second, "Second timestamp was written");
  Assert.greater(
    parseInt(second, 10),
    parseInt(first, 10),
    `Timestamp bumped on re-impression (first=${first}, second=${second})`
  );

  Services.prefs.clearUserPref(namespaced);
});

add_task(async function test_copyFromPref_string() {
  const sourcePrefName = "messaging-system-action.source.string";
  Services.prefs.setStringPref(sourcePrefName, "test");

  const targetPrefName = "messaging-system-action.target.string";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
    Services.prefs.clearUserPref(sourcePrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getStringPref(targetPrefName),
    Services.prefs.getStringPref(sourcePrefName),
    "Copied source string pref value to target pref"
  );
});

add_task(async function test_copyFromPref_bool() {
  const sourcePrefName = "messaging-system-action.source.bool";
  Services.prefs.setBoolPref(sourcePrefName, true);

  const targetPrefName = "messaging-system-action.target.bool";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
    Services.prefs.clearUserPref(sourcePrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getBoolPref(targetPrefName),
    Services.prefs.getBoolPref(sourcePrefName),
    "Copied source bool pref value to target pref"
  );
});

add_task(async function test_copyFromPref_int() {
  const sourcePrefName = "messaging-system-action.source.int";
  Services.prefs.setIntPref(sourcePrefName, 42);

  const targetPrefName = "messaging-system-action.target.int";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
    Services.prefs.clearUserPref(sourcePrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getIntPref(targetPrefName),
    Services.prefs.getIntPref(sourcePrefName),
    "Copied source int pref value to target pref"
  );
});

add_task(async function test_copyFromPref_type_mismatch_throws() {
  const sourcePrefName = "messaging-system-action.source.mismatch.int";
  Services.prefs.setIntPref(sourcePrefName, 7);

  const targetPrefName = "messaging-system-action.target.mismatch.bool";
  Services.prefs.setBoolPref(targetPrefName, true);

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
    Services.prefs.clearUserPref(sourcePrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await Assert.rejects(
    SMATestUtils.executeAndValidateAction(action),
    /SET_PREF\(copyFromPref\).*type/i,
    "Throws when destination type does not match source type"
  );
});

add_task(async function test_copyFromPref_missing_source_throws() {
  const sourcePrefName = "messaging-system-action.source.missing";
  const targetPrefName = "messaging-system-action.target.missing";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await Assert.rejects(
    SMATestUtils.executeAndValidateAction(action),
    /SET_PREF\(copyFromPref\).*(does not exist|invalid|unsupported)/i,
    "Throws when source pref does not exist"
  );

  Assert.equal(
    Services.prefs.getPrefType(targetPrefName),
    Services.prefs.PREF_INVALID,
    "Target pref not created on missing source"
  );
});

add_task(async function test_set_rs_granted_pref() {
  const RS_GRANTED_PREF = "rs.granted.pref";
  MessagingSystemAllowlists._setForTest({ setPref: [RS_GRANTED_PREF] });
  registerCleanupFunction(() => {
    MessagingSystemAllowlists._resetForTest();
    Services.prefs.clearUserPref(RS_GRANTED_PREF);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: RS_GRANTED_PREF,
        value: true,
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    Services.prefs.getBoolPref(RS_GRANTED_PREF),
    "Pref granted only via the Remote Settings allowlist was set under its real name"
  );
});

add_task(async function test_rs_granted_pref_not_honored_onImpression() {
  const RS_GRANTED_PREF = "rs.granted.pref.on.impression";
  MessagingSystemAllowlists._setForTest({ setPref: [RS_GRANTED_PREF] });
  registerCleanupFunction(() => {
    MessagingSystemAllowlists._resetForTest();
    Services.prefs.clearUserPref(`messaging-system-action.${RS_GRANTED_PREF}`);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: RS_GRANTED_PREF,
        value: true,
      },
      onImpression: true,
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.ok(
    !Services.prefs.prefHasUserValue(RS_GRANTED_PREF),
    "Remote Settings allowlist does not apply to onImpression prefs"
  );
  Assert.ok(
    Services.prefs.getBoolPref(`messaging-system-action.${RS_GRANTED_PREF}`),
    "Pref was namespaced instead of set under its real name"
  );
});

add_task(async function test_copyFromPref_overwrite_same_type() {
  const sourcePrefName = "messaging-system-action.source.overwrite.string";
  Services.prefs.setStringPref(sourcePrefName, "new");

  const targetPrefName = "messaging-system-action.target.overwrite.string";
  Services.prefs.setStringPref(targetPrefName, "old");

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(targetPrefName);
    Services.prefs.clearUserPref(sourcePrefName);
  });

  const action = {
    type: "SET_PREF",
    data: {
      pref: {
        name: targetPrefName,
        value: { copyFromPref: sourcePrefName },
      },
    },
  };

  await SMATestUtils.executeAndValidateAction(action);

  Assert.equal(
    Services.prefs.getStringPref(targetPrefName),
    Services.prefs.getStringPref(sourcePrefName),
    "Overwrote existing target value with source value (same type)"
  );
});

add_task(async function test_write_once_pref_not_overwritten() {
  const firstAcceptedDate = "1748736000000";
  Services.prefs.setStringPref(
    FIRST_ACCEPTED_TIMESTAMP_PREF,
    firstAcceptedDate
  );
  Services.prefs.setStringPref(TIMESTAMP_PREF, "1756684800000");

  await SMATestUtils.executeAndValidateAction({
    type: "SET_PREF",
    data: {
      pref: {
        name: FIRST_ACCEPTED_TIMESTAMP_PREF,
        value: { copyFromPref: TIMESTAMP_PREF },
      },
    },
  });

  Assert.equal(
    Services.prefs.getStringPref(FIRST_ACCEPTED_TIMESTAMP_PREF),
    firstAcceptedDate,
    "Write once pref with a non default value not overwritten"
  );
});
