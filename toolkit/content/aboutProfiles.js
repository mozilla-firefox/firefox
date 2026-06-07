/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { canDeleteProfile, deleteSharedProfilesStore } =
  ChromeUtils.importESModule(
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs"
  );

XPCOMUtils.defineLazyServiceGetter(
  this,
  "ProfileService",
  "@mozilla.org/toolkit/profile-service;1",
  Ci.nsIToolkitProfileService
);

async function flush() {
  try {
    await ProfileService.asyncFlush();
    rebuildProfileList();
  } catch (e) {
    let [title, msg, button] = await document.l10n.formatValues([
      { id: "profiles-flush-fail-title" },
      {
        id:
          e.result == Cr.NS_ERROR_DATABASE_CHANGED
            ? "profiles-flush-conflict"
            : "profiles-flush-failed",
      },
      { id: "profiles-flush-restart-button" },
    ]);

    const PS = Ci.nsIPromptService;
    let result = Services.prompt.confirmEx(
      window,
      title,
      msg,
      PS.BUTTON_POS_0 * PS.BUTTON_TITLE_CANCEL +
        PS.BUTTON_POS_1 * PS.BUTTON_TITLE_IS_STRING,
      null,
      button,
      null,
      null,
      {}
    );
    if (result == 1) {
      restart(false);
    }
  }
}

async function rebuildProfileList() {
  let parent = document.getElementById("profiles");
  while (parent.firstChild) {
    parent.firstChild.remove();
  }

  let defaultProfile;
  try {
    defaultProfile = ProfileService.defaultProfile;
  } catch (e) {}

  let currentProfile = ProfileService.currentProfile;

  for (let profile of ProfileService.profiles) {
    let isCurrentProfile = profile == currentProfile;
    let isInUse = isCurrentProfile;
    let canDelete = await canDeleteProfile(profile);
    if (!isInUse) {
      try {
        let lock = profile.lock({});
        lock.unlock();
      } catch (e) {
        if (
          e.result != Cr.NS_ERROR_FILE_NOT_DIRECTORY &&
          e.result != Cr.NS_ERROR_FILE_NOT_FOUND
        ) {
          isInUse = true;
        }
      }
    }
    display({
      profile,
      isDefault: profile == defaultProfile,
      isCurrentProfile,
      isInUse,
      canDelete,
    });
  }
}

function display(profileData) {
  let parent = document.getElementById("profiles");

  let div = document.createElement("div");
  parent.appendChild(div);

  let name = document.createElement("h2");

  div.appendChild(name);
  document.l10n.setAttributes(name, "profiles-name", {
    name: profileData.profile.name,
  });

  if (profileData.isCurrentProfile) {
    let profileMessage = document.createElement("h3");
    document.l10n.setAttributes(profileMessage, "profiles-current-profile");
    div.appendChild(profileMessage);
  } else if (profileData.isInUse) {
    let profileMessage = document.createElement("h3");
    document.l10n.setAttributes(profileMessage, "profiles-in-use-profile");
    div.appendChild(profileMessage);
  } else if (!profileData.canDelete) {
    let profileMessage = document.createElement("h3");
    document.l10n.setAttributes(
      profileMessage,
      "profiles-cannot-delete-profile"
    );
    div.appendChild(profileMessage);
  }

  let table = document.createElement("table");
  div.appendChild(table);

  let tbody = document.createElement("tbody");
  table.appendChild(tbody);

  function createItem(title, value, dir = false) {
    let tr = document.createElement("tr");
    tbody.appendChild(tr);

    let th = document.createElement("th");
    th.setAttribute("class", "column");
    document.l10n.setAttributes(th, title);
    tr.appendChild(th);

    let td = document.createElement("td");
    tr.appendChild(td);

    if (dir) {
      td.appendChild(document.createTextNode(value.path));

      if (value.exists()) {
        let button = document.createElement("button");
        button.setAttribute("class", "opendir");
        document.l10n.setAttributes(button, "profiles-opendir");

        td.appendChild(button);

        button.addEventListener("click", function () {
          value.reveal();
        });
      }
    } else {
      document.l10n.setAttributes(td, value);
    }
  }

  createItem(
    "profiles-is-default",
    profileData.isDefault ? "profiles-yes" : "profiles-no"
  );

  createItem("profiles-rootdir", profileData.profile.rootDir, true);

  if (profileData.profile.localDir.path != profileData.profile.rootDir.path) {
    createItem("profiles-localdir", profileData.profile.localDir, true);
  }

  let renameButton = document.createElement("button");
  document.l10n.setAttributes(renameButton, "profiles-rename");
  renameButton.onclick = function () {
    renameProfile(profileData.profile);
  };
  div.appendChild(renameButton);

  if (!profileData.isInUse && profileData.canDelete) {
    let removeButton = document.createElement("button");
    document.l10n.setAttributes(removeButton, "profiles-remove");
    removeButton.onclick = function () {
      removeProfile(profileData.profile);
    };

    div.appendChild(removeButton);
  }

  if (!profileData.isDefault) {
    let defaultButton = document.createElement("button");
    document.l10n.setAttributes(defaultButton, "profiles-set-as-default");
    defaultButton.onclick = function () {
      defaultProfile(profileData.profile);
    };
    div.appendChild(defaultButton);
  }

  if (!profileData.isInUse) {
    let runButton = document.createElement("button");
    document.l10n.setAttributes(runButton, "profiles-launch-profile");
    runButton.onclick = function () {
      openProfile(profileData.profile);
    };
    div.appendChild(runButton);
  }

  let sep = document.createElement("hr");
  div.appendChild(sep);
}

