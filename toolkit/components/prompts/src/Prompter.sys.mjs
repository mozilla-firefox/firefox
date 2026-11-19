/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { PromptUtils } from "resource://gre/modules/PromptUtils.sys.mjs";
import { BrowserUtils } from "resource://gre/modules/BrowserUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ClipboardContextMenu: "resource://gre/modules/ClipboardContextMenu.sys.mjs",
});

const {
  MODAL_TYPE_TAB,
  MODAL_TYPE_CONTENT,
  MODAL_TYPE_WINDOW,
  MODAL_TYPE_INTERNAL_WINDOW,
} = Ci.nsIPrompt;

const COMMON_DIALOG = "chrome://global/content/commonDialog.xhtml";
const SELECT_DIALOG = "chrome://global/content/selectDialog.xhtml";

const BUTTON_L10N_IDS = {
  [Ci.nsIPrompt.BUTTON_TITLE_OK]: "dialog-button-accept",
  [Ci.nsIPrompt.BUTTON_TITLE_CANCEL]: "dialog-button-cancel",
  [Ci.nsIPrompt.BUTTON_TITLE_YES]: "dialog-button-yes",
  [Ci.nsIPrompt.BUTTON_TITLE_NO]: "dialog-button-no",
  [Ci.nsIPrompt.BUTTON_TITLE_SAVE]: "dialog-button-save",
  [Ci.nsIPrompt.BUTTON_TITLE_DONT_SAVE]: "dialog-button-dont-save",
  [Ci.nsIPrompt.BUTTON_TITLE_REVERT]: "dialog-button-revert",
};

export function Prompter() {}

/**
 * Implements nsIPromptService and nsIPromptFactory
 *
 * @class Prompter
 */
