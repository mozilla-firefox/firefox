/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var doc, tab;

add_setup(async function () {
  info("Starting about:webauthn");
  tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:webauthn",
    waitForLoad: true,
  });

  doc = tab.linkedBrowser.contentDocument;
});

registerCleanupFunction(async function () {
  // Close tab.
  await BrowserTestUtils.removeTab(tab);
});

function send_auth_data_and_check(auth_data) {
  Services.obs.notifyObservers(
    null,
    "about-webauthn-prompt",
    JSON.stringify({ type: "selected-device", auth_info: auth_data })
  );

  let info_text = doc.getElementById("info-text-div");
  is(info_text.hidden, true, "Start prompt not hidden");

  let info_section = doc.getElementById("token-info-section");
  ok(BrowserTestUtils.isVisible(info_section), "Info section hidden");
}

add_task(async function multiple_devices() {
  Services.obs.notifyObservers(
    null,
    "about-webauthn-prompt",
    JSON.stringify({ type: "select-device" })
  );

  let info_text = doc.getElementById("info-text-div");
  is(info_text.hidden, false, "Start prompt hidden");
  let field = doc.getElementById("info-text-field");
  is(
    field.getAttribute("data-l10n-id"),
    "about-webauthn-text-select-device",
    "Field does not prompt user to touch device for selection"
  );
});

add_task(async function multiple_devices() {
  send_auth_data_and_check(REAL_AUTH_INFO_1);
  reset_about_page(doc);
  send_auth_data_and_check(REAL_AUTH_INFO_2);
  reset_about_page(doc);
  send_auth_data_and_check(REAL_AUTH_INFO_3);
  reset_about_page(doc);
});

// Yubikey BIO
const REAL_AUTH_INFO_1 = {
  versions: ["U2F_V2", "FIDO_2_0", "FIDO_2_1_PRE", "FIDO_2_1"],
  extensions: [
    "credProtect",
    "hmac-secret",
    "largeBlobKey",
    "credBlob",
    "minPinLength",
  ],
  aaguid: [
    216, 82, 45, 159, 87, 91, 72, 102, 136, 169, 186, 153, 250, 2, 243, 91,
  ],
  options: {
    plat: false,
    rk: true,
    clientPin: true,
    up: true,
    uv: true,
    pinUvAuthToken: true,
    noMcGaPermissionsWithClientPin: null,
    largeBlobs: true,
    ep: null,
    bioEnroll: true,
    userVerificationMgmtPreview: true,
    uvBioEnroll: null,
    authnrCfg: true,
    uvAcfg: null,
    credMgmt: true,
    credentialMgmtPreview: true,
    setMinPINLength: true,
    makeCredUvNotRqd: true,
    alwaysUv: false,
  },
  max_msg_size: 1200,
  pin_protocols: [2, 1],
  max_credential_count_in_list: 8,
  max_credential_id_length: 128,
  transports: ["usb"],
  algorithms: [
    { alg: -7, type: "public-key" },
    { alg: -8, type: "public-key" },
  ],
  max_ser_large_blob_array: 1024,
  force_pin_change: false,
  min_pin_length: 4,
  firmware_version: 328966,
  max_cred_blob_length: 32,
  max_rpids_for_set_min_pin_length: 1,
  preferred_platform_uv_attempts: 3,
  uv_modality: 2,
  certifications: null,
  remaining_discoverable_credentials: 20,
  vendor_prototype_config_commands: null,
};

// Yubikey 5
const REAL_AUTH_INFO_2 = {
  versions: ["U2F_V2", "FIDO_2_0", "FIDO_2_1_PRE"],
  extensions: ["credProtect", "hmac-secret"],
  aaguid: [
    47, 192, 87, 159, 129, 19, 71, 234, 177, 22, 187, 90, 141, 185, 32, 42,
  ],
  options: {
    plat: false,
    rk: true,
    clientPin: true,
    up: true,
    uv: null,
    pinUvAuthToken: null,
    noMcGaPermissionsWithClientPin: null,
    largeBlobs: null,
    ep: null,
    bioEnroll: null,
    userVerificationMgmtPreview: null,
    uvBioEnroll: null,
    authnrCfg: null,
    uvAcfg: null,
    credMgmt: null,
    credentialMgmtPreview: true,
    setMinPINLength: null,
    makeCredUvNotRqd: null,
    alwaysUv: null,
  },
  max_msg_size: 1200,
  pin_protocols: [1],
  max_credential_count_in_list: 8,
  max_credential_id_length: 128,
  transports: ["nfc", "usb"],
  algorithms: [
    { alg: -7, type: "public-key" },
    { alg: -8, type: "public-key" },
  ],
  max_ser_large_blob_array: null,
  force_pin_change: null,
  min_pin_length: null,
  firmware_version: null,
  max_cred_blob_length: null,
  max_rpids_for_set_min_pin_length: null,
  preferred_platform_uv_attempts: null,
  uv_modality: null,
  certifications: null,
  remaining_discoverable_credentials: null,
  vendor_prototype_config_commands: null,
};

// Nitrokey 3
const REAL_AUTH_INFO_3 = {
  versions: ["U2F_V2", "FIDO_2_0", "FIDO_2_1_PRE"],
  extensions: ["credProtect", "hmac-secret"],
  aaguid: [
    47, 192, 87, 159, 129, 19, 71, 234, 177, 22, 187, 90, 141, 185, 32, 42,
  ],
  options: {
    plat: false,
    rk: true,
    clientPin: true,
    up: true,
    uv: null,
    pinUvAuthToken: null,
    noMcGaPermissionsWithClientPin: null,
    largeBlobs: null,
    ep: null,
    bioEnroll: null,
    userVerificationMgmtPreview: null,
    uvBioEnroll: null,
    authnrCfg: null,
    uvAcfg: null,
    credMgmt: null,
    credentialMgmtPreview: true,
    setMinPINLength: null,
    makeCredUvNotRqd: null,
    alwaysUv: null,
  },
  max_msg_size: 1200,
  pin_protocols: [1],
  max_credential_count_in_list: 8,
  max_credential_id_length: 128,
  transports: ["nfc", "usb"],
  algorithms: [
    { alg: -7, type: "public-key" },
    { alg: -8, type: "public-key" },
  ],
  max_ser_large_blob_array: null,
  force_pin_change: null,
  min_pin_length: null,
  firmware_version: null,
  max_cred_blob_length: null,
  max_rpids_for_set_min_pin_length: null,
  preferred_platform_uv_attempts: null,
  uv_modality: null,
  certifications: null,
  remaining_discoverable_credentials: null,
  vendor_prototype_config_commands: null,
};
