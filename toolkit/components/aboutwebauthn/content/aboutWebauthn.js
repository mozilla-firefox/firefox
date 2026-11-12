/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let AboutWebauthnService = null;

var AboutWebauthnLogger = {
  _topic: "webauthn-logging",
  _initialized: false,

  start() {
    if (this._initialized) {
      return;
    }
    Services.obs.addObserver(this, this._topic);
    this._initialized = true;
  },

  stop() {
    if (!this._initialized) {
      return;
    }
    Services.obs.removeObserver(this, this._topic);
    this._initialized = false;
  },

  observe(_aSubject, _aTopic, aData) {
    let data = JSON.parse(aData);
    let direction = data.direction == "response" ? "<--" : "-->";
    let logarea = document.getElementById("webauthn-log");
    logarea.textContent += `\n${direction} [${data.protocol}] ${data.data}\n`;
  },
};
var AboutWebauthnManagerJS = {
  _topic: "about-webauthn-prompt",
  _initialized: false,
  _l10n: null,
  _bio_l10n: null,
  _curr_data: null,
  _current_tab: "",
  _previous_tab: "",
  _logging: false,

  init() {
    if (this._initialized) {
      return;
    }
    this._l10n = new Localization(["toolkit/about/aboutWebauthn.ftl"], true);
    this._bio_l10n = new Map();
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpGood",
      "about-webauthn-ctap2-enroll-feedback-good"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooHigh",
      "about-webauthn-ctap2-enroll-feedback-too-high"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooLow",
      "about-webauthn-ctap2-enroll-feedback-too-low"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooLeft",
      "about-webauthn-ctap2-enroll-feedback-too-left"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooRight",
      "about-webauthn-ctap2-enroll-feedback-too-right"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooFast",
      "about-webauthn-ctap2-enroll-feedback-too-fast"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooSlow",
      "about-webauthn-ctap2-enroll-feedback-too-slow"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpPoorQuality",
      "about-webauthn-ctap2-enroll-feedback-poor-quality"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooSkewed",
      "about-webauthn-ctap2-enroll-feedback-too-skewed"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpTooShort",
      "about-webauthn-ctap2-enroll-feedback-too-short"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpMergeFailure",
      "about-webauthn-ctap2-enroll-feedback-merge-failure"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackFpExists",
      "about-webauthn-ctap2-enroll-feedback-exists"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackNoUserActivity",
      "about-webauthn-ctap2-enroll-feedback-no-user-activity"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackNoUserPresenceTransition",
      "about-webauthn-ctap2-enroll-feedback-no-user-presence-transition"
    );
    this._bio_l10n.set(
      "Ctap2EnrollFeedbackOther",
      "about-webauthn-ctap2-enroll-feedback-other"
    );

    Services.obs.addObserver(this, this._topic);
    this._initialized = true;
    reset_page();
  },

  uninit() {
    Services.obs.removeObserver(this, this._topic);
    this._initialized = false;
    this._l10n = null;
    this._current_tab = "";
    this._previous_tab = "";
  },

  observe(aSubject, aTopic, aData) {
    let data = JSON.parse(aData);

    // We have token
    if (data.type == "selected-device") {
      this._curr_data = data.auth_info;
      this.show_ui_based_on_authenticator_info(data);
      fake_click_event_for_id("logging-tab-button");
    } else if (data.type == "select-device") {
      set_info_text("about-webauthn-text-select-device");
    } else if (data.type == "pin-required") {
      open_pin_required_tab();
    } else if (data.type == "pin-invalid") {
      let retries = data.retries ? data.retries : 0;
      show_results_banner(
        "error",
        "about-webauthn-results-pin-invalid-error",
        JSON.stringify({ retriesLeft: retries })
      );
      open_pin_required_tab();
    } else if (data.type == "bio-enrollment-update") {
      if (data.result.EnrollmentList) {
        show_results_banner("success", "about-webauthn-results-success");
        this.show_enrollment_list(data.result.EnrollmentList);
        bio_enrollment_in_progress(false);
      } else if (data.result.DeleteSuccess || data.result.AddSuccess) {
        show_results_banner("success", "about-webauthn-results-success");
        clear_bio_enrollment_samples();
        // Update AuthenticatorInfo
        this._curr_data = data.result.DeleteSuccess ?? data.result.AddSuccess;
        fake_click_event_for_id("bio-enrollments-tab-button");
        bio_enrollment_in_progress(false);
        // If we still have some enrollments to show, update the list
        // otherwise, remove it.
        if (
          this._curr_data.options.bioEnroll === true ||
          this._curr_data.options.userVerificationMgmtPreview === true
        ) {
          list_bio_enrollments();
        } else {
          // Hide the list, because it's empty
          document.getElementById("bio-enrollment-list-subsection").hidden =
            true;
        }
      } else if (data.result.UpdateSuccess) {
        fake_click_event_for_id("bio-enrollments-tab-button");
        list_bio_enrollments();
      } else if (data.result.SampleStatus) {
        show_add_bio_enrollment_section();
        let up = document.getElementById("enrollment-update");
        let sample_update = document.createElement("div");
        let new_line = document.createElement("label");
        new_line.classList.add("sample");
        new_line.setAttribute(
          "data-l10n-id",
          this._bio_l10n.get(data.result.SampleStatus[0])
        );
        sample_update.appendChild(new_line);
        let samples_needed_line = document.createElement("label");
        samples_needed_line.setAttribute(
          "data-l10n-id",
          "about-webauthn-samples-still-needed"
        );
        samples_needed_line.setAttribute(
          "data-l10n-args",
          JSON.stringify({ repeatCount: data.result.SampleStatus[1] })
        );
        sample_update.classList.add("bio-enrollment-sample");
        sample_update.appendChild(samples_needed_line);
        up.appendChild(sample_update);
      }
    } else if (data.type == "credential-management-update") {
      credential_management_in_progress(false);
      if (data.result.CredentialList) {
        show_results_banner("success", "about-webauthn-results-success");
        this.show_credential_list(data.result.CredentialList.credential_list);
      } else {
        // DeleteSuccess or UpdateSuccess
        show_results_banner("success", "about-webauthn-results-success");
        list_credentials();
      }
    } else if (data.type == "listen-success") {
      reset_page();
      this._reset_in_progress = "";

      // Show results
      show_results_banner("success", "about-webauthn-results-success");
      AboutWebauthnService.listen();
    } else if (data.type == "listen-error") {
      this._reset_in_progress = "";
      if (this._logging) {
        return;
      }
      reset_page();

      if (!data.error) {
        show_results_banner("error", "about-webauthn-results-general-error");
      } else if (data.error.type == "pin-auth-blocked") {
        show_results_banner(
          "error",
          "about-webauthn-results-pin-auth-blocked-error"
        );
      } else if (data.error.type == "pin-not-set") {
        show_results_banner(
          "error",
          "about-webauthn-results-pin-not-set-error"
        );
      } else if (data.error.type == "device-blocked") {
        show_results_banner(
          "error",
          "about-webauthn-results-pin-blocked-error"
        );
      } else if (data.error.type == "pin-is-too-short") {
        show_results_banner(
          "error",
          "about-webauthn-results-pin-too-short-error"
        );
      } else if (data.error.type == "pin-is-too-long") {
        show_results_banner(
          "error",
          "about-webauthn-results-pin-too-long-error"
        );
      } else if (data.error.type == "pin-invalid") {
        let retries = data.error.retries
          ? JSON.stringify({ retriesLeft: data.error.retries })
          : null;
        show_results_banner(
          "error",
          "about-webauthn-results-pin-invalid-error",
          retries
        );
      } else if (data.error.type == "cancel") {
        show_results_banner(
          "error",
          "about-webauthn-results-cancelled-by-user-error"
        );
      } else {
        show_results_banner("error", "about-webauthn-results-general-error");
      }
      AboutWebauthnService.listen();
    }
  },

  show_authenticator_options(options, element, l10n_base) {
    let table = document.getElementById(element);
    var empty_table = document.createElement("table");
    empty_table.id = element;
    table.parentNode.replaceChild(empty_table, table);
    table = document.getElementById(element);
    for (let key in options) {
      if (key == "options") {
        continue;
      }
      // Create an empty <tr> element and add it to the 1st position of the table:
      var row = table.insertRow(0);

      // Insert new cells (<td> elements) at the 1st and 2nd position of the "new" <tr> element:
      var cell1 = row.insertCell(0);
      var cell2 = row.insertCell(1);

      // Add some text to the new cells:
      let key_text = this._l10n.formatValueSync(
        l10n_base + "-" + key.toLowerCase().replace(/_/g, "-")
      );
      var key_node = document.createTextNode(key_text);
      cell1.appendChild(key_node);
      var raw_value = JSON.stringify(options[key]);
      var value = raw_value;
      if (["true", "false", "null"].includes(raw_value)) {
        value = this._l10n.formatValueSync(l10n_base + "-" + raw_value);
      }
      var value_node = document.createTextNode(value);
      cell2.appendChild(value_node);
    }
  },

  show_ui_based_on_authenticator_info(data) {
    // Hide the "Please plug in a token"-message
    document.getElementById("info-text-div").hidden = true;
    // Show options, based on what the token supports
    if (data.auth_info) {
      document.getElementById("ctap2-token-info").style.display = "flex";
      this.show_authenticator_options(
        data.auth_info.options,
        "authenticator-options",
        "about-webauthn-auth-option"
      );
      this.show_authenticator_options(
        data.auth_info,
        "authenticator-info",
        "about-webauthn-auth-info"
      );
      // Check if token supports PINs
      if (data.auth_info.options.clientPin != null) {
        document.getElementById("pin-tab-button").style.display = "flex";
        if (data.auth_info.options.clientPin === true) {
          // It has a Pin set
          document.getElementById("change-pin-button").style.display = "block";
          document.getElementById("set-pin-button").style.display = "none";
          document.getElementById("current-pin-div").hidden = false;
        } else {
          // It does not have a Pin set yet
          document.getElementById("change-pin-button").style.display = "none";
          document.getElementById("set-pin-button").style.display = "block";
          document.getElementById("current-pin-div").hidden = true;
        }
      } else {
        document.getElementById("pin-tab-button").style.display = "none";
      }

      if (
        data.auth_info.options.credMgmt ||
        data.auth_info.options.credentialMgmtPreview
      ) {
        document.getElementById("credentials-tab-button").style.display =
          "flex";
      } else {
        document.getElementById("credentials-tab-button").style.display =
          "none";
      }

      if (
        data.auth_info.options.bioEnroll != null ||
        data.auth_info.options.userVerificationMgmtPreview != null
      ) {
        document.getElementById("bio-enrollments-tab-button").style.display =
          "flex";
      } else {
        document.getElementById("bio-enrollments-tab-button").style.display =
          "none";
      }
    } else {
      // Currently auth-rs doesn't send this, because it filters out ctap2-devices.
      // U2F / CTAP1 tokens can't be managed
      set_info_text("about-webauthn-text-non-ctap2-device");
    }
  },

  show_credential_list(credential_list) {
    // We may have temporarily hidden the tab when asking the user for a PIN
    // so we have to show it again.
    fake_click_event_for_id("credentials-tab-button");
    document.getElementById("credential-list-subsection").hidden = false;
    let table = document.getElementById("credential-list");
    var empty_table = document.createElement("table");
    empty_table.id = "credential-list";
    table.parentNode.replaceChild(empty_table, table);
    if (!credential_list.length) {
      document.getElementById("credential-list-empty-label").hidden = false;
      return;
    }
    document.getElementById("credential-list-empty-label").hidden = true;
    table = document.getElementById("credential-list");
    credential_list.forEach(rp => {
      // Add some text to the new cells:
      let key_text = rp.rp.id;
      rp.credentials.forEach(cred => {
        let value_text = cred.user.name;
        // Create an empty <tr> element and add it to the 1st position of the table:
        var row = table.insertRow(0);
        var key_node = document.createTextNode(key_text);
        var value_node = document.createTextNode(value_text);
        row.insertCell(0).appendChild(key_node);
        row.insertCell(1).appendChild(value_node);
        var delete_button = document.createElement("button");
        delete_button.classList.add("delete-button");
        delete_button.classList.add("credentials-button");
        let garbage_icon = document.createElement("img");
        garbage_icon.setAttribute(
          "src",
          "chrome://global/skin/icons/delete.svg"
        );
        garbage_icon.classList.add("delete-icon");
        delete_button.appendChild(garbage_icon);
        let delete_text = document.createElement("span");
        delete_text.setAttribute(
          "data-l10n-id",
          "about-webauthn-delete-button"
        );
        delete_button.appendChild(delete_text);
        delete_button.addEventListener("click", function () {
          let context = document.getElementById("confirmation-context");
          context.textContent = key_text + " - " + value_text;
          credential_management_in_progress(true);
          let cmd = {
            CredentialManagement: { DeleteCredential: cred.credential_id },
          };
          context.setAttribute("data-ctap-command", JSON.stringify(cmd));
          open_delete_confirmation_tab();
        });
        row.insertCell(2).appendChild(delete_button);
      });
    });
  },

  show_enrollment_list(enrollment_list) {
    // We may have temporarily hidden the tab when asking the user for a PIN
    // so we have to show it again.
    fake_click_event_for_id("bio-enrollments-tab-button");
    document.getElementById("bio-enrollment-list-subsection").hidden = false;
    let table = document.getElementById("bio-enrollment-list");
    var empty_table = document.createElement("table");
    empty_table.id = "bio-enrollment-list";
    table.parentNode.replaceChild(empty_table, table);
    if (!enrollment_list.length) {
      document.getElementById("bio-enrollment-list-empty-label").hidden = false;
      return;
    }
    document.getElementById("bio-enrollment-list-empty-label").hidden = true;
    table = document.getElementById("bio-enrollment-list");
    enrollment_list.forEach(enrollment => {
      let key_text = enrollment.template_friendly_name ?? "<unnamed>";
      var row = table.insertRow(0);
      var key_node = document.createTextNode(key_text);
      row.insertCell(0).appendChild(key_node);
      var delete_button = document.createElement("button");
      delete_button.classList.add("delete-button");
      delete_button.classList.add("bio-enrollment-button");
      let garbage_icon = document.createElement("img");
      garbage_icon.setAttribute("src", "chrome://global/skin/icons/delete.svg");
      garbage_icon.classList.add("delete-icon");
      delete_button.appendChild(garbage_icon);
      let delete_text = document.createElement("span");
      delete_text.setAttribute("data-l10n-id", "about-webauthn-delete-button");
      delete_button.appendChild(delete_text);
      delete_button.addEventListener("click", function () {
        let context = document.getElementById("confirmation-context");
        context.textContent = key_text;
        bio_enrollment_in_progress(true);
        let cmd = {
          BioEnrollment: { DeleteEnrollment: enrollment.template_id },
        };
        context.setAttribute("data-ctap-command", JSON.stringify(cmd));
        open_delete_confirmation_tab();
      });
      row.insertCell(1).appendChild(delete_button);
    });
  },
};