Prompter.prototype = {
  classID: Components.ID("{1c978d25-b37f-43a8-a2d6-0c7a239ead87}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIPromptFactory",
    "nsIPromptService",
  ]),

  /* ----------  private members  ---------- */

  pickPrompter(options) {
    return new ModalPrompter(options);
  },

  /* ----------  nsIPromptFactory  ---------- */

  getPrompt(domWin, iid) {
    // This is still kind of dumb; the C++ code delegated to login manager
    // here, which in turn calls back into us via nsIPromptService.
    if (iid.equals(Ci.nsIAuthPrompt2) || iid.equals(Ci.nsIAuthPrompt)) {
      try {
        let pwmgr = Cc[
          "@mozilla.org/passwordmanager/authpromptfactory;1"
        ].getService(Ci.nsIPromptFactory);
        return pwmgr.getPrompt(domWin, iid);
      } catch (e) {
        console.error("nsPrompter: Delegation to password manager failed: ", e);
      }
    }

    let p = new ModalPrompter({ domWin });
    p.QueryInterface(iid);
    return p;
  },

  /* ----------  nsIPromptService  ---------- */

  /**
   * Puts up an alert dialog with an OK button.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   */
  alert(domWin, title, text) {
    let p = this.pickPrompter({ domWin });
    p.alert(title, text);
  },

  /**
   * Puts up an alert dialog with an OK button.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   */
  alertBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    p.alert(...promptArgs);
  },

  /**
   * Puts up an alert dialog with an OK button.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @returns {Promise} A promise which resolves when the prompt is dismissed.
   */
  asyncAlert(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.alert(...promptArgs);
  },

  /**
   * Puts up an alert dialog with an OK button and a labeled checkbox.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   */
  alertCheck(domWin, title, text, checkLabel, checkValue) {
    let p = this.pickPrompter({ domWin });
    p.alertCheck(title, text, checkLabel, checkValue);
  },

  /**
   * Puts up an alert dialog with an OK button and a labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   */
  alertCheckBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    p.alertCheck(...promptArgs);
  },

  /**
   * Puts up an alert dialog with an OK button and a labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {boolean} checkValue - The initial checked state of the checkbox.
   * @returns {Promise<nsIPropertyBag<{checked: boolean}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncAlertCheck(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.alertCheck(...promptArgs);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @returns {boolean} true for OK, false for Cancel.
   */
  confirm(domWin, title, text) {
    let p = this.pickPrompter({ domWin });
    return p.confirm(title, text);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @returns {boolean} true for OK, false for Cancel.
   */
  confirmBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.confirm(...promptArgs);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @returns {Promise<nsIPropertyBag<{ok: boolean}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncConfirm(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.confirm(...promptArgs);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons and a labeled checkbox.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   */
  confirmCheck(domWin, title, text, checkLabel, checkValue) {
    let p = this.pickPrompter({ domWin });
    return p.confirmCheck(title, text, checkLabel, checkValue);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons and a labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   * @returns {boolean} true for OK, false for Cancel
   */
  confirmCheckBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.confirmCheck(...promptArgs);
  },

  /**
   * Puts up a dialog with OK and Cancel buttons and a labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} checkLabel - Text to appear with the checkbox.
   * @param {boolean} checkValue - The initial checked state of the checkbox.
   * @returns {Promise<nsIPropertyBag<{ok: boolean, checked: boolean}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncConfirmCheck(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.confirmCheck(...promptArgs);
  },

  /**
   * Puts up a dialog with up to 3 buttons and an optional, labeled checkbox.
   *
   * Buttons are numbered 0 - 2. Button 0 is the default button unless one of
   * the Button Default Flags is specified.
   *
   * A button may use a predefined title, specified by one of the Button Title
   * Flags values.  Each title value can be multiplied by a position value to
   * assign the title to a particular button.  If BUTTON_TITLE_IS_STRING is
   * used for a button, the string parameter for that button will be used.  If
   * the value for a button position is zero, the button will not be shown.
   *
   * In general, flags is constructed per the following example:
   *
   * flags = (BUTTON_POS_0) * (BUTTON_TITLE_AAA) +
   *         (BUTTON_POS_1) * (BUTTON_TITLE_BBB) +
   *         BUTTON_POS_1_DEFAULT;
   *
   * where "AAA" and "BBB" correspond to one of the button titles.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {number} flags - A combination of Button Flags.
   * @param {string} button0 - Used when button 0 uses TITLE_IS_STRING.
   * @param {string} button1 - Used when button 1 uses TITLE_IS_STRING.
   * @param {string} button2 - Used when button 2 uses TITLE_IS_STRING.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        Null if no checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method
   *        is called and the final checked state after this method returns.
   * @returns {number} The index of the button pressed.
   */
  confirmEx(
    domWin,
    title,
    text,
    flags,
    button0,
    button1,
    button2,
    checkLabel,
    checkValue
  ) {
    let p = this.pickPrompter({ domWin });
    return p.confirmEx(
      title,
      text,
      flags,
      button0,
      button1,
      button2,
      checkLabel,
      checkValue
    );
  },

  /**
   * Puts up a dialog with up to 3 buttons and an optional, labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {number} flags - A combination of Button Flags.
   * @param {string} button0 - Used when button 0 uses TITLE_IS_STRING.
   * @param {string} button1 - Used when button 1 uses TITLE_IS_STRING.
   * @param {string} button2 - Used when button 2 uses TITLE_IS_STRING.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        Null if no checkbox.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   * @returns {number} The index of the button pressed.
   */
  confirmExBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.confirmEx(...promptArgs);
  },

  /**
   * Puts up a dialog with up to 3 buttons and an optional, labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {number} flags - A combination of Button Flags.
   * @param {string} button0 - Used when button 0 uses TITLE_IS_STRING.
   * @param {string} button1 - Used when button 1 uses TITLE_IS_STRING.
   * @param {string} button2 - Used when button 2 uses TITLE_IS_STRING.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        Null if no checkbox.
   * @param {boolean} checkValue - The initial checked state of the checkbox.
   * @param {object} [extraArgs] - Extra arguments for the prompt metadata.
   * @returns {Promise<nsIPropertyBag<{buttonNumClicked: number, checked: boolean, isExtra1Secondary: boolean}>>}
   */
  asyncConfirmEx(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.confirmEx(...promptArgs);
  },

  /**
   * Puts up a dialog with an edit field and an optional, labeled checkbox.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} value - Contains the default value for the dialog field
   *        when this method is called (null value is ok).  Upon return, if
   *        the user pressed OK, then this parameter contains a newly
   *        allocated string value.
   *        Otherwise, the parameter's value is unmodified.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        If null, check box will not be shown.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   * @returns {boolean} true for OK, false for Cancel.
   */
  prompt(domWin, title, text, value, checkLabel, checkValue) {
    let p = this.pickPrompter({ domWin });
    return p.nsIPrompt_prompt(title, text, value, checkLabel, checkValue);
  },

  /**
   * Puts up a dialog with an edit field and an optional, labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} value - Contains the default value for the dialog field
   *        when this method is called (null value is ok).  Upon return, if
   *        the user pressed OK, then this parameter contains a newly
   *        allocated string value.
   *        Otherwise, the parameter's value is unmodified.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        If null, check box will not be shown.
   * @param {object} checkValue - Contains the initial checked state of the
   *        checkbox when this method is called and the final checked state
   *        after this method returns.
   * @returns {boolean} true for OK, false for Cancel.
   */
  promptBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.nsIPrompt_prompt(...promptArgs);
  },

  /**
   * Puts up a dialog with an edit field and an optional, labeled checkbox.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} value - The default value for the dialog text field.
   * @param {string} checkLabel - Text to appear with the checkbox.
   *        If null, check box will not be shown.
   * @param {boolean} checkValue - The initial checked state of the checkbox.
   * @returns {Promise<nsIPropertyBag<{ok: boolean, checked: boolean, value: string}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncPrompt(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.nsIPrompt_prompt(...promptArgs);
  },

  /**
   * Puts up a dialog with an edit field and a password field.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} user - Contains the default value for the username
   *        field when this method is called (null value is ok).
   *        Upon return, if the user pressed OK, then this parameter contains
   *        a newly allocated string value. Otherwise, the parameter's value
   *        is unmodified.
   * @param {object} pass - Contains the default value for the password field
   *        when this method is called (null value is ok). Upon return, if the
   *        user pressed OK, this parameter contains a newly allocated string
   *        value. Otherwise, the parameter's value is unmodified.
   * @returns {boolean} true for OK, false for Cancel.
   */
  promptUsernameAndPassword(domWin, title, text, user, pass) {
    let p = this.pickPrompter({ domWin });
    return p.nsIPrompt_promptUsernameAndPassword(null, title, text, user, pass);
  },

  /**
   * Puts up a dialog with an edit field and a password field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} user - Contains the default value for the username
   *        field when this method is called (null value is ok).
   *        Upon return, if the user pressed OK, then this parameter contains
   *        a newly allocated string value. Otherwise, the parameter's value
   *        is unmodified.
   * @param {object} pass - Contains the default value for the password field
   *        when this method is called (null value is ok). Upon return, if the
   *        user pressed OK, this parameter contains a newly allocated string
   *        value. Otherwise, the parameter's value is unmodified.
   * @returns {boolean} true for OK, false for Cancel.
   */
  promptUsernameAndPasswordBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.nsIPrompt_promptUsernameAndPassword(null, ...promptArgs);
  },

  /**
   * Puts up a dialog with an edit field and a password field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} user - Default value for the username field.
   * @param {string} pass - Contains the default value for the password field.
   * @returns {Promise<nsIPropertyBag<{ok: boolean, user: string, pass: string}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncPromptUsernameAndPassword(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.nsIPrompt_promptUsernameAndPassword(null, ...promptArgs);
  },

  /**
   * Puts up a dialog with a password field.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} pass - Contains the default value for the password field
   *        when this method is called (null value is ok). Upon return, if the
   *        user pressed OK, this parameter contains a newly allocated string
   *        value. Otherwise, the parameter's value is unmodified.
   * @returns {boolean} true for OK, false for Cancel.
   */
  promptPassword(domWin, title, text, pass) {
    let p = this.pickPrompter({ domWin });
    return p.nsIPrompt_promptPassword(
      null, // no channel.
      title,
      text,
      pass
    );
  },

  /**
   * Puts up a dialog with a password field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {object} pass - Contains the default value for the password field
   *        when this method is called (null value is ok). Upon return, if the
   *        user pressed OK, this parameter contains a newly allocated string
   *        value. Otherwise, the parameter's value is unmodified.
   * @returns {boolean} true for OK, false for Cancel.
   */
  promptPasswordBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.nsIPrompt_promptPassword(null, ...promptArgs);
  },

  /**
   * Puts up a dialog with a password field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string} pass - Contains the default value for the password field.
   * @returns {Promise<nsIPropertyBag<{ok: boolean, pass: string}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncPromptPassword(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.nsIPrompt_promptPassword(null, ...promptArgs);
  },

  /**
   * Puts up a dialog box which has a list box of strings from which the user
   * may make a single selection.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string[]} list - The list of strings to display.
   * @param {object} selected - Contains the index of the selected item in the
   *        list when this method returns true.
   * @returns {boolean} true for OK, false for Cancel.
   */
  select(domWin, title, text, list, selected) {
    let p = this.pickPrompter({ domWin });
    return p.select(title, text, list, selected);
  },

  /**
   * Puts up a dialog box which has a list box of strings from which the user
   * may make a single selection.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string[]} list - The list of strings to display.
   * @param {object} selected - Contains the index of the selected item in the
   *        list when this method returns true.
   * @returns {boolean} true for OK, false for Cancel.
   */
  selectBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.select(...promptArgs);
  },

  /**
   * Puts up a dialog box which has a list box of strings from which the user
   * may make a single selection.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {string} title - Text to appear in the title of the dialog.
   * @param {string} text - Text to appear in the body of the dialog.
   * @param {string[]} list - The list of strings to display.
   * @returns {Promise<nsIPropertyBag<{selected: number, ok: boolean}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncSelect(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.select(...promptArgs);
  },

  /**
   * Requests a username and a password. Shows a dialog with username and
   * password field, depending on flags also a domain field.
   *
   * @param {mozIDOMWindowProxy} domWin - The parent window or null.
   * @param {nsIChannel} channel - The channel that requires authentication.
   * @param {number} level - Security level of the credential transmission.
   *        Any of nsIAuthPrompt2.<LEVEL_NONE|LEVEL_PW_ENCRYPTED|LEVEL_SECURE>
   * @param {nsIAuthInformation} authInfo - Authentication information object.
   * @returns {boolean}
   *          true: Authentication can proceed using the values
   *          in the authInfo object.
   *          false: Authentication should be cancelled, usually because the
   *          user did not provide username/password.
   */
  promptAuth(domWin, channel, level, authInfo) {
    let p = this.pickPrompter({ domWin });
    return p.promptAuth(channel, level, authInfo);
  },

  /**
   * Requests a username and a password. Shows a dialog with username and
   * password field, depending on flags also a domain field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {nsIChannel} channel - The channel that requires authentication.
   * @param {number} level - Security level of the credential transmission.
   *        Any of nsIAuthPrompt2.<LEVEL_NONE|LEVEL_PW_ENCRYPTED|LEVEL_SECURE>
   * @param {nsIAuthInformation} authInfo - Authentication information object.
   * @returns {boolean}
   *          true: Authentication can proceed using the values
   *          in the authInfo object.
   *          false: Authentication should be cancelled, usually because the
   *          user did not provide username/password.
   */
  promptAuthBC(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType });
    return p.promptAuth(...promptArgs);
  },

  /**
   * Requests a username and a password. Shows a dialog with username and
   * password field, depending on flags also a domain field.
   *
   * @param {BrowsingContext} browsingContext - The browsing context the
   *        prompt should be opened for.
   * @param {number} modalType - The modal type of the prompt.
   *        nsIPromptService.<MODAL_TYPE_WINDOW|MODAL_TYPE_TAB|MODAL_TYPE_CONTENT>
   * @param {nsIChannel} channel - The channel that requires authentication.
   * @param {number} level - Security level of the credential transmission.
   *        Any of nsIAuthPrompt2.<LEVEL_NONE|LEVEL_PW_ENCRYPTED|LEVEL_SECURE>
   * @param {nsIAuthInformation} authInfo - Authentication information object.
   * @returns {Promise<nsIPropertyBag<{ok: boolean}>>}
   *          A promise which resolves when the prompt is dismissed.
   */
  asyncPromptAuth(browsingContext, modalType, ...promptArgs) {
    let p = this.pickPrompter({ browsingContext, modalType, async: true });
    return p.promptAuth(...promptArgs);
  },

  /**
   * Displays a contextmenu to get user confirmation for clipboard read. Only
   * one context menu can be opened at a time.
   *
   * @param {WindowContext} windowContext - The window context that initiates
   *        the clipboard operation.
   * @returns {Promise<nsIPropertyBag<{ok: boolean}>>}
   *          A promise which resolves when the contextmenu is dismissed.
   */
  confirmUserPaste() {
    return lazy.ClipboardContextMenu.confirmUserPaste(...arguments);
  },
};

