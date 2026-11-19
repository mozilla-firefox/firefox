/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  EnableDelayHelper: "resource://gre/modules/PromptUtils.sys.mjs",
});

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

// Maximum number of buttons in a confirmEx dialog (button0 to button3).
const MAX_BUTTONS = 4;

export class CommonDialog {
  constructor(args, ui) {
    this.args = args;
    this.ui = ui;
    this.initialFocusPromise = new Promise(resolve => {
      this.initialFocusResolver = resolve;
    });
  }

  static DEFAULT_APP_ICON_CSS = `image-set(url("chrome://branding/content/icon16.png") 1x,
    url("chrome://branding/content/icon32.png") 2x,
    url("chrome://branding/content/icon64.png") 4x)`;

  args = null;
  ui = null;

  hasInputField = true;
  numButtons = undefined;
  iconClass = undefined;
  soundID = undefined;
  focusTimer = null;
  initialFocusPromise = null;
  initialFocusResolver = null;

  /**
   * @param [commonDialogEl] - Dialog element from commonDialog.xhtml.
   */
  async onLoad(commonDialogEl) {
    let isEmbedded =
      !!commonDialogEl.documentGlobal.docShell.chromeEventHandler;

    switch (this.args.promptType) {
      case "alert":
      case "alertCheck":
        this.hasInputField = false;
        this.numButtons = 1;
        this.iconClass = ["alert-icon"];
        this.soundID = Ci.nsISound.EVENT_ALERT_DIALOG_OPEN;
        break;
      case "confirmCheck":
      case "confirm":
        this.hasInputField = false;
        this.numButtons = 2;
        this.iconClass = ["question-icon"];
        this.soundID = Ci.nsISound.EVENT_CONFIRM_DIALOG_OPEN;
        break;
      case "confirmEx":
        var numButtons = 0;
        for (let i = 0; i < MAX_BUTTONS; i++) {
          if (this.hasButtonLabel(i)) {
            numButtons++;
          }
        }
        if (numButtons == 0 && !this.args.allowNoButtons) {
          throw new Error(
            "A dialog with no buttons requires the allowNoButtons argument"
          );
        }
        this.numButtons = numButtons;
        this.hasInputField = false;
        this.iconClass = ["question-icon"];
        this.soundID = Ci.nsISound.EVENT_CONFIRM_DIALOG_OPEN;
        break;
      case "prompt":
        this.numButtons = 2;
        this.iconClass = ["question-icon"];
        this.soundID = Ci.nsISound.EVENT_PROMPT_DIALOG_OPEN;
        this.initTextbox("login", this.args.value);
        // Clear the label, since this isn't really a username prompt.
        this.ui.loginLabel.setAttribute("value", "");
        // Ensure the labeling for the prompt is correct.
        this.ui.loginTextbox.setAttribute("aria-labelledby", "infoBody");
        break;
      case "promptUserAndPass":
        this.numButtons = 2;
        this.iconClass = ["authentication-icon", "question-icon"];
        this.soundID = Ci.nsISound.EVENT_PROMPT_DIALOG_OPEN;
        this.initTextbox("login", this.args.user);
        this.initTextbox("password1", this.args.pass);
        break;
      case "promptPassword":
        this.numButtons = 2;
        this.iconClass = ["authentication-icon", "question-icon"];
        this.soundID = Ci.nsISound.EVENT_PROMPT_DIALOG_OPEN;
        this.initTextbox("password1", this.args.pass);
        // Clear the label, since the message presumably indicates its purpose.
        this.ui.password1Label.setAttribute("value", "");
        break;
      default:
        console.error(
          "commonDialog opened for unknown type: ",
          this.args.promptType
        );
        throw new Error("unknown dialog type");
    }

    commonDialogEl.setAttribute("windowtype", "prompt:" + this.args.promptType);

    // set the document title
    let title = this.args.title;
    let infoTitle = this.ui.infoTitle;
    infoTitle.appendChild(infoTitle.ownerDocument.createTextNode(title));

    // After making these preventative checks, we can determine to show it if we're on
    // macOS (where there is no titlebar) or if the prompt is a common dialog document
    // and has been embedded (has a chromeEventHandler).
    infoTitle.hidden = !(AppConstants.platform === "macosx" || isEmbedded);

    commonDialogEl.ownerDocument.title = title;

    // Set button labels and visibility
    //
    // This assumes that button0 defaults to a visible "ok" button, and
    // button1 defaults to a visible "cancel" button. The other 2 buttons
    // have no default labels (and are hidden).
    let localizedButtons = [];
    switch (this.numButtons) {
      case 4:
        this.setButtonLabel(this.ui.button3, 3, localizedButtons);
        this.ui.button3.hidden = false;
      // fall through
      case 3:
        this.setButtonLabel(this.ui.button2, 2, localizedButtons);
        this.ui.button2.hidden = false;
      // fall through
      case 2:
        // Defaults to a visible "cancel" button
        this.setButtonLabel(this.ui.button1, 1, localizedButtons);
        break;
      case 0:
        this.ui.button0.hidden = true;
      // fall through
      case 1:
        this.ui.button1.hidden = true;
        break;
    }
    // Defaults to a visible "ok" button
    this.setButtonLabel(this.ui.button0, 0, localizedButtons);
    let buttonsTranslated = localizedButtons.length
      ? commonDialogEl.ownerDocument.l10n
          .translateElements(localizedButtons)
          .then(() => {
            if (!isEmbedded) {
              commonDialogEl.documentGlobal.sizeToContent();
            }
          })
      : null;

    // display the main text
    let croppedMessage = "";
    if (this.args.text) {
      // Bug 317334 - crop string length as a workaround.
      croppedMessage = this.args.text.substr(0, 10000);
      // TabModalPrompts don't have an infoRow to hide / not hide here, so
      // guard on that here so long as they are in use.
      if (this.ui.infoRow) {
        this.ui.infoRow.hidden = false;
      }
    }
    let infoBody = this.ui.infoBody;
    infoBody.appendChild(infoBody.ownerDocument.createTextNode(croppedMessage));

    let label = this.args.checkLabel;
    if (label) {
      // Only show the checkbox if label has a value.
      this.ui.checkboxContainer.hidden = false;
      this.ui.checkboxContainer.clientTop; // style flush to assure binding is attached
      this.setLabelForNode(this.ui.checkbox, label);
      this.ui.checkbox.checked = this.args.checked;
    }

    // set the icon
    let icon = this.ui.infoIcon;
    if (icon) {
      this.iconClass.forEach(el => icon.classList.add(el));
    }

    // set default result to cancelled
    this.args.ok = false;
    this.args.buttonNumClicked = 1;

    // Set the default button
    let b = this.args.defaultButtonNum || 0;
    commonDialogEl.defaultButton = ["accept", "cancel", "extra1", "extra2"][b];

    if (!isEmbedded && !this.ui.promptContainer?.hidden) {
      // Set default focus and select textbox contents if applicable. If we're
      // embedded SubDialogManager will call setDefaultFocus for us.
      this.setDefaultFocus(true);
    }

    if (this.args.enableDelay) {
      this.delayHelper = new lazy.EnableDelayHelper({
        disableDialog: () => this.setButtonsEnabledState(false),
        enableDialog: () => this.setButtonsEnabledState(true),
        focusTarget: this.ui.focusTarget,
      });
    }

    // Play a sound (unless we're showing a content prompt -- don't want those
    //               to feel like OS prompts).
    try {
      if (this.soundID && !this.args.openedWithTabDialog) {
        Cc["@mozilla.org/sound;1"]
          .getService(Ci.nsISound)
          .playEventSound(this.soundID);
      }
    } catch (e) {
      console.error("Couldn't play common dialog event sound: ", e);
    }

    if (isEmbedded) {
      // If we delayed default focus above, wait for it to be ready before
      // sending the notification.
      await this.initialFocusPromise;
    }
    await buttonsTranslated;
    Services.obs.notifyObservers(this.ui.prompt, "common-dialog-loaded");
  }

