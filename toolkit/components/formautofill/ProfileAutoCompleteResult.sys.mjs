/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  CreditCard: "resource://gre/modules/CreditCard.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(
      [
        "branding/brand.ftl",
        "toolkit/formautofill/formAutofill.ftl",
        "toolkit/main-window/autocomplete.ftl",
      ],
      true
    )
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "removeRecordsEnabled",
  "browser.autocomplete.removeRecords.enabled",
  false
);

function deleteSecondaryAction(
  entry,
  labelId,
  tooltipId,
  deleteMessageName,
  guid
) {
  return {
    type: "delete",
    label: lazy.l10n.formatValueSync(labelId, { entry }),
    tooltip: lazy.l10n.formatValueSync(tooltipId),
    fillMessageName: deleteMessageName,
    fillMessageData: { guid },
  };
}

export class ProfileAutoCompleteResult {
  externalEntries = [];

  constructor(
    searchString,
    focusedFieldDetail,
    allFieldNames,
    matchingProfiles,
    { resultCode = null, isSecure = true, isInputAutofilled = false }
  ) {
    // nsISupports
    this.QueryInterface = ChromeUtils.generateQI(["nsIAutoCompleteResult"]);

    // The user's query string
    this.searchString = searchString;
    // The field name of the focused input.
    this._focusedFieldName = focusedFieldDetail.fieldName;
    // The content dom reference id of the focused input.
    this._focusedElementId = focusedFieldDetail.elementId;
    // The matching profiles, used to work out which of the form's fields they
    // can fill. The profile a row fills is carried by the row itself, because
    // not every profile ends up with one.
    this._matchingProfiles = matchingProfiles;
    // The default item that should be entered if none is selected
    this.defaultIndex = 0;
    // The reason the search failed
    this.errorDescription = "";
    // The value used to determine whether the form is secure or not.
    this._isSecure = isSecure;
    // The value to indicate whether the focused input has been autofilled or not.
    this._isInputAutofilled = isInputAutofilled;
    // All fillable field names in the form including the field name of the currently-focused input.
    this._allFieldNames = [
      ...this._matchingProfiles.reduce((fieldSet, curProfile) => {
        for (let field of Object.keys(curProfile)) {
          fieldSet.add(field);
        }

        return fieldSet;
      }, new Set()),
    ].filter(field => allFieldNames.includes(field));

    // Force return success code if the focused field is auto-filled in order
    // to show clear form button popup.
    if (isInputAutofilled) {
      resultCode = Ci.nsIAutoCompleteResult.RESULT_SUCCESS;
    }
    // The result code of this result object.
    if (resultCode) {
      this.searchResult = resultCode;
    } else {
      this.searchResult = matchingProfiles.length
        ? Ci.nsIAutoCompleteResult.RESULT_SUCCESS
        : Ci.nsIAutoCompleteResult.RESULT_NOMATCH;
    }

    // An array of primary and secondary labels for each profile.
    this._popupLabels = this._generateLabels(
      this._focusedFieldName,
      this._allFieldNames,
      this._matchingProfiles
    );
    this._footerLabel = this._generateFooterLabel();
  }

  /**
   * The footer row, kept last so that it stays below the entries contributed
   * by other providers. Subclasses that have a footer override this; the
   * default is to have none.
   *
   * @returns {object | null} The footer row, or null when there is none.
   */
  _generateFooterLabel() {
    return null;
  }

  /**
   * The item groups in display order: the entries contributed by other
   * providers, then the profile rows, then the footer. On an insecure form
   * the warning row stays first.
   *
   * @returns {Array<Array<object>>} The groups, in display order.
   */
  _orderedGroups() {
    const groups = this._isSecure
      ? [this.externalEntries, this._popupLabels]
      : [this._popupLabels, this.externalEntries];
    return [...groups, this._footerLabel ? [this._footerLabel] : []];
  }

  _footerIndex() {
    return this._footerLabel ? this.matchCount - 1 : -1;
  }

  getAt(index) {
    for (const group of this._orderedGroups()) {
      if (index < group.length) {
        return group[index];
      }
      index -= group.length;
    }

    throw Components.Exception(
      "Index out of range.",
      Cr.NS_ERROR_ILLEGAL_VALUE
    );
  }

  /**
   * @returns {number} The number of results
   */
  get matchCount() {
    return (
      this._popupLabels.length +
      this.externalEntries.length +
      (this._footerLabel ? 1 : 0)
    );
  }