function set_info_text(l10nId) {
  document.getElementById("info-text-div").hidden = false;
  let field = document.getElementById("info-text-field");
  field.setAttribute("data-l10n-id", l10nId);
  document.getElementById("ctap2-token-info").style.display = "none";
}

function show_results_banner(result, l10n, l10n_args) {
  let ctap_result = document.getElementById("ctap-listen-result");
  ctap_result.setAttribute("data-l10n-id", l10n);
  ctap_result.classList.add(result);
  if (l10n_args) {
    ctap_result.setAttribute("data-l10n-args", l10n_args);
  }
}

function hide_results_banner() {
  let res_banner = document.getElementById("ctap-listen-result");
  let res_div = document.getElementById("ctap-listen-div");
  let empty_banner = document.createElement("label");
  empty_banner.id = "ctap-listen-result";
  res_div.replaceChild(empty_banner, res_banner);
}

function operation_in_progress(name, in_progress) {
  let buttons = Array.from(document.getElementsByClassName(name));
  buttons.forEach(button => {
    button.disabled = in_progress;
  });
}

function credential_management_in_progress(in_progress) {
  operation_in_progress("credentials-button", in_progress);
}

function bio_enrollment_in_progress(in_progress) {
  operation_in_progress("bio-enrollment-button", in_progress);
}

