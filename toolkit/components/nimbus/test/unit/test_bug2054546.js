const { PrefUtils } = ChromeUtils.importESModule(
  "moz-src:///toolkit/modules/PrefUtils.sys.mjs"
);

/** @typedef {import("../../lib/Migrations.sys.mjs").Migration} Migration */
/** @typedef {import("../../lib/Migrations.sys.mjs").Phase} Phase */

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({
    ...args,
    migrationState:
      NimbusTestUtils.migrationState.GRADUATED_FIREFOX_LABS_JPEG_XL,
  });
}

function setupPref(pref, defaultBranchValue = null, userBranchValue = null) {
  const originalValue = PrefUtils.getPref(pref, { branch: "default" });

  if (defaultBranchValue !== null) {
    PrefUtils.setPref(pref, defaultBranchValue, { branch: "default" });
  }

  if (userBranchValue !== null) {
    PrefUtils.setPref(pref, userBranchValue, { branch: "user" });
  }

  return () => {
    Services.prefs.clearUserPref(pref);
    PrefUtils.setPref(pref, originalValue, { branch: "default" });
  };
}

/*
 * Tests for captivedetect.canonicalURL
 */

const DETECT_PORTAL_PREF = "captivedetect.canonicalURL";
const CANONICAL_DETECT_PORTAL_VALUE =
  "http://detectportal.firefox.com/canonical.html";
const CANONICAL_DETECT_PORTAL_VALUE_204 =
  "http://detectportal.firefox.com/canonical204.html";
const DETECT_PORTAL_SLUG = "detectportal-fastly-testing";
const DETECT_PORTAL_ROLLOUT_VALUE = "rollout-value"; // Doesn't matter
const DETECT_PORTAL_ROLLOUT_RECIPE =
  NimbusTestUtils.factories.recipe.withFeatureConfig(
    DETECT_PORTAL_SLUG,
    {
      featureId: "prefFlips",
      value: {
        prefs: {
          [DETECT_PORTAL_PREF]: {
            branch: "user",
            value: DETECT_PORTAL_ROLLOUT_VALUE,
          },
        },
      },
    },
    { isRollout: true }
  );

add_task(async function testCaptiveNeverEnrolled() {
  info(
    "Testing the case where the user was never enrolled and canonical.html is the default"
  );

  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE,
    CANONICAL_DETECT_PORTAL_VALUE // This should be a no-op.
  );

  const { cleanup } = await setupTest({});

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );
  Assert.ok(!Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveNeverEnrolled204() {
  info(
    "Testing the case where the user was never enrolled and canonical204.html is the default"
  );

  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE_204,
    // The user was never in the detectportal-fastly-testing rollout, so they've
    // done this themselves.
    CANONICAL_DETECT_PORTAL_VALUE
  );

  const { cleanup } = await setupTest();

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.ok(Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));
  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "user" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveInactive() {
  info(
    "Testing the case where canonical.html is the default and the user is no longer enrolled"
  );
  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE,
    CANONICAL_DETECT_PORTAL_VALUE // This should be a no-op.
  );

  const { cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(DETECT_PORTAL_SLUG, {
          featureId: "no-feature-firefox-desktop",
        }),
        { store, extra: { active: false } }
      );
    },
  });

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );
  Assert.ok(!Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveInactive204() {
  info(
    "Testing the case where canonical204.html is the default and the user is no longer enrolled"
  );
  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE_204,
    CANONICAL_DETECT_PORTAL_VALUE
  );

  const { cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(DETECT_PORTAL_SLUG, {
          featureId: "no-feature-firefox-desktop",
        }),
        { store, extra: { active: false } }
      );
    },
  });

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "user" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.ok(!Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveInactiveCustom() {
  info(
    "Testing the case where canonical.html is the default and the user is no longer enrolled and has a custom captive portal"
  );

  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE,
    "custom-value"
  );

  const { cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(DETECT_PORTAL_SLUG, {
          featureId: "no-feature-firefox-desktop",
        }),
        { store, extra: { active: false } }
      );
    },
  });

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );
  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "user" }),
    "custom-value"
  );

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveInactive204Custom() {
  info(
    "Testing the case where canonical204.html is the default and the user is no longer enrolled and has a custom captive portal"
  );

  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE_204,
    "custom-value"
  );

  const { cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(DETECT_PORTAL_SLUG, {
          featureId: "no-feature-firefox-desktop",
        }),
        { store, extra: { active: false } }
      );
    },
  });

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.equal(
    Services.prefs.getStringPref(DETECT_PORTAL_PREF),
    "custom-value"
  );

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveEnrolled() {
  info(
    "Testing the case where canonical.html is the default and the user is enrolled"
  );

  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE,
    DETECT_PORTAL_ROLLOUT_VALUE
  );

  const { manager, cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(DETECT_PORTAL_ROLLOUT_RECIPE, {
        store,
        extra: {
          prefFlips: {
            originalValues: {
              [DETECT_PORTAL_PREF]: CANONICAL_DETECT_PORTAL_VALUE,
            },
          },
        },
      });
    },
  });

  const enrollment = manager.store.get(DETECT_PORTAL_SLUG);
  Assert.ok(!!enrollment, "Enrollment exists");
  Assert.ok(!enrollment.active, "Enrollment inactive");
  Assert.equal(enrollment.unenrollReason, "migration");

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );
  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "user" }),
    CANONICAL_DETECT_PORTAL_VALUE
  );
  Assert.ok(!Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));

  await cleanup();
  cleanupPref();
});

