/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  FunxplorerVpn: "resource:///modules/FunxplorerVpn.sys.mjs",
});

var gFunxplorerVpn = {
  _initialized: false,

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;
    MozXULElement.insertFTLIfNeeded("browser/appmenu.ftl");
    Services.obs.addObserver(this, FunxplorerVpn.TOPIC);
    window.addEventListener("unload", () => this.uninit(), { once: true });
    this.updateUI();
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    Services.obs.removeObserver(this, FunxplorerVpn.TOPIC);
    this._initialized = false;
  },

  observe() {
    this.updateUI();
  },

  updateUI() {
    let on = FunxplorerVpn.enabled;
    document.documentElement.toggleAttribute("funxplorervpn", on);
    let menuButton = document.getElementById("appMenu-funvpn-button");
    if (menuButton) {
      menuButton.checked = on;
    }
    for (let node of document.querySelectorAll("#funvpn-button")) {
      node.setAttribute("checked", on ? "true" : "false");
      node.setAttribute("aria-pressed", on ? "true" : "false");
    }
  },

  async toggle() {
    let result = await FunxplorerVpn.toggle();
    this.updateUI();
    if (window.gFunxplorerTor) {
      window.gFunxplorerTor.updateUI();
    }
    if (!result.ok) {
      Services.prompt.alert(
        window,
        "FUNVPN",
        result.error || "Could not connect to FUNVPN."
      );
    }
  },
};
