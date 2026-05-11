/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BrowserLoader } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/browser-loader.sys.mjs"
);
const require = BrowserLoader({
  baseURI: "resource://devtools/client/application/",
  window,
}).require;

const {
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  render,
  unmountComponentAtNode,
} = require("resource://devtools/client/shared/vendor/react-dom.mjs");
const Provider = createFactory(
  require("resource://devtools/client/shared/vendor/react-redux.js").Provider
);
const {
  bindActionCreators,
} = require("resource://devtools/client/shared/vendor/redux.js");
const {
  START_IGNORE_ACTION,
} = require("resource://devtools/client/shared/redux/middleware/ignore.js");
const {
  l10n,
} = require("resource://devtools/client/application/src/modules/l10n.js");

const {
  configureStore,
} = require("resource://devtools/client/application/src/create-store.js");
const actions = require("resource://devtools/client/application/src/actions/index.js");

const {
  WorkersListener,
} = require("resource://devtools/client/shared/workers-listener.js");

const {
  services,
} = require("resource://devtools/client/application/src/modules/application-services.js");

const App = createFactory(
  require("resource://devtools/client/application/src/components/App.js")
);

const {
  safeAsyncMethod,
} = require("resource://devtools/shared/async-utils.js");

/**
 * Global Application object in this panel. This object is expected by panel.js and is
 * called to start the UI for the panel.
 */
window.Application = {
  async bootstrap({ toolbox, commands }) {
    // bind event handlers to `this`
    this.updateDomain = this.updateDomain.bind(this);

    // wrap updateWorkers to swallow rejections occurring after destroy
    this.safeUpdateWorkers = safeAsyncMethod(
      () => this.updateWorkers(),
      () => this._destroyed
    );

    this.toolbox = toolbox;
    this._commands = commands;
    this.client = commands.client;

    this.store = configureStore(toolbox.telemetry);
    this.actions = bindActionCreators(actions, this.store.dispatch);

    services.init(this.toolbox);
    await l10n.init(["devtools/client/application.ftl"]);

    await this.updateWorkers();
    this.workersListener = new WorkersListener(this.client.mainRoot);
    this.workersListener.addListener(this.safeUpdateWorkers);

    const deviceFront = await this.client.mainRoot.getFront("device");
    const { canDebugServiceWorkers } = await deviceFront.getDescription();
    this.actions.updateCanDebugWorkers(
      canDebugServiceWorkers && services.features.doesDebuggerSupportWorkers
    );

    const { resourceCommand } = this._commands;
    this._watchedResources = [resourceCommand.TYPES.DOCUMENT_EVENT];
    const isSessionHistoryPanelEnabled = Services.prefs.getBoolPref(
      "devtools.application.sessionHistory.enabled",
      false
    );
    if (isSessionHistoryPanelEnabled) {
      if (
        resourceCommand.hasResourceCommandSupport(
          resourceCommand.TYPES.SESSION_HISTORY
        )
      ) {
        this._watchedResources.push(resourceCommand.TYPES.SESSION_HISTORY);
      } else {
        this.actions.disableSessionHistory();
      }
    }

    this.onResourcesAvailable = this.onResourcesAvailable.bind(this);
    this.onResourcesUpdated = this.onResourcesUpdated.bind(this);
    await resourceCommand.watchResources(this._watchedResources, {
      onAvailable: this.onResourcesAvailable,
      onUpdated: this.onResourcesUpdated,
    });

    // Render the root Application component.
    this.mount = document.querySelector("#mount");
    const app = App({
      client: this.client,
      fluentBundles: l10n.getBundles(),
    });
    render(Provider({ store: this.store }, app), this.mount);
  },

  async updateWorkers() {
    const registrationsWithWorkers =
      await this.client.mainRoot.listAllServiceWorkers();
    this.actions.updateWorkers(registrationsWithWorkers);
  },

  updateDomain() {
    this.actions.updateDomain(this.toolbox.target.url);
  },

  handleOnNavigate() {
    this.updateDomain();
    this.actions.resetManifest();
  },

  onResourcesAvailable(resources) {
    const { resourceCommand } = this._commands;
    for (const resource of resources) {
      if (
        resource.resourceType === resourceCommand.TYPES.DOCUMENT_EVENT &&
        resource.name === "dom-complete" &&
        // Only consider top level document, and ignore remote iframes top document
        resource.targetFront.isTopLevel
      ) {
        this.handleOnNavigate(); // update domain and manifest for the new target
      }

      if (resource.resourceType === resourceCommand.TYPES.SESSION_HISTORY) {
        this.actions.setAvailableSessionHistory(resource);
      }
    }
  },

  onResourcesUpdated(updates) {
    const { resourceCommand } = this._commands;
    for (const { resource, update } of updates) {
      if (resource.resourceType === resourceCommand.TYPES.SESSION_HISTORY) {
        // A single entry changed (e.g. title or URL update for an existing entry).
        if (update.resourceUpdates.sessionHistoryEntry) {
          this.actions.updateSessionHistoryEntry(
            update.resourceUpdates.sessionHistoryEntry
          );
        } else if (update.resourceUpdates.sessionHistory) {
          // The full session history changed (e.g. navigation added or removed entries).
          this.actions.updateSessionHistory(
            update.resourceUpdates.sessionHistory
          );
        }
      }
    }
  },

  destroy() {
    // Prevents any further action from being dispatched
    this.store.dispatch(START_IGNORE_ACTION);

    this.workersListener.removeListener();

    this._commands.resourceCommand.unwatchResources(this._watchedResources, {
      onAvailable: this.onResourcesAvailable,
      onUpdated: this.onResourcesUpdated,
    });

    unmountComponentAtNode(this.mount);
    this.mount = null;
    this.toolbox = null;
    this.client = null;
    this._commands = null;
    this.workersListener = null;
    this._destroyed = true;
  },
};
