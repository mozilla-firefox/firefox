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

const weekdayFormat = new Services.intl.DateTimeFormat(undefined, {
  weekday: "narrow",
  timeZone: "UTC",
});
const numberFormat = new Services.intl.NumberFormat();

// 6 September 2026 is a Sunday, the first column of the calendar.
const CALENDAR_WEEKDAYS = Array.from({ length: 7 }, (_, i) =>
  weekdayFormat.format(Date.UTC(2026, 8, 6 + i))
);
const CALENDAR_DAYS = Array.from({ length: 30 }, (_, i) => i + 1);

function calendarDayState(day) {
  if (day >= 20 && day <= 23) {
    return "done";
  }
  if (day == 24) {
    return "today";
  }
  return day > 24 ? "upcoming" : "";
}

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

  #openLearnMore(e) {
    e.preventDefault();
    this.topWindow.openTrustedLinkIn(e.currentTarget.href, "tab");
  }

  signInTemplate() {
    if (
      this.authentication.isStatusPending ||
      this.authentication.authenticated
    ) {
      return nothing;
    }
    return html`<section id="sign-in-prompt">
      <h1 id="sign-in-title" data-l10n-id="widget-studio-sign-in-title"></h1>
      <p
        id="sign-in-description"
        data-l10n-id="widget-studio-sign-in-description"
      ></p>
      <div id="sign-in-actions">
        <a
          id="sign-in-learn-more"
          is="moz-support-link"
          support-page="firefox-custom-widgets"
          @click=${this.#openLearnMore}
        ></a>
        <moz-button
          id="sign-in-button"
          type="primary"
          data-l10n-id="widget-studio-sign-in-button"
          ?disabled=${this.authentication.pending}
          @click=${() => this.authentication.signIn()}
        ></moz-button>
      </div>
      <p
        id="sign-in-example-heading"
        data-l10n-id="widget-studio-sign-in-example-heading"
      ></p>
      <div id="sign-in-example-prompt" aria-hidden="true">
        <img
          src="chrome://browser/content/aiwindow/assets/new-chat.svg"
          alt=""
        />
        <span data-l10n-id="widget-studio-sign-in-example-prompt"></span>
      </div>
      <div id="sign-in-illustration" aria-hidden="true">
        <svg id="sign-in-frame"><rect /></svg>
        <div id="sign-in-calendar">
          <strong
            data-l10n-id="widget-studio-sign-in-calendar-title"
            data-l10n-args='{"days": 4}'
          ></strong>
          <span data-l10n-id="widget-studio-sign-in-calendar-period"></span>
          <div id="sign-in-calendar-weekdays">
            ${CALENDAR_WEEKDAYS.map(letter => html`<span>${letter}</span>`)}
          </div>
          <div id="sign-in-calendar-days">
            ${CALENDAR_DAYS.map(
              day =>
                html`<span data-state=${calendarDayState(day)}
                  >${numberFormat.format(day)}</span
                >`
            )}
          </div>
        </div>
      </div>
      <img
        id="sign-in-kit"
        src="chrome://browser/content/widgets/kit-sparkle-ai.svg"
        alt=""
      />
    </section>`;
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
      ${this.signInTemplate()}
      <slot></slot>
      ${this.composerTemplate()}`;
  }
}

customElements.define("sidebar-widget-studio", SidebarWidgetStudio);