add_task(async function testCaptiveEnrolled204() {
  info(
    "Testing the case where canonical204.html is the default and the user enrolled with 200 was the default"
  );
  const cleanupPref = setupPref(
    DETECT_PORTAL_PREF,
    CANONICAL_DETECT_PORTAL_VALUE_204,
    CANONICAL_DETECT_PORTAL_VALUE
  );

  const { manager, cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(DETECT_PORTAL_ROLLOUT_RECIPE, {
        store,
        extra: {
          prefFlips: {
            originalValues: {
              [DETECT_PORTAL_PREF]: CANONICAL_DETECT_PORTAL_VALUE,
            },
          },
        },
      });
    },
  });

  const enrollment = manager.store.get(DETECT_PORTAL_SLUG);
  Assert.ok(!!enrollment, "Enrollment exists");
  Assert.ok(!enrollment.active, "Enrollment inactive");
  Assert.equal(enrollment.unenrollReason, "migration");

  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "default" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.equal(
    PrefUtils.getPref(DETECT_PORTAL_PREF, { branch: "user" }),
    CANONICAL_DETECT_PORTAL_VALUE_204
  );
  Assert.ok(!Services.prefs.prefHasUserValue(DETECT_PORTAL_PREF));

  await cleanup();
  cleanupPref();
});

/*
 * Test for browser.newtabpage.activity-stream.nova.enabled
 */

const HNT_NOVA_PREF = "browser.newtabpage.activity-stream.nova.enabled";
const HNT_NOVA_ROLLOUT_SLUG =
  "hnt-nova-pref-flip-rollout-content-market-widget-exp-cohort";
const HNT_NOVA_RESTORE_SLUG = "hnt-nova-pref-flip-restore";
const HNT_NOVA_RESTORE_RECIPE =
  NimbusTestUtils.factories.recipe.withFeatureConfig(
    HNT_NOVA_RESTORE_SLUG,
    {
      featureId: "prefFlips",
      value: {
        prefs: {
          [HNT_NOVA_PREF]: {
            branch: "user",
            value: true,
          },
        },
      },
    },
    { isRollout: true }
  );

add_task(async function testNovaEnrolled() {
  async function doTest(enrolledInRestore, restoreActive = null) {
    info(
      `Testing HNT nova restore with enrolledInRestore=${enrolledInRestore} restoreActive=${restoreActive}`
    );
    const cleanupPref = setupPref(
      HNT_NOVA_PREF,
      null,
      enrolledInRestore && restoreActive
    );
    const { manager, cleanup } = await setupTest({
      populateStore: store => {
        // The user will not still be enrolled in the original rollout.
        NimbusTestUtils.addEnrollmentForRecipe(
          NimbusTestUtils.factories.recipe.withFeatureConfig(
            HNT_NOVA_ROLLOUT_SLUG,
            { featureId: "no-feature-firefox-desktop" },
            { isRollout: true }
          ),
          {
            store,
            extra: { active: false },
          }
        );

        if (enrolledInRestore) {
          NimbusTestUtils.addEnrollmentForRecipe(HNT_NOVA_RESTORE_RECIPE, {
            store,
            extra: {
              // The original value doesn't matter in practice since we're going
              // to unenroll before the PrefFlipsFeature is initialized.
              prefFlips: { originalValues: { [HNT_NOVA_PREF]: false } },

              ...(restoreActive
                ? { active: true }
                : {
                    active: false,
                    unenrollReason: "unknown",
                  }),
            },
          });
        }
      },
    });

    const enrollment = manager.store.get(HNT_NOVA_RESTORE_SLUG);
    if (enrolledInRestore) {
      Assert.ok(!!enrollment, "Enrollment exists");
      Assert.ok(!enrollment.active, "Enrollment inactive");
      Assert.equal(
        enrollment.unenrollReason,
        restoreActive ? "migration" : "unknown"
      );
    } else {
      Assert.equal(typeof enrollment, "undefined", "Enrollment does not exist");
    }

    Assert.equal(PrefUtils.getPref(HNT_NOVA_PREF, { branch: "default" }), true);
    Assert.ok(!Services.prefs.prefHasUserValue(HNT_NOVA_PREF));

    await cleanup();
    cleanupPref();
  }

  await doTest(false);
  for (const restoreActive of [true, false]) {
    await doTest(true, restoreActive);
  }
});

