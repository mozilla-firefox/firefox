/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { throttle } = require("resource://devtools/shared/throttle.js");

let gLastResourceId = 0;

function cacheKey(resourceType, resourceId) {
  return `${resourceType}:${resourceId}`;
}

loader.lazyGetter(this, "logger", function () {
  return console.createInstance({
    prefix: "devtools_resourcecommand",
    maxLogLevel: "Warn",
  });
});

class ResourceCommand {
  #destroyed = false;

  /**
   * This class helps retrieving existing and listening to resources.
   * A resource is something that:
   *  - the target you are debugging exposes
   *  - can be created as early as the process/worker/page starts loading
   *  - can already exist, or will be created later on
   *  - doesn't require any user data to be fetched, only a type/category
   *
   * @param object commands
   *        The commands object with all interfaces defined from devtools/shared/commands/
   */
  constructor({ commands }) {
    this.targetCommand = commands.targetCommand;

    // Public attribute set by tests to disable throttling
    this.throttlingDisabled = false;

    this._onTargetAvailable = this._onTargetAvailable.bind(this);
    this._onTargetDestroyed = this._onTargetDestroyed.bind(this);

    // Array of all the currently registered watchers, which contains object with attributes:
    // - {String} resources: list of all resource watched by this one watcher
    // - {Function} onAvailable: watcher's function to call when a new resource is available
    // - {Function} onUpdated: watcher's function to call when a resource has been updated
    // - {Function} onDestroyed: watcher's function to call when a resource is destroyed
    this._watchers = [];

    // Set of watchers currently going through watchResources, only used to handle
    // early calls to unwatchResources. Using a Set instead of an array for easier
    // delete operations.
    this._pendingWatchers = new Set();

    // Caches for all resources by the order that the resource was taken.
    this._cache = new Map();
    this._listenedResources = new Set();

    // WeakMap used to avoid starting a legacy listener twice for the same
    // target + resource-type pair. Legacy listener creation can be subject to
    // race conditions.
    // Maps a target front to an array of resource types.
    this._existingLegacyListeners = new WeakMap();
    this._processingExistingResources = new Set();

    // List of targetFront event listener unregistration functions keyed by target front.
    // These are called when unwatching resources, so if a consumer starts watching resources again,
    // we don't have listeners registered twice.
    this._offTargetFrontListeners = new Map();

    // Bug 1914386: We used to throttle the resource on client and should try to remove it entirely.
    const throttleDelay = 0;
    this._notifyWatchers = this._notifyWatchers.bind(this);
    this._throttledNotifyWatchers = throttle(
      this._notifyWatchers,
      throttleDelay
    );
  }

  destroy() {
    this.#destroyed = true;
  }

  get watcherFront() {
    return this.targetCommand.watcherFront;
  }

  addResourceToCache(resource) {
    const { resourceId, resourceType } = resource;
    if (TRANSIENT_RESOURCE_TYPES.includes(resourceType)) {
      return;
    }
    this._cache.set(cacheKey(resourceType, resourceId), resource);
  }

  /**
   * Clear all the resources related to specifed resource types.
   * Should also trigger clearing of the caches that exists on the related
   * serverside resource watchers.
   *
   * @param {Array:string} resourceTypes
   *                       A list of all the resource types whose
   *                       resources shouled be cleared.
   */
  async clearResources(resourceTypes) {
    if (!Array.isArray(resourceTypes)) {
      throw new Error("clearResources expects a list of resources types");
    }
    // Clear the cached resources of the type.
    for (const [key, resource] of this._cache) {
      if (resourceTypes.includes(resource.resourceType)) {
        // NOTE: To anyone paranoid like me, yes it is okay to delete from a Map while iterating it.
        this._cache.delete(key);
      }
    }

    const resourcesToClear = resourceTypes.filter(resourceType =>
      this.hasResourceCommandSupport(resourceType)
    );
    if (resourcesToClear.length) {
      this.watcherFront.clearResources(resourcesToClear);
    }
  }
  /**
   * Return all specified resources cached in this watcher.
   *
   * @param {string} resourceType
   * @return {Array} resources cached in this watcher
   */
  getAllResources(resourceType) {
    const result = [];
    for (const resource of this._cache.values()) {
      if (resource.resourceType === resourceType) {
        result.push(resource);
      }
    }
    return result;
  }

  /**
   * Return the specified resource cached in this watcher.
   *
   * @param {string} resourceType
   * @param {string} resourceId
   * @return {object} resource cached in this watcher
   */
  getResourceById(resourceType, resourceId) {
    return this._cache.get(cacheKey(resourceType, resourceId));
  }

