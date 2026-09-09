/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  FunxplorerShield: "resource:///modules/FunxplorerShield.sys.mjs",
});

var gFunxplorerShield = {
  _initialized: false,

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;
    MozXULElement.insertFTLIfNeeded("browser/appmenu.ftl");
    Services.obs.addObserver(this, FunxplorerShield.TOPIC);
    Services.obs.addObserver(this, "startup");
    window.addEventListener("unload", () => this.uninit(), { once: true });
    this.updateUI();
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    Services.obs.removeObserver(this, FunxplorerShield.TOPIC);
    try {
      Services.obs.removeObserver(this, "startup");
    } catch (_e) {}
    this._initialized = false;
  },

  observe() {
    this.updateUI();
  },

  async updateUI() {
    let on = await FunxplorerShield.isEnabled();
    document.documentElement.toggleAttribute("funxplorershield", on);
    let menuButton = document.getElementById("appMenu-funshield-button");
    if (menuButton) {
      menuButton.checked = on;
    }
    for (let node of document.querySelectorAll("#funshield-button")) {
      node.setAttribute("checked", on ? "true" : "false");
      node.setAttribute("aria-pressed", on ? "true" : "false");
    }
  },

  async toggle() {
    let result = await FunxplorerShield.toggle();
    this.updateUI();
    if (!result.ok) {
      Services.prompt.alert(
        window,
        "FUNSHIELD",
        result.error || "FUNSHIELD could not change state."
      );
    }
  },

  async openDashboard() {
    let result = await FunxplorerShield.openDashboard(window);
    if (!result.ok) {
      Services.prompt.alert(
        window,
        "FUNSHIELD",
        result.error || "Could not open FUNSHIELD."
      );
    }
  },
};
