/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  FunxplorerTor: "resource:///modules/FunxplorerTor.sys.mjs",
});

var gFunxplorerTor = {
  _initialized: false,

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;
    MozXULElement.insertFTLIfNeeded("browser/appmenu.ftl");
    Services.obs.addObserver(this, FunxplorerTor.TOPIC);
    window.addEventListener("unload", () => this.uninit(), { once: true });
    this.updateUI();
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    Services.obs.removeObserver(this, FunxplorerTor.TOPIC);
    this._initialized = false;
  },

  observe() {
    this.updateUI();
  },

  updateUI() {
    let on = FunxplorerTor.enabled;
    document.documentElement.toggleAttribute("funxplorertor", on);
    let menuButton = document.getElementById("appMenu-tor-button");
    if (menuButton) {
      menuButton.checked = on;
    }
    for (let node of document.querySelectorAll("#tor-button")) {
      node.setAttribute("checked", on ? "true" : "false");
      node.setAttribute("aria-pressed", on ? "true" : "false");
    }
  },

  async toggle() {
    let result = await FunxplorerTor.toggle();
    this.updateUI();
    if (window.gFunxplorerVpn) {
      window.gFunxplorerVpn.updateUI();
    }
    if (!result.ok) {
      Services.prompt.alert(
        window,
        "FUNTOR",
        result.error || "Could not connect to FUNTOR."
      );
    }
  },
};
