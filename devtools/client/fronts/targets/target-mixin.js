/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyRequireGetter(
  this,
  "getFront",
  "resource://devtools/shared/protocol.js",
  true
);
loader.lazyRequireGetter(
  this,
  "readSourceResponse",
  "resource://devtools/client/fronts/source.js",
  true
);

/**
 * A Target represents a debuggable context. It can be a browser tab, a tab on
 * a remote device, like a tab on Firefox for Android. But it can also be an add-on,
 * as well as firefox parent process, or just one of its content process.
 * A Target is related to a given TargetActor, for which we derive this class.
 *
 * Providing a generalized abstraction of a web-page or web-browser (available
 * either locally or remotely) is beyond the scope of this class (and maybe
 * also beyond the scope of this universe) However Target does attempt to
 * abstract some common events and read-only properties common to many Tools.
 *
 * Supported read-only properties:
 * - name, url
 *
 * Target extends EventEmitter and provides support for the following events:
 * - close: The target window has been closed. All tools attached to this
 *          target should close. This event is not currently cancelable.
 *
 * Optional events only dispatched by WindowGlobalTarget:
 * - will-navigate: The target window will navigate to a different URL
 * - navigate: The target window has navigated to a different URL
 */
function TargetMixin(parentClass) {
  class Target extends parentClass {
    constructor(client, targetFront, parentFront) {
      super(client, targetFront, parentFront);

      // TargetCommand._onTargetAvailable will set this public attribute.
      // This is a reference to the related `commands` object and helps all fronts
      // easily call any command method. Without this bit of magic, Fronts wouldn't
      // be able to interact with any commands while it is frequently useful.
      this.commands = null;

      this.destroy = this.destroy.bind(this);

      this.threadFront = null;

      this._client = client;

      // Cache of already created targed-scoped fronts
      // [typeName:string => Front instance]
      this.fronts = new Map();

      // `resources-available-array` and `resources-updated-array` events can be emitted
      // by target actors before the ResourceCommand could add event listeners.
      // The target front will cache those events until the ResourceCommand has
      // added the listeners.
      this._resourceCache = {};

      // In order to avoid destroying the `_resourceCache[event]`, we need to call `super.on()`
      // instead of `this.on()`.
      const offResourceAvailableArray = super.on(
        "resources-available-array",
        this._onResourceEventArray.bind(this, "resources-available-array")
      );
      const offResourceUpdatedArray = super.on(
        "resources-updated-array",
        this._onResourceEventArray.bind(this, "resources-updated-array")
      );

      this._offResourceEvent = new Map([
        ["resources-available-array", offResourceAvailableArray],
        ["resources-updated-array", offResourceUpdatedArray],
      ]);

      // Expose a promise that is resolved once the target front is usable
      // i.e. once attachAndInitThread has been called and resolved.
      this.initialized = new Promise(resolve => {
        this._onInitialized = resolve;
      });
    }

    on(eventName, listener) {
      if (this._offResourceEvent && this._offResourceEvent.has(eventName)) {
        // If a callsite sets an event listener for resource-(available|update)-(form|array):

        // we want to remove the listener we set here in the constructor…
        const off = this._offResourceEvent.get(eventName);
        this._offResourceEvent.delete(eventName);
        off();

        // …and call the new listener with the resources that were put in the cache.
        if (this._resourceCache[eventName]) {
          for (const cache of this._resourceCache[eventName]) {
            listener(cache);
          }
          delete this._resourceCache[eventName];
        }
      }

      return super.on(eventName, listener);
    }

    /**
     * Boolean flag to help distinguish Target Fronts from other Fronts.
     * As we are using a Mixin, we can't easily distinguish these fronts via instanceof().
     */
    get isTargetFront() {
      return true;
    }

    get targetType() {
      return this.targetForm.targetType;
    }

    get isTopLevel() {
      // We can't use `getTrait` here as this might be called from a destroyed target (e.g.
      // from an onTargetDestroyed callback that was triggered by a legacy listener), which
      // means `this.client` would be null, which would make `getTrait` throw (See Bug 1714974)
      if (!this.targetForm.hasOwnProperty("isTopLevelTarget")) {
        return !!this._isTopLevel;
      }

      return this.targetForm.isTopLevelTarget;
    }

    setIsTopLevel(isTopLevel) {
      if (!this.getTrait("supportsTopLevelTargetFlag")) {
        this._isTopLevel = isTopLevel;
      }
    }

    /**
     * Get the immediate parent target for this target.
     *
     * @return {TargetMixin} the parent target.
     */
    async getParentTarget() {
      return this.commands.targetCommand.getParentTarget(this);
    }

    /**
     * Returns a Promise that resolves to a boolean indicating if the provided target is
     * an ancestor of this instance.
     *
     * @param {TargetFront} target: The possible ancestor target.
     * @returns Promise<Boolean>
     */
    async isTargetAnAncestor(target) {
      const parentTargetFront = await this.getParentTarget();
      if (!parentTargetFront) {
        return false;
      }

      if (parentTargetFront == target) {
        return true;
      }

      return parentTargetFront.isTargetAnAncestor(target);
    }

    /**
     * Get the target for the given Browsing Context ID.
     *
     * @return {TargetMixin} the requested target.
     */
    async getWindowGlobalTarget(browsingContextID) {
      // Just for sanity as commands attribute is set late from TargetCommand._onTargetAvailable
      // but ideally target front should be used before this happens.
      if (!this.commands) {
        return null;
      }
      // Tab and Process Descriptors expose a Watcher, which is creating the
      // targets and should be used to fetch any.
      const { watcherFront } = this.commands;
      if (watcherFront) {
        // Safety check, in theory all watcher should support frames.
        if (watcherFront.traits.frame) {
          return watcherFront.getWindowGlobalTarget(browsingContextID);
        }
        return null;
      }

      // For descriptors which don't expose a watcher (e.g. WebExtension)
      // we used to call RootActor::getBrowsingContextDescriptor, but it was
      // removed in FF77.
      // Support for watcher in WebExtension descriptors is Bug 1644341.
      throw new Error(
        `Unable to call getWindowGlobalTarget for ${this.actorID}`
      );
    }

    /**
     * Returns a boolean indicating whether or not the specific actor
     * type exists.
     *
     * @param {string} actorName
     * @return {boolean}
     */
    hasActor(actorName) {
      if (this.targetForm) {
        return !!this.targetForm[actorName + "Actor"];
      }
      return false;
    }

    /**
     * Returns a trait from the target actor if it exists,
     * if not it will fallback to that on the root actor.
     *
     * @param {string} traitName
     * @return {Mixed}
     */
    getTrait(traitName) {
      if (this.isDestroyedOrBeingDestroyed()) {
        return null;
      }
      // If the targeted actor exposes traits and has a defined value for this
      // traits, override the root actor traits
      if (this.targetForm.traits && traitName in this.targetForm.traits) {
        return this.targetForm.traits[traitName];
      }

      return this.client.traits[traitName];
    }

    // Get a Front for a target-scoped actor.
    // i.e. an actor served by RootActor.listTabs or RootActorActor.getTab requests
    async getFront(typeName) {
      if (this.isDestroyed()) {
        throw new Error(
          "Target already destroyed, unable to fetch children fronts"
        );
      }
      let front = this.fronts.get(typeName);
      if (front) {
        // XXX: This is typically the kind of spot where switching to
        // `isDestroyed()` is complicated, because `front` is not necessarily a
        // Front...
        const isFrontInitializing = typeof front.then === "function";
        const isFrontAlive = !isFrontInitializing && !front.isDestroyed();
        if (isFrontInitializing || isFrontAlive) {
          return front;
        }
      }

      front = getFront(this.client, typeName, this.targetForm, this);
      this.fronts.set(typeName, front);
      // replace the placeholder with the instance of the front once it has loaded
      front = await front;
      this.fronts.set(typeName, front);
      return front;
    }

    getCachedFront(typeName) {
      // do not wait for async fronts;
      const front = this.fronts.get(typeName);
      // ensure that the front is a front, and not async front
      if (front?.actorID) {
        return front;
      }
      return null;
    }

    /**
     * This method gets the source content for a given resource.
     *
     * @param {string} resourceType - The resource type for which to get the source content.
     * @param {string} [resourceId] - The ID of the resource actor. Optional if not applicable (e.g for html resources).
     * @returns {Promise<{source: string, contentType: string}>}
     */
    async getSourceContentForResource(resourceType, resourceId) {
      const { resourceCommand } = this.commands;
      // @backward-compat { version 160 } `getSourceContentForResource` on the window global target actor is not supported on older servers.
      // The `if` check for the trait as well as the fallback code (resourceType `switch check below) can be removed when 160 is no longer on
      // the release channel.
      if (this.getTrait("supportsGetSourceContentForResource")) {
        const response = await super.getSourceContentForResource(
          resourceType,
          resourceId
        );
        // The source is transfered as a front, either an array buffer for wasm
        // binaries or a long string for everything else.
        return response ? readSourceResponse(response) : null;
      }

      switch (resourceType) {
        case resourceCommand.TYPES.STYLESHEET: {
          const stylesheetsFront = await this.getFront("stylesheets");
          const sourceStr = await stylesheetsFront.getText(resourceId);
          return { source: await sourceStr.string(), contentType: "text/css" };
        }
        case resourceCommand.TYPES.SOURCE: {
          const sourceFront = this.threadFront.source({
            actor: resourceId,
          });
          const { source, contentType } = await sourceFront.source();
          return { source, contentType };
        }
      }
      return null;
    }

    get client() {
      return this._client;
    }

    // Tells us if the related actor implements WindowGlobalTargetActor
    // interface and requires to call `attach` request before being used and
    // `detach` during cleanup.
    get isBrowsingContext() {
      return this.typeName === "windowGlobalTarget";
    }

    /**
     * Return the name to be displayed in the debugger and console context selector.
     */
    get name() {
      // When debugging Web Extensions, all documents have moz-extension://${uuid}/... URL
      // When the developer don't set a custom title, fallback on displaying the pathname
      // to avoid displaying long URL prefix with the addon internal UUID.
      if (this.commands.descriptorFront.isWebExtensionDescriptor) {
        if (this._title) {
          return this._title;
        }
        const parsedURL = URL.parse(this._url);
        if (parsedURL) {
          return parsedURL.pathname;
        }
        // If document URL can't be parsed, fallback to the raw URL.
        return this._url;
      }

      if (this.isContentProcess) {
        return this.targetForm.name;
      }
      return this.title;
    }

    get title() {
      return this._title || this.url;
    }

    get url() {
      return this._url;
    }

    get isWorkerTarget() {
      // XXX Remove the check on `workerDescriptor` as part of Bug 1667404.
      return (
        this.typeName === "workerTarget" || this.typeName === "workerDescriptor"
      );
    }

    get isContentProcess() {
      // browser content toolbox's form will be of the form:
      //   server0.conn0.content-process0/contentProcessTarget7
      // while xpcshell debugging will be:
      //   server1.conn0.contentProcessTarget7
      return !!(
        this.targetForm &&
        this.targetForm.actor &&
        this.targetForm.actor.match(
          /conn\d+\.(content-process\d+\/)?contentProcessTarget\d+/
        )
      );
    }

    get isParentProcess() {
      return !!(
        this.targetForm &&
        this.targetForm.actor &&
        this.targetForm.actor.match(/conn\d+\.parentProcessTarget\d+/)
      );
    }

    /**
     * This method attaches the target and then attaches its related thread, sending it
     * the options it needs (e.g. breakpoints, pause on exception setting, …).
     * This function can be called multiple times, it will only perform the actual
     * initialization process once; on subsequent call the original promise (_onThreadInitialized)
     * will be returned.
     *
     * @param {TargetCommand} targetCommand
     * @returns {Promise} A promise that resolves once the thread is attached and resumed.
     */
    attachAndInitThread(targetCommand) {
      if (this._onThreadInitialized) {
        return this._onThreadInitialized;
      }

      this._onThreadInitialized = this._attachAndInitThread(targetCommand);
      // Resolve the `initialized` promise, while ignoring errors
      // The empty function passed to catch will avoid spawning a new possibly rejected promise
      this._onThreadInitialized.catch(() => {}).then(this._onInitialized);
      return this._onThreadInitialized;
    }

    /**
     * This method attach the target and then attach its related thread, sending it the
     * options it needs (e.g. breakpoints, pause on exception setting, …)
     *
     * @private
     * @param {TargetCommand} targetCommand
     * @returns {Promise} A promise that resolves once the thread is attached and resumed.
     */
    async _attachAndInitThread(targetCommand) {
      // If the target is destroyed or soon will be, don't go further
      if (this.isDestroyedOrBeingDestroyed()) {
        return;
      }

      // The current class we have is actually the WorkerDescriptorFront,
      // which will morph into a target by fetching the underlying target's form.
      // Ideally, worker targets would be spawn by the server, and we would no longer
      // have the hybrid descriptor/target class which brings lots of complexity and confusion.
      // To be removed in bug 1651522.
      if (this.morphWorkerDescriptorIntoWorkerTarget) {
        await this.morphWorkerDescriptorIntoWorkerTarget();
      }

      const isBrowserToolbox =
        targetCommand.descriptorFront.isBrowserProcessDescriptor;
      const isNonTopLevelFrameTarget =
        !this.isTopLevel && this.targetType === targetCommand.TYPES.FRAME;

      if (isBrowserToolbox && isNonTopLevelFrameTarget) {
        // In the BrowserToolbox, non-top-level frame targets are already
        // debugged via content-process targets.
        // Do not attach the thread here, as it was already done by the
        // corresponding content-process target.
        return;
      }

      // Avoid attaching any thread actor in the browser console or in
      // webextension commands in order to avoid triggering any type of
      // breakpoint.
      if (targetCommand.descriptorFront.doNotAttachThreadActor) {
        return;
      }

      // If the target is destroyed or soon will be, don't go further
      if (this.isDestroyedOrBeingDestroyed()) {
        return;
      }
      if (!this.targetForm || !this.targetForm.threadActor) {
        throw new Error(
          "TargetMixin sub class should set targetForm.threadActor before calling attachAndInitThread"
        );
      }
      this.threadFront = await this.getFront("thread");
    }

    isDestroyedOrBeingDestroyed() {
      return this.isDestroyed() || this._destroyer;
    }

    /**
     * Target is not alive anymore.
     */
    destroy() {
      // If several things call destroy then we give them all the same
      // destruction promise so we're sure to destroy only once
      if (this._destroyer) {
        return this._destroyer;
      }

      // This pattern allows to immediately return the destroyer promise.
      // See Bug 1602727 for more details.
      let destroyerResolve;
      this._destroyer = new Promise(r => (destroyerResolve = r));
      this._destroyTarget().then(destroyerResolve);

      return this._destroyer;
    }

    async _destroyTarget() {
      // If the target is being attached, try to wait until it's done, to prevent having
      // pending connection to the server when the toolbox is destroyed.
      if (this._onThreadInitialized) {
        try {
          await this._onThreadInitialized;
        } catch (e) {
          // We might still get into cases where attaching fails (e.g. the worker we're
          // trying to attach to is already closed). Since the target is being destroyed,
          // we don't need to do anything special here.
        }
      }

      for (let [name, front] of this.fronts) {
        try {
          // If a Front with an async initialize method is still being instantiated,
          // we should wait for completion before trying to destroy it.
          if (front instanceof Promise) {
            front = await front;
          }
          front.destroy();
        } catch (e) {
          console.warn("Error while destroying front:", name, e);
        }
      }
      this.fronts.clear();

      this.threadFront = null;
      this._offResourceEvent = null;

      // This event should be emitted before calling super.destroy(), because
      // super.destroy() will remove all event listeners attached to this front.
      this.emit("target-destroyed");

      // Not all targets supports attach/detach. For example content process doesn't.
      // Also ensure that the front is still active before trying to do the request.
      if (this.detach && !this.isDestroyed()) {
        // The client was handed to us, so we are not responsible for closing
        // it. We just need to detach from the tab, if already attached.
        // |detach| may fail if the connection is already dead, so proceed with
        // cleanup directly after this.
        try {
          await this.detach();
        } catch (e) {
          this.logDetachError(e);
        }
      }

      // Do that very last in order to let a chance to dispatch `detach` requests.
      super.destroy();

      this._cleanup();
    }

    /**
     * Detach can fail under regular circumstances, if the target was already
     * destroyed on the server side. All target fronts should handle detach
     * error logging in similar ways so this might be used by subclasses
     * with custom detach() implementations.
     *
     * @param {Error} e
     *        The real error object.
     * @param {string} targetType
     *        The type of the target front ("worker", "browsing-context", ...)
     */
    logDetachError(e, targetType) {
      const ignoredError =
        e?.message.includes("noSuchActor") ||
        e?.message.includes("Connection closed");

      // Silence exceptions for already destroyed actors and fronts:
      // - "noSuchActor" errors from the server
      // - "Connection closed" errors from the client, when purging requests
      if (ignoredError) {
        return;
      }

      // Properly log any other error.
      const message = targetType
        ? `Error while detaching the ${targetType} target:`
        : "Error while detaching target:";
      console.warn(message, e);
    }

    /**
     * Clean up references to what this target points to.
     */
    _cleanup() {
      this.threadFront = null;
      this._client = null;

      this._title = null;
      this._url = null;
    }

    _onResourceEventArray(eventName, array) {
      if (!this._resourceCache[eventName]) {
        this._resourceCache[eventName] = [];
      }
      this._resourceCache[eventName].push(array);
    }

    toString() {
      const id = this.targetForm ? this.targetForm.actor : null;
      return `Target:${id}`;
    }

    dumpPools() {
      // NOTE: dumpPools is defined in the Thread actor to avoid
      // adding it to multiple target specs and actors.
      return this.threadFront.dumpPools();
    }

    /**
     * Log an error of some kind to the tab's console.
     *
     * @param {string} text
     *                 The text to log.
     * @param {string} category
     *                 The category of the message.  @see nsIScriptError.
     * @returns {Promise}
     */
    logErrorInPage(text, category) {
      if (this.getTrait("logInPage")) {
        const errorFlag = 0;
        return this.logInPage({ text, category, flags: errorFlag });
      }
      return Promise.resolve();
    }

    /**
     * Log a warning of some kind to the tab's console.
     *
     * @param {string} text
     *                 The text to log.
     * @param {string} category
     *                 The category of the message.  @see nsIScriptError.
     * @returns {Promise}
     */
    logWarningInPage(text, category) {
      if (this.getTrait("logInPage")) {
        const warningFlag = 1;
        return this.logInPage({ text, category, flags: warningFlag });
      }
      return Promise.resolve();
    }

    /**
     * The tracer actor emits frames which should be collected per target/thread.
     * The tracer will emit other resources, refering to the frame indexes in that collected array.
     * The indexes and this array in general is specific to a given tracer actor instance
     * and so is specific per thread and target.
     */
    #jsTracerCollectedFrames = [];
    getJsTracerCollectedFramesArray() {
      return this.#jsTracerCollectedFrames;
    }
  }
  return Target;
}
exports.TargetMixin = TargetMixin;
