/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-message-bar.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/backup/password-validation-inputs.mjs";

import { ERRORS } from "chrome://browser/content/backup/backup-constants.mjs";

const ENABLE_ERROR_L10N_IDS = Object.freeze({
  [ERRORS.FILE_SYSTEM_ERROR]: "turn-on-scheduled-backups-error-file-system",
  [ERRORS.DEFAULT_DIR_ACCESS_DENIED]:
    "turn-on-scheduled-backups-error-default-dir-denied",
  [ERRORS.INVALID_PASSWORD]: "backup-error-password-requirements",
  [ERRORS.UNKNOWN]: "backup-error-retry",
});

/**
 * @param {number} errorCode Error code from backup-constants.mjs
 * @returns {string} Localization ID for error message
 */
function getEnableErrorL10nId(errorCode) {
  return (
    ENABLE_ERROR_L10N_IDS[errorCode] ?? ENABLE_ERROR_L10N_IDS[ERRORS.UNKNOWN]
  );
}

/**
 * The widget for showing available options when users want to turn on
 * scheduled backups.
 */
export default class TurnOnScheduledBackups extends MozLitElement {
  #placeholderIconURL = "chrome://global/skin/icons/page-portrait.svg";

  static properties = {
    backupServiceState: { type: Object },
    // passed in from parents
    defaultIconURL: { type: String, reflect: true },
    defaultLabel: { type: String, reflect: true },
    defaultPath: { type: String, reflect: true },
    supportBaseLink: { type: String },
    embeddedFxBackupOptIn: {
      type: Boolean,
      reflect: true,
      attribute: "embedded-fx-backup-opt-in",
    },
    hideFilePathChooser: {
      type: Boolean,
      reflect: true,
      attribute: "hide-file-path-chooser",
    },
    hideSecondaryButton: {
      type: Boolean,
      reflect: true,
      attribute: "hide-secondary-button",
    },
    backupIsEncrypted: {
      type: Boolean,
      reflect: true,
      attribute: "backup-is-encrypted",
    },
    filePathLabelL10nId: {
      type: String,
      reflect: true,
      attribute: "file-path-label-l10n-id",
    },
    turnOnBackupHeaderL10nId: {
      type: String,
      reflect: true,
      attribute: "turn-on-backup-header-l10n-id",
    },
    createPasswordLabelL10nId: {
      type: String,
      reflect: true,
      attribute: "create-password-label-l10n-id",
    },
    turnOnBackupConfirmBtnL10nId: {
      type: String,
      reflect: true,
      attribute: "turn-on-backup-confirm-btn-l10n-id",
    },
    turnOnBackupCancelBtnL10nId: {
      type: String,
      reflect: true,
      attribute: "turn-on-backup-cancel-btn-l10n-id",
    },
    // Identifier of the message, surface, or code path that hosted this
    // widget. Forwarded to BackupService.setScheduledBackups so that
    // browser.backup.scheduler_toggle_source can attribute the enable.
    source: {
      type: String,
      reflect: true,
      attribute: "source",
    },

    // internal state
    _newIconURL: { type: String, state: true },
    _newLabel: { type: String, state: true },
    _newPath: { type: String, state: true },
    _showPasswordOptions: { type: Boolean, reflect: true, state: true },
    _passwordsMatch: { type: Boolean, state: true },
    _inputPassValue: { type: String, state: true },

    // managed by BackupUIChild
    enableBackupErrorCode: { type: Number },
  };

  static get queries() {
    return {
      cancelButtonEl: "#backup-turn-on-scheduled-cancel-button",
      confirmButtonEl: "#backup-turn-on-scheduled-confirm-button",
      filePathButtonEl: "#backup-location-filepicker-button",
      filePathInputCustomEl: "#backup-location-filepicker-input-custom",
      filePathInputDefaultEl: "#backup-location-filepicker-input-default",
      passwordOptionsCheckboxEl: "#sensitive-data-checkbox-input",
      passwordOptionsExpandedEl: "#passwords",
      errorEl: "#enable-backup-encryption-error",
    };
  }