function clear_bio_enrollment_samples() {
  // Remove all previous status updates
  let up = document.getElementById("enrollment-update");
  while (up.firstChild) {
    up.removeChild(up.lastChild);
  }
  document.getElementById("enrollment-name").value = "";
}

function fake_click_event_for_id(id) {
  // Not using document.getElementById(id).click();
  // here, because we have to add additional data, so we don't
  // hide the results-div here, if there is any. 'Normal' clicking
  // by the user will hide it.
  const evt = new CustomEvent("click", {
    detail: { skip_results_clearing: true },
  });
  document.getElementById(id).dispatchEvent(evt);
}

function reset_page() {
  // Hide everything that needs a device to know if it should be displayed
  document.getElementById("ctap2-token-info").style.display = "none";
  Array.from(document.getElementsByClassName("optional-category")).forEach(
    div => {
      div.style.display = "none";
    }
  );

  // Only display the "please connect a device" - text
  set_info_text("about-webauthn-text-connect-device");

  // Clear results and input fields
  hide_results_banner();
  var divs = Array.from(document.getElementsByTagName("input"));
  divs.forEach(div => {
    div.value = "";
  });

  sidebar_set_disabled(false);

  // ListCredentials
  credential_management_in_progress(false);
  document.getElementById("credential-list-subsection").hidden = true;

  // BioEnrollment
  clear_bio_enrollment_samples();
  document.getElementById("bio-enrollment-list-subsection").hidden = true;
  bio_enrollment_in_progress(false);

  AboutWebauthnManagerJS._previous_tab = "";
  AboutWebauthnManagerJS._current_tab = "";

  // Not using `document.getElementById("logging-tab-button").click();`
  // here, because if we were focused on a category-button that got removed (e.g.
  // when unplugging the device), we have to reset the ARIA-related attributes
  // first, before we can click on the button, otherwise the a11y-tests
  // will complain. So we fake the click again.
  const evt = {
    detail: {},
    currentTarget: document.getElementById("logging-tab-button"),
  };
  open_logging_tab(evt);
}

