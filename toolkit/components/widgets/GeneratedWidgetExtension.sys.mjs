/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Extension } from "resource://gre/modules/Extension.sys.mjs";

/**
 * Extension overrides for generated widgets.
 *
 * Builds the manifest from the widget's name and prompt, passed in
 * `addonData.generatedWidget`, and limits the parsed manifest data to what a
 * widget page needs. Allows using `generated_widget` by overriding
 * canUseGeneratedWidgets().
 */
export class GeneratedWidgetExtension extends Extension {
  static PERMISSIONS = ["storage", "unlimitedStorage", "idle"];

  constructor(addonData, startupReason) {
    super(addonData, startupReason);
    // The XPIProvider database has no record of this add-on. Generated
    // widgets can not have a background page, so they can not register the
    // persistent listeners kept in its startup data.
    this.dontSaveStartupData = true;
    // Opt out of storing and retrieving normalized manifest data from the
    // webext.sc.lz4 to avoid the need to validate the cached manifest data
    // against tampering.
    this.dontCacheManifestData = true;
  }

  get manifestSchemaType() {
    return "manifest.GeneratedWidgetManifest";
  }

  canUseGeneratedWidgets() {
    return true;
  }

  async initializeAddonTypeAndID() {
    if (this.type) {
      return;
    }
    let { name, prompt } = this.addonData.generatedWidget;
    this.rawManifest = {
      manifest_version: 3,
      name,
      description: prompt,
      version: this.version,
      browser_specific_settings: { gecko: { id: this.id } },
      permissions: [...GeneratedWidgetExtension.PERMISSIONS],
      generated_widget: {
        page: "widget.html",
        sizes: ["small", "medium", "large"],
        default_size: "large",
      },
    };
    this.type = "extension";
  }

  async parseManifest() {
    let data = await super.parseManifest();
    if (!data) {
      return data;
    }
    // super.parseManifest() merges stored ExtensionPermissions grants into the
    // data.
    let origins = new Set(data.originPermissions);
    if (
      !data.permissions.isSubsetOf(
        new Set(GeneratedWidgetExtension.PERMISSIONS)
      ) ||
      origins.size !== 1 ||
      !origins.has(
        new MatchPattern(this.getURL(), { ignorePath: true }).pattern
      ) ||
      new Set(data.dataCollectionPermissions).size
    ) {
      throw new Error(
        `Generated widget ${this.id} has unexpected manifest data`
      );
    }
    return data;
  }
}