  /**
   * Get the secondary label based on the focused field name and related field names
   * in the same form.
   *
   * @param   {string} _focusedFieldName The field name of the focused input
   * @param   {Array<object>} _allFieldNames The field names in the same section
   * @param   {object} _profile The profile providing the labels to show.
   * @returns {string} The secondary label
   */
  _getSecondaryLabel(_focusedFieldName, _allFieldNames, _profile) {
    return "";
  }

  _generateLabels(_focusedFieldName, _allFieldNames, _profiles) {}

  /**
   * Get the value of the result at the given index.
   *
   * Always return empty string for form autofill feature to suppress
   * AutoCompleteController from autofilling, as we'll populate the
   * fields on our own.
   *
   * @param   {number} index The index of the result requested
   * @returns {string} The result at the specified index
   */
  getValueAt(index) {
    this.getAt(index);
    return "";
  }

  getLabelAt(index) {
    const item = this.getAt(index);
    return typeof item == "string" ? item : item.primary || item.label;
  }

  /**
   * Retrieves a comment (metadata instance)
   *
   * @param   {number} index The index of the comment requested
   * @returns {string} The comment at the specified index
   */
  getCommentAt(index) {
    const item = this.getAt(index);
    if (item.style == "status") {
      return JSON.stringify(item);
    }

    // A row carries the record it was generated from, so that a record that
    // doesn't get a row cannot shift the popup onto the wrong one.
    const { profile, ...row } = item;

    const data = {
      fillMessageData: {
        focusElementId: this._focusedElementId,
      },
    };

    const type = this.getTypeOfIndex(index);
    switch (type) {
      case "clear":
        data.fillMessageName = "FormAutofill:ClearForm";
        break;
      case "manage":
        data.fillMessageName = this.openPreferenceMessage;
        break;
      case "insecure":
        data.noLearnMore = true;
        break;
      default: {
        if (row.comment) {
          return row.comment;
        }

        data.fillMessageName = "FormAutofill:FillForm";
        data.fillMessageData.profile = profile;
        break;
      }
    }

    return JSON.stringify({ ...row, ...data });
  }

  /**
   * Retrieves a style hint specific to a particular index.
   *
   * @param   {number} index The index of the style hint requested
   * @returns {string} The style hint at the specified index
   */
  getStyleAt(index) {
    const itemStyle = this.getAt(index).style;
    if (itemStyle) {
      return itemStyle;
    }

    switch (this.getTypeOfIndex(index)) {
      case "manage":
        return "action";
      case "clear":
        return "action";
      case "insecure":
        return "insecureWarning";
      default:
        return "autofill";
    }
  }

  /**
   * Retrieves an image url.
   *
   * @param   {number} index The index of the image url requested
   * @returns {string} The image url at the specified index
   */
  getImageAt(index) {
    return this.getAt(index).image ?? "";
  }

  /**
   * Retrieves a result
   *
   * @param   {number} index The index of the result requested
   * @returns {string} The result at the specified index
   */
  getFinalCompleteValueAt(index) {
    return this.getValueAt(index);
  }

  /**
   * Returns true if the value at the given index is removable
   *
   * @param   {number}  _index The index of the result to remove
   * @returns {boolean} True if the value is removable
   */
  isRemovableAt(_index) {
    return false;
  }

  /**
   * Removes a result from the resultset
   *
   * @param {number} _index The index of the result to remove
   */
  removeValueAt(_index) {
    // There is no plan to support removing profiles via autocomplete.
  }

  /**
   * Returns a type string that identifies te type of row at the given index.
   *
   * @param   {number} index The index of the result requested
   * @returns {string} The type at the specified index
   */
  getTypeOfIndex(index) {
    if (this._isInputAutofilled && index == 0) {
      return "clear";
    }

    if (index == this._footerIndex()) {
      return "manage";
    }

    return "item";
  }

  /**
   * Build the autocomplete result for a data type. The result classes live
   * here, so this factory owns the type-id -> class mapping.
   *
   * @param {string} typeId An AutofillDataTypes id.
   * @param {...any} args Forwarded to the result constructor.
   * @returns {ProfileAutoCompleteResult}
   */
  static createResult(typeId, ...args) {
    switch (typeId) {
      case lazy.AutofillDataTypes.CREDIT_CARD:
        return new CreditCardResult(...args);
      default:
        return new AddressResult(...args);
    }
  }
}

