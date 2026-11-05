/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";

const CATEGORY_NAME = "browser-newtab-external-component";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "AboutNewTabComponents",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.externalComponents.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

/**
 * @typedef {object} NewTabComponentConfiguration
 * @property {string} type
 * @property {string[]} l10nURLs
 * @property {string[]} [stylesURLs]
 *   Stylesheets to link and load before the component is mounted, so it is
 *   never shown unstyled.
 * @property {string} componentURL
 * @property {string} tagName
 */

/**
 * The AboutNewTabComponentRegistry is a class that manages a list of
 * external components registered to appear on the newtab page.
 *
 * The registry is an EventEmitter, and will emit the UPDATED_EVENT when the
 * registry changes.
 *
 * The registry is bootstrapped via entries in the nsICategoryManager of name
 * CATEGORY_NAME.
 */
class AboutNewTabComponentRegistry extends EventEmitter {
  static TYPES = Object.freeze({
    SEARCH: "SEARCH",
  });
  static UPDATED_EVENT = "updated";

  /**
   * The list of registered external component configurations, keyed on their
   * type.
   *
   * @type {Map<string, NewTabComponentConfiguration>}
   */
  #registeredComponents = new Map();

  /**
   * A mapping of external component registrant instances, keyed on their
   * module URI.
   *
   * @type {Map<string, BaseAboutNewTabComponentRegistrant>}
   */
  #registrants = new Map();

  constructor() {
    super();

    lazy.logConsole.debug("Instantiating AboutNewTabComponentRegistry");
    this.#infalliblyLoadConfigurations();

    Services.obs.addObserver(this, "xpcom-category-entry-removed");
    Services.obs.addObserver(this, "xpcom-category-entry-added");
    Services.obs.addObserver(this, "xpcom-category-cleared");
    Services.obs.addObserver(this, "profile-before-change");
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "xpcom-category-entry-removed":
      // Intentional fall-through
      case "xpcom-category-entry-added":
      // Intentional fall-through
      case "xpcom-category-cleared": {
        // Intentional fall-through
        if (data === CATEGORY_NAME) {
          this.#infalliblyLoadConfigurations();
        }
        break;
      }
      case "profile-before-change": {
        this.destroy();
        break;
      }
    }
  }

  destroy() {
    for (let registrant of this.#registrants.values()) {
      registrant.destroy();
    }
    this.#registrants.clear();
    this.#registeredComponents.clear();

    Services.obs.removeObserver(this, "xpcom-category-entry-removed");
    Services.obs.removeObserver(this, "xpcom-category-entry-added");
    Services.obs.removeObserver(this, "xpcom-category-cleared");
    Services.obs.removeObserver(this, "profile-before-change");
  }

  /**
   * Iterates the CATEGORY_NAME nsICategoryManager category, and attempts to
   * load each registrant's configuration, which updates the
   * #registeredComponents. Updating the #registeredComponents will cause the
   * UPDATED_EVENT event to be emitted from this class.
   *
   * Invalid configurations are skipped.
   *
   * This method will log errors but is guaranteed to return, even if one or
   * more of the configurations is invalid.
   */
  #infalliblyLoadConfigurations() {
    lazy.logConsole.debug("Loading configurations");
    this.#registeredComponents.clear();

    for (let { entry, value } of Services.catMan.enumerateCategory(
      CATEGORY_NAME
    )) {
      try {
        lazy.logConsole.debug("Loading ", entry, value);
        let registrar = null;
        if (this.#registrants.has(entry)) {
          lazy.logConsole.debug("Found pre-existing registrant for ", entry);
          registrar = this.#registrants.get(entry);
        } else {
          lazy.logConsole.debug("Constructing registrant for ", entry);
          const module = ChromeUtils.importESModule(entry);
          const registrarClass = module[value];

          if (
            !(
              registrarClass.prototype instanceof
              BaseAboutNewTabComponentRegistrant
            )
          ) {
            throw new Error(
              `Registrant for ${entry} does not subclass BaseAboutNewTabComponentRegistrant`
            );
          }

          registrar = new registrarClass();
          this.#registrants.set(entry, registrar);
          registrar.on(AboutNewTabComponentRegistry.UPDATED_EVENT, () => {
            this.#infalliblyLoadConfigurations();
          });
        }

        let configurations = registrar.getComponents();
        for (let configuration of configurations) {
          if (this.#validateConfiguration(configuration)) {
            lazy.logConsole.debug(
              `Validated a configuration for type ${configuration.type}`
            );
            this.#registeredComponents.set(configuration.type, configuration);
          } else {
            lazy.logConsole.error(
              `Failed to validate a configuration:`,
              configuration
            );
          }
        }
      } catch (e) {
        lazy.logConsole.error(
          "Failed to load configurations ",
          entry,
          value,
          e.message
        );
      }
    }

    this.emit(AboutNewTabComponentRegistry.UPDATED_EVENT);
  }

  /**
   * Ensures that the configuration abides by newtab's external component
   * rules. Currently, that just means that two components cannot share the
   * same type.
   *
   * @param {NewTabComponentConfiguration} configuration
   * @returns {boolean}
   */
  #validateConfiguration(configuration) {
    if (!configuration.type) {
      return false;
    }

    // Currently, the only validation is to ensure that something isn't already
    // registered with the same type. This rule might evolve over time if we
    // start allowing multiples of a type.
    if (this.#registeredComponents.has(configuration.type)) {
      return false;
    }

    return true;
  }

  /**
   * Returns a copy of the configuration registry for external consumption.
   *
   * @returns {NewTabComponentConfiguration[]}
   */
  get values() {
    return Array.from(this.#registeredComponents.values());
  }
}

/**
 * Any registrants that want to register an external component onto newtab must
 * subclass this base class in order to provide the configuration for their
 * component. They must then add their registrant to the nsICategoryManager
 * category `browser-newtab-external-component`, where the entry is the URI
 * for the module containing the subclass, and the value is the name of the
 * subclass exported by the module.
 */
class BaseAboutNewTabComponentRegistrant extends EventEmitter {
  /**
   * Subclasses can override this method to do any cleanup when the component
   * registry starts being shut down.
   */
  destroy() {}

  /**
   * Subclasses should override this method to provide one or more
   * NewTabComponentConfiguration's.
   *
   * @returns {NewTabComponentConfiguration[]}
   */
  getComponents() {
    return [];
  }

  /**
   * Subclasses can call this method if their component registry ever needs
   * updating. This will alert the registry to update itself.
   */
  updated() {
    this.emit(AboutNewTabComponentRegistry.UPDATED_EVENT);
  }
}

export { AboutNewTabComponentRegistry, BaseAboutNewTabComponentRegistrant };
