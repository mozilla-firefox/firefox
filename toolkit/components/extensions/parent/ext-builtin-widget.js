/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
});

/**
 * Registers the widget an extension declares in `builtin_widget` or
 * `generated_widget` with WidgetManager from the time the extension is ready
 * until it shuts down. `builtin_widget` is available to privileged extensions
 * and `generated_widget` to generated widget extensions.
 */
this.builtinWidget = class extends ExtensionAPI {
  /**
   * The extension's pending "ready" listener.
   *
   * @type {Function|null}
   */
  readyListener = null;

  onManifestEntry(entryName) {
    const { extension } = this;
    // Extension.sys.mjs keeps `generated_widget` only when
    // canUseGeneratedWidgets() and `builtin_widget` only for privileged
    // extensions, so only `builtin_widget` will be used if both are present.
    if (entryName == "generated_widget" && extension.manifest.builtin_widget) {
      return;
    }
    const options = extension.manifest[entryName];
    this.readyListener = () => this.onReady(entryName, options);
    // "ready" is emitted only after the extension has started successfully.
    extension.once("ready", this.readyListener);
  }

  /**
   * Registers the widget with WidgetManager when the extension is still
   * running.
   *
   * @param {string} entryName - the manifest key declaring the widget,
   *   `builtin_widget` or `generated_widget`
   * @param {object} options - the validated value of that manifest key
   */
  onReady(entryName, options) {
    const { extension } = this;
    this.readyListener = null;
    if (extension.hasShutdown) {
      return;
    }
    WidgetManager.register({
      id: extension.id,
      name: extension.name,
      src: options.page,
      sizes: options.sizes,
      defaultSize: options.default_size,
      prefBranch: options.pref_branch,
      generated: entryName == "generated_widget",
    });
  }

  /**
   * @param {boolean} isAppShutdown - whether the application is quitting
   */
  onShutdown(isAppShutdown) {
    const { extension } = this;
    if (this.readyListener) {
      extension.off("ready", this.readyListener);
      this.readyListener = null;
    }
    if (!isAppShutdown) {
      WidgetManager.unregister(extension.id);
    }
  }
};
