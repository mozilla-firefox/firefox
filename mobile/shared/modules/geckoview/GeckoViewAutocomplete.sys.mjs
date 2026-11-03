/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
  AddressRecord: "resource://gre/modules/shared/AddressRecord.sys.mjs",
  CreditCardRecord: "resource://gre/modules/shared/CreditCardRecord.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// How long to keep an autocomplete selection prompt open after its field loses
// focus. If focus moves to another field of the same form within this window
// (e.g. tabbing through it), we keep the existing prompt alive instead of
// dismissing and reopening it, which would otherwise flicker. The default was
// determined by testing on a Samsung Galaxy A51 (our low-end benchmark at time
// of implementation) to be long enough to bridge a focus change, while staying
// short enough that dismissals feel instant; it is pref-tunable for slower
// devices and tests.
const DEFAULT_DISMISS_COALESCE_DELAY_MS = 150;

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "dismissCoalesceDelayMs",
  "geckoview.autocomplete.selection_dismiss_delay_ms",
  DEFAULT_DISMISS_COALESCE_DELAY_MS
);

ChromeUtils.defineLazyGetter(lazy, "LoginInfo", () =>
  Components.Constructor(
    "@mozilla.org/login-manager/loginInfo;1",
    "nsILoginInfo",
    "init"
  )
);

export class LoginEntry {
  constructor({
    origin,
    formActionOrigin,
    httpRealm,
    username,
    password,
    guid,
    timeCreated,
    timeLastUsed,
    timePasswordChanged,
    timesUsed,
  }) {
    this.origin = origin ?? "";
    this.formActionOrigin = formActionOrigin ?? null;
    this.httpRealm = httpRealm ?? null;
    this.username = username ?? "";
    this.password = password ?? "";

    // Metadata.
    this.guid = guid ?? null;
    // TODO: Not supported by GV.
    this.timeCreated = timeCreated ?? null;
    this.timeLastUsed = timeLastUsed ?? null;
    this.timePasswordChanged = timePasswordChanged ?? null;
    this.timesUsed = timesUsed ?? null;
  }

  toLoginInfo() {
    const info = new lazy.LoginInfo(
      this.origin,
      this.formActionOrigin,
      this.httpRealm,
      this.username,
      this.password
    );

    // Metadata.
    info.QueryInterface(Ci.nsILoginMetaInfo);
    info.guid = this.guid;
    info.timeCreated = this.timeCreated;
    info.timeLastUsed = this.timeLastUsed;
    info.timePasswordChanged = this.timePasswordChanged;
    info.timesUsed = this.timesUsed;

    return info;
  }

  static parse(aObj) {
    const entry = new LoginEntry({});
    Object.assign(entry, aObj);

    return entry;
  }

  static fromLoginInfo(aInfo) {
    const entry = new LoginEntry({});
    entry.origin = aInfo.origin;
    entry.formActionOrigin = aInfo.formActionOrigin;
    entry.httpRealm = aInfo.httpRealm;
    entry.username = aInfo.username;
    entry.password = aInfo.password;

    // Metadata.
    aInfo.QueryInterface(Ci.nsILoginMetaInfo);
    entry.guid = aInfo.guid;
    entry.timeCreated = aInfo.timeCreated;
    entry.timeLastUsed = aInfo.timeLastUsed;
    entry.timePasswordChanged = aInfo.timePasswordChanged;
    entry.timesUsed = aInfo.timesUsed;

    return entry;
  }
}

