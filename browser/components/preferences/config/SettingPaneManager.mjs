/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {SettingPaneConfig, SettingPaneFullConfig, SettingPaneId, SettingPane} from "chrome://browser/content/preferences/widgets/setting-pane.mjs" */

/**
 * Converts a friendly category name to internal pane name.
 *
 * @param {string} categoryName
 * @returns {string}
 */
export function friendlyPrefCategoryNameToInternalName(categoryName) {
  if (categoryName.startsWith("pane")) {
    return categoryName;
  }
  return (
    "pane" + categoryName.substring(0, 1).toUpperCase() + categoryName.substr(1)
  );
}

export const SettingPaneManager = {
  /** @type {Map<string, SettingPaneFullConfig>} */
  _data: new Map(),

  /**
   * @param {string} id
   */
  get(id) {
    if (!this._data.has(id)) {
      throw new Error(`Setting pane "${id}" not found`);
    }
    return this._data.get(id);
  },

  /**
   * @param {string} id
   */
  getWithParents(id) {
    let configs = [this.get(id)];
    while (configs[0].parent) {
      configs.unshift(this.get(configs[0].parent));
    }
    return configs;
  },

  /**
   * Whether a registered pane and all of its parents are visible. Unknown
   * panes are considered visible.
   *
   * @param {string} id
   */
  isVisible(id) {
    if (!this._data.has(id)) {
      return true;
    }
    return this.getWithParents(id).every(config => config.visible?.() ?? true);
  },

  /**
   * @param {string} id
   */
  importPane(id) {
    for (let config of this.getWithParents(id)) {
      // Each import spins the event loop, so the window can be closed part way
      // through this loop; the remaining panes have nothing left to set up.
      if (window.closed) {
        return;
      }
      if (config.module) {
        ChromeUtils.importESModule(config.module, { global: "current" });
      }
    }
  },

  /**
   * @param {SettingPaneId} id
   * @param {SettingPaneConfig} config
   */
  registerPane(id, config) {
    if (this._data.has(id)) {
      throw new Error(`Setting pane "${id}" already registered`);
    }
    let fullConfig = { ...config, id };
    this._data.set(id, fullConfig);
    if (!fullConfig.groupIds.length) {
      // If we don't have groupIds then we're just registering the l10nId.
      return;
    }
    let subPane = friendlyPrefCategoryNameToInternalName(id);
    let settingPane = /** @type {SettingPane} */ (
      document.createElement("setting-pane")
    );
    settingPane.name = subPane;
    settingPane.config = fullConfig;
    settingPane.isSubPane = !!config.parent;

    document.getElementById("mainPrefPane").append(settingPane);
    window.register_module(subPane, {
      init() {
        settingPane.init();
      },
    });
  },

  /**
   * @param {Record<string, SettingPaneConfig>} paneConfigs
   */
  registerPanes(paneConfigs) {
    for (let id in paneConfigs) {
      this.registerPane(id, paneConfigs[id]);
    }
  },
};
