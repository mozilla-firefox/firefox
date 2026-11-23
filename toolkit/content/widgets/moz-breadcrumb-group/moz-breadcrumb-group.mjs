/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-breadcrumb-group.tagmap.d.ts" />

import { html } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded(
  "toolkit/global/mozBreadcrumbGroup.ftl"
);

/**
 * @tagname moz-breadcrumb
 */

export class MozBreadcrumb extends MozLitElement {
  static properties = {
    href: { type: String },
    label: { type: String, fluent: true },
    heading: { type: String, fluent: true },
    ariaCurrent: { attribute: "aria-current", type: String },
  };

  constructor() {
    super();

    /**
     * The href of the breadcrumb link.
     *
     * @type {string}
     */
    this.href = "";

    /**
     * The label text.
     *
     * @type {string}
     */
    this.label = "";

    /**
     * Heading text used as the label when no label is set.
     *
     * @type {string}
     */
    this.heading = "";

    /**
     * The aria-current value for the breadcrumb of the current page.
     *
     * @type {string | undefined}
     */
    this.ariaCurrent = undefined;
  }

  render() {
    const labelTemplate = this.label || this.heading || html`<slot></slot>`;
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-breadcrumb.css"
      />
      ${this.ariaCurrent
        ? labelTemplate
        : html`<a href=${this.href}>${labelTemplate}</a>`}
    `;
  }
}

customElements.define("moz-breadcrumb", MozBreadcrumb);

/**
 * @tagname moz-breadcrumb-group
 */
export class MozBreadcrumbGroup extends MozLitElement {
  /**
   * @type {MutationObserver | void}
   */
  #observer;

  /**
   * Fired when a breadcrumb is either
   * added or removed from the DOM, at which
   * an update is requested to re-render breadcrumbs.
   *
   * @type {MutationCallback}
   */
  #onBreadcrumbMutation(mutations) {
    for (const mutation of mutations) {
      if (mutation.type === "childList") {
        this.requestUpdate();
      }
    }
  }

  /**
   * @type {Array<MozBreadcrumb>}
   */
  get breadcrumbs() {
    /**
     * @type {NodeListOf<MozBreadcrumb>}
     */
    const breadcrumbElements = this.querySelectorAll("moz-breadcrumb");
    return Array.from(breadcrumbElements);
  }

  setupBreadcrumbs() {
    const { breadcrumbs } = this;
    return breadcrumbs.map((breadcrumb, i) => {
      breadcrumb.setAttribute("slot", i + "");

      if (i === breadcrumbs.length - 1) {
        breadcrumb.setAttribute("aria-current", "page");
      }

      return breadcrumb;
    });
  }

  update() {
    super.update();
    this.setupBreadcrumbs();
  }

  firstUpdated() {
    if (!this.#observer) {
      this.#observer = new MutationObserver((mutations, observer) =>
        this.#onBreadcrumbMutation(mutations, observer)
      );
      this.#observer.observe(this, {
        childList: true,
      });
    }
  }

  disconnectedCallback() {
    if (this.#observer) {
      this.#observer.disconnect();
      this.#observer = undefined;
    }
    super.disconnectedCallback();
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-breadcrumb-group.css"
      />
      <nav data-l10n-id="moz-breadcrumb-group-nav">
        <ol>
          ${this.breadcrumbs.map((breadcrumb, i) => {
            return html`<li>
              <slot name=${i}></slot>
            </li>`;
          })}
        </ol>
      </nav>
    `;
  }
}

customElements.define("moz-breadcrumb-group", MozBreadcrumbGroup);