export class Address {
  constructor({
    name,
    givenName,
    additionalName,
    familyName,
    organization,
    streetAddress,
    addressLevel1,
    addressLevel2,
    addressLevel3,
    postalCode,
    country,
    tel,
    email,
    guid,
    timeCreated,
    timeLastUsed,
    timeLastModified,
    timesUsed,
    version,
  }) {
    this.name = name ?? "";
    this.givenName = givenName ?? "";
    this.additionalName = additionalName ?? "";
    this.familyName = familyName ?? "";
    this.organization = organization ?? "";
    this.streetAddress = streetAddress ?? "";
    this.addressLevel1 = addressLevel1 ?? "";
    this.addressLevel2 = addressLevel2 ?? "";
    this.addressLevel3 = addressLevel3 ?? "";
    this.postalCode = postalCode ?? "";
    this.country = country ?? "";
    this.tel = tel ?? "";
    this.email = email ?? "";

    // Metadata.
    this.guid = guid ?? null;
    // TODO: Not supported by GV.
    this.timeCreated = timeCreated ?? null;
    this.timeLastUsed = timeLastUsed ?? null;
    this.timeLastModified = timeLastModified ?? null;
    this.timesUsed = timesUsed ?? null;
    this.version = version ?? null;
  }

  isValid() {
    return (
      (this.name ?? this.givenName ?? this.familyName) !== "" &&
      this.streetAddress !== "" &&
      this.postalCode !== ""
    );
  }

  static fromGecko(aObj) {
    return new Address({
      version: aObj.version,
      name: aObj.name,
      givenName: aObj["given-name"],
      additionalName: aObj["additional-name"],
      familyName: aObj["family-name"],
      organization: aObj.organization,
      streetAddress: aObj["street-address"],
      addressLevel1: aObj["address-level1"],
      addressLevel2: aObj["address-level2"],
      addressLevel3: aObj["address-level3"],
      postalCode: aObj["postal-code"],
      country: aObj.country,
      tel: aObj.tel,
      email: aObj.email,
      guid: aObj.guid,
      timeCreated: aObj.timeCreated,
      timeLastUsed: aObj.timeLastUsed,
      timeLastModified: aObj.timeLastModified,
      timesUsed: aObj.timesUsed,
    });
  }

  static parse(aObj) {
    const entry = new Address({});
    Object.assign(entry, aObj);

    return entry;
  }

  toGecko() {
    let address = {
      version: this.version,
      name: this.name,
      organization: this.organization,
      "street-address": this.streetAddress,
      "address-level1": this.addressLevel1,
      "address-level2": this.addressLevel2,
      "address-level3": this.addressLevel3,
      "postal-code": this.postalCode,
      country: this.country,
      tel: this.tel,
      email: this.email,
      guid: this.guid,
      ...(this.givenName && {
        "given-name": this.givenName,
        "additional-name": this.additionalName,
        "family-name": this.familyName,
      }),
    };

    lazy.AddressRecord.computeFields(address);

    return address;
  }
}

export class CreditCard {
  constructor({
    name,
    number,
    expMonth,
    expYear,
    type,
    guid,
    timeCreated,
    timeLastUsed,
    timeLastModified,
    timesUsed,
    version,
  }) {
    this.name = name ?? "";
    this.number = number ?? "";
    this.expMonth = expMonth ?? "";
    this.expYear = expYear ?? "";
    this.type = type ?? "";

    // Metadata.
    this.guid = guid ?? null;
    // TODO: Not supported by GV.
    this.timeCreated = timeCreated ?? null;
    this.timeLastUsed = timeLastUsed ?? null;
    this.timeLastModified = timeLastModified ?? null;
    this.timesUsed = timesUsed ?? null;
    this.version = version ?? null;
  }

  isValid() {
    return this.number !== "";
  }

  static fromGecko(aObj) {
    return new CreditCard({
      version: aObj.version,
      name: aObj["cc-name"],
      number: aObj["cc-number"],
      expMonth: aObj["cc-exp-month"]?.toString(),
      expYear: aObj["cc-exp-year"]?.toString(),
      type: aObj["cc-type"],
      guid: aObj.guid,
      timeCreated: aObj.timeCreated,
      timeLastUsed: aObj.timeLastUsed,
      timeLastModified: aObj.timeLastModified,
      timesUsed: aObj.timesUsed,
    });
  }

