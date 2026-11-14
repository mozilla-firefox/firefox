/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  LitElement,
  html,
  classMap,
  css,
} from "chrome://global/content/vendor/lit.all.mjs";
import { chromeMap } from "../.storybook/chrome-map.js";

/**
 * @import { MozMessageBar } from "chrome://global/content/elements/moz-message-bar.mjs"
 * @import { MozToggle } from "chrome://global/content/elements/moz-toggle.mjs"
 *
 * @typedef {string} FolderPath An in-tree folder path that has relevant files.
 * @typedef {string} FilePath An in-tree path to a file.
 * @typedef {string} FileName An in-tree file name (leaf name e.g. arrow.svg).
 * @typedef {string} ChromeUri A chrome:// URI
 * @typedef {{ chromeUri: ChromeUri, fileName: FileName, filePath: FilePath }} FileInfo
 * Info about a file in the chrome-map.
 * @typedef {Map<FolderPath, FileInfo[]>} BundleFileInfoMap
 * Map of chrome URI bundle prefix to file info objects.
 * @typedef {Values<IconSize>} IconSizeValue
 */

const SUCCESS_MESSAGE_DURATION_MS = 3000;
const IconSize = Object.freeze({
  Normalize: "normalize",
  Full: "full",
});

export default {
  title: "Docs/Icon Directory",
  parameters: {
    options: { showPanel: false },
    docs: { source: { state: "closed" } },
  },
};

/**
 * Group the icons for display.
 *
 * @param {BundleFileInfoMap} bundleFileInfoMap
 * @returns {BundleFileInfoMap}
 */
function prioritizeGroups(bundleFileInfoMap) {
  /** @type {FolderPath[]} */
  let bundleGroupings = [
    "browser/themes",
    "toolkit/themes",
    "browser/components",
    "toolkit/components",
  ];
  let newGroups = new Map(
    bundleGroupings.map(
      /**
       * @param {FolderPath} bg
       * @returns {[FolderPath, FileInfo[]]}
       */
      bg => [bg, []]
    )
  );
  for (let group of bundleFileInfoMap.keys()) {
    let bundleGroup = bundleGroupings.find(bg => group.startsWith(bg)) || group;
    if (!newGroups.has(bundleGroup)) {
      newGroups.set(bundleGroup, []);
    }
    newGroups.get(bundleGroup).push(...bundleFileInfoMap.get(group));
  }
  return newGroups;
}

/**
 * Build icon data from chrome-map, organizing icons by directory.
 *
 * @returns {BundleFileInfoMap}
 */
function buildIconData() {
  const [prefixMap, , sourceMap] = chromeMap;

  // Build reverse lookup: bundleDir -> chromePrefix
  const reversePrefixMap = new Map();
  for (const [chromePrefix, bundleDirs] of Object.entries(prefixMap)) {
    for (const dir of bundleDirs) {
      reversePrefixMap.set(dir, chromePrefix);
    }
  }

  /**
   * @param {FilePath} bundlePath
   * @returns {ChromeUri | null}
   */
  function resolveToChrome(bundlePath) {
    let dirPath = bundlePath;
    while (dirPath.includes("/")) {
      let lastSlash = dirPath.lastIndexOf("/");
      let dir = dirPath.substring(0, lastSlash);
      let remainder = bundlePath.substring(dir.length + 1);
      let chromePrefix = reversePrefixMap.get(dir);
      if (chromePrefix) {
        return chromePrefix + remainder;
      }
      dirPath = dir;
    }
    return null;
  }

  /** @type {BundleFileInfoMap} */
  const bundleFileInfoMap = new Map();
  for (const [bundlePath, [srcPath]] of Object.entries(sourceMap)) {
    if (!bundlePath.endsWith(".svg")) {
      continue;
    }
    let chromeUri = resolveToChrome(bundlePath);
    if (
      !chromeUri ||
      !(
        chromeUri.startsWith("chrome://browser/") ||
        chromeUri.startsWith("chrome://global/")
      )
    ) {
      continue;
    }
    let lastSlash = srcPath.lastIndexOf("/");
    let bundleDir = srcPath.substring(0, lastSlash);
    let fileName = srcPath.substring(lastSlash + 1);
    if (!bundleFileInfoMap.has(bundleDir)) {
      bundleFileInfoMap.set(bundleDir, []);
    }
    bundleFileInfoMap
      .get(bundleDir)
      .push({ chromeUri, fileName, filePath: srcPath });
  }

  for (const icons of bundleFileInfoMap.values()) {
    icons.sort((a, b) => a.fileName.localeCompare(b.fileName));
  }

  return prioritizeGroups(bundleFileInfoMap);
}

const iconData = buildIconData();