add_task(async function testNovaEnrolledAfterMigration() {
  info(
    `Testing enrolling in the HNT nova mitigation after the migration has run`
  );

  const { manager, cleanup } = await setupTest({
    secureExperiments: [HNT_NOVA_RESTORE_RECIPE],
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(
          HNT_NOVA_PREF,
          { featureId: "no-feature-firefox-desktop" },
          { isRollout: true }
        ),
        {
          store,
          active: false,
        }
      );
    },
  });

  const enrollment = manager.store.get(HNT_NOVA_RESTORE_SLUG);
  Assert.ok(!!enrollment, "enrollment exists");
  Assert.ok(enrollment.active, "enrollment is active");
  Assert.equal(
    enrollment.prefFlips.originalValues[HNT_NOVA_PREF],
    null,
    "Correctly determined original value"
  );

  Assert.equal(
    PrefUtils.getPref(HNT_NOVA_PREF),
    true,
    "pref true while enrolled"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(HNT_NOVA_PREF),
    "pref has no user branch value"
  );

  manager.unenroll(HNT_NOVA_RESTORE_SLUG, "test");

  Assert.equal(
    PrefUtils.getPref(HNT_NOVA_PREF),
    true,
    "pref still true after unenrollment"
  );

  await cleanup();
});

/*
 * Tests for network.cookie.CHIPS.enabled
 */

const CHIPS_PREF = "network.cookie.CHIPS.enabled";
const CHIPS_RESTORE_SLUG = "restore-chips-133";
const CHIPS_RESTORE_RECIPE = NimbusTestUtils.factories.recipe.withFeatureConfig(
  CHIPS_RESTORE_SLUG,
  {
    featureId: "prefFlips",
    value: {
      prefs: {
        [CHIPS_PREF]: {
          branch: "user",
          value: true,
        },
      },
    },
  },
  { isRollout: true }
);

add_task(async function testChipsNeverEnrolled() {
  async function doTest(userBranchValue) {
    info(
      `Testing CHIPS restore when user never enrolled and pref is ${userBranchValue}`
    );

    const cleanupPref = setupPref(CHIPS_PREF, null, userBranchValue);
    const { cleanup } = await setupTest();

    Assert.equal(
      PrefUtils.getPref(CHIPS_PREF),
      true,
      "pref enabled after migration"
    );

    await cleanup();
    cleanupPref();
  }

  for (const userBranchValue of [false, true]) {
    await doTest(userBranchValue);
  }
});

add_task(async function testChipsEnrolled() {
  async function doTest(restoreActive) {
    info(
      `Testing CHIPS restore restore when enrolled in CHIPS restore rollout with restoreActive=${restoreActive}`
    );

    const cleanupPref = setupPref(CHIPS_PREF, null, restoreActive);
    const { manager, cleanup } = await setupTest({
      populateStore: store => {
        NimbusTestUtils.addEnrollmentForRecipe(CHIPS_RESTORE_RECIPE, {
          store,
          extra: {
            prefFlips: { originalValues: { [CHIPS_PREF]: false } },
            ...(restoreActive
              ? { active: true }
              : {
                  active: false,
                  unenrollReason: "unknown",
                }),
          },
        });
      },
    });

    const enrollment = manager.store.get(CHIPS_RESTORE_SLUG);
    Assert.ok(!!enrollment, "Enrollment exists");
    Assert.ok(!enrollment.active, "Enrollment inactive");
    Assert.equal(
      enrollment.unenrollReason,
      restoreActive ? "migration" : "unknown"
    );

    Assert.equal(PrefUtils.getPref(CHIPS_PREF, { branch: "default" }), true);
    Assert.ok(!Services.prefs.prefHasUserValue(CHIPS_PREF));

    await cleanup();
    cleanupPref();
  }

  for (const restoreActive of [false, true]) {
    await doTest(restoreActive);
  }
});

add_task(async function testChipsEnrolledAfterMigration() {
  // This test varies over the user branch value because we are executing the
  // migration on all users because we cannot determine if they were enrolled in
  // the original rollout.
  async function doTest(userBranchValue) {
    info(
      `Testing enrolling in the CHIPS mitigation after the migration has run with userBranchValue=${userBranchValue}`
    );

    const cleanupPref = setupPref(CHIPS_PREF, null, userBranchValue);
    const { manager, cleanup } = await setupTest({
      secureExperiments: [CHIPS_RESTORE_RECIPE],
    });

    const enrollment = manager.store.get(CHIPS_RESTORE_SLUG);
    Assert.ok(!!enrollment, "enrollment exists");
    Assert.ok(enrollment.active, "enrollment is active");
    Assert.equal(
      enrollment.prefFlips.originalValues[CHIPS_PREF],
      null,
      "Correctly determined original value"
    );

    Assert.equal(
      PrefUtils.getPref(CHIPS_PREF),
      true,
      "pref true while enrolled"
    );
    Assert.ok(
      !Services.prefs.prefHasUserValue(CHIPS_PREF),
      "pref has no user branch value"
    );

    manager.unenroll(CHIPS_RESTORE_SLUG, "test");

    Assert.equal(
      PrefUtils.getPref(CHIPS_PREF),
      true,
      "pref still true after unenrollment"
    );

    await cleanup();
    cleanupPref();
  }

  for (const userBranchValue of [false, true]) {
    await doTest(userBranchValue);
  }
});