  /**
   * Request to start retrieving all already existing instances of given
   * type of resources and also start watching for the one to be created after.
   *
   * @param {Array:string} resources
   *        List of all resources which should be fetched and observed.
   * @param {object} options
   *        - {Function} onAvailable: This attribute is mandatory.
   *                                  Function which will be called with an array of resources
   *                                  each time resource(s) are created.
   *                                  A second dictionary argument with `areExistingResources` boolean
   *                                  attribute helps knowing if that's live resources, or some coming
   *                                  from ResourceCommand cache.
   *        - {Function} onUpdated:   This attribute is optional.
   *                                  Function which will be called with an array of updates resources
   *                                  each time resource(s) are updated.
   *                                  These resources were previously notified via onAvailable.
   *        - {Function} onDestroyed: This attribute is optional.
   *                                  Function which will be called with an array of deleted resources
   *                                  each time resource(s) are destroyed.
   *        - {boolean} ignoreExistingResources:
   *                                  This attribute is optional. Default value is false.
   *                                  If set to true, onAvailable won't be called with
   *                                  existing resources.
   */
  async watchResources(resources, options) {
    const {
      onAvailable,
      onUpdated,
      onDestroyed,
      ignoreExistingResources = false,
    } = options;

    if (typeof onAvailable !== "function") {
      throw new Error(
        "ResourceCommand.watchResources expects an onAvailable function as argument"
      );
    }

    for (const type of resources) {
      if (!this._isValidResourceType(type)) {
        throw new Error(
          `ResourceCommand.watchResources invoked with an unknown type: "${type}"`
        );
      }
    }

    logger.debug("Start watching: " + resources.join(", "));

    // Copy the array in order to avoid the callsite to modify the list of watched resources by mutating the array.
    // You have to call (un)watchResources to update the list of resources being watched!
    resources = [...resources];

    // Pending watchers are used in unwatchResources to remove watchers which
    // are not fully registered yet. Store `onAvailable` which is the unique key
    // for a watcher, as well as the resources array, so that unwatchResources
    // can update the array if we stop watching a specific resource.
    const pendingWatcher = {
      resources,
      onAvailable,
    };
    this._pendingWatchers.add(pendingWatcher);

    // Bug 1675763: Watcher actor is not available in all situations yet.
    if (!this._listenerRegistered && this.watcherFront) {
      this._listenerRegistered = true;
      // Resources watched from the parent process will be emitted on the Watcher Actor.
      // So that we also have to listen for this event on it, in addition to all targets.
      this.watcherFront.on(
        "resources-available-array",
        this._onResourceAvailableArray.bind(this, {
          watcherFront: this.watcherFront,
        })
      );
      this.watcherFront.on(
        "resources-updated-array",
        this._onResourceUpdatedArray.bind(this, {
          watcherFront: this.watcherFront,
        })
      );
      this.watcherFront.on(
        "resources-destroyed-array",
        this._onResourceDestroyedArray.bind(this, {
          watcherFront: this.watcherFront,
        })
      );
    }

    const promises = [];
    for (const resource of resources) {
      promises.push(this._startListening(resource));
    }
    await Promise.all(promises);

    // The resource cache is immediately filled when receiving the sources, but they are
    // emitted with a delay due to throttling. Since the cache can contain resources that
    // will soon be emitted, we have to flush it before adding the new listeners.
    // Otherwise _forwardExistingResources might emit resources that will also be emitted by
    // the next `_notifyWatchers` call done when calling `_startListening`, which will pull the
    // "already existing" resources.
    this._notifyWatchers();

    // Update the _pendingWatchers set before adding the watcher to _watchers.
    this._pendingWatchers.delete(pendingWatcher);

    // If unwatchResources was called in the meantime, use pendingWatcher's
    // resources to get the updated list of watched resources.
    const watchedResources = pendingWatcher.resources;

    // If no resource needs to be watched anymore, do not add an empty watcher
    // to _watchers, and do not notify about cached resources.
    if (!watchedResources.length) {
      return;
    }

    // Register the watcher just after calling _startListening in order to avoid it being called
    // for already existing resources, which will optionally be notified via _forwardExistingResources
    this._watchers.push({
      resources: watchedResources,
      onAvailable,
      onUpdated,
      onDestroyed,
      pendingEvents: [],
    });

    if (!ignoreExistingResources) {
      await this._forwardExistingResources(watchedResources, onAvailable);
    }
  }

  /**
   * Stop watching for given type of resources.
   * See `watchResources` for the arguments as both methods receive the same.
   * Note that `onUpdated` and `onDestroyed` attributes of `options` aren't used here.
   * Only `onAvailable` attribute is looked up and we unregister all the other registered callbacks
   * when a matching available callback is found.
   */
  unwatchResources(resources, options) {
    const { onAvailable } = options;

    if (typeof onAvailable !== "function") {
      throw new Error(
        "ResourceCommand.unwatchResources expects an onAvailable function as argument"
      );
    }

    for (const type of resources) {
      if (!this._isValidResourceType(type)) {
        throw new Error(
          `ResourceCommand.unwatchResources invoked with an unknown type: "${type}"`
        );
      }
    }

    logger.debug("Stop watching: " + resources.join(", "));

    // Unregister the callbacks from the watchers registries.
    // Check _watchers for the fully initialized watchers, as well as
    // `_pendingWatchers` for new watchers still being created by `watchResources`
    const allWatchers = [...this._watchers, ...this._pendingWatchers];
    for (const watcherEntry of allWatchers) {
      // onAvailable is the only mandatory argument which ends up being used to match
      // the right watcher entry.
      if (watcherEntry.onAvailable == onAvailable) {
        // Remove all resources that we stop watching. We may still watch for some others.
        watcherEntry.resources = watcherEntry.resources.filter(resourceType => {
          return !resources.includes(resourceType);
        });
      }
    }
    this._watchers = this._watchers.filter(entry => {
      // Remove entries entirely if it isn't watching for any resource type
      return !!entry.resources.length;
    });

    // Stop listening to all resources for which we removed the last watcher
    for (const resource of resources) {
      const isResourceWatched = allWatchers.some(watcherEntry =>
        watcherEntry.resources.includes(resource)
      );

      // Also check in _listenedResources as we may call unwatchResources
      // for resources that we haven't started watching for.
      if (!isResourceWatched && this._listenedResources.has(resource)) {
        this._stopListening(resource);
      }
    }

    // Stop watching for targets if we removed the last listener.
    if (this._listenedResources.size == 0) {
      this._unwatchAllTargets();
    }
  }

  /**
   * Wait for a single resource of the provided resourceType.
   *
   * @param {string} resourceType
   *        One of ResourceCommand.TYPES, type of the expected resource.
   * @param {object} additional options
   *        - {Boolean} ignoreExistingResources: ignore existing resources or not.
   *        - {Function} predicate: if provided, will wait until a resource makes
   *          predicate(resource) return true.
   * @return {Promise<object>}
   *         Return a promise which resolves once we fully settle the resource listener.
   *         You should await for its resolution before doing the action which may fire
   *         your resource.
   *         This promise will expose an object with `onResource` attribute,
   *         itself being a promise, which will resolve once a matching resource is received.
   */
  async waitForNextResource(
    resourceType,
    { ignoreExistingResources = false, predicate } = {}
  ) {
    // If no predicate was provided, convert to boolean to avoid resolving for
    // empty `resources` arrays.
    predicate = predicate || (resource => !!resource);

    let resolve;
    const promise = new Promise(r => (resolve = r));
    const onAvailable = async resources => {
      const matchingResource = resources.find(resource => predicate(resource));
      if (matchingResource) {
        this.unwatchResources([resourceType], { onAvailable });
        resolve(matchingResource);
      }
    };

    await this.watchResources([resourceType], {
      ignoreExistingResources,
      onAvailable,
    });
    return { onResource: promise };
  }

