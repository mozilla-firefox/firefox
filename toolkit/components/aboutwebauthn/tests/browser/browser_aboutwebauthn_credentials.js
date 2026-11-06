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

function send_credential_list(credlist) {
  let num_of_creds = 0;
  credlist.forEach(domain => {
    domain.credentials.forEach(() => {
      num_of_creds += 1;
    });
  });
  let msg = JSON.stringify({
    type: "credential-management-update",
    result: {
      CredentialList: {
        existing_resident_credentials_count: num_of_creds,
        max_possible_remaining_resident_credentials_count: 20,
        credential_list: credlist,
      },
    },
  });
  Services.obs.notifyObservers(null, "about-webauthn-prompt", msg);
}

function check_cred_buttons_disabled(disabled) {
  let buttons = Array.from(doc.getElementsByClassName("credentials-button"));
  buttons.forEach(button => {
    is(button.disabled, disabled);
  });
  return buttons;
}

async function send_authinfo_and_open_cred_section(ops) {
  reset_about_page(doc);
  send_auth_info_and_check_categories(doc, ops);

  ["pin-tab-button", "bio-enrollments-tab-button"].forEach(button_id => {
    let button = doc.getElementById(button_id);
    is(button.style.display, "none", button_id + " in the sidebar not hidden");
  });

  if (ops.credMgmt !== null || ops.credentialMgmtPreview !== null) {
    let credentials_tab_button = doc.getElementById("credentials-tab-button");
    // Check if credentials section is visible
    isnot(
      credentials_tab_button.style.display,
      "none",
      "credentials button in the sidebar not visible"
    );

    // Click the section and wait for it to open
    let credentials_section = doc.getElementById(
      "credential-management-section"
    );
    credentials_tab_button.click();
    ok(
      BrowserTestUtils.isVisible(credentials_section),
      "credentials section not visible"
    );
    is(
      credentials_tab_button.getAttribute("selected"),
      "true",
      "credentials section button not selected"
    );
  }
}

add_task(async function cred_mgmt_not_supported() {
  // Not setting credMgmt at all should lead to not showing it in the sidebar
  send_authinfo_and_open_cred_section({
    credMgmt: null,
    credentialMgmtPreview: null,
  });
  let credentials_tab_button = doc.getElementById("credentials-tab-button");
  is(
    credentials_tab_button.style.display,
    "none",
    "credentials button in the sidebar visible"
  );
});

add_task(async function cred_mgmt_supported() {
  // Setting credMgmt should show the button in the sidebar
  // The function is checking this for us.
  send_authinfo_and_open_cred_section({
    credMgmt: true,
    credentialMgmtPreview: null,
  });
  // Setting credMgmtPreview should show the button in the sidebar
  send_authinfo_and_open_cred_section({
    credMgmt: null,
    credentialMgmtPreview: true,
  });
  // Setting both should also work
  send_authinfo_and_open_cred_section({
    credMgmt: true,
    credentialMgmtPreview: true,
  });
});

add_task(async function cred_mgmt_empty_list() {
  send_authinfo_and_open_cred_section({ credMgmt: true });

  let list_credentials_button = doc.getElementById("list-credentials-button");
  isnot(
    list_credentials_button.style.display,
    "none",
    "credentials button in the sidebar not visible"
  );

  // Credential list should initially not be visible
  let credential_list = doc.getElementById("credential-list-subsection");
  is(credential_list.hidden, true, "credentials list visible");

  list_credentials_button.click();
  is(
    list_credentials_button.disabled,
    true,
    "credentials button not disabled while op in progress"
  );

  send_credential_list([]);
  is(list_credentials_button.disabled, false, "credentials button disabled");

  is(credential_list.hidden, false, "credentials list visible");
  let credential_list_empty_label = doc.getElementById(
    "credential-list-empty-label"
  );
  is(
    credential_list_empty_label.hidden,
    false,
    "credentials list empty label not visible"
  );
});

