/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/profiles/profile-card.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-checkbox.mjs";

const { SelectableProfileService } = ChromeUtils.importESModule(
  "resource:///modules/profiles/SelectableProfileService.sys.mjs"
);

/**
 * The element for display SelectableProfiles in the profile selector window
 */
export class ProfileSelector extends MozLitElement {
  static properties = {
    profiles: { type: Array },
    showSelector: { type: Boolean },
  };

  static queries = {
    checkbox: "moz-checkbox",
    profileCards: { all: "profile-card" },
    createProfileCard: "new-profile-card",
  };

  #initPromise = null;
  #startupParams = null;

  constructor() {
    super();

    this.#initPromise = this.init();
    if (window.arguments?.[0] instanceof Ci.nsIDialogParamBlock) {
      this.#startupParams = window.arguments[0];
    }
  }

  get isStartupUI() {
    return !!this.#startupParams;
  }

  /**
   * Sets the return block for the startup UI.
   *
   * @param {SelectableProfile} profile The profile to launch
   * @param {string[]} args Any additional command line arguments to append
   */
  async setLaunchArguments(profile, args = []) {
    if (!this.#startupParams) {
      return;
    }

    this.#startupParams.SetInt(
      0,
      Ci.nsIToolkitProfileService.launchWithProfile
    );
    // Set start offline to false.
    this.#startupParams.SetInt(1, 0);
    // Number of new arguments.
    this.#startupParams.SetInt(2, args.length);

    this.#startupParams.objects.insertElementAt(await profile.rootDir, 0);
    this.#startupParams.objects.insertElementAt(await profile.localDir, 1);

    this.#startupParams.SetNumberStrings(args.length);
    for (let i = 0; i < args.length; i++) {
      this.#startupParams.SetString(i, args[i]);
    }
  }

  async getUpdateComplete() {
    let result = await super.getUpdateComplete();
    await this.#initPromise;
    return result;
  }

  async init() {
    if (this.initialized) {
      return;
    }

    document.addEventListener("LaunchProfile", this);
    document.addEventListener("CreateProfile", this);
    document.addEventListener("DeleteProfile", this);

    this.selectableProfileService = SelectableProfileService;

    await this.selectableProfileService.init();
    await this.selectableProfileService.maybeSetupDataStore();
    this.profiles = await this.selectableProfileService.getAllProfiles();
    this.showSelector =
      this.selectableProfileService.groupToolkitProfile.showProfileSelector;

    if (!this.profiles.length) {
      await this.selectableProfileService.setShowProfileSelectorWindow(false);
    }

    this.initialized = true;
    this.#initPromise = null;

    if (this.isStartupUI) {
      window.addEventListener("unload", () => {
        // In case the user closed the window manually.
        this.selectableProfileService.uninit();
      });
    }
  }

  handleCheckboxToggle() {
    this.showSelector = this.checkbox.checked;
    let state = this.showSelector ? "enabled" : "disabled";
    Glean.profilesSelectorWindow.showAtStartup.record({ value: state });
    this.selectableProfileService.setShowProfileSelectorWindow(
      this.showSelector
    );
  }

  async launchProfile(profile, url) {
    if (this.isStartupUI) {
      await this.setLaunchArguments(profile, url ? ["-url", url] : []);
      await this.selectableProfileService.uninit();
    } else {
      this.selectableProfileService.launchInstance(profile, url);
    }

    window.close();
  }

  async handleEvent(event) {
    switch (event.type) {
      case "LaunchProfile": {
        Glean.profilesSelectorWindow.launch.record();
        let { profile, url } = event.detail;
        await this.launchProfile(profile, url);
        break;
      }
      case "CreateProfile": {
        let profile =
          await this.selectableProfileService.createNewProfile(false);
        await this.launchProfile(profile, "about:newprofile");
        break;
      }
      case "DeleteProfile": {
        let profile = event.detail;
        await this.launchProfile(profile, "about:deleteprofile");
        break;
      }
    }
  }

  render() {
    if (!this.profiles) {
      return null;
    }

    return html`<link
        rel="stylesheet"
        href="chrome://browser/content/profiles/profile-selector.css"
      />
      <link
        rel="stylesheet"
        href="chrome://global/skin/in-content/common.css"
      />
      <img
        class="logo"
        data-l10n-id="profile-window-logo"
        data-l10n-attrs="alt"
        src="chrome://branding/content/about-logo.svg"
      />
      <h1 data-l10n-id="profile-window-heading"></h1>
      <p class="profiles-body-text" data-l10n-id="profile-window-body"></p>
      <div class="profile-list">
        ${this.profiles.map(
          p => html`<profile-card .profile=${p}></profile-card>`
        )}
        <new-profile-card></new-profile-card>
      </div>
      <moz-checkbox
        @click=${this.handleCheckboxToggle}
        data-l10n-id="profile-window-checkbox-label-2"
        ?checked=${this.showSelector}
      >
        ${!this.showSelector
          ? html`<span
              slot="description"
              data-l10n-id="profile-window-checkbox-subcopy"
            ></span>`
          : null}
      </moz-checkbox>`;
  }
}

customElements.define("profile-selector", ProfileSelector);
