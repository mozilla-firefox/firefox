/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PromptUtils: "resource://gre/modules/PromptUtils.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "gTabBrowserLocalization", function () {
  return new Localization(["browser/tabbrowser.ftl"], true);
});

/**
 * @typedef {object} Dialog
 */

/**
 * gBrowserDialogs weakly maps BrowsingContexts to a Map of their currently
 * active Dialogs.
 *
 * @type {WeakMap<BrowsingContext, Dialog>}
 */
let gBrowserDialogs = new WeakMap();

export class PromptParent extends JSWindowActorParent {
  didDestroy() {
    // In the event that the subframe or tab crashed, make sure that
    // we close any active Prompts.
    this.forceClosePrompts();
  }

  /**
   * Registers a new dialog to be tracked for a particular BrowsingContext.
   * We need to track a dialog so that we can, for example, force-close the
   * dialog if the originating subframe or tab unloads or crashes.
   *
   * @param {Dialog} dialog
   *        The dialog that will be shown to the user.
   * @param {string} id
   *        A unique ID to differentiate multiple dialogs coming from the same
   *        BrowsingContext.
   */
  registerDialog(dialog, id) {
    let dialogs = gBrowserDialogs.get(this.browsingContext);
    if (!dialogs) {
      dialogs = new Map();
      gBrowserDialogs.set(this.browsingContext, dialogs);
    }

    dialogs.set(id, dialog);
  }

  /**
   * Removes a Prompt for a BrowsingContext with a particular ID from the registry.
   * This needs to be done to avoid leaking <xul:browser>'s.
   *
   * @param {string} id
   *        A unique ID to differentiate multiple Prompts coming from the same
   *        BrowsingContext.
   */
  unregisterPrompt(id) {
    let dialogs = gBrowserDialogs.get(this.browsingContext);
    dialogs?.delete(id);
  }

  /**
   * Programmatically closes all Prompts for the current BrowsingContext.
   */
  forceClosePrompts() {
    let dialogs = gBrowserDialogs.get(this.browsingContext) || [];

    for (let [, dialog] of dialogs) {
      dialog?.abort();
    }
  }

  isAboutAddonsOptionsPage(browsingContext) {
    const { embedderWindowGlobal, name } = browsingContext;
    if (!embedderWindowGlobal) {
      // Return earlier if there is no embedder global, this is definitely
      // not an about:addons extensions options page.
      return false;
    }

    return (
      embedderWindowGlobal.documentPrincipal.isSystemPrincipal &&
      embedderWindowGlobal.documentURI.spec === "about:addons" &&
      name === "addon-inline-options"
    );
  }

