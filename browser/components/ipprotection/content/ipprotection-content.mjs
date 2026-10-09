/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
import {
  BANDWIDTH,
  LINKS,
} from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";
import { formatRemainingBandwidth } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

const { ERRORS } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
);

// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/ipprotection/ipprotection-message-bar.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/ipprotection/ipprotection-unauthenticated.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/ipprotection/ipprotection-status-card.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/ipprotection/ipprotection-status-box.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-toggle.mjs";

const SITE_RULES_SETTINGS_PANE = "privacy-vpnsiterules";

/**
 * Custom element that implements a message bar and status card for IP protection.
 */
export default class IPProtectionContentElement extends MozLitElement {
  static queries = {
    unauthenticatedEl: "ipprotection-unauthenticated",
    messagebarEl: "ipprotection-message-bar",
    statusCardEl: "ipprotection-status-card",
    upgradeEl: "#upgrade-vpn-content",
    activeSubscriptionEl: "#active-subscription-vpn-content",
    supportLinkEl: "#vpn-support-link",
    statusBoxEl: "ipprotection-status-box",
    siteExclusionControlEl: "#site-exclusion-control",
    siteExclusionToggleEl: "#site-exclusion-toggle",
    siteExclusionDescriptionEl: '#site-exclusion-toggle > [slot="description"]',
    siteRulesEl: "#site-rules-control",
    settingsButtonEl: "#vpn-settings-button",
  };

  static properties = {
    state: { type: Object, attribute: false },
    _showMessageBar: { type: Boolean, state: true },
    _messageDismissed: { type: Boolean, state: true },
  };

  #prevBandwidthWarning = false;

  constructor() {
    super();

    this.state = {};

    this.messageBarListener = this.#messageBarListener.bind(this);
    this.statusCardListener = this.#statusCardListener.bind(this);
    this._showMessageBar = false;
    this._messageDismissed = false;
  }

  connectedCallback() {
    super.connectedCallback();
    this.dispatchEvent(new CustomEvent("IPProtection:Init", { bubbles: true }));
    this.addEventListener(
      "ipprotection-status-card:user-toggled-on",
      this.#statusCardListener
    );
    this.addEventListener(
      "ipprotection-status-card:user-toggled-off",
      this.#statusCardListener
    );
    this.addEventListener(
      "ipprotection-message-bar:user-dismissed",
      this.#messageBarListener
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();

    this.removeEventListener(
      "ipprotection-status-card:user-toggled-on",
      this.#statusCardListener
    );
    this.removeEventListener(
      "ipprotection-status-card:user-toggled-off",
      this.#statusCardListener
    );
    this.removeEventListener(
      "ipprotection-message-bar:user-dismissed",
      this.#messageBarListener
    );
  }

  get canEnableConnection() {
    return this.state && this.state.isProtectionEnabled && !this.state.error;
  }

  get hasSiteExclusion() {
    return this.state?.siteData?.isExclusion ?? false;
  }

  get hasSiteInclusion() {
    return this.state?.siteData?.isInclusion ?? false;
  }

  get hasSiteRule() {
    return this.state?.siteData?.hasSiteRule ?? false;
  }

  get #hasErrors() {
    return !this.state || !!this.state.error;
  }

  handleClickSupportLink(event) {
    const win = event.target.documentGlobal;

    if (event.target === this.supportLinkEl) {
      event.preventDefault();
      win.openWebLinkIn(LINKS.PRODUCT_URL, "tab");
      this.dispatchEvent(
        new CustomEvent("IPProtection:Close", { bubbles: true })
      );
    }
  }

  handleUpgrade(event) {
    const win = event.target.documentGlobal;
    win.openWebLinkIn(LINKS.PRODUCT_URL + "#pricing", "tab");
    // Close the panel
    this.dispatchEvent(
      new CustomEvent("IPProtection:ClickUpgrade", { bubbles: true })
    );

    Glean.ipprotection.clickUpgradeButton.record();
  }

  focus() {
    if (this.state.unauthenticated) {
      this.unauthenticatedEl?.focus();
    } else {
      this.statusCardEl?.focus();
    }
  }

