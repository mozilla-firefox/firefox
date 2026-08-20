/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable import/no-unassigned-import */

import {
  gHasSts,
  gIsCertError,
  gErrorCode,
  searchParams,
  isCaptive,
  getCSSClass,
  getHostName,
  getSubjectAltNames,
  getFailedCertificatesAsPEMString,
  handleNSSFailure,
  recordSecurityUITelemetry,
  getFilePath,
  gOffline,
  gNoConnectivity,
  retryThis,
  VPN_ACTIVE,
  detectClockSkew,
} from "chrome://global/content/aboutNetErrorHelpers.mjs";
import { initializeRegistry } from "chrome://global/content/errors/error-registry.mjs";
import {
  getResolvedErrorConfig,
  resolveErrorID,
} from "chrome://global/content/errors/error-lookup.mjs";
import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { NET_ERROR_ILLUSTRATIONS } from "chrome://global/content/errors/net-error-illustrations.mjs";
import "chrome://global/content/elements/moz-button-group.mjs";
import "chrome://global/content/elements/moz-button.mjs";
import "chrome://global/content/elements/moz-support-link.mjs";

const HOST_NAME = getHostName();
const FELT_PRIVACY_REFRESH = RPMGetBoolPref(
  "security.certerrors.felt-privacy-v1",
  false
);
const EXPERT_BAD_CERT = getCSSClass() === "expertBadCert";
const SEARCH_CTA_ENABLED = RPMGetBoolPref(
  "browser.netError.searchCTA.enabled",
  false
);
// The only value of the parent's action vocabulary this page has to recognize,
// meaning no CTA. It is duplicated rather than imported because the vocabulary
// lives in URLKeywordAnalyzer, which is chrome-only, and this module runs in the
// content process. It arrives as a string over IPC either way.
const SEARCH_CTA_ACTION_NONE = "none";

export class NetErrorCard extends MozLitElement {
  static properties = {
    hostname: { type: String },
    domainMismatchNames: { type: String },
    advancedShowing: { type: Boolean, reflect: true },
    certErrorDebugInfoShowing: { type: Boolean, reflect: true },
    certificateErrorText: { type: String },
    showPrefReset: { type: Boolean },
    showTlsNotice: { type: Boolean },
    showTrrSettingsButton: { type: Boolean },
    searchCTAResolved: { type: Boolean },
    searchCTAHasEngine: { type: Boolean },
    searchCTADomain: { type: String },
    searchCTAQuery: { type: String },
    searchCTAAction: { type: String },
    searchCTAOfflineAborted: { type: Boolean },
  };

  static queries = {
    copyButtonTop: "#copyToClipboardTop",
    copyButtonBot: "#copyToClipboardBot",
    exceptionButton: "#exception-button",
    errorCode: "#errorCode",
    advancedContainer: ".advanced-container",
    advancedButton: "#advanced-button",
    errorIntro: "#error-intro",
    dnsSuggestion: "#dns-suggestion",
    certErrorDebugInfo: "#certificateErrorDebugInformation",
    certErrorText: "#certificateErrorText",
    viewCertificate: "#viewCertificate",
    errorTitle: "#error-title",
    responseStatusLabel: "#response-status-label",
    returnButton: "#returnButton",
    learnMoreLink: "#error-learn-more-link",
    whatCanYouDo: "#whatCanYouDo",
    whyDangerous: "#fp-why-site-dangerous",
    tryAgainButton: "#tryAgainButton",
    prefResetButton: "#prefResetButton",
    tlsNotice: "#tlsVersionNotice",
    badStsCertExplanation: "#badStsCertExplanation",
    reloadButton: "#reloadButton",
    searchCTAButton: "#searchCTAButton",
    searchCTAOfflineMessage: "#searchCTAOfflineMessage",
  };

  static isSupported() {
    if (!FELT_PRIVACY_REFRESH) {
      return false;
    }

    initializeRegistry();

    let errorInfo = { errorCodeString: "" };
    try {
      errorInfo = gIsCertError
        ? document.getFailedCertSecurityInfo()
        : document.getNetErrorInfo();
    } catch {}

    const resolvedErrorId = resolveErrorID({
      errorCodeString: errorInfo.errorCodeString,
      gErrorCode,
      noConnectivity: gNoConnectivity,
      vpnActive: VPN_ACTIVE,
    });

    // Bug 2038887: the felt privacy error page does not surface the DoH
    // domain, learn-more link, exclude-domain button, or settings shortcut
    // that the legacy page provides for TRR-only failures. Fall back to the
    // legacy page until the felt privacy page supports this case.
    if (
      resolvedErrorId === "dnsNotFound" &&
      !gNoConnectivity &&
      RPMIsTRROnlyFailure()
    ) {
      return false;
    }

    return resolvedErrorId !== null;
  }

  constructor() {
    super();

    this.domainMismatchNames = null;
    this.advancedShowing = false;
    this.certErrorDebugInfoShowing = false;
    this.certificateErrorText = null;
    this.domainMismatchNamesPromise = null;
    this.certificateErrorTextPromise = null;
    this.showCustomNetErrorCard = false;
    this.showPrefReset = false;
    this.showTlsNotice = false;
    this.showTrrSettingsButton = false;
    this.trrTelemetryData = null;
    this.searchCTAResolved = false;
    this.searchCTAHasEngine = false;
    this.searchCTADomain = "";
    this.searchCTAQuery = "";
    this.searchCTAAction = "";
    this.searchCTAOfflineAborted = false;
    // Exit-outcome tracking (bug 2055717): plain, non-reactive flags mapped to
    // an exit_reason at pagehide.
    this.ctaClicked = false;
    this.reloadClicked = false;
    this.suggestionClicked = false;
    this.exitRecorded = false;
    this.onPageHide = () => this.recordExitReason();
    this.onShadowClick = e => this.handleShadowClick(e);
  }