  constructor() {
    super();
    this.backupServiceState = {};
    this.defaultIconURL = "";
    this.defaultLabel = "";
    this.defaultPath = "";
    this._newIconURL = "";
    this._newLabel = "";
    this._newPath = "";
    this._showPasswordOptions = false;
    this._passwordsMatch = false;
    this.enableBackupErrorCode = 0;
    this.disableSubmit = false;
    this._pendingConfirmDetail = null;
  }

  /**
   * Whether the default backup location should be shown, i.e. the user hasn't
   * chosen a custom path and there's no persisted path from a previous screen.
   *
   * @returns {boolean}
   */
  get showDefaultFilePath() {
    return (
      !this._newPath &&
      !this.backupServiceState?.embeddedComponentPersistentData?.path
    );
  }

  connectedCallback() {
    super.connectedCallback();
    this.dispatchEvent(
      new CustomEvent("BackupUI:InitWidget", { bubbles: true })
    );

    // listen to events from BackupUIChild
    this.addEventListener("BackupUI:SelectNewFilepickerPath", this);
    this.addEventListener("BackupUI:DefaultDirProbeResult", this);

    // listen to events from <password-validation-inputs>
    this.addEventListener("ValidPasswordsDetected", this);
    this.addEventListener("InvalidPasswordsDetected", this);

    // listens to keydown events
    this.addEventListener("keydown", this);
  }

  handleEvent(event) {
    if (event.type == "BackupUI:SelectNewFilepickerPath") {
      let { path, filename, iconURL } = event.detail;
      this._newPath = path;
      this._newLabel = filename;
      this._newIconURL = iconURL;

      if (this.enableBackupErrorCode == ERRORS.DEFAULT_DIR_ACCESS_DENIED) {
        this.enableBackupErrorCode = ERRORS.NONE;
      }

      if (this.embeddedFxBackupOptIn) {
        // Let's set a persistent path
        this.dispatchEvent(
          new CustomEvent("BackupUI:SetEmbeddedComponentPersistentData", {
            bubbles: true,
            detail: {
              path,
              label: filename,
              iconURL,
            },
          })
        );
      }
    } else if (event.type == "BackupUI:DefaultDirProbeResult") {
      let { readAccessGranted } = event.detail;
      if (readAccessGranted) {
        // We have access! Let's go ahead and create backups.
        this.dispatchEvent(
          new CustomEvent("BackupUI:EnableScheduledBackups", {
            bubbles: true,
            detail: this._pendingConfirmDetail,
          })
        );
      } else {
        this.defaultLabel = "";
        this.defaultPath = "";
        this.defaultIconURL = "";
        this.enableBackupErrorCode = ERRORS.DEFAULT_DIR_ACCESS_DENIED;
      }
      this._pendingConfirmDetail = null;
    } else if (event.type == "ValidPasswordsDetected") {
      let { password } = event.detail;
      this._passwordsMatch = true;
      this._inputPassValue = password;
    } else if (event.type == "InvalidPasswordsDetected") {
      this._passwordsMatch = false;
      this._inputPassValue = "";
    } else if (event.type == "keydown") {
      if (
        event.key === "Enter" &&
        (event.originalTarget.id ==
          "backup-location-filepicker-input-default" ||
          event.originalTarget.id == "backup-location-filepicker-input-custom")
      ) {
        event.preventDefault();
      }
    }
  }

  async handleChooseLocation() {
    this.dispatchEvent(
      new CustomEvent("BackupUI:ShowFilepicker", {
        bubbles: true,
        detail: {
          win: window.browsingContext,
        },
      })
    );
  }