  /**
   * Check if there are any watchers for the specified resource.
   *
   * @param {string} resourceType
   *         One of ResourceCommand.TYPES
   * @return {boolean}
   *         If the resources type is beibg watched.
   */
  isResourceWatched(resourceType) {
    return this._listenedResources.has(resourceType);
  }

  /**
   * Start watching for all already existing and future targets.
   *
   * We are using ALL_TYPES, but this won't force listening to all types.
   * It will only listen for types which are defined by `TargetCommand.startListening`.
   */
  async _watchAllTargets() {
    if (!this._watchTargetsPromise) {
      // If this is the very first listener registered, of all kind of resource types:
      // * we want to start observing targets via TargetCommand
      // * _onTargetAvailable will be called for each already existing targets and the next one to come
      this._watchTargetsPromise = this.targetCommand.watchTargets({
        types: this.targetCommand.ALL_TYPES,
        onAvailable: this._onTargetAvailable,
        onDestroyed: this._onTargetDestroyed,
      });
    }
    return this._watchTargetsPromise;
  }

  _unwatchAllTargets() {
    if (!this._watchTargetsPromise) {
      return;
    }

    for (const offList of this._offTargetFrontListeners.values()) {
      offList.forEach(off => off());
    }
    this._offTargetFrontListeners.clear();

    this._watchTargetsPromise = null;
    this.targetCommand.unwatchTargets({
      types: this.targetCommand.ALL_TYPES,
      onAvailable: this._onTargetAvailable,
      onDestroyed: this._onTargetDestroyed,
    });
  }

  /**
   * For a given resource type, start the legacy listeners for all already existing targets.
   * Do that only if we have to. If this resourceType requires legacy listeners.
   */
  async _startLegacyListenersForExistingTargets(resourceType) {
    // If we were already listening to targets, we want to start the legacy listeners
    // for all already existing targets.
    //
    // Only try instantiating the legacy listener, if this resource type:
    //   - has legacy listener implementation
    // (new resource types may not be supported by old runtime and just not be received without breaking anything)
    //   - isn't supported by the server, or, the target type requires the a legacy listener implementation.
    const shouldRunLegacyListeners =
      resourceType in LegacyListeners &&
      (!this.hasResourceCommandSupport(resourceType) ||
        this._shouldRunLegacyListenerEvenWithWatcherSupport(resourceType));
    if (shouldRunLegacyListeners) {
      const promises = [];
      const targets = this.targetCommand.getAllTargets(
        this.targetCommand.ALL_TYPES
      );
      for (const targetFront of targets) {
        // We disable warning in case we already registered the legacy listener for this target
        // as this code may race with the call from onTargetAvailable if we end up having multiple
        // calls to _startListening in parallel.
        promises.push(
          this._watchResourcesForTarget({
            targetFront,
            resourceType,
            disableWarning: true,
          })
        );
      }
      await Promise.all(promises);
    }
  }

  /**
   * Method called by the TargetCommand for each already existing or target which has just been created.
   *
   * @param {object} arg
   * @param {Front} arg.targetFront
   *        The Front of the target that is available.
   *        This Front inherits from TargetMixin and is typically
   *        composed of a WindowGlobalTargetFront or ContentProcessTargetFront.
   * @param {boolean} arg.isTargetSwitching
   *         true when the new target was created because of a target switching.
   */
  async _onTargetAvailable({ targetFront, isTargetSwitching }) {
    const resources = [];
    if (isTargetSwitching) {
      // WatcherActor currently only watches additional frame targets and
      // explicitely ignores top level one that may be created when navigating
      // to a new process.
      // In order to keep working resources that are being watched via the
      // Watcher actor, we have to unregister and re-register the resource
      // types. This will force calling `Resources.watchResources` on the new top
      // level target.
      for (const resourceType of Object.values(ResourceCommand.TYPES)) {
        // ...which has at least one listener...
        if (!this._listenedResources.has(resourceType)) {
          continue;
        }

        if (this._shouldRestartListenerOnTargetSwitching(resourceType)) {
          this._stopListening(resourceType, {
            bypassListenerCount: true,
          });
          resources.push(resourceType);
        }
      }
    }

    if (targetFront.isDestroyed()) {
      return;
    }

    // If we are target switching, we already stop & start listening to all the
    // currently monitored resources.
    if (!isTargetSwitching) {
      // For each resource type...
      for (const resourceType of Object.values(ResourceCommand.TYPES)) {
        // ...which has at least one listener...
        if (!this._listenedResources.has(resourceType)) {
          continue;
        }
        // ...request existing resource and new one to come from this one target
        // *but* only do that for backward compat, where we don't have the watcher API
        // (See bug 1626647)
        await this._watchResourcesForTarget({ targetFront, resourceType });
      }
    }

    // Compared to the TargetCommand and Watcher.watchTargets,
    // We do call Watcher.watchResources, but the events are fired on the target.
    // That's because the Watcher runs in the parent process/main thread, while resources
    // are available from the target's process/thread.
    const offResourceAvailableArray = targetFront.on(
      "resources-available-array",
      this._onResourceAvailableArray.bind(this, { targetFront })
    );
    const offResourceUpdatedArray = targetFront.on(
      "resources-updated-array",
      this._onResourceUpdatedArray.bind(this, { targetFront })
    );
    const offResourceDestroyedArray = targetFront.on(
      "resources-destroyed-array",
      this._onResourceDestroyedArray.bind(this, { targetFront })
    );

    const offList = this._offTargetFrontListeners.get(targetFront) || [];
    offList.push(
      offResourceAvailableArray,
      offResourceUpdatedArray,
      offResourceDestroyedArray
    );

    if (isTargetSwitching) {
      await Promise.all(
        resources.map(resourceType =>
          this._startListening(resourceType, {
            bypassListenerCount: true,
          })
        )
      );
    }

    // DOCUMENT_EVENT's will-navigate should replace target actor's will-navigate event,
    // but only for targets provided by the watcher actor.
    // Emit a fake DOCUMENT_EVENT's "will-navigate" out of target actor's will-navigate
    // until watcher actor is supported by all descriptors (bug 1675763).
    if (!this.targetCommand.hasTargetWatcherSupport()) {
      const offWillNavigate = targetFront.on(
        "will-navigate",
        ({ url, isFrameSwitching }) => {
          targetFront.emit("resource-available-form", [
            {
              resourceType: this.TYPES.DOCUMENT_EVENT,
              name: "will-navigate",
              time: Date.now(), // will-navigate was not passing any timestamp
              isFrameSwitching,
              newURI: url,
            },
          ]);
        }
      );
      offList.push(offWillNavigate);
    }

    this._offTargetFrontListeners.set(targetFront, offList);
  }