/**
 * Convert RGB color string to hex format.
 *
 * @param {string} rgb A CSS RGB value.
 * @returns {string} A CSS hex value.
 */
function rgbToHex(rgb) {
  let match = rgb.match(/\d+/g);
  if (!match || match.length < 3) {
    return "#000000";
  }
  return (
    "#" +
    match
      .slice(0, 3)
      .map(n => parseInt(n, 10).toString(16).padStart(2, "0"))
      .join("")
  );
}

/**
 * Browsable, searchable directory of SVG icons available in the Firefox
 * codebase, grouped by source directory with chrome:// URI previews.
 *
 * @property {string} filter Current search filter text
 * @property {string} fillColor Hex color for icon fill
 * @property {string} strokeColor Hex color for icon stroke
 * @property {IconSizeValue} iconSize Icon display size mode
 */
class IconDirectory extends LitElement {
  static properties = {
    filter: { type: String, state: true },
    fillColor: { type: String, state: true },
    strokeColor: { type: String, state: true },
    iconSize: { type: String, reflect: true },
  };

  static styles = css`
    :host {
      --icon-item-width: 125px;
      --icon-item-padding: var(--space-small);
      --icon-item-fill: var(--icon-color);
      /*
       * The rgbToHex function doesn't work with oklch...
       * Use --color-accent-primary manual conversion to hex so the stroke is more visible.
       */
      --icon-item-stroke: light-dark(#0062fa, #00cadb);
      --icon-item-width-content: calc(
        var(--icon-item-width) - 2 * var(--icon-item-padding)
      );

      display: flex;
      flex-direction: column;
      gap: var(--space-large);
    }

    .sticky-header {
      position: sticky;
      inset-block-start: 0;
      background-color: var(--background-color-canvas);
      z-index: 1;
      margin-inline: calc(-1 * var(--space-large));
      padding-inline: var(--space-large);
      padding-block-end: var(--space-small);
      display: flex;
      flex-direction: column;
      gap: var(--space-small);
      border-bottom: var(--border-width) solid var(--border-color);
    }

    #color-probe {
      display: none;
      fill: var(--icon-item-fill);
      stroke: var(--icon-item-stroke);
    }

    moz-box-item {
      --box-padding: var(--icon-item-padding);
    }

    .view-controls {
      display: flex;
      align-items: center;
      gap: var(--space-large);
    }

    .icon-list {
      display: grid;
      grid-template-columns: repeat(
        auto-fill,
        minmax(var(--icon-item-width), 1fr)
      );
      gap: var(--space-small);
    }

    .icon-item {
      display: flex;
      flex-direction: column;
      align-items: center;

      img {
        -moz-context-properties: fill, stroke;
        fill: var(--icon-item-fill);
        stroke: var(--icon-item-stroke);

        :host([iconsize="normalize"]) & {
          width: 16px;
          aspect-ratio: 1;

          &.icon-12 {
            width: 12px;
          }

          &.icon-24 {
            width: 24px;
          }
        }

        :host([iconsize="full"]) & {
          max-width: var(--icon-item-width-content);
        }
      }
    }

    .icon-name {
      font-size: var(--font-size-small);
      width: var(--icon-item-width-content);
      margin: var(--icon-item-padding) auto 0;

      button& {
        appearance: none;
        border: none;
        background: none;
        cursor: pointer;
        padding: 0;
      }
    }

    #success-message {
      position-area: end center;
      margin: 0;
      transition: opacity 250ms;
      box-shadow: var(--box-shadow-popup);

      &:not(:popover-open) {
        display: none;
      }

      &[hiding] {
        opacity: 0;
      }
    }
  `;

  constructor() {
    super();
    this.filter = "";
    this.fillColor = "";
    this.strokeColor = "";
    /** @type {IconSizeValue} */
    this.iconSize = IconSize.Normalize;
    /** @type {number | undefined} */
    this._successMessageTimeout = undefined;
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    clearTimeout(this._successMessageTimeout);
  }

  /**
   * Initialize color values from computed styles on first render.
   */
  firstUpdated() {
    let probe = this.renderRoot.querySelector("#color-probe");
    let probeStyles = getComputedStyle(probe);
    this.fillColor = rgbToHex(probeStyles.fill);
    this.strokeColor = rgbToHex(probeStyles.stroke);
  }

  /** @type {MozMessageBar} */
  get successMessageBar() {
    return this.renderRoot.querySelector("#success-message");
  }

  /** @param {CustomEvent & { query: string }} e */
  handleSearch(e) {
    this.filter = e.detail.query.toLowerCase();
  }

  /** @param {InputEvent & { target: HTMLInputElement }} e */
  handleFillChange(e) {
    this.fillColor = e.target.value;
    this.style.setProperty("--icon-item-fill", this.fillColor);
  }

