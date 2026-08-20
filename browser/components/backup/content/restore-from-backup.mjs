/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, styleMap } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { ERRORS } from "chrome://browser/content/backup/backup-constants.mjs";
import { getErrorL10nId } from "chrome://browser/content/backup/backup-errors.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-message-bar.mjs";

/**
 * The widget for allowing users to select and restore from a
 * a backup file.
 */
export default class RestoreFromBackup extends MozLitElement {
  #placeholderFileIconURL = "chrome://global/skin/icons/page-portrait.svg";
  /**
   * When the user clicks the button to choose a backup file to restore, we send
   * a message to the `BackupService` process asking it to read that file.
   * When we do this, we set this property to be a promise, which we resolve
   * when the file reading is complete.
   */
  #backupFileReadPromise = null;

  /**
   * Resolves when BackupUIParent sends state for the first time.
   */
  get initializedPromise() {
    return this.#initializedResolvers.promise;
  }
  #initializedResolvers = Promise.withResolvers();

  /**
   * It's possible if the user selected an invalid backup file that there is a
   * filename but no info. To prevent that case from repeatedly asking for the
   * 'missing' info, this tracks the previous filename we asked for.
   */
  #lastBackupInfoFilename = null;

  static properties = {
    _fileIconURL: { type: String },
    _restoreType: { type: String },
    aboutWelcomeEmbedded: { type: Boolean },
    backupServiceState: { type: Object },
  };

  static get queries() {
    return {
      filePicker: "#backup-filepicker-input",
      passwordInput: "#backup-password-input",
      cancelButtonEl: "#restore-from-backup-cancel-button",
      confirmButtonEl: "#restore-from-backup-confirm-button",
      chooseButtonEl: "#backup-filepicker-button",
      errorMessageEl: "#restore-from-backup-error",
    };
  }

  get isIncorrectPassword() {
    return this.backupServiceState?.recoveryErrorCode === ERRORS.UNAUTHORIZED;
  }

  get isFileError() {
    const code = this.backupServiceState?.recoveryErrorCode;
    return (
      code === ERRORS.CORRUPTED_ARCHIVE ||
      code === ERRORS.UNSUPPORTED_BACKUP_VERSION ||
      code === ERRORS.UNSUPPORTED_APPLICATION
    );
  }

  constructor() {
    super();
    this._fileIconURL = "";
    this._restoreType = "add";
    // Set the default state
    this.backupServiceState = {
      backupDirPath: "",
      backupFileToRestore: null,
      backupFileInfo: null,
      defaultParent: {
        fileName: "",
        path: "",
        iconURL: "",
      },
      encryptionEnabled: false,
      scheduledBackupsEnabled: false,
      selectableProfilesAllowed: false,
      lastBackupDate: null,
      lastBackupFileName: "",
      supportBaseLink: "https://support.mozilla.org/",
      backupInProgress: false,
      recoveryInProgress: false,
      recoveryErrorCode: ERRORS.NONE,
    };
  }

  /**
   * Dispatches the BackupUI:InitWidget custom event upon being attached to the
   * DOM, which registers with BackupUIChild for BackupService state updates.
   */
  connectedCallback() {
    super.connectedCallback();
    this.dispatchEvent(
      new CustomEvent("BackupUI:InitWidget", { bubbles: true })
    );

    // If we have a backup file, but not the associated info, fetch the info
    this.maybeGetBackupFileInfo();

    this.addEventListener("BackupUI:SelectNewFilepickerPath", this);
    this.addEventListener("BackupUI:StateWasUpdated", this);
  }

