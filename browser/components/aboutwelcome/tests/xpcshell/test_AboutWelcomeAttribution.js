/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { AboutWelcomeDefaults } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeDefaults.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { AttributionCode } = ChromeUtils.importESModule(
  "moz-src:///browser/components/attribution/AttributionCode.sys.mjs"
);
const { AddonRepository } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/AddonRepository.sys.mjs"
);

const TEST_ATTRIBUTION_DATA = {
  source: "addons.mozilla.org",
  medium: "referral",
  campaign: "non-fx-button",
  content: "rta:iridium%40particlecore.github.io",
};

add_task(async function test_handleAddonInfoNotFound() {
  let sandbox = sinon.createSandbox();
  const stub = sandbox.stub(AttributionCode, "getAttrDataAsync").resolves(null);
  let result = await AboutWelcomeDefaults.getAttributionContent();
  equal(stub.callCount, 1, "Call was made");
  equal(result, null, "No data is returned");

  sandbox.restore();
});

add_task(async function test_UAAttribution() {
  let sandbox = sinon.createSandbox();
  const stub = sandbox
    .stub(AttributionCode, "getAttrDataAsync")
    .resolves({ ua: "test" });
  let result = await AboutWelcomeDefaults.getAttributionContent();
  equal(stub.callCount, 1, "Call was made");
  equal(result.template, undefined, "Template was not returned");
  equal(result.ua, "test", "UA was returned");

  sandbox.restore();
});

add_task(async function test_formatAttributionData() {
  let sandbox = sinon.createSandbox();
  const TEST_ADDON_INFO = {
    sourceURI: { scheme: "https", spec: "https://test.xpi" },
    name: "Test Add-on",
    icons: { 64: "http://test.svg", 128: "http://test-128.svg" },
  };
  sandbox
    .stub(AttributionCode, "getAttrDataAsync")
    .resolves(TEST_ATTRIBUTION_DATA);
  sandbox.stub(AddonRepository, "getAddonsByIDs").resolves([TEST_ADDON_INFO]);
  let result = await AboutWelcomeDefaults.getAttributionContent(
    TEST_ATTRIBUTION_DATA
  );
  equal(AddonRepository.getAddonsByIDs.callCount, 1, "Retrieve addon content");
  equal(result.template, "return_to_amo", "RTAMO template returned");
  equal(result.name, TEST_ADDON_INFO.name, "AddonInfo returned");
  equal(result.iconURL, TEST_ADDON_INFO.icons["128"], "Prefers 128px icon");

  sandbox.restore();
});