  _shouldRestartListenerOnTargetSwitching(resourceType) {
    // Note that we aren't using isServerTargetSwitchingEnabled, nor checking the
    // server side target switching preference as we may have server side targets
    // even when this is false/disabled.
    // This will happen for bfcache navigations, even with server side targets disabled.
    // `followWindowGlobalLifeCycle` will be false for the first top level target
    // and only become true when doing a bfcache navigation.
    // (only server side targets follow the WindowGlobal lifecycle)
    // When server side targets are enabled, this will always be true.
    const isServerSideTarget =
      this.targetCommand.targetFront.targetForm.followWindowGlobalLifeCycle;
    if (isServerSideTarget) {
      // For top-level targets created from the server, only restart legacy
      // listeners.
      return !this.hasResourceCommandSupport(resourceType);
    }

    // For top-level targets created from the client we should always restart
    // listeners.
    return true;
  }

  /**
   * Method called by the TargetCommand when a target has just been destroyed
   *
   * @param {object} arg
   * @param {Front} arg.targetFront
   *        The Front of the target that was destroyed
   * @param {boolean} arg.isModeSwitching
   *         true when this is called as the result of a change to the devtools.browsertoolbox.scope pref.
   */
  _onTargetDestroyed({ targetFront, isModeSwitching }) {
    // Clear the map of legacy listeners for this target.
    this._existingLegacyListeners.set(targetFront, []);
    this._offTargetFrontListeners.delete(targetFront);

    // Purge the cache from any resource related to the destroyed target.
    // Top level BrowsingContext target will be purge via DOCUMENT_EVENT will-navigate events.
    // If we were to clean resources from target-destroyed, we will clear resources
    // happening between will-navigate and target-destroyed. Typically the navigation request
    // At the moment, isModeSwitching can only be true when targetFront.isTopLevel isn't true,
    // so we don't need to add a specific check for isModeSwitching.
    if (!targetFront.isTopLevel || !targetFront.isBrowsingContext) {
      for (const [key, resource] of this._cache) {
        if (resource.targetFront === targetFront) {
          // NOTE: To anyone paranoid like me, yes it is okay to delete from a Map while iterating it.
          this._cache.delete(key);
        }
      }
    }

    // Purge "available" pendingEvents for resources from the destroyed target when switching
    // mode as we want to ignore those.
    if (isModeSwitching) {
      for (const watcherEntry of this._watchers) {
        for (const pendingEvent of watcherEntry.pendingEvents) {
          if (pendingEvent.callbackType == "available") {
            pendingEvent.updates = pendingEvent.updates.filter(
              update => update.targetFront !== targetFront
            );
          }
        }
      }
    }
  }

  async _onResourceAvailableArray({ targetFront, watcherFront }, array) {
    let includesDocumentEventWillNavigate = false;
    let includesDocumentEventDomLoading = false;
    for (const [resourceType, resources] of array) {
      const isAlreadyExistingResource =
        this._processingExistingResources.has(resourceType);
      const transformer = ResourceTransformers[resourceType];

      for (let i = 0; i < resources.length; i++) {
        let resource = resources[i];
        if (!("resourceType" in resource)) {
          resource.resourceType = resourceType;
        }

        if (watcherFront) {
          try {
            targetFront = await this._getTargetForWatcherResource(resource);
          } catch (e) {
            if (this.#destroyed) {
              // If the resource-command was destroyed while waiting for
              // _getTargetForWatcherResource, swallow the error.
              return;
            }
            throw e;
          }
          // When we receive resources from the Watcher actor,
          // there is no guarantee that the target front is fully initialized.
          // The Target Front is initialized by the TargetCommand, by calling TargetFront.attachAndInitThread.
          // We have to wait for its completion as resources watchers are expecting it to be completed.
          //
          // But when navigating, we may receive resources packets for a destroyed target.
          // Or, in the context of the browser toolbox, they may not relate to any target.
          if (targetFront) {
            await targetFront.initialized;
          }
        }

        // Put the targetFront on the resource for easy retrieval.
        // (Resources from the legacy listeners may already have the attribute set)
        if (!resource.targetFront) {
          resource.targetFront = targetFront;
        }

        if (transformer) {
          resource = transformer({
            resource,
            targetCommand: this.targetCommand,
            targetFront,
            watcherFront: this.watcherFront,
          });
          resources[i] = resource;
        }

        // isAlreadyExistingResource indicates that the resources already existed before
        // the resource command started watching for this type of resource.
        resource.isAlreadyExistingResource = isAlreadyExistingResource;

        if (!resource.resourceId) {
          resource.resourceId = `auto:${++gLastResourceId}`;
        }

        logger.debug(
          `Received resource available from backend: type=${resource.resourceType}, resourceId=${resource.resourceId}`
        );

        // Only consider top level document, and ignore remote iframes top document
        let isWillNavigate = false;
        if (resourceType == DOCUMENT_EVENT) {
          isWillNavigate = resource.name === "will-navigate";
          const isBrowserToolbox =
            this.targetCommand.descriptorFront.isBrowserProcessDescriptor;
          if (
            isWillNavigate &&
            resource.targetFront.isTopLevel &&
            // When selecting a document in the Browser Toolbox iframe picker, we're getting
            // a will-navigate event. In such case, we don't want to clear the cache,
            // otherwise we'd miss some resources that we might already received (e.g. stylesheets)
            // See Bug 1981937
            (!isBrowserToolbox || !resource.isFrameSwitching)
          ) {
            includesDocumentEventWillNavigate = true;
            this._onWillNavigate(resource.targetFront);
          }

          if (
            resource.name === "dom-loading" &&
            resource.targetFront.isTopLevel
          ) {
            includesDocumentEventDomLoading = true;
          }
        }

        // Avoid storing will-navigate resource and consider it as a transcient resource.
        // We do that to prevent leaking this resource (and its target) on navigation.
        // We do clear the cache in _onWillNavigate, that we call a few lines before this.
        if (!isWillNavigate) {
          this.addResourceToCache(resource);
        }
      }

      this._queueResourceEvent("available", resourceType, resources);
    }

    // If we receive the DOCUMENT_EVENT for:
    // - will-navigate
    // - dom-loading + we're using the service worker legacy listener
    // then flush immediately the resources to notify about the navigation sooner than later.
    // (this is especially useful for tests, even if they should probably avoid depending on this...)
    if (
      includesDocumentEventWillNavigate ||
      (includesDocumentEventDomLoading &&
        !this.targetCommand.hasTargetWatcherSupport("service_worker")) ||
      this.throttlingDisabled
    ) {
      this._notifyWatchers();
    } else {
      this._throttledNotifyWatchers();
    }
  }

