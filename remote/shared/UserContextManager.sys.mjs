/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  EventEmitter: "resource://gre/modules/EventEmitter.sys.mjs",

  ContextualIdentityListener:
    "chrome://remote/content/shared/listeners/ContextualIdentityListener.sys.mjs",
  generateUUID: "chrome://remote/content/shared/UUID.sys.mjs",
  NavigableManager: "chrome://remote/content/shared/NavigableManager.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
});

const DEFAULT_CONTEXT_ID = "default";
const DEFAULT_INTERNAL_ID = 0;

/**
 * A UserContextManager instance keeps track of all public user contexts and
 * maps their internal platform.
 *
 * This class is exported for test purposes. Otherwise the UserContextManager
 * singleton should be used.
 *
 * @fires UserContextManagerClass#"user-context-created"
 *      - {string} userContextId
 *            The UUID of the user context which was just created.
 * @fires UserContextManagerClass#"user-context-deleted"
 *      - {string} userContextId
 *            The UUID of the user context which was just deleted.
 *      - {number} internalId
 *            The internal platform id of the user context.
 */
export class UserContextManagerClass {
  #contextualIdentityListener;
  #userContextIds;

  constructor() {
    lazy.EventEmitter.decorate(this);

    // Map from internal ids (numbers) from the ContextualIdentityService to
    // opaque UUIDs (string).
    this.#userContextIds = new Map();

    // The default user context is always using 0 as internal user context id
    // and should be exposed as "default" instead of a randomly generated id.
    this.#userContextIds.set(DEFAULT_INTERNAL_ID, DEFAULT_CONTEXT_ID);

    // Register other (non-default) public contexts.
    lazy.ContextualIdentityService.getPublicIdentities().forEach(identity =>
      this.#registerIdentity(identity)
    );

    this.#contextualIdentityListener = new lazy.ContextualIdentityListener();
    this.#contextualIdentityListener.on("created", this.#onIdentityCreated);
    this.#contextualIdentityListener.on("deleted", this.#onIdentityDeleted);
    this.#contextualIdentityListener.startListening();
  }

  destroy() {
    this.#contextualIdentityListener.off("created", this.#onIdentityCreated);
    this.#contextualIdentityListener.off("deleted", this.#onIdentityDeleted);
    this.#contextualIdentityListener.destroy();

    this.#userContextIds = null;
  }

  /**
   * Retrieve the user context id corresponding to the default user context.
   *
   * @returns {string}
   *     The default user context id.
   */
  get defaultUserContextId() {
    return DEFAULT_CONTEXT_ID;
  }

  /**
   * Creates a new user context.
   *
   * @param {object} options
   * @param {string=} options.color
   *     The color to use for the container, from the list of container colors
   *     supported by ContextualIdentityService. Defaults to "gray".
   * @param {string=} options.icon
   *     The icon to use for the container, from the list of container icons
   *     supported by ContextualIdentityService. Defaults to "circle".
   * @param {string=} options.name
   *     The name to use for the user context. When provided options.prefix will
   *     be ignored. Defaults to null.
   * @param {string=} options.prefix
   *     A prefix to use for the name of the user context. Defaults to "remote".
   *
   * @returns {string}
   *     The user context id of the new user context.
   */
  createContext(options = {}) {
    const { color, icon, name = null, prefix = "remote" } = options;

    // Use provided name if valid or generate a unique name.
    // Note that several containers can use the same name.
    const isValidName = typeof name == "string" && name.trim();
    const userContextName = isValidName
      ? name
      : `${prefix}-${lazy.generateUUID()}`;

    // Create the user context.
    const identity = lazy.ContextualIdentityService.create(
      userContextName,
      icon,
      color
    );

    // An id has been set already by the contextual-identity-created observer.
    return this.#userContextIds.get(identity.userContextId);
  }

  /**
   * Retrieve the user context id corresponding to the provided browsing context.
   *
   * @param {BrowsingContext} browsingContext
   *     The browsing context to get the user context id from.
   *
   * @returns {string}
   *     The corresponding user context id.
   *
   * @throws {TypeError}
   *     If `browsingContext` is not a CanonicalBrowsingContext instance.
   */
  getIdByBrowsingContext(browsingContext) {
    if (!CanonicalBrowsingContext.isInstance(browsingContext)) {
      throw new TypeError(
        `Expected browsingContext to be a CanonicalBrowsingContext, got ${browsingContext}`
      );
    }

    return this.getIdByInternalId(
      browsingContext.originAttributes.userContextId
    );
  }

