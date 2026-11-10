/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const DEFAULT_NEW_REPORT_ENDPOINT = "https://webcompat.com/issues/new";
const MINIMUM_DESCRIPTION_LENGTH = 10;

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SafeBrowsing: "resource://gre/modules/SafeBrowsing.sys.mjs",
});

export class ViewState {
  #doc;
  #mainView;
  #detailsView;
  #previewView;
  #reportSentView;

  #reportURL;
  currentTabURL;
  currentTabWebcompatDetailsPromise;

  constructor(doc) {
    this.#doc = doc;
    this.#mainView = doc.documentGlobal.PanelMultiView.getViewNode(
      this.#doc,
      "report-broken-site-popup-mainView"
    );
    this.#detailsView = doc.documentGlobal.PanelMultiView.getViewNode(
      this.#doc,
      "report-broken-site-popup-detailsView"
    );
    this.#previewView = doc.documentGlobal.PanelMultiView.getViewNode(
      this.#doc,
      "report-broken-site-popup-previewView"
    );
    this.#reportSentView = doc.documentGlobal.PanelMultiView.getViewNode(
      this.#doc,
      "report-broken-site-popup-reportSentView"
    );
    ViewState.#cache.set(doc, this);
  }

  static #cache = new WeakMap();
  static get(doc) {
    return ViewState.#cache.get(doc) ?? new ViewState(doc);
  }

  get mainPanelview() {
    return this.#mainView;
  }

  get detailsPanelview() {
    return this.#detailsView;
  }

  get previewPanelview() {
    return this.#previewView;
  }

  get reportSentPanelview() {
    return this.#reportSentView;
  }

  /**
   * Convenience method to set a given Report Broken Site CSS variable.
   * We use these variables to easily show or hide or disable key elements.
   * For instance, no-screenshots, which is used to hide the screenshot UI
   * on both the details and preview panels when we somehow failed to take
   * a screenshot anyhow.
   *
   * @param {string} [cls] The CSS class to change.
   * @param {bool} [bool] `true` to set, `false` to unset.
   */
  #setCSSClass(cls, bool) {
    for (const view of [this.#mainView, this.#detailsView, this.#previewView]) {
      view.classList.toggle(cls, bool);
    }
  }

  /**
   * Helper to toggle the wrong-tab-info CSS class for the UI.
   * Adding the class hides tab-specific information from the report preview
   * panel, as well as hiding the screenshot and ETP toggles on the details
   * panel, and the favicon on the URL inputs. This is done if the user
   * changes the URL's origin, as that implies any tab-specific info is
   * invalid and should not be sent with the report.
   */
  #wrongTabInfo = false;
  get wrongTabInfo() {
    return this.#wrongTabInfo;
  }
  set wrongTabInfo(_value) {
    const value = Boolean(_value);
    this.#wrongTabInfo = value;
    this.#setCSSClass("wrong-tab-info", value);
  }

  /**
   * Helper to toggle the screenshots-disable CSS class for the UI.
   * Adding the class hides the screenshot and its toggle on the details and
   * preview panels. It is set when screenshots are disabled by pref.
   */
  #screenshotsDisabled = false;
  get screenshotsDisabled() {
    return this.#screenshotsDisabled;
  }
  set screenshotsDisabled(_value) {
    const value = Boolean(_value);
    this.#screenshotsDisabled = value;
    this.#setCSSClass("screenshots-disabled", value);
  }

  /**
   * Helper to toggle the screenshot-opt-out CSS class for the UI.
   * Adding the class hides the screenshot on the details and preview
   * panels (but not its toggle on the details panel). This is set when
   * the user toggles the screenshot option to ensure one is not sent.
   */
  #screenshotOptOut = false;
  get screenshotOptOut() {
    return this.#screenshotOptOut;
  }
  set screenshotOptOut(_value) {
    const value = Boolean(_value);
    this.#screenshotOptOut = value;
    this.#setCSSClass("screenshot-opt-out", value);
    this.screenshotToggle.pressed = !value;
  }

  /**
   * Helper to toggle the no-blocked-trackers CSS class for the UI.
   * Adding the class hides the blocked tracker info and its toggle on
   * the details and preview panels. This is used when the report has
   * no blocked tracker info to send anyway to keep the UI clean.
   */
  #noBlockedTrackers = false;
  get noBlockedTrackers() {
    return this.#noBlockedTrackers;
  }
  set noBlockedTrackers(_value) {
    const value = Boolean(_value);
    this.#noBlockedTrackers = !!value;
    this.#setCSSClass("no-blocked-trackers", value);
  }

  /**
   * Helper to toggle the blocked-trackers-opt-out CSS class for the UI.
   * Adding the class hides the blocked tracker info on the preview panel.
   * This is set if the user does not toggle the related option to indicate
   * their willingness to send this info with the report.
   */
  #blockedTrackersOptOut = false;
  get blockedTrackersOptOut() {
    return this.#blockedTrackersOptOut;
  }
  set blockedTrackersOptOut(_value) {
    const value = Boolean(_value);
    this.#blockedTrackersOptOut = value;
    this.#setCSSClass("blocked-trackers-opt-out", value);
    this.blockedTrackersToggle.pressed = !value;
  }

  get shouldSendBlockedTrackers() {
    return (
      !this.noBlockedTrackers &&
      !this.blockedTrackersOptOut &&
      this.blockedTrackersToggle.pressed
    );
  }

  get url() {
    return this.#reportURL + "";
  }

  #isURLValid = false;

  set url(spec) {
    const url = URL.parse(spec);
    this.#isURLValid = url !== null;

    if (url) {
      this.#reportURL = url;
      this.wrongTabInfo = url.hostname != this.currentTabURL.hostname;
    }
    for (const input of this.urlInputs) {
      input.url = spec;
    }

    this.updateProgressDisabledState();
  }

  resetURLToCurrentTab() {
    const { currentURI } = this.#doc.documentGlobal.gBrowser.selectedBrowser;
    this.url = this.currentTabURL = URL.fromURI(currentURI);
  }

  focusInput(view, input) {
    const panelview = this.#doc.documentGlobal.PanelView.forNode(view);
    panelview.selectedElement = input;
    panelview.focusSelectedElement(true);
    // Ignore the next mouse-move to prevent the focus from accidentally being
    // cleared immediately when the user clicks on "Something else" (see bz2040437).
    panelview.ignoreMouseMove = true;
    input.addEventListener("blur", () => (panelview.ignoreMouseMove = false), {
      once: true,
    });
  }

  lastBlurredURLInputSelection;

  focusFirstInvalidInputOnView({ target }) {
    const panelview = target.closest("panelview");
    const urlInput = panelview.querySelector("url-input");
    const description = panelview.querySelector("textarea");
    if (urlInput && !this.isURLValid) {
      this.focusInput(panelview, urlInput.input);
      if (this.lastBlurredURLInputSelection) {
        urlInput.input.setSelectionRange(...this.lastBlurredURLInputSelection);
      }
      return true;
    } else if (description && !this.isDescriptionValid) {
      this.focusInput(panelview, description);
      return true;
    }
    return false;
  }

  updateProgressDisabledState() {
    const { isURLValid, isDescriptionValid } = this;
    for (const btn of this.#mainView.querySelectorAll(".progression")) {
      btn.toggleAttribute("disabled", !isURLValid);
    }
    for (const view of [this.#detailsView, this.#previewView]) {
      for (const btn of view.querySelectorAll(".progression")) {
        btn.toggleAttribute("disabled", !isURLValid || !isDescriptionValid);
      }
    }
  }

  get descriptionTextArea() {
    return this.#detailsView.querySelector(
      "#report-broken-site-popup-description"
    );
  }

  get description() {
    return this.descriptionTextArea.value;
  }

  set description(value) {
    this.descriptionTextArea.value = value.trim();
  }

  get blockedTrackersToggle() {
    return this.#detailsView.querySelector(
      "#report-broken-site-popup-blocked-trackers-toggle"
    );
  }

  get screenshotToggle() {
    return this.#detailsView.querySelector(
      "#report-broken-site-popup-screenshot-toggle"
    );
  }

  set screenshot(dataURI) {
    this.#setCSSClass("no-screenshot", !dataURI);
    this.#detailsView.querySelector(
      "#report-broken-site-popup-screenshot"
    ).src = dataURI ?? "";
  }

  set detailsViewTitle(title) {
    this.#detailsView.setAttribute("title", title);
  }

  get detailsViewInstructions() {
    return this.#detailsView.querySelector(
      "#report-broken-site-details-instructions"
    );
  }

  get detailsViewDescriptionError() {
    return this.#detailsView.querySelector(
      "#report-broken-site-details-description-error"
    );
  }

  reset() {
    this.currentTabWebcompatDetailsPromise = undefined;
    this.lastBlurredURLInputSelection = undefined;

    this.wrongTabInfo = false;
    this.screenshot = "";
    this.noBlockedTrackers = true;
    this.screenshotOptOut = false;
    this.blockedTrackersOptOut = true;

    delete this.cachedPreviewData;

    this.description = "";
    this.reason = "";

    this.resetURLToCurrentTab();
  }

  get isURLValid() {
    return this.#isURLValid;
  }

  get descriptionIsOptional() {
    return this.reason != "other";
  }

  get isDescriptionValid() {
    const { value } = this.descriptionTextArea;
    if (value) {
      return value.trim().length >= MINIMUM_DESCRIPTION_LENGTH;
    }
    return this.descriptionIsOptional;
  }

  createElement(name) {
    return this.#doc.createElement(name);
  }

  get learnMoreLink() {
    return this.#mainView.querySelector(
      "#report-broken-site-popup-learn-more-link"
    );
  }

  get sendMoreInfoButton() {
    return this.#detailsView.querySelector(
      "#report-broken-site-popup-send-more-info-button"
    );
  }

  get reasonButtons() {
    return this.#mainView.querySelectorAll(".reason-button");
  }

  get urlInputs() {
    return [
      ...this.#mainView.querySelectorAll("url-input"),
      ...this.#detailsView.querySelectorAll("url-input"),
    ];
  }

  get cancelButtons() {
    return [
      ...this.#detailsView.querySelectorAll(".cancel-button"),
      ...this.#previewView.querySelectorAll(".cancel-button"),
    ];
  }

  get sendButtons() {
    return [
      this.#detailsView.querySelector("#report-broken-site-popup-send-button"),
      this.#previewView.querySelector(
        "#report-broken-site-popup-preview-send-button"
      ),
    ];
  }

  get mainView() {
    return this.#mainView;
  }

  get reportSentView() {
    return this.#reportSentView;
  }

  get okayButton() {
    return this.#reportSentView.querySelector(
      "#report-broken-site-popup-okay-button"
    );
  }

  get previewBox() {
    return this.#previewView.querySelector(
      "#report-broken-site-panel-preview-items"
    );
  }

  get previewButton() {
    return this.#detailsView.querySelector(
      "#report-broken-site-popup-preview-button"
    );
  }
}