add_task(async function cred_mgmt_real_data() {
  send_authinfo_and_open_cred_section({ credMgmt: true });
  let list_credentials_button = doc.getElementById("list-credentials-button");
  list_credentials_button.click();
  send_credential_list(REAL_DATA);
  is(list_credentials_button.disabled, false, "credentials button disabled");
  let credential_list = doc.getElementById("credential-list");
  is(
    credential_list.rows.length,
    4,
    "credential list table doesn't contain 4 credentials"
  );
  is(credential_list.rows[0].cells[0].textContent, "webauthn.io");
  is(credential_list.rows[0].cells[1].textContent, "fasdfasd");
  is(credential_list.rows[1].cells[0].textContent, "webauthn.io");
  is(credential_list.rows[1].cells[1].textContent, "twetwetw");
  is(credential_list.rows[2].cells[0].textContent, "webauthn.io");
  is(credential_list.rows[2].cells[1].textContent, "hhhhhg");
  is(credential_list.rows[3].cells[0].textContent, "example.com");
  is(credential_list.rows[3].cells[1].textContent, "A. Nother");
  let buttons = check_cred_buttons_disabled(false);
  // 4 for each cred + 1 for the list button
  is(buttons.length, 5);

  list_credentials_button.click();
  buttons = check_cred_buttons_disabled(true);
  send_credential_list(REAL_DATA);
  buttons[2].click();
  check_cred_buttons_disabled(true);

  // Confirmation section should now be open
  let credential_section = doc.getElementById("credential-management-section");
  ok(
    BrowserTestUtils.isHidden(credential_section),
    "credential section still visible"
  );
  let confirmation_section = doc.getElementById("confirm-deletion-section");
  ok(
    BrowserTestUtils.isVisible(confirmation_section),
    "Confirmation section did not open."
  );

  // Check if the label displays the correct data
  let confirmation_context = doc.getElementById("confirmation-context");
  is(
    confirmation_context.textContent,
    "webauthn.io - hhhhhg",
    "Deletion context show wrong credential name"
  );
  // Check if the delete-button has the correct context-data
  let cmd = {
    CredentialManagement: {
      DeleteCredential: REAL_DATA[1].credentials[0].credential_id,
    },
  };
  is(
    confirmation_context.getAttribute("data-ctap-command"),
    JSON.stringify(cmd),
    "Confirm-button has the wrong context data"
  );

  let cancel_button = doc.getElementById("cancel-confirmation-button");
  cancel_button.click();
  ok(
    BrowserTestUtils.isVisible(credential_section),
    "credential section still visible"
  );
  ok(
    BrowserTestUtils.isHidden(confirmation_section),
    "Confirmation section did not open."
  );
  check_cred_buttons_disabled(false);
});

