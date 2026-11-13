/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AboutAddonsElementMixin,
  formatUTMParams,
  getScreenshotUrlForAddon,
} from "../aboutaddons-utils.mjs";
import { gViewController } from "../view-controller.mjs";
import { AddonCard } from "./addon-card.mjs";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const AboutAddonsHTMLDivElement = AboutAddonsElementMixin(HTMLDivElement);

class RecommendedAddonName extends AboutAddonsHTMLDivElement {
  static get markup() {
    return `
      <template>
        <div class="disco-card-head">
          <h3 class="disco-addon-name"></h3>
          <span class="disco-addon-author"
            ><a data-l10n-name="author" target="_blank"></a
          ></span>
        </div>
        <button class="disco-cta-button" action="install-addon"></button>
        <button
          class="disco-cta-button"
          data-l10n-id="manage-addon-button"
          action="manage-addon"
        ></button>
      </template>
    `;
  }
  connectCallback() {
    this.render();
  }
  disconnectCallback() {
    this.textContent = "";
  }
  render() {
    this.append(RecommendedAddonName.fragment);
  }
}
customElements.define("recommended-addon-name", RecommendedAddonName, {
  extends: "div",
});

class RecommendedAddonDescription extends AboutAddonsHTMLDivElement {
  static get markup() {
    return `
      <template>
        <div>
          <span class="disco-description-main"></span>
        </div>
        <div class="disco-description-statistics">
          <moz-five-star></moz-five-star>
          <span class="disco-user-count"></span>
        </div>
      </template>
    `;
  }
  connectCallback() {
    this.render();
  }
  disconnectCallback() {
    this.textContent = "";
  }
  render() {
    this.append(RecommendedAddonDescription.fragment);
  }
}
customElements.define(
  "recommended-addon-description",
  RecommendedAddonDescription,
  { extends: "div" }
);

/**
 * A child element of `<recommended-addon-list>`. It should be initialized
 * by calling `setDiscoAddon()` first. Call `setAddon(addon)` if it has been
 * installed, and call `setAddon(null)` upon uninstall.
 *
 *    let discoAddon = new DiscoAddonWrapper({ ... });
 *    let card = document.createElement("recommended-addon-card");
 *    card.setDiscoAddon(discoAddon);
 *    document.body.appendChild(card);
 *
 *    AddonManager.getAddonsByID(discoAddon.id)
 *      .then(addon => card.setAddon(addon));
 */
class RecommendedAddonCard extends HTMLElement {
  /**
   * @param {DiscoAddonWrapper} addon
   *        The details of the add-on that should be rendered in the card.
   */
  setDiscoAddon(addon) {
    this.addonId = addon.id;

    // Save the information so we can install.
    this.discoAddon = addon;

    let card = AddonCard.fragment.firstElementChild;

    // Replace addon card name container with an instance
    // of the RecommendedAddonNameContainer webcomponent.
    let heading = card.querySelector(".addon-name-container");
    let recommendedHeading = document.createElement("div", {
      is: "recommended-addon-name",
    });
    recommendedHeading.setAttribute("class", "addon-name-container");
    recommendedHeading.render();
    heading.replaceWith(recommendedHeading);

    this.setCardContent(card, addon);

    if (addon.type != "theme") {
      // Replace addon card description with an instance
      // of the RecommendedAddonNameContainer webcomponent.
      let description = card.querySelector(".addon-description");
      let recommendedDescription = document.createElement("div", {
        is: "recommended-addon-description",
      });
      recommendedDescription.setAttribute("class", "addon-description");
      description.replaceWith(recommendedDescription);
      recommendedDescription.render();
      this.setCardDescription(card, addon);
    }
    this.registerButtons(card, addon);

    this.textContent = "";
    this.append(card);

    // We initially assume that the add-on is not installed.
    this.setAddon(null);
  }

