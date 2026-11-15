/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { CommonDialog } = ChromeUtils.importESModule(
  "resource://gre/modules/CommonDialog.sys.mjs"
);
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentAnalysisUtils:
    "moz-src:///toolkit/components/contentanalysis/ContentAnalysisUtils.sys.mjs",
});

// imported by adjustableTitle.js loaded in the same context:
/* globals PromptUtils, goDoCommand, goUpdateCommand */

var propBag, args, Dialog;

function commonDialogOnLoad() {
  propBag = window.arguments[0]
    .QueryInterface(Ci.nsIWritablePropertyBag2)
    .QueryInterface(Ci.nsIWritablePropertyBag);
  // Convert to a JS object
  args = {};
  for (let prop of propBag.enumerator) {
    args[prop.name] = prop.value;
  }

  let dialog = document.getElementById("commonDialog");

  let needIconifiedHeader =
    args.modalType == Ci.nsIPrompt.MODAL_TYPE_CONTENT ||
    ["promptUserAndPass", "promptPassword"].includes(args.promptType) ||
    args.headerIconCSSValue;
  let root = document.documentElement;
  if (needIconifiedHeader) {
    root.setAttribute("neediconheader", "true");
  }
  let title = { raw: args.title };
  let { useTitle, promptPrincipal } = args;
  if (!useTitle) {
    if (promptPrincipal) {
      if (promptPrincipal.isNullPrincipal) {
        title = { l10nId: "common-dialog-title-null" };
      } else if (promptPrincipal.isSystemPrincipal) {
        title = { l10nId: "common-dialog-title-system" };
        root.style.setProperty("--icon-url", CommonDialog.DEFAULT_APP_ICON_CSS);
      } else if (promptPrincipal.addonPolicy) {
        title.raw = promptPrincipal.addonPolicy.name;
      } else if (promptPrincipal.isContentPrincipal) {
        try {
          title.raw = promptPrincipal.URI.displayHostPort;
        } catch (ex) {
          // hostPort getter can throw, e.g. for about URIs.
          title.raw = promptPrincipal.originNoSuffix;
        }
        // hostPort can be empty for file URIs.
        if (!title.raw) {
          title.raw = promptPrincipal.prePath;
        }
      } else {
        title = { l10nId: "common-dialog-title-unknown" };
      }
    } else if (args.authOrigin) {
      title = { raw: args.authOrigin };
    }
  }
  if (args.headerIconCSSValue) {
    root.style.setProperty("--icon-url", args.headerIconCSSValue);
  }
  // Fade and crop potentially long raw titles, e.g., origins and hostnames.
  title.shouldUseMaskFade =
    !useTitle && title.raw && (args.authOrigin || promptPrincipal);
  root.setAttribute("headertitle", JSON.stringify(title));
  if (args.isInsecureAuth) {
    dialog.setAttribute("insecureauth", "true");
  }

  let ui = {
    prompt: window,
    loginContainer: document.getElementById("loginContainer"),
    loginTextbox: document.getElementById("loginTextbox"),
    loginLabel: document.getElementById("loginLabel"),
    password1Container: document.getElementById("password1Container"),
    password1Textbox: document.getElementById("password1Textbox"),
    password1Label: document.getElementById("password1Label"),
    infoRow: document.getElementById("infoRow"),
    infoBody: document.getElementById("infoBody"),
    infoTitle: document.getElementById("infoTitle"),
    infoIcon: document.getElementById("infoIcon"),
    checkbox: document.getElementById("checkbox"),
    checkboxContainer: document.getElementById("checkboxContainer"),
    button3: dialog.getButton("extra2"),
    button2: dialog.getButton("extra1"),
    button1: dialog.getButton("cancel"),
    button0: dialog.getButton("accept"),
    focusTarget: window,
  };

  if (args.isExtra1Secondary) {
    dialog.setAttribute("extra1-is-secondary", true);
  }

  Dialog = new CommonDialog(args, ui);
  window.addEventListener("dialogclosing", function (aEvent) {
    if (aEvent.detail?.abort) {
      Dialog.abortPrompt();
    }
  });
  document.addEventListener("dialogaccept", function () {
    Dialog.onButton0();
  });
  document.addEventListener("dialogcancel", function () {
    Dialog.onButton1();
  });
  document.addEventListener("dialogextra1", function () {
    Dialog.onButton2();
    window.close();
  });
  document.addEventListener("dialogextra2", function () {
    Dialog.onButton3();
    window.close();
  });
  document.subDialogSetDefaultFocus = isInitialFocus =>
    Dialog.setDefaultFocus(isInitialFocus);
  Dialog.onLoad(dialog);

  document.addEventListener("command", event => {
    switch (event.target.id) {
      case "cmd_copy":
      case "cmd_selectAll":
        goDoCommand(event.target.id);
        break;
      case "checkbox":
        Dialog.onCheckbox();
        break;
    }
  });

  document
    .getElementById("contentAreaContextMenu")
    .addEventListener("popupshowing", () => goUpdateCommand("cmd_copy"));

  // resize the window to the content
  window.sizeToContent();

  // If the icon hasn't loaded yet, size the window to the content again when
  // it does, as its layout can change.
  ui.infoIcon.addEventListener("load", () => window.sizeToContent());
  if (args.owningBrowsingContext?.isContent) {
    lazy.ContentAnalysisUtils.setupContentAnalysisEventsForTextElement(
      ui.loginTextbox,
      args.owningBrowsingContext
    );
    // The message text comes from the page and is selectable, so copying out of
    // alert()/confirm()/prompt() needs to be analyzed as well.
    // This is registered on the root rather than on #infoBody because the
    // copy event is dispatched at the element holding the
    // start of the selection, so a selection spanning #infoTitle into
    // #infoBody would otherwise slip past unanalyzed.
    lazy.ContentAnalysisUtils.setupContentAnalysisEventsForCopyFromElement(
      document.documentElement,
      args.owningBrowsingContext
    );
  }

  window.getAttention();
}

function commonDialogOnUnload() {
  // Convert args back into property bag
  for (let propName in args) {
    propBag.setProperty(propName, args[propName]);
  }
}

document.addEventListener("DOMContentLoaded", commonDialogOnLoad);
window.addEventListener("unload", commonDialogOnUnload);