function sidebar_set_disabled(disabled) {
  var cats = Array.from(document.getElementsByClassName("category"));
  cats.forEach(cat => {
    if (disabled) {
      cat.classList.add("disabled-category");
    } else {
      cat.classList.remove("disabled-category");
    }
  });
}

function check_pin_repeat_is_correct() {
  let pin = document.getElementById("new-pin");
  let pin_repeat = document.getElementById("new-pin-repeat");
  let has_current_pin = !document.getElementById("current-pin-div").hidden;
  let current_pin = document.getElementById("current-pin");
  let can_enable_button =
    pin.value != null && pin.value != "" && pin.value == pin_repeat.value;
  if (has_current_pin && !current_pin.value) {
    can_enable_button = false;
  }
  if (!can_enable_button) {
    pin.classList.add("different");
    pin_repeat.classList.add("different");
    document.getElementById("set-pin-button").disabled = true;
    document.getElementById("change-pin-button").disabled = true;
    return false;
  }
  pin.classList.remove("different");
  pin_repeat.classList.remove("different");
  document.getElementById("set-pin-button").disabled = false;
  document.getElementById("change-pin-button").disabled = false;
  return true;
}

function send_pin() {
  close_temporary_overlay_tab();
  let pin = document.getElementById("pin-required").value;
  AboutWebauthnService.pinCallback(0, pin);
}

