/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// keyword to use in telemetry, as `reason` parameter
const REASON = "application";

class ManifestDevToolsError extends Error {
  constructor(...params) {
    super(...params);

    this.name = "ManifestDevToolsError";
  }
}

class ApplicationServices {
  init(toolbox) {
    this._toolbox = toolbox;

    this.features = {
      doesDebuggerSupportWorkers: Services.prefs.getBoolPref(
        "devtools.debugger.features.windowless-service-workers",
        false
      ),
    };
  }

  selectTool(toolId) {
    this._assertInit();
    return this._toolbox.selectTool(toolId, REASON);
  }

  async openWorkerInDebugger(workerDescriptorFront) {
    const debuggerPanel = await this.selectTool("jsdebugger");
    debuggerPanel.selectServiceWorker(workerDescriptorFront);
  }

  async viewWorkerSource(workerDescriptorFront) {
    // NOTE: this falls back to view-source: if the source can't be inspected
    //       within the debugger.
    this._toolbox.viewSourceInDebugger(
      workerDescriptorFront.url,
      1,
      1,
      null,
      REASON
    );
  }

  canNavigateToSessionHistoryIndex() {
    this._assertInit();
    return !!this._toolbox.target.getTrait("navigationByIndex");
  }

  navigateToSessionHistoryIndex(index) {
    this._assertInit();
    return this._toolbox.target.gotoIndex(index);
  }

  async fetchManifest() {
    let response;

    try {
      this._assertInit();
      const manifestFront = await this._toolbox.target.getFront("manifest");
      response = await manifestFront.fetchCanonicalManifest();
    } catch (error) {
      throw new ManifestDevToolsError(
        error.message,
        error.fileName,
        error.lineNumber
      );
    }

    if (response.errorMessage) {
      throw new Error(response.errorMessage);
    }

    return response.manifest;
  }

  _assertInit() {
    if (!this._toolbox) {
      throw new Error("Services singleton has not been initialized");
    }
  }
}

module.exports = {
  ManifestDevToolsError,
  // exports a singleton, which will be used across all application panel modules
  services: new ApplicationServices(),
};