// Common utils not specific to a particular prompter style.
var InternalPromptUtils = {
  getLocalizedString(key, formatArgs) {
    if (formatArgs) {
      return this.strBundle.formatStringFromName(key, formatArgs);
    }
    return this.strBundle.GetStringFromName(key);
  },

  confirmExHelper(flags, button0, button1, button2) {
    const BUTTON_DEFAULT_MASK = 0x03000000;
    let defaultButtonNum = (flags & BUTTON_DEFAULT_MASK) >> 24;
    let isDelayEnabled = flags & Ci.nsIPrompt.BUTTON_DELAY_ENABLE;

    // Sanity check: If the flags indicate there should be no button0 then flags
    // must contain BUTTON_NONE (notably, it must include BUTTON_NONE_ENABLE_BIT).
    let allowNoButtons =
      (flags & Ci.nsIPromptService.BUTTON_NONE) ==
      Ci.nsIPromptService.BUTTON_NONE;
    const NO_BUTTON0 =
      Ci.nsIPrompt.BUTTON_POS_0 * Ci.nsIPrompt.BUTTON_TITLE_IS_STRING;
    if (!allowNoButtons && !button0 && (flags & NO_BUTTON0) == NO_BUTTON0) {
      throw new Error(
        `Request for modal prompt with no buttons requires flags to be ` +
          `BUTTON_NONE.  Got ${flags}`
      );
    }
    if (allowNoButtons && (button0 || button1 || button2)) {
      throw new Error(
        `Request for modal prompt with no buttons requires button names to be ` +
          `null.  Got ${button0}, ${button1}, ${button2}`
      );
    }

    // Flags can be used to select a specific pre-defined button label or
    // a caller-supplied string (button0/button1/button2). If no flags are
    // set for a button, then the button won't be shown.
    let argText = [button0, button1, button2];
    let buttonLabels = [null, null, null];
    let buttonL10nIds = [null, null, null];
    for (let i = 0; i < 3; i++) {
      let title = flags & 0xff;
      if (title == Ci.nsIPrompt.BUTTON_TITLE_IS_STRING) {
        buttonLabels[i] = argText[i] || null;
      } else if (BUTTON_L10N_IDS[title]) {
        buttonL10nIds[i] = BUTTON_L10N_IDS[title];
      }
      flags >>= 8;
    }

    return [
      buttonLabels,
      buttonL10nIds,
      defaultButtonNum,
      isDelayEnabled,
      allowNoButtons,
    ];
  },

  getAuthInfo(authInfo) {
    let username, password;

    let flags = authInfo.flags;
    if (flags & Ci.nsIAuthInformation.NEED_DOMAIN && authInfo.domain) {
      username = authInfo.domain + "\\" + authInfo.username;
    } else {
      username = authInfo.username;
    }

    password = authInfo.password;

    return [username, password];
  },

  setAuthInfo(authInfo, username, password) {
    let flags = authInfo.flags;
    if (flags & Ci.nsIAuthInformation.NEED_DOMAIN) {
      // Domain is separated from username by a backslash
      let idx = username.indexOf("\\");
      if (idx == -1) {
        authInfo.username = username;
      } else {
        authInfo.domain = username.substring(0, idx);
        authInfo.username = username.substring(idx + 1);
      }
    } else {
      authInfo.username = username;
    }
    authInfo.password = password;
  },

  makeAuthMessage(prompt, channel, authInfo) {
    if (prompt.modalType != MODAL_TYPE_TAB) {
      return this._legacyMakeAuthMessage(channel, authInfo);
    }

    let isProxy = authInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY;
    let isPassOnly = authInfo.flags & Ci.nsIAuthInformation.ONLY_PASSWORD;
    let isCrossOrig =
      authInfo.flags & Ci.nsIAuthInformation.CROSS_ORIGIN_SUB_RESOURCE;
    let username = authInfo.username;

    // We use the realm and displayHost only for proxy auth,
    // and the displayHostOnly only for x-origin auth prompts.
    // Otherwise we rely on the title of the dialog displaying the correct
    // title.
    let { displayHost, realm, displayHostOnly } = PromptUtils.getAuthTarget(
      channel,
      authInfo
    );

    if (isProxy) {
      // The realm is server-controlled. Trim it if it's very long, to
      // avoid the dialog becoming unusable.
      // For background, see https://bugzilla.mozilla.org/show_bug.cgi?id=244273
      if (realm.length > 150) {
        realm = realm.substring(0, 150);
        // Append "..." (or localized equivalent).
        realm += Services.locale.ellipsis;
      }

      return this.getLocalizedString("EnterLoginForProxy3", [
        realm,
        displayHost,
      ]);
    }
    if (isPassOnly) {
      return this.getLocalizedString("EnterPasswordOnlyFor", [username]);
    }
    if (isCrossOrig) {
      return this.getLocalizedString("EnterCredentialsCrossOrigin", [
        displayHostOnly,
      ]);
    }
    return this.getLocalizedString("EnterCredentials");
  },

  _legacyMakeAuthMessage(channel, authInfo) {
    let isProxy = authInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY;
    let isPassOnly = authInfo.flags & Ci.nsIAuthInformation.ONLY_PASSWORD;
    let isCrossOrig =
      authInfo.flags & Ci.nsIAuthInformation.CROSS_ORIGIN_SUB_RESOURCE;

    let username = authInfo.username;
    let { displayHost, realm } = PromptUtils.getAuthTarget(channel, authInfo);

    // Suppress "the site says: $realm" when we synthesized a missing realm.
    if (!authInfo.realm && !isProxy) {
      realm = "";
    }

    // The realm is server-controlled. Trim it if it's very long, to
    // avoid the dialog becoming unusable.
    // For background, see https://bugzilla.mozilla.org/show_bug.cgi?id=244273
    if (realm.length > 150) {
      realm = realm.substring(0, 150);
      // Append "..." (or localized equivalent).
      realm += Services.locale.ellipsis;
    }

    let text;
    if (isProxy) {
      text = this.getLocalizedString("EnterLoginForProxy3", [
        realm,
        displayHost,
      ]);
    } else if (isPassOnly) {
      text = this.getLocalizedString("EnterPasswordFor", [
        username,
        displayHost,
      ]);
    } else if (isCrossOrig) {
      text = this.getLocalizedString("EnterUserPasswordForCrossOrigin2", [
        displayHost,
      ]);
    } else if (!realm) {
      text = this.getLocalizedString("EnterUserPasswordFor2", [displayHost]);
    } else {
      text = this.getLocalizedString("EnterLoginForRealm3", [
        realm,
        displayHost,
      ]);
    }

    return text;
  },

  getBrandFullName() {
    return this.brandBundle.GetStringFromName("brandFullName");
  },
};

