/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html, when } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/sidebar/sidebar-panel-switcher.mjs";

/**
 * Shared header showing the panel title and a close button.
 * In horizontal-tabs "hide-launcher" mode the title is replaced
 * by the sidebar-panel-switcher dropdown (which switches between panels), since
 * there is no launcher to switch them with; this is toggled via CSS (see
 * sidebar-panel-header.css).
 */
export class SidebarPanelHeader extends MozLitElement {
  static properties = {
    view: { type: String },
    heading: { type: String },
  };

  static queries = {
    closeButton: "moz-button",
    switcher: "sidebar-panel-switcher",
  };

  #l10n = new Localization(
    ["browser/sidebar.ftl", "preview/genai.ftl", "preview/widgets.ftl"],
    true
  );

  closeSidebarPanel(e) {
    e.preventDefault();
    window.SidebarController._maybeAnimate(() => {
      window.SidebarController.hide({
        dismissPanel: !window.SidebarController._state.launcherHiddenWithPanel,
        // If closing this panel with a keyboard, restore focus back to the
        // launcher button that opened it.
        restoreFocus: e.detail === 0,
      });
    });
  }

  /**
   * Set the active view and compute the new heading.
   *
   * @param {string} view
   */
  setView(view) {
    this.view = view;
    const panelInfo = window.SidebarController.sidebars.get(view);
    if (panelInfo?.revampL10nId) {
      this.heading = this.#getLabelForL10nId(panelInfo.revampL10nId);
    } else if (panelInfo?.label) {
      this.heading = panelInfo.label;
    } else {
      this.heading = "";
      console.error(`Could not find a label for view: ${view}`);
    }
    this.toggleAttribute(
      "panel-has-custom-header",
      !!panelInfo?.hasCustomHeader
    );
  }

  /**
   * Extract the "label" value from the specified Fluent message.
   *
   * @param {string} id
   * @returns {string}
   */
  #getLabelForL10nId(id) {
    const [message] = this.#l10n.formatMessagesSync([{ id }]);
    return (
      message?.attributes?.find(attr => attr.name === "label")?.value ?? ""
    );
  }

  render() {
    return html`
      <link rel="stylesheet" href="chrome://browser/content/sidebar/sidebar-panel-header.css"></link>
      <div class="sidebar-panel-heading">
        <h4 class="sidebar-panel-title text-truncated-ellipsis">${this.heading}</h4>
        <sidebar-panel-switcher .view=${this.view} .label=${this.heading}></sidebar-panel-switcher>
        ${when(
          this.view,
          () =>
            html`<moz-button
              iconsrc="chrome://global/skin/icons/close.svg"
              data-l10n-id="sidebar-panel-header-close-button"
              @click=${this.closeSidebarPanel}
              .view=${this.view}
              size="default"
              type="icon ghost"
            >
            </moz-button>`
        )}
      </div>
    `;
  }
}
customElements.define("sidebar-panel-header", SidebarPanelHeader);