  static parse(aObj) {
    const entry = new CreditCard({});
    Object.assign(entry, aObj);

    return entry;
  }

  toGecko() {
    let creditCard = {
      version: this.version,
      "cc-name": this.name,
      "cc-number": this.number,
      "cc-exp-month": this.expMonth,
      "cc-exp-year": this.expYear,
      "cc-type": this.type,
      guid: this.guid,
    };

    lazy.CreditCardRecord.computeFields(creditCard);

    return creditCard;
  }
}

export class SelectOption {
  // Sync with Autocomplete.SelectOption.Hint in Autocomplete.java.
  static Hint = {
    NONE: 0,
    GENERATED: 1 << 0,
    INSECURE_FORM: 1 << 1,
    DUPLICATE_USERNAME: 1 << 2,
    MATCHING_ORIGIN: 1 << 3,
    FIREFOX_RELAY: 1 << 4,
  };

  constructor({ value, hint }) {
    this.value = value ?? null;
    this.hint = hint ?? SelectOption.Hint.NONE;
  }
}

// Sync with Autocomplete.UsedField in Autocomplete.java.
const UsedField = { PASSWORD: 1 };

export const GeckoViewAutocomplete = {
  /** current opened prompt */
  _prompt: null,

  /**
   * Fill context for the in-flight selection (browsingContext, browser,
   * inputElementIdentifier, formOrigin, selectionType, loginStyle). This is
   * re-pointed when focus moves between same-type fields so the eventual fill
   * targets the currently focused field rather than the one that first opened
   * the prompt.
   */
  _activeSelection: null,

  /**
   * Monotonically increasing id identifying the current selection. Each new
   * (non-coalesced) selection bumps it; a delegateSelection invocation only
   * touches shared state if it still owns the current generation. This keeps a
   * superseded or abandoned selection (e.g. a prompt torn down by navigation)
   * from clobbering or blocking a later one.
   */
  _selectionGeneration: 0,

  /** Timer id for a deferred prompt dismissal, or null if none is pending. */
  _pendingDismissTimer: null,

  /**
   * Delegates login entry fetching for the given domain to the attached
   * LoginStorage GeckoView delegate.
   *
   * @param aDomain
   *        The domain string to fetch login entries for. If null, all logins
   *        will be fetched.
   * @return {Promise}
   *         Resolves with an array of login objects or null.
   *         Rejected if no delegate is attached.
   *         Login object string properties:
   *         { guid, origin, formActionOrigin, httpRealm, username, password }
   */
  fetchLogins(aDomain = null) {
    debug`fetchLogins for ${aDomain ?? "All domains"}`;

    return lazy.EventDispatcher.instance.sendRequestForResult(
      "GeckoView:Autocomplete:Fetch:Login",
      { domain: aDomain }
    );
  },

  /**
   * Delegates credit card entry fetching to the attached LoginStorage
   * GeckoView delegate.
   *
   * @return {Promise}
   *         Resolves with an array of credit card objects or null.
   *         Rejected if no delegate is attached.
   *         Login object string properties:
   *         { guid, name, number, expMonth, expYear, type }
   */
  fetchCreditCards() {
    debug`fetchCreditCards`;

    return lazy.EventDispatcher.instance.sendRequestForResult(
      "GeckoView:Autocomplete:Fetch:CreditCard"
    );
  },

  /**
   * Delegates address entry fetching to the attached LoginStorage
   * GeckoView delegate.
   *
   * @return {Promise}
   *         Resolves with an array of address objects or null.
   *         Rejected if no delegate is attached.
   *         Login object string properties:
   *         { guid, name, givenName, additionalName, familyName,
   *           organization, streetAddress, addressLevel1, addressLevel2,
   *           addressLevel3, postalCode, country, tel, email }
   */
  fetchAddresses() {
    debug`fetchAddresses`;

    return lazy.EventDispatcher.instance.sendRequestForResult(
      "GeckoView:Autocomplete:Fetch:Address"
    );
  },

  /**
   * Delegates credit card entry saving to the attached LoginStorage GeckoView delegate.
   * Call this when a new or modified credit card entry has been submitted.
   *
   * @param aCreditCard The {CreditCard} to be saved.
   */
  onCreditCardSave(aCreditCard) {
    debug`onCreditCardSave ${aCreditCard}`;

    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:Autocomplete:Save:CreditCard",
      { creditCard: aCreditCard }
    );
  },

  /**
   * Delegates address entry saving to the attached LoginStorage GeckoView delegate.
   * Call this when a new or modified address entry has been submitted.
   *
   * @param aAddress The {Address} to be saved.
   */
  onAddressSave(aAddress) {
    debug`onAddressSave ${aAddress}`;

    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:Autocomplete:Save:Address",
      { address: aAddress }
    );
  },

  /**
   * Delegates login entry saving to the attached LoginStorage GeckoView delegate.
   * Call this when a new login entry or a new password for an existing login
   * entry has been submitted.
   *
   * @param aLogin The {LoginEntry} to be saved.
   */
  onLoginSave(aLogin) {
    debug`onLoginSave ${aLogin}`;

    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:Autocomplete:Save:Login",
      { login: aLogin }
    );
  },

  /**
   * Delegates login entry password usage to the attached LoginStorage GeckoView
   * delegate.
   * Call this when the password of an existing login entry, as returned by
   * fetchLogins, has been used for autofill.
   *
   * @param aLogin The {LoginEntry} whose password was used.
   */
  onLoginPasswordUsed(aLogin) {
    debug`onLoginUsed ${aLogin}`;

    lazy.EventDispatcher.instance.sendRequest(
      "GeckoView:Autocomplete:Used:Login",
      {
        usedFields: UsedField.PASSWORD,
        login: aLogin,
      }
    );
  },

  /**
   * Delegates login entry selection.
   * Call this when there are multiple login entry option for a form to delegate
   * the selection.
   *
   * @param aBrowser The browser instance the triggered the selection.
   * @param aOptions The list of {SelectOption} depicting viable options.
   */
  onLoginSelect(aBrowser, aOptions) {
    debug`onLoginSelect ${aOptions}`;

    return new Promise((resolve, reject) => {
      if (!aBrowser || !aOptions) {
        debug`onLoginSelect Rejecting - no browser or options provided`;
        reject();
        return;
      }

      const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
      prompt.asyncShowPrompt(
        {
          type: "Autocomplete:Select:Login",
          options: aOptions,
        },
        result => {
          if (!result || !result.selection) {
            reject();
            return;
          }

          const option = new SelectOption({
            value: LoginEntry.parse(result.selection.value),
            hint: result.selection.hint,
          });
          resolve(option);
        }
      );
      this._prompt = prompt;
    });
  },

  /**
   * Delegates credit card entry selection.
   * Call this when there are multiple credit card entry option for a form to delegate
   * the selection.
   *
   * @param aBrowser The browser instance the triggered the selection.
   * @param aOptions The list of {SelectOption} depicting viable options.
   */
  onCreditCardSelect(aBrowser, aOptions) {
    debug`onCreditCardSelect ${aOptions}`;

    return new Promise((resolve, reject) => {
      if (!aBrowser || !aOptions) {
        debug`onCreditCardSelect Rejecting - no browser or options provided`;
        reject();
        return;
      }

      const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
      prompt.asyncShowPrompt(
        {
          type: "Autocomplete:Select:CreditCard",
          options: aOptions,
        },
        result => {
          if (!result || !result.selection) {
            reject();
            return;
          }

          const option = new SelectOption({
            value: CreditCard.parse(result.selection.value),
            hint: result.selection.hint,
          });
          resolve(option);
        }
      );
      this._prompt = prompt;
    });
  },

  /**
   * Delegates address entry selection.
   * Call this when there are multiple address entry option for a form to delegate
   * the selection.
   *
   * @param aBrowser The browser instance the triggered the selection.
   * @param aOptions The list of {SelectOption} depicting viable options.
   */
  onAddressSelect(aBrowser, aOptions) {
    debug`onAddressSelect ${aOptions}`;

    return new Promise((resolve, reject) => {
      if (!aBrowser || !aOptions) {
        debug`onAddressSelect Rejecting - no browser or options provided`;
        reject();
        return;
      }

      const prompt = new lazy.GeckoViewPrompter(aBrowser.documentGlobal);
      prompt.asyncShowPrompt(
        {
          type: "Autocomplete:Select:Address",
          options: aOptions,
        },
        result => {
          if (!result || !result.selection) {
            reject();
            return;
          }

          const option = new SelectOption({
            value: Address.parse(result.selection.value),
            hint: result.selection.hint,
          });
          resolve(option);
        }
      );
      this._prompt = prompt;
    });
  },

  async delegateSelection({
    browsingContext,
    options,
    inputElementIdentifier,
    formOrigin,
  }) {
    debug`delegateSelection ${options}`;

    if (!options.length) {
      return;
    }

    const { selectOptions, selectionType, loginStyle } =
      this._parseSelectOptions(options);

    if (selectOptions.length < 1) {
      debug`Abort delegateSelection - no valid options provided`;
      return;
    }

    const browser = browsingContext.top.embedderElement;
    const context = {
      browsingContext,
      browser,
      inputElementIdentifier,
      formOrigin,
      selectionType,
      loginStyle,
      // Identifies the specific document the selection belongs to. It changes
      // on navigation, so it lets us tell a still-live selection from a stale
      // one left over from a torn-down document.
      innerWindowId: browsingContext.currentWindowContext?.innerWindowId,
    };

    // If a live prompt of the same type is showing for the same document, keep
    // it and just re-point the fill target instead of dismissing and reopening
    // it (which would flicker). This covers both focus moving between same-type
    // fields and results being re-searched while the prompt is open. The option
    // set is identical across fields of the same form, so there is no need to
    // refresh the displayed options. We require a live prompt and a matching
    // document so a stale selection left over from a torn-down document is
    // never coalesced into.
    if (
      this._prompt &&
      this._activeSelection?.selectionType === selectionType &&
      this._activeSelection.innerWindowId === context.innerWindowId
    ) {
      debug`delegateSelection - coalescing into the active prompt`;
      this._cancelPendingDismiss();
      this._activeSelection = context;
      return;
    }

    // Otherwise this is a new, independent selection. Tear down whatever is
    // showing (a different type, or a stale prompt) and start fresh. We do not
    // wait for the previous selection to unwind; the generation check below
    // ensures it can neither clobber nor block this one.
    this._cancelPendingDismiss();
    try {
      this._prompt?.dismiss();
    } catch (e) {
      debug`delegateSelection - error dismissing previous prompt: ${e}`;
    }
    this._prompt = null;

    const generation = ++this._selectionGeneration;
    this._activeSelection = context;

    try {
      const selectedOption = await this._requestSelection(
        selectionType,
        browser,
        selectOptions
      );

      // A newer selection superseded us while the prompt was open; let it own
      // the shared state.
      if (generation !== this._selectionGeneration) {
        return;
      }

      // prompt is closed now.
      this._prompt = null;

      debug`delegateSelection selected option: ${selectedOption}`;

      if (!selectedOption) {
        debug`Abort delegateSelection - no option selected`;
        return;
      }

      // Read the fill context, which may have been re-pointed to a different
      // field while the prompt was open.
      await this._fillSelection(this._activeSelection, selectedOption);
    } finally {
      if (generation === this._selectionGeneration) {
        this._cancelPendingDismiss();
        this._prompt = null;
        this._activeSelection = null;
      }
    }
  },

  /**
   * Parses the raw autocomplete options into {SelectOption}s and determines the
   * selection type and login style.
   *
   * @param aOptions The raw autocomplete options from the content process.
   * @returns {object} { selectOptions, selectionType, loginStyle }
   */
  _parseSelectOptions(aOptions) {
    let insecureHint = SelectOption.Hint.NONE;
    let loginStyle = null;

    // TODO: Replace this string with more robust mechanics.
    let selectionType = null;
    const selectOptions = [];

    for (const option of aOptions) {
      switch (option.style) {
        case "insecureWarning": {
          // We depend on the insecure warning to be the first option.
          insecureHint = SelectOption.Hint.INSECURE_FORM;
          break;
        }
        case "generatedPassword": {
          selectionType = "login";
          const comment = JSON.parse(option.comment);
          selectOptions.push(
            new SelectOption({
              value: new LoginEntry({
                password: comment.generatedPassword,
              }),
              hint: SelectOption.Hint.GENERATED | insecureHint,
            })
          );
          break;
        }
        case "login":
        // Fallthrough.
        case "loginWithOrigin": {
          selectionType = "login";
          loginStyle = option.style;
          const comment = JSON.parse(option.comment);

          let hint = SelectOption.Hint.NONE | insecureHint;
          if (comment.isDuplicateUsername) {
            hint |= SelectOption.Hint.DUPLICATE_USERNAME;
          }
          if (comment.isOriginMatched) {
            hint |= SelectOption.Hint.MATCHING_ORIGIN;
          }

          selectOptions.push(
            new SelectOption({
              value: LoginEntry.parse(comment.fillMessageData),
              hint,
            })
          );
          break;
        }
        case "autofill": {
          const { fillMessageData } = JSON.parse(option.comment);
          const profile = fillMessageData.profile;
          debug`delegateSelection - autofill profile ${profile}`;
          const creditCard = CreditCard.fromGecko(profile);
          const address = Address.fromGecko(profile);
          if (creditCard.isValid()) {
            selectionType = "creditCard";
            selectOptions.push(
              new SelectOption({
                value: creditCard,
                hint: insecureHint,
              })
            );
          } else if (address.isValid()) {
            selectionType = "address";
            selectOptions.push(
              new SelectOption({
                value: address,
                hint: insecureHint,
              })
            );
          }
          break;
        }
        case "generic": {
          const { fillMessageName } = JSON.parse(option.comment);
          if (fillMessageName == "PasswordManager:firefoxRelay") {
            // The Relay option may be passed along with address autocomplete items.
            // Only set the selection type if it has not already been set.
            if (!selectionType) {
              selectionType = "login";
            }
            selectOptions.push(
              new SelectOption({
                value: {},
                hint: SelectOption.Hint.FIREFOX_RELAY | insecureHint,
              })
            );
          }
          break;
        }
        default:
          debug`delegateSelection - ignoring unknown option style ${option.style}`;
      }
    }

    return { selectOptions, selectionType, loginStyle };
  },

  /**
   * Shows the selection prompt for the given type and resolves with the option
   * the user selected, or null if the prompt was dismissed or no delegate is
   * attached.
   */
  _requestSelection(aSelectionType, aBrowser, aSelectOptions) {
    const onError = _ => {
      debug`No GV delegate attached`;
    };
    if (aSelectionType === "login") {
      return this.onLoginSelect(aBrowser, aSelectOptions).catch(onError);
    } else if (aSelectionType === "creditCard") {
      return this.onCreditCardSelect(aBrowser, aSelectOptions).catch(onError);
    } else if (aSelectionType === "address") {
      return this.onAddressSelect(aBrowser, aSelectOptions).catch(onError);
    }
    return Promise.resolve(null);
  },

  /**
   * Fills the form for the selected option using the given fill context.
   *
   * @param aSession The fill context (browsingContext, browser,
   *                 inputElementIdentifier, formOrigin, selectionType,
   *                 loginStyle).
   * @param aSelectedOption The {SelectOption} the user selected.
   */
  async _fillSelection(aSession, aSelectedOption) {
    if (
      aSession.selectionType === "login" ||
      SelectOption.Hint.FIREFOX_RELAY & aSelectedOption.hint
    ) {
      const selectedLogin = aSelectedOption?.value?.toLoginInfo();

      if (!selectedLogin) {
        debug`Abort delegateSelection - no login entry selected`;
        return;
      }

      debug`delegateSelection - filling form`;

      if (aSelectedOption.hint & SelectOption.Hint.GENERATED) {
        this.onLoginSave(selectedLogin);
      }

      const actor =
        aSession.browsingContext.currentWindowGlobal.getActor("LoginManager");

      await actor.fillForm({
        browser: aSession.browser,
        inputElementIdentifier: aSession.inputElementIdentifier,
        loginFormOrigin: aSession.formOrigin,
        login: selectedLogin,
        style:
          aSelectedOption.hint & SelectOption.Hint.GENERATED
            ? "generatedPassword"
            : aSession.loginStyle,
      });
    } else if (aSession.selectionType === "creditCard") {
      const actor =
        aSession.browsingContext.currentWindowGlobal.getActor("FormAutofill");
      const elementId = JSON.stringify(aSession.inputElementIdentifier);
      const selectedCreditCard = aSelectedOption?.value?.toGecko();

      actor.autofillFields(elementId, selectedCreditCard);
    } else if (aSession.selectionType === "address") {
      const actor =
        aSession.browsingContext.currentWindowGlobal.getActor("FormAutofill");
      const elementId = JSON.stringify(aSession.inputElementIdentifier);
      const selectedAddress = aSelectedOption?.value?.toGecko();

      actor.autofillFields(elementId, selectedAddress);
    }

    debug`delegateSelection - form filled`;
  },

  _cancelPendingDismiss() {
    if (this._pendingDismissTimer) {
      lazy.clearTimeout(this._pendingDismissTimer);
      this._pendingDismissTimer = null;
    }
  },

  /**
   * Tears down any in-flight selection whose document is going away, so a
   * prompt can't outlive its page. Called when a browsing context is destroyed
   * (e.g. navigation). Scoped by the destroyed document's inner window id so we
   * never dismiss a prompt owned by another, still-live context.
   *
   * @param aInnerWindowId The inner window id of the document being destroyed.
   */
  reset(aInnerWindowId) {
    if (
      !this._activeSelection ||
      this._activeSelection.innerWindowId !== aInnerWindowId
    ) {
      return;
    }

    debug`reset - tearing down selection for destroyed document`;
    this._cancelPendingDismiss();
    try {
      this._prompt?.dismiss();
    } catch (e) {
      debug`reset - error dismissing prompt: ${e}`;
    }
    this._prompt = null;
    this._activeSelection = null;
    // Bump the generation so the orphaned in-flight selection, if it ever
    // resumes, no-ops instead of clobbering later state.
    this._selectionGeneration++;
  },

  delegateDismiss() {
    debug`delegateDismiss`;

    if (!this._prompt) {
      return;
    }

    // Defer the dismissal briefly. If focus moves to another field of the same
    // type, the follow-up delegateSelection will cancel this and keep the
    // prompt alive, avoiding a dismiss/reopen flicker. If nothing follows
    // (focus left all fields), the prompt is dismissed for real. We capture the
    // prompt so a deferred dismissal only ever closes the prompt it was
    // scheduled for, never one that replaced it in the meantime.
    this._cancelPendingDismiss();
    const promptToDismiss = this._prompt;
    this._pendingDismissTimer = lazy.setTimeout(() => {
      this._pendingDismissTimer = null;
      if (this._prompt === promptToDismiss) {
        try {
          this._prompt.dismiss();
        } catch (e) {
          debug`delegateDismiss - error dismissing prompt: ${e}`;
        }
      }
    }, lazy.dismissCoalesceDelayMs);
  },
};

const { debug } = GeckoViewUtils.initLogging("GeckoViewAutocomplete");