ChromeUtils.defineLazyGetter(InternalPromptUtils, "strBundle", function () {
  let bundle = Services.strings.createBundle(
    "chrome://global/locale/commonDialogs.properties"
  );
  if (!bundle) {
    throw new Error("String bundle for Prompter not present!");
  }
  return bundle;
});

ChromeUtils.defineLazyGetter(InternalPromptUtils, "brandBundle", function () {
  let bundle = Services.strings.createBundle(
    "chrome://branding/locale/brand.properties"
  );
  if (!bundle) {
    throw new Error("String bundle for branding not present!");
  }
  return bundle;
});

class ModalPrompter {
  constructor({
    browsingContext = null,
    domWin = null,
    modalType = null,
    async = false,
  }) {
    if (browsingContext && domWin) {
      throw new Error("Pass either browsingContext or domWin");
    }

    if (domWin) {
      // We have a domWin, get the associated browsing context
      this.browsingContext = BrowsingContext.getFromWindow(domWin);
    } else {
      this.browsingContext = browsingContext;
    }

    if (
      domWin &&
      (!modalType || modalType == MODAL_TYPE_WINDOW) &&
      !this.browsingContext?.isContent &&
      this.browsingContext?.associatedWindow?.gDialogBox
    ) {
      modalType = MODAL_TYPE_INTERNAL_WINDOW;
    }

    // Use given modal type or fallback to default
    this.modalType = modalType || ModalPrompter.defaultModalType;

    this.async = async;

    this.QueryInterface = ChromeUtils.generateQI([
      "nsIPrompt",
      "nsIAuthPrompt",
      "nsIAuthPrompt2",
      "nsIWritablePropertyBag2",
    ]);
  }

