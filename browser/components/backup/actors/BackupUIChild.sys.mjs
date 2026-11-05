/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "BackupUIChild",
    maxLogLevel: Services.prefs.getBoolPref("browser.backup.log", false)
      ? "Debug"
      : "Warn",
  });
});

/**
 * A JSWindowActor that is responsible for marshalling information between
 * the BackupService singleton and any registered UI widgets that need to
 * represent data from that service. Any UI widgets that want to receive
 * state updates from BackupService should emit a BackupUI:InitWidget
 * event in a document that this actor pair is registered for.
 */
export class BackupUIChild extends JSWindowActorChild {
  #inittedWidgets = new WeakSet();

  /**
   * Finds the first connected widget matching the given node name.
   *
   * @param {string} nodeName
   *   The uppercase node name to match (e.g. "BACKUP-SETTINGS").
   * @returns {Element|null}
   */
  #findWidget(nodeName) {
    let widgets = ChromeUtils.nondeterministicGetWeakSetKeys(
      this.#inittedWidgets
    );
    for (let widget of widgets) {
      if (widget.isConnected && widget.nodeName == nodeName) {
        return widget;
      }
    }
    return null;
  }

  /**
   * Handles custom events fired by widgets that want to register with
   * BackupUIChild.
   *
   * @param {Event} event
   *   The custom event that the widget fired.
   */
  async handleEvent(event) {
    /**
     * BackupUI:InitWidget sends a message to the parent to request the BackupService state
     * which will result in a `backupServiceState` property of the widget to be set when that
     * state is received. Subsequent state updates will also cause that state property to
     * be set.
     */
    if (event.type == "BackupUI:InitWidget") {
      this.#inittedWidgets.add(event.target);
      this.sendAsyncMessage("RequestState");
    } else if (event.type == "BackupUI:TriggerCreateBackup") {
      let result = await this.sendQuery("TriggerCreateBackup", event.detail);

      if (!result.success) {
        event.target.backupErrorCode = result.errorCode;
      }
    } else if (event.type == "BackupUI:EnableScheduledBackups") {
      const target = event.target;

      const result = await this.sendQuery(
        "EnableScheduledBackups",
        event.detail
      );
      if (result.success) {
        target.close();
      } else {
        target.enableBackupErrorCode = result.errorCode;
      }
    } else if (event.type == "BackupUI:DisableScheduledBackups") {
      const target = event.target;

      this.sendAsyncMessage("DisableScheduledBackups", event.detail);
      // backups will always end up disabled even if there was an error
      // with other bookkeeping related to turning off backups

      target.close();
    } else if (event.type == "BackupUI:ShowFilepicker") {
      let targetNodeName = event.composedTarget.nodeName;
      let { path, filename, iconURL } = await this.sendQuery("ShowFilepicker", {
        win: event.detail?.win,
        filter: event.detail?.filter,
        existingBackupPath: event.detail?.existingBackupPath,
        alsoDeleteLastBackup: event.detail?.alsoDeleteLastBackup,
      });

      let widget = this.#findWidget(targetNodeName);
      if (widget) {
        const win = widget.documentGlobal;
        // Using Cu.cloneInto here allows us to embed components that use this event
        // in non-parent-processes such as about:welcome
        const detail = Cu.cloneInto({ path, filename, iconURL }, win, {
          wrapReflectors: true,
        });
        const evt = new win.CustomEvent("BackupUI:SelectNewFilepickerPath", {
          bubbles: true,
          composed: true,
          detail,
        });
        widget.dispatchEvent(evt);
      }
    } else if (event.type == "BackupUI:GetBackupFileInfo") {
      this.sendAsyncMessage("GetBackupFileInfo");
    } else if (event.type == "BackupUI:RestoreFromBackupFile") {
      let { backupPassword, restoreType, source } = event.detail;
      let result = await this.sendQuery("RestoreFromBackupFile", {
        backupPassword,
        restoreType,
        source,
      });

      if (result.success) {
        event.target.restoreFromBackupDialogEl?.close();

        // Since we always launch the new profile from this event, let's close the current instance now
        this.sendAsyncMessage("QuitCurrentProfile");
      }
    } else if (event.type == "BackupUI:RestoreFromBackupChooseFile") {
      this.sendAsyncMessage("RestoreFromBackupChooseFile");
    } else if (event.type == "BackupUI:EnableEncryption") {
      const target = event.target;

      const result = await this.sendQuery("EnableEncryption", event.detail);
      if (result.success) {
        target.close();
      } else {
        target.enableEncryptionErrorCode = result.errorCode;
      }
    } else if (event.type == "BackupUI:DisableEncryption") {
      const target = event.target;

      const result = await this.sendQuery("DisableEncryption", event.detail);
      if (result.success) {
        target.close();
      } else {
        target.disableEncryptionErrorCode = result.errorCode;
      }
    } else if (event.type == "BackupUI:ShowBackupLocation") {
      this.sendAsyncMessage("ShowBackupLocation");
    } else if (event.type == "BackupUI:SetEmbeddedComponentPersistentData") {
      this.sendAsyncMessage("SetEmbeddedComponentPersistentData", event.detail);
    } else if (event.type == "BackupUI:FlushEmbeddedComponentPersistentData") {
      this.sendAsyncMessage("FlushEmbeddedComponentPersistentData");
    } else if (event.type == "BackupUI:ErrorBarDismissed") {
      this.sendAsyncMessage("ErrorBarDismissed");
    } else if (event.type == "BackupUI:FindBackupsInWellKnownLocations") {
      this.sendAsyncMessage("FindBackupsInWellKnownLocations", event.detail);
    } else if (event.type == "BackupUI:ProbeDefaultBackupDir") {
      let targetNodeName = event.composedTarget.nodeName;
      let readAccessGranted = false;
      try {
        ({ readAccessGranted } = await this.sendQuery("ProbeDefaultBackupDir"));
      } catch (e) {
        lazy.logConsole.error("ProbeDefaultBackupDir failed:", e);
      }

      let widget = this.#findWidget(targetNodeName);
      if (widget) {
        const win = widget.documentGlobal;
        const detail = Cu.cloneInto({ readAccessGranted }, win, {
          wrapReflectors: true,
        });
        widget.dispatchEvent(
          new win.CustomEvent("BackupUI:DefaultDirProbeResult", {
            bubbles: false,
            detail,
          })
        );
      }
    } else if (event.type == "BackupUI:PrepareRestoreDialog") {
      let targetNodeName = event.composedTarget.nodeName;
      try {
        await this.sendQuery("PrepareRestoreDialog", event.detail);
      } catch (e) {
        lazy.logConsole.error("PrepareRestoreDialog failed:", e);
      }

      let widget = this.#findWidget(targetNodeName);
      if (widget) {
        widget.dispatchEvent(
          new widget.documentGlobal.CustomEvent("BackupUI:RestoreDialogReady", {
            bubbles: false,
          })
        );
      }
    }
  }

  /**
   * Handles messages sent by BackupUIParent.
   *
   * @param {ReceiveMessageArgument} message
   *   The message received from the BackupUIParent.
   */
  receiveMessage(message) {
    if (message.name == "StateUpdate") {
      let widgets = ChromeUtils.nondeterministicGetWeakSetKeys(
        this.#inittedWidgets
      );
      for (let widget of widgets) {
        if (!widget.isConnected || !widget.documentGlobal) {
          continue;
        }

        const state = Cu.cloneInto(message.data.state, widget.documentGlobal);

        const waivedWidget = Cu.waiveXrays(widget);
        waivedWidget.backupServiceState = state;
        //dispatch the event for the React listeners
        widget.dispatchEvent(
          new this.contentWindow.CustomEvent("BackupUI:StateWasUpdated", {
            bubbles: true,
            composed: true,
            detail: { state },
          })
        );
      }
    }
  }
}
