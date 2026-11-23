/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Utilities for working with DOM elements in password/login fields.
 * Used by content process code (LoginManagerChild, LoginFormFactory, etc.)
 */

import { LoginOriginUtils } from "moz-src:///toolkit/components/passwordmgr/shared/LoginOriginUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { NewPasswordModel } from "resource://gre/modules/shared/NewPasswordModel.sys.mjs";

class LoginHelperDOM {
  static inputTypeIsCompatibleWithUsername(input) {
    const fieldType = input.getAttribute("type")?.toLowerCase() || input.type;
    return (
      ["text", "email", "url", "tel", "number", "search"].includes(fieldType) ||
      fieldType?.includes("user")
    );
  }

  /**
   * Test whether the element has the keyword in its attributes.
   * The tested attributes include id, name, className, ariaLabel and placeholder.
   */
  static elementAttrsMatchRegex(element, regex) {
    if (
      regex.test(element.id) ||
      regex.test(element.name) ||
      regex.test(element.className) ||
      regex.test(element.ariaLabel ?? "")
    ) {
      return true;
    }

    const placeholder = element.getAttribute("placeholder");
    return placeholder && regex.test(placeholder);
  }

  /**
   * Test whether associated labels of the element have the keyword.
   * This is a simplified rule of hasLabelMatchingRegex in NewPasswordModel.sys.mjs
   */
  static hasLabelMatchingRegex(element, regex) {
    return regex.test(element.labels?.[0]?.textContent);
  }

  static hasTextContentMatchingRegex(element, regex) {
    return regex.test(element.textContent);
  }

  static getFormActionOrigin(form) {
    let uriString = form.action;

    // A blank or missing action submits to where it came from.
    if (uriString == "") {
      // ala bug 297761
      uriString = form.baseURI;
    }

    return LoginOriginUtils.getLoginOrigin(uriString, true);
  }

  /**
   * Checks if a field type is username compatible.
   *
   * @param {Element} element
   *                  the field we want to check.
   * @param {object} options
   * @param {bool} [options.ignoreConnect] - Whether to ignore checking isConnected
   *                                         of the element.
   *
   * @returns {boolean} true if the field type is one
   *                    of the username types.
   */
  static isUsernameFieldType(element, { ignoreConnect = false } = {}) {
    if (!HTMLInputElement.isInstance(element)) {
      return false;
    }

    if (!element.isConnected && !ignoreConnect) {
      // If the element isn't connected then it isn't visible to the user so
      // shouldn't be considered. It must have been connected in the past.
      return false;
    }

    if (element.hasBeenTypePassword) {
      return false;
    }

    if (!LoginHelperDOM.inputTypeIsCompatibleWithUsername(element)) {
      return false;
    }

    let acFieldName = element.getAutocompleteInfo().fieldName;
    if (
      !(
        acFieldName == "username" ||
        acFieldName == "webauthn" ||
        // Bug 1540154: Some sites use tel/email on their username fields.
        acFieldName == "email" ||
        acFieldName == "tel" ||
        acFieldName == "tel-national" ||
        acFieldName == "off" ||
        acFieldName == "on" ||
        acFieldName == ""
      )
    ) {
      return false;
    }
    return true;
  }

  /**
   * Checks if a field type is password compatible.
   *
   * @param {Element} element
   *                  the field we want to check.
   * @param {object} options
   * @param {bool} [options.ignoreConnect] - Whether to ignore checking isConnected
   *                                         of the element.
   *
   * @returns {boolean} true if the field can
   *                    be treated as a password input
   */
  static isPasswordFieldType(element, { ignoreConnect = false } = {}) {
    if (!HTMLInputElement.isInstance(element)) {
      return false;
    }

    if (!element.isConnected && !ignoreConnect) {
      // If the element isn't connected then it isn't visible to the user so
      // shouldn't be considered. It must have been connected in the past.
      return false;
    }

    if (!element.hasBeenTypePassword) {
      return false;
    }

    // Ensure the element is of a type that could have autocomplete.
    // These include the types with user-editable values. If not, even if it used to be
    // a type=password, we can't treat it as a password input now
    let acInfo = element.getAutocompleteInfo();
    if (!acInfo) {
      return false;
    }

    return true;
  }

  static #cachedNewPasswordScore = new WeakMap();

