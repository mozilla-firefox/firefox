"use strict";

const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.addresses.experiments.enabled", false],
      ["extensions.formautofill.addresses.supportedCountries", "FR"],
    ],
  });
});

add_task(async function test_address_autofill_feature_enabled() {
  await ExperimentAPI.ready();

  const cleanupExperiment = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "address-autofill-feature",
    value: { status: true },
  });

  is(
    NimbusFeatures["address-autofill-feature"].getVariable("status"),
    true,
    "Nimbus feature should be enabled"
  );

  is(
    FormAutofill.isAutofillAddressesAvailable,
    true,
    "Address autofill should be available when feature is enabled in nimbus."
  );

  await cleanupExperiment();
});

add_task(async function test_address_autofill_feature_disabled() {
  await ExperimentAPI.ready();

  const cleanupExperiment = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "address-autofill-feature",
    value: { status: false },
  });

  NimbusFeatures["address-autofill-feature"].recordExposureEvent();

  is(
    NimbusFeatures["address-autofill-feature"].getVariable("status"),
    false,
    "Nimbus feature shouldn't be enabled"
  );

  is(
    FormAutofill.isAutofillAddressesAvailable,
    AppConstants.NIGHTLY_BUILD,
    "Address autofill shouldn't be available when feature is off in nimbus."
  );

  await cleanupExperiment();
});
