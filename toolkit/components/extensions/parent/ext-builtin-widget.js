/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
});

/**
 * Registers the widget a privileged extension declares in `builtin_widget`
 * with WidgetManager from the time the extension is ready until it shuts down.
 */
this.builtinWidget = class extends ExtensionAPI {
  /**
   * The extension's pending "ready" listener.
   *
   * @type {Function|null}
   */
  readyListener = null;

  onManifestEntry() {
    const { extension } = this;
    const options = extension.manifest.builtin_widget;
    this.readyListener = () => this.onReady(options);
    // "ready" is emitted only after the extension has started successfully.
    extension.once("ready", this.readyListener);
  }

  /**
   * Registers the widget with WidgetManager when the extension is still
   * running.
   *
   * @param {object} options - the validated `builtin_widget` manifest key
   */
  onReady(options) {
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