  #statusCardListener(event) {
    if (event.type === "ipprotection-status-card:user-toggled-on") {
      this.dispatchEvent(
        new CustomEvent("IPProtection:UserEnable", { bubbles: true })
      );
    } else if (event.type === "ipprotection-status-card:user-toggled-off") {
      this.dispatchEvent(
        new CustomEvent("IPProtection:UserDisable", { bubbles: true })
      );
    }
  }

  #messageBarListener(event) {
    if (event.type === "ipprotection-message-bar:user-dismissed") {
      this._showMessageBar = false;
      this._messageDismissed = true;
      this.state.error = "";

      if (this.state.bandwidthWarning) {
        const threshold = Services.prefs.getIntPref(
          "browser.ipProtection.bandwidthThreshold",
          0
        );
        this.dispatchEvent(
          new CustomEvent("IPProtection:DismissBandwidthWarning", {
            bubbles: true,
            composed: true,
            detail: { threshold },
          })
        );
      }
    }
  }

  handleToggleUseVPN(event) {
    let isEnabled = event.target.pressed;

    if (isEnabled) {
      this.dispatchEvent(
        new CustomEvent("IPProtection:UserEnableVPNForSite", {
          bubbles: true,
        })
      );
    } else {
      this.dispatchEvent(
        new CustomEvent("IPProtection:UserDisableVPNForSite", {
          bubbles: true,
          composed: true,
        })
      );
    }
  }

  handleClickSiteRulesLink(event) {
    event.preventDefault();
    const win = event.target.documentGlobal;
    win.openPreferences(SITE_RULES_SETTINGS_PANE);
    this.dispatchEvent(
      new CustomEvent("IPProtection:Close", { bubbles: true, composed: true })
    );
  }

  handleClickSettingsButton(event) {
    event.preventDefault();
    const win = event.target.documentGlobal;
    win.openPreferences("privacy-vpn");
    this.dispatchEvent(
      new CustomEvent("IPProtection:Close", { bubbles: true, composed: true })
    );
  }

  updated(changedProperties) {
    super.updated(changedProperties);

    // Clear messages when there is an error.
    if (this.state.error) {
      this._messageDismissed = false;
    }

    // Reset dismissed state when a higher bandwidth threshold is crossed.
    if (
      this.state.bandwidthWarning &&
      !this.#prevBandwidthWarning &&
      this._messageDismissed
    ) {
      this._messageDismissed = false;
    }
    this.#prevBandwidthWarning = !!this.state.bandwidthWarning;
  }

  messageBarTemplate() {
    let messageId;
    let messageLink;
    let messageLinkl10nId;
    let messageLinkL10nArgs;
    let messageType = "info";

    if (this.state.bandwidthWarning && this.state.bandwidthUsage) {
      messageType = "warning";
      const { value: usageLeft, useGB } = formatRemainingBandwidth(
        this.state.bandwidthUsage.remaining
      );
      const maxUsage = this.state.bandwidthUsage.max / BANDWIDTH.BYTES_IN_GB;

      messageId = useGB
        ? "ipprotection-message-bandwidth-warning"
        : "ipprotection-message-bandwidth-warning-mb";

      messageLinkL10nArgs = JSON.stringify({
        usageLeft,
        maxUsage,
      });
    } else if (this.state.onboardingMessage) {
      messageId = this.state.onboardingMessage;
      messageType = "info";

      switch (this.state.onboardingMessage) {
        case "ipprotection-message-continuous-onboarding-intro":
          break;
        case "ipprotection-message-continuous-onboarding-autostart":
          messageLink = "about:settings#privacy";
          messageLinkl10nId = "setting-link";
          break;
        case "ipprotection-message-continuous-onboarding-site-settings":
          messageLink = "about:settings#privacy";
          messageLinkl10nId = "setting-link";
          break;
      }
    }

    return html`
      <ipprotection-message-bar
        class="vpn-top-content"
        type=${messageType}
        .messageId=${ifDefined(messageId)}
        .messageLink=${ifDefined(messageLink)}
        .messageLinkl10nId=${ifDefined(messageLinkl10nId)}
        .messageLinkL10nArgs=${ifDefined(messageLinkL10nArgs)}
        .bandwidthUsage=${ifDefined(this.state.bandwidthUsage)}
      ></ipprotection-message-bar>
    `;
  }

  statusCardTemplate() {
    let hasExclusion = this.hasSiteExclusion;

    return html`
      <ipprotection-status-card
        .protectionEnabled=${this.canEnableConnection}
        .location=${this.state.location}
        .bandwidthUsage=${ifDefined(this.state.bandwidthUsage)}
        .hasExclusion=${hasExclusion}
        .isActivating=${this.state.isActivating}
        .showLocationButtonBadge=${this.state.showLocationButtonBadge}
        .isPremium=${ifDefined(this.state.isPremium)}
        .hasUpgraded=${this.state.hasUpgraded}
      ></ipprotection-status-card>
    `;
  }

  upgradeTemplate() {
    if (this.state.hasUpgraded || this.state.upgradeNotAvailable) {
      return null;
    }

    return html` <div slot="content">
      <link
        rel="stylesheet"
        href="chrome://browser/content/ipprotection/ipprotection-content.css"
      />
      <div id="upgrade-vpn-content">
        <h2
          id="upgrade-vpn-title"
          data-l10n-id="upgrade-vpn-title"
          class="vpn-title"
        ></h2>
        <span
          id="upgrade-vpn-description"
          data-l10n-id="upgrade-vpn-description"
          class="vpn-description"
        ></span>
        <moz-button
          id="upgrade-vpn-button"
          class="vpn-button"
          type="primary"
          data-l10n-id="upgrade-vpn-button"
          @click=${this.handleUpgrade}
        ></moz-button>
      </div>
    </div>`;
  }

  errorTemplate() {
    const isNetworkError = this.state.error === ERRORS.NETWORK;
    const isCatastrophicError = this.state.error === ERRORS.CATASTROPHIC;
    const isRestrictedGeoError = this.state.error === ERRORS.VPN_UNAVAILABLE;

    let headerL10nId = "ipprotection-connection-status-generic-error-title-1";
    let descriptionL10nId =
      "ipprotection-connection-status-generic-error-description";
    let errorType = ERRORS.GENERIC;
    let imageSrc = null;
    let supportSlug = null;

    if (isNetworkError) {
      headerL10nId = "ipprotection-connection-status-network-error-title-1";
      descriptionL10nId =
        "ipprotection-connection-status-network-error-description";
      errorType = ERRORS.NETWORK;
      imageSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-info.svg";
    } else if (isCatastrophicError) {
      headerL10nId = "ipprotection-connection-status-blocked-error-title-1";
      descriptionL10nId =
        "ipprotection-connection-status-generic-error-try-again";
      errorType = ERRORS.CATASTROPHIC;
      imageSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-error.svg";
    } else if (isRestrictedGeoError) {
      headerL10nId = "ipprotection-connection-status-blocked-error-title-1";
      descriptionL10nId =
        "ipprotection-connection-status-blocked-error-description-1";
      errorType = ERRORS.VPN_UNAVAILABLE;
      supportSlug = LINKS.NO_ACCESS_SUPPORT_SLUG;
    }

    return html`
      <ipprotection-status-box
        .headerL10nId=${headerL10nId}
        .descriptionL10nId=${descriptionL10nId}
        .descriptionSupportSlug=${ifDefined(supportSlug)}
        .type=${errorType}
      >
        ${imageSrc
          ? html`
              <img
                slot="image"
                role="presentation"
                class="icon"
                src=${imageSrc}
              />
            `
          : null}
      </ipprotection-status-box>
    `;
  }

  pausedTemplate() {
    return html`
      <ipprotection-status-box
        headerL10nId="ipprotection-connection-status-paused-title-2"
        descriptionL10nId="ipprotection-connection-status-paused-description-1"
        .descriptionL10nArgs=${JSON.stringify({
          maxUsage: this.state.bandwidthUsage.max / BANDWIDTH.BYTES_IN_GB,
        })}
        type="paused"
      >
        <img
          slot="image"
          role="presentation"
          class="icon"
          src="chrome://browser/content/ipprotection/assets/states/ipprotection-paused.svg"
        />
        ${this.upgradeTemplate()}
      </ipprotection-status-box>
    `;
  }

  siteRulesStatusTemplate() {
    if (
      !this.state.isSiteInclusionsEnabled ||
      !this.state.siteData ||
      this.#hasErrors ||
      !this.hasSiteRule
    ) {
      return null;
    }

    // Check to see if the rule is actually taking effect i.e. is it opposite of the state of the global VPN
    if (
      (this.hasSiteInclusion && this.state.isProtectionEnabled) ||
      (this.hasSiteExclusion && !this.state.isProtectionEnabled)
    ) {
      return null;
    }

    let dataL10nId;
    let imgSrc;

    // hasSiteRule can be set without a rule that classifies this site, so the
    // rule type has to be resolved before we commit to rendering anything.
    if (this.hasSiteInclusion) {
      dataL10nId = "site-rules-description-inclusion";
      imgSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-on.svg";
    } else if (this.hasSiteExclusion) {
      dataL10nId = "site-rules-description-exclusion";
      imgSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-off.svg";
    } else {
      return null;
    }

    return html`<div id="site-rule-status-container">
      <div id="site-rule-status-text-container">
        <span
          id="site-rule-heading"
          data-l10n-id="site-rules-status-heading"
          class="text-deemphasized"
        ></span>
        <span id="site-rule-description" data-l10n-id=${dataL10nId}></span>
      </div>
      <img src=${imgSrc} alt="" />
    </div>`;
  }

  siteRulesSettingsLinkTemplate() {
    if (
      !this.state.isSiteInclusionsEnabled ||
      !this.state.siteData ||
      this.#hasErrors
    ) {
      return null;
    }

    return html` <moz-button
      id="site-rules-control"
      type="ghost"
      data-l10n-id="site-rules-manage-rules-link-text"
      iconsrc="chrome://browser/skin/permissions.svg"
      @click=${this.handleClickSiteRulesLink}
    >
    </moz-button>`;
  }

  exclusionToggleTemplate() {
    if (
      !this.state.isSiteExceptionsEnabled ||
      this.state.isSiteInclusionsEnabled ||
      !this.state.siteData ||
      !this.state.isProtectionEnabled ||
      this.#hasErrors
    ) {
      return null;
    }

    const hasExclusion = this.hasSiteExclusion;
    const siteExclusionToggleStateL10nId = hasExclusion
      ? "site-exclusion-toggle-disabled-1"
      : "site-exclusion-toggle-enabled-1";
    return html` <div id="site-exclusion-control">
        <moz-toggle
          data-l10n-id=${siteExclusionToggleStateL10nId}
          data-l10n-attrs="label"
          id="site-exclusion-toggle"
          iconsrc="chrome://browser/content/ipprotection/assets/shield-vpn-exceptions.svg"
          inputlayout="inline-end"
          ?pressed=${!hasExclusion}
          @toggle=${this.handleToggleUseVPN}
          >${!hasExclusion
            ? html`<span
                slot="description"
                data-l10n-id="site-exclusion-toggle-description"
              ></span>`
            : null}
        </moz-toggle>
      </div>
      <hr role="separator" />`;
  }

  footerTemplate() {
    return html`
      <moz-button
        type="ghost"
        data-l10n-id="ipprotection-settings-link"
        iconsrc="chrome://global/skin/icons/settings.svg"
        id="vpn-settings-button"
        @click=${this.handleClickSettingsButton}
      >
        ></moz-button
      >
    `;
  }

  enrollingTemplate() {
    return html`
      <div id="enrolling-container" aria-busy="true">
        <span id="enrolling-header">
          <span>
            <div class="skeleton skeleton-title"></div>
            <div class="skeleton skeleton-line"></div>
          </span>
          <div class="skeleton skeleton-image"></div>
        </span>
        <div class="skeleton skeleton-line-thick"></div>
        <div class="skeleton skeleton-line-thick"></div>
      </div>
    `;
  }

  mainContentTemplate() {
    if (this.state.isEnrolling) {
      return html`${this.enrollingTemplate()} ${this.footerTemplate()}`;
    }

    if (this.state.unauthenticated) {
      return html`
        <ipprotection-unauthenticated></ipprotection-unauthenticated>
      `;
    }

    if (this.#hasErrors) {
      return html` ${this.errorTemplate()}
        <div class="vpn-bottom-content">${this.footerTemplate()}</div>`;
    }

    if (this.state.paused) {
      return html` ${this.pausedTemplate()}
        <div class="vpn-bottom-content">${this.footerTemplate()}</div>`;
    }

    if (this.state.isSiteInclusionsEnabled) {
      return html`
        ${this.statusCardTemplate()}
        <div class="vpn-bottom-content">
          ${this.siteRulesStatusTemplate()}
          ${this.siteRulesSettingsLinkTemplate()} ${this.footerTemplate()}
        </div>
      `;
    }

    return html`
      ${this.statusCardTemplate()} ${this.exclusionToggleTemplate()}
      <div class="vpn-bottom-content">${this.footerTemplate()}</div>
    `;
  }

  render() {
    if (
      (this.state.onboardingMessage || this.state.bandwidthWarning) &&
      !this._messageDismissed &&
      !this.state.unauthenticated &&
      !this.state.paused
    ) {
      this._showMessageBar = true;
    } else if (
      (!this.state.onboardingMessage && !this.state.bandwidthWarning) ||
      this.state.paused
    ) {
      // Remove the message bar if we can no longer render messages before they were dismissed
      // or when in the paused state.
      this._showMessageBar = false;
    }

    const messageBar = this._showMessageBar ? this.messageBarTemplate() : null;

    let content = html`${messageBar}${this.mainContentTemplate()}`;

    // TODO: Conditionally render post-upgrade subview within #ipprotection-content-wrapper - Bug 1973813
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/ipprotection/ipprotection-content.css"
      />
      <div id="ipprotection-content-wrapper">${content}</div>
    `;
  }
}

customElements.define("ipprotection-content", IPProtectionContentElement);