export class AddressResult extends ProfileAutoCompleteResult {
  get openPreferenceMessage() {
    return `FormAutofill:OpenAddressPreferences`;
  }

  _getSecondaryLabel(focusedFieldName, allFieldNames, profile) {
    // We group similar fields into the same field name so we won't pick another
    // field in the same group as the secondary label.
    const GROUP_FIELDS = {
      name: ["name", "given-name", "additional-name", "family-name"],
      "street-address": [
        "street-address",
        "address-line1",
        "address-line2",
        "address-line3",
      ],
      "country-name": ["country", "country-name"],
      tel: [
        "tel",
        "tel-country-code",
        "tel-national",
        "tel-area-code",
        "tel-local",
        "tel-local-prefix",
        "tel-local-suffix",
      ],
    };

    const secondaryLabelOrder = [
      "street-address", // Street address
      "name", // Full name
      "address-level3", // Townland / Neighborhood / Village
      "address-level2", // City/Town
      "organization", // Company or organization name
      "address-level1", // Province/State (Standardized code if possible)
      "country-name", // Country name
      "postal-code", // Postal code
      "tel", // Phone number
      "email", // Email address
    ];

    for (let field in GROUP_FIELDS) {
      if (GROUP_FIELDS[field].includes(focusedFieldName)) {
        focusedFieldName = field;
        break;
      }
    }

    for (const currentFieldName of secondaryLabelOrder) {
      if (focusedFieldName == currentFieldName || !profile[currentFieldName]) {
        continue;
      }

      let matching = GROUP_FIELDS[currentFieldName]
        ? allFieldNames.some(fieldName =>
            GROUP_FIELDS[currentFieldName].includes(fieldName)
          )
        : allFieldNames.includes(currentFieldName);

      if (matching) {
        if (
          currentFieldName == "street-address" &&
          profile["-moz-street-address-one-line"]
        ) {
          return profile["-moz-street-address-one-line"];
        }
        return profile[currentFieldName];
      }
    }

    return ""; // Nothing matched.
  }

  _generateFooterLabel() {
    return {
      primary: lazy.l10n.formatValueSync("autofill-manage-addresses-label"),
      secondary: "",
    };
  }

  _generateLabels(focusedFieldName, allFieldNames, profiles) {
    if (this._isInputAutofilled) {
      const clearLabel = lazy.l10n.formatValueSync("autofill-clear-form-label");

      return [
        {
          primary: clearLabel,
        },
      ];
    }

    const labels = [];
    const seenFillValues = new Set();
    for (const profile of profiles) {
      let primary = profile[focusedFieldName];
      // Skip results without a primary label.
      if (!primary) {
        continue;
      }

      // The values this address would fill into the section. A field stored
      // empty and a field not stored at all both fill nothing, so they map to
      // the same value.
      const fillValues = JSON.stringify(
        allFieldNames.map(fieldName => profile[fieldName] ?? "")
      );
      // An earlier address already fills the section this way, so a second row
      // would fill the same thing and may not even look different. Records
      // arrive sorted by timeLastUsed, so the one kept is the most recent.
      if (seenFillValues.has(fillValues)) {
        continue;
      }
      seenFillValues.add(fillValues);

      if (
        focusedFieldName == "street-address" &&
        profile["-moz-street-address-one-line"]
      ) {
        primary = profile["-moz-street-address-one-line"];
      }

      const secondary = this._getSecondaryLabel(
        focusedFieldName,
        allFieldNames,
        profile
      );
      // Exclude empty chunks.
      const ariaLabel = [primary, secondary].filter(chunk => !!chunk).join(" ");

      labels.push({
        primary,
        secondary,
        ariaLabel,
        // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
        image: "chrome://browser/skin/fxa/avatar-empty.svg",
        type: "address",
        profile,
        ...(lazy.removeRecordsEnabled && {
          secondaryAction: deleteSecondaryAction(
            ariaLabel,
            "autocomplete-delete-address-entry",
            "autocomplete-delete-address",
            "FormAutofill:DeleteAddress",
            profile.guid
          ),
        }),
      });
    }

    return labels;
  }
}

export class CreditCardResult extends ProfileAutoCompleteResult {
  get openPreferenceMessage() {
    return `FormAutofill:OpenPaymentPreferences`;
  }