  maybeGetBackupFileInfo() {
    if (
      this.backupServiceState?.backupFileToRestore &&
      !this.backupServiceState?.backupFileInfo
    ) {
      this.getBackupFileInfo();
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
  }

  updated(changedProperties) {
    super.updated(changedProperties);

    if (changedProperties.has("backupServiceState")) {
      // If we got a recovery error, recoveryInProgress should be false
      const inProgress =
        this.backupServiceState.recoveryInProgress &&
        !this.backupServiceState.recoveryErrorCode;

      this.dispatchEvent(
        new CustomEvent("BackupUI:RecoveryProgress", {
          bubbles: true,
          composed: true,
          detail: { recoveryInProgress: inProgress },
        })
      );

      // It's possible that backupFileToRestore got updated and we need to
      // refetch the fileInfo
      this.maybeGetBackupFileInfo();
    }
  }

  handleEvent(event) {
    if (event.type == "BackupUI:SelectNewFilepickerPath") {
      let { iconURL } = event.detail;
      this._fileIconURL = iconURL;

      // Check the backup info again even if it was the same file.
      this.#lastBackupInfoFilename = null;

      this.#backupFileReadPromise = Promise.withResolvers();
      this.#backupFileReadPromise.promise.then(() => {
        const payload = {
          location: this.backupServiceState?.backupFileCoarseLocation,
          valid: this.backupServiceState?.recoveryErrorCode == ERRORS.NONE,
        };
        if (payload.valid) {
          payload.backup_timestamp = new Date(
            this.backupServiceState?.backupFileInfo?.date || 0
          ).getTime();
          payload.restore_id = this.backupServiceState?.restoreID;
          payload.encryption =
            this.backupServiceState?.backupFileInfo?.isEncrypted;
          payload.app_name = this.backupServiceState?.backupFileInfo?.appName;
          payload.version = this.backupServiceState?.backupFileInfo?.appVersion;
          payload.build_id = this.backupServiceState?.backupFileInfo?.buildID;
          payload.os_name = this.backupServiceState?.backupFileInfo?.osName;
          payload.os_version =
            this.backupServiceState?.backupFileInfo?.osVersion;
          payload.os_build_number =
            this.backupServiceState?.backupFileInfo?.osBuildNumber;
          payload.telemetry_enabled =
            this.backupServiceState?.backupFileInfo?.healthTelemetryEnabled;
        }
        Glean.browserBackup.restoreFileChosen.record(payload);
        Services.obs.notifyObservers(null, "browser-backup-glean-sent");
      });

      this.getBackupFileInfo();
    } else if (event.type == "BackupUI:StateWasUpdated") {
      this.#initializedResolvers.resolve();
      if (this.#backupFileReadPromise) {
        this.#backupFileReadPromise.resolve();
        this.#backupFileReadPromise = null;
      }
    }
  }

  handleRestoreTypeChange(event) {
    this._restoreType = event.target.value;
  }

  handleChooseBackupFile() {
    this.dispatchEvent(
      new CustomEvent("BackupUI:ShowFilepicker", {
        bubbles: true,
        composed: true,
        detail: {
          win: window.browsingContext,
          filter: "filterHTML",
          existingBackupPath: this.backupServiceState?.backupFileToRestore,
        },
      })
    );
  }

  getBackupFileInfo() {
    let backupFile = this.backupServiceState?.backupFileToRestore;
    if (!backupFile || this.#lastBackupInfoFilename === backupFile) {
      return;
    }

    this.#lastBackupInfoFilename = backupFile;
    this.dispatchEvent(
      new CustomEvent("BackupUI:GetBackupFileInfo", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleCancel() {
    this.dispatchEvent(
      new CustomEvent("dialogCancel", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleConfirm() {
    if (
      !this.backupServiceState?.backupFileToRestore ||
      this.backupServiceState?.recoveryInProgress
    ) {
      return;
    }
    let backupPassword = this.passwordInput?.value;
    this.dispatchEvent(
      new CustomEvent("BackupUI:RestoreFromBackupFile", {
        bubbles: true,
        composed: true,
        detail: {
          backupPassword,
          restoreType: this._restoreType,
          source: this.aboutWelcomeEmbedded ? "onboarding" : "preferences",
        },
      })
    );
  }

  /**
   * Constructs a support URL with UTM parameters for use
   * when embedded in about:welcome
   *
   * @param {string} supportPage - The support page slug
   * @returns {string} The full support URL including UTM params
   */

  getSupportURLWithUTM(supportPage) {
    let supportURL = new URL(
      supportPage,
      this.backupServiceState.supportBaseLink
    );
    supportURL.searchParams.set("utm_medium", "firefox-desktop");
    supportURL.searchParams.set("utm_source", "npo");
    supportURL.searchParams.set("utm_campaign", "fx-backup-restore");
    supportURL.searchParams.set("utm_content", "restore-error");
    return supportURL.href;
  }

  applyContentCustomizations() {
    if (this.aboutWelcomeEmbedded) {
      this.style.setProperty(
        "--label-font-weight",
        "var(--font-weight-semibold)"
      );
    }
  }

  renderBackupFileInfo(backupFileInfo) {
    return html`<p
      id="restore-from-backup-backup-found-info"
      data-l10n-id="backup-file-creation-metadata2"
      data-l10n-args=${JSON.stringify({
        profileName: backupFileInfo.profileName ?? "",
        machineName: backupFileInfo.deviceName ?? "",
        date: backupFileInfo.date ? new Date(backupFileInfo.date).getTime() : 0,
      })}
    ></p>`;
  }

  renderBackupFileStatus() {
    const { backupFileInfo, recoveryErrorCode } = this.backupServiceState || {};

    if (
      recoveryErrorCode &&
      !this.isIncorrectPassword &&
      (this.isFileError || this.aboutWelcomeEmbedded)
    ) {
      return this.genericFileErrorTemplate();
    }

    if (!backupFileInfo) {
      return null;
    }

    // Backup file found and no error
    return this.renderBackupFileInfo(backupFileInfo);
  }

  controlsTemplate() {
    let iconURL = this.#placeholderFileIconURL;
    if (
      this.backupServiceState?.backupFileToRestore &&
      !this.aboutWelcomeEmbedded
    ) {
      iconURL = this._fileIconURL || this.#placeholderFileIconURL;
    }
    return html`
      <fieldset id="backup-restore-controls">
        ${this.aboutWelcomeEmbedded
          ? null
          : html`<div>
              <a
                id="restore-from-backup-support-link"
                slot="support-link"
                is="moz-support-link"
                support-page="firefox-backup"
                data-l10n-id="restore-from-backup-support-link1"
              ></a>
            </div>`}
        <fieldset id="backup-filepicker-controls">
          <label
            id="backup-filepicker-label"
            for="backup-filepicker-input"
            data-l10n-id="restore-from-backup-filepicker-label"
          ></label>
          <div id="backup-filepicker">
            ${this.inputTemplate(iconURL)}
            <moz-button
              id="backup-filepicker-button"
              @click=${this.handleChooseBackupFile}
              data-l10n-id="restore-from-backup-file-choose-button"
              aria-controls="backup-filepicker-input"
            ></moz-button>
          </div>

          ${this.renderBackupFileStatus()}
        </fieldset>
        <fieldset id="password-entry-controls">
          ${this.backupServiceState?.backupFileInfo?.isEncrypted
            ? this.passwordEntryTemplate()
            : null}
        </fieldset>

        ${this.backupServiceState?.selectableProfilesAllowed
          ? html` <moz-radio-group
              name="restore-from-backup-type"
              id="restore-from-backup-type-group"
              data-l10n-id="restore-from-backup-type-group-label"
              heading-level="3"
              @change=${this.handleRestoreTypeChange}
            >
              <moz-radio
                data-l10n-id="restore-from-backup-type-add"
                value="add"
                checked
              ></moz-radio>
              <moz-radio
                data-l10n-id="restore-from-backup-type-replace"
                value="replace"
              ></moz-radio>
            </moz-radio-group>`
          : html` <moz-message-bar type="info">
              <span
                slot="message"
                data-l10n-id="restore-from-backup-profiles-disabled-message"
              >
              </span>
            </moz-message-bar>`}
      </fieldset>
    `;
  }

  inputTemplate(iconURL) {
    const styles = styleMap({
      ...(iconURL ? { backgroundImage: `url(${iconURL})` } : {}),
      fieldSizing: "content",
      width: "100%",
    });
    const backupFileName = this.backupServiceState?.backupFileToRestore || "";

    const { backupFileInfo, recoveryErrorCode } = this.backupServiceState || {};
    const hasInlineFileError =
      recoveryErrorCode &&
      !this.isIncorrectPassword &&
      (this.isFileError || this.aboutWelcomeEmbedded);

    let describedBy = "";
    if (hasInlineFileError) {
      describedBy = "backup-generic-file-error";
    } else if (!backupFileInfo) {
      describedBy = "restore-from-backup-no-backup-file-link";
    } else {
      describedBy = "restore-from-backup-backup-found-info";
    }

    return html`
      <textarea
        id="backup-filepicker-input"
        rows="1"
        readonly
        .value=${backupFileName}
        style=${styles}
        aria-invalid=${String(!!hasInlineFileError)}
        aria-describedby=${describedBy}
        data-l10n-id="restore-from-backup-filepicker-input"
      ></textarea>
    `;
  }

  passwordEntryTemplate() {
    const isInvalid = this.isIncorrectPassword;
    const describedBy = isInvalid
      ? "backup-password-error"
      : "backup-password-description";

    let passwordStatus;
    if (isInvalid && this.aboutWelcomeEmbedded) {
      passwordStatus = html`
        <span
          id="backup-password-error"
          class="field-error"
          data-l10n-id="backup-service-error-incorrect-password"
        >
          <a
            id="backup-incorrect-password-support-link"
            target="_blank"
            href=${this.getSupportURLWithUTM("firefox-backup")}
            data-l10n-name="incorrect-password-support-link"
            dir="auto"
            rel="noopener noreferrer"
          ></a>
        </span>
      `;
    } else if (isInvalid) {
      passwordStatus = html`
        <span
          id="backup-password-error"
          class="field-error"
          data-l10n-id="backup-service-error-incorrect-password"
        >
          <a
            id="backup-incorrect-password-support-link"
            slot="support-link"
            is="moz-support-link"
            support-page="firefox-backup"
            data-l10n-name="incorrect-password-support-link"
            dir="auto"
          ></a>
        </span>
      `;
    } else {
      passwordStatus = html`<label
        id="backup-password-description"
        data-l10n-id="restore-from-backup-password-description"
      ></label>`;
    }

    return html` <fieldset id="backup-password">
      <label id="backup-password-label" for="backup-password-input">
        <span
          id="backup-password-span"
          data-l10n-id="restore-from-backup-password-label"
        ></span>
        <input
          type="password"
          id="backup-password-input"
          aria-invalid=${String(isInvalid)}
          aria-describedby=${describedBy}
        />
      </label>
      ${passwordStatus}
    </fieldset>`;
  }

  contentTemplate() {
    let buttonL10nId = !this.backupServiceState?.recoveryInProgress
      ? "restore-from-backup-confirm-button"
      : "restore-from-backup-restoring-button";

    return html`
      <div
        id="restore-from-backup-wrapper"
        aria-labelledby="restore-from-backup-header"
        aria-describedby="restore-from-backup-description"
      >
        ${this.aboutWelcomeEmbedded ? null : this.headerTemplate()}
        <main id="restore-from-backup-content">
          ${!this.aboutWelcomeEmbedded &&
          this.backupServiceState?.recoveryErrorCode
            ? this.errorTemplate()
            : null}
          ${this.controlsTemplate()}
        </main>

        <moz-button-group id="restore-from-backup-button-group">
          ${this.aboutWelcomeEmbedded ? null : this.cancelButtonTemplate()}
          <moz-button
            id="restore-from-backup-confirm-button"
            @click=${this.handleConfirm}
            type="primary"
            data-l10n-id=${buttonL10nId}
            ?disabled=${!this.backupServiceState?.backupFileToRestore ||
            !this.backupServiceState?.backupFileInfo ||
            this.backupServiceState?.recoveryInProgress ||
            (this.backupServiceState?.selectableProfilesAllowed &&
              !this._restoreType)}
          ></moz-button>
        </moz-button-group>
      </div>
    `;
  }

  headerTemplate() {
    return html`
      <h2
        id="restore-from-backup-header"
        class="heading-medium"
        data-l10n-id="restore-from-backup-header"
      ></h2>
    `;
  }

  cancelButtonTemplate() {
    return html`
      <moz-button
        id="restore-from-backup-cancel-button"
        @click=${this.handleCancel}
        data-l10n-id="restore-from-backup-cancel-button"
      ></moz-button>
    `;
  }

  errorTemplate() {
    // We handle incorrect password errors in the password input
    // and file errors inline below the file picker
    if (this.isIncorrectPassword || this.isFileError) {
      return null;
    }

    return html`
      <moz-message-bar
        id="restore-from-backup-error"
        type="error"
        data-l10n-id=${getErrorL10nId(
          this.backupServiceState?.recoveryErrorCode
        )}
      >
      </moz-message-bar>
    `;
  }

  genericFileErrorTemplate() {
    if (this.isIncorrectPassword) {
      return null;
    }

    if (this.aboutWelcomeEmbedded) {
      return html`
        <span
          id="backup-generic-file-error"
          class="field-error"
          data-l10n-id="backup-file-restore-file-validation-error"
        >
          <a
            id="backup-generic-error-link"
            target="_blank"
            href=${this.getSupportURLWithUTM("firefox-backup")}
            data-l10n-name="restore-problems"
            dir="auto"
            rel="noopener noreferrer"
          ></a>
        </span>
      `;
    }

    return html`
      <span
        id="backup-generic-file-error"
        class="field-error"
        data-l10n-id="backup-file-restore-file-validation-error"
      >
        <a
          id="backup-generic-error-link"
          slot="support-link"
          is="moz-support-link"
          support-page="firefox-backup"
          data-l10n-name="restore-problems"
          dir="auto"
        ></a>
      </span>
    `;
  }

  render() {
    this.applyContentCustomizations();
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/backup/restore-from-backup.css"
      />
      ${this.contentTemplate()}
    `;
  }
}

customElements.define("restore-from-backup", RestoreFromBackup);
