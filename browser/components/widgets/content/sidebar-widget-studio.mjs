/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  ifDefined,
  nothing,
  render,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { SidebarPage } from "chrome://browser/content/sidebar/sidebar-page.mjs";
import { SUBMIT_ICON } from "chrome://browser/content/widgets/widget-composer.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/widgets/moz-widget.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/widgets/widget-placeholder.mjs";

const CUSTOM_WIDGETS_SUPPORT_PAGE = "firefox-custom-widgets";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AuthenticationController:
    "moz-src:///browser/components/widgets/AuthenticationController.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  WidgetsController:
    "moz-src:///browser/components/widgets/WidgetsController.sys.mjs",
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
    composer: "widget-composer",
    placeholder: "widget-placeholder",
  };

  // The more button and its menu live in the shared sidebar header, not in
  // this page's shadow root, and are undefined while the page is disconnected.
  menuButton;
  menu;

  authentication = new lazy.AuthenticationController(this);
  widgets = new lazy.WidgetsController(this);

  #hadFocus = false;
  #focusNewPlaceholder = false;

  constructor() {
    super();
    this.composing = true;
  }

  connectedCallback() {
    super.connectedCallback();
    this.composing = !this.widgets.generating && !this.widgets.failed;
    this.#mountHeaderMenu();
    this.addSidebarFocusedListeners();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#unmountHeaderMenu();
    this.removeSidebarFocusedListeners();
  }

  handleSidebarFocusedEvent() {
    (this.composer ?? this.placeholder ?? this.composeButton)?.focus();
  }

  willUpdate(changes) {
    super.willUpdate(changes);
    this.#hadFocus = !!this.renderRoot.activeElement;
  }

  update(changes) {
    super.update(changes);
    // Browsers in the light DOM can open pickers and keep their content
    // when moved with moveBefore().
    let slotted = this.renderSlotted();
    render(slotted, this, { host: this });
  }

  async updated() {
    if (this.#focusNewPlaceholder) {
      this.#focusNewPlaceholder = false;
      let { placeholder } = this;
      await placeholder.updateComplete;
      placeholder.focus();
    } else if (this.#hadFocus && !this.renderRoot.activeElement) {
      await this.updateComplete;
      this.handleSidebarFocusedEvent();
    }
  }

  #mountHeaderMenu() {
    if (this.menuButton) {
      return;
    }

    let doc = this.topWindow.document;
    this.menu = doc.createElement("panel-list");
    this.menu.id = "widget-studio-menu";
    this.menu.slot = "actions";

    let shareFeedback = doc.createElement("panel-item");
    shareFeedback.setAttribute("no-icon", "");
    doc.l10n.setAttributes(shareFeedback, "widget-studio-menu-share-feedback");
    shareFeedback.addEventListener("click", () =>
      this.#openUrl("https://connect.mozilla.org/")
    );

    let about = doc.createElement("panel-item");
    about.setAttribute("no-icon", "");
    doc.l10n.setAttributes(about, "widget-studio-menu-about");
    about.addEventListener("click", () => this.#openSupportPage());
    this.menu.append(shareFeedback, about);

    this.menuButton = doc.createElement("moz-button");
    this.menuButton.slot = "actions";
    this.menuButton.setAttribute("type", "ghost icon");
    this.menuButton.setAttribute(
      "iconsrc",
      "chrome://global/skin/icons/more.svg"
    );
    this.menuButton.setAttribute("menuid", this.menu.id);
    doc.l10n.setAttributes(this.menuButton, "sidebar-options-menu-button");

    this.sidebarController.panelHeader.append(this.menuButton, this.menu);
  }

  #unmountHeaderMenu() {
    if (!this.menu) {
      return;
    }

    this.menu.hide(null, { force: true });
    this.menuButton.remove();
    this.menu.remove();
    this.menuButton = undefined;
    this.menu = undefined;
  }

  async #openComposer() {
    this.composing = true;
    await this.updateComplete;
    this.composer.focus();
  }

  #closeComposer() {
    this.composing = false;
  }

  #create(event) {
    this.composing = false;
    this.#focusNewPlaceholder = true;
    this.widgets.create(event.detail.prompt);
  }

  async #editPrompt(event) {
    let placeholder = event.target;
    if (placeholder.error) {
      this.widgets.dismiss();
    } else {
      this.widgets.cancel();
    }
    let draft = this.composing && this.composer?.value.trim();
    this.composing = true;
    await this.updateComplete;
    let { composer } = this;
    if (composer) {
      if (!draft) {
        composer.value = placeholder.prompt;
      }
      composer.focus();
    }
  }

  #sizeOf(widget) {
    // The sidebar does not support small widgets.
    if (widget.size == "small") {
      return "medium";
    }
    return widget.size;
  }

  cardsTemplate() {
    return repeat(
      this.widgets.all,
      widget => widget.id,
      widget =>
        html`<moz-widget
          widget-id=${widget.id}
          .title=${widget.name}
          padded
          remote-type=${lazy.E10SUtils.EXTENSION_REMOTE_TYPE}
          group-id=${ifDefined(
            WebExtensionPolicy.getByID(widget.id)?.browsingContextGroupId
          )}
          src=${widget.src}
          size=${this.#sizeOf(widget)}
        ></moz-widget>`
    );
  }

  placeholderTemplate() {
    let widget = { prompt: this.widgets.prompt, error: null };
    if (!this.widgets.generating) {
      widget = this.widgets.failed;
    }
    if (!widget) {
      return nothing;
    }
    return html`<widget-placeholder
      .prompt=${widget.prompt}
      .error=${widget.error}
      @WidgetPlaceholder:Edit=${this.#editPrompt}
    ></widget-placeholder>`;
  }

  #openLearnMore(e) {
    e.preventDefault();
    this.#openUrl(e.currentTarget.href);
  }

  #openUrl(url) {
    this.topWindow.openTrustedLinkIn(url, "tab");
  }

  #openSupportPage() {
    this.#openUrl(
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
        CUSTOM_WIDGETS_SUPPORT_PAGE
    );
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
          support-page=${CUSTOM_WIDGETS_SUPPORT_PAGE}
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

  get #composerBlocked() {
    return this.widgets.generating || !!this.widgets.failed;
  }

  composerTemplate() {
    if (!this.authentication.authenticated) {
      return nothing;
    }
    if (this.composing) {
      return html`<widget-composer
        autofocus
        .submitDisabled=${this.#composerBlocked}
        @WidgetComposer:Dismiss=${this.#closeComposer}
        @WidgetComposer:Submit=${this.#create}
      ></widget-composer>`;
    }
    if (this.widgets.failed) {
      return nothing;
    }
    return html`<moz-button
      id="compose-button"
      iconsrc=${SUBMIT_ICON}
      data-l10n-id="widget-studio-compose-button"
      ?disabled=${this.widgets.generating}
      @click=${this.#openComposer}
    ></moz-button>`;
  }

  renderSlotted() {
    return html`${this.cardsTemplate()}`;
  }

  render() {
    return html`<link
        rel="stylesheet"
        href="chrome://browser/content/widgets/sidebar-widget-studio.css"
      />
      ${this.signInTemplate()}
      <div class="widgets" ?hidden=${!this.widgets.all.length}>
        <slot></slot>
      </div>
      ${this.placeholderTemplate()} ${this.composerTemplate()}`;
  }
}

customElements.define("sidebar-widget-studio", SidebarWidgetStudio);