  _getSecondaryLabel(focusedFieldName, allFieldNames, profile) {
    // A form can ask for the security code on its own, in which case there is
    // no other credit card field to match against below. The card number is
    // the only thing keeping those entries distinguishable, so always use it.
    if (focusedFieldName == "cc-csc") {
      return profile["cc-number"]
        ? lazy.CreditCard.formatMaskedNumber(profile["cc-number"])
        : "";
    }

    const GROUP_FIELDS = {
      "cc-name": [
        "cc-name",
        "cc-given-name",
        "cc-additional-name",
        "cc-family-name",
      ],
      "cc-exp": ["cc-exp", "cc-exp-month", "cc-exp-year"],
    };

    const secondaryLabelOrder = [
      "cc-number", // Credit card number
      "cc-name", // Full name
      "cc-exp", // Expiration date
    ];

    for (let field in GROUP_FIELDS) {
      if (GROUP_FIELDS[field].includes(focusedFieldName)) {
        focusedFieldName = field;
        break;
      }
    }

    for (const currentFieldName of secondaryLabelOrder) {
      if (focusedFieldName == currentFieldName || !profile[currentFieldName]) {
        continue;
      }

      let matching = GROUP_FIELDS[currentFieldName]
        ? allFieldNames.some(fieldName =>
            GROUP_FIELDS[currentFieldName].includes(fieldName)
          )
        : allFieldNames.includes(currentFieldName);

      if (matching) {
        if (currentFieldName == "cc-number") {
          return lazy.CreditCard.formatMaskedNumber(profile[currentFieldName]);
        }
        return profile[currentFieldName];
      }
    }

    return ""; // Nothing matched.
  }

  _generateFooterLabel() {
    // An insecure form only shows the warning row.
    if (!this._isSecure) {
      return null;
    }

    return {
      primary: lazy.l10n.formatValueSync(
        "autofill-manage-payment-methods-label"
      ),
    };
  }

  _generateLabels(focusedFieldName, allFieldNames, profiles) {
    if (!this._isSecure) {
      return [
        lazy.l10n.formatValueSync(
          "autofill-insecure-field-warning-description"
        ),
      ];
    }

    if (this._isInputAutofilled) {
      const clearLabel = lazy.l10n.formatValueSync("autofill-clear-form-label");

      return [
        {
          primary: clearLabel,
        },
      ];
    }

    // Skip results without a primary label.
    let labels = profiles
      .filter(profile => {
        return !!profile[focusedFieldName];
      })
      .map(profile => {
        let primary = profile[focusedFieldName];

        if (focusedFieldName == "cc-number") {
          primary = lazy.CreditCard.formatMaskedNumber(primary);
        } else if (focusedFieldName == "cc-csc") {
          // The security code must never be displayed, so name the entry after
          // the field instead, the way the card's network does.
          primary = lazy.CreditCard.formatSecurityCodeName(profile);
        }

        const secondary = this._getSecondaryLabel(
          focusedFieldName,
          allFieldNames,
          profile
        );
        // The card type is displayed visually using an image. For a11y, we need
        // to expose it as text. We do this using aria-label. However,
        // aria-label overrides the text content, so we must include that also.
        const ccType = profile["cc-type"];
        const image = lazy.CreditCard.getCreditCardLogo(ccType);
        const ccTypeL10nId = lazy.CreditCard.getNetworkL10nId(ccType);
        const ccTypeName = ccTypeL10nId
          ? lazy.l10n.formatValueSync(ccTypeL10nId)
          : (ccType ?? ""); // Unknown card type
        const ariaLabel = [
          ccTypeName,
          primary.toString().replaceAll("•", ""),
          secondary,
        ]
          .filter(chunk => !!chunk) // Exclude empty chunks.
          .join(" ");
        return {
          primary: primary.toString().replaceAll("*", "•"),
          secondary: secondary.toString().replaceAll("*", "•"),
          ariaLabel,
          image,
          type: "payment",
          profile,
          ...(lazy.removeRecordsEnabled && {
            secondaryAction: deleteSecondaryAction(
              ariaLabel,
              "autocomplete-delete-payment-method-entry",
              "autocomplete-delete-payment-method",
              "FormAutofill:DeleteCreditCard",
              profile.guid
            ),
          }),
        };
      });

    return labels;
  }

  getTypeOfIndex(index) {
    if (!this._isSecure) {
      return "insecure";
    }

    return super.getTypeOfIndex(index);
  }
}
