/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const COLLECTION_NAME = "fingerprinting-protection-overrides";

// The javascript bitwise operator only support 32bits. So, we only test
// RFPTargets that is under low 32 bits.
const TARGET_DEFAULT =
  Services.rfp.enabledFingerprintingProtections.getNth32BitSet(0);
const TARGET_PointerEvents = 1 << 2;
const TARGET_CanvasRandomization = 1 << 9;
const TARGET_WindowOuterSize = 1 << 26;
const TARGET_Gamepad = 1 << 24;

const TEST_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "empty.html";

const TEST_ANOTHER_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.net"
  ) + "empty.html";

const TEST_CASES = [
  // Test simple addition.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize",
        firstPartyDomain: "example.org",
      },
    ],
    expects: [
      {
        domain: "example.org",
        overrides: TARGET_DEFAULT | TARGET_WindowOuterSize,
      },
      { domain: "example.com", noEntry: true },
    ],
  },
  // Test simple subtraction
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "-WindowOuterSize",
        firstPartyDomain: "example.org",
      },
    ],
    expects: [
      {
        domain: "example.org",
        overrides: TARGET_DEFAULT & ~TARGET_WindowOuterSize,
      },
      { domain: "example.com", noEntry: true },
    ],
  },
  // Test simple subtraction for default targets.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "-CanvasRandomization",
        firstPartyDomain: "example.org",
      },
    ],
    expects: [
      {
        domain: "example.org",
        overrides: TARGET_DEFAULT & ~TARGET_CanvasRandomization,
      },
      { domain: "example.com", noEntry: true },
    ],
  },
  // Test multiple targets
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "example.org",
      },
    ],
    expects: [
      {
        domain: "example.org",
        overrides:
          (TARGET_DEFAULT | TARGET_WindowOuterSize | TARGET_PointerEvents) &
          ~TARGET_Gamepad,
      },
      { domain: "example.com", noEntry: true },
    ],
  },
  // Test multiple entries
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "example.org",
      },
      {
        id: "2",
        last_modified: 1000000000000001,
        overrides: "+Gamepad",
        firstPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "example.org",
        overrides:
          (TARGET_DEFAULT | TARGET_WindowOuterSize | TARGET_PointerEvents) &
          ~TARGET_Gamepad,
      },
      {
        domain: "example.com",
        overrides: TARGET_DEFAULT | TARGET_Gamepad,
      },
    ],
  },
  // Test an entry with both first-party and third-party domains.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "example.org",
        thirdPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "example.org,example.com",
        overrides:
          (TARGET_DEFAULT | TARGET_WindowOuterSize | TARGET_PointerEvents) &
          ~TARGET_Gamepad,
      },
    ],
  },
  // Test an entry with the first-party set to a wildcard.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "*",
      },
    ],
    expects: [
      {
        domain: "*",
        overrides:
          (TARGET_DEFAULT | TARGET_WindowOuterSize | TARGET_PointerEvents) &
          ~TARGET_Gamepad,
      },
    ],
  },
  // Test a entry with the first-party set to a wildcard and the third-party set
  // to a domain.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "*",
        thirdPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "*,example.com",
        overrides:
          (TARGET_DEFAULT | TARGET_WindowOuterSize | TARGET_PointerEvents) &
          ~TARGET_Gamepad,
      },
    ],
  },
  // Test an entry with all targets disabled using '-AllTargets' but only enable one target.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "-AllTargets,+WindowOuterSize",
        firstPartyDomain: "*",
        thirdPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "*,example.com",
        overrides: TARGET_WindowOuterSize,
      },
    ],
  },
  // Test an entry with all targets disabled using '-AllTargets' but enable some targets.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "-AllTargets,+WindowOuterSize,+Gamepad",
        firstPartyDomain: "*",
        thirdPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "*,example.com",
        overrides: TARGET_WindowOuterSize | TARGET_Gamepad,
      },
    ],
  },
  // Test an invalid entry with only third party domain.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        thirdPartyDomain: "example.com",
      },
    ],
    expects: [
      {
        domain: "example.com",
        noEntry: true,
      },
    ],
  },
  // Test an invalid entry with both wildcards.
  {
    entires: [
      {
        id: "1",
        last_modified: 1000000000000001,
        overrides: "+WindowOuterSize,+PointerEvents,-Gamepad",
        firstPartyDomain: "*",
        thirdPartyDomain: "*",
      },
    ],
    expects: [
      {
        domain: "example.org",
        noEntry: true,
      },
    ],
  },
];

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.fingerprintingProtection.remoteOverrides.testing", true]],
  });

  registerCleanupFunction(() => {
    Services.rfp.cleanAllOverrides();
  });
});