  set modalType(modalType) {
    // Setting modal type window is always allowed
    if (modalType == MODAL_TYPE_WINDOW) {
      this._modalType = modalType;
      return;
    }

    // For content prompts for non-content windows, use window prompts:
    if (modalType == MODAL_TYPE_CONTENT && !this.browsingContext?.isContent) {
      this._modalType = MODAL_TYPE_WINDOW;
      return;
    }

    // We can't use content / tab prompts if we don't have a suitable parent.
    if (
      !this.browsingContext?.isContent &&
      modalType != MODAL_TYPE_INTERNAL_WINDOW
    ) {
      // Only show this error if we're not about to fall back again and show a different one.
      if (this.browsingContext?.associatedWindow?.gDialogBox) {
        console.error(
          "Prompter: Browser not available. Falling back to internal window prompt."
        );
      }
      modalType = MODAL_TYPE_INTERNAL_WINDOW;
    }

    if (
      modalType == MODAL_TYPE_INTERNAL_WINDOW &&
      (this.browsingContext?.isContent ||
        !this.browsingContext?.associatedWindow?.gDialogBox)
    ) {
      console.error(
        "Prompter: internal dialogs not available in this context. Falling back to window prompt."
      );
      modalType = MODAL_TYPE_WINDOW;
    }

    this._modalType = modalType;
  }

  get modalType() {
    return this._modalType;
  }

  /* ---------- internal methods ---------- */

  /**
   * Synchronous wrapper around {@link ModalPrompter#openPrompt}
   *
   * @param {object} args Prompt arguments. When prompt has been closed, they are updated to reflect the result state.
   */
  openPromptSync(args) {
    let closed = false;
    this.openPrompt(args)
      .then(returnedArgs => {
        if (returnedArgs) {
          for (let key in returnedArgs) {
            args[key] = returnedArgs[key];
          }
        }
      })
      .finally(() => {
        closed = true;
      });
    Services.tm.spinEventLoopUntilOrQuit(
      "prompts/Prompter.sys.mjs:openPromptSync",
      () => closed
    );
  }