  async getUpdateComplete() {
    // Fetch domain mismatch names and cert error text before rendering
    // to ensure Fluent localization has all required variables
    const promises = [
      this.errorConfig?.advanced?.requiresDomainMismatchNames &&
        !this.domainMismatchNames &&
        this.getDomainMismatchNames(),
      document.getFailedCertSecurityInfo &&
        !this.certificateErrorText &&
        this.getCertificateErrorText(),
      this.domainMismatchNamesPromise,
      this.certificateErrorTextPromise,
    ].filter(Boolean);

    if (promises.length) {
      await Promise.all(promises);
    }

    return super.getUpdateComplete();
  }

  connectedCallback() {
    super.connectedCallback();
    this.init();
  }

  firstUpdated() {
    // Dispatch this event so tests can detect that we finished loading the error page.
    document.dispatchEvent(
      new CustomEvent("AboutNetErrorLoad", { bubbles: true })
    );
    this.focusPrimaryButton();

    // Record how the user leaves a CTA-eligible page (bug 2055717). The
    // suggestion link is injected into the shadow tree by NetErrorChild, so
    // catch its clicks via delegation. Only CTA-eligible pages need these:
    // every other error page would carry a pagehide handler that runs during
    // teardown just to decide it has nothing to record.
    if (this.shouldShowSearchCTA()) {
      window.addEventListener("pagehide", this.onPageHide);
      this.shadowRoot.addEventListener("click", this.onShadowClick);
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    // Only the window listener has to come off. window outlives this element,
    // so a listener left there would keep the card alive and would still record
    // an exit for a card that is no longer on the page. The shadow root cannot
    // outlive us, so its listener needs no cleanup.
    window.removeEventListener("pagehide", this.onPageHide);
  }

  shouldHideExceptionButton() {
    let prefValue = RPMGetBoolPref(
      "security.certerror.hideAddException",
      false
    );
    if (prefValue || this.errorConfig.hasNoUserFix) {
      return true;
    }

    const isIframed = window.self !== window.top;
    return gHasSts || !this.errorInfo.errorIsOverridable || isIframed;
  }

  init() {
    this.hostname = HOST_NAME;
    this.errorInfo = this.getErrorInfo();
    // isSupported() gates component creation, so resolvedErrorId should never
    // be null here. getErrorConfig() guards against it defensively regardless.
    this.resolvedErrorId = resolveErrorID({
      errorCodeString: this.errorInfo.errorCodeString,
      gErrorCode,
      noConnectivity: gNoConnectivity,
      vpnActive: VPN_ACTIVE,
    });
    this.errorConfig = this.getErrorConfig();
    this.hideExceptionButton = this.shouldHideExceptionButton();

    const titles = {
      net: "neterror-page-title",
      blocked: "neterror-blocked-by-policy-page-title",
    };
    document.l10n.setAttributes(
      document.querySelector("title"),
      titles[this.errorConfig.category] ?? "fp-certerror-page-title"
    );

    // Record telemetry when the error page loads
    if (gIsCertError && !isCaptive()) {
      recordSecurityUITelemetry(
        "securityUiCerterror",
        "loadAboutcerterror",
        this.errorInfo
      );
    }

    // nssFailure2 are TLS errors which are tracked by load_abouttlserror
    if (!gIsCertError && gErrorCode !== "nssFailure2" && !isCaptive()) {
      let neterrorInfo = Object.assign({}, this.errorInfo);
      if (!neterrorInfo.errorCodeString) {
        neterrorInfo.errorCodeString = gErrorCode;
      }
      recordSecurityUITelemetry(
        "securityUiNeterror",
        "loadAboutneterror",
        neterrorInfo
      );
    }

    // Check if the connection is being man-in-the-middled. When the parent
    // detects an intercepted connection, the page may be reloaded with a new
    // error code (MOZILLA_PKIX_ERROR_MITM_DETECTED).
    const mitmPrimingEnabled = RPMGetBoolPref(
      "security.certerrors.mitm.priming.enabled"
    );
    if (
      mitmPrimingEnabled &&
      this.errorConfig.errorCode == "SEC_ERROR_UNKNOWN_ISSUER" &&
      // Only do this check for top-level failures.
      window.parent == window
    ) {
      RPMSendAsyncMessage("Browser:PrimeMitm");
    }

    // We show an offline support page in case of a system-wide error,
    // when a user cannot connect to the internet and access the SUMO website.
    // For example, clock error, which causes certerrors across the web or
    // a security software conflict where the user is unable to connect
    // to the internet.
    // The URL that prompts us to show an offline support page should have the following
    // format: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/supportPageSlug",
    // so we can extract the support page slug.
    let baseURL = RPMGetFormatURLPref("app.support.baseURL");
    if (document.location.href.startsWith(baseURL)) {
      let supportPageSlug = document.location.pathname.split("/").pop();
      RPMSendAsyncMessage("DisplayOfflineSupportPage", {
        supportPageSlug,
      });
    }

    if (getCSSClass() == "expertBadCert") {
      this.toggleAdvancedShowing();
    }

    this.checkAndRecordTRRTelemetry();
    this.checkForDomainSuggestions();

    // Eligibility rather than shouldShowSearchCTA(): a frame still asks, so its
    // decision is still recorded, and only the layout is suppressed.
    if (this.isSearchCTAEligible()) {
      this.searchCTADomain = this.hostname;
      this.searchCTAInfoPromise = this.requestSearchCTAInfo();
    }
  }

  // Check for alternate host for dnsNotFound errors.
  checkForDomainSuggestions() {
    if (this.resolvedErrorId === "dnsNotFound" && !this.isTRROnlyFailure()) {
      RPMCheckAlternateHostAvailable();
    }
  }

  isTRROnlyFailure() {
    return this.resolvedErrorId === "dnsNotFound" && RPMIsTRROnlyFailure();
  }

  // Whether this load is in scope for the search CTA at all. Use this one to
  // decide whether to ask the parent, and shouldShowSearchCTA() to decide
  // whether to draw anything. A frame asks but never draws (bug 2063091).
  isSearchCTAEligible() {
    return (
      SEARCH_CTA_ENABLED &&
      this.resolvedErrorId === "dnsNotFound" &&
      !gNoConnectivity &&
      !isCaptive() &&
      !this.isTRROnlyFailure()
    );
  }

  // Whether to draw the dnsNotFound Search CTA layout. The Search button needs
  // a default engine on top of this, which the parent answers later. Frames get
  // the standard error page instead (bug 2063091).
  shouldShowSearchCTA() {
    return this.isSearchCTAEligible() && window.parent == window;
  }

  // Whether the Search button itself will render, once the parent has answered.
  hasSearchCTAButton() {
    return (
      this.searchCTAResolved &&
      this.searchCTAHasEngine &&
      this.searchCTAAction !== SEARCH_CTA_ACTION_NONE
    );
  }

  async requestSearchCTAInfo() {
    const failedURL = searchParams.get("u");
    try {
      const info = await RPMSendQuery("SearchCTA:GetInfo", { url: failedURL });
      this.searchCTADomain = info.domain ?? this.hostname;
      this.searchCTAQuery = info.query ?? "";
      this.searchCTAAction = info.action ?? SEARCH_CTA_ACTION_NONE;
      this.searchCTAHasEngine = !!info.hasEngine;
    } catch (e) {
      // If the parent can't answer, fall back to a Reload-only page.
      this.searchCTAHasEngine = false;
    } finally {
      this.searchCTAResolved = true;
    }
  }

  async focusPrimaryButton() {
    if (this.shouldShowSearchCTA()) {
      await this.focusSearchCTAButton();
    } else {
      await this.focusTryAgainButton();
    }
  }

  // Focus the first button in the CTA layout, which is Search when it renders
  // and Reload otherwise. This waits for the parent to answer rather than
  // focusing Reload and moving focus once Search appears: one focus event, in
  // DOM order, so keyboard users are not left having to tab backwards to reach
  // the primary action. If the user has already moved focus while waiting,
  // leave it where they put it.
  async focusSearchCTAButton() {
    await this.searchCTAInfoPromise;
    await this.getUpdateComplete();

    if (window.top != window || this.shadowRoot.activeElement) {
      return;
    }

    const target = this.searchCTAButton ?? this.reloadButton;
    target?.focus();
  }

  handleSearchCTAClick() {
    // Connectivity can drop between render and click; re-check before searching
    // (bug 2055712). RPMHasConnectivity() is updated promptly by link-status
    // events. On a drop, abort the search and show an offline message instead.
    if (!RPMHasConnectivity()) {
      this.searchCTAOfflineAborted = true;
      RPMSendAsyncMessage("SearchCTA:SearchAborted");
      return;
    }
    this.ctaClicked = true;
    this.recordExitReason();
    RPMSendAsyncMessage("SearchCTA:Search", { query: this.searchCTAQuery });
  }

  handleReloadClick(e) {
    // Reload replaces Try Again on this page, so it records the same
    // click_try_again_button event and the retry signal stays continuous.
    this.handleTelemetryClick(e);
    this.reloadClicked = true;
    this.recordExitReason();
    retryThis(e.currentTarget);
  }

  // The "did you mean" suggestion link is injected into the shadow tree by
  // NetErrorChild; flag clicks on it as a distinct recovery path (bug 2055717).
  handleShadowClick(e) {
    if (e.target.closest?.("#dns-suggestion")) {
      this.suggestionClicked = true;
      this.recordExitReason();
    }
  }

  // Record, once, how the user left a CTA-eligible dnsNotFound page (bug
  // 2055717). Click-driven exits record at the click: clicking Search opens a
  // separate tab and leaves this page loaded, so waiting for pagehide would
  // delay the event indefinitely and lose it entirely if pagehide never fires
  // — biasing the efficacy signal against exactly the clicks that worked.
  // pagehide then only ever reports navigated-or-closed (navigation and
  // tab/window close are indistinguishable there, so they are merged).
  // exitRecorded keeps it to one event either way.
  recordExitReason() {
    if (this.exitRecorded || !this.shouldShowSearchCTA()) {
      return;
    }
    this.exitRecorded = true;

    let reason = "navigated-or-closed";
    if (this.ctaClicked) {
      reason = "clicked-cta";
    } else if (this.reloadClicked) {
      reason = "clicked-reload";
    } else if (this.suggestionClicked) {
      reason = "clicked-suggestion";
    }

    RPMRecordGleanEvent("securityUiNeterror", "searchCtaExit", {
      reason,
      cta_shown: this.hasSearchCTAButton(),
      // Without this, clicked-suggestion has no denominator: a low rate cannot
      // be told apart from the suggestion rarely being offered at all.
      suggestion_shown: !!this.dnsSuggestion,
    });
  }

  checkAndRecordTRRTelemetry() {
    if (!this.isTRROnlyFailure() || isCaptive()) {
      return;
    }

    this.recordTRRLoadTelemetry();
    this.showTrrSettingsButton = true;
  }

  recordTRRLoadTelemetry() {
    const trrMode = RPMGetIntPref("network.trr.mode");
    const trrDomain = RPMGetTRRDomain();
    const skipReason = RPMGetTRRSkipReason();

    this.trrTelemetryData = {
      value: "TRROnlyFailure",
      mode: trrMode.toString(),
      provider_key: trrDomain,
      skip_reason: skipReason,
    };

    RPMRecordGleanEvent("securityDohNeterror", "loadDohwarning", {
      value: "TRROnlyFailure",
      mode: trrMode,
      provider_key: trrDomain,
      skip_reason: skipReason,
    });
  }

  handlePrefChangeDetected() {
    this.showPrefReset = true;
    this.focusPrefResetButton();
  }

  async focusTryAgainButton() {
    await this.getUpdateComplete();

    if (window.top != window) {
      return;
    }

    if (!this.tryAgainButton) {
      return;
    }

    await this.tryAgainButton.updateComplete;
    this.tryAgainButton.focus();
  }

  async focusPrefResetButton() {
    await this.getUpdateComplete();

    if (window.top != window) {
      return;
    }

    if (!this.prefResetButton) {
      return;
    }

    requestAnimationFrame(() => {
      this.prefResetButton.focus();
    });
  }

  handlePrefResetClick() {
    RPMSendAsyncMessage("Browser:ResetSSLPreferences");
  }

  prefResetContainerTemplate() {
    if (!this.showPrefReset) {
      return null;
    }

    return html`<div id="prefChangeContainer" class="button-container">
      <p data-l10n-id="neterror-pref-reset"></p>
      <moz-button
        id="prefResetButton"
        type="primary"
        data-l10n-id="neterror-pref-reset-button"
        @click=${this.handlePrefResetClick}
      ></moz-button>
    </div>`;
  }

  getErrorInfo() {
    try {
      return gIsCertError
        ? document.getFailedCertSecurityInfo()
        : document.getNetErrorInfo();
    } catch {
      return { errorCodeString: "" };
    }
  }

  getErrorConfig() {
    const id = this.resolvedErrorId;
    if (!id) {
      return {};
    }
    const errorConfig = getResolvedErrorConfig(id, {
      hostname: this.hostname,
      errorInfo: this.errorInfo,
      cssClass: getCSSClass(),
      domainMismatchNames: this.domainMismatchNames,
      mitmName: this.errorInfo?.issuerCommonName ?? "",
      offline: gOffline,
      filePath: getFilePath(),
      showOSXPermissionWarning:
        !gIsCertError && RPMShowOSXLocalNetworkPermissionWarning(),
    });

    if (errorConfig.checkClockSkew && gIsCertError) {
      const now = Date.now();
      if (detectClockSkew(this.errorInfo, now)) {
        this.showCustomNetErrorCard = true;
        return getResolvedErrorConfig("CLOCK_SKEW_ERROR", {
          hostname: this.hostname,
          now,
        });
      }
    }

    if (errorConfig.customNetError) {
      this.showCustomNetErrorCard = true;
    }

    if (gErrorCode === "nssFailure2") {
      const result = handleNSSFailure(() => this.handlePrefChangeDetected());
      if (result.versionError) {
        this.showTlsNotice = true;
      }
    }
    return errorConfig;
  }

  introContentTemplate() {
    const config = this.errorConfig;
    if (!config.introContent) {
      return null;
    }

    const elementId = "error-intro";

    if (Array.isArray(config.introContent)) {
      return html`<p id=${elementId}>
        ${config.introContent.map(
          ic =>
            html`<span
              data-l10n-id=${ic.dataL10nId}
              data-l10n-args=${ic.dataL10nArgs
                ? JSON.stringify(ic.dataL10nArgs)
                : null}
            ></span>`
        )}
      </p>`;
    }

    const { dataL10nId, dataL10nArgs } = config.introContent;

    if (config.errorCode === "NS_ERROR_BASIC_HTTP_AUTH_DISABLED") {
      return html`<p id=${elementId} data-l10n-id=${dataL10nId}></p>
        ${this.hideExceptionButton
          ? html`<p
              id="fp-http-auth-disabled-secure-connection-text"
              data-l10n-id="fp-neterror-http-auth-disabled-secure-connection"
            ></p> `
          : null} `;
    }

    // Handle HSTS certificate errors with additional explanation
    // For HSTS errors, we show additional explanation about why they can't bypass
    return html`<p
        id=${elementId}
        data-l10n-id=${dataL10nId}
        data-l10n-args=${dataL10nArgs ? JSON.stringify(dataL10nArgs) : null}
      ></p>
      ${gHasSts
        ? html`<p
            id="badStsCertExplanation"
            data-l10n-id="certerror-what-should-i-do-bad-sts-cert-explanation"
            data-l10n-args=${JSON.stringify({ hostname: this.hostname })}
          ></p>`
        : null} `;
  }

  advancedContainerTemplate() {
    if (!this.advancedShowing) {
      return null;
    }

    const config = this.errorConfig;
    if (!config?.advanced) {
      return null;
    }

    const content = this.advancedSectionTemplate(
      this.mapAdvancedConfigToParams(config.advanced)
    );

    return html`<div class="advanced-container">
      ${EXPERT_BAD_CERT
        ? null
        : html`<h2 data-l10n-id="fp-certerror-advanced-title"></h2>`}
      ${EXPERT_BAD_CERT ? this.certErrorCodeTemplate() : null} ${content}
    </div>`;
  }

  mapAdvancedConfigToParams(advancedConfig) {
    const params = {
      whyDangerousL10nId: advancedConfig.whyDangerous?.dataL10nId,
      whyDangerousL10nArgs: advancedConfig.whyDangerous?.dataL10nArgs,
      whatCanYouDoL10nId: advancedConfig.whatCanYouDo?.dataL10nId,
      whatCanYouDoL10nArgs: advancedConfig.whatCanYouDo?.dataL10nArgs,
      importantNote: advancedConfig.importantNote,
      learnMoreL10nId: advancedConfig.learnMore?.dataL10nId,
      learnMoreSupportPage: advancedConfig.learnMore?.supportPage,
      viewCert: advancedConfig.showViewCertificate,
      viewDateTime: advancedConfig.showDateTime,
    };

    // Inject hostname into args that need it
    if (params.whyDangerousL10nArgs) {
      if (params.whyDangerousL10nArgs.hostname === null) {
        params.whyDangerousL10nArgs = {
          ...params.whyDangerousL10nArgs,
          hostname: this.hostname,
        };
      }
      // Handle SSL_ERROR_BAD_CERT_DOMAIN's validHosts arg
      if (params.whyDangerousL10nArgs.validHosts === null) {
        params.whyDangerousL10nArgs = {
          ...params.whyDangerousL10nArgs,
          validHosts: this.domainMismatchNames ?? "",
        };
      }
    }

    // Handle whatCanYouDo date args
    if (params.whatCanYouDoL10nArgs?.date === null) {
      params.whatCanYouDoL10nArgs = {
        ...params.whatCanYouDoL10nArgs,
        date: Date.now(),
      };
    }

    return params;
  }

  getNSSErrorWhyDangerousL10nId(errorString) {
    return errorString.toLowerCase().replace(/_/g, "-");
  }

  certErrorCodeTemplate() {
    if (!this.errorConfig?.errorCode || !gIsCertError) {
      return null;
    }
    return html`<p>
      <a
        id="errorCode"
        data-l10n-id="fp-cert-error-code"
        data-l10n-name="error-code-link"
        data-telemetry-id="error_code_link"
        data-l10n-args='{"error": "${this.errorConfig.errorCode}"}'
        @click=${this.toggleCertErrorDebugInfoShowing}
        href="#certificateErrorDebugInformation"
      ></a>
    </p>`;
  }

  advancedSectionTemplate(params) {
    let {
      whyDangerousL10nId,
      whyDangerousL10nArgs,
      whatCanYouDoL10nId,
      whatCanYouDoL10nArgs,
      importantNote,
      learnMoreL10nId,
      learnMoreL10nArgs,
      learnMoreSupportPage,
      viewCert,
      viewDateTime,
    } = params;
    return html`<div>
        ${whyDangerousL10nId
          ? html`<h3 data-l10n-id="fp-certerror-why-site-dangerous"></h3>
              <p
                id="fp-why-site-dangerous"
                data-l10n-id=${whyDangerousL10nId}
                data-l10n-args=${JSON.stringify(whyDangerousL10nArgs)}
              ></p>`
          : null}
      </div>
      ${whatCanYouDoL10nId
        ? html`<div>
            <h3 data-l10n-id="fp-certerror-what-can-you-do"></h3>
            <p
              id="whatCanYouDo"
              data-l10n-id=${whatCanYouDoL10nId}
              data-l10n-args=${JSON.stringify(whatCanYouDoL10nArgs)}
            ></p>
          </div>`
        : null}
      ${importantNote ? html`<p data-l10n-id=${importantNote}></p>` : null}
      ${this.prefResetContainerTemplate()} ${this.tlsNoticeTemplate()}
      ${viewCert
        ? html`<p>
            <a
              id="viewCertificate"
              data-l10n-id="fp-certerror-view-certificate-link"
              href="javascript:void(0)"
            ></a>
          </p>`
        : null}
      ${learnMoreL10nId
        ? html`<p>
            <a
              is="moz-support-link"
              support-page=${learnMoreSupportPage}
              data-l10n-id=${learnMoreL10nId}
              data-l10n-args=${JSON.stringify(learnMoreL10nArgs)}
              data-telemetry-id="learn_more_link"
              id="error-learn-more-link"
              @click=${this.handleTelemetryClick}
            ></a>
          </p>`
        : null}
      ${EXPERT_BAD_CERT ? null : this.certErrorCodeTemplate()}
      ${this.errorConfig?.errorCode && !gIsCertError
        ? html`<p
            data-l10n-id="fp-cert-error-code"
            data-l10n-args='{"error": "${this.errorConfig.errorCode}"}'
          ></p>`
        : null}
      ${viewDateTime
        ? html`<p
            data-l10n-id="fp-datetime"
            data-l10n-args=${JSON.stringify({ datetime: Date.now() })}
          ></p>`
        : null}
      ${!this.hideExceptionButton
        ? html` <moz-button
            id="exception-button"
            data-l10n-id="fp-certerror-override-exception-button-2"
            data-l10n-attrs="accesskey"
            data-l10n-args=${JSON.stringify({ hostname: this.hostname })}
            data-telemetry-id="exception_button"
            @click=${this.handleProceedToUrlClick}
          ></moz-button>`
        : null} `;
  }

  tlsNoticeTemplate() {
    if (!this.showTlsNotice) {
      return null;
    }

    return html`<p
      id="tlsVersionNotice"
      data-l10n-id="cert-error-old-tls-version"
    ></p>`;
  }

  customNetErrorContainerTemplate() {
    if (!this.showCustomNetErrorCard) {
      return null;
    }

    const config = this.errorConfig;
    if (!config.customNetError) {
      // For errors with advanced sections but no custom net error section
      if (config.buttons?.showAdvanced) {
        const content = this.customNetErrorSectionTemplate({
          titleL10nId: config.bodyTitleL10nId || "fp-certerror-body-title",
          buttons: {
            goBack: config.buttons?.showGoBack && window.self === window.top,
            tryAgain: config.buttons?.showTryAgain,
          },
          useAdvancedSection: true,
        });

        return html`<div class="custom-net-error-card">${content}</div>`;
      }
      return null;
    }

    const customNetError = config.customNetError;
    const params = this.mapCustomNetErrorConfigToParams(customNetError, config);
    const content = this.customNetErrorSectionTemplate(params);

    return html`<div class="custom-net-error-card">${content}</div>`;
  }

  mapCustomNetErrorConfigToParams(customNetError, config) {
    const params = {
      titleL10nId: customNetError.titleL10nId,
      showResponseStatus: customNetError.showResponseStatus,
      whyDangerousL10nId: customNetError.whyDangerousL10nId,
      whyDangerousL10nArgs: customNetError.whyDangerousL10nArgs,
      whyDidThisHappenL10nId: customNetError.whyDidThisHappenL10nId,
      whyDidThisHappenL10nArgs: customNetError.whyDidThisHappenL10nArgs,
      whatCanYouDoL10nId: customNetError.whatCanYouDoL10nId,
      whatCanYouDoL10nArgs: customNetError.whatCanYouDoL10nArgs,
      whatCanYouDoItems: customNetError.whatCanYouDoItems,
      learnMoreL10nId: customNetError.learnMoreL10nId,
      learnMoreSupportPage: customNetError.learnMoreSupportPage,
      errorCode:
        this.errorInfo?.errorCodeString ||
        (customNetError.showErrorCode ? config.errorCode : null),
      buttons: {
        tryAgain: config.buttons?.showTryAgain,
        goBack: config.buttons?.showGoBack && window.self === window.top,
      },
      useAdvancedSection: config.buttons?.showAdvanced,
    };

    // Inject hostname into args that need it
    if (params.whatCanYouDoL10nArgs?.hostname === null) {
      params.whatCanYouDoL10nArgs = {
        ...params.whatCanYouDoL10nArgs,
        hostname: this.hostname,
      };
    }
    if (params.whyDidThisHappenL10nArgs?.hostname === null) {
      params.whyDidThisHappenL10nArgs = {
        ...params.whyDidThisHappenL10nArgs,
        hostname: this.hostname,
      };
    }

    return params;
  }

  returnButtonTemplate() {
    return html`<moz-button
      type="primary"
      data-l10n-id="fp-certerror-return-to-previous-page-recommended-button-2"
      data-l10n-attrs="accesskey"
      data-telemetry-id="return_button_adv"
      id="returnButton"
      @click=${this.handleGoBackClick}
    ></moz-button>`;
  }

  tryAgainButtonTemplate() {
    return html`<moz-button
      id="tryAgainButton"
      type="primary"
      data-l10n-id="neterror-try-again-button-2"
      data-l10n-attrs="accesskey"
      data-telemetry-id="try_again_button"
      @click=${this.handleTryAgain}
    ></moz-button>`;
  }

  searchCTATemplate() {
    return html`<h1
        id="error-title"
        data-l10n-id="neterror-search-cta-title"
      ></h1>
      <p
        id="error-intro"
        data-l10n-id="neterror-search-cta-intro"
        data-l10n-args=${JSON.stringify({ domain: this.searchCTADomain })}
      ></p>
      <div>
        <h2
          id="whatCanYouDo"
          data-l10n-id="neterror-search-cta-things-to-try"
        ></h2>
        <ul class="what-can-you-do-list">
          <li data-l10n-id="neterror-search-cta-hint-check-address"></li>
          ${this.searchCTAHintTemplate()}
        </ul>
      </div>
      <div class="search-cta-buttons">
        ${this.searchCTAButtonTemplate()}${this.reloadButtonTemplate()}
      </div>
      <p
        class="search-cta-error-code"
        data-l10n-id="neterror-search-cta-error-code"
        data-l10n-args=${JSON.stringify({ error: "dnsNotFound" })}
      ></p>
      <p class="search-cta-learn-more">
        <a
          is="moz-support-link"
          id="error-learn-more-link"
          support-page="server-not-found-connection-problem"
          data-l10n-id="neterror-search-cta-learn-more"
        ></a>
      </p>`;
  }

  // Name the exact query the Search button will run, so the user can see what
  // would be sent before choosing to send it. Falls back to generic wording
  // while the parent is still answering, and when no Search button will show.
  searchCTAHintTemplate() {
    if (!this.hasSearchCTAButton() || !this.searchCTAQuery) {
      return html`<li data-l10n-id="neterror-search-cta-hint-search"></li>`;
    }

    return html`<li
      data-l10n-id="neterror-search-cta-hint-search-query"
      data-l10n-args=${JSON.stringify({ query: this.searchCTAQuery })}
    ></li>`;
  }

  searchCTAButtonTemplate() {
    // Connectivity dropped when the button was clicked (bug 2055712): show an
    // announced offline message where the Search button was; Reload remains.
    if (this.searchCTAOfflineAborted) {
      return html`<p
        id="searchCTAOfflineMessage"
        class="search-cta-offline"
        role="alert"
        data-l10n-id="neterror-search-cta-offline"
      ></p>`;
    }

    if (!this.searchCTAResolved) {
      // The label is visible rather than screen-reader-only: it is the only
      // text equivalent for the spinner, which is decorative. loading.svg
      // swaps its rotating arrows for a static hourglass under
      // prefers-reduced-motion, so the busy state survives by shape.
      return html`<div class="search-cta-loading">
        <img
          class="search-cta-loading-icon"
          src="chrome://global/skin/icons/loading.svg"
          alt=""
        />
        <span data-l10n-id="neterror-search-cta-loading"></span>
      </div>`;
    }

    // No engine, or the query-derivation module rejected the host: keep the
    // page (with Reload) but render no Search button.
    if (!this.hasSearchCTAButton()) {
      return null;
    }

    return html`<moz-button
      id="searchCTAButton"
      type="primary"
      iconSrc="chrome://global/skin/icons/search-glass.svg"
      data-l10n-id="neterror-search-cta-search-button"
      data-l10n-attrs="accesskey"
      @click=${this.handleSearchCTAClick}
    ></moz-button>`;
  }

  reloadButtonTemplate() {
    return html`<moz-button
      id="reloadButton"
      iconSrc="chrome://global/skin/icons/reload.svg"
      data-l10n-id="neterror-search-cta-reload-button"
      data-l10n-attrs="accesskey"
      data-telemetry-id="try_again_button"
      @click=${this.handleReloadClick}
    ></moz-button>`;
  }

  customNetErrorSectionTemplate(params) {
    const {
      titleL10nId,
      showResponseStatus,
      whyDangerousL10nId,
      whyDangerousL10nArgs,
      whyDidThisHappenL10nId,
      whyDidThisHappenL10nArgs,
      whatCanYouDoL10nId,
      whatCanYouDoL10nArgs,
      whatCanYouDoItems,
      learnMoreL10nId,
      learnMoreSupportPage,
      errorCode,
      buttons = {},
      useAdvancedSection,
    } = params;

    const { goBack = false, tryAgain = false } = buttons;

    // Format the learn more link with base URL if it's a SUMO slug
    let learnMoreHref = learnMoreSupportPage;
    if (
      learnMoreSupportPage &&
      !learnMoreSupportPage.startsWith("http://") &&
      !learnMoreSupportPage.startsWith("https://")
    ) {
      const baseURL = RPMGetFormatURLPref("app.support.baseURL");
      learnMoreHref = baseURL + learnMoreSupportPage;
    }

    let whatCanYouDoSection = null;
    if (whatCanYouDoItems?.length) {
      whatCanYouDoSection = html`<div>
        <h3 data-l10n-id="fp-certerror-what-can-you-do"></h3>
        <ul class="what-can-you-do-list">
          ${whatCanYouDoItems.map(id => html`<li data-l10n-id=${id}></li>`)}
        </ul>
      </div>`;
    } else if (whatCanYouDoL10nId) {
      whatCanYouDoSection = html`<div>
        <h3 data-l10n-id="fp-certerror-what-can-you-do"></h3>
        <p
          id="whatCanYouDo"
          data-l10n-id=${whatCanYouDoL10nId}
          data-l10n-args=${JSON.stringify(whatCanYouDoL10nArgs)}
        ></p>
      </div>`;
    }

    const content = html`
      ${whyDangerousL10nId
        ? html`<div>
            <h3 data-l10n-id="fp-certerror-why-site-dangerous"></h3>
            <p
              data-l10n-id=${whyDangerousL10nId}
              data-l10n-args=${JSON.stringify(whyDangerousL10nArgs)}
            ></p>
          </div>`
        : null}
      ${whatCanYouDoSection}
      ${whyDidThisHappenL10nId
        ? html`<div>
            <h3 data-l10n-id="fp-certerror-what-can-you-do"></h3>
            <p
              data-l10n-id=${whyDidThisHappenL10nId}
              data-l10n-args=${JSON.stringify(whyDidThisHappenL10nArgs)}
            ></p>
          </div>`
        : null}
      ${learnMoreL10nId
        ? html`<p>
            <a
              href=${learnMoreHref}
              data-l10n-id=${learnMoreL10nId}
              data-telemetry-id="learn_more_link"
              id="error-learn-more-link"
              @click=${this.handleTelemetryClick}
              rel="noopener noreferrer"
              target="_blank"
            ></a>
          </p>`
        : null}
      ${errorCode
        ? html`<p
            data-l10n-id="fp-cert-error-code"
            data-l10n-args=${JSON.stringify({ error: errorCode })}
          ></p>`
        : null}
      ${tryAgain
        ? html`<moz-button-group>
            ${this.tryAgainButtonTemplate()}
            ${this.showTrrSettingsButton
              ? html`<moz-button
                  id="trrSettingsButton"
                  type="default"
                  data-l10n-id="neterror-settings-button"
                  data-telemetry-id="settings_button"
                  @click=${this.handleTRRSettingsClick}
                ></moz-button>`
              : null}
          </moz-button-group>`
        : null}
      ${goBack
        ? html`<moz-button-group
            >${this.returnButtonTemplate()}</moz-button-group
          >`
        : null}
    `;

    return html`<h1 id="error-title" data-l10n-id=${titleL10nId}></h1>
      ${this.introContentTemplate()}
      ${showResponseStatus && this.errorInfo?.responseStatus >= 400
        ? html`<p
            id="response-status-label"
            data-l10n-id="neterror-response-status-code"
            data-l10n-args=${JSON.stringify({
              responsestatus: this.errorInfo.responseStatus,
              responsestatustext: this.errorInfo.responseStatusText ?? "",
            })}
          ></p>`
        : null}
      ${useAdvancedSection
        ? html`<moz-button-group>
            ${goBack ? this.returnButtonTemplate() : null}
            ${tryAgain ? this.tryAgainButtonTemplate() : null}
            <moz-button
              id="advanced-button"
              data-l10n-id=${this.advancedShowing
                ? "fp-certerror-hide-advanced-button"
                : "fp-certerror-advanced-button"}
              data-telemetry-id="advanced_button"
              @click=${this.toggleAdvancedShowing}
            ></moz-button
          ></moz-button-group>`
        : content}
      ${useAdvancedSection ? this.advancedContainerTemplate() : null} `;
  }

  async getDomainMismatchNames() {
    if (this.domainMismatchNamesPromise) {
      return;
    }

    this.domainMismatchNamesPromise = getSubjectAltNames(this.errorInfo);
    let subjectAltNames = await this.domainMismatchNamesPromise;
    this.domainMismatchNames = subjectAltNames.join(", ");

    // Re-resolve errorConfig to display domain mismatch names
    if (this.errorConfig?.advanced?.requiresDomainMismatchNames) {
      this.errorConfig = this.getErrorConfig();
    }
  }

  async getCertificateErrorText() {
    if (this.certificateErrorTextPromise) {
      return;
    }

    this.certificateErrorTextPromise = getFailedCertificatesAsPEMString();
    this.certificateErrorText = await this.certificateErrorTextPromise;
  }

  certErrorDebugInfoTemplate() {
    if (!this.certErrorDebugInfoShowing) {
      return null;
    }

    if (!this.certificateErrorText) {
      this.getCertificateErrorText();
      return null;
    }

    return html`<div
      id="certificateErrorDebugInformation"
      class="advanced-panel"
    >
      <moz-button
        id="copyToClipboardTop"
        data-telemetry-id="clipboard_button_top"
        data-l10n-id="neterror-copy-to-clipboard-button"
        @click=${this.copyCertErrorTextToClipboard}
      ></moz-button>
      <div id="certificateErrorText">${this.certificateErrorText}</div>
      <moz-button
        id="copyToClipboardBot"
        data-telemetry-id="clipboard_button_bot"
        data-l10n-id="neterror-copy-to-clipboard-button"
        @click=${this.copyCertErrorTextToClipboard}
      ></moz-button>
    </div>`;
  }

  handleGoBackClick(e) {
    this.handleTelemetryClick(e);
    RPMSendAsyncMessage("Browser:SSLErrorGoBack");
  }

  handleProceedToUrlClick(e) {
    this.handleTelemetryClick(e);
    const isPermanent =
      !RPMIsWindowPrivate() &&
      RPMGetBoolPref("security.certerrors.permanentOverride");
    document.addCertException(!isPermanent).then(
      () => {
        location.reload();
      },
      () => {}
    );
  }

  handleTryAgain(e) {
    this.handleTelemetryClick(e);
    retryThis(e);
  }

  handleTRRSettingsClick(e) {
    this.handleTelemetryClick(e);
    RPMSendAsyncMessage("OpenTRRPreferences");
  }

  toggleAdvancedShowing(e) {
    if (e) {
      this.handleTelemetryClick(e);
    }

    this.advancedShowing = !this.advancedShowing;

    if (!this.advancedShowing) {
      return;
    }

    this.revealAdvancedContainer();
  }

  async revealAdvancedContainer() {
    await this.getUpdateComplete();

    // Toggling the advanced panel must ensure that the debugging
    // information panel is hidden as well, since it's opened by the
    // error code link in the advanced panel.
    this.certErrorDebugInfoShowing = false;

    if (!this.exceptionButton) {
      this.resetReveal = null;
      return;
    }

    // Reveal, but disabled (and grayed-out) for 3.0s.
    if (this.exceptionButton) {
      this.exceptionButton.disabled = true;
    }

    // -

    if (this.resetReveal) {
      this.resetReveal(); // Reset if previous is pending.
    }
    let wasReset = false;
    this.resetReveal = () => {
      wasReset = true;
    };

    // Wait for 10 frames to ensure that the warning text is rendered
    // and gets all the way to the screen for the user to read it.
    // This is only ~0.160s at 60Hz, so it's not too much extra time that we're
    // taking to ensure that we're caught up with rendering, on top of the
    // (by default) whole second(s) we're going to wait based on the
    // security.dialog_enable_delay pref.
    // The catching-up to rendering is the important part, not the
    // N-frame-delay here.
    for (let i = 0; i < 10; i++) {
      await new Promise(requestAnimationFrame);
    }

    // Wait another Nms (default: 1000) for the user to be very sure. (Sorry speed readers!)
    const securityDelayMs = RPMGetIntPref("security.dialog_enable_delay", 1000);
    await new Promise(go => setTimeout(go, securityDelayMs));

    if (wasReset || !this.advancedShowing) {
      this.resetReveal = null;
      return;
    }

    // Enable and un-gray-out.
    if (this.exceptionButton) {
      this.exceptionButton.disabled = false;
    }
  }

  async toggleCertErrorDebugInfoShowing(event) {
    this.handleTelemetryClick(event);
    event.preventDefault();

    this.certErrorDebugInfoShowing = !this.certErrorDebugInfoShowing;

    if (this.certErrorDebugInfoShowing) {
      await this.getUpdateComplete();
      this.copyButtonTop.scrollIntoView({
        block: "start",
        behavior: "smooth",
      });
      this.copyButtonTop.focus();
    }
  }

  copyCertErrorTextToClipboard(e) {
    this.handleTelemetryClick(e);
    navigator.clipboard.writeText(this.certificateErrorText);
  }

  handleTelemetryClick(event) {
    let target = event.originalTarget;
    if (!target.hasAttribute("data-telemetry-id")) {
      target = target.getRootNode().host;
    }
    let telemetryId = target.dataset.telemetryId;

    if (this.trrTelemetryData) {
      RPMRecordGleanEvent(
        "securityDohNeterror",
        "click" +
          telemetryId
            .split("_")
            .map(word => word[0].toUpperCase() + word.slice(1))
            .join(""),
        this.trrTelemetryData
      );
    } else {
      const category = gIsCertError
        ? "securityUiCerterror"
        : "securityUiNeterror";
      let errorInfo = this.errorInfo;
      if (!gIsCertError && !errorInfo.errorCodeString) {
        errorInfo = Object.assign({}, errorInfo, {
          errorCodeString: gErrorCode,
        });
      }
      void recordSecurityUITelemetry(
        category,
        "click" +
          telemetryId
            .split("_")
            .map(word => word[0].toUpperCase() + word.slice(1))
            .join(""),
        errorInfo
      );
    }
  }

  containerContentTemplate(title) {
    if (this.shouldShowSearchCTA()) {
      return this.searchCTATemplate();
    }
    if (this.showCustomNetErrorCard) {
      return this.customNetErrorContainerTemplate();
    }
    return html`<h1 id="error-title" data-l10n-id=${title}></h1>
      ${this.introContentTemplate()}
      <moz-button-group
        >${this.returnButtonTemplate()}${EXPERT_BAD_CERT
          ? null
          : html`<moz-button
              id="advanced-button"
              data-l10n-id=${this.advancedShowing
                ? "fp-certerror-hide-advanced-button"
                : "fp-certerror-advanced-button"}
              data-telemetry-id="advanced_button"
              @click=${this.toggleAdvancedShowing}
            ></moz-button>`}</moz-button-group
      >
      ${this.advancedContainerTemplate()} ${this.certErrorDebugInfoTemplate()}`;
  }

  render() {
    if (!this.errorInfo) {
      return null;
    }

    const { bodyTitleL10nId, image } = this.errorConfig;
    // The CTA invites the user to weigh up where to go next, so it shows the
    // security illustration rather than dnsNotFound's no-connection one.
    const {
      src,
      alt = "",
      className,
    } = this.shouldShowSearchCTA()
      ? NET_ERROR_ILLUSTRATIONS.securityError
      : (image ?? NET_ERROR_ILLUSTRATIONS.securityError);
    const title = bodyTitleL10nId ?? "fp-certerror-body-title";

    return html`<link
        rel="stylesheet"
        href="chrome://global/skin/aboutNetError.css"
      />
      <article
        class="felt-privacy-container"
        aria-labelledby="error-title"
        aria-describedby="error-intro whatCanYouDo"
      >
        <div class="img-container">
          <img src=${src} class=${ifDefined(className)} alt=${alt} />
        </div>
        <div class="container">${this.containerContentTemplate(title)}</div>
      </article>`;
  }
}