  hasButtonLabel(aIndex) {
    return !!(
      this.args[`button${aIndex}L10nId`] || this.args[`button${aIndex}Label`]
    );
  }

  /**
   * Sets a button label from either a Fluent ID or a caller-supplied label
   * with an embedded access key (&foo). Buttons localized with Fluent are
   * added to aLocalizedButtons.
   */
  setButtonLabel(aNode, aIndex, aLocalizedButtons) {
    let l10nId = this.args[`button${aIndex}L10nId`];
    if (l10nId) {
      aNode.ownerDocument.l10n.setAttributes(aNode, l10nId);
      aLocalizedButtons.push(aNode);
      return;
    }
    let label = this.args[`button${aIndex}Label`];
    if (label) {
      aNode.removeAttribute("data-l10n-id");
      this.setLabelForNode(aNode, label);
    }
  }

  setLabelForNode(aNode, aLabel) {
    // This is for labels which may contain embedded access keys.
    // If we end in (&X) where X represents the access key, optionally preceded
    // by spaces and/or followed by the ':' character, store the access key and
    // remove the access key placeholder + leading spaces from the label.
    // Otherwise a character preceded by one but not two &s is the access key.
    // Store it and remove the &.

    // Note that if you change the following code, see the comment of
    // nsTextBoxFrame::UpdateAccessTitle.
    var accessKey = null;
    if (/ *\(\&([^&])\)(:?)$/.test(aLabel)) {
      aLabel = RegExp.leftContext + RegExp.$2;
      accessKey = RegExp.$1;
    } else if (/^([^&]*)\&(([^&]).*$)/.test(aLabel)) {
      aLabel = RegExp.$1 + RegExp.$2;
      accessKey = RegExp.$3;
    }

    // && is the magic sequence to embed an & in your label.
    aLabel = aLabel.replace(/\&\&/g, "&");
    aNode.label = aLabel;

    // XXXjag bug 325251
    // Need to set this after aNode.setAttribute("value", aLabel);
    if (accessKey) {
      aNode.accessKey = accessKey;
    }
  }