  async openPrompt(args) {
    if (!this.browsingContext) {
      // We don't have a browsing context, fallback to a window prompt.
      args.modalType = MODAL_TYPE_WINDOW;
      this.openWindowPrompt(null, args);
      return args;
    }

    if (this._modalType == MODAL_TYPE_INTERNAL_WINDOW) {
      await this.openInternalWindowPrompt(
        this.browsingContext.associatedWindow,
        args
      );
      return args;
    }

    // Select prompts are not part of CommonDialog
    // and thus not supported as tab or content prompts yet. See Bug 1622817.
    // Once they are integrated this override should be removed.
    if (args.promptType == "select" && this.modalType !== MODAL_TYPE_WINDOW) {
      console.error(
        "Prompter: 'select' prompts do not support tab/content prompting. Falling back to window prompt."
      );
      args.modalType = MODAL_TYPE_WINDOW;
    } else {
      args.modalType = this.modalType;
    }

    const IS_CONTENT =
      Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT;

    let actor;
    try {
      if (IS_CONTENT) {
        // When in the content, get the PromptChild actor.
        actor =
          this.browsingContext.window.windowGlobalChild.getActor("Prompt");
      } else {
        // When in the parent, get the PromptParent actor.
        actor = this.browsingContext.currentWindowGlobal.getActor("Prompt");
      }
    } catch (_) {
      // We can't get the prompt actor, fallback to window prompt.
      let parentWin;
      // If given a chrome BC we can try to get its window
      if (!this.browsingContext.isContent && this.browsingContext.window) {
        parentWin = this.browsingContext.window;
      } else {
        // Try to get the window which is the browsers parent
        parentWin = this.browsingContext.top?.embedderElement?.documentGlobal;
      }
      this.openWindowPrompt(parentWin, args);
      return args;
    }

    /* For prompts with a channel, we want to show the origin requesting
     * authentication. This is different from the prompt principal,
     * which is based on the document loaded in the browsing context over
     * which the prompt appears. So if page foo.com loads bar.com, and the
     * latter asks for auth, we want that bar.com's origin, not foo.com.
     * To avoid confusion, we use different properties
     * (authOrigin / promptPrincipal) to track this information.
     */
    if (args.channel) {
      try {
        // Bug 1767292: Display scheme if it is HTTP, otherwise omit it.
        args.authOrigin = BrowserUtils.formatURIForDisplay(args.channel.URI, {
          showInsecureHTTP: true,
        });
      } catch (ex) {
        args.authOrigin = args.channel.URI.prePath;
      }
      args.isInsecureAuth = args.channel.URI.schemeIs("http");
      // whether we are going to prompt the user for their credentials for a different base domain.
      // When true, auth prompt spoofing protection mechanisms will be triggered (see bug 791594).
      args.isTopLevelCrossDomainAuth = false;
      // We don't support auth prompt spoofing protections for sub resources and window prompts
      if (
        args.modalType == MODAL_TYPE_TAB &&
        args.channel.loadInfo.isTopLevelLoad
      ) {
        // check if this is a request from a third party
        try {
          args.isTopLevelCrossDomainAuth =
            this.browsingContext.currentWindowGlobal?.documentPrincipal?.isThirdPartyURI(
              args.channel.URI
            );
        } catch (e) {
          // isThirdPartyURI failes for about:/blob/data URIs. For error pages,
          // compare against the URI that failed to load instead, and treat
          // anything else as cross domain.
          try {
            args.isTopLevelCrossDomainAuth = Services.scriptSecurityManager
              .createContentPrincipal(this.browsingContext.currentURI, {})
              .isThirdPartyURI(args.channel.URI);
          } catch {
            args.isTopLevelCrossDomainAuth = true;
          }
        }
      }
    } else {
      args.promptPrincipal =
        this.browsingContext.window?.document.nodePrincipal;
    }
    if (IS_CONTENT) {
      let docShell = this.browsingContext.docShell;
      let inPermitUnload = docShell?.docViewer?.inPermitUnload;
      args.inPermitUnload = inPermitUnload;
      let eventDetail = Cu.cloneInto(
        {
          tabPrompt: this.modalType != MODAL_TYPE_WINDOW,
          inPermitUnload,
        },
        this.browsingContext.window
      );
      PromptUtils.fireDialogEvent(
        this.browsingContext.window,
        "DOMWillOpenModalDialog",
        null,
        eventDetail
      );

      // Put content window in the modal state while the prompt is open.
      let windowUtils = this.browsingContext.window?.windowUtils;
      if (windowUtils) {
        windowUtils.enterModalState();
      }
    } else if (args.inPermitUnload) {
      args.promptPrincipal =
        this.browsingContext.currentWindowGlobal.documentPrincipal;
    }

    // It is technically possible for multiple prompts to be sent from a single
    // BrowsingContext. See bug 1266353. We use a randomly generated UUID to
    // differentiate between the different prompts.
    let id = "id" + Services.uuid.generateUUID().toString();

    args._remoteId = args.promptID ?? id;

    let returnedArgs;
    try {
      if (IS_CONTENT) {
        // If we're in the content process, send a message to the PromptParent
        // window actor.
        returnedArgs = await actor.sendQuery("Prompt:Open", args);
      } else {
        // If we're in the parent process we already have the parent actor.
        // We can call its message handler directly.
        returnedArgs = await actor.receiveMessage({
          name: "Prompt:Open",
          data: args,
        });
      }

      if (returnedArgs?.promptAborted) {
        throw Components.Exception(
          "prompt aborted by user",
          Cr.NS_ERROR_NOT_AVAILABLE
        );
      }
    } finally {
      if (IS_CONTENT) {
        let windowUtils = this.browsingContext.window?.windowUtils;
        if (windowUtils) {
          windowUtils.leaveModalState();
        }
        PromptUtils.fireDialogEvent(
          this.browsingContext.window,
          "DOMModalDialogClosed"
        );
      }
    }
    return returnedArgs;
  }

  /**
   * Open a window modal prompt
   *
   * There's an implied contract that says modal prompts should still work when
   * no "parent" window is passed for the dialog (eg, the "Master Password"
   * dialog does this).  These prompts must be shown even if there are *no*
   * visible windows at all.
   * We try and find a window to use as the parent, but don't consider if that
   * is visible before showing the prompt. parentWindow may still be null if
   * there are _no_ windows open.
   *
   * @param {Window} [parentWindow] - The parent window for the prompt, may be
   *        null.
   * @param {object} args - Prompt options and return values.
   */
  openWindowPrompt(parentWindow = null, args) {
    let uri = args.promptType == "select" ? SELECT_DIALOG : COMMON_DIALOG;
    let propBag = PromptUtils.objectToPropBag(args);
    Services.ww.openWindow(
      parentWindow || Services.ww.activeWindow,
      uri,
      "_blank",
      "centerscreen,chrome,modal,titlebar",
      propBag
    );
    PromptUtils.propBagToObject(propBag, args);
  }

  async openInternalWindowPrompt(parentWindow, args) {
    if (!parentWindow?.gDialogBox) {
      this.openWindowPrompt(parentWindow, args);
      return;
    }
    let propBag = PromptUtils.objectToPropBag(args);
    propBag.setProperty("async", this.async);
    let uri = args.promptType == "select" ? SELECT_DIALOG : COMMON_DIALOG;
    await parentWindow.gDialogBox.open(uri, propBag);
    propBag.deleteProperty("async");
    PromptUtils.propBagToObject(propBag, args);
  }

  /**
   * Calls async prompt method and optionally runs promise chained task on
   * result data. Converts result data to nsIPropertyBag.
   *
   * @param {object} args - Prompt arguments.
   * @param {Function} [task] - Function which is called with the modified
   *  prompt args object once the prompt has been closed. Must return a
   *  result object for the prompt caller.
   * @returns {Promise<nsIPropertyBag>} - Resolves with a property bag holding the
   * prompt result properties. Resolves once prompt has been closed.
   */
  async openPromptAsync(args, task) {
    let result = await this.openPrompt(args);
    // If task is not defined, the prompt method does not return
    // anything. In this case we can resolve without value.
    if (!task) {
      return undefined;
    }
    // Convert task result to nsIPropertyBag and resolve
    let taskResult = task(result);
    if (!(taskResult instanceof Object)) {
      throw new Error("task must return object");
    }
    return PromptUtils.objectToPropBag(taskResult);
  }

