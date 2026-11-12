// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

/* global MozXULElement */

var gAppManagerDialog = {
  _removed: [],

  onLoad() {
    document.mozSubdialogReady = this.init();
  },

  async init() {
    this.handlerInfo = window.arguments[0];
    let { isValidHandlerApp, getIconURLForHandlerApp } = window.arguments[1];

    document.addEventListener("dialogaccept", function () {
      gAppManagerDialog.onOK();
    });

    document
      .getElementById("cmd_remove")
      .addEventListener("command", () => this.remove());

    const appDescElem = document.getElementById("appDescription");
    if (this.handlerInfo.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      let { typeDescription } = this.handlerInfo;
      let typeStr;
      if (typeDescription.id) {
        MozXULElement.insertFTLIfNeeded("browser/preferences/preferences.ftl");
        typeStr = await document.l10n.formatValue(
          typeDescription.id,
          typeDescription.args
        );
      } else {
        typeStr = typeDescription.raw;
      }
      document.l10n.setAttributes(appDescElem, "app-manager-handle-file", {
        type: typeStr,
      });
    } else {
      document.l10n.setAttributes(appDescElem, "app-manager-handle-protocol", {
        type: this.handlerInfo.typeDescription.raw,
      });
    }

    let list = document.getElementById("appList");
    list.addEventListener("select", () => this.onSelect());
    let listFragment = document.createDocumentFragment();
    for (let app of this.handlerInfo.possibleApplicationHandlers.enumerate()) {
      if (!isValidHandlerApp(app)) {
        continue;
      }

      let item = document.createXULElement("richlistitem");
      listFragment.append(item);
      item.app = app;

      let image = document.createXULElement("image");
      image.setAttribute("src", getIconURLForHandlerApp(app));
      item.appendChild(image);

      let label = document.createXULElement("label");
      label.setAttribute("value", app.name);
      item.appendChild(label);
    }
    list.append(listFragment);

    // Triggers onSelect which populates label
    list.selectedIndex = 0;

    // We want to block on those elements being localized because the
    // result will impact the size of the subdialog.
    await document.l10n.translateElements([
      appDescElem,
      document.getElementById("appType"),
    ]);
  },

  onOK: function appManager_onOK() {
    if (this._removed.length) {
      for (var i = 0; i < this._removed.length; ++i) {
        this.handlerInfo.removePossibleApplicationHandler(this._removed[i]);
      }

      this.handlerInfo.store();
    }
  },

  remove: function appManager_remove() {
    var list = document.getElementById("appList");
    this._removed.push(list.selectedItem.app);
    var index = list.selectedIndex;
    var element = list.selectedItem;
    list.removeItemFromSelection(element);
    element.remove();
    if (list.itemCount == 0) {
      // The list is now empty, make the bottom part disappear
      document.getElementById("appDetails").hidden = true;
    } else {
      // Select the item at the same index, if we removed the last
      // item of the list, select the previous item
      if (index == list.itemCount) {
        --index;
      }
      list.selectedIndex = index;
    }
  },

  onSelect: function appManager_onSelect() {
    var list = document.getElementById("appList");
    if (!list.selectedItem) {
      document.getElementById("remove").disabled = true;
      return;
    }
    document.getElementById("remove").disabled = false;
    var app = list.selectedItem.app;
    var address = "";
    if (app instanceof Ci.nsILocalHandlerApp) {
      address = app.executable.path;
    } else if (app instanceof Ci.nsIWebHandlerApp) {
      address = app.uriTemplate;
    }
    document.getElementById("appLocation").value = address;
    const l10nId =
      app instanceof Ci.nsILocalHandlerApp
        ? "app-manager-local-app-info"
        : "app-manager-web-app-info";
    const appTypeElem = document.getElementById("appType");
    document.l10n.setAttributes(appTypeElem, l10nId);
  },
};

window.addEventListener("load", () => gAppManagerDialog.onLoad());
