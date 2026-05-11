/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { countryName } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

/**
 * A custom element that renders the single-select list of egress locations.
 */
export default class LocationsList extends MozLitElement {
  static properties = {
    locations: { type: Array },
    selectedLocation: { type: String, state: true },
  };

  static defaultLocation = "REC";

  static collator = new Intl.Collator(undefined, { sensitivity: "base" });

  get #sortedLocations() {
    return Array.from(this.locations ?? []).sort((a, b) => {
      const nameA = countryName(a.code) ?? a.code;
      const nameB = countryName(b.code) ?? b.code;
      return LocationsList.collator.compare(nameA, nameB);
    });
  }

  constructor() {
    super();
    this.selectedLocation = "";
    this.locations = [];
  }

  createRenderRoot() {
    return this;
  }

  connectedCallback() {
    super.connectedCallback();
  }

  getSelectedLocation() {
    if (
      !this.selectedLocation ||
      (this.selectedLocation !== LocationsList.defaultLocation &&
        !this.locations?.some(l => l.code === this.selectedLocation))
    ) {
      return LocationsList.defaultLocation;
    }
    return this.selectedLocation;
  }

  handleSelectLocation(code) {
    if (this.selectedLocation === code) {
      return;
    }
    this.selectedLocation = code;
    this.dispatchEvent(
      new CustomEvent("IPProtection:UserSelectLocation", {
        bubbles: true,
        composed: true,
        detail: { code },
      })
    );
  }

  #locationRow(aLocation) {
    const isSelected = aLocation.code === this.getSelectedLocation();
    return html`
      <li role="presentation">
        <button
          class="location-item subviewbutton"
          role="radio"
          id="location-option-${aLocation.code}"
          aria-checked=${isSelected ? "true" : "false"}
          @click=${() => this.handleSelectLocation(aLocation.code)}
          ?disabled=${!aLocation.available}
        >
          <img
            class="location-check"
            src="chrome://global/skin/icons/check.svg"
            aria-hidden="true"
          />
          <span class="location-label-group">
            ${aLocation.code === LocationsList.defaultLocation
              ? html`<span
                    class="location-label"
                    data-l10n-id="ipprotecion-locations-subview-recommended-label"
                  ></span>
                  <span
                    class="location-description"
                    data-l10n-id="ipprotection-locations-subview-recommended-description"
                  ></span>`
              : html`<span class="location-label"
                  >${countryName(aLocation.code)}</span
                >`}
          </span>
          <!--TODO: append an "unavailable" label if a location option is considered disabled-->
        </button>
      </li>
    `;
  }

  render() {
    const recommendedLocation = {
      code: LocationsList.defaultLocation,
      available: true,
    };

    return html`
      <div id="locations-list-wrapper">
        <span
          id="locations-list-description"
          data-l10n-id="ipprotection-locations-subview-description"
        ></span>
        <ul
          id="locations-list"
          role="radiogroup"
          aria-labelledby="locations-list-description"
        >
          ${this.#locationRow(recommendedLocation)}
          ${this.#sortedLocations.map(aLocation =>
            this.#locationRow(aLocation)
          )}
        </ul>
      </div>
    `;
  }
}

customElements.define("locations-list", LocationsList);