  /*
   * ---------- interface disambiguation ----------
   *
   * nsIPrompt and nsIAuthPrompt share 3 method names with slightly
   * different arguments. All but prompt() have the same number of
   * arguments, so look at the arg types to figure out how we're being
   * called. :-(
   */
  prompt() {
    // also, the nsIPrompt flavor has 5 args instead of 6.
    if (typeof arguments[2] == "object") {
      return this.nsIPrompt_prompt.apply(this, arguments);
    }
    return this.nsIAuthPrompt_prompt.apply(this, arguments);
  }

  promptUsernameAndPassword() {
    // Both have 6 args, so use types.
    if (typeof arguments[2] == "object") {
      // Add the null channel:
      let args = Array.from(arguments);
      args.unshift(null);
      return this.nsIPrompt_promptUsernameAndPassword.apply(this, args);
    }
    return this.nsIAuthPrompt_promptUsernameAndPassword.apply(this, arguments);
  }

  promptPassword() {
    // Both have 5 args, so use types.
    if (typeof arguments[2] == "object") {
      // Add the null channel:
      let args = Array.from(arguments);
      args.unshift(null);
      return this.nsIPrompt_promptPassword.apply(this, args);
    }
    return this.nsIAuthPrompt_promptPassword.apply(this, arguments);
  }

  /* ----------  nsIPrompt  ---------- */

  alert(title, text) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Alert");
    }

    let args = {
      promptType: "alert",
      title,
      text,
    };

    if (this.async) {
      return this.openPromptAsync(args);
    }

    return this.openPromptSync(args);
  }

  alertCheck(title, text, checkLabel, checkValue) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Alert");
    }

    // For sync calls checkValue is an XPCOM inout. XPCOM wraps primitves in
    // objects for call by reference.
    // The async version of this method uses call by value.
    let checked = this.async ? checkValue : checkValue.value;

    let args = {
      promptType: "alertCheck",
      title,
      text,
      checkLabel,
      checked,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        checked: result.checked,
      }));
    }

    this.openPromptSync(args);
    checkValue.value = args.checked;
    return undefined;
  }

  confirm(title, text) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Confirm");
    }

    let args = {
      promptType: "confirm",
      title,
      text,
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({ ok: result.ok }));
    }

    this.openPromptSync(args);
    return args.ok;
  }

  confirmCheck(title, text, checkLabel, checkValue) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("ConfirmCheck");
    }

    let checked = this.async ? checkValue : checkValue.value;

    let args = {
      promptType: "confirmCheck",
      title,
      text,
      checkLabel,
      checked,
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        // Checkbox state always returned, even if cancel clicked.
        checked: result.checked,
        // Did user click Ok or Cancel?
        ok: result.ok,
      }));
    }

    this.openPromptSync(args);
    checkValue.value = args.checked;
    return args.ok;
  }

  confirmEx(
    title,
    text,
    flags,
    button0,
    button1,
    button2,
    checkLabel,
    checkValue,
    extraArgs = {}
  ) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Confirm");
    }

    let args = {
      promptType: "confirmEx",
      title,
      text,
      checkLabel,
      checked: this.async ? checkValue : checkValue.value,
      ok: false,
      buttonNumClicked: 1,
      ...extraArgs,
    };

    let [
      buttonLabels,
      buttonL10nIds,
      defaultButtonNum,
      isDelayEnabled,
      allowNoButtons,
    ] = InternalPromptUtils.confirmExHelper(flags, button0, button1, button2);

    args.defaultButtonNum = defaultButtonNum;
    args.enableDelay = isDelayEnabled;
    args.allowNoButtons = allowNoButtons;

    for (let i = 0; i < 3; i++) {
      if (buttonL10nIds[i]) {
        args[`button${i}L10nId`] = buttonL10nIds[i];
      } else if (buttonLabels[i]) {
        args[`button${i}Label`] = buttonLabels[i];
      } else {
        break;
      }
    }

    if (flags & Ci.nsIPrompt.BUTTON_POS_1_IS_SECONDARY) {
      args.isExtra1Secondary = true;
    }

    if (flags & Ci.nsIPrompt.SHOW_SPINNER) {
      args.headerIconCSSValue = "url('chrome://global/skin/icons/loading.svg')";
    }

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        checked: !!result.checked,
        buttonNumClicked: result.buttonNumClicked,
        isExtra1Secondary: result.isExtra1Secondary,
      }));
    }

    this.openPromptSync(args);
    checkValue.value = args.checked;
    return args.buttonNumClicked;
  }

  nsIPrompt_prompt(title, text, value, checkLabel, checkValue) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Prompt");
    }

    let args = {
      promptType: "prompt",
      title,
      text,
      value: this.async ? value : value.value,
      checkLabel,
      checked: this.async ? checkValue : checkValue.value,
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        checked: !!result.checked,
        value: result.value,
        ok: result.ok,
      }));
    }

    this.openPromptSync(args);

    // Did user click Ok or Cancel?
    let ok = args.ok;
    if (ok) {
      checkValue.value = args.checked;
      value.value = args.value;
    }

    return ok;
  }

  nsIPrompt_promptUsernameAndPassword(channel, title, text, user, pass) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString(
        "PromptUsernameAndPassword3",
        [InternalPromptUtils.getBrandFullName()]
      );
    }

    let args = {
      channel,
      promptType: "promptUserAndPass",
      title,
      text,
      user: this.async ? user : user.value,
      pass: this.async ? pass : pass.value,
      button0Label: InternalPromptUtils.getLocalizedString("SignIn"),
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        user: result.user,
        pass: result.pass,
        ok: result.ok,
      }));
    }

    this.openPromptSync(args);

    // Did user click Ok or Cancel?
    let ok = args.ok;
    if (ok) {
      user.value = args.user;
      pass.value = args.pass;
    }

    return ok;
  }

  nsIPrompt_promptPassword(channel, title, text, pass) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("PromptPassword3", [
        InternalPromptUtils.getBrandFullName(),
      ]);
    }

    let args = {
      channel,
      promptType: "promptPassword",
      title,
      text,
      pass: this.async ? pass : pass.value,
      button0Label: InternalPromptUtils.getLocalizedString("SignIn"),
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        pass: result.pass,
        ok: result.ok,
      }));
    }

    this.openPromptSync(args);

    // Did user click Ok or Cancel?
    let ok = args.ok;
    if (ok) {
      pass.value = args.pass;
    }

    return ok;
  }

  select(title, text, list, selected) {
    if (!title) {
      title = InternalPromptUtils.getLocalizedString("Select");
    }

    let args = {
      promptType: "select",
      title,
      text,
      list,
      selected: -1,
      ok: false,
    };

    if (this.async) {
      return this.openPromptAsync(args, result => ({
        selected: result.selected,
        ok: result.ok,
      }));
    }

    this.openPromptSync(args);

    // Did user click Ok or Cancel?
    let ok = args.ok;
    if (ok) {
      selected.value = args.selected;
    }

    return ok;
  }

  /* ----------  nsIAuthPrompt  ---------- */

  nsIAuthPrompt_prompt(
    title,
    text,
    passwordRealm,
    savePassword,
    defaultText,
    result
  ) {
    // The passwordRealm and savePassword args were ignored by nsPrompt.cpp
    if (defaultText) {
      result.value = defaultText;
    }
    return this.nsIPrompt_prompt(title, text, result, null, {});
  }

  nsIAuthPrompt_promptUsernameAndPassword(
    title,
    text,
    passwordRealm,
    savePassword,
    user,
    pass
  ) {
    // The passwordRealm and savePassword args were ignored by nsPrompt.cpp
    return this.nsIPrompt_promptUsernameAndPassword(
      null,
      title,
      text,
      user,
      pass
    );
  }

  nsIAuthPrompt_promptPassword(title, text, passwordRealm, savePassword, pass) {
    // The passwordRealm and savePassword args were ignored by nsPrompt.cpp,
    // and we don't have a channel here.
    return this.nsIPrompt_promptPassword(null, title, text, pass);
  }

  /* ----------  nsIAuthPrompt2  ---------- */

  promptAuth(channel, level, authInfo) {
    let message = InternalPromptUtils.makeAuthMessage(this, channel, authInfo);

    let [username, password] = InternalPromptUtils.getAuthInfo(authInfo);

    let userParam = this.async ? username : { value: username };
    let passParam = this.async ? password : { value: password };

    let result;
    if (authInfo.flags & Ci.nsIAuthInformation.ONLY_PASSWORD) {
      result = this.nsIPrompt_promptPassword(channel, null, message, passParam);
    } else {
      result = this.nsIPrompt_promptUsernameAndPassword(
        channel,
        null,
        message,
        userParam,
        passParam
      );
    }

    // For the async case result is an nsIPropertyBag with prompt results.
    if (this.async) {
      return result.then(bag => {
        let ok = bag.getProperty("ok");
        if (ok) {
          InternalPromptUtils.setAuthInfo(
            authInfo,
            bag.getProperty("user"),
            bag.getProperty("pass")
          );
        }
        return ok;
      });
    }

    // For the sync case result is the "ok" boolean which indicates whether
    // the user has confirmed the dialog.
    if (result) {
      InternalPromptUtils.setAuthInfo(
        authInfo,
        userParam.value,
        passParam.value
      );
    }
    return result;
  }

  asyncPromptAuth() {
    // Nothing calls this directly; netwerk ends up going through
    // nsIPromptService::GetPrompt, which delegates to login manager.
    // Login manger handles the async bits itself, and only calls out
    // promptAuth, never asyncPromptAuth.
    //
    // Bug 565582 will change this.
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  /* ----------  nsIWritablePropertyBag2 ---------- */
  // Legacy way to set modal type when prompting via nsIPrompt.
  // Please prompt via nsIPromptService. This will be removed in the future.
  setPropertyAsUint32(name, value) {
    if (name == "modalType") {
      this.modalType = value;
    } else {
      throw Components.Exception("", Cr.NS_ERROR_ILLEGAL_VALUE);
    }
  }
}