const client = RemoteSettings(COLLECTION_NAME);
const db = client.db;
async function addRemoteOverrides(entries) {
  const promise = promiseObserver("fpp-test:set-overrides-finishes");
  await client.db.clear();
  await client.db.importChanges({}, Date.now(), entries);

  await client.emit("sync", {});
  await promise;
}

add_task(async function test_remote_settings() {
  // Add initial empty record.
  await db.importChanges({}, Date.now(), []);

  for (let test of TEST_CASES) {
    info(`Testing with entry ${JSON.stringify(test.entires)}`);

    await addRemoteOverrides(test.entires);

    ok(true, "Got overrides update");

    for (let expect of test.expects) {
      // Get the addition and subtraction flags for the domain.
      try {
        let overrides = Services.rfp.getFingerprintingOverrides(
          expect.domain + ",0"
        );

        // Verify if the flags are matching to expected values.
        is(
          overrides.getNth32BitSet(0),
          expect.overrides,
          "The override value is correct."
        );
      } catch (e) {
        ok(expect.noEntry, "The override entry doesn't exist.");
      }
    }
  }

  db.clear();
});

add_task(async function test_remote_settings_pref() {
  // Add initial empty record.
  await db.importChanges({}, Date.now(), []);

  for (let test of TEST_CASES) {
    info(`Testing with entry ${JSON.stringify(test.entires)}`);

    // Disable remote overrides
    await SpecialPowers.pushPrefEnv({
      set: [
        ["privacy.fingerprintingProtection.remoteOverrides.enabled", false],
      ],
    });

    await addRemoteOverrides(test.entires);
    ok(true, "Got overrides update");

    for (let expect of test.expects) {
      try {
        // Check for the existance of RFP overrides
        Services.rfp.getFingerprintingOverrides(expect.domain + ",0");
        ok(
          false,
          "This line should never run as the override should not exist and the previous line would throw an exception"
        );
      } catch (e) {
        ok(true, "Received an exception as expected");
      }
    }
  }

  db.clear();
});

add_task(async function test_pref() {
  for (let test of TEST_CASES) {
    info(`Testing with entry ${JSON.stringify(test.entires)}`);

    // Create a promise for waiting the overrides get updated.
    let promise = promiseObserver("fpp-test:set-overrides-finishes");

    // Trigger the fingerprinting overrides update by setting the pref.
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "privacy.fingerprintingProtection.granularOverrides",
          JSON.stringify(test.entires),
        ],
      ],
    });

    await promise;
    ok(true, "Got overrides update");

    for (let expect of test.expects) {
      try {
        // Get the addition and subtraction flags for the domain.
        let overrides = Services.rfp.getFingerprintingOverrides(
          expect.domain + ",0"
        );

        // Verify if the flags are matching to expected values.
        is(
          overrides.getNth32BitSet(0),
          expect.overrides,
          "The override value is correct."
        );
      } catch (e) {
        ok(expect.noEntry, "The override entry doesn't exist.");
      }
    }
  }
});

// Verify if the pref overrides the remote settings.
add_task(async function test_pref_override_remote_settings() {
  // Add initial empty record.
  let db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, Date.now(), []);

  // Trigger a remote settings sync.
  await addRemoteOverrides([
    {
      id: "1",
      last_modified: 1000000000000001,
      overrides: "+WindowOuterSize",
      firstPartyDomain: "example.org",
    },
  ]);

  // Then, setting the pref.
  const promise = promiseObserver("fpp-test:set-overrides-finishes");
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.fingerprintingProtection.granularOverrides",
        JSON.stringify([
          {
            id: "1",
            last_modified: 1000000000000001,
            overrides: "+PointerEvents,-WindowOuterSize,-Gamepad",
            firstPartyDomain: "example.org",
          },
        ]),
      ],
    ],
  });
  await promise;

  // Get the addition and subtraction flags for the domain.
  let overrides = Services.rfp.getFingerprintingOverrides("example.org,0");

  // Verify if the flags are matching to the pref settings.
  is(
    overrides.getNth32BitSet(0),
    (TARGET_DEFAULT | TARGET_PointerEvents) &
      ~TARGET_Gamepad &
      ~TARGET_WindowOuterSize,
    "The override addition value is correct."
  );

  db.clear();
});