  /**
   * Retrieve the user context id corresponding to the provided internal id.
   *
   * @param {number} internalId
   *     The internal user context id.
   *
   * @returns {string|null}
   *     The corresponding user context id or null if the user context does not
   *     exist.
   */
  getIdByInternalId(internalId) {
    if (this.#userContextIds.has(internalId)) {
      return this.#userContextIds.get(internalId);
    }
    return null;
  }

  /**
   * Retrieve the user context id corresponding to the provided navigable id.
   *
   * @param {number} navigableId
   *     The navigable id.
   *
   * @returns {string|null}
   *     The corresponding user context id or null if the user context does not
   *     exist.
   */
  getIdByNavigableId(navigableId) {
    const context = lazy.NavigableManager.getBrowsingContextById(navigableId);
    if (context) {
      return this.getIdByBrowsingContext(context);
    }
    return null;
  }

  /**
   * Retrieve the internal id corresponding to the provided user
   * context id.
   *
   * @param {string} userContextId
   *     The user context id.
   *
   * @returns {number|null}
   *     The internal user context id or null if the user context does not
   *     exist.
   */
  getInternalIdById(userContextId) {
    for (const [internalId, id] of this.#userContextIds) {
      if (userContextId == id) {
        return internalId;
      }
    }
    return null;
  }

  /**
   * Returns an array of tabs related
   * to the provided internal user context id.
   *
   * @param {string} internalId
   *     The internal user context id.
   *
   * @returns {Array<Tab>}
   *     The array of tabs.
   */
  getTabsForUserContext(internalId) {
    return lazy.TabManager.allTabs.filter(tab => {
      return (
        (tab.hasAttribute("usercontextid") &&
          parseInt(tab.getAttribute("usercontextid"), 10) == internalId) ||
        (!tab.hasAttribute("usercontextid") && internalId === 0)
      );
    });
  }

  /**
   * Returns an array of all known user context ids.
   *
   * @returns {Array<string>}
   *     The array of user context ids.
   */
  getUserContextIds() {
    return Array.from(this.#userContextIds.values());
  }

  /**
   * Retrieve the user context ids for containers matching the provided name.
   *
   * @param {string} name
   *     The expected name for the container.
   *
   * @returns {Array<string>}
   *     The array of user context ids with the expected name, can be empty.
   */
  getUserContextIdsByName(name) {
    const userContextIds = [];
    for (const [internalId, id] of this.#userContextIds) {
      const identity =
        lazy.ContextualIdentityService.getPublicIdentityFromId(internalId);
      if (identity?.name === name) {
        userContextIds.push(id);
      }
    }
    return userContextIds;
  }

  /**
   * Checks if the provided user context id is known by this UserContextManager.
   *
   * @param {string} userContextId
   *     The id of the user context to check.
   */
  hasUserContextId(userContextId) {
    return this.getUserContextIds().includes(userContextId);
  }

  /**
   * Removes a user context and closes all related container tabs.
   *
   * Note: When closing the related container tabs possible "beforeunload"
   * prompts will be ignored.
   *
   * @param {string} userContextId
   *     The id of the user context to remove.
   * @param {object=} options
   * @param {boolean=} options.closeContextTabs
   *     Pass true if the tabs owned by the user context should also be closed.
   *     Defaults to false.
   */
  removeUserContext(userContextId, options = {}) {
    const { closeContextTabs = false } = options;

    if (!this.hasUserContextId(userContextId)) {
      return;
    }

    const internalId = this.getInternalIdById(userContextId);
    if (closeContextTabs) {
      lazy.ContextualIdentityService.closeContainerTabs(internalId, {
        skipPermitUnload: true,
      });
    }
    lazy.ContextualIdentityService.remove(internalId);
  }

  #onIdentityCreated = (eventName, data) => {
    this.#registerIdentity(data.identity);
  };

  #onIdentityDeleted = (eventName, data) => {
    const internalId = data.identity.userContextId;
    const userContextId = this.#userContextIds.get(internalId);
    this.#userContextIds.delete(internalId);
    this.emit("user-context-deleted", { userContextId, internalId });
  };

  #registerIdentity(identity) {
    const userContextId = lazy.generateUUID();
    this.#userContextIds.set(identity.userContextId, userContextId);
    this.emit("user-context-created", { userContextId });
  }
}

// Expose a shared singleton.
export const UserContextManager = new UserContextManagerClass();