  /**
   * Fills in all static parts of the card.
   *
   * @param {HTMLElement} card
   *        The primary content of this card.
   * @param {DiscoAddonWrapper} addon
   */
  setCardContent(card, addon) {
    // Set the icon.
    if (addon.type == "theme") {
      card.querySelector(".addon-icon").hidden = true;
    } else {
      card.querySelector(".addon-icon").src = AddonManager.getPreferredIconURL(
        addon,
        32,
        window
      );
    }

    // Set the theme preview.
    let preview = card.querySelector(".card-heading-image");
    if (addon.type == "theme") {
      let screenshotUrl = getScreenshotUrlForAddon(addon);
      if (screenshotUrl) {
        preview.src = screenshotUrl;
        preview.hidden = false;
      }
    } else {
      preview.hidden = true;
    }

    // Set the name.
    card.querySelector(".disco-addon-name").textContent = addon.name;

    // Set the author name and link to AMO.
    if (addon.creator) {
      let authorInfo = card.querySelector(".disco-addon-author");
      document.l10n.setAttributes(authorInfo, "created-by-author", {
        author: addon.creator.name,
      });
      // This is intentionally a link to the add-on listing instead of the
      // author page, because the add-on listing provides more relevant info.
      authorInfo.querySelector("a").href = formatUTMParams(
        "discopane-entry-link",
        addon.amoListingUrl
      );
      authorInfo.hidden = false;
    }
  }

  setCardDescription(card, addon) {
    // Set the description. Note that this is the editorial description, not
    // the add-on's original description that would normally appear on a card.
    card.querySelector(".disco-description-main").textContent =
      addon.editorialDescription;

    let hasStats = false;
    if (addon.averageRating) {
      hasStats = true;
      card.querySelector("moz-five-star").rating = addon.averageRating;
    } else {
      card.querySelector("moz-five-star").hidden = true;
    }

    if (addon.dailyUsers) {
      hasStats = true;
      let userCountElem = card.querySelector(".disco-user-count");
      document.l10n.setAttributes(userCountElem, "user-count", {
        dailyUsers: addon.dailyUsers,
      });
    }

    card.querySelector(".disco-description-statistics").hidden = !hasStats;
  }

  registerButtons(card, addon) {
    let installButton = card.querySelector("[action='install-addon']");
    if (addon.type == "theme") {
      document.l10n.setAttributes(installButton, "install-theme-button");
    } else {
      document.l10n.setAttributes(installButton, "install-extension-button");
    }

    this.addEventListener("click", this);
  }

  handleEvent(event) {
    let action = event.target.getAttribute("action");
    switch (action) {
      case "install-addon":
        this.installDiscoAddon();
        break;
      case "manage-addon":
        gViewController.loadView(`detail/${this.addonId}`);
        break;
    }
  }

  async installDiscoAddon() {
    let addon = this.discoAddon;
    let url = addon.sourceURI.spec;
    let install = await AddonManager.getInstallForURL(url, {
      id: addon.id,
      name: addon.name,
      telemetryInfo: {
        source: "disco",
        taarRecommended: addon.taarRecommended,
      },
      type: addon.type,
    });
    // We are hosted in a <browser> in about:addons, but we can just use the
    // main tab's browser since all of it is using the system principal.
    let browser = window.docShell.chromeEventHandler;
    AddonManager.installAddonFromWebpage(
      "application/x-xpinstall",
      browser,
      Services.scriptSecurityManager.getSystemPrincipal(),
      install
    );
  }

  /**
   * @param {AddonWrapper|null} addon
   *        The add-on that has been installed; null if it has been removed.
   */
  setAddon(addon) {
    let card = this.firstElementChild;
    card.querySelector("[action='install-addon']").hidden = !!addon;
    card.querySelector("[action='manage-addon']").hidden = !addon;

    this.dispatchEvent(new CustomEvent("disco-card-updated")); // For testing.
  }
}
customElements.define("recommended-addon-card", RecommendedAddonCard);