  #isNestedInSidebarBrowser(browser) {
    return (
      browser?.documentGlobal?.browsingContext.embedderElement?.id == "sidebar"
    );
  }

  // Note that this will return false for the sidebar <browser> element
  // itself.
  isEmbeddedInSidebar(browser) {
    if (!this.#isNestedInSidebarBrowser(browser)) {
      return false;
    }
    // Extensions in the sidebar have more layers of nesting, and this causes
    // window leaks in tests. We would like to fix this at some point (bug 1513656)
    if (
      browser.getAttribute("messagemanagergroup") == "webext-browsers" ||
      browser.getAttribute("messagemanagergroup") == "chatbot-browser"
    ) {
      return false;
    }
    return true;
  }

  // Prompts from the AI chatbot browser also need to be hoisted to the sidebar
  // <browser> so they are shown as tab dialogs (bug 1955250), even though
  // isEmbeddedInSidebar() excludes it.
  shouldShowPromptOnSidebarBrowser(browser) {
    return (
      this.isEmbeddedInSidebar(browser) ||
      (this.#isNestedInSidebarBrowser(browser) &&
        browser.getAttribute("messagemanagergroup") == "chatbot-browser")
    );
  }

  receiveMessage(message) {
    switch (message.name) {
      case "Prompt:Open":
        if (!this.windowContext.isActiveInTab) {
          return undefined;
        }

        return this.openPromptWithTabDialogBox(message.data);
    }

    return undefined;
  }

  /**
   * Opens either a window prompt or TabDialogBox at the content or tab level
   * for a BrowsingContext, and puts the associated browser in the modal state
   * until the prompt is closed.
   *
   * @param {object} args
   *        The arguments passed up from the BrowsingContext to be passed
   *        directly to the modal prompt.
   * @return {Promise<object>}
   *         Resolves with the arguments returned from the modal prompt.
   */
  async openPromptWithTabDialogBox(args) {
    const COMMON_DIALOG = "chrome://global/content/commonDialog.xhtml";
    const SELECT_DIALOG = "chrome://global/content/selectDialog.xhtml";
    let uri = args.promptType == "select" ? SELECT_DIALOG : COMMON_DIALOG;

    let browsingContext = this.browsingContext.top;

    let browserId = browsingContext.browserId;
    let browser = browsingContext.embedderElement;

    let showOnSidebarBrowser = this.shouldShowPromptOnSidebarBrowser(browser);
    if (
      showOnSidebarBrowser ||
      this.isAboutAddonsOptionsPage(browsingContext)
    ) {
      browser = browser.documentGlobal.browsingContext.embedderElement;
    }

    let promptRequiresBrowser =
      args.modalType === Services.prompt.MODAL_TYPE_TAB ||
      args.modalType === Services.prompt.MODAL_TYPE_CONTENT;
    if (promptRequiresBrowser && !browser) {
      let modal_type =
        args.modalType === Services.prompt.MODAL_TYPE_TAB ? "tab" : "content";
      throw new Error(`Cannot ${modal_type}-prompt without a browser!`);
    }

    const closingEventDetails =
      args.modalType === Services.prompt.MODAL_TYPE_CONTENT
        ? {
            owningBrowsingContext: this.browsingContext,
            promptType: args.inPermitUnload ? "beforeunload" : args.promptType,
          }
        : null;

    let win;

    // If we are a chrome actor we can use the associated chrome win.
    if (!browsingContext.isContent && browsingContext.window) {
      win = browsingContext.window;
    } else {
      win = browser?.documentGlobal;
    }

    // There's a requirement for prompts to be blocked if a window is
    // passed and that window is hidden (eg, auth prompts are suppressed if the
    // passed window is the hidden window).
    // See bug 875157 comment 30 for more..
    if (win?.winUtils && !win.winUtils.isParentWindowMainWidgetVisible) {
      throw new Error("Cannot open a prompt in a hidden window");
    }

    try {
      if (browsingContext.embedderElement) {
        browsingContext.embedderElement.enterModalState();
        lazy.PromptUtils.fireDialogEvent(
          win,
          "DOMWillOpenModalDialog",
          browsingContext.embedderElement,
          this.getOpenEventDetail(args)
        );
      }

      args.promptAborted = false;
      args.openedWithTabDialog = true;
      args.owningBrowsingContext = this.browsingContext;

      // Convert args object to a prop bag for the dialog to consume.
      let bag;

      if (promptRequiresBrowser && win?.gBrowser?.getTabDialogBox) {
        // Tab or content level prompt
        let dialogBox = win.gBrowser.getTabDialogBox(browser);

        if (dialogBox._allowTabFocusByPromptPrincipal) {
          this.addTabSwitchCheckboxToArgs(dialogBox, args);
        }

        let currentLocationsTabLabel;

        let targetTab = win.gBrowser.getTabForBrowser(browser);
        if (
          !Services.prefs.getBoolPref(
            "privacy.authPromptSpoofingProtection",
            false
          )
        ) {
          args.isTopLevelCrossDomainAuth = false;
        }
        // Auth prompt spoofing protection, see bug 791594.
        if (args.isTopLevelCrossDomainAuth && targetTab) {
          // Set up the url bar with the url of the cross domain resource.
          // onLocationChange will change the url back to the current browsers
          // if we do not hold the state here.
          // onLocationChange will favour currentAuthPromptURI over the current browsers uri
          browser.currentAuthPromptURI = args.channel.URI;
          if (browser == win.gBrowser.selectedBrowser) {
            win.gURLBar.setURI();
          }
          // Set up the tab title for the cross domain resource.
          // We need to remember the original tab title in case
          // the load does not happen after the prompt, then we need to reset the tab title manually.
          currentLocationsTabLabel = targetTab.label;
          win.gBrowser.setTabLabelForAuthPrompts(
            targetTab,
            lazy.BrowserUtils.formatURIForDisplay(args.channel.URI)
          );
        }
        bag = lazy.PromptUtils.objectToPropBag(args);
        let promptID = args._remoteId;
        try {
          let { dialog, closedPromise } = dialogBox.open(
            uri,
            {
              features: "resizable=no",
              modalType: args.modalType,
              allowFocusCheckbox: args.allowFocusCheckbox,
              hideContent: args.isTopLevelCrossDomainAuth,
              // If we are in the sidebar, use the inner browser to detect when navigation is done
              webProgress: showOnSidebarBrowser
                ? browsingContext?.webProgress
                : undefined,
            },
            bag
          );
          dialog.promptID = promptID;
          this.registerDialog(dialog, promptID);
          await closedPromise;
        } finally {
          if (args.isTopLevelCrossDomainAuth) {
            browser.currentAuthPromptURI = null;
            // If the user is stopping the page load before answering the prompt, no navigation will happen after the prompt
            // so we need to reset the uri and tab title here to the current browsers for that specific case
            if (browser == win.gBrowser.selectedBrowser) {
              win.gURLBar.setURI();
            }
            win.gBrowser.setTabLabelForAuthPrompts(
              targetTab,
              currentLocationsTabLabel
            );
          }
          this.unregisterPrompt(promptID);
        }
      } else {
        // Ensure we set the correct modal type at this point.
        // If we use window prompts as a fallback it may not be set.
        args.modalType = Services.prompt.MODAL_TYPE_WINDOW;
        // Window prompt
        bag = lazy.PromptUtils.objectToPropBag(args);
        Services.ww.openWindow(
          win,
          uri,
          "_blank",
          "centerscreen,chrome,modal,titlebar",
          bag
        );
      }

      lazy.PromptUtils.propBagToObject(bag, args);
    } finally {
      // We need to look up the BrowsingContext and embedder element from
      // browserId, as we could have torn the tab out into a different window
      // (meaning the embedder element is stale), and/or we could have performed
      // a cross-group navigation (meaning the BrowsingContext is stale).
      let closingEmbedder =
        BrowsingContext.getCurrentTopByBrowserId(browserId)?.embedderElement;
      if (closingEmbedder) {
        closingEmbedder.maybeLeaveModalState();
        lazy.PromptUtils.fireDialogEvent(
          closingEmbedder.documentGlobal,
          "DOMModalDialogClosed",
          closingEmbedder,
          closingEventDetails
            ? {
                ...closingEventDetails,
                areLeaving: args.ok,
                // If a prompt was not accepted, do not return the prompt value.
                value: args.ok ? args.value : null,
              }
            : null
        );
      }
    }
    return args;
  }

  getOpenEventDetail(args) {
    let details =
      args.modalType === Services.prompt.MODAL_TYPE_CONTENT
        ? {
            inPermitUnload: args.inPermitUnload,
            promptPrincipal: args.promptPrincipal,
            tabPrompt: true,
          }
        : null;

    return details;
  }

  /**
   * Set properties on `args` needed by the dialog to allow tab switching for the
   * page that opened the prompt.
   *
   * @param {TabDialogBox}  dialogBox
   *        The dialog to show the tab-switch checkbox for.
   * @param {object}  args
   *        The `args` object to set tab switching permission info on.
   */
  addTabSwitchCheckboxToArgs(dialogBox, args) {
    let allowTabFocusByPromptPrincipal =
      dialogBox._allowTabFocusByPromptPrincipal;

    if (
      allowTabFocusByPromptPrincipal &&
      args.modalType === Services.prompt.MODAL_TYPE_CONTENT
    ) {
      let domain = allowTabFocusByPromptPrincipal.addonPolicy?.name;
      try {
        domain ||= allowTabFocusByPromptPrincipal.URI.displayHostPort;
      } catch (ex) {
        /* Ignore exceptions from fetching the display host/port. */
      }
      // If it's still empty, use `prePath` so we have *something* to show:
      domain ||= allowTabFocusByPromptPrincipal.URI.prePath;
      let [allowFocusMsg] = lazy.gTabBrowserLocalization.formatMessagesSync([
        {
          id: "tabbrowser-allow-dialogs-to-get-focus",
          args: { domain },
        },
      ]);
      let labelAttr = allowFocusMsg.attributes.find(a => a.name == "label");
      if (labelAttr) {
        args.allowFocusCheckbox = true;
        args.checkLabel = labelAttr.value;
      }
    }
  }
}