function set_pin() {
  let pin = document.getElementById("new-pin").value;
  let cmd = { SetPIN: pin };
  AboutWebauthnService.runCommand(JSON.stringify(cmd));
}

function change_pin() {
  let curr_pin = document.getElementById("current-pin").value;
  let new_pin = document.getElementById("new-pin").value;
  let cmd = { ChangePIN: [curr_pin, new_pin] };
  AboutWebauthnService.runCommand(JSON.stringify(cmd));
}

function list_credentials() {
  credential_management_in_progress(true);
  let cmd = { CredentialManagement: "GetCredentials" };
  AboutWebauthnService.runCommand(JSON.stringify(cmd));
}

function list_bio_enrollments() {
  bio_enrollment_in_progress(true);
  let cmd = { BioEnrollment: "GetEnrollments" };
  AboutWebauthnService.runCommand(JSON.stringify(cmd));
}

function show_add_bio_enrollment_section() {
  const evt = new CustomEvent("click", {
    detail: { temporary_overlay: true },
  });
  open_tab(evt, "add-bio-enrollment-section");
  document.getElementById("enrollment-name").focus();
}

function start_bio_enrollment() {
  bio_enrollment_in_progress(true);
  let name = document.getElementById("enrollment-name").value;
  if (!name) {
    name = null; // Empty means "Not set"
  }
  let cmd = { BioEnrollment: { StartNewEnrollment: name } };
  AboutWebauthnService.runCommand(JSON.stringify(cmd));
}

