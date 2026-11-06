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

function send_bio_enrollment_command(state) {
  let msg = JSON.stringify({
    type: "bio-enrollment-update",
    result: state,
  });
  Services.obs.notifyObservers(null, "about-webauthn-prompt", msg);
}

function check_bio_buttons_disabled(disabled) {
  let buttons = Array.from(doc.getElementsByClassName("bio-enrollment-button"));
  buttons.forEach(button => {
    is(button.disabled, disabled);
  });
  return buttons;
}

function check_tab_buttons_aria(button_id) {
  let button = doc.getElementById(button_id);
  is(button.role, "tab", button_id + " in the sidebar is a tab");
  ok(
    button.hasAttribute("aria-controls"),
    button_id + " in the sidebar is a tab with an assigned tablist"
  );
  ok(
    button.hasAttribute("aria-selected"),
    button_id + " in the sidebar is a tab that can be or is selected"
  );
  ok(
    button.hasAttribute("tabindex"),
    button_id + " in the sidebar is a tab with keyboard focusability provided"
  );
}

async function send_authinfo_and_open_bio_section(ops) {
  reset_about_page(doc);
  send_auth_info_and_check_categories(doc, ops);

  ["pin-tab-button", "credentials-tab-button"].forEach(button_id => {
    let button = doc.getElementById(button_id);
    is(button.style.display, "none", button_id + " in the sidebar not hidden");
    check_tab_buttons_aria(button_id);
  });

  if (ops.bioEnroll !== null || ops.userVerificationMgmtPreview !== null) {
    let bio_enrollments_tab_button = doc.getElementById(
      "bio-enrollments-tab-button"
    );
    // Check if bio enrollment section is visible
    isnot(
      bio_enrollments_tab_button.style.display,
      "none",
      "bio enrollment button in the sidebar not visible"
    );
    check_tab_buttons_aria("bio-enrollments-tab-button");

    // Click the section and wait for it to open
    let bio_enrollment_section = doc.getElementById("bio-enrollment-section");
    bio_enrollments_tab_button.click();
    ok(
      BrowserTestUtils.isVisible(bio_enrollment_section),
      "bio enrollment section not visible"
    );
    is(
      bio_enrollments_tab_button.getAttribute("selected"),
      "true",
      "bio enrollment section button not selected"
    );
  }
}

add_task(async function bio_enrollment_not_supported() {
  send_authinfo_and_open_bio_section({
    bioEnroll: null,
    userVerificationMgmtPreview: null,
  });
  let bio_enrollments_tab_button = doc.getElementById(
    "bio-enrollments-tab-button"
  );
  is(
    bio_enrollments_tab_button.style.display,
    "none",
    "bio enrollments button in the sidebar visible"
  );
});

add_task(async function bio_enrollment_supported() {
  // Setting bioEnroll should show the button in the sidebar
  // The function is checking this for us.
  send_authinfo_and_open_bio_section({
    bioEnroll: true,
    userVerificationMgmtPreview: null,
  });
  // Setting Preview should show the button in the sidebar
  send_authinfo_and_open_bio_section({
    bioEnroll: null,
    userVerificationMgmtPreview: true,
  });
  // Setting both should also work
  send_authinfo_and_open_bio_section({
    bioEnroll: true,
    userVerificationMgmtPreview: true,
  });
});

add_task(async function bio_enrollment_empty_list() {
  send_authinfo_and_open_bio_section({ bioEnroll: true });

  let list_bio_enrollments_button = doc.getElementById(
    "list-bio-enrollments-button"
  );
  let add_bio_enrollment_button = doc.getElementById(
    "add-bio-enrollment-button"
  );
  isnot(
    list_bio_enrollments_button.style.display,
    "none",
    "List bio enrollments button in the sidebar not visible"
  );
  isnot(
    add_bio_enrollment_button.style.display,
    "none",
    "Add bio enrollment button in the sidebar not visible"
  );

  // Bio list should initially not be visible
  let bio_enrollment_list = doc.getElementById(
    "bio-enrollment-list-subsection"
  );
  is(bio_enrollment_list.hidden, true, "bio enrollment list visible");

  list_bio_enrollments_button.click();
  is(
    list_bio_enrollments_button.disabled,
    true,
    "List bio enrollments button not disabled while op in progress"
  );
  is(
    add_bio_enrollment_button.disabled,
    true,
    "Add bio enrollment button not disabled while op in progress"
  );

  send_bio_enrollment_command({ EnrollmentList: [] });
  is(
    list_bio_enrollments_button.disabled,
    false,
    "List bio enrollments button disabled"
  );
  is(
    add_bio_enrollment_button.disabled,
    false,
    "Add bio enrollment button disabled"
  );

  is(bio_enrollment_list.hidden, false, "bio enrollments list visible");
  let bio_enrollment_list_empty_label = doc.getElementById(
    "bio-enrollment-list-empty-label"
  );
  is(
    bio_enrollment_list_empty_label.hidden,
    false,
    "bio enrollments list empty label not visible"
  );
});

