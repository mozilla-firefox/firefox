/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
  MigrationWizardConstants:
    "chrome://browser/content/migration/migration-wizard-constants.mjs",
});

/**
 * This file manages a MigrationWizard embedded in a dialog that runs
 * in a top-level dialog window or in a tab modal dialog. It's main
 * responsibility is to listen for dialog-specific events from the
 * embedded MigrationWizard and to respond appropriately to them.
 *
 * A single object argument is expected to be passed when opening
 * this dialog.
 *
 * @param {object} window.arguments.0
 * @param {object} window.arguments.0.options
 *   A series of options for configuring the dialog. See
 *   MigrationUtils.showMigrationWizard for a description of this
 *   object.
 */

const MigrationDialog = {
  _wiz: null,

  init() {
    addEventListener("load", this);
  },

  onLoad() {
    this._wiz = document.getElementById("wizard");
    this._wiz.addEventListener("MigrationWizard:Close", this);
    document.addEventListener("keypress", this);

    let args = window.arguments[0];
    // When opened via nsIWindowWatcher.openWindow, the arguments are
    // passed through C++, and they arrive to us wrapped as an XPCOM
    // object. We use wrappedJSObject to get at the underlying JS
    // object.
    if (args instanceof Ci.nsISupports) {
      args = args.wrappedJSObject;
    }

    if (args.options?.entrypoint) {
      this._wiz.setAttribute("entrypoint", args.options.entrypoint);
    }

    let observer = new ResizeObserver(() => {
      if (window.resizeDialog) {
        window.resizeDialog();
      } else {
        window.sizeToContent();
      }
    });
    observer.observe(this._wiz);

    customElements.whenDefined("migration-wizard").then(() => {
      if (args.options?.skipSourceSelection) {
        // This is an automigration for a profile refresh, so begin migration
        // automatically once ready.
        this.doProfileRefresh(
          args.options.migratorKey,
          args.options.migrator,
          args.options.profileId
        );
      } else {
        this._wiz.requestState();
      }
    });
  },

  handleEvent(event) {
    switch (event.type) {
      case "load": {
        this.onLoad();
        break;
      }
      case "keypress": {
        if (event.keyCode == KeyEvent.DOM_VK_ESCAPE) {
          window.close();
        }
        break;
      }
      case "MigrationWizard:Close": {
        window.close();
        break;
      }
    }
  },

  async doProfileRefresh(migratorKey, migrator, profileId) {
    let profile = { id: profileId };
    let resourceTypeData = await migrator.getMigrateData(profile);
    let resourceTypeStrs = [];
    for (let type in lazy.MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES) {
      if (resourceTypeData & lazy.MigrationUtils.resourceTypes[type]) {
        resourceTypeStrs.push(
          lazy.MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES[type]
        );
      }
    }

    this._wiz.doAutoImport(migratorKey, profile, resourceTypeStrs);
    this._wiz.addEventListener(
      "MigrationWizard:DoneMigration",
      () => {
        setTimeout(() => {
          window.close();
        }, 5000);
      },
      { once: true }
    );
  },
};

MigrationDialog.init();
