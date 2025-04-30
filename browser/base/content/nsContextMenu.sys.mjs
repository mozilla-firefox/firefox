/* -*- tab-width: 2; indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ts=2 sw=2 sts=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  DevToolsShim: "chrome://devtools-startup/content/DevToolsShim.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  GenAI: "resource:///modules/GenAI.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  LoginManagerContextMenu:
    "resource://gre/modules/LoginManagerContextMenu.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PlacesUIUtils: "resource:///modules/PlacesUIUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  WebsiteFilter: "resource:///modules/policies/WebsiteFilter.sys.mjs",
});

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

ChromeUtils.defineLazyGetter(lazy, "ReferrerInfo", () =>
  Components.Constructor(
    "@mozilla.org/referrer-info;1",
    "nsIReferrerInfo",
    "init"
  )
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "SCREENSHOT_BROWSER_COMPONENT",
  "screenshots.browser.component.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "TEXT_RECOGNITION_ENABLED",
  "dom.text-recognition.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "STRIP_ON_SHARE_ENABLED",
  "privacy.query_stripping.strip_on_share.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "STRIP_ON_SHARE_CAN_DISABLE",
  "privacy.query_stripping.strip_on_share.canDisable",
  false
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "QueryStringStripper",
  "@mozilla.org/url-query-string-stripper;1",
  "nsIURLQueryStringStripper"
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "clipboard",
  "@mozilla.org/widget/clipboardhelper;1",
  "nsIClipboardHelper"
);

const PASSWORD_FIELDNAME_HINTS = ["current-password", "new-password"];
const USERNAME_FIELDNAME_HINT = "username";

export class nsContextMenu {
  /**
   * A promise to retrieve the translations language pair
   * if the context menu was opened in a context relevant to
   * open the SelectTranslationsPanel.
   * @type {Promise<{sourceLanguage: string, targetLanguage: string}>}
   */
  #translationsLangPairPromise;

  constructor(aXulMenu, aIsShift) {
    this.window = aXulMenu.ownerGlobal;
    this.document = aXulMenu.ownerDocument;

    // Get contextual info.
    this.setContext();

    if (!this.shouldDisplay) {
      return;
    }

    const { gBrowser } = this.window;

    this.isContentSelected = !this.selectionInfo.docSelectionIsCollapsed;
    if (!aIsShift) {
      let tab =
        gBrowser && gBrowser.getTabForBrowser
          ? gBrowser.getTabForBrowser(this.browser)
          : undefined;

      let subject = {
        menu: aXulMenu,
        tab,
        timeStamp: this.timeStamp,
        isContentSelected: this.isContentSelected,
        inFrame: this.inFrame,
        isTextSelected: this.isTextSelected,
        onTextInput: this.onTextInput,
        onLink: this.onLink,
        onImage: this.onImage,
        onVideo: this.onVideo,
        onAudio: this.onAudio,
        onCanvas: this.onCanvas,
        onEditable: this.onEditable,
        onSpellcheckable: this.onSpellcheckable,
        onPassword: this.onPassword,
        passwordRevealed: this.passwordRevealed,
        srcUrl: this.originalMediaURL,
        frameUrl: this.contentData ? this.contentData.docLocation : undefined,
        pageUrl: this.browser ? this.browser.currentURI.spec : undefined,
        linkText: this.linkTextStr,
        linkUrl: this.linkURL,
        linkURI: this.linkURI,
        selectionText: this.isTextSelected
          ? this.selectionInfo.fullText
          : undefined,
        frameId: this.frameID,
        webExtBrowserType: this.webExtBrowserType,
        webExtContextData: this.contentData
          ? this.contentData.webExtContextData
          : undefined,
      };
      subject.wrappedJSObject = subject;
      Services.obs.notifyObservers(subject, "on-build-contextmenu");
    }

    this.viewFrameSourceElement = this.document.getElementById(
      "context-viewframesource"
    );
    this.ellipsis = "\u2026";
    try {
      this.ellipsis = Services.prefs.getComplexValue(
        "intl.ellipsis",
        Ci.nsIPrefLocalizedString
      ).data;
    } catch (e) {}

    // Reset after "on-build-contextmenu" notification in case selection was
    // changed during the notification.
    this.isContentSelected = !this.selectionInfo.docSelectionIsCollapsed;
    this.onPlainTextLink = false;

    // Initialize (disable/remove) menu items.
    this.initItems(aXulMenu);
  }

  setContext() {
    let context = Object.create(null);

    if (nsContextMenu.contentData) {
      this.contentData = nsContextMenu.contentData;
      context = this.contentData.context;
      nsContextMenu.contentData = null;
    }

    const { gBrowser } = this.window;

    this.shouldDisplay = context.shouldDisplay;
    this.timeStamp = context.timeStamp;

    // Assign what's _possibly_ needed from `context` sent by ContextMenuChild.sys.mjs
    // Keep this consistent with the similar code in ContextMenu's _setContext
    this.imageDescURL = context.imageDescURL;
    this.imageInfo = context.imageInfo;
    this.mediaURL = context.mediaURL || context.bgImageURL;
    this.originalMediaURL = context.originalMediaURL || this.mediaURL;
    this.webExtBrowserType = context.webExtBrowserType;

    this.canSpellCheck = context.canSpellCheck;
    this.hasBGImage = context.hasBGImage;
    this.hasMultipleBGImages = context.hasMultipleBGImages;
    this.isDesignMode = context.isDesignMode;
    this.inFrame = context.inFrame;
    this.inPDFViewer = context.inPDFViewer;
    this.inPDFEditor = context.inPDFEditor;
    this.inSrcdocFrame = context.inSrcdocFrame;
    this.inSyntheticDoc = context.inSyntheticDoc;
    this.inTabBrowser = context.inTabBrowser;
    this.inWebExtBrowser = context.inWebExtBrowser;

    this.link = context.link;
    this.linkDownload = context.linkDownload;
    this.linkProtocol = context.linkProtocol;
    this.linkTextStr = context.linkTextStr;
    this.linkURL = context.linkURL;
    this.linkURI = this.getLinkURI(); // can't send; regenerate

    this.onAudio = context.onAudio;
    this.onCanvas = context.onCanvas;
    this.onCompletedImage = context.onCompletedImage;
    this.onDRMMedia = context.onDRMMedia;
    this.onPiPVideo = context.onPiPVideo;
    this.onEditable = context.onEditable;
    this.onImage = context.onImage;
    this.onKeywordField = context.onKeywordField;
    this.onSearchField = context.onSearchField;
    this.onLink = context.onLink;
    this.onLoadedImage = context.onLoadedImage;
    this.onMailtoLink = context.onMailtoLink;
    this.onTelLink = context.onTelLink;
    this.onMozExtLink = context.onMozExtLink;
    this.onNumeric = context.onNumeric;
    this.onPassword = context.onPassword;
    this.passwordRevealed = context.passwordRevealed;
    this.onSaveableLink = context.onSaveableLink;
    this.onSpellcheckable = context.onSpellcheckable;
    this.onTextInput = context.onTextInput;
    this.onVideo = context.onVideo;

    this.pdfEditorStates = context.pdfEditorStates;

    this.target = context.target;
    this.targetIdentifier = context.targetIdentifier;

    this.principal = context.principal;
    this.storagePrincipal = context.storagePrincipal;
    this.frameID = context.frameID;
    this.frameOuterWindowID = context.frameOuterWindowID;
    this.frameBrowsingContext = BrowsingContext.get(
      context.frameBrowsingContextID
    );

    this.inSyntheticDoc = context.inSyntheticDoc;
    this.inAboutDevtoolsToolbox = context.inAboutDevtoolsToolbox;

    this.isSponsoredLink = context.isSponsoredLink;

    // Everything after this isn't sent directly from ContextMenu
    if (this.target) {
      this.ownerDoc = this.target.ownerDocument;
    }

    this.csp = lazy.E10SUtils.deserializeCSP(context.csp);

    if (this.contentData) {
      this.browser = this.contentData.browser;
      this.selectionInfo = this.contentData.selectionInfo;
      this.actor = this.contentData.actor;
    } else {
      const { SelectionUtils } = ChromeUtils.importESModule(
        "resource://gre/modules/SelectionUtils.sys.mjs"
      );

      this.browser = this.ownerDoc.defaultView.docShell.chromeEventHandler;
      this.selectionInfo = SelectionUtils.getSelectionDetails(
        this.browser.ownerGlobal
      );
      this.actor =
        this.browser.browsingContext.currentWindowGlobal.getActor(
          "ContextMenu"
        );
    }

    this.remoteType = this.actor.manager.domProcess.remoteType;

    this.selectedText = this.selectionInfo.text;
    this.isTextSelected = !!this.selectedText.length;
    this.webExtBrowserType = this.browser.getAttribute(
      "webextension-view-type"
    );
    this.inWebExtBrowser = !!this.webExtBrowserType;
    this.inTabBrowser =
      gBrowser && gBrowser.getTabForBrowser
        ? !!gBrowser.getTabForBrowser(this.browser)
        : false;

    let { InlineSpellCheckerUI } = this.window;
    if (context.shouldInitInlineSpellCheckerUINoChildren) {
      InlineSpellCheckerUI.initFromRemote(
        this.contentData.spellInfo,
        this.actor.manager
      );
    }

    if (this.contentData.spellInfo) {
      this.spellSuggestions = this.contentData.spellInfo.spellSuggestions;
    }

    if (context.shouldInitInlineSpellCheckerUIWithChildren) {
      InlineSpellCheckerUI.initFromRemote(
        this.contentData.spellInfo,
        this.actor.manager
      );
      let canSpell = InlineSpellCheckerUI.canSpellCheck && this.canSpellCheck;
      this.showItem("spell-check-enabled", canSpell);
    }

    this.hasTextFragments = context.hasTextFragments;
    this.textFragmentURL = null;
  } // setContext

  hiding(aXulMenu) {
    if (this.actor) {
      this.actor.hiding();
    }

    aXulMenu.showHideSeparators = null;

    this.contentData = null;
    this.window.InlineSpellCheckerUI.clearSuggestionsFromMenu();
    this.window.InlineSpellCheckerUI.clearDictionaryListFromMenu();
    this.window.InlineSpellCheckerUI.uninit();
    if (
      Cu.isESModuleLoaded(
        "resource://gre/modules/LoginManagerContextMenu.sys.mjs"
      )
    ) {
      lazy.LoginManagerContextMenu.clearLoginsFromMenu(this.document);
    }

    // This handler self-deletes, only run it if it is still there:
    if (this._onPopupHiding) {
      this._onPopupHiding();
    }
  }

  initItems(aXulMenu) {
    this.initOpenItems();
    this.initNavigationItems();
    this.initViewItems();
    this.initImageItems();
    this.initMiscItems();
    this.initPocketItems();
    this.initSpellingItems();
    this.initSaveItems();
    this.initSyncItems();
    this.initClipboardItems();
    this.initMediaPlayerItems();
    this.initLeaveDOMFullScreenItems();
    this.initPasswordManagerItems();
    this.initViewSourceItems();
    this.initScreenshotItem();
    this.initPasswordControlItems();
    this.initPDFItems();
    this.initTextFragmentItems();

    this.showHideSeparators(aXulMenu);
    if (!aXulMenu.showHideSeparators) {
      // Set the showHideSeparators function on the menu itself so that
      // the extension code (ext-menus.js) can call it after modifying
      // the menus.
      aXulMenu.showHideSeparators = () => {
        this.showHideSeparators(aXulMenu);
      };
    }
  }

  initPDFItems() {
    for (const id of [
      "context-pdfjs-undo",
      "context-pdfjs-redo",
      "context-sep-pdfjs-redo",
      "context-pdfjs-cut",
      "context-pdfjs-copy",
      "context-pdfjs-paste",
      "context-pdfjs-delete",
      "context-pdfjs-selectall",
      "context-sep-pdfjs-selectall",
    ]) {
      this.showItem(id, this.inPDFEditor);
    }

    this.showItem(
      "context-pdfjs-highlight-selection",
      this.pdfEditorStates?.hasSelectedText
    );

    if (!this.inPDFEditor) {
      return;
    }

    const {
      isEmpty,
      hasSomethingToUndo,
      hasSomethingToRedo,
      hasSelectedEditor,
    } = this.pdfEditorStates;

    const hasEmptyClipboard = !Services.clipboard.hasDataMatchingFlavors(
      ["application/pdfjs"],
      Ci.nsIClipboard.kGlobalClipboard
    );

    this.setItemAttr("context-pdfjs-undo", "disabled", !hasSomethingToUndo);
    this.setItemAttr("context-pdfjs-redo", "disabled", !hasSomethingToRedo);
    this.setItemAttr(
      "context-sep-pdfjs-redo",
      "disabled",
      !hasSomethingToUndo && !hasSomethingToRedo
    );
    this.setItemAttr(
      "context-pdfjs-cut",
      "disabled",
      isEmpty || !hasSelectedEditor
    );
    this.setItemAttr(
      "context-pdfjs-copy",
      "disabled",
      isEmpty || !hasSelectedEditor
    );
    this.setItemAttr("context-pdfjs-paste", "disabled", hasEmptyClipboard);
    this.setItemAttr(
      "context-pdfjs-delete",
      "disabled",
      isEmpty || !hasSelectedEditor
    );
    this.setItemAttr("context-pdfjs-selectall", "disabled", isEmpty);
    this.setItemAttr("context-sep-pdfjs-selectall", "disabled", isEmpty);
  }

  initTextFragmentItems() {
    const shouldShow =
      Services.prefs.getBoolPref(
        "dom.text_fragments.create_text_fragment.enabled",
        false
      ) &&
      lazy.STRIP_ON_SHARE_ENABLED &&
      !(this.inPDFViewer || this.inFrame || this.onEditable) &&
      this.isContentSelected;
    this.showItem("context-copy-link-to-highlight", shouldShow);
    this.showItem("context-copy-clean-link-to-highlight", shouldShow);

    // disables both options by default, while API tries to build a text fragment
    this.setItemAttr("context-copy-link-to-highlight", "disabled", true);
    this.setItemAttr("context-copy-clean-link-to-highlight", "disabled", true);

    // Only show remove option if there are text fragments on the page.
    this.showItem("context-sep-highlights", this.hasTextFragments);
    this.showItem("context-remove-all-highlights", this.hasTextFragments);
  }

  async getTextDirective() {
    if (
      !Services.prefs.getBoolPref(
        "dom.text_fragments.create_text_fragment.enabled",
        false
      )
    ) {
      return;
    }
    this.textFragmentURL = await this.actor.getTextDirective();

    // enable menu items when a text fragment can be built
    if (this.textFragmentURL) {
      this.setItemAttr("context-copy-link-to-highlight", "disabled", false);

      // only enables the clean link based on preference and canStripForShare()
      // this follows the same pattern as https://bugzilla.mozilla.org/show_bug.cgi?id=1895334
      let canNotStripTextFragmentParams =
        lazy.STRIP_ON_SHARE_CAN_DISABLE &&
        !this.#canStripParams(this.getLinkURI(this.textFragmentURL));

      this.setItemAttr(
        "context-copy-clean-link-to-highlight",
        "disabled",
        canNotStripTextFragmentParams
      );
    }
  }

  async removeAllTextFragments() {
    await this.actor.removeAllTextFragments();
  }

  copyLinkToHighlight(stripSiteTracking = false) {
    if (this.textFragmentURL) {
      if (stripSiteTracking) {
        const uri = this.getLinkURI(this.textFragmentURL);
        this.copyStrippedLink(uri);
      } else {
        this.copyLink(this.textFragmentURL);
      }
    }
  }

  initOpenItems() {
    var isMailtoInternal = false;
    if (this.onMailtoLink) {
      var mailtoHandler = Cc[
        "@mozilla.org/uriloader/external-protocol-service;1"
      ]
        .getService(Ci.nsIExternalProtocolService)
        .getProtocolHandlerInfo("mailto");
      isMailtoInternal =
        !mailtoHandler.alwaysAskBeforeHandling &&
        mailtoHandler.preferredAction == Ci.nsIHandlerInfo.useHelperApp &&
        mailtoHandler.preferredApplicationHandler instanceof
          Ci.nsIWebHandlerApp;
    }

    if (
      this.isTextSelected &&
      !this.onLink &&
      this.selectionInfo &&
      this.selectionInfo.linkURL
    ) {
      this.linkURL = this.selectionInfo.linkURL;
      this.linkURI = this.getLinkURI();

      this.linkTextStr = this.selectionInfo.linkText;
      this.onPlainTextLink = true;
    }

    let { window, document } = this;
    var inContainer = false;
    if (this.contentData.userContextId) {
      inContainer = true;
      var item = document.getElementById("context-openlinkincontainertab");

      item.setAttribute("data-usercontextid", this.contentData.userContextId);

      var label = lazy.ContextualIdentityService.getUserContextLabel(
        this.contentData.userContextId
      );

      document.l10n.setAttributes(
        item,
        "main-context-menu-open-link-in-container-tab",
        {
          containerName: label,
        }
      );
    }

    var shouldShow =
      this.onSaveableLink || isMailtoInternal || this.onPlainTextLink;
    var isWindowPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
    let showContainers =
      Services.prefs.getBoolPref("privacy.userContext.enabled") &&
      lazy.ContextualIdentityService.getPublicIdentities().length;
    this.showItem("context-openlink", shouldShow && !isWindowPrivate);
    this.showItem(
      "context-openlinkprivate",
      shouldShow && lazy.PrivateBrowsingUtils.enabled
    );
    this.showItem("context-openlinkintab", shouldShow && !inContainer);
    this.showItem("context-openlinkincontainertab", shouldShow && inContainer);
    this.showItem(
      "context-openlinkinusercontext-menu",
      shouldShow && !isWindowPrivate && showContainers
    );
    this.showItem("context-openlinkincurrent", this.onPlainTextLink);
  }

  initNavigationItems() {
    var shouldShow =
      !(
        this.isContentSelected ||
        this.onLink ||
        this.onImage ||
        this.onCanvas ||
        this.onVideo ||
        this.onAudio ||
        this.onTextInput
      ) && this.inTabBrowser;
    if (AppConstants.platform == "macosx") {
      for (let id of [
        "context-back",
        "context-forward",
        "context-reload",
        "context-stop",
        "context-sep-navigation",
      ]) {
        this.showItem(id, shouldShow);
      }
    } else {
      this.showItem("context-navigation", shouldShow);
    }

    let stopped =
      this.window.XULBrowserWindow.stopCommand.getAttribute("disabled") ==
      "true";

    let stopReloadItem = "";
    if (shouldShow) {
      stopReloadItem = stopped ? "reload" : "stop";
    }

    this.showItem("context-reload", stopReloadItem == "reload");
    this.showItem("context-stop", stopReloadItem == "stop");

    let { document } = this;
    let initBackForwardMenuItemTooltip = (menuItemId, l10nId, shortcutId) => {
      // On macOS regular menuitems are used and the shortcut isn't added
      if (AppConstants.platform == "macosx") {
        return;
      }

      let shortcut = document.getElementById(shortcutId);
      if (shortcut) {
        shortcut = lazy.ShortcutUtils.prettifyShortcut(shortcut);
      } else {
        // Sidebar doesn't have navigation buttons or shortcuts, but we still
        // want to format the menu item tooltip to remove "$shortcut" string.
        shortcut = "";
      }

      let menuItem = document.getElementById(menuItemId);
      document.l10n.setAttributes(menuItem, l10nId, { shortcut });
    };

    initBackForwardMenuItemTooltip(
      "context-back",
      "main-context-menu-back-2",
      "goBackKb"
    );

    initBackForwardMenuItemTooltip(
      "context-forward",
      "main-context-menu-forward-2",
      "goForwardKb"
    );
  }

  initLeaveDOMFullScreenItems() {
    // only show the option if the user is in DOM fullscreen
    var shouldShow = this.target.ownerDocument.fullscreen;
    this.showItem("context-leave-dom-fullscreen", shouldShow);
  }

  initSaveItems() {
    var shouldShow = !(
      this.onTextInput ||
      this.onLink ||
      this.isContentSelected ||
      this.onImage ||
      this.onCanvas ||
      this.onVideo ||
      this.onAudio
    );
    this.showItem("context-savepage", shouldShow);

    // Save link depends on whether we're in a link, or selected text matches valid URL pattern.
    this.showItem(
      "context-savelink",
      this.onSaveableLink || this.onPlainTextLink
    );
    if (
      (this.onSaveableLink || this.onPlainTextLink) &&
      Services.policies.status === Services.policies.ACTIVE
    ) {
      this.setItemAttr(
        "context-savelink",
        "disabled",
        !lazy.WebsiteFilter.isAllowed(this.linkURL)
      );
    }

    // Save video and audio don't rely on whether it has loaded or not.
    this.showItem("context-savevideo", this.onVideo);
    this.showItem("context-saveaudio", this.onAudio);
    this.showItem("context-video-saveimage", this.onVideo);
    this.setItemAttr("context-savevideo", "disabled", !this.mediaURL);
    this.setItemAttr("context-saveaudio", "disabled", !this.mediaURL);
    this.showItem("context-sendvideo", this.onVideo);
    this.showItem("context-sendaudio", this.onAudio);
    let mediaIsBlob = this.mediaURL.startsWith("blob:");
    this.setItemAttr(
      "context-sendvideo",
      "disabled",
      !this.mediaURL || mediaIsBlob
    );
    this.setItemAttr(
      "context-sendaudio",
      "disabled",
      !this.mediaURL || mediaIsBlob
    );

    if (
      Services.policies.status === Services.policies.ACTIVE &&
      !Services.policies.isAllowed("filepickers")
    ) {
      // When file pickers are disallowed by enterprise policy,
      // these items silently fail. So to avoid confusion, we
      // disable them.
      for (let item of [
        "context-savepage",
        "context-savelink",
        "context-savevideo",
        "context-saveaudio",
        "context-video-saveimage",
        "context-saveaudio",
      ]) {
        this.setItemAttr(item, "disabled", true);
      }
    }
  }

  initImageItems() {
    // Reload image depends on an image that's not fully loaded
    this.showItem(
      "context-reloadimage",
      this.onImage && !this.onCompletedImage
    );

    // View image depends on having an image that's not standalone
    // (or is in a frame), or a canvas. If this isn't an image, check
    // if there is a background image.
    let showViewImage =
      ((this.onImage && (!this.inSyntheticDoc || this.inFrame)) ||
        this.onCanvas) &&
      !this.inPDFViewer;
    let showBGImage =
      this.hasBGImage &&
      !this.hasMultipleBGImages &&
      !this.inSyntheticDoc &&
      !this.inPDFViewer &&
      !this.isContentSelected &&
      !this.onImage &&
      !this.onCanvas &&
      !this.onVideo &&
      !this.onAudio &&
      !this.onLink &&
      !this.onTextInput;
    this.showItem("context-viewimage", showViewImage || showBGImage);

    // Save image depends on having loaded its content.
    this.showItem(
      "context-saveimage",
      (this.onLoadedImage || this.onCanvas) && !this.inPDFEditor
    );

    if (Services.policies.status === Services.policies.ACTIVE) {
      // When file pickers are disallowed by enterprise policy,
      // this item silently fails. So to avoid confusion, we
      // disable it.
      this.setItemAttr(
        "context-saveimage",
        "disabled",
        !Services.policies.isAllowed("filepickers")
      );
    }

    // Copy image contents depends on whether we're on an image.
    // Note: the element doesn't exist on all platforms, but showItem() takes
    // care of that by itself.
    this.showItem("context-copyimage-contents", this.onImage);

    // Copy image location depends on whether we're on an image.
    this.showItem("context-copyimage", this.onImage || showBGImage);

    // Performing text recognition only works on images, and if the feature is enabled.
    this.showItem(
      "context-imagetext",
      this.onImage &&
        Services.appinfo.isTextRecognitionSupported &&
        lazy.TEXT_RECOGNITION_ENABLED
    );

    // Send media URL (but not for canvas, since it's a big data: URL)
    this.showItem("context-sendimage", this.onImage || showBGImage);

    // View Image Info defaults to false, user can enable
    var showViewImageInfo =
      this.onImage &&
      Services.prefs.getBoolPref("browser.menu.showViewImageInfo", false);

    this.showItem("context-viewimageinfo", showViewImageInfo);
    // The image info popup is broken for WebExtension popups, since the browser
    // is destroyed when the popup is closed.
    this.setItemAttr(
      "context-viewimageinfo",
      "disabled",
      this.webExtBrowserType === "popup"
    );
    // Open the link to more details about the image. Does not apply to
    // background images.
    this.showItem(
      "context-viewimagedesc",
      this.onImage && this.imageDescURL !== ""
    );

    // Set as Desktop background depends on whether an image was clicked on,
    // and only works if we have a shell service.
    var haveSetDesktopBackground = false;

    if (
      AppConstants.HAVE_SHELL_SERVICE &&
      Services.policies.isAllowed("setDesktopBackground")
    ) {
      // Only enable Set as Desktop Background if we can get the shell service.
      var shell = this.window.getShellService();
      if (shell) {
        haveSetDesktopBackground = shell.canSetDesktopBackground;
      }
    }

    this.showItem(
      "context-setDesktopBackground",
      haveSetDesktopBackground && this.onLoadedImage
    );

    if (haveSetDesktopBackground && this.onLoadedImage) {
      this.document.getElementById("context-setDesktopBackground").disabled =
        this.contentData.disableSetDesktopBackground;
    }
  }

  initViewItems() {
    // View source is always OK, unless in directory listing.
    this.showItem(
      "context-viewpartialsource-selection",
      !this.inAboutDevtoolsToolbox &&
        this.isContentSelected &&
        this.selectionInfo.isDocumentLevelSelection
    );

    this.showItem(
      "context-print-selection",
      !this.inAboutDevtoolsToolbox &&
        this.isContentSelected &&
        this.selectionInfo.isDocumentLevelSelection
    );

    var shouldShow = !(
      this.isContentSelected ||
      this.onImage ||
      this.onCanvas ||
      this.onVideo ||
      this.onAudio ||
      this.onLink ||
      this.onTextInput
    );

    var showInspect =
      this.inTabBrowser &&
      !this.inAboutDevtoolsToolbox &&
      Services.prefs.getBoolPref("devtools.inspector.enabled", true) &&
      !Services.prefs.getBoolPref("devtools.policy.disabled", false);

    var showInspectA11Y =
      showInspect &&
      Services.prefs.getBoolPref("devtools.accessibility.enabled", false) &&
      Services.prefs.getBoolPref("devtools.enabled", true) &&
      (Services.prefs.getBoolPref("devtools.everOpened", false) ||
        // Note: this is a legacy usecase, we will remove it in bug 1695257,
        // once existing users have had time to set devtools.everOpened
        // through normal use, and we've passed an ESR cycle (91).
        lazy.DevToolsShim.isDevToolsUser());

    this.showItem("context-viewsource", shouldShow);
    this.showItem("context-inspect", showInspect);

    this.showItem("context-inspect-a11y", showInspectA11Y);

    // View video depends on not having a standalone video.
    this.showItem(
      "context-viewvideo",
      this.onVideo && (!this.inSyntheticDoc || this.inFrame)
    );
    this.setItemAttr("context-viewvideo", "disabled", !this.mediaURL);
  }

  initMiscItems() {
    let { window, document } = this;
    // Use "Bookmark Link…" if on a link.
    let bookmarkPage = document.getElementById("context-bookmarkpage");
    this.showItem(
      bookmarkPage,
      !(
        this.isContentSelected ||
        this.onTextInput ||
        this.onLink ||
        this.onImage ||
        this.onVideo ||
        this.onAudio ||
        this.onCanvas ||
        this.inWebExtBrowser
      )
    );

    this.showItem(
      "context-bookmarklink",
      (this.onLink &&
        !this.onMailtoLink &&
        !this.onTelLink &&
        !this.onMozExtLink) ||
        this.onPlainTextLink
    );
    this.showItem("context-add-engine", this.shouldShowAddEngine());
    this.showItem("context-keywordfield", this.shouldShowAddKeyword());
    this.showItem("frame", this.inFrame);

    if (this.inFrame) {
      // To make it easier to debug the browser running with out-of-process iframes, we
      // display the process PID of the iframe in the context menu for the subframe.
      let frameOsPid =
        this.actor.manager.browsingContext.currentWindowGlobal.osPid;
      this.setItemAttr("context-frameOsPid", "label", "PID: " + frameOsPid);

      // We need to check if "Take Screenshot" should be displayed in the "This Frame"
      // context menu
      let shouldShowTakeScreenshotFrame = this.shouldShowTakeScreenshot();
      this.showItem(
        "context-take-frame-screenshot",
        shouldShowTakeScreenshotFrame
      );
      this.showItem(
        "context-sep-frame-screenshot",
        shouldShowTakeScreenshotFrame
      );
    }

    this.showAndFormatSearchContextItem();
    this.showTranslateSelectionItem();
    lazy.GenAI.buildAskChatMenu(
      document.getElementById("context-ask-chat"),
      this
    );

    // srcdoc cannot be opened separately due to concerns about web
    // content with about:srcdoc in location bar masquerading as trusted
    // chrome/addon content.
    // No need to also test for this.inFrame as this is checked in the parent
    // submenu.
    this.showItem("context-showonlythisframe", !this.inSrcdocFrame);
    this.showItem("context-openframeintab", !this.inSrcdocFrame);
    this.showItem("context-openframe", !this.inSrcdocFrame);
    this.showItem("context-bookmarkframe", !this.inSrcdocFrame);

    // Hide menu entries for images, show otherwise
    if (this.inFrame) {
      this.viewFrameSourceElement.hidden =
        !lazy.BrowserUtils.mimeTypeIsTextBased(
          this.target.ownerDocument.contentType
        );
    }

    // BiDi UI
    this.showItem(
      "context-bidi-text-direction-toggle",
      this.onTextInput && !this.onNumeric && window.top.gBidiUI
    );
    this.showItem(
      "context-bidi-page-direction-toggle",
      !this.onTextInput && window.top.gBidiUI
    );
  }

  initPocketItems() {
    const pocketEnabled = Services.prefs.getBoolPref(
      "extensions.pocket.enabled"
    );
    let showSaveCurrentPageToPocket = false;
    let showSaveLinkToPocket = false;

    // We can skip all this is Pocket is not enabled.
    if (pocketEnabled) {
      let targetURL, targetURI;
      // If the context menu is opened over a link, we target the link,
      // if not, we target the page.
      if (this.onLink) {
        targetURL = this.linkURL;
        // linkURI may be null if the URL is invalid.
        targetURI = this.linkURI;
      } else {
        targetURL = this.browser?.currentURI?.spec;
        targetURI = Services.io.newURI(targetURL);
      }

      const canPocket =
        targetURI?.schemeIs("http") ||
        targetURI?.schemeIs("https") ||
        (targetURI?.schemeIs("about") &&
          lazy.ReaderMode?.getOriginalUrl(targetURL));

      // If the target is valid, decide which menu item to enable.
      if (canPocket) {
        showSaveLinkToPocket = this.onLink;
        showSaveCurrentPageToPocket = !(
          this.onTextInput ||
          this.onLink ||
          this.isContentSelected ||
          this.onImage ||
          this.onCanvas ||
          this.onVideo ||
          this.onAudio
        );
      }
    }

    this.showItem("context-pocket", showSaveCurrentPageToPocket);
    this.showItem("context-savelinktopocket", showSaveLinkToPocket);
  }

  initSpellingItems() {
    let { document } = this;
    let { InlineSpellCheckerUI } = this.window;
    var canSpell =
      InlineSpellCheckerUI.canSpellCheck &&
      !InlineSpellCheckerUI.initialSpellCheckPending &&
      this.canSpellCheck;
    let showDictionaries = canSpell && InlineSpellCheckerUI.enabled;
    var onMisspelling = InlineSpellCheckerUI.overMisspelling;
    var showUndo = canSpell && InlineSpellCheckerUI.canUndo();
    this.showItem("spell-check-enabled", canSpell);
    document
      .getElementById("spell-check-enabled")
      .setAttribute("checked", canSpell && InlineSpellCheckerUI.enabled);

    this.showItem("spell-add-to-dictionary", onMisspelling);
    this.showItem("spell-undo-add-to-dictionary", showUndo);

    // suggestion list
    if (onMisspelling) {
      var suggestionsSeparator = document.getElementById(
        "spell-add-to-dictionary"
      );
      var numsug = InlineSpellCheckerUI.addSuggestionsToMenu(
        suggestionsSeparator.parentNode,
        suggestionsSeparator,
        this.spellSuggestions
      );
      this.showItem("spell-no-suggestions", numsug == 0);
    } else {
      this.showItem("spell-no-suggestions", false);
    }

    // dictionary list
    this.showItem("spell-dictionaries", showDictionaries);
    if (canSpell) {
      var dictMenu = document.getElementById("spell-dictionaries-menu");
      var dictSep = document.getElementById("spell-language-separator");
      InlineSpellCheckerUI.addDictionaryListToMenu(dictMenu, dictSep);
      this.showItem("spell-add-dictionaries-main", false);
    } else if (this.onSpellcheckable) {
      // when there is no spellchecker but we might be able to spellcheck
      // add the add to dictionaries item. This will ensure that people
      // with no dictionaries will be able to download them
      this.showItem("spell-add-dictionaries-main", showDictionaries);
    } else {
      this.showItem("spell-add-dictionaries-main", false);
    }
  }

  initClipboardItems() {
    // Copy depends on whether there is selected text.
    // Enabling this context menu item is now done through the global
    // command updating system
    // this.setItemAttr( "context-copy", "disabled", !this.isTextSelected() );
    this.window.goUpdateGlobalEditMenuItems();

    this.showItem("context-undo", this.onTextInput);
    this.showItem("context-redo", this.onTextInput);
    this.showItem("context-cut", this.onTextInput);
    this.showItem("context-copy", this.isContentSelected || this.onTextInput);
    this.showItem("context-paste", this.onTextInput);
    this.showItem("context-paste-no-formatting", this.isDesignMode);
    this.showItem("context-delete", this.onTextInput);
    this.showItem(
      "context-selectall",
      !(
        this.onLink ||
        this.onImage ||
        this.onVideo ||
        this.onAudio ||
        this.inSyntheticDoc ||
        this.inPDFEditor
      ) || this.isDesignMode
    );

    // XXX dr
    // ------
    // nsDocumentViewer.cpp has code to determine whether we're
    // on a link or an image. we really ought to be using that...

    // Copy email link depends on whether we're on an email link.
    this.showItem("context-copyemail", this.onMailtoLink);

    // Copy phone link depends on whether we're on a phone link.
    this.showItem("context-copyphone", this.onTelLink);

    // Copy link location depends on whether we're on a non-mailto link.
    this.showItem(
      "context-copylink",
      this.onLink && !this.onMailtoLink && !this.onTelLink
    );

    // Showing "Copy Clean link" depends on whether the strip-on-share feature is enabled
    // and the user is selecting a URL
    this.showItem(
      "context-stripOnShareLink",
      lazy.STRIP_ON_SHARE_ENABLED &&
        (this.onLink || this.onPlainTextLink) &&
        !this.onMailtoLink &&
        !this.onTelLink &&
        !this.onMozExtLink &&
        !this.isSecureAboutPage()
    );

    let canNotStrip =
      lazy.STRIP_ON_SHARE_CAN_DISABLE && !this.#canStripParams();

    this.setItemAttr("context-stripOnShareLink", "disabled", canNotStrip);

    let copyLinkSeparator = this.document.getElementById(
      "context-sep-copylink"
    );
    // Show "Copy Link", "Copy" and "Copy Clean Link" with no divider, and "copy link" and "Send link to Device" with no divider between.
    // Other cases will show a divider.
    copyLinkSeparator.toggleAttribute(
      "ensureHidden",
      this.onLink &&
        !this.onMailtoLink &&
        !this.onTelLink &&
        !this.onImage &&
        this.syncItemsShown
    );

    this.showItem("context-copyvideourl", this.onVideo);
    this.showItem("context-copyaudiourl", this.onAudio);
    this.setItemAttr("context-copyvideourl", "disabled", !this.mediaURL);
    this.setItemAttr("context-copyaudiourl", "disabled", !this.mediaURL);
  }

  initMediaPlayerItems() {
    var onMedia = this.onVideo || this.onAudio;
    // Several mutually exclusive items... play/pause, mute/unmute, show/hide
    this.showItem(
      "context-media-play",
      onMedia && (this.target.paused || this.target.ended)
    );
    this.showItem(
      "context-media-pause",
      onMedia && !this.target.paused && !this.target.ended
    );
    this.showItem("context-media-mute", onMedia && !this.target.muted);
    this.showItem("context-media-unmute", onMedia && this.target.muted);
    this.showItem(
      "context-media-playbackrate",
      onMedia && this.target.duration != Number.POSITIVE_INFINITY
    );
    this.showItem("context-media-loop", onMedia);
    this.showItem(
      "context-media-showcontrols",
      onMedia && !this.target.controls
    );
    this.showItem(
      "context-media-hidecontrols",
      this.target.controls &&
        (this.onVideo || (this.onAudio && !this.inSyntheticDoc))
    );
    this.showItem(
      "context-video-fullscreen",
      this.onVideo && !this.target.ownerDocument.fullscreen
    );
    {
      let shouldDisplay =
        Services.prefs.getBoolPref(
          "media.videocontrols.picture-in-picture.enabled"
        ) &&
        this.onVideo &&
        !this.target.ownerDocument.fullscreen &&
        this.target.readyState > 0;
      this.showItem("context-video-pictureinpicture", shouldDisplay);
    }
    this.showItem("context-media-eme-learnmore", this.onDRMMedia);

    // Disable them when there isn't a valid media source loaded.
    if (onMedia) {
      this.setItemAttr(
        "context-media-playbackrate-050x",
        "checked",
        this.target.playbackRate == 0.5
      );
      this.setItemAttr(
        "context-media-playbackrate-100x",
        "checked",
        this.target.playbackRate == 1.0
      );
      this.setItemAttr(
        "context-media-playbackrate-125x",
        "checked",
        this.target.playbackRate == 1.25
      );
      this.setItemAttr(
        "context-media-playbackrate-150x",
        "checked",
        this.target.playbackRate == 1.5
      );
      this.setItemAttr(
        "context-media-playbackrate-200x",
        "checked",
        this.target.playbackRate == 2.0
      );
      this.setItemAttr("context-media-loop", "checked", this.target.loop);
      var hasError =
        this.target.error != null ||
        this.target.networkState == this.target.NETWORK_NO_SOURCE;
      this.setItemAttr("context-media-play", "disabled", hasError);
      this.setItemAttr("context-media-pause", "disabled", hasError);
      this.setItemAttr("context-media-mute", "disabled", hasError);
      this.setItemAttr("context-media-unmute", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate-050x", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate-100x", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate-125x", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate-150x", "disabled", hasError);
      this.setItemAttr("context-media-playbackrate-200x", "disabled", hasError);
      this.setItemAttr("context-media-showcontrols", "disabled", hasError);
      this.setItemAttr("context-media-hidecontrols", "disabled", hasError);
      if (this.onVideo) {
        let canSaveSnapshot =
          !this.onDRMMedia &&
          this.target.readyState >= this.target.HAVE_CURRENT_DATA;
        this.setItemAttr(
          "context-video-saveimage",
          "disabled",
          !canSaveSnapshot
        );
        this.setItemAttr("context-video-fullscreen", "disabled", hasError);
        this.setItemAttr(
          "context-video-pictureinpicture",
          "checked",
          this.onPiPVideo
        );
        this.setItemAttr(
          "context-video-pictureinpicture",
          "disabled",
          !this.onPiPVideo && hasError
        );
      }
    }
  }

  initPasswordManagerItems() {
    let { document } = this;
    let showUseSavedLogin = false;
    let showGenerate = false;
    let showManage = false;
    let enableGeneration = Services.logins.isLoggedIn;
    try {
      // If we could not find a password field we don't want to
      // show the form fill, manage logins and the password generation items.
      if (!this.isLoginForm()) {
        return;
      }
      showManage = true;

      // Disable the fill option if the user hasn't unlocked with their primary password
      // or if the password field or target field are disabled.
      // XXX: Bug 1529025 to maybe respect signon.rememberSignons.
      let loginFillInfo = this.contentData?.loginFillInfo;
      let disableFill =
        !Services.logins.isLoggedIn ||
        loginFillInfo?.passwordField.disabled ||
        loginFillInfo?.activeField.disabled;
      this.setItemAttr("fill-login", "disabled", disableFill);

      let onPasswordLikeField = PASSWORD_FIELDNAME_HINTS.includes(
        loginFillInfo.activeField.fieldNameHint
      );

      // Set the correct label for the fill menu
      let fillMenu = document.getElementById("fill-login");
      document.l10n.setAttributes(
        fillMenu,
        "main-context-menu-use-saved-password"
      );

      let documentURI = this.contentData?.documentURIObject;
      let formOrigin = lazy.LoginHelper.getLoginOrigin(documentURI?.spec);
      let isGeneratedPasswordEnabled =
        lazy.LoginHelper.generationAvailable &&
        lazy.LoginHelper.generationEnabled;
      showGenerate =
        onPasswordLikeField &&
        isGeneratedPasswordEnabled &&
        Services.logins.getLoginSavingEnabled(formOrigin);

      if (disableFill) {
        showUseSavedLogin = true;

        // No need to update the submenu if the fill item is disabled.
        return;
      }

      // Update sub-menu items.
      let fragment = lazy.LoginManagerContextMenu.addLoginsToMenu(
        this.targetIdentifier,
        this.browser,
        formOrigin
      );

      if (!fragment) {
        return;
      }

      showUseSavedLogin = true;
      let popup = document.getElementById("fill-login-popup");
      popup.appendChild(fragment);
    } finally {
      const documentURI = this.contentData?.documentURIObject;
      const showRelay =
        this.contentData?.context.showRelay &&
        lazy.LoginHelper.getLoginOrigin(documentURI?.spec);

      this.showItem("fill-login", showUseSavedLogin);
      this.showItem("fill-login-generated-password", showGenerate);
      this.showItem("use-relay-mask", showRelay);
      this.showItem("manage-saved-logins", showManage);
      this.setItemAttr(
        "fill-login-generated-password",
        "disabled",
        !enableGeneration
      );
      this.setItemAttr(
        "passwordmgr-items-separator",
        "ensureHidden",
        showUseSavedLogin || showGenerate || showManage || showRelay
          ? null
          : true
      );
    }
  }

  initSyncItems() {
    this.syncItemsShown = this.window.gSync.updateContentContextMenu(this);
  }

  initViewSourceItems() {
    const getString = aName => {
      const { bundle } = this.window.gViewSourceUtils.getPageActor(
        this.browser
      );
      return bundle.GetStringFromName(aName);
    };
    const showViewSourceItem = (id, check, accesskey) => {
      const fullId = `context-viewsource-${id}`;
      this.showItem(fullId, onViewSource);
      if (!onViewSource) {
        return;
      }
      this.setItemAttr(fullId, "checked", check());
      this.setItemAttr(fullId, "label", getString(`context_${id}_label`));
      if (accesskey) {
        this.setItemAttr(
          fullId,
          "accesskey",
          getString(`context_${id}_accesskey`)
        );
      }
    };

    const onViewSource = this.browser.currentURI.schemeIs("view-source");

    showViewSourceItem("goToLine", () => false, true);
    showViewSourceItem("wrapLongLines", () =>
      Services.prefs.getBoolPref("view_source.wrap_long_lines", false)
    );
    showViewSourceItem("highlightSyntax", () =>
      Services.prefs.getBoolPref("view_source.syntax_highlight", false)
    );
  }

  // Iterate over the visible items on the menu and its submenus and
  // hide any duplicated separators next to each other.
  // The attribute "ensureHidden" will override this process and keep a particular separator hidden in special cases.
  showHideSeparators(aPopup) {
    let lastVisibleSeparator = null;
    let count = 0;
    for (let menuItem of aPopup.children) {
      // Skip any items that were added by the page menu.
      if (menuItem.hasAttribute("generateditemid")) {
        count++;
        continue;
      }

      if (menuItem.localName == "menuseparator") {
        // Individual separators can have the `ensureHidden` attribute added to avoid them
        // becoming visible. We also set `count` to 0 below because otherwise the
        // next separator would be made visible, with the same visual effect.
        if (!count || menuItem.hasAttribute("ensureHidden")) {
          menuItem.hidden = true;
        } else {
          menuItem.hidden = false;
          lastVisibleSeparator = menuItem;
        }

        count = 0;
      } else if (!menuItem.hidden) {
        if (menuItem.localName == "menu") {
          this.showHideSeparators(menuItem.menupopup);
        } else if (menuItem.localName == "menugroup") {
          this.showHideSeparators(menuItem);
        }
        count++;
      }
    }

    // If count is 0 yet lastVisibleSeparator is set, then there must be a separator
    // visible at the end of the menu, so hide it. Note that there could be more than
    // one but this isn't handled here.
    if (!count && lastVisibleSeparator) {
      lastVisibleSeparator.hidden = true;
    }
  }

  shouldShowTakeScreenshot() {
    let shouldShow =
      !this.window.gScreenshots.shouldScreenshotsButtonBeDisabled() &&
      this.inTabBrowser &&
      !this.onTextInput &&
      !this.onLink &&
      !this.onPlainTextLink &&
      !this.onAudio &&
      !this.onEditable &&
      !this.onPassword;

    return shouldShow;
  }

  initScreenshotItem() {
    let shouldShow = this.shouldShowTakeScreenshot() && !this.inFrame;

    this.showItem("context-sep-screenshots", shouldShow);
    this.showItem("context-take-screenshot", shouldShow);
  }

  initPasswordControlItems() {
    let shouldShow = this.onPassword;
    if (shouldShow) {
      let revealPassword = this.document.getElementById(
        "context-reveal-password"
      );
      if (this.passwordRevealed) {
        revealPassword.setAttribute("checked", "true");
      } else {
        revealPassword.removeAttribute("checked");
      }
    }
    this.showItem("context-reveal-password", shouldShow);
  }

  toggleRevealPassword() {
    this.actor.toggleRevealPassword(this.targetIdentifier);
  }

  openPasswordManager() {
    lazy.LoginHelper.openPasswordManager(this.window, {
      entryPoint: "Contextmenu",
    });
  }

  useRelayMask() {
    const documentURI = this.contentData?.documentURIObject;
    const aOrigin = lazy.LoginHelper.getLoginOrigin(documentURI?.spec);
    this.actor.useRelayMask(this.targetIdentifier, aOrigin);
  }

  useGeneratedPassword() {
    lazy.LoginManagerContextMenu.useGeneratedPassword(this.targetIdentifier);
  }

  isLoginForm() {
    let loginFillInfo = this.contentData?.loginFillInfo;
    let documentURI = this.contentData?.documentURIObject;

    // If we could not find a password field or this is not a username-only
    // form, then don't treat this as a login form.
    return (
      (loginFillInfo?.passwordField?.found ||
        loginFillInfo?.activeField.fieldNameHint == USERNAME_FIELDNAME_HINT) &&
      !documentURI?.schemeIs("about") &&
      this.browser.contentPrincipal.spec != "resource://pdf.js/web/viewer.html"
    );
  }

  inspectNode() {
    return lazy.DevToolsShim.inspectNode(
      this.window.gBrowser.selectedTab,
      this.targetIdentifier
    );
  }

  inspectA11Y() {
    return lazy.DevToolsShim.inspectA11Y(
      this.window.gBrowser.selectedTab,
      this.targetIdentifier
    );
  }

  _openLinkInParameters(extra) {
    let params = {
      charset: this.contentData.charSet,
      originPrincipal: this.principal,
      originStoragePrincipal: this.storagePrincipal,
      triggeringPrincipal: this.principal,
      triggeringRemoteType: this.remoteType,
      csp: this.csp,
      frameID: this.contentData.frameID,
      hasValidUserGestureActivation: true,
    };
    for (let p in extra) {
      params[p] = extra[p];
    }

    let referrerInfo = this.onLink
      ? this.contentData.linkReferrerInfo
      : this.contentData.referrerInfo;
    // If we want to change userContextId, we must be sure that we don't
    // propagate the referrer.
    if (
      ("userContextId" in params &&
        params.userContextId != this.contentData.userContextId) ||
      this.onPlainTextLink
    ) {
      referrerInfo = new lazy.ReferrerInfo(
        referrerInfo.referrerPolicy,
        false,
        referrerInfo.originalReferrer
      );
    }

    params.referrerInfo = referrerInfo;
    return params;
  }

  _getGlobalHistoryOptions() {
    if (this.isSponsoredLink) {
      return {
        globalHistoryOptions: { triggeringSponsoredURL: this.linkURL },
      };
    } else if (this.browser.hasAttribute("triggeringSponsoredURL")) {
      return {
        globalHistoryOptions: {
          triggeringSponsoredURL: this.browser.getAttribute(
            "triggeringSponsoredURL"
          ),
          triggeringSponsoredURLVisitTimeMS: this.browser.getAttribute(
            "triggeringSponsoredURLVisitTimeMS"
          ),
        },
      };
    }
    return {};
  }

  // Open linked-to URL in a new window.
  openLink() {
    const params = this._getGlobalHistoryOptions();

    this.window.openLinkIn(
      this.linkURL,
      "window",
      this._openLinkInParameters(params)
    );
  }

  // Open linked-to URL in a new private window.
  openLinkInPrivateWindow() {
    this.window.openLinkIn(
      this.linkURL,
      "window",
      this._openLinkInParameters({ private: true })
    );
  }

  // Open linked-to URL in a new tab.
  openLinkInTab(event) {
    let params = {
      userContextId: parseInt(event.target.getAttribute("data-usercontextid")),
      ...this._getGlobalHistoryOptions(),
    };

    this.window.openLinkIn(
      this.linkURL,
      "tab",
      this._openLinkInParameters(params)
    );
  }

  // open URL in current tab
  openLinkInCurrent() {
    this.window.openLinkIn(
      this.linkURL,
      "current",
      this._openLinkInParameters()
    );
  }

  // Open frame in a new tab.
  openFrameInTab() {
    this.window.openLinkIn(this.contentData.docLocation, "tab", {
      charset: this.contentData.charSet,
      triggeringPrincipal: this.browser.contentPrincipal,
      csp: this.browser.csp,
      referrerInfo: this.contentData.frameReferrerInfo,
    });
  }

  // Reload clicked-in frame.
  reloadFrame(aEvent) {
    let forceReload = aEvent.shiftKey;
    this.actor.reloadFrame(this.targetIdentifier, forceReload);
  }

  // Open clicked-in frame in its own window.
  openFrame() {
    this.window.openLinkIn(this.contentData.docLocation, "window", {
      charset: this.contentData.charSet,
      triggeringPrincipal: this.browser.contentPrincipal,
      csp: this.browser.csp,
      referrerInfo: this.contentData.frameReferrerInfo,
    });
  }

  // Open clicked-in frame in the same window.
  showOnlyThisFrame() {
    this.window.urlSecurityCheck(
      this.contentData.docLocation,
      this.browser.contentPrincipal,
      Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT
    );
    this.window.openWebLinkIn(this.contentData.docLocation, "current", {
      referrerInfo: this.contentData.frameReferrerInfo,
      triggeringPrincipal: this.browser.contentPrincipal,
    });
  }

  takeScreenshot() {
    if (lazy.SCREENSHOT_BROWSER_COMPONENT) {
      Services.obs.notifyObservers(
        this.window,
        "menuitem-screenshot",
        "ContextMenu"
      );
    } else {
      Services.obs.notifyObservers(
        null,
        "menuitem-screenshot-extension",
        "contextMenu"
      );
    }
  }

  pdfJSCmd(aName) {
    if (["cut", "copy", "paste"].includes(aName)) {
      const cmd = `cmd_${aName}`;
      this.document.commandDispatcher
        .getControllerForCommand(cmd)
        .doCommand(cmd);
      if (Cu.isInAutomation) {
        this.browser.sendMessageToActor(
          "PDFJS:Editing",
          { name: aName },
          "Pdfjs"
        );
      }
      return;
    }
    this.browser.sendMessageToActor("PDFJS:Editing", { name: aName }, "Pdfjs");
  }

  // View Partial Source
  viewPartialSource() {
    let { browser } = this;
    let openSelectionFn = () => {
      let tabBrowser = this.window.gBrowser;
      let relatedToCurrent = tabBrowser?.selectedBrowser === browser;
      const inNewWindow = !Services.prefs.getBoolPref("view_source.tab");
      // In the case of popups, we need to find a non-popup browser window.
      // We might also not have a tabBrowser reference (if this isn't in a
      // a tabbrowser scope) or might have a fake/stub tabbrowser reference
      // (in the sidebar). Deal with those cases:
      if (!tabBrowser || !tabBrowser.addTab || !this.window.toolbar.visible) {
        // This returns only non-popup browser windows by default.
        let browserWindow = lazy.BrowserWindowTracker.getTopWindow();
        tabBrowser = browserWindow.gBrowser;
      }

      let tab = tabBrowser.addTab("about:blank", {
        relatedToCurrent,
        inBackground: inNewWindow,
        skipAnimation: inNewWindow,
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
      const viewSourceBrowser = tabBrowser.getBrowserForTab(tab);
      if (inNewWindow) {
        tabBrowser.hideTab(tab);
        tabBrowser.replaceTabsWithWindow(tab);
      }
      return viewSourceBrowser;
    };

    this.window.gViewSourceUtils.viewPartialSourceInBrowser(
      this.actor.browsingContext,
      openSelectionFn
    );
  }

  // Open new "view source" window with the frame's URL.
  viewFrameSource() {
    this.window.BrowserCommands.viewSourceOfDocument({
      browser: this.browser,
      URL: this.contentData.docLocation,
      outerWindowID: this.frameOuterWindowID,
    });
  }

  viewInfo() {
    this.window.BrowserCommands.pageInfo(
      this.contentData.docLocation,
      null,
      null,
      null,
      this.browser
    );
  }

  viewImageInfo() {
    this.window.BrowserCommands.pageInfo(
      this.contentData.docLocation,
      "mediaTab",
      this.imageInfo,
      null,
      this.browser
    );
  }

  viewImageDesc(e) {
    this.window.urlSecurityCheck(
      this.imageDescURL,
      this.principal,
      Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT
    );
    this.window.openUILink(this.imageDescURL, e, {
      referrerInfo: this.contentData.referrerInfo,
      triggeringPrincipal: this.principal,
      triggeringRemoteType: this.remoteType,
      csp: this.csp,
    });
  }

  viewFrameInfo() {
    this.window.BrowserCommands.pageInfo(
      this.contentData.docLocation,
      null,
      null,
      this.actor.browsingContext,
      this.browser
    );
  }

  reloadImage() {
    this.window.urlSecurityCheck(
      this.mediaURL,
      this.principal,
      Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT
    );
    this.actor.reloadImage(this.targetIdentifier);
  }

  _canvasToBlobURL(targetIdentifier) {
    return this.actor.canvasToBlobURL(targetIdentifier);
  }

  // Change current window to the URL of the image, video, or audio.
  viewMedia(e) {
    let where = lazy.BrowserUtils.whereToOpenLink(e, false, false);
    if (where == "current") {
      where = "tab";
    }
    let referrerInfo = this.contentData.referrerInfo;
    let systemPrincipal = Services.scriptSecurityManager.getSystemPrincipal();
    if (this.onCanvas) {
      this._canvasToBlobURL(this.targetIdentifier).then(blobURL => {
        this.window.openLinkIn(blobURL, where, {
          referrerInfo,
          triggeringPrincipal: systemPrincipal,
        });
      }, console.error);
    } else {
      this.window.urlSecurityCheck(
        this.mediaURL,
        this.principal,
        Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT
      );

      // Default to opening in a new tab.
      this.window.openLinkIn(this.mediaURL, where, {
        referrerInfo,
        forceAllowDataURI: true,
        triggeringPrincipal: this.principal,
        triggeringRemoteType: this.remoteType,
        csp: this.csp,
      });
    }
  }

  saveVideoFrameAsImage() {
    let isPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(this.browser);

    let aName = "";
    if (this.mediaURL) {
      try {
        let uri = this.window.makeURI(this.mediaURL);
        let url = uri.QueryInterface(Ci.nsIURL);
        if (url.fileBaseName) {
          aName = decodeURI(url.fileBaseName) + ".jpg";
        }
      } catch (e) {}
    }
    if (!aName) {
      aName = "snapshot.jpg";
    }

    // Cache this because we fetch the data async
    let referrerInfo = this.contentData.referrerInfo;
    let cookieJarSettings = this.contentData.cookieJarSettings;

    this.actor.saveVideoFrameAsImage(this.targetIdentifier).then(dataURL => {
      // FIXME can we switch this to a blob URL?
      this.window.internalSave(
        dataURL,
        null, // originalURL
        null, // document
        aName,
        null, // content disposition
        "image/jpeg", // content type - keep in sync with ContextMenuChild!
        true, // bypass cache
        "SaveImageTitle",
        null, // chosen data
        referrerInfo,
        cookieJarSettings,
        null, // initiating doc
        false, // don't skip prompt for where to save
        null, // cache key
        isPrivate,
        this.principal
      );
    });
  }

  leaveDOMFullScreen() {
    this.document.exitFullscreen();
  }

  // Change current window to the URL of the background image.
  viewBGImage(e) {
    this.window.urlSecurityCheck(
      this.bgImageURL,
      this.principal,
      Ci.nsIScriptSecurityManager.DISALLOW_SCRIPT
    );

    this.window.openUILink(this.bgImageURL, e, {
      referrerInfo: this.contentData.referrerInfo,
      forceAllowDataURI: true,
      triggeringPrincipal: this.principal,
      triggeringRemoteType: this.remoteType,
      csp: this.csp,
    });
  }

  setDesktopBackground() {
    if (!Services.policies.isAllowed("setDesktopBackground")) {
      return;
    }

    this.actor
      .setAsDesktopBackground(this.targetIdentifier)
      .then(({ failed, dataURL, imageName }) => {
        if (failed) {
          return;
        }

        let image = this.document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "img"
        );
        image.src = dataURL;

        // Confirm since it's annoying if you hit this accidentally.
        const kDesktopBackgroundURL =
          "chrome://browser/content/setDesktopBackground.xhtml";

        if (AppConstants.platform == "macosx") {
          // On Mac, the Set Desktop Background window is not modal.
          // Don't open more than one Set Desktop Background window.
          let dbWin = Services.wm.getMostRecentWindow(
            "Shell:SetDesktopBackground"
          );
          if (dbWin) {
            dbWin.gSetBackground.init(image, imageName);
            dbWin.focus();
          } else {
            this.window.openDialog(
              kDesktopBackgroundURL,
              "",
              "centerscreen,chrome,dialog=no,dependent,resizable=no",
              image,
              imageName
            );
          }
        } else {
          // On non-Mac platforms, the Set Wallpaper dialog is modal.
          this.window.openDialog(
            kDesktopBackgroundURL,
            "",
            "centerscreen,chrome,dialog,modal,dependent",
            image,
            imageName
          );
        }
      });
  }

  // Save URL of clicked-on frame.
  saveFrame() {
    this.window.saveBrowser(this.browser, false, this.frameBrowsingContext);
  }

  // Helper function to wait for appropriate MIME-type headers and
  // then prompt the user with a file picker
  saveHelper(
    linkURL,
    linkText,
    dialogTitle,
    bypassCache,
    doc,
    referrerInfo,
    cookieJarSettings,
    windowID,
    linkDownload,
    isContentWindowPrivate
  ) {
    // canonical def in nsURILoader.h
    const NS_ERROR_SAVE_LINK_AS_TIMEOUT = 0x805d0020;

    // an object to proxy the data through to
    // nsIExternalHelperAppService.doContent, which will wait for the
    // appropriate MIME-type headers and then prompt the user with a
    // file picker
    function saveAsListener(principal, aWindow) {
      this._triggeringPrincipal = principal;
      this._window = aWindow;
    }
    saveAsListener.prototype = {
      extListener: null,

      onStartRequest: function saveLinkAs_onStartRequest(aRequest) {
        // if the timer fired, the error status will have been caused by that,
        // and we'll be restarting in onStopRequest, so no reason to notify
        // the user
        if (aRequest.status == NS_ERROR_SAVE_LINK_AS_TIMEOUT) {
          return;
        }

        timer.cancel();

        // some other error occured; notify the user...
        if (!Components.isSuccessCode(aRequest.status)) {
          try {
            const l10n = new Localization(["browser/downloads.ftl"], true);

            let msg = null;
            try {
              const channel = aRequest.QueryInterface(Ci.nsIChannel);
              const reason = channel.loadInfo.requestBlockingReason;
              if (
                reason == Ci.nsILoadInfo.BLOCKING_REASON_EXTENSION_WEBREQUEST
              ) {
                try {
                  const properties = channel.QueryInterface(Ci.nsIPropertyBag);
                  const id = properties.getProperty("cancelledByExtension");
                  msg = l10n.formatValueSync("downloads-error-blocked-by", {
                    extension: WebExtensionPolicy.getByID(id).name,
                  });
                } catch (err) {
                  // "cancelledByExtension" doesn't have to be available.
                  msg = l10n.formatValueSync("downloads-error-extension");
                }
              }
            } catch (ex) {}
            msg ??= l10n.formatValueSync("downloads-error-generic");

            const win = Services.wm.getOuterWindowWithId(windowID);
            const title = l10n.formatValueSync("downloads-error-alert-title");
            Services.prompt.alert(win, title, msg);
          } catch (ex) {}
          return;
        }

        let extHelperAppSvc = Cc[
          "@mozilla.org/uriloader/external-helper-app-service;1"
        ].getService(Ci.nsIExternalHelperAppService);
        let channel = aRequest.QueryInterface(Ci.nsIChannel);
        this.extListener = extHelperAppSvc.doContent(
          channel.contentType,
          aRequest,
          null,
          true,
          this._window
        );
        this.extListener.onStartRequest(aRequest);
      },

      onStopRequest: function saveLinkAs_onStopRequest(aRequest, aStatusCode) {
        if (aStatusCode == NS_ERROR_SAVE_LINK_AS_TIMEOUT) {
          // do it the old fashioned way, which will pick the best filename
          // it can without waiting.
          this._window.saveURL(
            linkURL,
            null,
            linkText,
            dialogTitle,
            bypassCache,
            false,
            referrerInfo,
            cookieJarSettings,
            doc,
            isContentWindowPrivate,
            this._triggeringPrincipal
          );
        }
        if (this.extListener) {
          this.extListener.onStopRequest(aRequest, aStatusCode);
        }
      },

      onDataAvailable: function saveLinkAs_onDataAvailable(
        aRequest,
        aInputStream,
        aOffset,
        aCount
      ) {
        this.extListener.onDataAvailable(
          aRequest,
          aInputStream,
          aOffset,
          aCount
        );
      },
    };

    function callbacks() {}
    callbacks.prototype = {
      getInterface: function sLA_callbacks_getInterface(aIID) {
        if (aIID.equals(Ci.nsIAuthPrompt) || aIID.equals(Ci.nsIAuthPrompt2)) {
          // If the channel demands authentication prompt, we must cancel it
          // because the save-as-timer would expire and cancel the channel
          // before we get credentials from user.  Both authentication dialog
          // and save as dialog would appear on the screen as we fall back to
          // the old fashioned way after the timeout.
          timer.cancel();
          channel.cancel(NS_ERROR_SAVE_LINK_AS_TIMEOUT);
        }
        throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
      },
    };

    // if it we don't have the headers after a short time, the user
    // won't have received any feedback from their click.  that's bad.  so
    // we give up waiting for the filename.
    function timerCallback() {}
    timerCallback.prototype = {
      notify: function sLA_timer_notify() {
        channel.cancel(NS_ERROR_SAVE_LINK_AS_TIMEOUT);
      },
    };

    // setting up a new channel for 'right click - save link as ...'
    var channel = lazy.NetUtil.newChannel({
      uri: this.window.makeURI(linkURL),
      loadingPrincipal: this.principal,
      contentPolicyType: Ci.nsIContentPolicy.TYPE_SAVEAS_DOWNLOAD,
      securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
    });

    if (linkDownload) {
      channel.contentDispositionFilename = linkDownload;
    }
    if (channel instanceof Ci.nsIPrivateBrowsingChannel) {
      let docIsPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(
        this.browser
      );
      channel.setPrivate(docIsPrivate);
    }
    channel.notificationCallbacks = new callbacks();

    let flags = Ci.nsIChannel.LOAD_CALL_CONTENT_SNIFFERS;

    if (bypassCache) {
      flags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
    }

    if (channel instanceof Ci.nsICachingChannel) {
      flags |= Ci.nsICachingChannel.LOAD_BYPASS_LOCAL_CACHE_IF_BUSY;
    }

    channel.loadFlags |= flags;

    if (channel instanceof Ci.nsIHttpChannel) {
      channel.referrerInfo = referrerInfo;
      if (channel instanceof Ci.nsIHttpChannelInternal) {
        channel.forceAllowThirdPartyCookie = true;
      }

      channel.loadInfo.cookieJarSettings = cookieJarSettings;
    }

    // fallback to the old way if we don't see the headers quickly
    var timeToWait = Services.prefs.getIntPref(
      "browser.download.saveLinkAsFilenameTimeout"
    );
    var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(
      new timerCallback(),
      timeToWait,
      timer.TYPE_ONE_SHOT
    );

    // kick off the channel with our proxy object as the listener
    channel.asyncOpen(new saveAsListener(this.principal, this.window));
  }

  // Save URL of clicked-on link.
  saveLink() {
    let referrerInfo = this.onLink
      ? this.contentData.linkReferrerInfo
      : this.contentData.referrerInfo;

    let isPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(this.browser);
    this.saveHelper(
      this.linkURL,
      this.linkTextStr,
      null,
      true,
      this.ownerDoc,
      referrerInfo,
      this.contentData.cookieJarSettings,
      this.frameOuterWindowID,
      this.linkDownload,
      isPrivate
    );
  }

  // Backwards-compatibility wrapper
  saveImage() {
    if (this.onCanvas || this.onImage) {
      this.saveMedia();
    }
  }

  // Save URL of the clicked upon image, video, or audio.
  saveMedia() {
    let doc = this.ownerDoc;
    let isPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(this.browser);
    let referrerInfo = this.contentData.referrerInfo;
    let cookieJarSettings = this.contentData.cookieJarSettings;
    if (this.onCanvas) {
      // Bypass cache, since it's a data: URL.
      this._canvasToBlobURL(this.targetIdentifier).then(blobURL => {
        this.window.internalSave(
          blobURL,
          null, // originalURL
          null, // document
          "canvas.png",
          null, // content disposition
          "image/png", // _canvasToBlobURL uses image/png by default.
          true, // bypass cache
          "SaveImageTitle",
          null, // chosen data
          referrerInfo,
          cookieJarSettings,
          null, // initiating doc
          false, // don't skip prompt for where to save
          null, // cache key
          isPrivate,
          this.document.nodePrincipal /* system, because blob: */
        );
      }, console.error);
    } else if (this.onImage) {
      this.window.urlSecurityCheck(this.mediaURL, this.principal);
      this.window.internalSave(
        this.mediaURL,
        null, // originalURL
        null, // document
        null, // file name; we'll take it from the URL
        this.contentData.contentDisposition,
        this.contentData.contentType,
        false, // do not bypass the cache
        "SaveImageTitle",
        null, // chosen data
        referrerInfo,
        cookieJarSettings,
        null, // initiating doc
        false, // don't skip prompt for where to save
        null, // cache key
        isPrivate,
        this.principal
      );
    } else if (this.onVideo || this.onAudio) {
      let defaultFileName = "";
      if (this.mediaURL.startsWith("data")) {
        // Use default file name "Untitled" for data URIs
        defaultFileName =
          this.window.ContentAreaUtils.stringBundle.GetStringFromName(
            "UntitledSaveFileName"
          );
      }

      var dialogTitle = this.onVideo ? "SaveVideoTitle" : "SaveAudioTitle";
      this.saveHelper(
        this.mediaURL,
        null,
        dialogTitle,
        false,
        doc,
        referrerInfo,
        cookieJarSettings,
        this.frameOuterWindowID,
        defaultFileName,
        isPrivate
      );
    }
  }

  // Backwards-compatibility wrapper
  sendImage() {
    if (this.onCanvas || this.onImage) {
      this.sendMedia();
    }
  }

  sendMedia() {
    this.window.MailIntegration.sendMessage(this.mediaURL, "");
  }

  // Generate email address and put it on clipboard.
  copyEmail() {
    // Copy the comma-separated list of email addresses only.
    // There are other ways of embedding email addresses in a mailto:
    // link, but such complex parsing is beyond us.
    var url = this.linkURL;
    var qmark = url.indexOf("?");
    var addresses;

    // 7 == length of "mailto:"
    addresses = qmark > 7 ? url.substring(7, qmark) : url.substr(7);

    // Let's try to unescape it using a character set
    // in case the address is not ASCII.
    try {
      addresses = Services.textToSubURI.unEscapeURIForUI(addresses);
    } catch (ex) {
      // Do nothing.
    }

    lazy.clipboard.copyString(
      addresses,
      this.actor.manager.browsingContext.currentWindowGlobal
    );
  }

  // Extract phone and put it on clipboard
  copyPhone() {
    // Copies the phone number only. We won't be doing any complex parsing
    var url = this.linkURL;
    var phone = url.substr(4);

    // Let's try to unescape it using a character set
    // in case the phone number is not ASCII.
    try {
      phone = Services.textToSubURI.unEscapeURIForUI(phone);
    } catch (ex) {
      // Do nothing.
    }

    lazy.clipboard.copyString(
      phone,
      this.actor.manager.browsingContext.currentWindowGlobal
    );
  }

  copyLink(url = this.linkURL) {
    // If we're in a view source tab, remove the view-source: prefix
    let linkURL = url.replace(/^view-source:/, "");
    lazy.clipboard.copyString(
      linkURL,
      this.actor.manager.browsingContext.currentWindowGlobal
    );
  }

  /**
   * Copies a stripped version of a URI to the clipboard.
   * 'Stripped' means that query parameters for tracking/ link decoration
   * that are known to us will be removed from the URI.
   */
  copyStrippedLink(uri = this.linkURI) {
    let strippedLinkURI = this.getStrippedLink(uri);
    let strippedLinkURL =
      Services.io.createExposableURI(strippedLinkURI)?.displaySpec;
    if (strippedLinkURL) {
      lazy.clipboard.copyString(
        strippedLinkURL,
        this.actor.manager.browsingContext.currentWindowGlobal
      );
    }
  }

  addKeywordForSearchField() {
    this.actor.getSearchFieldBookmarkData(this.targetIdentifier).then(data => {
      let title = this.window.gNavigatorBundle.getFormattedString(
        "addKeywordTitleAutoFill",
        [data.title]
      );
      lazy.PlacesUIUtils.showBookmarkDialog(
        {
          action: "add",
          type: "bookmark",
          uri: this.window.makeURI(data.spec),
          title,
          keyword: "",
          postData: data.postData,
          charSet: data.charset,
          hiddenRows: ["location", "tags"],
        },
        this.window
      );
    });
  }

  async addSearchFieldAsEngine() {
    let { url, formData, charset, method } =
      await this.actor.getSearchFieldEngineData(this.targetIdentifier);

    let { engineInfo } = await this.window.gDialogBox.open(
      "chrome://browser/content/search/addEngine.xhtml",
      {
        mode: "FORM",
        title: true,
        nameTemplate: Services.io.newURI(url).host,
      }
    );

    // If the user saved, engineInfo contains `name` and `alias`.
    // Otherwise, it's undefined.
    if (engineInfo) {
      let searchEngine = await Services.search.addUserEngine({
        name: engineInfo.name,
        alias: engineInfo.alias,
        url,
        formData,
        charset,
        method,
        icon: this.browser.mIconURL,
      });

      this.window.gURLBar.search("", { searchEngine });
    }
  }

  /**
   * Utilities
   */

  /**
   * Show/hide one item (specified via name or the item element itself).
   * If the element is not found, then this function finishes silently.
   *
   * @param {Element|String} aItemOrId The item element or the name of the element
   *                                   to show.
   * @param {Boolean} aShow Set to true to show the item, false to hide it.
   */
  showItem(aItemOrId, aShow) {
    var item =
      aItemOrId.constructor == String
        ? this.document.getElementById(aItemOrId)
        : aItemOrId;
    if (item) {
      item.hidden = !aShow;
    }
  }

  // Set given attribute of specified context-menu item.  If the
  // value is null, then it removes the attribute (which works
  // nicely for the disabled attribute).
  setItemAttr(aID, aAttr, aVal) {
    var elem = this.document.getElementById(aID);
    if (elem) {
      if (aVal == null) {
        // null indicates attr should be removed.
        elem.removeAttribute(aAttr);
      } else {
        // Set attr=val.
        elem.setAttribute(aAttr, aVal);
      }
    }
  }

  // Temporary workaround for DOM api not yet implemented by XUL nodes.
  cloneNode(aItem) {
    // Create another element like the one we're cloning.
    var node = this.document.createElement(aItem.tagName);

    // Copy attributes from argument item to the new one.
    var attrs = aItem.attributes;
    for (var i = 0; i < attrs.length; i++) {
      var attr = attrs.item(i);
      node.setAttribute(attr.nodeName, attr.nodeValue);
    }

    // Voila!
    return node;
  }

  getLinkURI(url = this.linkURL) {
    try {
      return this.window.makeURI(url);
    } catch (ex) {
      // e.g. empty URL string
    }

    return null;
  }

  /**
   * Strips any known query params from the link URI.
   * @returns {nsIURI|null} - the stripped version of the URI,
   * or the original URI if we could not strip any query parameter.
   *
   */
  getStrippedLink(uri = this.linkURI) {
    if (!uri) {
      return null;
    }
    let strippedLinkURI = null;
    try {
      strippedLinkURI = lazy.QueryStringStripper.stripForCopyOrShare(uri);
    } catch (e) {
      console.warn(`getStrippedLink: ${e.message}`);
      return uri;
    }

    // If nothing can be stripped, we return the original URI
    // so the feature can still be used.
    return strippedLinkURI ?? uri;
  }

  /**
   * Checks if there is a query parameter that can be stripped
   * @returns {Boolean}
   *
   */
  #canStripParams(uri = this.linkURI) {
    if (!uri) {
      return false;
    }
    try {
      return lazy.QueryStringStripper.canStripForShare(uri);
    } catch (e) {
      console.warn("canStripForShare failed!", e);
      return false;
    }
  }

  /**
   * Checks if a webpage is a secure interal webpage
   * @returns {Boolean}
   *
   */
  isSecureAboutPage() {
    let { currentURI } = this.browser;
    if (currentURI?.schemeIs("about")) {
      let module = lazy.E10SUtils.getAboutModule(currentURI);
      if (module) {
        let flags = module.getURIFlags(currentURI);
        return !!(flags & Ci.nsIAboutModule.IS_SECURE_CHROME_UI);
      }
    }
    return false;
  }

  // Kept for addon compat
  linkText() {
    return this.linkTextStr;
  }

  // Determines whether or not the separator with the specified ID should be
  // shown or not by determining if there are any non-hidden items between it
  // and the previous separator.
  shouldShowSeparator(aSeparatorID) {
    var separator = this.document.getElementById(aSeparatorID);
    if (separator) {
      var sibling = separator.previousSibling;
      while (sibling && sibling.localName != "menuseparator") {
        if (!sibling.hidden) {
          return true;
        }
        sibling = sibling.previousSibling;
      }
    }
    return false;
  }

  shouldShowAddKeyword() {
    return (
      this.onTextInput &&
      this.onKeywordField &&
      !this.isLoginForm() &&
      !Services.prefs.getBoolPref(
        "browser.urlbar.update2.engineAliasRefresh",
        false
      )
    );
  }

  shouldShowAddEngine() {
    return (
      this.onTextInput &&
      this.onSearchField &&
      !this.isLoginForm() &&
      Services.prefs.getBoolPref(
        "browser.urlbar.update2.engineAliasRefresh",
        false
      )
    );
  }

  addDictionaries() {
    var uri = Services.urlFormatter.formatURLPref(
      "browser.dictionaries.download.url"
    );

    var locale = "-";
    try {
      locale = Services.prefs.getComplexValue(
        "intl.accept_languages",
        Ci.nsIPrefLocalizedString
      ).data;
    } catch (e) {}

    var version = "-";
    try {
      version = Services.appinfo.version;
    } catch (e) {}

    uri = uri.replace(/%LOCALE%/, escape(locale)).replace(/%VERSION%/, version);

    var newWindowPref = Services.prefs.getIntPref(
      "browser.link.open_newwindow"
    );
    var where = newWindowPref == 3 ? "tab" : "window";

    this.window.openTrustedLinkIn(uri, where);
  }

  bookmarkThisPage() {
    this.window.top.PlacesCommandHook.bookmarkPage().catch(console.error);
  }

  bookmarkLink() {
    this.window.top.PlacesCommandHook.bookmarkLink(
      this.linkURL,
      this.linkTextStr
    ).catch(console.error);
  }

  addBookmarkForFrame() {
    let uri = this.contentData.documentURIObject;

    this.actor.getFrameTitle(this.targetIdentifier).then(title => {
      this.window.top.PlacesCommandHook.bookmarkLink(uri.spec, title).catch(
        console.error
      );
    });
  }

  savePageAs() {
    this.window.saveBrowser(this.browser);
  }

  printFrame() {
    this.window.PrintUtils.startPrintWindow(this.actor.browsingContext, {
      printFrameOnly: true,
    });
  }

  printSelection() {
    this.window.PrintUtils.startPrintWindow(this.actor.browsingContext, {
      printSelectionOnly: true,
    });
  }

  switchPageDirection() {
    this.window.gBrowser.selectedBrowser.sendMessageToActor(
      "SwitchDocumentDirection",
      {},
      "SwitchDocumentDirection",
      "roots"
    );
  }

  mediaCommand(command, data) {
    this.actor.mediaCommand(this.targetIdentifier, command, data);
  }

  copyMediaLocation() {
    lazy.clipboard.copyString(
      this.originalMediaURL,
      this.actor.manager.browsingContext.currentWindowGlobal
    );
  }

  getImageText() {
    let dialogBox = this.window.gBrowser.getTabDialogBox(this.browser);
    const imageTextResult = this.actor.getImageText(this.targetIdentifier);
    let timerId = Glean.textRecognition.apiPerformance.start();
    const { dialog } = dialogBox.open(
      "chrome://browser/content/textrecognition/textrecognition.html",
      {
        features: "resizable=no",
        modalType: Services.prompt.MODAL_TYPE_CONTENT,
      },
      imageTextResult,
      () => dialog.resizeVertically(),
      this.window.openLinkIn,
      timerId
    );
  }

  drmLearnMore(aEvent) {
    let drmInfoURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "drm-content";
    let dest = lazy.BrowserUtils.whereToOpenLink(aEvent);
    // Don't ever want this to open in the same tab as it'll unload the
    // DRM'd video, which is going to be a bad idea in most cases.
    if (dest == "current") {
      dest = "tab";
    }
    this.window.openTrustedLinkIn(drmInfoURL, dest);
  }

  /**
   * Opens the SelectTranslationsPanel singleton instance.
   *
   * @param {Event} event - The triggering event for opening the panel.
   */
  openSelectTranslationsPanel(event) {
    const context = this.contentData.context;
    let screenX = context.screenXDevPx / this.window.devicePixelRatio;
    let screenY = context.screenYDevPx / this.window.devicePixelRatio;
    this.window.SelectTranslationsPanel.open(
      event,
      screenX,
      screenY,
      this.#getTextToTranslate(),
      this.isTextSelected,
      this.#translationsLangPairPromise
    ).catch(console.error);
  }

  /**
   * Localizes the translate-selection menuitem.
   *
   * The item will either be localized with a target language's display name
   * or localized in a generic way without a target language.
   *
   * @param {Element} translateSelectionItem
   * @returns {Promise<void>}
   */
  async localizeTranslateSelectionItem(translateSelectionItem) {
    const { targetLanguage } = await this.#translationsLangPairPromise;

    if (targetLanguage) {
      // A valid to-language exists, so localize the menuitem for that language.
      let displayName;

      try {
        const languageDisplayNames =
          lazy.TranslationsParent.createLanguageDisplayNames();
        displayName = languageDisplayNames.of(targetLanguage);
      } catch {
        // languageDisplayNames.of threw, do nothing.
      }

      if (displayName) {
        translateSelectionItem.setAttribute("target-language", targetLanguage);
        this.document.l10n.setAttributes(
          translateSelectionItem,
          this.isTextSelected
            ? "main-context-menu-translate-selection-to-language"
            : "main-context-menu-translate-link-text-to-language",
          { language: displayName }
        );
        return;
      }
    }

    // Either no to-language exists, or an error occurred,
    // so localize the menuitem without a target language.
    translateSelectionItem.removeAttribute("target-language");
    this.document.l10n.setAttributes(
      translateSelectionItem,
      this.isTextSelected
        ? "main-context-menu-translate-selection"
        : "main-context-menu-translate-link-text"
    );
  }

  /**
   * Fetches text for translation, prioritizing selected text over link text.
   *
   * @returns {string} The text to translate.
   */
  #getTextToTranslate() {
    if (this.isTextSelected) {
      // If there is an active selection, we will always offer to translate.
      return this.selectionInfo.fullText.trim();
    }

    const linkText = this.linkTextStr.trim();
    if (!linkText) {
      // There was no underlying link text, so do not offer to translate.
      return "";
    }

    if (URL.canParse(linkText)) {
      // The underlying link text is a URL, we should not offer to translate.
      return "";
    }

    // Since the underlying link text is not a URL, we should offer to translate it.
    return linkText;
  }

  /**
   * Displays or hides the translate-selection item in the context menu.
   */
  showTranslateSelectionItem() {
    const translateSelectionItem = this.document.getElementById(
      "context-translate-selection"
    );
    const translationsEnabled = Services.prefs.getBoolPref(
      "browser.translations.enable"
    );
    const selectTranslationsEnabled = Services.prefs.getBoolPref(
      "browser.translations.select.enable"
    );

    const textToTranslate = this.#getTextToTranslate();

    translateSelectionItem.hidden =
      // Only show the item if the feature is enabled.
      !(translationsEnabled && selectTranslationsEnabled) ||
      // Only show the item if Translations is supported on this hardware.
      !lazy.TranslationsParent.getIsTranslationsEngineSupported() ||
      // If there is no text to translate, we have nothing to do.
      textToTranslate.length === 0;

    if (translateSelectionItem.hidden) {
      translateSelectionItem.removeAttribute("target-language");
      return;
    }

    this.#translationsLangPairPromise =
      this.window.SelectTranslationsPanel.getLangPairPromise(textToTranslate);
    this.localizeTranslateSelectionItem(translateSelectionItem);
  }

  // Formats the 'Search <engine> for "<selection or link text>"' context menu.
  showAndFormatSearchContextItem() {
    let { document } = this.window;
    let menuItem = document.getElementById("context-searchselect");
    let menuItemPrivate = document.getElementById(
      "context-searchselect-private"
    );
    if (!Services.search.hasSuccessfullyInitialized) {
      menuItem.hidden = true;
      menuItemPrivate.hidden = true;
      return;
    }
    const docIsPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(
      this.browser
    );
    const privatePref = "browser.search.separatePrivateDefault.ui.enabled";
    let showSearchSelect =
      !this.inAboutDevtoolsToolbox &&
      (this.isTextSelected || this.onLink) &&
      !this.onImage;
    // Don't show the private search item when we're already in a private
    // browsing window.
    let showPrivateSearchSelect =
      showSearchSelect &&
      !docIsPrivate &&
      Services.prefs.getBoolPref(privatePref);

    menuItem.hidden = !showSearchSelect;
    menuItemPrivate.hidden = !showPrivateSearchSelect;
    let frameSeparator = document.getElementById("frame-sep");

    // Add a divider between "Search X for Y" and "This Frame", and between "Search X for Y" and "Check Spelling",
    // but no divider in other cases.
    frameSeparator.toggleAttribute(
      "ensureHidden",
      !showSearchSelect && this.inFrame
    );
    // If we're not showing the menu items, we can skip formatting the labels.
    if (!showSearchSelect) {
      return;
    }

    let selectedText = this.isTextSelected
      ? this.selectedText
      : this.linkTextStr;

    // Store searchTerms in context menu item so we know what to search onclick
    menuItem.searchTerms = menuItemPrivate.searchTerms = selectedText;
    menuItem.principal = menuItemPrivate.principal = this.principal;
    menuItem.csp = menuItemPrivate.csp = this.csp;

    // Copied to alert.js' prefillAlertInfo().
    // If the JS character after our truncation point is a trail surrogate,
    // include it in the truncated string to avoid splitting a surrogate pair.
    if (selectedText.length > 15) {
      let truncLength = 15;
      let truncChar = selectedText[15].charCodeAt(0);
      if (truncChar >= 0xdc00 && truncChar <= 0xdfff) {
        truncLength++;
      }
      selectedText = selectedText.substr(0, truncLength) + this.ellipsis;
    }

    const { gNavigatorBundle } = this.window;
    // format "Search <engine> for <selection>" string to show in menu
    let engineName = Services.search.defaultEngine.name;
    let privateEngineName = Services.search.defaultPrivateEngine.name;
    menuItem.usePrivate = docIsPrivate;
    let menuLabel = gNavigatorBundle.getFormattedString("contextMenuSearch", [
      docIsPrivate ? privateEngineName : engineName,
      selectedText,
    ]);
    menuItem.label = menuLabel;
    menuItem.accessKey = gNavigatorBundle.getString(
      "contextMenuSearch.accesskey"
    );

    if (showPrivateSearchSelect) {
      let otherEngine = engineName != privateEngineName;
      let accessKey = "contextMenuPrivateSearch.accesskey";
      if (otherEngine) {
        menuItemPrivate.label = gNavigatorBundle.getFormattedString(
          "contextMenuPrivateSearchOtherEngine",
          [privateEngineName]
        );
        accessKey = "contextMenuPrivateSearchOtherEngine.accesskey";
      } else {
        menuItemPrivate.label = gNavigatorBundle.getString(
          "contextMenuPrivateSearch"
        );
      }
      menuItemPrivate.accessKey = gNavigatorBundle.getString(accessKey);
    }
  }

  createContainerMenu(aEvent) {
    let createMenuOptions = {
      isContextMenu: true,
      excludeUserContextId: this.contentData.userContextId,
    };
    return this.window.createUserContextMenu(aEvent, createMenuOptions);
  }
}