  async _onResourceUpdatedArray(context, array) {
    for (const [resourceType, resources] of array) {
      for (const resource of resources) {
        if (!("resourceType" in resource)) {
          resource.resourceType = resourceType;
        }
      }
      await this._onResourceUpdated(context, resources);
    }
  }

  async _onResourceDestroyedArray(context, array) {
    const resources = [];
    for (const [resourceType, resourceIds] of array) {
      for (const resourceId of resourceIds) {
        resources.push({ resourceType, resourceId });
      }
    }
    await this._onResourceDestroyed(context, resources);
  }

  /**
   * Called every time a resource is updated in the remote target.
   *
   * Method called either by:
   * - the backward compatibility code (LegacyListeners)
   * - target actors RDP events
   *
   * @param {object} source
   *        A dictionary object with only one of these two attributes:
   *        - targetFront: a Target Front, if the resource is watched from the
   *          target process or thread.
   *        - watcherFront: a Watcher Front, if the resource is watched from
   *          the parent process.
   * @param {Array<object>} updates
   *        Depending on the listener.
   *
   *        Among the element in the array, the following attributes are given special handling.
   *          - resourceType {String}:
   *            The type of resource to be updated.
   *          - resourceId {String}:
   *            The id of resource to be updated.
   *          - resourceUpdates {Object}:
   *            If resourceUpdates is in the element, a cached resource specified by resourceType
   *            and resourceId is updated by Object.assign(cachedResource, resourceUpdates).
   *          - nestedResourceUpdates {Object}:
   *            If `nestedResourceUpdates` is passed, update one nested attribute with a new value
   *            This allows updating one attribute of an object stored in a resource's attribute,
   *            as well as adding new elements to arrays.
   *            `path` is an array mentioning all nested attribute to walk through.
   *            `value` is the new nested attribute value to set.
   *
   *        And also, the element is passed to the listener as it is as “update” object.
   *        So if we don't want to update a cached resource but have information want to
   *        pass on to the listener, can pass it on using attributes other than the ones
   *        listed above.
   *        For example, if the element consists of like
   *        "{ resourceType:… resourceId:…, testValue: “test”, }”,
   *        the listener can receive the value as follows.
   *
   *        onResourceUpdate({ update }) {
   *          console.log(update.testValue); // “test” should be displayed
   *        }
   */
  async _onResourceUpdated({ targetFront, watcherFront }, updates) {
    for (const update of updates) {
      const {
        resourceType,
        resourceId,
        resourceUpdates,
        nestedResourceUpdates,
      } = update;

      logger.debug(
        `Received resource update from backend: type=${resourceType}, resourceId=${resourceId}`
      );

      if (!resourceId) {
        console.warn(`Expected resource ${resourceType} to have a resourceId`);
      }

      // See _onResourceAvailableArray()
      // We also need to wait for the related targetFront to be initialized
      // otherwise we would notify about the update *before* it's available
      // and the resource won't be in _cache.
      if (watcherFront) {
        try {
          targetFront = await this._getTargetForWatcherResource(update);
        } catch (e) {
          if (this.#destroyed) {
            // If the resource-command was destroyed while waiting for
            // _getTargetForWatcherResource, swallow the error.
            return;
          }
          throw e;
        }

        // When we receive the navigation request, the target front has already been
        // destroyed, but this is fine. The cached resource has the reference to
        // the (destroyed) target front and it is fully initialized.
        if (targetFront) {
          await targetFront.initialized;
        }
      }

      const existingResource = this._cache.get(
        cacheKey(resourceType, resourceId)
      );
      if (!existingResource) {
        continue;
      }

      if (resourceUpdates) {
        Object.assign(existingResource, resourceUpdates);
      }

      if (nestedResourceUpdates) {
        for (const { path, value } of nestedResourceUpdates) {
          let target = existingResource;

          for (let i = 0; i < path.length - 1; i++) {
            target = target[path[i]];
          }

          target[path[path.length - 1]] = value;
        }
      }
      this._queueResourceEvent("updated", resourceType, [
        {
          resource: existingResource,
          update,
        },
      ]);
    }

    this._throttledNotifyWatchers();
  }

  /**
   * Called every time a resource is destroyed in the remote target.
   *
   * @param {object} source
   *        A dictionary object with only one of these two attributes:
   *        - targetFront: a Target Front, if the resource is watched from the
   *          target process or thread.
   *        - watcherFront: a Watcher Front, if the resource is watched from
   *          the parent process.
   * @param {Array<json/Front>} resources
   *        Depending on the resource Type, it can be an Array composed of
   *        either JSON objects or Fronts, which describes the resource.
   */
  async _onResourceDestroyed({ targetFront }, resources) {
    for (const resource of resources) {
      const { resourceType, resourceId } = resource;
      logger.debug(
        `Received resource destroyed from backend: type=${resourceType}, resourceId=${resourceId}`
      );

      this._cache.delete(cacheKey(resourceType, resourceId));
      if (!resource.targetFront) {
        resource.targetFront = targetFront;
      }
      this._queueResourceEvent("destroyed", resourceType, [resource]);
    }
    this._throttledNotifyWatchers();
  }

