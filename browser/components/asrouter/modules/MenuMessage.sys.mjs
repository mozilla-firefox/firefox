/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  PanelMultiView: "resource:///modules/PanelMultiView.sys.mjs",
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

  SHOWING_FXA_MENU_MESSAGE_ATTR: "showing-fxa-menu-message",

  async showMenuMessage(browser, message, trigger, force) {
    if (!browser) {
      return;
    }

    let win = browser.ownerGlobal;

    if (!win || lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }

    let source = trigger?.context?.source || message.testingTriggerContext;

    switch (source) {
      case MenuMessage.SOURCES.APP_MENU: {
        this.showAppMenuMessage(browser, message, force);
        break;
      }
      case MenuMessage.SOURCES.PXI_MENU: {
        this.showPxiMenuMessage(browser, message, force);
        break;
      }
    }
  },

  async showAppMenuMessage(browser, message, force) {
    const win = browser.ownerGlobal;
    const msgContainer = this.hideAppMenuMessage(browser);

    // This version of the browser only supports the fxa_cta version
    // of this message in the AppMenu. We also don't draw focus away from any
    // existing AppMenuNotifications.
    if (
      !message ||
      message.content.messageType !== "fxa_cta" ||
      lazy.AppMenuNotifications.activeNotification
    ) {
      return;
    }

    // Since we know this is an fxa_cta message, we know that if we're already
    // signed in, we don't want to show it in the AppMenu.
    if (lazy.UIState.get().status === lazy.UIState.STATUS_SIGNED_IN) {
      return;
    }

    win.PanelUI.mainView.setAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR,
      message.id
    );

    let msgElement = await this.constructFxAMessage(
      win,
      message,
      MenuMessage.SOURCES.APP_MENU
    );

    msgElement.addEventListener("FxAMenuMessage:Close", () => {
      win.PanelUI.mainView.removeAttribute(
        MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR
      );
    });

    msgElement.addEventListener("FxAMenuMessage:SignUp", () => {
      win.PanelUI.hide();
    });

    msgContainer.appendChild(msgElement);

    if (force) {
      win.PanelUI.show();
    }
  },

  hideAppMenuMessage(browser) {
    const win = browser.ownerGlobal;
    const document = browser.ownerDocument;
    const msgContainer = lazy.PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-menu-message"
    );
    msgContainer.innerHTML = "";
    win.PanelUI.mainView.removeAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR
    );

    return msgContainer;
  },

  async showPxiMenuMessage(browser, message, force) {
    const win = browser.ownerGlobal;
    const { document } = win;
    const msgContainer = this.hidePxiMenuMessage(browser);

    // This version of the browser only supports the fxa_cta version
    // of this message in the PXI menu.
    if (!message || message.content.messageType !== "fxa_cta") {
      return;
    }

    // Since we know this is an fxa_cta message, we know that if we're already
    // signed in, we don't want to show it in the AppMenu.
    if (lazy.UIState.get().status === lazy.UIState.STATUS_SIGNED_IN) {
      return;
    }

    let fxaPanelView = lazy.PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.setAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR,
      message.id
    );

    let msgElement = await this.constructFxAMessage(
      win,
      message,
      MenuMessage.SOURCES.PXI_MENU
    );

    msgElement.addEventListener("FxAMenuMessage:Close", () => {
      fxaPanelView.removeAttribute(MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR);
    });

    msgElement.addEventListener("FxAMenuMessage:SignUp", () => {
      let panelNode = fxaPanelView.closest("panel");

      if (panelNode) {
        lazy.PanelMultiView.hidePopup(panelNode);
      }
    });

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

  async constructFxAMessage(win, message, source) {
    let { document, gBrowser } = win;

    win.MozXULElement.insertFTLIfNeeded("browser/newtab/asrouter.ftl");

    const msgElement = document.createElement("fxa-menu-message");
    msgElement.layout = message.content.layout ?? "column";
    msgElement.imageURL = message.content.imageURL;
    msgElement.buttonText = await lazy.RemoteL10n.formatLocalizableText(
      message.content.primaryActionText
    );
    msgElement.primaryText = await lazy.RemoteL10n.formatLocalizableText(
      message.content.primaryText
    );
    msgElement.secondaryText = await lazy.RemoteL10n.formatLocalizableText(
      message.content.secondaryText
    );
    msgElement.dataset.navigableWithTabOnly = "true";
    if (message.content.imageWidth !== undefined) {
      msgElement.style.setProperty(
        "--image-width",
        `${message.content.imageWidth}px`
      );
    }
    msgElement.style.setProperty(
      "--illustration-margin-block-start-offset",
      `${message.content.imageVerticalTopOffset}px`
    );
    msgElement.style.setProperty(
      "--illustration-margin-block-end-offset",
      `${message.content.imageVerticalBottomOffset}px`
    );
    msgElement.style.setProperty(
      "--container-margin-block-end-offset",
      `${message.content.containerVerticalBottomOffset}px`
    );

    msgElement.addEventListener("FxAMenuMessage:Close", () => {
      msgElement.remove();

      this.recordMenuMessageTelemetry("DISMISS", source, message.id);

      lazy.SpecialMessageActions.handleAction(
        message.content.closeAction,
        gBrowser.selectedBrowser
      );
    });

    msgElement.addEventListener("FxAMenuMessage:SignUp", () => {
      this.recordMenuMessageTelemetry("CLICK", source, message.id);

      // Depending on the source that showed the message, we'll want to set
      // a particular entrypoint in the data payload in the event that we're
      // opening up the FxA sign-up page.
      let clonedPrimaryAction = structuredClone(message.content.primaryAction);
      if (source === MenuMessage.SOURCES.APP_MENU) {
        clonedPrimaryAction.data.entrypoint = "fxa_app_menu";
        clonedPrimaryAction.data.extraParams.utm_content += "-app_menu";
      } else if (source === MenuMessage.SOURCES.PXI_MENU) {
        clonedPrimaryAction.data.entrypoint = "fxa_avatar_menu";
        clonedPrimaryAction.data.extraParams.utm_content += "-avatar";
      }

      lazy.SpecialMessageActions.handleAction(
        clonedPrimaryAction,
        gBrowser.selectedBrowser
      );
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