add_task(async function bio_enrollment_real_data() {
  send_authinfo_and_open_bio_section({ bioEnroll: true });
  let list_bio_enrollments_button = doc.getElementById(
    "list-bio-enrollments-button"
  );
  let add_bio_enrollment_button = doc.getElementById(
    "add-bio-enrollment-button"
  );
  list_bio_enrollments_button.click();
  send_bio_enrollment_command({ EnrollmentList: REAL_DATA });
  is(
    list_bio_enrollments_button.disabled,
    false,
    "List bio enrollments button disabled"
  );
  is(
    add_bio_enrollment_button.disabled,
    false,
    "Add bio enrollment button disabled"
  );
  let bio_enrollment_list = doc.getElementById("bio-enrollment-list");
  is(
    bio_enrollment_list.rows.length,
    2,
    "bio enrollment list table doesn't contain 2 bio enrollments"
  );
  is(bio_enrollment_list.rows[0].cells[0].textContent, "right-middle");
  is(bio_enrollment_list.rows[1].cells[0].textContent, "right-index");
  let buttons = check_bio_buttons_disabled(false);
  // 2 for each bio + 1 for the list button + 1 for the add button + hidden "start enroll"
  is(buttons.length, 5);

  list_bio_enrollments_button.click();
  buttons = check_bio_buttons_disabled(true);
});

add_task(async function bio_enrollment_add_new() {
  send_authinfo_and_open_bio_section({ bioEnroll: true });
  let add_bio_enrollment_button = doc.getElementById(
    "add-bio-enrollment-button"
  );
  add_bio_enrollment_button.click();
  let add_bio_enrollment_section = doc.getElementById(
    "add-bio-enrollment-section"
  );
  ok(
    BrowserTestUtils.isVisible(add_bio_enrollment_section),
    "Add bio enrollment section invisible"
  );
  let enrollment_name = doc.getElementById("enrollment-name");
  enrollment_name.value = "Test123";

  let start_enrollment_button = doc.getElementById("start-enrollment-button");
  start_enrollment_button.click();

  check_bio_buttons_disabled(true);
  send_bio_enrollment_command({
    SampleStatus: ["Ctap2EnrollFeedbackFpGood", 3],
  });

  let enrollment_update = doc.getElementById("enrollment-update");
  is(enrollment_update.children.length, 1);
  check_bio_buttons_disabled(true);

  send_bio_enrollment_command({
    SampleStatus: ["Ctap2EnrollFeedbackFpGood", 2],
  });
  send_bio_enrollment_command({
    SampleStatus: ["Ctap2EnrollFeedbackFpGood", 1],
  });
  send_bio_enrollment_command({
    SampleStatus: ["Ctap2EnrollFeedbackFpGood", 0],
  });
  is(enrollment_update.children.length, 4);
  check_bio_buttons_disabled(true);
  // We tell the about-page that we still have enrollments (bioEnroll: true).
  // So it will automatically request a refreshed list from the authenticator, disabling
  // all the buttons again.
  send_bio_enrollment_command({ AddSuccess: { options: { bioEnroll: true } } });
  is(enrollment_update.children.length, 0);
  is(enrollment_name.value, "", "Enrollment name field did not get cleared");
  check_bio_buttons_disabled(true);
});

add_task(async function bio_enrollment_delete() {
  send_authinfo_and_open_bio_section({ bioEnroll: true });
  let list_bio_enrollments_button = doc.getElementById(
    "list-bio-enrollments-button"
  );
  list_bio_enrollments_button.click();
  send_bio_enrollment_command({ EnrollmentList: REAL_DATA });
  let buttons = check_bio_buttons_disabled(false);
  buttons[1].click();
  check_bio_buttons_disabled(true);
  // Tell the about page, we still have enrollments
  send_bio_enrollment_command({
    DeleteSuccess: { options: { bioEnroll: true } },
  });
  // This will cause it to automatically request the new list, thus disabling all buttons
  check_bio_buttons_disabled(true);
  send_bio_enrollment_command({ EnrollmentList: REAL_DATA });
  buttons = check_bio_buttons_disabled(false);
  buttons[1].click();

  // Confirmation dialog should pop open
  let confirmation_section = doc.getElementById("confirm-deletion-section");
  ok(
    BrowserTestUtils.isVisible(confirmation_section),
    "Confirmation section did not open."
  );

  // Check if the label displays the correct data
  let confirmation_context = doc.getElementById("confirmation-context");
  // Items get listed in reverse order (inserRow(0) in a loop), so we use [0] instead of [1] here
  is(
    confirmation_context.textContent,
    REAL_DATA[0].template_friendly_name,
    "Deletion context show wrong credential name"
  );
  // Check if the delete-button has the correct context-data
  let cmd = {
    BioEnrollment: {
      DeleteEnrollment: REAL_DATA[0].template_id,
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
    BrowserTestUtils.isHidden(confirmation_section),
    "Confirmation section did not close."
  );
  check_bio_buttons_disabled(false);

  // Click the delete-button again
  buttons[1].click();

  // Now we tell it that we deleted all enrollments (bioEnroll: false)
  send_bio_enrollment_command({
    DeleteSuccess: { options: { bioEnroll: false } },
  });
  // Thus, all buttons should get re-enabled and the subsection should be hidden
  check_bio_buttons_disabled(false);

  let bio_enrollment_subsection = doc.getElementById(
    "bio-enrollment-list-subsection"
  );
  is(bio_enrollment_subsection.hidden, true, "Bio Enrollment List not hidden.");
});

const REAL_DATA = [
  { template_id: [248, 82], template_friendly_name: "right-index" },
  { template_id: [14, 163], template_friendly_name: "right-middle" },
];