  _queueResourceEvent(callbackType, resourceType, updates) {
    for (const { resources, pendingEvents } of this._watchers) {
      // This watcher doesn't listen to this type of resource
      if (!resources.includes(resourceType)) {
        continue;
      }
      // Avoid trying to coalesce with last pending event as mutating `updates` may have side effects
      // with other watchers as this array is shared between all the watchers.
      pendingEvents.push({
        callbackType,
        updates,
      });
    }
  }

  /**
   * Flush the pending event and notify all the currently registered watchers
   * about all the available, updated and destroyed events that have been accumulated in
   * `_watchers`'s `pendingEvents` arrays.
   */
  _notifyWatchers() {
    for (const watcherEntry of this._watchers) {
      const { onAvailable, onUpdated, onDestroyed, pendingEvents } =
        watcherEntry;

      if (!pendingEvents.length) {
        // Bail out early if there are no events to handle.
        continue;
      }

      logger.debug(
        `Notify a ResourceCommand listener for ${pendingEvents.length} pendingEvents`
      );
      // Immediately clear the buffer in order to avoid possible races, where an event listener
      // would end up somehow adding a new throttled resource
      watcherEntry.pendingEvents = [];

      for (const { callbackType, updates } of pendingEvents) {
        try {
          if (callbackType == "available") {
            onAvailable(updates, { areExistingResources: false });
          } else if (callbackType == "updated" && onUpdated) {
            onUpdated(updates);
          } else if (callbackType == "destroyed" && onDestroyed) {
            onDestroyed(updates);
          }
        } catch (e) {
          console.error(
            "Exception while calling a ResourceCommand",
            callbackType,
            "callback",
            ":",
            e
          );
        }
      }
    }
  }

  // Compute the target front if the resource comes from the Watcher Actor.
  // (`targetFront` will be null as the watcher is in the parent process
  // and targets are in distinct processes)
  _getTargetForWatcherResource(resource) {
    const { browsingContextID, innerWindowId, resourceType } = resource;

    // Some privileged resources aren't related to any BrowsingContext
    // and so aren't bound to any Target Front.
    // Server watchers should pass an explicit "-1" value in order to prevent
    // silently ignoring an undefined browsingContextID attribute.
    if (browsingContextID == -1) {
      return this.targetCommand.targetFront;
    }

    if (innerWindowId && this.targetCommand.isServerTargetSwitchingEnabled()) {
      return this.watcherFront.getWindowGlobalTargetByInnerWindowId(
        innerWindowId
      );
    } else if (browsingContextID) {
      return this.watcherFront.getWindowGlobalTarget(browsingContextID);
    }
    console.error(
      `Resource of ${resourceType} is missing a browsingContextID or innerWindowId attribute`
    );
    return null;
  }

  _onWillNavigate() {
    // Special case for toolboxes debugging a document,
    // purge the cache entirely when we start navigating to a new document.
    // Other toolboxes and additional target for remote iframes or content process
    // will be purge from onTargetDestroyed.

    // NOTE: we could `clear` the cache here, but technically if anything is
    // currently iterating over resources provided by getAllResources, that
    // would interfere with their iteration. We just assign a new Map here to
    // leave those iterators as is.
    this._cache = new Map();
  }

  /**
   * Tells if the server supports listening to the given resource type
   * via the watcher actor's watchResources method.
   *
   * @return {boolean} True, if the server supports this type.
   */
  hasResourceCommandSupport(resourceType) {
    return this.watcherFront?.traits?.resources?.[resourceType];
  }

  /**
   * Tells if the server supports listening to the given resource type
   * via the watcher actor's watchResources method, and that, for a specific
   * target.
   *
   * @return {boolean} True, if the server supports this type.
   */
  _hasResourceCommandSupportForTarget(resourceType, targetFront) {
    // First check if the watcher supports this target type.
    // If it doesn't, no resource type can be listened via the Watcher actor for this target.
    if (!this.targetCommand.hasTargetWatcherSupport(targetFront.targetType)) {
      return false;
    }

    return this.hasResourceCommandSupport(resourceType);
  }

  _isValidResourceType(type) {
    return this.ALL_TYPES.includes(type);
  }

  /**
   * Start listening for a given type of resource.
   * For backward compatibility code, we register the legacy listeners on
   * each individual target
   *
   * @param {string} resourceType
   *        One string of ResourceCommand.TYPES, which designates the types of resources
   *        to be listened.
   * @param {object}
   *        - {Boolean} bypassListenerCount
   *          Pass true to avoid checking/updating the listenersCount map.
   *          Exclusively used when target switching, to stop & start listening
   *          to all resources.
   */
  async _startListening(resourceType, { bypassListenerCount = false } = {}) {
    if (!bypassListenerCount) {
      if (this._listenedResources.has(resourceType)) {
        return;
      }
      this._listenedResources.add(resourceType);
    }

    this._processingExistingResources.add(resourceType);

    // Ensuring enabling listening to targets.
    // This will be a no-op expect for the very first call to `_startListening`,
    // where it is going to call `onTargetAvailable` for all already existing targets,
    // as well as for those who will be created later.
    //
    // Do this *before* calling WatcherActor.watchResources in order to register "resource-available"
    // listeners on targets before these events start being emitted.
    await this._watchAllTargets(resourceType);

    // When we are calling _startListening for the first time, _watchAllTargets
    // will register legacylistener when it will call onTargetAvailable for all existing targets.
    // But for any next calls to _startListening, _watchAllTargets will be a no-op,
    // and nothing will start legacy listener for each already registered targets.
    await this._startLegacyListenersForExistingTargets(resourceType);

    // If the server supports the Watcher API and the Watcher supports
    // this resource type, use this API
    if (this.hasResourceCommandSupport(resourceType)) {
      await this.watcherFront.watchResources([resourceType]);
    }
    this._processingExistingResources.delete(resourceType);
  }

