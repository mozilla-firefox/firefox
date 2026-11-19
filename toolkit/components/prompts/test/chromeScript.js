const { clearInterval, setInterval, setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const { BrowserTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/BrowserTestUtils.sys.mjs"
);

// Define these to make EventUtils happy.
let window = this;
let parent = {};

let EventUtils = {};
Services.scriptloader.loadSubScript(
  "chrome://mochikit/content/tests/SimpleTest/EventUtils.js",
  EventUtils
);

addMessageListener("handlePrompt", msg => {
  handlePromptWhenItAppears(msg.action, msg.modalType, msg.isSelect);
});

async function handlePromptWhenItAppears(action, modalType, isSelect) {
  if (!(await handlePrompt(action, modalType, isSelect))) {
    setTimeout(
      () => this.handlePromptWhenItAppears(action, modalType, isSelect),
      100
    );
  }
}

async function handlePrompt(action, modalType, isSelect) {
  let ui;
  let browserWin = Services.wm.getMostRecentWindow("navigator:browser");

  let doc = getDialogDoc();
  if (!doc) {
    return false; // try again in a bit
  }

  if (isSelect) {
    ui = doc;
  } else {
    ui = doc.defaultView.Dialog.ui;
  }

  let dialogClosed = BrowserTestUtils.waitForEvent(
    browserWin,
    "DOMModalDialogClosed"
  );

  let promptState;
  if (isSelect) {
    promptState = getSelectState(ui);
    dismissSelect(ui, action);
  } else {
    promptState = getPromptState(ui);
    dismissPrompt(ui, action);
  }

  // Wait until the prompt has been closed before sending callback msg.
  // Unless the test explicitly doesn't request a button click.
  if (action.buttonClick !== "none") {
    await dialogClosed;
  }

  sendAsyncMessage("promptHandled", { promptState });
  return true;
}

function getSelectState(ui) {
  let listbox = ui.getElementById("list");

  let state = {};
  state.msg = ui.getElementById("info.txt").firstChild.textContent;
  state.selectedIndex = listbox.selectedIndex;
  state.items = [];

  for (let i = 0; i < listbox.itemCount; i++) {
    let item = listbox.getItemAtIndex(i).label;
    state.items.push(item);
  }

  return state;
}

function getPromptState(ui) {
  let state = {};
  state.msg = ui.infoBody.textContent;
  state.infoRowHidden = ui.infoRow?.hidden || false;
  state.titleHidden = ui.infoTitle.hidden;
  state.textHidden = ui.loginContainer.hidden;
  state.passHidden = ui.password1Container.hidden;
  state.checkHidden = ui.checkboxContainer.hidden;
  state.checkMsg = state.checkHidden ? "" : ui.checkbox.label;
  state.checked = state.checkHidden ? false : ui.checkbox.checked;
  state.iconClass = ui.infoIcon.className;
  state.textValue = ui.loginTextbox.value;
  state.passValue = ui.password1Textbox.value;

  state.butt0Label = ui.button0.label;
  state.butt1Label = ui.button1.label;
  state.butt2Label = ui.button2.label;
  state.butt0AccessKey = ui.button0.getAttribute("accesskey") || "";
  state.butt1AccessKey = ui.button1.getAttribute("accesskey") || "";
  state.butt2AccessKey = ui.button2.getAttribute("accesskey") || "";

  state.butt0Disabled = ui.button0.disabled;
  state.butt1Disabled = ui.button1.disabled;
  state.butt2Disabled = ui.button2.disabled;

  function isDefaultButton(b) {
    return b.hasAttribute("default") && b.getAttribute("default") == "true";
  }
  state.defButton0 = isDefaultButton(ui.button0);
  state.defButton1 = isDefaultButton(ui.button1);
  state.defButton2 = isDefaultButton(ui.button2);

  let e = Services.focus.focusedElement;

  if (e == null) {
    state.focused = null;
  } else if (ui.button0.isSameNode(e)) {
    state.focused = "button0";
  } else if (ui.button1.isSameNode(e)) {
    state.focused = "button1";
  } else if (ui.button2.isSameNode(e)) {
    state.focused = "button2";
  } else if (e.isSameNode(ui.loginTextbox)) {
    state.focused = "textField";
  } else if (e.isSameNode(ui.password1Textbox)) {
    state.focused = "passField";
  } else if (ui.infoBody.isSameNode(e)) {
    state.focused = "infoBody";
  } else {
    state.focused =
      "ERROR: unexpected element focused: " + (e ? e.localName : "<null>");
  }

  function checkIsExtra1Secondary() {
    const buttonBox = ui.button1.parentNode;
    const button1Index = Array.from(buttonBox.children).indexOf(ui.button1);
    const button2Index = Array.from(buttonBox.children).indexOf(ui.button2);
    return button1Index < button2Index;
  }
  state.isExtra1Secondary = checkIsExtra1Secondary();

  let treeOwner =
    ui.prompt && ui.prompt.docShell && ui.prompt.docShell.treeOwner;
  if (treeOwner && treeOwner.QueryInterface(Ci.nsIInterfaceRequestor)) {
    // Check that the dialog is modal, chrome and dependent;
    // We can't just check window.opener because that'll be
    // a content window, which therefore isn't exposed (it'll lie and
    // be null).
    let flags = treeOwner.getInterface(Ci.nsIAppWindow).chromeFlags;
    state.chrome = (flags & Ci.nsIWebBrowserChrome.CHROME_OPENAS_CHROME) != 0;
    state.dialog = (flags & Ci.nsIWebBrowserChrome.CHROME_OPENAS_DIALOG) != 0;
    state.chromeDependent =
      (flags & Ci.nsIWebBrowserChrome.CHROME_DEPENDENT) != 0;
    let wbc = treeOwner.getInterface(Ci.nsIWebBrowserChrome);
    state.isWindowModal = wbc.isWindowModal();
  }

  // Check the dialog is a common dialog document and has been embedded.
  let isEmbedded = !!ui.prompt?.docShell?.chromeEventHandler;
  let isCommonDialogDoc =
    getDialogDoc()?.location.href.includes("commonDialog.xhtml");
  state.isSubDialogPrompt = isCommonDialogDoc && isEmbedded;
  state.showCallerOrigin = ui.prompt.args.showCallerOrigin;

  return state;
}

function dismissSelect(ui, action) {
  let dialog = ui.getElementsByTagName("dialog")[0];
  let listbox = ui.getElementById("list");

  if (action.selectItem) {
    listbox.selectedIndex = 1;
  }

  if (action.buttonClick == "ok") {
    dialog.acceptDialog();
  } else if (action.buttonClick == "cancel") {
    dialog.cancelDialog();
  }
}

function dismissPrompt(ui, action) {
  if (action.setCheckbox) {
    // Annoyingly, the prompt code is driven by oncommand.
    ui.checkbox.checked = true;
    ui.checkbox.doCommand();
  }

  if ("textField" in action) {
    ui.loginTextbox.setAttribute("value", action.textField);
  }

  if ("passField" in action) {
    ui.password1Textbox.setAttribute("value", action.passField);
  }

  switch (action.buttonClick) {
    case "ok":
    case 0:
      ui.button0.click();
      break;
    case "cancel":
    case 1:
      ui.button1.click();
      break;
    case 2:
      ui.button2.click();
      break;
    case "ESC": {
      // XXX This is assuming tab-modal.
      let browserWin = Services.wm.getMostRecentWindow("navigator:browser");
      EventUtils.synthesizeKey("KEY_Escape", {}, browserWin);
      break;
    }
    case "pollOK": {
      // Buttons are disabled at the moment, poll until they're reenabled.
      // Can't use setInterval here, because the window's in a modal state
      // and thus DOM events are suppressed.
      let interval = setInterval(() => {
        if (ui.button0.disabled) {
          return;
        }
        ui.button0.click();
        clearInterval(interval);
      }, 100);
      break;
    }
    case "abort_dialogs": {
      let abortDialogEvent = new ui.prompt.CustomEvent("dialogclosing", {
        bubbles: true,
        detail: { abort: true },
      });
      ui.prompt.close(abortDialogEvent);
      break;
    }
    case "none":
      break;

    default:
      throw new Error("dismissPrompt action listed unknown button.");
  }
}

function getDialogDoc() {
  // Trudge through all the open windows, until we find the one
  // that has either commonDialog.xhtml or selectDialog.xhtml loaded.
  // var enumerator = Services.wm.getEnumerator("navigator:browser");
  for (let { docShell } of Services.wm.getEnumerator(null)) {
    var containedDocShells = docShell.getAllDocShellsInSubtree(
      docShell.typeChrome,
      docShell.ENUMERATE_FORWARDS
    );
    for (let childDocShell of containedDocShells) {
      // Get the corresponding document for this docshell
      // We don't want it if it's not done loading.
      if (childDocShell.busyFlags != Ci.nsIDocShell.BUSY_FLAGS_NONE) {
        continue;
      }
      var childDoc = childDocShell.docViewer.DOMDocument;

      if (
        childDoc.location.href !=
          "chrome://global/content/commonDialog.xhtml" &&
        childDoc.location.href != "chrome://global/content/selectDialog.xhtml"
      ) {
        continue;
      }

      // We're expecting the dialog to be focused. If it's not yet, try later.
      // (In particular, this is needed on Linux to reliably check focused elements.)
      if (Services.focus.focusedWindow != childDoc.defaultView) {
        continue;
      }

      return childDoc;
    }
  }

  return null;
}
