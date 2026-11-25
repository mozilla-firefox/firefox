/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  live,
  nothing,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-textarea.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-input-url.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-select.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button-group.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-chip.mjs";

// Storybook's ChromeUtils stand-in can't load system modules, so the
// defaults are a copy of the values for it.
const {
  DIGEST_LENGTH = { STANDARD: "standard", SHORT: "short" },
  DIGEST_SCHEDULE_TYPE = {
    DAILY: "daily",
    WEEKLY: "weekly",
    ON_DEMAND: "on-demand",
  },
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  DigestManager:
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

const DIGEST_FORM_MODES = Object.freeze({
  DEFAULT: "default",
  EDIT: "edit",
});

const FREQUENCY_L10N_IDS = Object.freeze({
  [DIGEST_SCHEDULE_TYPE.DAILY]: "smartwindow-digest-form-frequency-daily",
  [DIGEST_SCHEDULE_TYPE.WEEKLY]: "smartwindow-digest-form-frequency-weekly",
  [DIGEST_SCHEDULE_TYPE.ON_DEMAND]:
    "smartwindow-digest-form-frequency-on-demand",
});

const LENGTH_L10N_IDS = Object.freeze({
  [DIGEST_LENGTH.STANDARD]: "smartwindow-digest-form-length-standard",
  [DIGEST_LENGTH.SHORT]: "smartwindow-digest-form-length-short",
});

const DEFAULT_HOUR = 9;
const INFO_ICON = "chrome://global/skin/icons/info.svg";

// Services.intl follows the OS clock setting, so a 24-hour clock is used
// wherever the user's locale or system prefers one. Storybook has no Services.
const timeFormat = new (globalThis.Services?.intl ?? Intl).DateTimeFormat(
  undefined,
  { timeStyle: "short" }
);
const HOUR_OPTIONS = Array.from({ length: 24 }, (_, hour) => {
  const date = new Date();
  date.setHours(hour, 0, 0, 0);
  return { value: String(hour), label: timeFormat.format(date) };
});

// Indexed by DigestSchedule's weekday (0 = Sunday). 2026-01-04 is a Sunday.
const WEEKDAY_OPTIONS = Array.from({ length: 7 }, (_, weekday) => ({
  value: String(weekday),
  label: new Date(2026, 0, 4 + weekday).toLocaleDateString(undefined, {
    weekday: "long",
  }),
}));

/**
 * The settings form for a stored digest, for turning on a suggested one or
 * editing an enabled one.
 * It is host-agnostic so it can sit in a modal or, later, in chat. It saves
 * through DigestManager and reports the outcome with bubbling events:
 *  - digest-form:save-success, detail: { digest, mode, length }
 *  - digest-form:save-error, detail: { error, mode }
 *  - digest-form:cancel
 *
 * @property {"default"|"edit"} mode - "default" enables a suggested digest,
 *   "edit" saves changes to an enabled one.
 * @property {?object} digest - The stored digest, from DigestManager. See
 *   Digest in DigestStore.sys.mjs for the shape.
 */
export class SmartWindowDigestForm extends MozLitElement {
  static properties = {
    mode: { type: String, reflect: true },
    digest: { type: Object },
    description: { state: true },
    domains: { state: true },
    pendingUrl: { state: true },
    frequency: { state: true },
    weekday: { state: true },
    hour: { state: true },
    length: { state: true },
    fieldErrors: { state: true },
    saving: { state: true },
  };

  constructor() {
    super();
    this.mode = DIGEST_FORM_MODES.DEFAULT;
    this.digest = null;
    this.#reset();
  }

  #reset() {
    const digest = this.digest ?? {};
    // A new digest starts blank so the user writes what it covers.
    this.description = this.#isEdit ? (digest.description ?? "") : "";
    this.domains = [...(digest.domains ?? [])];
    this.pendingUrl = "";
    // A new digest starts on Daily
    const schedule = this.#isEdit ? digest.schedule : {};
    this.frequency = schedule.type ?? DIGEST_SCHEDULE_TYPE.DAILY;
    this.weekday = schedule.weekday ?? new Date().getDay();
    this.hour = schedule.hour ?? DEFAULT_HOUR;
    // Not saved yet: DigestStore gets a length field in bug 2085182.
    this.length = DIGEST_LENGTH.STANDARD;
    this.fieldErrors = { description: null, sources: null };
    this.saving = false;
  }

  willUpdate(changedProperties) {
    if (changedProperties.has("digest") || changedProperties.has("mode")) {
      this.#reset();
    }
    if (changedProperties.has("domains")) {
      this.#findIconPages();
    }
  }

  // Page each source chip takes its favicon from, by domain.
  #iconPages = new Map();

  #iconPage(domain) {
    return this.#iconPages.get(domain) ?? `https://${domain}/`;
  }

  // Sources are stored without "www.", but sites served from www keep their
  // favicon under the www host, so fall back to it when the bare host has none.
  async #findIconPages() {
    if (!lazy.PlacesUtils) {
      return;
    }
    for (const domain of this.domains) {
      if (this.#iconPages.has(domain)) {
        continue;
      }
      this.#iconPages.set(domain, `https://${domain}/`);
      try {
        const [bare, www] = await Promise.all(
          [domain, `www.${domain}`].map(host =>
            lazy.PlacesUtils.favicons.getFaviconForPage(
              Services.io.newURI(`https://${host}/`)
            )
          )
        );
        if (!bare && www) {
          this.#iconPages.set(domain, `https://www.${domain}/`);
          this.requestUpdate();
        }
      } catch (error) {
        console.error("Failed to look up a source's favicon:", error);
      }
    }
  }

  #onMemoryInfoClick(event) {
    if (!event.target.closest("a")) {
      return;
    }
    event.preventDefault();
    this.shadowRoot.querySelector(".memory-tooltip")?.hidePopover();
    window.browsingContext?.topChromeWindow?.openPreferences("manageMemories");
  }

  get #isEdit() {
    return this.mode === DIGEST_FORM_MODES.EDIT;
  }

  #dispatch(type, detail) {
    this.dispatchEvent(
      new CustomEvent(type, { detail, bubbles: true, composed: true })
    );
  }

  #setFieldError(key, error) {
    if (this.fieldErrors[key] !== error) {
      this.fieldErrors = { ...this.fieldErrors, [key]: error };
    }
  }

  #onDescriptionInput(event) {
    this.description = event.target.value;
    this.#setFieldError("description", null);
  }

  // Normalize a user-entered address to an http(s) URL. A value with no scheme
  // (e.g. "cnn.com") gets https. Returns "" if it isn't a website.
  #normalizeUrl(url) {
    const value = url.trim();
    if (!value) {
      return "";
    }
    const candidate = value.includes("://") ? value : `https://${value}`;
    try {
      const { protocol, host } = new URL(candidate);
      if (
        (protocol === "http:" || protocol === "https:") &&
        this.#isWebsite(host)
      ) {
        return new URL(candidate).href;
      }
    } catch {}
    return "";
  }

  // Reject single words, IP addresses, ports and local names like localhost.
  #isWebsite(host) {
    if (!lazy.UrlUtils) {
      return !!host;
    }
    return (
      lazy.UrlUtils.looksLikeOrigin(host, { noIp: true, noPort: true }) ===
      lazy.UrlUtils.LOOKS_LIKE_ORIGIN.OTHER
    );
  }

  // Adds the typed URL as a source. Returns false if it was rejected.
  #tryAddPendingUrl() {
    if (!this.pendingUrl.trim()) {
      return true;
    }
    const normalized = this.#normalizeUrl(this.pendingUrl);
    if (!normalized) {
      this.#setFieldError("sources", "smartwindow-digest-form-error-url");
      return false;
    }
    const domain = new URL(normalized).hostname.replace(/^www\./, "");
    if (!this.domains.includes(domain)) {
      this.domains = [...this.domains, domain];
    }
    this.pendingUrl = "";
    this.#setFieldError("sources", null);
    return true;
  }

  #onPendingUrlInput(event) {
    this.pendingUrl = event.target.value;
    this.#setFieldError("sources", null);
  }

  #onPendingUrlKeydown(event) {
    if (event.key === "Enter") {
      event.preventDefault();
      this.#tryAddPendingUrl();
    }
  }

  #removeDomain(domain) {
    this.domains = this.domains.filter(d => d !== domain);
    this.shadowRoot.querySelector(".sources-input")?.focus();
  }

  #onFrequencyChange(event) {
    this.frequency = event.target.value;
  }

  #onWeekdayChange(event) {
    this.weekday = Number(event.target.value);
  }

  #onHourChange(event) {
    this.hour = Number(event.target.value);
  }

  #onLengthChange(event) {
    this.length = event.target.value;
  }

  #buildSchedule() {
    switch (this.frequency) {
      case DIGEST_SCHEDULE_TYPE.ON_DEMAND:
        return { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND };
      case DIGEST_SCHEDULE_TYPE.WEEKLY:
        return {
          type: DIGEST_SCHEDULE_TYPE.WEEKLY,
          weekday: this.weekday,
          hour: this.hour,
          minute: 0,
        };
      default:
        return { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: this.hour, minute: 0 };
    }
  }

  // Returns the selector of the first invalid field, or null if all are valid.
  #validate() {
    const sourcesValid = this.#tryAddPendingUrl();
    if (!this.description.trim()) {
      this.#setFieldError(
        "description",
        "smartwindow-digest-form-error-description"
      );
      return ".description-input";
    }
    return sourcesValid ? null : ".sources-input";
  }

  async #save(fields) {
    const manager = lazy.DigestManager;
    if (this.#isEdit) {
      return manager.editDigest(this.digest.id, fields);
    }
    await manager.editDigest(this.digest.id, fields);
    return manager.enableDigest(this.digest.id);
  }

  // moz-button can't submit a form itself, so Create asks the form to.
  #onSubmitClick() {
    this.shadowRoot.querySelector("form").requestSubmit();
  }

  async #onSubmit(event) {
    event.preventDefault();
    if (this.saving) {
      return;
    }
    const invalidSelector = this.#validate();
    if (invalidSelector) {
      await this.updateComplete;
      this.shadowRoot.querySelector(invalidSelector)?.focus();
      return;
    }

    const fields = {
      title: this.digest?.title,
      description: this.description.trim(),
      domains: this.domains,
      schedule: this.#buildSchedule(),
    };
    this.saving = true;
    try {
      const digest = await this.#save(fields);
      if (!digest) {
        throw new Error("Digest not found");
      }
      this.#dispatch("digest-form:save-success", {
        digest,
        mode: this.mode,
        length: this.length,
      });
    } catch (error) {
      console.error("Failed to save digest:", error);
      this.#dispatch("digest-form:save-error", { error, mode: this.mode });
    } finally {
      this.saving = false;
    }
  }

  #onCancel() {
    this.#dispatch("digest-form:cancel", {});
  }

  #renderFieldError(id) {
    return id
      ? html`<div class="error-message" data-l10n-id=${id}></div>`
      : nothing;
  }

  #renderSelect(l10nId, value, onChange, options) {
    return html`<moz-select
      data-l10n-id=${l10nId}
      data-l10n-attrs="label"
      .value=${String(value)}
      @change=${onChange}
    >
      ${options.map(
        ({ value: optionValue, l10nId: optionL10nId, label }) =>
          html`<moz-option
            value=${optionValue}
            data-l10n-id=${optionL10nId ?? nothing}
            label=${label ?? nothing}
          ></moz-option>`
      )}
    </moz-select>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-digest-form.css"
      />
      <form aria-labelledby="heading" novalidate @submit=${this.#onSubmit}>
        <header>
          <h2
            id="heading"
            data-l10n-id=${this.#isEdit
              ? "smartwindow-digest-form-edit-heading"
              : "smartwindow-digest-form-create-heading"}
          ></h2>
          <p
            data-l10n-id=${this.#isEdit
              ? "smartwindow-digest-form-edit-subheading"
              : "smartwindow-digest-form-create-subheading"}
          ></p>
        </header>

        <div class="field field-topic">
          <span
            class="field-label"
            data-l10n-id="smartwindow-digest-form-topic-label"
          ></span>
          <div class="field-input field-readonly">
            <span>${this.digest?.title}</span>
            <moz-button
              class="memory-tooltip-trigger"
              type="icon ghost"
              size="small"
              iconsrc=${INFO_ICON}
              popovertarget="memory-tooltip"
              data-l10n-id="smartwindow-digest-form-memory-tooltip-trigger"
              data-l10n-attrs="aria-label"
            ></moz-button>
          </div>
          <div
            id="memory-tooltip"
            class="memory-tooltip"
            popover
            @click=${this.#onMemoryInfoClick}
          >
            <p data-l10n-id="smartwindow-digest-form-memory-tooltip">
              <a
                href="about:preferences#manageMemories"
                data-l10n-name="memories-link"
              ></a>
            </p>
          </div>
        </div>

        <div
          class="field field-description ${this.fieldErrors.description
            ? "field-error"
            : ""}"
        >
          <moz-textarea
            class="description-input"
            data-l10n-id="smartwindow-digest-form-description"
            data-l10n-attrs="label,placeholder"
            required
            ?invalid=${!!this.fieldErrors.description}
            .value=${live(this.description)}
            @input=${this.#onDescriptionInput}
          ></moz-textarea>
          ${this.#renderFieldError(this.fieldErrors.description)}
        </div>

        <div
          class="field field-sources ${this.fieldErrors.sources
            ? "field-error"
            : ""}"
        >
          <moz-input-url
            class="sources-input"
            data-l10n-id="smartwindow-digest-form-sources"
            data-l10n-attrs="label,placeholder"
            ?invalid=${!!this.fieldErrors.sources}
            .value=${live(this.pendingUrl)}
            @input=${this.#onPendingUrlInput}
            @keydown=${this.#onPendingUrlKeydown}
          ></moz-input-url>
          ${this.#renderFieldError(this.fieldErrors.sources)}
          ${this.domains.length
            ? html`<div class="chips">
                ${this.domains.map(
                  domain =>
                    html`<smartwindow-chip
                      type="source"
                      removable
                      label=${domain}
                      .iconSrc=${`page-icon:${this.#iconPage(domain)}`}
                      @smartwindow-chip:remove=${() =>
                        this.#removeDomain(domain)}
                    ></smartwindow-chip>`
                )}
              </div>`
            : nothing}
        </div>

        <div class="field-row">
          ${this.#renderSelect(
            "smartwindow-digest-form-frequency",
            this.frequency,
            this.#onFrequencyChange,
            Object.values(DIGEST_SCHEDULE_TYPE).map(value => ({
              value,
              l10nId: FREQUENCY_L10N_IDS[value],
            }))
          )}
          ${this.frequency === DIGEST_SCHEDULE_TYPE.WEEKLY
            ? this.#renderSelect(
                "smartwindow-digest-form-day",
                this.weekday,
                this.#onWeekdayChange,
                WEEKDAY_OPTIONS
              )
            : nothing}
        </div>
        ${this.frequency === DIGEST_SCHEDULE_TYPE.ON_DEMAND
          ? nothing
          : this.#renderSelect(
              "smartwindow-digest-form-time",
              this.hour,
              this.#onHourChange,
              HOUR_OPTIONS
            )}
        ${this.#renderSelect(
          "smartwindow-digest-form-length",
          this.length,
          this.#onLengthChange,
          Object.values(DIGEST_LENGTH).map(value => ({
            value,
            l10nId: LENGTH_L10N_IDS[value],
          }))
        )}

        <p
          class="required-note"
          data-l10n-id="smartwindow-digest-form-required-note"
        ></p>

        <moz-button-group>
          <moz-button
            data-l10n-id="smartwindow-digest-form-cancel"
            @click=${this.#onCancel}
          ></moz-button>
          <moz-button
            class="submit-button"
            type="primary"
            data-l10n-id=${this.#isEdit
              ? "smartwindow-digest-form-save"
              : "smartwindow-digest-form-create"}
            ?disabled=${this.saving}
            @click=${this.#onSubmitClick}
          ></moz-button>
        </moz-button-group>
      </form>
    `;
  }
}

customElements.define("smartwindow-digest-form", SmartWindowDigestForm);