  /**
   * Return true if the resource should be watched via legacy listener,
   * even when watcher supports this resource type.
   *
   * Bug 1678385: In order to support watching for JS Source resource
   * for service workers and parent process workers, which aren't supported yet
   * by the watcher actor, we do not bail out here and allow to execute
   * the legacy listener for these targets.
   * Once bug 1608848 is fixed, we can remove this and never trigger
   * the legacy listeners codepath for these resource types.
   *
   * If this isn't fixed soon, we may add other resources we want to see
   * being fetched from these targets.
   */
  _shouldRunLegacyListenerEvenWithWatcherSupport(resourceType) {
    return WORKER_RESOURCE_TYPES.includes(resourceType);
  }

  async _forwardExistingResources(resourceTypes, onAvailable) {
    const existingResources = [];
    for (const resource of this._cache.values()) {
      if (resourceTypes.includes(resource.resourceType)) {
        existingResources.push(resource);
      }
    }
    if (existingResources.length) {
      await onAvailable(existingResources, { areExistingResources: true });
    }
  }

  /**
   * Call backward compatibility code from `LegacyListeners` in order to listen for a given
   * type of resource from a given target.
   */
  async _watchResourcesForTarget({
    targetFront,
    resourceType,
    disableWarning = false,
  }) {
    if (this._hasResourceCommandSupportForTarget(resourceType, targetFront)) {
      // This resource / target pair should already be handled by the watcher,
      // no need to start legacy listeners.
      return;
    }

    // All workers target types are still not supported by the watcher
    // so that we have to spawn legacy listener for all their resources.
    // But some resources are irrelevant to workers, like network events.
    // And we removed the related legacy listener as they are no longer used.
    if (
      targetFront.targetType.endsWith("worker") &&
      !WORKER_RESOURCE_TYPES.includes(resourceType)
    ) {
      return;
    }

    if (targetFront.isDestroyed()) {
      return;
    }

    const onAvailableArray = this._onResourceAvailableArray.bind(this, {
      targetFront,
    });
    const onUpdatedArray = this._onResourceUpdatedArray.bind(this, {
      targetFront,
    });
    const onDestroyedArray = this._onResourceDestroyedArray.bind(this, {
      targetFront,
    });

    if (!(resourceType in LegacyListeners)) {
      throw new Error(`Missing legacy listener for ${resourceType}`);
    }

    const legacyListeners =
      this._existingLegacyListeners.get(targetFront) || [];
    if (legacyListeners.includes(resourceType)) {
      if (!disableWarning) {
        console.warn(
          `Already started legacy listener for ${resourceType} on ${targetFront.actorID}`
        );
      }
      return;
    }
    this._existingLegacyListeners.set(
      targetFront,
      legacyListeners.concat(resourceType)
    );

    try {
      await LegacyListeners[resourceType]({
        targetCommand: this.targetCommand,
        targetFront,
        onAvailableArray,
        onDestroyedArray,
        onUpdatedArray,
      });
    } catch (e) {
      // Swallow the error to avoid breaking calls to watchResources which will
      // loop on all existing targets to create legacy listeners.
      // If a legacy listener fails to handle a target for some reason, we
      // should still try to process other targets as much as possible.
      // See Bug 1687645.
      console.error(
        `Failed to start [${resourceType}] legacy listener for target ${targetFront.actorID}`,
        e
      );
    }
  }

  /**
   * Reverse of _startListening. Stop listening for a given type of resource.
   * For backward compatibility, we unregister from each individual target.
   *
   * See _startListening for parameters description.
   */
  _stopListening(resourceType, { bypassListenerCount = false } = {}) {
    if (!bypassListenerCount) {
      if (!this._listenedResources.has(resourceType)) {
        throw new Error(
          `Stopped listening for resource '${resourceType}' that isn't being listened to`
        );
      }
      this._listenedResources.delete(resourceType);
    }

    // Clear the cached resources of the type.
    for (const [key, resource] of this._cache) {
      if (resource.resourceType == resourceType) {
        // NOTE: To anyone paranoid like me, yes it is okay to delete from a Map while iterating it.
        this._cache.delete(key);
      }
    }

    // If the server supports the Watcher API and the Watcher supports
    // this resource type, use this API
    if (this.hasResourceCommandSupport(resourceType)) {
      if (!this.watcherFront.isDestroyed()) {
        this.watcherFront.unwatchResources([resourceType]);
      }

      const shouldRunLegacyListeners =
        this._shouldRunLegacyListenerEvenWithWatcherSupport(resourceType);
      if (!shouldRunLegacyListeners) {
        return;
      }
    }
    // Otherwise, fallback on backward compat mode and use LegacyListeners.

    // If this was the last listener, we should stop watching these events from the actors
    // and the actors should stop watching things from the platform
    const targets = this.targetCommand.getAllTargets(
      this.targetCommand.ALL_TYPES
    );
    for (const target of targets) {
      this._unwatchResourcesForTarget(target, resourceType);
    }
  }

  /**
   * Backward compatibility code, reverse of _watchResourcesForTarget.
   */
  _unwatchResourcesForTarget(targetFront, resourceType) {
    if (this._hasResourceCommandSupportForTarget(resourceType, targetFront)) {
      // This resource / target pair should already be handled by the watcher,
      // no need to stop legacy listeners.
    }
    // Is there really a point in:
    // - unregistering `onAvailable` RDP event callbacks from target-scoped actors?
    // - calling `stopListeners()` as we are most likely closing the toolbox and destroying everything?
    //
    // It is important to keep this method synchronous and do as less as possible
    // in the case of toolbox destroy.
    //
    // We are aware of one case where that might be useful.
    // When a panel is disabled via the options panel, after it has been opened.
    // Would that justify doing this? Is there another usecase?

    // XXX: This is most likely only needed to avoid growing the Map infinitely.
    // Unless in the "disabled panel" use case mentioned in the comment above,
    // we should not see the same target actorID again.
    const listeners = this._existingLegacyListeners.get(targetFront);
    if (listeners && listeners.includes(resourceType)) {
      const remainingListeners = listeners.filter(l => l !== resourceType);
      this._existingLegacyListeners.set(targetFront, remainingListeners);
    }
  }
}