export var ReportBrokenSite = new (class ReportBrokenSite {
  static WEBCOMPAT_REPORTER_CONFIG = {
    src: "desktop-reporter",
    utm_campaign: "report-broken-site",
    utm_source: "desktop-reporter",
  };

  static DATAREPORTING_PREF = "datareporting.healthreport.uploadEnabled";
  static REPORTER_ENABLED_PREF = "ui.new-webcompat-reporter.enabled";

  static SCREENSHOTS_ENABLED_PREF =
    "ui.new-webcompat-reporter.screenshots.enabled";
  static SHOW_SEND_MORE_INFO_PREF =
    "ui.new-webcompat-reporter.show-send-more-info-link";
  static NEW_REPORT_ENDPOINT_PREF =
    "ui.new-webcompat-reporter.new-report-endpoint";

  static MAIN_PANELVIEW_ID = "report-broken-site-popup-mainView";
  static SENT_PANELVIEW_ID = "report-broken-site-popup-reportSentView";
  static DETAILS_PANELVIEW_ID = "report-broken-site-popup-detailsView";
  static PREVIEW_PANELVIEW_ID = "report-broken-site-popup-previewView";

  get enabled() {
    return (
      Services.policies.isAllowed("feedbackCommands") &&
      Services.prefs.getBoolPref(ReportBrokenSite.DATAREPORTING_PREF, false) &&
      Services.prefs.getBoolPref(ReportBrokenSite.REPORTER_ENABLED_PREF, true)
    );
  }

  canReportURI(uri) {
    return uri && (uri.schemeIs("http") || uri.schemeIs("https"));
  }

  #recordGleanEvent(name, extra) {
    Glean.webcompatreporting[name].record(extra);
  }

  updateParentMenu(event) {
    // We need to make sure that the Report Broken Site menu item
    // is disabled if the tab's location changes to a non-reportable
    // one while the menu is open.
    const tabbrowser = event.target.documentGlobal.gBrowser;
    this.enableOrDisableMenuitems(tabbrowser.selectedBrowser);

    tabbrowser.addTabsProgressListener(this);
    event.target.addEventListener(
      "popuphidden",
      () => {
        tabbrowser.removeTabsProgressListener(this);
      },
      { once: true }
    );
  }

  #OBSERVED_PREFS = {
    [ReportBrokenSite.SCREENSHOTS_ENABLED_PREF]: "onScreenshotsPrefChanged",
    [ReportBrokenSite.SHOW_SEND_MORE_INFO_PREF]:
      "onShowSendMoreInfoPrefChanged",
  };

  #windows = new Set();

  #onNewWindow(win) {
    for (const pref of Object.keys(this.#OBSERVED_PREFS)) {
      if (!this.#windows.size) {
        Services.prefs.addObserver(pref, this);
      }
      this.observe(null, null, pref, [win]);
    }
    this.#windows.add(win);
  }

  #onWindowClosed(win) {
    this.#windows.delete(win);
    if (!this.#windows.size) {
      for (const pref of Object.keys(this.#OBSERVED_PREFS)) {
        Services.prefs.removeObserver(pref, this);
      }
    }
  }

  observe(_, __, pref, windows = this.#windows) {
    const prefValue = Services.prefs.getBoolPref(pref, false);
    const checkFn = this[this.#OBSERVED_PREFS[pref]];
    for (const { document } of windows) {
      const state = ViewState.get(document);
      checkFn(prefValue, state);
    }
  }

  onScreenshotsPrefChanged(prefValue, state) {
    state.screenshotsDisabled = !prefValue;
  }

  onShowSendMoreInfoPrefChanged(_, state) {
    // This pref is unset by default. In that default state, we condition our
    // behavior here based on release channel (technically update channel)
    // to exempt release and ESR from seeing this UI.
    let hidden;
    if (
      Services.prefs.prefHasUserValue(ReportBrokenSite.SHOW_SEND_MORE_INFO_PREF)
    ) {
      hidden = !Services.prefs.getBoolPref(
        ReportBrokenSite.SHOW_SEND_MORE_INFO_PREF,
        false
      );
    } else {
      // enable the send-more-info link on pre-release builds.
      hidden = ["release", "esr"].includes(AppConstants.MOZ_UPDATE_CHANNEL);
    }
    state.sendMoreInfoButton.hidden = hidden;
  }

  uninit(win) {
    this.#onWindowClosed(win);
  }

  /**
   * Loads the reportbrokensite custom element script
   * into a given window.
   *
   * Called on RequestBrokenSite.init for a new browser window.
   *
   * @param {Window} window
   */
  static REGISTER_CUSTOM_ELEMENTS_SCRIPT =
    "chrome://browser/content/reportbrokensite/components/register.js";

  static #hasCustomElements = new WeakSet();

  static #loadCustomElements(window) {
    if (ReportBrokenSite.#hasCustomElements.has(window)) {
      // Don't add the elements again for the same window.
      return;
    }
    Services.scriptloader.loadSubScriptWithOptions(
      ReportBrokenSite.REGISTER_CUSTOM_ELEMENTS_SCRIPT,
      {
        target: window,
      }
    );
    ReportBrokenSite.#hasCustomElements.add(window);
  }

  #descriptionErrorTextPromise;

  init(win) {
    // Called in browser-init.js via the category manager registration
    // in BrowserComponents.manifest
    ReportBrokenSite.#loadCustomElements(win);

    this.#onNewWindow(win);

    const { document } = win;

    const state = ViewState.get(document);

    document.l10n.setAttributes(
      state.detailsViewDescriptionError,
      "report-broken-site-panel-invalid-description-label",
      { minLength: MINIMUM_DESCRIPTION_LENGTH }
    );

    // use Promise.resolve to avoid leaking document until shutdown
    this.#descriptionErrorTextPromise ??= Promise.resolve(
      document.l10n
        .formatMessages([
          {
            id: "report-broken-site-panel-invalid-description-label",
            args: { minLength: MINIMUM_DESCRIPTION_LENGTH },
          },
        ])
        .then(result => result[0].value)
    );

    for (const id of ["menu_HelpPopup", "appMenu-popup"]) {
      document
        .getElementById(id)
        .addEventListener("popupshown", e => this.updateParentMenu(e));
    }

    // The panelview code will close on Escape, but if the focus is on our
    // URL input, we want to blur that instead of closing the panelview.
    document.addEventListener(
      "keydown",
      e => this.#resetURLInputsOrCloseOnEscapePress(e),
      true
    );

    for (const btn of state.reasonButtons) {
      btn.addEventListener("click", e => this.#reasonButtonHandler(e));
    }

    for (const sendButton of state.sendButtons) {
      sendButton.addEventListener("command", e => this.#sendButtonHandler(e));
    }

    state.sendMoreInfoButton.addEventListener("command", e =>
      this.#sendMoreInfoButtonHandler(e)
    );

    state.previewButton.addEventListener("command", e =>
      this.#previewButtonHandler(e)
    );

    state.learnMoreLink.addEventListener("click", e =>
      this.#learnMoreLinkHandler(e)
    );

    state.descriptionTextArea.addEventListener("input", e =>
      this.updateDescriptionValidity(e)
    );

    state.okayButton.addEventListener("command", ({ target }) => {
      target.documentGlobal.CustomizableUI.hidePanelForNode(target);
    });

    for (const btn of state.cancelButtons) {
      btn.addEventListener("command", e => this.#cancelButtonHandler(e));
    }

    state.blockedTrackersToggle.addEventListener("toggle", ({ target }) => {
      state.blockedTrackersOptOut = !target.pressed;
    });

    state.screenshotToggle.addEventListener("toggle", ({ target }) => {
      state.screenshotOptOut = !target.pressed;
    });

    for (const input of state.urlInputs) {
      input.addEventListener("input", e => this.#onURLEdited(e));
      input.addEventListener("reset", e => this.#onURLInputReset(e));
      input.addEventListener("change", e => this.#onURLInputChanged(e));
      input.addEventListener("blur", e => this.#saveURLInputSelectionRange(e));
    }

    state.mainPanelview.addEventListener("ViewShowing", e =>
      this.#onMainViewShowing(e)
    );
    state.mainPanelview.addEventListener("ViewShown", e => {
      this.#focusFirstInvalidInputOnView(e);
    });
    state.detailsPanelview.addEventListener(
      "ViewShown",
      () => {
        // We do this because ViewShowing events are not fired on the main panel
        // when a back button is clicked (unlike the other views).
        state.detailsPanelview
          .querySelector(".subviewbutton-back")
          .addEventListener("click", () => {
            state.updateProgressDisabledState();
          });
      },
      { once: true }
    );

    state.detailsPanelview.addEventListener("ViewShowing", e =>
      this.#onDetailsViewShowing(e)
    );
    state.detailsPanelview.addEventListener("ViewShown", e => {
      this.#focusFirstInvalidInputOnView(e);
    });
    state.reportSentPanelview.addEventListener("ViewShown", e =>
      this.#onReportSentViewShown(e)
    );

    win.document
      .getElementById("cmd_reportBrokenSite")
      .addEventListener("command", e => this.#onReportBrokenSiteHandler(e));
  }

  enableOrDisableMenuitems(selectedbrowser) {
    // Ensures that the various Report Broken Site menu items and
    // toolbar buttons are enabled/hidden when appropriate.

    const canReportUrl = this.canReportURI(selectedbrowser.currentURI);

    const { document } = selectedbrowser.documentGlobal;

    // Altering the disabled attribute on the command does not propagate
    // the change to the related menuitems (see bug 805653), so we change them all.
    const cmd = document.getElementById("cmd_reportBrokenSite");
    const allowedByPolicy = Services.policies.isAllowed("feedbackCommands");
    cmd.toggleAttribute("hidden", !allowedByPolicy);
    // Note that this element does not exist until the protections popup is actually opened.
    const prot = document.getElementById(
      "protections-popup-report-broken-site-button"
    );
    if (canReportUrl) {
      cmd.removeAttribute("disabled");
      prot?.removeAttribute("disabled");
    } else {
      cmd.setAttribute("disabled", "true");
      prot?.setAttribute("disabled", "true");
    }

    // Changes to the "hidden" and "disabled" state of the command aren't reliably
    // reflected on the main menu unless we open it twice, or do it manually.
    // (See bug 1864953).
    const mainmenuItem = document.getElementById("help_reportBrokenSite");
    if (mainmenuItem) {
      mainmenuItem.hidden = !allowedByPolicy;
      mainmenuItem.disabled = !canReportUrl;
    }
  }

  updateDescriptionValidity(event) {
    const { target } = event;
    const state = ViewState.get(target.documentGlobal.document);
    const { descriptionTextArea, isDescriptionValid } = state;
    this.#descriptionErrorTextPromise.then(errorText =>
      descriptionTextArea.setCustomValidity(isDescriptionValid ? "" : errorText)
    );
    state.updateProgressDisabledState();
    return isDescriptionValid;
  }

  #focusFirstInvalidInputOnView(event) {
    const state = ViewState.get(event.target.documentGlobal.document);
    state.focusFirstInvalidInputOnView(event);
  }

  #onReportBrokenSiteHandler(event) {
    if (this.enabled) {
      this.open(event);
    } else {
      const { documentGlobal } = event.target;
      const { document, gBrowser } = documentGlobal;
      const { selectedBrowser } = gBrowser;
      const state = ViewState.get(document);
      state.resetURLToCurrentTab();
      this.promiseWebCompatInfo(state, selectedBrowser);
      this.#openWebCompatTab(gBrowser)
        .catch(err => {
          console.error(
            "Report Broken Site: unexpected error opening tab to webcompat.com",
            err
          );
        })
        .finally(() => {
          state.reset();
        });
    }
  }

  handleParentMenuButtonCommand(button) {
    this.#onReportBrokenSiteHandler({
      target: button,
      sourceEvent: { target: button },
    });
  }

  #onURLInputReset(event) {
    event.preventDefault();
    const { document } = event.target.documentGlobal;
    const state = ViewState.get(document);
    state.url = state.currentTabURL;
  }

  #onURLEdited({ target }) {
    const state = ViewState.get(target.documentGlobal.document);
    state.url = target.input.value;
  }

  #onURLInputChanged({ target, detail }) {
    const state = ViewState.get(target.documentGlobal.document);
    state.url = detail.newValue;
  }

  #saveURLInputSelectionRange({ target }) {
    const state = ViewState.get(target.documentGlobal.document);
    state.lastBlurredURLInputSelection = [
      target.input.selectionStart,
      target.input.selectionEnd,
    ];
  }

  #resetURLInputsOrCloseOnEscapePress(event) {
    const { document } = event.target.documentGlobal;
    const { activeElement } = document;

    if (event.key != "Escape" || activeElement?.nodeName != "html:url-input") {
      return;
    }

    event.stopImmediatePropagation();

    const state = ViewState.get(document);
    state.url = state.currentTabURL;

    // We need to ask the URL input component to blur.
    activeElement.requestBlur();
  }

  async #reasonButtonHandler(event) {
    const { target } = event;
    const state = ViewState.get(target.documentGlobal.document);

    if (state.focusFirstInvalidInputOnView(event)) {
      return;
    }

    if (target.matches("#report-broken-site-popup-reason-deceptive")) {
      target.documentGlobal.CustomizableUI.hidePanelForNode(target);

      // Remove the query and hash to avoid including potentially sensitive data
      const url = URL.parse(state.url);
      url.search = url.hash = "";
      const safebrowsingUrl = lazy.SafeBrowsing.getReportURL("Phish", {
        uri: url.href,
      });

      target.documentGlobal.gBrowser.addTab(safebrowsingUrl, {
        inBackground: false,
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
      });

      return;
    }

    const reason = target.id?.replace("report-broken-site-popup-reason-", "");
    if (!reason) {
      return;
    }
    state.reason = reason;
    state.detailsViewTitle = target.textContent;

    state.detailsViewInstructions.setAttribute(
      "data-l10n-id",
      `report-broken-site-panel-instructions-other${state.descriptionIsOptional ? "-optional" : ""}`
    );

    const multiview = target.closest("panelmultiview");
    multiview.showSubView(ReportBrokenSite.DETAILS_PANELVIEW_ID, target);
  }

  async #sendButtonHandler(event) {
    const { target } = event;
    const state = ViewState.get(target.documentGlobal.document);

    if (state.focusFirstInvalidInputOnView(event)) {
      return;
    }

    for (const sendButton of state.sendButtons) {
      sendButton.disabled = true;
    }

    const multiview = target.closest("panelmultiview");
    this.#recordGleanEvent("send", {
      sent_with_blocked_trackers: state.shouldSendBlockedTrackers,
    });

    const { documentGlobal } = target;
    const { gBrowser } = documentGlobal;
    await this.#sendReportAsGleanPing(gBrowser.selectedBrowser, state);

    multiview.showSubView("report-broken-site-popup-reportSentView");
    state.reset();

    for (const sendButton of state.sendButtons) {
      sendButton.disabled = false;
    }
  }

  #learnMoreLinkHandler({ target }) {
    this.#recordGleanEvent("learnMore");
    target.documentGlobal.requestAnimationFrame(() => {
      target.documentGlobal.CustomizableUI.hidePanelForNode(target);
    });
  }

  async #sendMoreInfoButtonHandler(event) {
    const { target } = event;
    event.preventDefault();
    const tabbrowser = target.documentGlobal.gBrowser;
    this.#recordGleanEvent("sendMoreInfo");
    event.target.documentGlobal.CustomizableUI.hidePanelForNode(target);
    await this.#openWebCompatTab(tabbrowser);
  }

  #previewButtonHandler(event) {
    const { target } = event;
    const state = ViewState.get(target.documentGlobal.document);

    if (state.focusFirstInvalidInputOnView(event)) {
      return;
    }

    state.currentTabWebcompatDetailsPromise
      ?.catch(_ => {})
      .then(info => {
        this.generatePreviewMarkup(state, info);

        // Update the live data on the preview which the user can edit in the reporter.
        const { description, previewBox, reason, url } = state;
        if (state.cachedPreviewData) {
          state.cachedPreviewData.basic.description = description;
          state.cachedPreviewData.basic.reason = reason;
          state.cachedPreviewData.basic.url = url;
        }
        previewBox.querySelector(".preview-description > .value").innerText =
          JSON.stringify(description);
        previewBox.querySelector(".preview-reason > .value").innerText =
          JSON.stringify(reason);
        previewBox.querySelector(".preview-url > .value").innerText =
          JSON.stringify(url);

        const multiview = target.closest("panelmultiview");
        multiview.showSubView(ReportBrokenSite.PREVIEW_PANELVIEW_ID, target);
        this.#recordGleanEvent("previewed");
      });
  }

  #cancelButtonHandler({ target }) {
    const state = ViewState.get(target.documentGlobal.document);
    target.documentGlobal.CustomizableUI.hidePanelForNode(target);
    state.reset();
  }

  async #onMainViewShowing({ target }) {
    const { selectedBrowser } = target.documentGlobal.gBrowser;
    let source = "helpMenu";
    switch (target.closest("panelmultiview")?.id) {
      case "appMenu-multiView":
        source = "hamburgerMenu";
        break;
      case "protections-popup-multiView":
        source = "ETPShieldIconMenu";
        break;
    }

    let didReset = false;
    const state = ViewState.get(selectedBrowser.documentGlobal.document);
    const url = selectedBrowser.currentURI.spec;
    if (!state.isURLValid) {
      state.reset();
      didReset = true;
    } else if (url != state.currentTabURL) {
      state.reset();
      didReset = true;
    } else if (!state.currentTabURL) {
      state.resetURLToCurrentTab();
    }

    this.#recordGleanEvent("opened", { source });

    if (didReset || !state.currentTabWebcompatDetailsPromise) {
      this.promiseWebCompatInfo(state, selectedBrowser);
    }

    state.updateProgressDisabledState();
  }

  #onDetailsViewShowing(event) {
    const { target } = event;
    const panelview = target.closest("panelview");
    const state = ViewState.get(target.documentGlobal.document);
    if (!this.updateDescriptionValidity(event)) {
      panelview.addEventListener(
        "ViewShown",
        () => state.focusInput(target, state.descriptionTextArea),
        { once: true }
      );
    }
    state.updateProgressDisabledState();
  }

  #onReportSentViewShown({ target: { documentGlobal } }) {
    // Make sure the Okay button is focused when the report sent view pops up.
    const state = ViewState.get(documentGlobal.document);
    const panelview = documentGlobal.PanelView.forNode(
      state.reportSentPanelview
    );
    panelview.selectedElement = state.okayButton;
    panelview.focusSelectedElement();
  }

  promiseWebCompatInfo(state, selectedBrowser) {
    const actor = this.#getActor(selectedBrowser);
    state.currentTabWebcompatDetailsPromise = actor
      .getBrokenSiteReport()
      .then(info => {
        state.screenshot = info.tabInfo?.screenshot?.value;
        state.noBlockedTrackers =
          !info?.antitracking?.blockedOrigins?.value?.length;
        const faviconDataURL = info?.tabInfo?.favicon?.value ?? "";
        for (const input of state.urlInputs) {
          input.favicon = faviconDataURL;
        }
        return info;
      })
      ?.catch(err => {
        console.error(
          "Report Broken Site: unexpected error gathering info",
          err
        );
        state.screenshot = "";
        state.noBlockedTrackers = true;
        state.currentTabWebcompatDetailsPromise = undefined;
      });
  }

  cachePreviewData(state, brokenSiteReportData) {
    const { description, reason, url } = state;

    const previewData = Object.assign({
      basic: {
        description,
        reason,
        url,
      },
    });

    if (brokenSiteReportData) {
      for (const [category, values] of Object.entries(brokenSiteReportData)) {
        previewData[category] = Object.fromEntries(
          Object.entries(values)
            .filter(([_, { doNotPreview }]) => !doNotPreview)
            .map(([name, value]) => [name, value])
        );
      }

      // The screenshot is tabInfo, but we want to present it as the last item in the
      // basic section because that is the first section and is expanded by default.
      previewData.basic.screenshot = brokenSiteReportData.tabInfo.screenshot;
    }

    state.cachedPreviewData = previewData;
    return previewData;
  }

  generatePreviewMarkup(state, reportData) {
    // If we have already cached preview data, we have already generated the markup as well.
    if (state.cachedPreviewData) {
      return;
    }
    const previewData = this.cachePreviewData(state, reportData);
    const preview = state.previewBox;
    preview.innerHTML = "";
    for (const [name, values] of Object.entries(previewData)) {
      const details = state.createElement("details");
      details.className = `preview-${name}`;

      const summary = state.createElement("summary");
      summary.innerText = name;
      details.appendChild(summary);

      const info = state.createElement("div");
      // text-link so it gets focus, but without the weird behavior of data-captures-focus.
      info.className = "data text-link";
      for (const [k, v] of Object.entries(values)) {
        if (k == "isTabSpecific") {
          details.classList.add("tab-specific-data");
          continue;
        }
        const { value, isTabSpecific } = v;
        const div = state.createElement("div");
        div.classList.add("entry");
        div.classList.add(`preview-${k}`);
        if (isTabSpecific) {
          div.classList.add("tab-specific-data");
        }
        const span_name = state.createElement("span");
        const span_value = state.createElement("span");
        span_value.className = "value";
        span_name.innerText = `${k}:`;
        if (typeof value === "string" && value.startsWith("data:image/")) {
          const img = state.createElement("img");
          img.src = value;
          span_value.appendChild(img);
        } else {
          // Add some extra word-wrapping opportunities to the data by adding spaces,
          // so users don't have to horizontally scroll as much.
          span_value.innerText =
            JSON.stringify(value)?.replace(/[,:]/g, "$& ") ?? "";
        }
        div.append(span_name, span_value);
        info.appendChild(div);
      }
      details.appendChild(info);

      preview.appendChild(details);
    }
    const first = preview.querySelector("details");
    if (first) {
      first.setAttribute("open", "");
    }
  }

  #getActor(browser) {
    return browser.browsingContext.currentWindowGlobal.getActor(
      "ReportBrokenSite"
    );
  }

  async #loadTab(tabbrowser, url, triggeringPrincipal) {
    const tab = tabbrowser.addTab(url, {
      inBackground: false,
      triggeringPrincipal,
    });
    const expectedBrowser = tabbrowser.getBrowserForTab(tab);
    return new Promise(resolve => {
      const listener = {
        onLocationChange(browser, webProgress, request, uri) {
          if (
            browser == expectedBrowser &&
            uri.spec == url &&
            webProgress.isTopLevel
          ) {
            resolve(tab);
            tabbrowser.removeTabsProgressListener(listener);
          }
        },
      };
      tabbrowser.addTabsProgressListener(listener);
    });
  }

  async #openWebCompatTab(tabbrowser) {
    const { document } = tabbrowser.selectedBrowser.documentGlobal;
    const {
      description,
      reason,
      screenshotToggle,
      url,
      currentTabWebcompatDetailsPromise,
      shouldSendBlockedTrackers,
      wrongTabInfo,
    } = ViewState.get(document);
    const webcompatInfo = await currentTabWebcompatDetailsPromise;
    if (!screenshotToggle.pressed) {
      webcompatInfo.tabInfo.screenshot.value = undefined;
    }

    const endpointUrl =
      Services.prefs.getStringPref(
        ReportBrokenSite.NEW_REPORT_ENDPOINT_PREF,
        ""
      ) || DEFAULT_NEW_REPORT_ENDPOINT;

    const principal = Services.scriptSecurityManager.createNullPrincipal({});
    const tab = await this.#loadTab(tabbrowser, endpointUrl, principal);

    const actor = this.#getActor(tabbrowser.selectedBrowser);
    return actor
      .sendQuery(
        "SendDataToWebcompatCom",
        {
          reason,
          description,
          endpointUrl,
          reportUrl: url,
          reporterConfig: ReportBrokenSite.WEBCOMPAT_REPORTER_CONFIG,
          sendTabSpecificInfo: !wrongTabInfo,
          sendBlockedUrls: shouldSendBlockedTrackers,
          webcompatInfo,
        },
        tab.linkedBrowser
      )
      .catch(err => {
        console.error(
          "Report Broken Site: error opening tab to webcompat.com",
          err
        );
      });
  }

  #sendReportAsGleanPing(
    browser,
    {
      currentTabWebcompatDetailsPromise,
      description,
      reason,
      shouldSendBlockedTrackers,
      url,
      wrongTabInfo,
    }
  ) {
    return currentTabWebcompatDetailsPromise
      .catch(() => undefined)
      .then(details => {
        return this.#getActor(browser).sendBrokenSiteReport({
          details,
          description,
          reason,
          sendTabSpecificInfo: !wrongTabInfo,
          sendBlockedUrls: shouldSendBlockedTrackers,
          url,
        });
      });
  }

  open(event) {
    const { target } = event.sourceEvent;
    const { selectedBrowser } = target.documentGlobal.gBrowser;
    const { documentGlobal } = selectedBrowser;
    const { document } = documentGlobal;

    switch (target.id) {
      case "protections-popup-report-broken-site-button":
        document
          .getElementById("protections-popup-multiView")
          .showSubView(ReportBrokenSite.MAIN_PANELVIEW_ID);
        break;
      // Open the "Report Broken Site" menu from the "Help and Report" subview.
      case "appMenu_help_reportBrokenSite":
        documentGlobal.PanelUI.showSubView(
          ReportBrokenSite.MAIN_PANELVIEW_ID,
          target
        );
        break;
      // Open the "Report Broken Site" menu from the top menu bar (Help > Report
      // Broken Site). This has to be directly anchored to the app menu button
      // since it is not a subview of any other menu.
      case "help_reportBrokenSite": {
        // hide the hamburger menu first, as we overlap with it.
        const appMenuPopup = document.getElementById("appMenu-popup");
        appMenuPopup?.hidePopup();

        documentGlobal.PanelUI.showSubView(
          ReportBrokenSite.MAIN_PANELVIEW_ID,
          documentGlobal.PanelUI.menuButton
        );
        break;
      }
    }
  }
})();
