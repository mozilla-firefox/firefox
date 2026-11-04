/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function closeWindow(aClose, aPromptFunction, aSource) {
  let { AppConstants } = ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs"
  );

  // Closing the last window doesn't quit the application on OS X.
  if (AppConstants.platform != "macosx") {
    var windowCount = 0;
    for (let w of Services.wm.getEnumerator(null)) {
      if (w.closed) {
        continue;
      }
      if (++windowCount == 2) {
        break;
      }
    }

    // If we're down to the last window and someone tries to shut down, check to make sure we can!
    if (windowCount == 1 && !canQuitApplication("lastwindow", aSource)) {
      return false;
    }
    if (
      windowCount != 1 &&
      typeof aPromptFunction == "function" &&
      !aPromptFunction()
    ) {
      return false;
    }
  } else if (typeof aPromptFunction == "function" && !aPromptFunction()) {
    return false;
  }

  if (aClose) {
    window.close();
    return window.closed;
  }

  return true;
}

function canQuitApplication(aData, aSource) {
  const kCID = "@mozilla.org/browser/browserglue;1";
  if (kCID in Cc && !(aData || "").includes("restart")) {
    let BrowserGlue = Cc[kCID].getService(Ci.nsISupports).wrappedJSObject;
    BrowserGlue._registerQuitSource(aSource);
  }
  try {
    var cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      aData || null
    );

    // Something aborted the quit process.
    if (cancelQuit.data) {
      return false;
    }
  } catch (ex) {}
  return true;
}

function goQuitApplication(event) {
  // We can't know for sure if the user used a shortcut to trigger quit.
  // Proxy by means of checking for the shortcut modifier.
  let isMac = navigator.platform.startsWith("Mac");
  let key = isMac ? "metaKey" : "ctrlKey";
  let source = "OS";
  if (event[key]) {
    source = "shortcut";
    // Note that macOS likes pretending something came from this menu even if
    // activated by keyboard shortcut, hence checking that first.
  } else if (event.sourceEvent?.target?.id?.startsWith("menu_")) {
    source = "menuitem";
  } else if (event.sourceEvent?.target?.id?.startsWith("appMenu")) {
    source = "appmenu";
  }
  if (!canQuitApplication(undefined, source)) {
    return false;
  }

  Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);
  return true;
}

//
// Command Updater functions
//
function goUpdateCommand(aCommand) {
  try {
    var controller =
      top.document.commandDispatcher.getControllerForCommand(aCommand);

    var enabled = false;
    if (controller) {
      enabled = controller.isCommandEnabled(aCommand);
    }

    goSetCommandEnabled(aCommand, enabled);
  } catch (e) {
    console.error("An error occurred updating the ", aCommand, " command: ", e);
  }
}

function goDoCommand(aCommand) {
  try {
    var controller =
      top.document.commandDispatcher.getControllerForCommand(aCommand);
    if (controller && controller.isCommandEnabled(aCommand)) {
      controller.doCommand(aCommand);
    }
  } catch (e) {
    console.error(
      "An error occurred executing the ",
      aCommand,
      " command: ",
      e
    );
  }
}

function goSetCommandEnabled(aID, aEnabled) {
  var node = document.getElementById(aID);

  if (node) {
    if (aEnabled) {
      node.removeAttribute("disabled");
    } else {
      node.setAttribute("disabled", "true");
    }
  }
}