const DOCUMENT_EVENT = "document-event";
ResourceCommand.TYPES = ResourceCommand.prototype.TYPES = {
  CONSOLE_MESSAGE: "console-message",
  CSS_CHANGE: "css-change",
  CSS_MESSAGE: "css-message",
  CSS_REGISTERED_PROPERTIES: "css-registered-properties",
  ERROR_MESSAGE: "error-message",
  PLATFORM_MESSAGE: "platform-message",
  DOCUMENT_EVENT,
  ROOT_NODE: "root-node",
  STYLESHEET: "stylesheet",
  NETWORK_EVENT: "network-event",
  NETWORK_EVENT_DECODED_BODY_SIZE: "network-event-decoded-body-size",
  WEBSOCKET: "websocket",
  WEBTRANSPORT: "webtransport",
  COOKIE: "cookies",
  LOCAL_STORAGE: "local-storage",
  SESSION_STORAGE: "session-storage",
  CACHE_STORAGE: "Cache",
  EXTENSION_STORAGE: "extension-storage",
  INDEXED_DB: "indexed-db",
  NETWORK_EVENT_STACKTRACE: "network-event-stacktrace",
  REFLOW: "reflow",
  SOURCE: "source",
  THREAD_STATE: "thread-state",
  JSTRACER_TRACE: "jstracer-trace",
  JSTRACER_STATE: "jstracer-state",
  SERVER_SENT_EVENT: "server-sent-event",
  SESSION_HISTORY: "session-history",
  LAST_PRIVATE_CONTEXT_EXIT: "last-private-context-exit",
};
ResourceCommand.ALL_TYPES = ResourceCommand.prototype.ALL_TYPES = Object.values(
  ResourceCommand.TYPES
);
module.exports = ResourceCommand;

// This is the list of resource types supported by workers.
// We need such list to know when forcing to run the legacy listeners
// and when to avoid try to spawn some unsupported ones for workers.
const WORKER_RESOURCE_TYPES = [
  ResourceCommand.TYPES.CONSOLE_MESSAGE,
  ResourceCommand.TYPES.ERROR_MESSAGE,
  ResourceCommand.TYPES.SOURCE,
  ResourceCommand.TYPES.THREAD_STATE,
];

// List of resource types which aren't stored in the internal ResourceCommand cache.
// Only the first `watchResources()` call for a given resource type may receive already existing
// resources. All subsequent call to `watchResources()` for the same resource type will
// only receive future resource, and not the one already notified in the past.
// This is typically used for resources with very high throughput.
const TRANSIENT_RESOURCE_TYPES = [
  ResourceCommand.TYPES.JSTRACER_TRACE,
  ResourceCommand.TYPES.JSTRACER_STATE,
];

// Backward compat code for each type of resource.
// Each section added here should eventually be removed once the equivalent server
// code is implement in Firefox, in its release channel.
const LegacyListeners = {
  async [ResourceCommand.TYPES.DOCUMENT_EVENT]({ targetFront, onAvailable }) {
    // DocumentEventsListener of webconsole handles only top level document.
    if (!targetFront.isTopLevel) {
      return;
    }

    const webConsoleFront = await targetFront.getFront("console");
    webConsoleFront.on("documentEvent", event => {
      event.resourceType = ResourceCommand.TYPES.DOCUMENT_EVENT;
      onAvailable([event]);
    });
    await webConsoleFront.startListeners(["DocumentEvents"]);
  },
};
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.CONSOLE_MESSAGE,
  "resource://devtools/shared/commands/resource/legacy-listeners/console-messages.js"
);
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.CSS_MESSAGE,
  "resource://devtools/shared/commands/resource/legacy-listeners/css-messages.js"
);
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.ERROR_MESSAGE,
  "resource://devtools/shared/commands/resource/legacy-listeners/error-messages.js"
);
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.PLATFORM_MESSAGE,
  "resource://devtools/shared/commands/resource/legacy-listeners/platform-messages.js"
);
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.ROOT_NODE,
  "resource://devtools/shared/commands/resource/legacy-listeners/root-node.js"
);

loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.SOURCE,
  "resource://devtools/shared/commands/resource/legacy-listeners/source.js"
);
loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.THREAD_STATE,
  "resource://devtools/shared/commands/resource/legacy-listeners/thread-states.js"
);

loader.lazyRequireGetter(
  LegacyListeners,
  ResourceCommand.TYPES.REFLOW,
  "resource://devtools/shared/commands/resource/legacy-listeners/reflow.js"
);

// Optional transformers for each type of resource.
// Each module added here should be a function that will receive the resource, the target, …
// and perform some transformation on the resource before it will be emitted.
// This is a good place to handle backward compatibility and manual resource marshalling.
const ResourceTransformers = {};

loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.CONSOLE_MESSAGE,
  "resource://devtools/shared/commands/resource/transformers/console-messages.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.ERROR_MESSAGE,
  "resource://devtools/shared/commands/resource/transformers/error-messages.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.CACHE_STORAGE,
  "resource://devtools/shared/commands/resource/transformers/storage-cache.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.COOKIE,
  "resource://devtools/shared/commands/resource/transformers/storage-cookie.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.EXTENSION_STORAGE,
  "resource://devtools/shared/commands/resource/transformers/storage-extension.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.INDEXED_DB,
  "resource://devtools/shared/commands/resource/transformers/storage-indexed-db.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.LOCAL_STORAGE,
  "resource://devtools/shared/commands/resource/transformers/storage-local-storage.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.SESSION_STORAGE,
  "resource://devtools/shared/commands/resource/transformers/storage-session-storage.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.NETWORK_EVENT,
  "resource://devtools/shared/commands/resource/transformers/network-events.js"
);
loader.lazyRequireGetter(
  ResourceTransformers,
  ResourceCommand.TYPES.THREAD_STATE,
  "resource://devtools/shared/commands/resource/transformers/thread-states.js"
);
