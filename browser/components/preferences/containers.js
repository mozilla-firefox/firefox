/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from preferences.js */

const defaultContainerIcon = "fingerprint";
const defaultContainerColor = "blue";

let gContainersPane = {
  init() {
    this._list = document.getElementById("containersView");

    document
      .getElementById("backContainersButton")
      .addEventListener("command", function () {
        Services.prefs.getBoolPref("browser.settings-redesign.enabled", false)
          ? gotoPref("tabsBrowsing")
          : gotoPref("general");
      });

    document
      .getElementById("containersAdd")
      .addEventListener("command", function () {
        gContainersPane.onAddButtonCommand();
      });

    this._rebuildView();
  },

  _rebuildView() {
    const containers = ContextualIdentityService.getPublicIdentities();
    while (this._list.firstChild) {
      this._list.firstChild.remove();
    }
    for (let container of containers) {
      let item = document.createXULElement("richlistitem");

      let outer = document.createXULElement("hbox");
      outer.setAttribute("flex", 1);
      outer.setAttribute("align", "center");
      item.appendChild(outer);

      let userContextIcon = document.createXULElement("hbox");
      userContextIcon.className = "userContext-icon";
      userContextIcon.classList.add("userContext-icon-inprefs");
      userContextIcon.classList.add("identity-icon-" + container.icon);
      userContextIcon.classList.add("identity-color-" + container.color);
      outer.appendChild(userContextIcon);

      let label = document.createXULElement("label");
      label.className = "userContext-label-inprefs";
      label.setAttribute("flex", 1);
      let containerName = ContextualIdentityService.getUserContextLabel(
        container.userContextId
      );
      label.textContent = containerName;
      label.setAttribute("tooltiptext", containerName);
      outer.appendChild(label);

      let containerButtons = document.createXULElement("hbox");
      containerButtons.className = "container-buttons";
      item.appendChild(containerButtons);

      let prefsButton = document.createXULElement("button");
      prefsButton.addEventListener("command", function (event) {
        gContainersPane.onPreferenceCommand(event.originalTarget);
      });
      prefsButton.setAttribute("value", container.userContextId);
      document.l10n.setAttributes(prefsButton, "containers-settings-button");
      containerButtons.appendChild(prefsButton);

      let removeButton = document.createXULElement("button");
      removeButton.addEventListener("command", function (event) {
        gContainersPane.onRemoveCommand(event.originalTarget);
      });
      removeButton.setAttribute("value", container.userContextId);
      document.l10n.setAttributes(removeButton, "containers-remove-button");
      containerButtons.appendChild(removeButton);

      this._list.appendChild(item);
    }
  },

  async onRemoveCommand(button) {
    let userContextId = parseInt(button.getAttribute("value"), 10);

    let count = ContextualIdentityService.countContainerTabs(userContextId);
    if (count > 0) {
      let [title, message, okButton, cancelButton] =
        await document.l10n.formatValues([
          { id: "containers-remove-alert-title" },
          { id: "containers-remove-alert-msg", args: { count } },
          { id: "containers-remove-ok-button" },
          { id: "containers-remove-cancel-button" },
        ]);

      let buttonFlags =
        Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
        Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;

      let rv = Services.prompt.confirmEx(
        window,
        title,
        message,
        buttonFlags,
        okButton,
        cancelButton,
        null,
        null,
        {}
      );
      if (rv != 0) {
        return;
      }

      await ContextualIdentityService.closeContainerTabs(userContextId);
    }

    ContextualIdentityService.remove(userContextId);
    this._rebuildView();
  },

  onPreferenceCommand(button) {
    this.openPreferenceDialog(button.getAttribute("value"));
  },

  onAddButtonCommand() {
    this.openPreferenceDialog(null);
  },

  openPreferenceDialog(userContextId) {
    let identity = {
      name: "",
      icon: defaultContainerIcon,
      color: defaultContainerColor,
    };
    if (userContextId) {
      identity =
        ContextualIdentityService.getPublicIdentityFromId(userContextId);
      identity.name = ContextualIdentityService.getUserContextLabel(
        identity.userContextId
      );
    }

    const params = { userContextId, identity };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/containers.xhtml",
      undefined,
      params
    );
  },
};