  static isProbablyANewPasswordField(inputElement) {
    const autocompleteInfo = inputElement.getAutocompleteInfo();
    if (autocompleteInfo.fieldName === "new-password") {
      return true;
    }

    if (LoginHelperDOM.newPasswordFieldFathomThreshold == -1) {
      // Fathom is disabled
      return false;
    }

    let score = this.#cachedNewPasswordScore.get(inputElement);
    if (score) {
      return score >= LoginHelperDOM.newPasswordFieldFathomThreshold;
    }

    const { rules, type } = NewPasswordModel;
    const results = rules.against(inputElement);
    score = results.get(inputElement).scoreFor(type);
    this.#cachedNewPasswordScore.set(inputElement, score);
    return score >= LoginHelperDOM.newPasswordFieldFathomThreshold;
  }

  static findConfirmationField(passwordField, formFactory) {
    const form = formFactory.createFromField(passwordField);
    let confirmPasswordInput = null;
    const MAX_CONFIRM_PASSWORD_DISTANCE = 3;

    const startIndex = form.elements.indexOf(passwordField);
    if (startIndex === -1) {
      throw new Error(
        "Password field is not in the form's elements collection"
      );
    }

    // Get a list of input fields to search in.
    // Pre-filter type=hidden fields; they don't count against the distance threshold
    const afterFields = form.elements
      .slice(startIndex + 1)
      .filter(elem => elem.type !== "hidden");

    const acFieldName = passwordField.getAutocompleteInfo()?.fieldName;

    // Match same autocomplete values first
    if (acFieldName === "new-password") {
      const matchIndex = afterFields.findIndex(
        elem =>
          LoginHelperDOM.isPasswordFieldType(elem) &&
          elem.getAutocompleteInfo().fieldName === acFieldName &&
          !elem.disabled &&
          !elem.readOnly
      );
      if (matchIndex >= 0 && matchIndex < MAX_CONFIRM_PASSWORD_DISTANCE) {
        confirmPasswordInput = afterFields[matchIndex];
      }
    }

    if (!confirmPasswordInput) {
      for (
        let idx = 0;
        idx < Math.min(MAX_CONFIRM_PASSWORD_DISTANCE, afterFields.length);
        idx++
      ) {
        if (
          LoginHelperDOM.isPasswordFieldType(afterFields[idx]) &&
          !afterFields[idx].disabled &&
          !afterFields[idx].readOnly
        ) {
          confirmPasswordInput = afterFields[idx];
          break;
        }
      }
    }

    return confirmPasswordInput;
  }

  /**
   * Infer whether a form is a sign-in form by searching keywords
   * in its attributes
   *
   * @param {Element} formElement
   *                  the form we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  static isInferredLoginForm(formElement) {
    if (
      LoginHelperDOM.elementAttrsMatchRegex(
        formElement,
        NewPasswordModel.LoginRegex
      )
    ) {
      return true;
    }

    const buttons = Array.from(
      formElement.querySelectorAll("button[type=submit]")
    );
    // Limit to forms with only one submit button to avoid false positives.
    if (
      buttons.length == 1 &&
      LoginHelperDOM.hasTextContentMatchingRegex(
        buttons[0],
        NewPasswordModel.LoginFormAttrRegex
      )
    ) {
      return true;
    }

    return false;
  }

  /**
   * Infer whether an input field is a username field by searching
   * 'username' keyword in its attributes
   *
   * @param {Element} element
   *                  the field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  static isInferredUsernameField(element) {
    const expr = /username/i;

    let ac = element.getAutocompleteInfo()?.fieldName;
    if (ac && (ac == "username" || ac == "webauthn")) {
      return true;
    }

    if (
      LoginHelperDOM.elementAttrsMatchRegex(element, expr) ||
      LoginHelperDOM.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  }

  /**
   * Search for keywords that indicates the input field is not likely a
   * field of a username login form.
   *
   * @param {Element} element
   *                  the input field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  static isInferredNonUsernameField(element) {
    const expr = /\b(search|code|add)\b/i;

    if (
      LoginHelperDOM.elementAttrsMatchRegex(element, expr) ||
      LoginHelperDOM.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  }

  /**
   * Infer whether an input field is an email field by searching
   * 'email' keyword in its attributes.
   *
   * @param {Element} element
   *                  the field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  static isInferredEmailField(element) {
    const expr = /email|邮箱/i;

    if (element.type == "email") {
      return true;
    }

    let ac = element.getAutocompleteInfo()?.fieldName;
    if (ac && ac == "email") {
      return true;
    }

    if (
      LoginHelperDOM.elementAttrsMatchRegex(element, expr) ||
      LoginHelperDOM.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  }
}

XPCOMUtils.defineLazyPreferenceGetter(
  LoginHelperDOM,
  "newPasswordFieldFathomThreshold",
  "signon.generation.confidenceThreshold",
  null,
  null,
  pref => parseFloat(pref)
);

export { LoginHelperDOM };