// Verify if the pref overrides the remote settings (again).
add_task(async function test_pref_override_remote_settings2() {
  // Add initial empty record.
  let db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, Date.now(), []);

  // Trigger a remote settings sync.
  await addRemoteOverrides([
    {
      id: "1",
      last_modified: 1000000000000001,
      overrides: "+PointerEvents,+Gamepad",
      firstPartyDomain: "example.org",
    },
  ]);

  // Then, setting the pref.
  const promise = promiseObserver("fpp-test:set-overrides-finishes");
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.fingerprintingProtection.granularOverrides",
        JSON.stringify([
          {
            id: "1",
            last_modified: 1000000000000001,
            overrides: "+WindowOuterSize",
            firstPartyDomain: "example.org",
          },
        ]),
      ],
    ],
  });
  await promise;

  // Get the addition and subtraction flags for the domain.
  let overrides = Services.rfp
    .getFingerprintingOverrides("example.org,0")
    .getNth32BitSet(0);

  // Verify if the flags are matching to the pref settings.
  is(
    overrides & TARGET_PointerEvents,
    0,
    "The override addition value should not have TARGET_PointerEvents."
  );

  is(
    overrides & TARGET_Gamepad,
    0,
    "The override addition value should not have TARGET_Gamepad."
  );

  is(
    overrides & TARGET_WindowOuterSize,
    TARGET_WindowOuterSize,
    "The override addition value should have TARGET_WindowOuterSize."
  );

  db.clear();
});

// Bug 1873682 - Verify that a third-party beacon request won't hit the
// assertion in nsRFPService::GetOverriddenFingerprintingSettingsForChannel().
add_task(async function test_beacon_request() {
  // Open an empty page.
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TEST_ANOTHER_PAGE],
    async url => {
      // Create a third-party iframe
      let ifr = content.document.createElement("iframe");

      await new content.Promise(resolve => {
        ifr.onload = resolve;
        ifr.src = url;
        content.document.body.appendChild(ifr);
      });

      await SpecialPowers.spawn(ifr, [url], url => {
        // Sending the beacon request right before the tab navigates away.
        content.addEventListener("unload", _ => {
          let value = ["text"];
          let blob = new Blob(value, {
            type: "application/x-www-form-urlencoded",
          });
          content.navigator.sendBeacon(url, blob);
        });
      });

      // Navigate the tab to another page.
      content.location = url;
    }
  );

  await BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    TEST_ANOTHER_PAGE
  );

  ok(true, "Successfully navigates away.");

  BrowserTestUtils.removeTab(tab);
});

function hasOverrideEntry(domainKey) {
  try {
    Services.rfp.getFingerprintingOverrides(domainKey);
    return true;
  } catch (e) {
    return false;
  }
}

// Setting the granular override prefs synchronously re-imports and populates
// the overrides from the pref observer, so the result can be checked as soon
// as pushPrefEnv resolves.
async function setGranularOverridePrefs(baselineValue, fppValue) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.baselineFingerprintingProtection.granularOverrides", ""],
      ["privacy.fingerprintingProtection.granularOverrides", ""],
    ],
  });
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.baselineFingerprintingProtection.granularOverrides",
        baselineValue,
      ],
      ["privacy.fingerprintingProtection.granularOverrides", fppValue],
    ],
  });
}