// This is called from the createProfileWizard.xhtml dialog.
function CreateProfile(profile) {
  // The wizard created a profile, just make it the default.
  defaultProfile(profile);
}

function createProfileWizard() {
  // This should be rewritten in HTML eventually.
  window.browsingContext.topChromeWindow.openDialog(
    "chrome://mozapps/content/profile/createProfileWizard.xhtml",
    "",
    "centerscreen,chrome,modal,titlebar",
    ProfileService,
    { CreateProfile },
    "aboutprofiles"
  );
}

async function renameProfile(profile) {
  let newName = { value: profile.name };
  let [title, msg] = await document.l10n.formatValues([
    { id: "profiles-rename-profile-title" },
    { id: "profiles-rename-profile", args: { name: profile.name } },
  ]);

  if (Services.prompt.prompt(window, title, msg, newName, null, { value: 0 })) {
    newName = newName.value;

    if (newName == profile.name) {
      return;
    }

    try {
      profile.name = newName;
    } catch (e) {
      let [title, msg] = await document.l10n.formatValues([
        { id: "profiles-invalid-profile-name-title" },
        { id: "profiles-invalid-profile-name", args: { name: newName } },
      ]);

      Services.prompt.alert(window, title, msg);
      return;
    }

    flush();
  }
}

async function removeProfile(profile) {
  let deleteFiles = false;

  if (profile.rootDir.exists()) {
    if (!(await canDeleteProfile(profile))) {
      let [title, msg] = await document.l10n.formatValues([
        "profile-has-selectable-profiles-title",
        "profile-has-selectable-profiles-message",
      ]);
      Services.prompt.alert(window, title, msg);

      return;
    }

    let [title, msg, dontDeleteStr, deleteStr] =
      await document.l10n.formatValues([
        { id: "profiles-delete-profile-title" },
        {
          id: "profiles-delete-profile-confirm",
          args: { dir: profile.rootDir.path },
        },
        { id: "profiles-dont-delete-files" },
        { id: "profiles-delete-files" },
      ]);
    let buttonPressed = Services.prompt.confirmEx(
      window,
      title,
      msg,
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
        Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1 +
        Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_2,
      dontDeleteStr,
      null,
      deleteStr,
      null,
      { value: 0 }
    );
    if (buttonPressed == 1) {
      return;
    }

    if (buttonPressed == 2) {
      deleteFiles = true;
    }
  }

  // If we are deleting the default profile we must choose a different one.
  let isDefault = false;
  try {
    isDefault = ProfileService.defaultProfile == profile;
  } catch (e) {}

  if (isDefault) {
    for (let p of ProfileService.profiles) {
      if (profile == p) {
        continue;
      }

      if (isDefault) {
        try {
          ProfileService.defaultProfile = p;
        } catch (e) {
          // This can happen on dev-edition if a non-default profile is in use.
          // In such a case the next time that dev-edition is started it will
          // find no default profile and just create a new one.
        }
      }

      break;
    }
  }

  let { storeID } = profile;

  try {
    profile.removeInBackground(deleteFiles);
  } catch (e) {
    let [title, msg] = await document.l10n.formatValues([
      { id: "profiles-delete-profile-failed-title" },
      { id: "profiles-delete-profile-failed-message" },
    ]);

    Services.prompt.alert(window, title, msg);
    return;
  }

  if (deleteFiles && storeID) {
    await deleteSharedProfilesStore(storeID);
  }

  flush();
}

async function defaultProfile(profile) {
  try {
    ProfileService.defaultProfile = profile;
    flush();
  } catch (e) {
    // This can happen on dev-edition.
    let [title, msg] = await document.l10n.formatValues([
      { id: "profiles-cannot-set-as-default-title" },
      { id: "profiles-cannot-set-as-default-message" },
    ]);

    Services.prompt.alert(window, title, msg);
  }
}

function openProfile(profile) {
  Services.startup.createInstanceWithProfile(profile);
}

function restart(safeMode) {
  let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
    Ci.nsISupportsPRBool
  );
  Services.obs.notifyObservers(
    cancelQuit,
    "quit-application-requested",
    "restart"
  );

  if (cancelQuit.data) {
    return;
  }

  let flags = Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart;

  if (safeMode) {
    Services.startup.restartInSafeMode(flags);
  } else {
    Services.startup.quit(flags);
  }
}

function displayCurrentProfile(){

  let currentProfile = ProfileService.currentProfile;
  let profileInfo = document.getElementById("currentProfileInfo");

  if(!currentProfile){
    return;
  }

  profileInfo.removeAttribute("hidden");

  let name = document.getElementById("currentProfileName");
  let path = document.getElementById("currentProfilePath");

  document.l10n.setAttributes(name, "default-profile", { name: currentProfile.name });
  document.l10n.setAttributes(path, "default-profile-path", { path: currentProfile.rootDir.path });
}

window.addEventListener(
  "DOMContentLoaded",
  function () {
    let createButton = document.getElementById("create-button");
    createButton.addEventListener("click", createProfileWizard);

    let restartSafeModeButton = document.getElementById(
      "restart-in-safe-mode-button"
    );
    if (!Services.policies || Services.policies.isAllowed("safeMode")) {
      restartSafeModeButton.addEventListener("click", () => {
        restart(true);
      });
    } else {
      restartSafeModeButton.setAttribute("disabled", "true");
    }

    let restartNormalModeButton = document.getElementById("restart-button");
    restartNormalModeButton.addEventListener("click", () => {
      restart(false);
    });

    displayCurrentProfile();

    if (ProfileService.isListOutdated) {
      document.getElementById("owned").hidden = true;
    } else {
      document.getElementById("conflict").hidden = true;
      rebuildProfileList();
    }
  },
  { once: true }
);
