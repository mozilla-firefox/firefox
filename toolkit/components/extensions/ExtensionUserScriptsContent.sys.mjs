/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This file handles logic related to user script code execution in content.
 * It complements ExtensionContent.sys.mjs and is a separate file because this
 * module only needs to be loaded when an extension runs user scripts.
 */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
});

const { DefaultMap, DefaultWeakMap, ExtensionError } = ExtensionUtils;
const { BaseContext, redefineGetter } = ExtensionCommon;

class WorldConfigHolder {
  /** @type {WeakMap<ExtensionChild,WorldConfigHolder>} */
  static allMaps = new DefaultWeakMap(ext => new WorldConfigHolder(ext));

  constructor(extension) {
    this.defaultCSP = extension.policy.baseCSP;
    this.configs = new Map(extension.getSharedData("userScriptsWorldConfigs"));
  }

  configureWorld(properties) {
    this.configs.set(properties.worldId, properties);
  }

  resetWorldConfiguration(worldId) {
    this.configs.delete(worldId);
  }

  getCSPForWorldId(worldId) {
    return (
      this.configs.get(worldId)?.csp ??
      this.configs.get("")?.csp ??
      this.defaultCSP
    );
  }

  isMessagingEnabledForWorldId(worldId) {
    return (
      this.configs.get(worldId)?.messaging ??
      this.configs.get("")?.messaging ??
      false
    );
  }
}

/**
 * A light wrapper around a ContentScriptContextChild to serve as a BaseContext
 * instance to support APIs exposed to USER_SCRIPT worlds. Such contexts are
 * usually heavier due to the need to track the document lifetime, but because
 * all user script worlds and a content script for a document (and extension)
 * share the same lifetime, we delegate to the only ContentScriptContextChild
 * that exists for the document+extension.
 *
 * @typedef {import("./ExtensionContent.sys.mjs").ContentScriptContextChild} ContentScriptContextChild
 */
class UserScriptContext extends BaseContext {
  /**
   * @param {ContentScriptContextChild} contentContext
   * @param {Sandbox} sandbox
   * @param {string} worldId
   * @param {boolean} messaging
   */
  constructor(contentContext, sandbox, worldId, messaging) {
    // Note: envType "userscript_child" is currently not recognized elsewhere.
    // In particular ParentAPIManager.recvCreateProxyContext refuses to create
    // ProxyContextParent instances, which is desirable because an extension
    // can create many user script worlds, and we do not want the overhead of
    // a new ProxyContextParent for each USER_SCRIPT worldId.
    super("userscript_child", contentContext.extension);

    this.contentContext = contentContext;
    this.#forwardGetterToOwnerContext("active");
    this.#forwardGetterToOwnerContext("incognito");
    this.#forwardGetterToOwnerContext("messageManager");
    this.#forwardGetterToOwnerContext("contentWindow");
    this.#forwardGetterToOwnerContext("innerWindowID");
    this.cloneScopeError = sandbox.Error;
    this.cloneScopePromise = sandbox.Promise;

    this.sandbox = sandbox;
    Object.defineProperty(this, "principal", {
      value: Cu.getObjectPrincipal(sandbox),
      enumerable: true,
      configurable: true,
    });
    this.worldId = worldId;
    this.enableMessaging = messaging;

    contentContext.callOnClose(this);
  }

  close() {
    super.close();
    this.contentContext = null;
    this.sandbox = null;
  }

  async logActivity(type, name, data) {
    return this.contentContext.logActivity(type, name, data);
  }

  get cloneScope() {
    return this.sandbox;
  }

