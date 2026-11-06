/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */
/** @import {AboutNewTabComponentRegistry} from "moz-src:///browser/components/newtab/AboutNewTabComponents.sys.mjs" */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

const lazy = XPCOMUtils.declareLazy({
  AboutNewTabComponentRegistry:
    "moz-src:///browser/components/newtab/AboutNewTabComponents.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "ExternalComponentsFeed",
      // @backward-compat { version 162 }
      // Older versions ignore logModule and name the module after the prefix.
      // Remove prefix: once 162 reaches Release.
      prefix: "ExternalComponentsFeed",
      maxLogLevel: Services.prefs.getBoolPref(
        "browser.newtabpage.activity-stream.externalComponents.log",
        false
      )
        ? "Debug"
        : "Warn",
    }),
});

const TRAIN_HOPPING_COMPONENT_CONFIGURATIONS = [
  {
    type: "ASROUTER_MULTISTAGE_MESSAGE",
    mountStrategy: "react-bundle",
    bundleURL:
      "chrome://newtab/content/data/content/external-components/asrouter-newtab-multistage/asrouter-newtab-multistage.bundle.js",
    stylesURLs: [
      "chrome://global/skin/in-content/common.css",
      "chrome://newtab/content/data/content/external-components/asrouter-newtab-multistage/asrouter-newtab-multistage.css",
    ],
    mountFunction: "mountMultistageMessage",
    l10nURLs: [
      "browser/newtab/onboarding.ftl",
      "browser/featureCallout.ftl",
      "browser/backupSettings.ftl",
    ],
    moduleURLs: [
      // While it's more than likely that moz-button is already available on newtab,
      // better safe than sorry.
      "chrome://global/content/elements/moz-button.mjs",
      "chrome://browser/content/backup/turn-on-scheduled-backups.mjs",
    ],
  },
  {
    type: "ASROUTER_NEWTAB_MESSAGE",
    l10nURLs: [],
    componentURL:
      "chrome://newtab/content/data/content/external-components/asrouter-newtab-message/asrouter-newtab-message.mjs",
    tagName: "asrouter-newtab-message",
    cssVariables: {},
    attributes: {},
    actors: {
      ASRouterNewTabMessage: {
        parent: {
          esModuleURI:
            "chrome://newtab/content/data/content/external-components/asrouter-newtab-message/ASRouterNewTabMessageParent.sys.mjs",
        },
        child: {
          esModuleURI:
            "chrome://newtab/content/data/content/external-components/asrouter-newtab-message/ASRouterNewTabMessageChild.sys.mjs",
          events: {
            "ASRouterNewTabMessage:SpecialMessageAction": {
              wantUntrusted: true,
            },
            "ASRouterNewTabMessage:EvaluateTargeting": {
              wantUntrusted: true,
            },
          },
        },
        matches: ["about:home", "about:newtab"],
        remoteTypes: ["privilegedabout"],
      },
    },
  },
];

/**
 * ExternalComponentsFeed manages the integration between the
 * AboutNewTabComponentRegistry and the New Tab Redux store.
 *
 * This feed:
 * - Listens to the AboutNewTabComponentRegistry for component updates
 * - Dispatches REFRESH_EXTERNAL_COMPONENTS actions to update the store
 * - Ensures external components are loaded during New Tab initialization
 *
 * External components registered through this system can be rendered on the
 * newtab page via the ExternalComponentWrapper React component.
 */
export class ExternalComponentsFeed {
  /** @type {Store} */
  store = null;

  /**
   * The AboutNewTabComponentRegistry instance that tracks registered components.
   *
   * @type {AboutNewTabComponentRegistry}
   */
  #registry = null;

  /**
   * Creates a new ExternalComponentsFeed instance.
   *
   * Initializes the AboutNewTabComponentRegistry and sets up a listener to
   * refresh components whenever the registry updates.
   */
  constructor() {
    this.#registry = new lazy.AboutNewTabComponentRegistry();
    this.#registry.on(lazy.AboutNewTabComponentRegistry.UPDATED_EVENT, () => {
      this.refreshComponents();
    });
  }

  /**
   * Dispatches a REFRESH_EXTERNAL_COMPONENTS action with the current list of
   * registered components from the registry.
   *
   * This action is broadcast to all content processes to update their Redux
   * stores with the latest component configurations.
   *
   * @param {object} options - Optional configuration
   * @param {boolean} [options.isStartup=false] - If true, marks the action as a
   *   startup action (meta.isStartup: true), which prevents the cached
   *   about:home document from unnecessarily reprocessing the action.
   */
  refreshComponents(options = {}) {
    for (let configuration of this.#registry.values) {
      if (configuration.actors) {
        for (let actorName of Object.keys(configuration.actors)) {
          try {
            ChromeUtils.unregisterWindowActor(actorName);
          } catch (e) {
            lazy.logConsole.warn(
              `Failed to unregister actor ${actorName} for ${configuration.type}`
            );
          }
        }
      }
    }

    let newConfigurations = [
      ...this.#registry.values,
      ...TRAIN_HOPPING_COMPONENT_CONFIGURATIONS,
    ];

    for (let configuration of newConfigurations) {
      if (configuration.actors) {
        for (let actorName of Object.keys(configuration.actors)) {
          try {
            ChromeUtils.registerWindowActor(
              actorName,
              configuration.actors[actorName]
            );
          } catch (e) {
            lazy.logConsole.error(
              `Failed to register actor ${actorName} for ${configuration.type}`,
              e
            );
          }
        }
      }
    }

    const action = {
      type: at.REFRESH_EXTERNAL_COMPONENTS,
      data: newConfigurations,
    };

    if (options.isStartup) {
      action.meta = { isStartup: true };
    }

    this.store.dispatch(ac.BroadcastToContent(action));
  }

  /**
   * Handles Redux actions dispatched to this feed.
   *
   * Currently handles:
   * - INIT: Refreshes components when Activity Stream initializes, marking
   *   the action as a startup action to optimize cached document handling.
   *
   * @param {object} action - The Redux action to handle
   * @param {string} action.type - The action type
   */
  onAction(action) {
    switch (action.type) {
      case at.INIT:
        this.refreshComponents({ isStartup: true });
        break;
    }
  }
}