  close() {
    this.dispatchEvent(
      new CustomEvent("dialogCancel", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleConfirm() {
    let detail = {
      source: this.source,
    };

    if (this._showPasswordOptions && this._passwordsMatch) {
      detail.password = this._inputPassValue;
    }

    if (
      this.embeddedFxBackupOptIn &&
      this.backupIsEncrypted &&
      !detail.password
    ) {
      this.dispatchEvent(
        new CustomEvent("SpotlightOnboardingAdvanceScreens", {
          bubbles: true,
        })
      );
      return;
    }

    this._pendingConfirmDetail = detail;
    this.dispatchEvent(
      new CustomEvent("BackupUI:ProbeDefaultBackupDir", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleTogglePasswordOptions() {
    this._showPasswordOptions = this.passwordOptionsCheckboxEl?.checked;
    this._passwordsMatch = false;
  }

  updated(changedProperties) {
    super.updated?.(changedProperties);

    if (changedProperties.has("hideFilePathChooser")) {
      // If hideFilePathChooser is true, show password options
      this._showPasswordOptions = !!this.hideFilePathChooser;

      // Uncheck the checkbox if it exists
      if (this.passwordOptionsCheckboxEl) {
        this.passwordOptionsCheckboxEl.checked = this._showPasswordOptions;
      }
    }
  }

  reset() {
    this._showPasswordOptions = false;
    this.passwordOptionsCheckboxEl.checked = false;
    this._passwordsMatch = false;
    this._inputPassValue = "";
    this.enableBackupErrorCode = 0;
    this.disableSubmit = false;
    this._pendingConfirmDetail = null;
    // we don't want to reset the path when embedded in the spotlight
    if (!this.embeddedFxBackupOptIn) {
      this._newPath = "";
      this._newIconURL = "";
      this._newLabel = "";
    }

    if (this.passwordOptionsExpandedEl) {
      /** @type {import("./password-validation-inputs.mjs").default} */
      const passwordElement = this.passwordOptionsExpandedEl;
      passwordElement.reset();
    }

    if (
      this.embeddedFxBackupOptIn &&
      this.backupServiceState?.embeddedComponentPersistentData
    ) {
      this.dispatchEvent(
        new CustomEvent("BackupUI:FlushEmbeddedComponentPersistentData", {
          bubbles: true,
        })
      );
    }
  }

  defaultFilePathInputTemplate() {
    let filename = this.defaultLabel;
    let iconURL = this.defaultIconURL || this.#placeholderIconURL;

    const hasFilename = !!filename;
    const l10nArgs = hasFilename
      ? JSON.stringify({ recommendedFolder: filename })
      : null;

    return html`
      <input
        id="backup-location-filepicker-input-default"
        class="backup-location-filepicker-input"
        type="text"
        readonly
        data-l10n-id=${hasFilename
          ? "turn-on-scheduled-backups-location-default-folder"
          : "turn-on-scheduled-backups-location-choose-folder"}
        data-l10n-args=${hasFilename ? l10nArgs : nothing}
        data-l10n-attrs="value"
        style=${`background-image: url(${iconURL})`}
      />
    `;
  }

  /**
   * Note: We also consider the embeddedComponentPersistentData since we might be in the
   *    Spotlight where we need this persistent data between screens. This state property should
   *    not be set if we are not in the Spotlight.
   */
  customFilePathInputTemplate() {
    let filename =
      this._newLabel ||
      this.backupServiceState?.embeddedComponentPersistentData?.label;
    let iconURL =
      this._newIconURL ||
      this.backupServiceState?.embeddedComponentPersistentData?.iconURL ||
      this.#placeholderIconURL;

    return html`
      <input
        id="backup-location-filepicker-input-custom"
        class="backup-location-filepicker-input"
        type="text"
        readonly
        .value=${filename}
        style=${`background-image: url(${iconURL})`}
      />
    `;
  }

  errorTemplate() {
    return html`
      <moz-message-bar
        id="enable-backup-encryption-error"
        type="error"
        .messageL10nId=${getEnableErrorL10nId(this.enableBackupErrorCode)}
      ></moz-message-bar>
    `;
  }

  allOptionsTemplate() {
    let locationInputId = this.showDefaultFilePath
      ? "backup-location-filepicker-input-default"
      : "backup-location-filepicker-input-custom";
    return html`
      <fieldset id="all-controls">
        <div id="backup-location-controls">
          <label
            id="backup-location-label"
            for=${locationInputId}
            data-l10n-id=${this.filePathLabelL10nId ||
            "turn-on-scheduled-backups-location-label"}
          ></label>
          <div id="backup-location-filepicker">
            ${this.showDefaultFilePath
              ? this.defaultFilePathInputTemplate()
              : this.customFilePathInputTemplate()}
            <moz-button
              id="backup-location-filepicker-button"
              @click=${this.handleChooseLocation}
              data-l10n-id="turn-on-scheduled-backups-location-choose-button"
              aria-controls=${locationInputId}
            ></moz-button>
          </div>
        </div>
        <fieldset id="sensitive-data-controls">
          <div id="sensitive-data-checkbox">
            <label
              id="sensitive-data-checkbox-label"
              for="sensitive-data-checkbox-input"
              aria-controls="passwords"
              aria-expanded=${this._showPasswordOptions}
            >
              <input
                id="sensitive-data-checkbox-input"
                .value=${this._showPasswordOptions}
                @click=${this.handleTogglePasswordOptions}
                type="checkbox"
              />
              <span
                id="sensitive-data-checkbox-span"
                data-l10n-id="turn-on-scheduled-backups-encryption-label"
              ></span>
            </label>
            <span
              class="text-deemphasized"
              data-l10n-id="settings-sensitive-data-encryption-description"
            ></span>
          </div>

          ${this._showPasswordOptions ? this.passwordsTemplate() : null}
        </fieldset>
      </fieldset>
    `;
  }

  passwordsTemplate() {
    return html`
      <password-validation-inputs
        id="passwords"
        .supportBaseLink=${this.supportBaseLink}
        .createPasswordLabelL10nId=${this.createPasswordLabelL10nId}
        ?embedded-fx-backup-opt-in=${this.embeddedFxBackupOptIn}
      ></password-validation-inputs>
    `;
  }

  contentTemplate() {
    const hasEmbeddedPersistentData =
      this.embeddedFxBackupOptIn &&
      this.backupServiceState?.embeddedComponentPersistentData?.path;
    // All the situations where we want to disable submit:
    // - passwords don't match
    // - there's no destination folder
    // - other unknown errors
    if (
      (this._showPasswordOptions && !this._passwordsMatch) ||
      (!this._newPath && !this.defaultLabel && !hasEmbeddedPersistentData) ||
      this.enableBackupErrorCode != ERRORS.NONE
    ) {
      this.disableSubmit = true;
    } else {
      this.disableSubmit = false;
    }

    return html`
      <form
        id="backup-turn-on-scheduled-wrapper"
        aria-labelledby="backup-turn-on-scheduled-header"
        aria-describedby="backup-turn-on-scheduled-description"
        part="form"
      >
        <h2
          id="backup-turn-on-scheduled-header"
          class="heading-medium"
          data-l10n-id=${this.turnOnBackupHeaderL10nId ||
          "turn-on-scheduled-backups-header"}
        ></h2>
        <main id="backup-turn-on-scheduled-content">
          <div id="backup-turn-on-scheduled-description">
            <span
              id="backup-turn-on-scheduled-description-span"
              data-l10n-id="turn-on-scheduled-backups-description"
            ></span>
            <a
              id="backup-turn-on-scheduled-learn-more-link"
              is="moz-support-link"
              support-page="firefox-backup"
              data-l10n-id="turn-on-scheduled-backups-support-link"
              utm-content="turn-on-backup"
            ></a>
          </div>
          ${this.allOptionsTemplate()}
          ${this.enableBackupErrorCode ? this.errorTemplate() : null}
        </main>

        <moz-button-group id="backup-turn-on-scheduled-button-group">
          <moz-button
            id="backup-turn-on-scheduled-cancel-button"
            @click=${this.close}
            data-l10n-id=${this.turnOnBackupCancelBtnL10nId ||
            "turn-on-scheduled-backups-cancel-button"}
          ></moz-button>
          <moz-button
            id="backup-turn-on-scheduled-confirm-button"
            form="backup-turn-on-scheduled-wrapper"
            @click=${this.handleConfirm}
            type="primary"
            data-l10n-id=${this.turnOnBackupConfirmBtnL10nId ||
            "turn-on-scheduled-backups-confirm-button"}
            ?disabled=${this.disableSubmit}
          ></moz-button>
        </moz-button-group>
      </form>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/backup/turn-on-scheduled-backups.css"
      />
      ${this.contentTemplate()}
    `;
  }
}

customElements.define("turn-on-scheduled-backups", TurnOnScheduledBackups);