function cancel_transaction() {
  credential_management_in_progress(false);
  bio_enrollment_in_progress(false);
  AboutWebauthnService.cancel(0);
}

function confirm_deletion() {
  let context = document.getElementById("confirmation-context");
  let cmd = context.getAttribute("data-ctap-command");
  AboutWebauthnService.runCommand(cmd);
}

function cancel_confirmation() {
  credential_management_in_progress(false);
  bio_enrollment_in_progress(false);
  close_temporary_overlay_tab();
}

function set_logging_buttons(logging) {
  document.getElementById("start-logging-button").disabled = logging;
  document.getElementById("stop-logging-button").disabled = !logging;
}

function start_logging() {
  AboutWebauthnService.startLogging();
  AboutWebauthnLogger.start();
  AboutWebauthnManagerJS._logging = true;
  reset_page();
  sidebar_set_disabled(true);
  set_logging_buttons(true);
  AboutWebauthnService.cancel(0);
}

function stop_logging() {
  AboutWebauthnService.stopLogging();
  AboutWebauthnLogger.stop();
  AboutWebauthnManagerJS._logging = false;
  set_logging_buttons(false);
  sidebar_set_disabled(false);
  AboutWebauthnService.listen();
}

function clear_logging() {
  let logarea = document.getElementById("webauthn-log");
  logarea.textContent = "";
}

