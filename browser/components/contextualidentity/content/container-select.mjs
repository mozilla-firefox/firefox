/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import MozSelect from "chrome://global/content/elements/moz-select.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
});

/**
 * The attributes of the moz-option children a container-select expects, one
 * entry per container.
 *
 * @returns {Array<Record<string, string>>}
 */
export function containerOptions() {
  return lazy.ContextualIdentityService.getPublicIdentities().map(identity => ({
    value: String(identity.userContextId),
    label: lazy.ContextualIdentityService.getUserContextLabel(
      identity.userContextId
    ),
    iconsrc: lazy.ContextualIdentityService.getContainerIconURL(identity.icon),
    itemclass: `identity-color-${identity.color}`,
  }));
}

/**
 * A select listing the containers, its value being a userContextId. The caller
 * supplies the moz-option children, see containerOptions.
 *
 * @tagname container-select
 * @property {string} site
 *   Host bound to the picked container. Without one the element only reports
 *   the choice through its change event.
 */
export default class ContainerSelect extends MozSelect {
  static properties = {
    site: { type: String },
  };

  // moz-select only watches the attributes it reads itself.
  #colorObserver = new MutationObserver(() => this.populateOptions());

  constructor() {
    super();
    this.site = "";
    this.addEventListener("change", () => this.#bindSite());
  }

  /**
   * @type {MozSelect['firstUpdated']}
   */
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.#colorObserver.observe(this, {
      attributeFilter: ["itemclass"],
      subtree: true,
    });
  }

  /**
   * @type {MozSelect['inputStylesTemplate']}
   */
  inputStylesTemplate() {
    return html`${super.inputStylesTemplate()}
      <link
        rel="stylesheet"
        href="chrome://browser/content/usercontext/usercontext.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/usercontext/container-select.css"
      />`;
  }

  /**
   * @type {MozSelect['populateOptions']}
   */
  populateOptions() {
    super.populateOptions();
    let nodes = this.slotRef.value
      ?.assignedNodes()
      .filter(node => ["moz-option", "hr"].includes(node.localName));
    this.options = this.options.map((option, index) => ({
      ...option,
      itemClass: nodes?.[index]?.getAttribute("itemclass") ?? "",
    }));
  }

  /**
   * @type {MozSelect['willUpdate']}
   */
  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);

    // The color class carries the identity variables the icon is painted with.
    for (let cls of [...this.classList]) {
      if (cls.startsWith("identity-color-")) {
        this.classList.remove(cls);
      }
    }
    if (this.selectedOption?.itemClass) {
      this.classList.add(this.selectedOption.itemClass);
    }
  }

  /**
   * The base template renders the panel rows and binds no class on them, so the
   * color class is applied once they exist.
   *
   * @type {MozSelect['updated']}
   */
  updated(changedProperties) {
    super.updated(changedProperties);
    for (let [index, item] of [
      ...(this.panelList?.querySelectorAll("panel-item") ?? []),
    ].entries()) {
      item.classList.remove(
        ...[...item.classList].filter(cls => cls.startsWith("identity-color-"))
      );
      if (this.options[index]?.itemClass) {
        item.classList.add(this.options[index].itemClass);
      }
    }
  }

  #bindSite() {
    let userContextId = parseInt(this.value, 10);
    if (this.site && userContextId) {
      lazy.ContextualIdentityService.setSiteAssociation(
        this.site,
        userContextId
      );
    }
  }
}

customElements.define("container-select", ContainerSelect);