const REAL_DATA = [
  {
    rp: { id: "example.com" },
    rp_id_hash: [
      163, 121, 166, 246, 238, 175, 185, 165, 94, 55, 140, 17, 128, 52, 226,
      117, 30, 104, 47, 171, 159, 45, 48, 171, 19, 210, 18, 85, 134, 206, 25,
      71,
    ],
    credentials: [
      {
        user: {
          id: [65, 46, 32, 78, 111, 116, 104, 101, 114],
          name: "A. Nother",
        },
        credential_id: {
          id: [
            195, 239, 221, 151, 76, 77, 255, 242, 217, 50, 87, 144, 238, 79,
            199, 120, 234, 148, 142, 69, 163, 133, 189, 254, 74, 138, 119, 140,
            197, 171, 36, 215, 191, 176, 36, 111, 113, 158, 204, 147, 101, 200,
            20, 239, 191, 174, 51, 15,
          ],
          type: "public-key",
        },
        public_key: {
          1: 2,
          3: -7,
          "-1": 1,
          "-2": [
            195, 239, 221, 151, 76, 77, 255, 242, 217, 50, 87, 144, 238, 235,
            230, 51, 155, 142, 121, 60, 136, 63, 80, 184, 41, 238, 217, 61, 1,
            206, 253, 141,
          ],
          "-3": [
            15, 81, 111, 204, 199, 48, 18, 121, 134, 243, 26, 49, 6, 244, 25,
            156, 188, 71, 245, 122, 93, 47, 218, 235, 25, 222, 191, 116, 20, 14,
            195, 114,
          ],
        },
        cred_protect: 1,
        large_blob_key: [
          223, 32, 77, 171, 223, 133, 38, 175, 229, 40, 85, 216, 203, 79, 194,
          223, 32, 191, 119, 241, 115, 6, 101, 180, 92, 194, 208, 193, 181, 163,
          164, 64,
        ],
      },
    ],
  },
  {
    rp: { id: "webauthn.io" },
    rp_id_hash: [
      116, 166, 234, 146, 19, 201, 156, 47, 116, 178, 36, 146, 179, 32, 207, 64,
      38, 42, 148, 193, 169, 80, 160, 57, 127, 41, 37, 11, 96, 132, 30, 240,
    ],
    credentials: [
      {
        user: { id: [97, 71, 104, 111, 97, 71, 104, 110], name: "hhhhhg" },
        credential_id: {
          id: [
            64, 132, 129, 5, 185, 62, 86, 253, 199, 113, 219, 14, 207, 113, 145,
            78, 177, 198, 130, 217, 122, 105, 225, 111, 32, 227, 237, 209, 6,
            220, 202, 234, 144, 227, 246, 42, 73, 68, 37, 142, 95, 139, 224, 36,
            156, 168, 118, 181,
          ],
          type: "public-key",
        },
        public_key: {
          1: 2,
          3: -7,
          "-1": 1,
          "-2": [
            64, 132, 129, 5, 185, 62, 86, 253, 199, 113, 219, 14, 207, 22, 10,
            241, 230, 152, 5, 204, 35, 94, 22, 191, 213, 2, 247, 220, 227, 62,
            76, 182,
          ],
          "-3": [
            13, 30, 30, 149, 170, 118, 78, 115, 101, 218, 245, 52, 154, 242, 67,
            146, 17, 184, 112, 225, 51, 47, 242, 157, 195, 80, 76, 101, 147,
            161, 3, 185,
          ],
        },
        cred_protect: 1,
        large_blob_key: [
          10, 67, 27, 233, 8, 115, 69, 191, 105, 213, 77, 123, 210, 118, 193,
          234, 3, 12, 234, 228, 215, 106, 24, 228, 102, 247, 255, 156, 99, 196,
          215, 230,
        ],
      },
      {
        user: {
          id: [100, 72, 100, 108, 100, 72, 100, 108, 100, 72, 99],
          name: "twetwetw",
        },
        credential_id: {
          id: [
            29, 8, 25, 57, 66, 234, 22, 27, 227, 141, 77, 93, 233, 234, 251, 61,
            100, 199, 176, 97, 112, 48, 172, 118, 145, 0, 156, 76, 156, 215, 18,
            25, 118, 32, 241, 127, 13, 177, 249, 101, 26, 209, 142, 116, 74, 95,
            117, 29,
          ],
          type: "public-key",
        },
        public_key: {
          1: 2,
          3: -7,
          "-1": 1,
          "-2": [
            29, 8, 25, 57, 66, 234, 22, 27, 227, 141, 77, 93, 233, 154, 113,
            177, 251, 161, 54, 76, 150, 15, 6, 143, 117, 214, 232, 215, 118, 41,
            116, 19,
          ],
          "-3": [
            201, 6, 43, 178, 3, 249, 175, 123, 149, 81, 127, 20, 116, 152, 238,
            84, 52, 113, 3, 165, 176, 105, 200, 137, 209, 0, 141, 50, 42, 192,
            174, 26,
          ],
        },
        cred_protect: 1,
        large_blob_key: [
          125, 175, 155, 1, 14, 247, 182, 241, 234, 66, 115, 236, 200, 223, 176,
          88, 88, 88, 202, 173, 147, 217, 9, 193, 114, 7, 29, 169, 224, 179,
          187, 188,
        ],
      },
      {
        user: {
          id: [90, 109, 70, 122, 90, 71, 90, 104, 99, 50, 81],
          name: "fasdfasd",
        },
        credential_id: {
          id: [
            58, 174, 92, 116, 17, 108, 28, 203, 233, 192, 182, 60, 80, 236, 133,
            196, 98, 32, 103, 53, 107, 48, 46, 236, 228, 166, 21, 33, 228, 75,
            85, 191, 71, 18, 214, 177, 56, 254, 89, 28, 187, 220, 241, 7, 21,
            11, 45, 151,
          ],
          type: "public-key",
        },
        public_key: {
          1: 2,
          3: -7,
          "-1": 1,
          "-2": [
            58, 174, 92, 116, 17, 108, 28, 203, 233, 192, 182, 60, 80, 111, 150,
            192, 102, 255, 211, 156, 5, 186, 29, 105, 154, 79, 14, 2, 106, 159,
            57, 156,
          ],
          "-3": [
            182, 164, 251, 221, 237, 36, 239, 109, 146, 184, 146, 29, 143, 16,
            35, 188, 84, 148, 247, 83, 181, 40, 88, 111, 245, 13, 254, 206, 242,
            164, 234, 159,
          ],
        },
        cred_protect: 1,
        large_blob_key: [
          113, 154, 217, 69, 45, 108, 115, 20, 104, 43, 214, 120, 253, 93, 223,
          204, 125, 234, 220, 148, 98, 118, 98, 157, 175, 41, 154, 97, 87, 233,
          208, 171,
        ],
      },
    ],
  },
];
