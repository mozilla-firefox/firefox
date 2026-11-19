/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
});

export const MenuMessage = {
  SOURCES: Object.freeze({
    APP_MENU: "app_menu",
    PXI_MENU: "pxi_menu",
  }),

  MESSAGE_TYPES: Object.freeze({
    FXA_CTA: "fxa_cta",
    DEFAULT_CTA: "default_cta",
  }),

  MESSAGE_TYPE_ALLOWED_BY_SOURCE: Object.freeze({
    app_menu: new Set(["fxa_cta", "default_cta"]),
    pxi_menu: new Set(["fxa_cta"]),
  }),
  SHOWING_FXA_MENU_MESSAGE_ATTR: "showing-fxa-menu-message",
  SHOWING_SET_TO_DEFAULT_MENU_MESSAGE_ATTR: "showing-default-cta-menu-message",

  async showMenuMessage(browser, message, trigger, force) {
    if (!browser) {
      return;
    }

    let win = browser.documentGlobal;

    if (!win || lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }

    let source = trigger?.context?.source || message.testingTriggerContext;

    // Restrict message types to their allowed sources
    if (
      !this.MESSAGE_TYPE_ALLOWED_BY_SOURCE[source]?.has(
        message.content.messageType
      )
    ) {
      return;
    }

    switch (source) {
      case this.SOURCES.APP_MENU: {
        this.showAppMenuMessage(browser, message, force);
        break;
      }

      case this.SOURCES.PXI_MENU: {
        this.showPxiMenuMessage(browser, message, force);
        break;
      }
    }
  },

  /**
   * Whether the message should be suppressed for a signed-in user.
   *  - fxa_cta: suppress when signed in by default, unless
   *    content.allowWhenSignedIn is set to true.
   *  - default_cta: never suppress
   */
  shouldSuppressForSignedIn(message) {
    const type = message?.content?.messageType;
    const isSignedIn =
      lazy.UIState.get().status === lazy.UIState.STATUS_SIGNED_IN;

    // If not signed in, no need to suppress
    if (!isSignedIn) {
      return false;
    }

    //  Suppress fxa_cta messages by default, unless we explicitly allow it.
    if (type === this.MESSAGE_TYPES.FXA_CTA) {
      const allowWhenSignedIn = !!message.content?.allowWhenSignedIn;
      return !allowWhenSignedIn;
    }

    // For any other message, we don't suppress it.
    return false;
  },

  preparePrimaryAction(message, source) {
    const type = message?.content?.messageType;
    const primaryAction = message?.content?.primaryAction;
    if (!primaryAction) {
      return null;
    }
    const action = structuredClone(primaryAction);

    // For fxa_cta messages, depending on the source that showed the
    // message, we'll want to set a particular entrypoint in the data
    // payload in the event that we're  opening up the FxA sign-up page.
    if (type === this.MESSAGE_TYPES.FXA_CTA) {
      action.data = action.data || {};
      action.data.extraParams = action.data.extraParams || {};

      if (source === this.SOURCES.APP_MENU) {
        action.data.entrypoint = "fxa_app_menu";
        action.data.extraParams.utm_content = `${action.data.extraParams.utm_content}-app_menu`;
      } else if (source === this.SOURCES.PXI_MENU) {
        action.data.entrypoint = "fxa_avatar_menu";
        action.data.extraParams.utm_content = `${action.data.extraParams.utm_content}-avatar`;
      }
    }
    return action;
  },

  async showAppMenuMessage(browser, message, force) {
    const win = browser.documentGlobal;
    const msgContainer = this.hideAppMenuMessage(browser);
    const type = message?.content?.messageType;

    // This version of the browser only supports the fxa_cta and
    // default_cta versions of this message in the AppMenu.
    // We also don't draw focus away from any existing AppMenuNotifications.
    if (!message || lazy.AppMenuNotifications.activeNotification) {
      return;
    }

    // Respect message type signed-in render rules (fxa_cta suppresses by default)
    if (this.shouldSuppressForSignedIn(message)) {
      return;
    }

    const menuMessageAttribute =
      type === this.MESSAGE_TYPES.DEFAULT_CTA
        ? MenuMessage.SHOWING_SET_TO_DEFAULT_MENU_MESSAGE_ATTR
        : MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR;

    win.PanelUI.mainView.setAttribute(menuMessageAttribute, message.id);

    let msgElement = await this.constructMenuMessage(
      win,
      message,
      MenuMessage.SOURCES.APP_MENU
    );

    msgElement.addEventListener("MenuMessage:Close", () => {
      win.PanelUI.mainView.removeAttribute(menuMessageAttribute);
    });

    msgElement.addEventListener("MenuMessage:PrimaryButton", () => {
      win.PanelUI.hide();
    });

    if (message.content.layout === "simple") {
      msgContainer.style.display = "flex";
      msgElement.style.flex = "1";
    }
    msgContainer.appendChild(msgElement);

    if (force) {
      win.PanelUI.show();
    }
  },

  hideAppMenuMessage(browser) {
    const win = browser.documentGlobal;
    const document = browser.ownerDocument;
    const msgContainer = lazy.PanelMultiView.getViewNode(
      document,
      "appMenu-menu-message"
    );
    msgContainer.innerHTML = "";
    win.PanelUI.mainView.removeAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR
    );
    win.PanelUI.mainView.removeAttribute(
      MenuMessage.SHOWING_SET_TO_DEFAULT_MENU_MESSAGE_ATTR
    );

    return msgContainer;
  },

  async showPxiMenuMessage(browser, message, force) {
    const win = browser.documentGlobal;
    const { document } = win;
    const msgContainer = this.hidePxiMenuMessage(browser);

    // Respect message type signed-in render rules (fxa_cta suppresses by default)
    if (this.shouldSuppressForSignedIn(message)) {
      return;
    }

    let fxaPanelView = lazy.PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.setAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR,
      message.id
    );

    let msgElement = await this.constructMenuMessage(
      win,
      message,
      MenuMessage.SOURCES.PXI_MENU
    );

    msgElement.addEventListener("MenuMessage:Close", () => {
      fxaPanelView.removeAttribute(MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR);
    });

    msgElement.addEventListener("MenuMessage:PrimaryButton", () => {
      let panelNode = fxaPanelView.closest("panel");

      if (panelNode) {
        lazy.PanelMultiView.hidePopup(panelNode);
      }
    });

    if (message.content.layout === "simple") {
      msgContainer.style.display = "flex";
      msgElement.style.flex = "1";
    }
    msgContainer.appendChild(msgElement);

    if (force) {
      await win.gSync.toggleAccountPanel(
        document.getElementById("fxa-toolbar-menu-button"),
        new MouseEvent("mousedown")
      );
    }
  },

  hidePxiMenuMessage(browser) {
    const document = browser.ownerDocument;
    const msgContainer = lazy.PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-message"
    );
    msgContainer.innerHTML = "";
    let fxaPanelView = lazy.PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.removeAttribute(MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR);
    return msgContainer;
  },

  async constructMenuMessage(win, message, source) {
    let { document, gBrowser } = win;

    win.MozXULElement.insertFTLIfNeeded("browser/newtab/asrouter.ftl");

    const msgElement = document.createElement("menu-message");
    msgElement.layout = message.content.layout ?? "column";
    if (message.content.imagePosition !== undefined) {
      msgElement.imagePosition = message.content.imagePosition;
    }
    if (message.content.directionalImage) {
      msgElement.directionalImage = true;
    }
    msgElement.imageURL = message.content.imageURL;
    if (message.content.rtlImageURL !== undefined) {
      msgElement.rtlImageURL = message.content.rtlImageURL;
    }
    msgElement.logoURL = message.content.logoURL;
    msgElement.primaryButtonSize =
      message.content.primaryButtonSize ?? "default";
    msgElement.buttonText = await lazy.RemoteL10n.formatLocalizableText(
      message.content.primaryActionText
    );
    msgElement.primaryText = await lazy.RemoteL10n.formatLocalizableText(
      message.content.primaryText
    );
    // Simple layout does not support secondary text
    if (message.content.layout !== "simple" && message.content.secondaryText) {
      msgElement.secondaryText = await lazy.RemoteL10n.formatLocalizableText(
        message.content.secondaryText
      );
    }
    msgElement.dataset.navigableWithTabOnly = "true";
    if (message.content.imageWidth !== undefined) {
      msgElement.style.setProperty(
        "--image-width",
        `${message.content.imageWidth}px`
      );
    }
    if (message.content.imageHeight !== undefined) {
      msgElement.style.setProperty(
        "--image-height",
        `${message.content.imageHeight}px`
      );
    }
    if (message.content.logoWidth !== undefined) {
      msgElement.style.setProperty(
        "--logo-width",
        `${message.content.logoWidth}px`
      );
    }
    if (message.content.imageVerticalTopOffset !== undefined) {
      msgElement.style.setProperty(
        "--illustration-margin-block-start-offset",
        `${message.content.imageVerticalTopOffset}px`
      );
    }
    if (message.content.imageVerticalBottomOffset !== undefined) {
      msgElement.style.setProperty(
        "--illustration-margin-block-end-offset",
        `${message.content.imageVerticalBottomOffset}px`
      );
    }
    if (message.content.containerVerticalBottomOffset !== undefined) {
      msgElement.style.setProperty(
        "--container-margin-block-end-offset",
        `${message.content.containerVerticalBottomOffset}px`
      );
    }
    if (message.content.containerPaddingBottom !== undefined) {
      msgElement.style.setProperty(
        "--container-padding-block-end",
        `${message.content.containerPaddingBottom}px`
      );
    }

    msgElement.addEventListener("MenuMessage:Close", () => {
      msgElement.remove();

      this.recordMenuMessageTelemetry("DISMISS", source, message.id);

      lazy.SpecialMessageActions.handleAction(
        message.content.closeAction,
        gBrowser.selectedBrowser
      );
    });

    msgElement.addEventListener("MenuMessage:PrimaryButton", () => {
      this.recordMenuMessageTelemetry("CLICK", source, message.id);

      const primaryAction = this.preparePrimaryAction(message, source);
      if (primaryAction) {
        lazy.SpecialMessageActions.handleAction(
          primaryAction,
          gBrowser.selectedBrowser
        );
      }
    });

    return msgElement;
  },

  recordMenuMessageTelemetry(event, source, messageId) {
    let ping = {
      message_id: messageId,
      event,
      source,
    };
    lazy.ASRouter.dispatchCFRAction({
      type: "MENU_MESSAGE_TELEMETRY",
      data: { action: "menu_message_user_event", ...ping },
    });
  },
};