  initTextbox(aName, aValue) {
    this.ui[aName + "Container"].hidden = false;
    this.ui[aName + "Textbox"].setAttribute(
      "value",
      aValue !== null ? aValue : ""
    );
  }

  setButtonsEnabledState(enabled) {
    this.ui.button0.disabled = !enabled;
    // button1 (cancel) remains enabled.
    this.ui.button2.disabled = !enabled;
    this.ui.button3.disabled = !enabled;
  }

  setDefaultFocus(isInitialLoad) {
    let b = this.args.defaultButtonNum || 0;
    let button = this.ui["button" + b];

    if (!this.hasInputField) {
      let isOSX = "nsILocalFileMac" in Ci;
      // If the infoRow exists and is is hidden, then the infoBody is also hidden,
      // which means it can't be focused. At that point, we fall back to focusing
      // the default button, regardless of platform.
      if (isOSX && !(this.ui.infoRow && this.ui.infoRow.hidden)) {
        this.ui.infoBody.focus();
      } else {
        button.focus({ focusVisible: false });
      }
    } else if (this.args.promptType == "promptPassword") {
      // When the prompt is initialized, focus and select the textbox
      // contents. Afterwards, only focus the textbox.
      if (isInitialLoad) {
        this.ui.password1Textbox.select();
      } else {
        this.ui.password1Textbox.focus();
      }
    } else if (isInitialLoad) {
      this.ui.loginTextbox.select();
    } else {
      this.ui.loginTextbox.focus();
    }

    if (isInitialLoad) {
      this.initialFocusResolver();
    }
  }

  onCheckbox() {
    this.args.checked = this.ui.checkbox.checked;
  }

  onButton0() {
    this.args.promptActive = false;
    this.args.ok = true;
    this.args.buttonNumClicked = 0;

    let username = this.ui.loginTextbox.value;
    let password = this.ui.password1Textbox.value;

    // Return textfield values
    switch (this.args.promptType) {
      case "prompt":
        this.args.value = username;
        break;
      case "promptUserAndPass":
        this.args.user = username;
        this.args.pass = password;
        break;
      case "promptPassword":
        this.args.pass = password;
        break;
    }
  }

  onButton1() {
    this.args.promptActive = false;
    this.args.buttonNumClicked = 1;
  }

  onButton2() {
    this.args.promptActive = false;
    this.args.buttonNumClicked = 2;
  }

  onButton3() {
    this.args.promptActive = false;
    this.args.buttonNumClicked = 3;
  }

  abortPrompt() {
    this.args.promptActive = false;
    this.args.promptAborted = true;
  }
}