XPCOMUtils.defineLazyPreferenceGetter(
  ModalPrompter,
  "defaultModalType",
  "prompts.defaultModalType",
  MODAL_TYPE_WINDOW
);

export function AuthPromptAdapterFactory() {}
AuthPromptAdapterFactory.prototype = {
  classID: Components.ID("{6e134924-6c3a-4d86-81ac-69432dd971dc}"),
  QueryInterface: ChromeUtils.generateQI(["nsIAuthPromptAdapterFactory"]),

  /* ----------  nsIAuthPromptAdapterFactory ---------- */

  createAdapter(oldPrompter) {
    return new AuthPromptAdapter(oldPrompter);
  },
};

// Takes an nsIAuthPrompt implementation, wraps it with a nsIAuthPrompt2 shell.
function AuthPromptAdapter(oldPrompter) {
  this.oldPrompter = oldPrompter;
}
AuthPromptAdapter.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIAuthPrompt2"]),
  oldPrompter: null,

  /* ----------  nsIAuthPrompt2 ---------- */

  promptAuth(channel, level, authInfo) {
    let message = InternalPromptUtils.makeAuthMessage(
      this.oldPrompter,
      channel,
      authInfo
    );

    let [username, password] = InternalPromptUtils.getAuthInfo(authInfo);
    let userParam = { value: username };
    let passParam = { value: password };

    let { displayHost, realm } = PromptUtils.getAuthTarget(channel, authInfo);
    let authTarget = displayHost + " (" + realm + ")";

    let ok;
    if (authInfo.flags & Ci.nsIAuthInformation.ONLY_PASSWORD) {
      ok = this.oldPrompter.promptPassword(
        null,
        message,
        authTarget,
        Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY,
        passParam
      );
    } else {
      ok = this.oldPrompter.promptUsernameAndPassword(
        null,
        message,
        authTarget,
        Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY,
        userParam,
        passParam
      );
    }

    if (ok) {
      InternalPromptUtils.setAuthInfo(
        authInfo,
        userParam.value,
        passParam.value
      );
    }
    return ok;
  },

  asyncPromptAuth() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
};
