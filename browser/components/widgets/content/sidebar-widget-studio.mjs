/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { SidebarPage } from "chrome://browser/content/sidebar/sidebar-page.mjs";

/**
 * The widgets sidebar panel.
 */
export class SidebarWidgetStudio extends SidebarPage {
  static queries = {
    closeButton: "#close-button",
  };

  #closePanel(e) {
    let controller = this.sidebarController;
    controller._maybeAnimate(() =>
      controller.hide({ restoreFocus: e.detail === 0 })
    );
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
      <slot></slot>`;
  }
}

customElements.define("sidebar-widget-studio", SidebarWidgetStudio);