  #forwardGetterToOwnerContext(name) {
    Object.defineProperty(this, name, {
      configurable: true,
      enumerable: true,
      get() {
        return this.contentContext[name];
      },
    });
  }

  get browserObj() {
    const browser = {};
    // The set of APIs exposed to user scripts is minimal. For simplicity and
    // minimizing overhead, we do not use Schemas-generated bindings.

    const wrapF = func => {
      return (...args) => {
        try {
          return func.apply(this, args);
        } catch (e) {
          throw this.normalizeError(e);
        }
      };
    };

    if (this.enableMessaging) {
      browser.runtime = {};
      browser.runtime.connect = wrapF(this.runtimeConnect);
      browser.runtime.sendMessage = wrapF(this.runtimeSendMessage);
    }
    const value = Cu.cloneInto(browser, this.sandbox, { cloneFunctions: true });
    return redefineGetter(this, "browserObj", value);
  }

  runtimeConnect(...args) {
    args = this.#schemaCheckParameters("runtime", "connect", args);
    let [extensionId, options] = args;
    if (extensionId !== null) {
      throw new ExtensionError("extensionId is not supported");
    }
    let name = options?.name ?? "";
    return this.contentContext.messenger.connect({
      context: this,
      userScriptWorldId: this.worldId,
      name,
    });
  }

  runtimeSendMessage(...args) {
    // Simplified version of parseBonkersArgs in child/ext-runtime.js
    let callback = typeof args[args.length - 1] === "function" && args.pop();

    // The extensionId and options parameters are an optional part of the
    // runtime.sendMessage() interface, but not supported in user scripts:
    // runtime.sendMessage() will only trigger runtime.onUserScriptMessage and
    // never runtime.onMessage nor runtime.onMessageExternal.
    if (!args.length) {
      throw new ExtensionError(
        "runtime.sendMessage's message argument is missing"
      );
    } else if (args.length > 1) {
      throw new ExtensionError(
        "runtime.sendMessage received too many arguments"
      );
    }

    let [message] = args;

    return this.contentContext.messenger.sendRuntimeMessage({
      context: this,
      userScriptWorldId: this.worldId,
      message,
      callback,
    });
  }

  #schemaCheckParameters(namespace, method, args) {
    let ns = this.contentContext.childManager.schema.getNamespace(namespace);
    let schemaContext = lazy.Schemas.paramsValidationContexts.get(this);
    return ns.get(method).checkParameters(args, schemaContext);
  }
}

class WorldCollection {
  /** @type {Map<ContentScriptContextChild,WorldCollection>} */
  static allByContext = new DefaultMap(context => new WorldCollection(context));

  /** @type {Map<string,Sandbox>} */
  sandboxes = new DefaultMap(worldId => this.newSandbox(worldId));

  /**
   * Retrieve a Sandbox for the given context and worldId. May throw if the
   * context has unloaded.
   *
   * @param {ContentScriptContextChild} context
   *        Context that wraps the document where the scripts should execute.
   *        The context keeps track of the document & extension lifetime.
   * @param {string} worldId
   *        The identifier of the userScript world.
   * @returns {Sandbox}
   */
  static sandboxFor(context, worldId) {
    return WorldCollection.allByContext.get(context).sandboxes.get(worldId);
  }

  constructor(context) {
    if (context.unloaded) {
      throw new Error("Cannot create user script world after context unloaded");
    }
    this.context = context;
    this.configHolder = WorldConfigHolder.allMaps.get(context.extension);
    context.callOnClose(this);
  }

  close() {
    WorldCollection.allByContext.delete(this.context);
    for (let sandbox of this.sandboxes.values()) {
      Cu.nukeSandbox(sandbox);
    }
    this.sandboxes.clear();
  }

  newSandbox(worldId) {
    let contentWindow = this.context.contentWindow;
    let docPrincipal = contentWindow.document.nodePrincipal;
    let policy = this.context.extension.policy;

    if (docPrincipal.isSystemPrincipal) {
      throw new Error("User scripts are not supported in system principals");
    }

    let sandbox = Cu.Sandbox([docPrincipal], {
      metadata: {
        "inner-window-id": this.context.innerWindowID,
        addonId: policy.id,
      },
      sandboxName: `User script world ${worldId} for ${policy.debugName}`,
      sandboxPrototype: contentWindow,
      sandboxContentSecurityPolicy: this.configHolder.getCSPForWorldId(worldId),
      sameZoneAs: contentWindow,
      wantXrays: true,
      isWebExtensionContentScript: true,
      wantExportHelpers: true,
      originAttributes: docPrincipal.originAttributes,
    });

    let messaging = this.configHolder.isMessagingEnabledForWorldId(worldId);
    if (messaging) {
      let userScriptContext = new UserScriptContext(
        this.context,
        sandbox,
        worldId,
        messaging
      );

      const getBrowserObj = () => userScriptContext.browserObj;
      lazy.Schemas.exportLazyGetter(sandbox, "browser", getBrowserObj);
      lazy.Schemas.exportLazyGetter(sandbox, "chrome", getBrowserObj);
    }

    return sandbox;
  }
}

export const ExtensionUserScriptsContent = {
  sandboxFor(context, worldId) {
    return WorldCollection.sandboxFor(context, worldId);
  },
  updateWorldConfig(extension, reset, update) {
    let configHolder = WorldConfigHolder.allMaps.get(extension);
    if (reset) {
      for (let worldId of reset) {
        configHolder.resetWorldConfiguration(worldId);
      }
    }
    if (update) {
      for (let properties of update) {
        configHolder.configureWorld(properties);
      }
    }
  },
};