// A malformed value in one granular override pref must not prevent the other
// granular override pref from being imported.
add_task(async function test_malformed_granular_pref_is_isolated() {
  const fppEntries = JSON.stringify([
    { firstPartyDomain: "example.info", overrides: "+WindowOuterSize" },
  ]);
  const baselineEntries = JSON.stringify([
    { firstPartyDomain: "example.net", overrides: "+WindowOuterSize" },
  ]);

  for (let malformed of ["not json", "{}", '"a string"']) {
    info(`Malformed baseline pref ${malformed}, valid FPP pref`);
    await setGranularOverridePrefs(malformed, fppEntries);
    ok(
      hasOverrideEntry("example.info,0"),
      "The FPP granular override was imported despite a malformed baseline pref."
    );
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();

    info(`Valid baseline pref, malformed FPP pref ${malformed}`);
    await setGranularOverridePrefs(baselineEntries, malformed);
    ok(
      hasOverrideEntry("example.net,1"),
      "The baseline granular override was imported despite a malformed FPP pref."
    );
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
});

// Verify that granular override entries from the pref are validated against
// the schema: valid entries (including the "*" wildcard, long TLDs and IDN
// TLDs) are imported, and malformed entries are dropped.
add_task(async function test_granular_pref_schema_validation() {
  const valid = [
    { firstPartyDomain: "*", overrides: "+WindowOuterSize" },
    { firstPartyDomain: "example.photography", overrides: "+WindowOuterSize" },
    { firstPartyDomain: "example.xn--p1ai", overrides: "+WindowOuterSize" },
    {
      firstPartyDomain: "example.co.uk",
      thirdPartyDomain: "",
      overrides: "+WindowOuterSize",
    },
  ];
  const invalid = [
    { firstPartyDomain: "bad domain", overrides: "+WindowOuterSize" },
    { firstPartyDomain: "example.com", overrides: "+Window OuterSize" },
    { firstPartyDomain: "example.net" },
    {
      firstPartyDomain: "example.edu",
      thirdPartyDomain: "not a domain",
      overrides: "+WindowOuterSize",
    },
  ];

  await setGranularOverridePrefs("", JSON.stringify([...valid, ...invalid]));

  for (let key of [
    "*,0",
    "example.photography,0",
    "example.xn--p1ai,0",
    "example.co.uk,0",
  ]) {
    ok(hasOverrideEntry(key), `The valid override ${key} was imported.`);
  }
  for (let key of [
    "bad domain,0",
    "example.com,0",
    "example.net,0",
    "example.edu,not a domain,0",
  ]) {
    ok(!hasOverrideEntry(key), `The invalid override ${key} was dropped.`);
  }

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

// Granular overrides are applied on top of the global overrides. Verify that
// they are recomputed when the global overrides prefs change, rather than
// keeping the global set that was in effect when they were imported.
add_task(async function test_granular_overrides_follow_global_overrides() {
  await setGranularOverridePrefs(
    JSON.stringify([
      { firstPartyDomain: "example.net", overrides: "+Gamepad" },
    ]),
    JSON.stringify([
      { firstPartyDomain: "example.info", overrides: "+Gamepad" },
    ])
  );

  let fpp = Services.rfp
    .getFingerprintingOverrides("example.info,0")
    .getNth32BitSet(0);
  is(
    fpp & TARGET_CanvasRandomization,
    TARGET_CanvasRandomization,
    "The FPP granular override starts from the default FPP targets."
  );
  is(fpp & TARGET_Gamepad, TARGET_Gamepad, "The FPP override adds Gamepad.");

  let baseline = Services.rfp
    .getFingerprintingOverrides("example.net,1")
    .getNth32BitSet(0);
  is(
    baseline & TARGET_PointerEvents,
    0,
    "The baseline granular override doesn't include PointerEvents yet."
  );

  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.fingerprintingProtection.overrides", "-CanvasRandomization"],
      ["privacy.baselineFingerprintingProtection.overrides", "+PointerEvents"],
    ],
  });

  fpp = Services.rfp
    .getFingerprintingOverrides("example.info,0")
    .getNth32BitSet(0);
  is(
    fpp & TARGET_CanvasRandomization,
    0,
    "The FPP granular override follows the updated global overrides."
  );
  is(
    fpp & TARGET_Gamepad,
    TARGET_Gamepad,
    "The FPP granular override still adds Gamepad."
  );

  baseline = Services.rfp
    .getFingerprintingOverrides("example.net,1")
    .getNth32BitSet(0);
  is(
    baseline & TARGET_PointerEvents,
    TARGET_PointerEvents,
    "The baseline granular override follows the updated global overrides."
  );
  is(
    baseline & TARGET_Gamepad,
    TARGET_Gamepad,
    "The baseline granular override still adds Gamepad."
  );

  await SpecialPowers.popPrefEnv();

  fpp = Services.rfp
    .getFingerprintingOverrides("example.info,0")
    .getNth32BitSet(0);
  is(
    fpp & TARGET_CanvasRandomization,
    TARGET_CanvasRandomization,
    "The FPP granular override follows the global overrides being reset."
  );

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});