async function onLoad() {
  document.getElementById("set-pin-button").addEventListener("click", set_pin);
  document
    .getElementById("change-pin-button")
    .addEventListener("click", change_pin);
  document
    .getElementById("list-credentials-button")
    .addEventListener("click", list_credentials);
  document
    .getElementById("list-bio-enrollments-button")
    .addEventListener("click", list_bio_enrollments);
  document
    .getElementById("add-bio-enrollment-button")
    .addEventListener("click", show_add_bio_enrollment_section);
  document
    .getElementById("start-enrollment-button")
    .addEventListener("click", start_bio_enrollment);
  document
    .getElementById("new-pin")
    .addEventListener("input", check_pin_repeat_is_correct);
  document
    .getElementById("new-pin-repeat")
    .addEventListener("input", check_pin_repeat_is_correct);
  document
    .getElementById("current-pin")
    .addEventListener("input", check_pin_repeat_is_correct);
  let logging_button = document.getElementById("logging-tab-button");
  logging_button.addEventListener("click", open_logging_tab);
  logging_button.addEventListener("keydown", handle_keydowns);
  let info_button = document.getElementById("info-tab-button");
  info_button.addEventListener("click", open_info_tab);
  info_button.addEventListener("keydown", handle_keydowns);
  let pin_button = document.getElementById("pin-tab-button");
  pin_button.addEventListener("click", open_pin_tab);
  pin_button.addEventListener("keydown", handle_keydowns);
  let credentials_button = document.getElementById("credentials-tab-button");
  credentials_button.addEventListener("click", open_credentials_tab);
  credentials_button.addEventListener("keydown", handle_keydowns);
  let bio_enrollments_button = document.getElementById(
    "bio-enrollments-tab-button"
  );
  bio_enrollments_button.addEventListener("click", open_bio_enrollments_tab);
  bio_enrollments_button.addEventListener("keydown", handle_keydowns);
  document
    .getElementById("logging-tab-button")
    .addEventListener("click", open_logging_tab);
  document
    .getElementById("send-pin-button")
    .addEventListener("click", send_pin);
  document
    .getElementById("cancel-send-pin-button")
    .addEventListener("click", cancel_transaction);
  document
    .getElementById("cancel-enrollment-button")
    .addEventListener("click", cancel_transaction);
  document
    .getElementById("cancel-confirmation-button")
    .addEventListener("click", cancel_confirmation);
  document
    .getElementById("confirm-deletion-button")
    .addEventListener("click", confirm_deletion);
  document
    .getElementById("start-logging-button")
    .addEventListener("click", start_logging);
  document
    .getElementById("stop-logging-button")
    .addEventListener("click", stop_logging);
  document
    .getElementById("clear-logging-button")
    .addEventListener("click", clear_logging);
  AboutWebauthnManagerJS.init();
  try {
    AboutWebauthnService.listen();
  } catch (ex) {
    set_info_text("about-webauthn-text-not-available");
    AboutWebauthnManagerJS.uninit();
  }
}

function handle_keydowns(event) {
  let index;
  let event_was_handled = true;
  let tabs = Array.from(document.getElementsByClassName("category"));
  if (tabs.length <= 0) {
    return;
  }

  switch (event.key) {
    case "ArrowLeft":
    case "ArrowUp":
      if (event.currentTarget === tabs[0]) {
        event.currentTarget.focus();
      } else {
        index = tabs.indexOf(event.currentTarget);
        tabs[index - 1].focus();
      }
      break;

    case "ArrowRight":
    case "ArrowDown":
      if (event.currentTarget === tabs[tabs.length - 1]) {
        event.currentTarget.focus();
      } else {
        index = tabs.indexOf(event.currentTarget);
        tabs[index + 1].focus();
      }
      break;

    case "Home":
      tabs[0].focus();
      break;

    case "End":
      tabs[tabs.length - 1].focus();
      break;

    case "Enter":
    case " ":
      event.currentTarget.click();
      break;

    default:
      event_was_handled = false;
      break;
  }

  if (event_was_handled) {
    event.stopPropagation();
    event.preventDefault();
  }
}