  /** @param {InputEvent & { target: HTMLInputElement }} e */
  handleStrokeChange(e) {
    this.strokeColor = e.target.value;
    this.style.setProperty("--icon-item-stroke", this.strokeColor);
  }

  /** @param {MouseEvent & { target: HTMLButtonElement }} e */
  async handleCopy({ target }) {
    try {
      this.hideSuccessMessage(true);
      await navigator.clipboard.writeText(target.dataset.url);
      this.showSuccessMessage(target);
    } catch (error) {
      console.error("Failed to copy to clipboard:", error);
    }
  }

  /**
   * Show the success message popover, then hide it after SUCCESS_MESSAGE_DURATION_MS.
   *
   * @param {HTMLElement} source The element that triggered the success message.
   */
  showSuccessMessage(source) {
    this.successMessageBar.removeAttribute("hiding");
    this.successMessageBar.showPopover({ source });
    clearTimeout(this._successMessageTimeout);
    this._successMessageTimeout = setTimeout(() => {
      this.hideSuccessMessage();
    }, SUCCESS_MESSAGE_DURATION_MS);
  }

  /**
   * Hide the success message popover with fade-out animation.
   *
   * @param {boolean} instant If we should instantly hide (default: false).
   */
  hideSuccessMessage(instant = false) {
    if (instant) {
      this.successMessageBar.hidePopover();
      return;
    }
    this.successMessageBar.setAttribute("hiding", "");
    this.successMessageBar.addEventListener(
      "transitionend",
      () => {
        this.successMessageBar.hidePopover();
        this.successMessageBar.removeAttribute("hiding");
      },
      { once: true }
    );
  }

  /**
   * Filter icons based on current search query.
   *
   * @param {FileInfo[]} icons
   * @returns {FileInfo[]}
   */
  filteredIcons(icons) {
    if (!this.filter) {
      return icons;
    }
    return icons.filter(
      ({ chromeUri, filePath }) =>
        filePath.toLowerCase().includes(this.filter) ||
        chromeUri.toLowerCase().includes(this.filter)
    );
  }

  /**
   * @param {FolderPath} dirKey
   * @param {FileInfo[]} icons
   */
  iconGroupTemplate(dirKey, icons) {
    let filtered = this.filteredIcons(icons);
    if (!filtered.length) {
      return "";
    }
    return html`
      <moz-card .heading=${dirKey} type="accordion" expanded>
        ${html`
          <div class="icon-list">
            ${filtered.map(
              ({ chromeUri, fileName }) => html`
                <moz-box-item>
                  <div class="icon-item">
                    <img
                      class=${classMap({
                        "icon-12": fileName.endsWith("-12.svg"),
                        "icon-24": fileName.endsWith("-24.svg"),
                      })}
                      src=${chromeUri}
                      alt=""
                    />
                    <button
                      class="icon-name text-truncated-ellipsis"
                      data-url=${chromeUri}
                      @click=${this.handleCopy}
                      aria-description="Copy chrome URL"
                      aria-expanded="false"
                      title=${`${fileName} — Copy chrome:// URL`}
                    >
                      ${fileName}
                    </button>
                  </div>
                </moz-box-item>
              `
            )}
          </div>
        `}
      </moz-card>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
      <div class="sticky-header">
        <moz-page-header
          heading="Icon Directory"
          description="View icons available in Firefox Desktop. Click the file name to copy the chrome:// URL."
        ></moz-page-header>
        <moz-input-search
          placeholder="Filter icons..."
          @MozInputSearch:search=${this.handleSearch}
        ></moz-input-search>
        <span id="color-probe"></span>
        <div class="view-controls">
          <moz-input-color
            label="Fill"
            value=${this.fillColor}
            @change=${this.handleFillChange}
          ></moz-input-color>
          <moz-input-color
            label="Stroke"
            value=${this.strokeColor}
            @change=${this.handleStrokeChange}
          ></moz-input-color>
          <moz-toggle
            label="Show full size icons"
            @toggle=${
              /** @param {CustomEvent & { target: MozToggle }} e */
              e =>
                (this.iconSize = e.target.pressed
                  ? IconSize.Full
                  : IconSize.Normalize)
            }
          ></moz-toggle>
        </div>
      </div>
      ${[...iconData.entries()].map(([dirKey, icons]) =>
        this.iconGroupTemplate(dirKey, icons)
      )}
      <moz-message-bar
        id="success-message"
        type="success"
        message="Copied!"
        popover="manual"
      ></moz-message-bar>
    `;
  }
}

customElements.define("icon-directory", IconDirectory);

export const Default = () => {
  return html`<icon-directory></icon-directory>`;
};
