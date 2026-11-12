/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { SidebarPage } from "chrome://browser/content/sidebar/sidebar-page.mjs";
import { SUBMIT_ICON } from "chrome://browser/content/widgets/widget-composer.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AuthenticationController:
    "moz-src:///browser/components/widgets/AuthenticationController.sys.mjs",
});

/**
 * The widgets sidebar panel.
 */
export class SidebarWidgetStudio extends SidebarPage {
  static properties = {
    composing: { type: Boolean, state: true },
  };

  static shadowRootOptions = {
    ...SidebarPage.shadowRootOptions,
    delegatesFocus: true,
  };

  static queries = {
    composeButton: "#compose-button",
    closeButton: "#close-button",
    composer: "widget-composer",
  };

  authentication = new lazy.AuthenticationController(this);

  constructor() {
    super();
    this.composing = true;
  }

  connectedCallback() {
    super.connectedCallback();
    this.addSidebarFocusedListeners();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeSidebarFocusedListeners();
  }

  handleSidebarFocusedEvent() {
    (this.composer ?? this.composeButton)?.focus();
  }

  #closePanel(e) {
    let controller = this.sidebarController;
    controller._maybeAnimate(() =>
      controller.hide({ restoreFocus: e.detail === 0 })
    );
  }

  async #openComposer() {
    this.composing = true;
    await this.updateComplete;
    this.composer.focus();
  }

  async #closeComposer() {
    this.composing = false;
    await this.updateComplete;
    this.composeButton.focus();
  }

  composerTemplate() {
    if (!this.authentication.authenticated) {
      return nothing;
    }
    if (this.composing) {
      return html`<widget-composer
        autofocus
        @WidgetComposer:Dismiss=${this.#closeComposer}
        @WidgetComposer:Submit=${this.#closeComposer}
      ></widget-composer>`;
    }
    return html`<moz-button
      id="compose-button"
      type="ghost icon"
      iconsrc=${SUBMIT_ICON}
      data-l10n-id="widget-studio-compose-button"
      @click=${this.#openComposer}
    ></moz-button>`;
  }

  render() {
    return html`<link
        rel="stylesheet"
        href="chrome://browser/content/widgets/sidebar-widget-studio.css"
      />
      <moz-button
        id="close-button"
        type="ghost icon"
        iconsrc="chrome://global/skin/icons/close.svg"
        data-l10n-id="sidebar-panel-header-close-button"
        @click=${this.#closePanel}
      ></moz-button>
      <slot></slot>
      ${this.composerTemplate()}`;
  }
}

customElements.define("sidebar-widget-studio", SidebarWidgetStudio);