function open_tab(evt, tabName) {
  var tabcontent, tablinks;
  // Hide all others
  tabcontent = Array.from(document.getElementsByClassName("tabcontent"));
  tabcontent.forEach(tab => {
    tab.hidden = true;
  });
  // Display the one we selected
  document.getElementById(tabName).hidden = false;

  // If this is a temporary overlay, like pin-required, we don't
  // touch the sidebar and which button is selected.
  if (!evt.detail.temporary_overlay) {
    tablinks = Array.from(document.getElementsByClassName("category"));
    tablinks.forEach(tablink => {
      tablink.removeAttribute("selected");
      tablink.setAttribute("aria-selected", "false");
      tablink.setAttribute("tabindex", "-1");
      tablink.disabled = false;
    });
    evt.currentTarget.setAttribute("selected", "true");
    evt.currentTarget.setAttribute("tabindex", "0");
    evt.currentTarget.setAttribute("aria-selected", "true");
  }

  if (!evt.detail.skip_results_clearing) {
    hide_results_banner();
  }
  sidebar_set_disabled(false);
  AboutWebauthnManagerJS._previous_tab = AboutWebauthnManagerJS._current_tab;
  AboutWebauthnManagerJS._current_tab = tabName;
}

function open_info_tab(evt) {
  open_tab(evt, "token-info-section");
}
function open_pin_tab(evt) {
  open_tab(evt, "set-change-pin-section");
}
function open_credentials_tab(evt) {
  open_tab(evt, "credential-management-section");
}
function open_logging_tab(evt) {
  open_tab(evt, "logging-section");
}
function open_bio_enrollments_tab(evt) {
  // We can only list, if there are any registered already
  if (
    AboutWebauthnManagerJS._curr_data.options.bioEnroll === true ||
    AboutWebauthnManagerJS._curr_data.options.userVerificationMgmtPreview ===
      true
  ) {
    document.getElementById("list-bio-enrollments-button").style.display =
      "inline-block";
  } else {
    document.getElementById("list-bio-enrollments-button").style.display =
      "none";
  }
  open_tab(evt, "bio-enrollment-section");
}
function open_reset_tab(evt) {
  open_tab(evt, "reset-token-section");
}
function open_pin_required_tab() {
  // Remove any old value we might have had
  document.getElementById("pin-required").value = "";
  const evt = new CustomEvent("click", {
    detail: {
      temporary_overlay: true,
      skip_results_clearing: true, // We might be called multiple times, if PIN was invalid
    },
  });
  open_tab(evt, "pin-required-section");
  document.getElementById("pin-required").focus();
  // This is a temporary overlay, so we don't want the
  // user to click away from it, unless via the Cancel-button.
  sidebar_set_disabled(true);
}
function close_temporary_overlay_tab() {
  const evt = new CustomEvent("click", {
    detail: { temporary_overlay: true },
  });
  open_tab(evt, AboutWebauthnManagerJS._previous_tab);
  sidebar_set_disabled(false);
}
function open_delete_confirmation_tab() {
  const evt = new CustomEvent("click", {
    detail: {
      temporary_overlay: true,
    },
  });
  open_tab(evt, "confirm-deletion-section");
  // This is a temporary overlay, so we don't want the
  // user to click away from it, unless via the Cancel-button.
  sidebar_set_disabled(true);
}

try {
  AboutWebauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
    Ci.nsIWebAuthnService
  );
  document.addEventListener("DOMContentLoaded", onLoad);
  window.addEventListener("beforeunload", () => {
    AboutWebauthnManagerJS.uninit();
    if (AboutWebauthnService) {
      AboutWebauthnService.cancel(0);
    }
  });
  window.addEventListener("unload", () => {
    if (AboutWebauthnManagerJS._logging) {
      AboutWebauthnService.stopLogging();
    }
    AboutWebauthnLogger.stop();
  });
} catch (ex) {
  // Do nothing if we fail to create a singleton instance,
  // showing the default no-module message.
  console.error(ex);
}
