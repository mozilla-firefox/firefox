/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Targets = require("resource://devtools/server/actors/targets/index.js");

const TYPES = {
  CONSOLE_MESSAGE: "console-message",
  CSS_CHANGE: "css-change",
  CSS_MESSAGE: "css-message",
  CSS_REGISTERED_PROPERTIES: "css-registered-properties",
  DOCUMENT_EVENT: "document-event",
  ERROR_MESSAGE: "error-message",
  LAST_PRIVATE_CONTEXT_EXIT: "last-private-context-exit",
  NETWORK_EVENT: "network-event",
  NETWORK_EVENT_DECODED_BODY_SIZE: "network-event-decoded-body-size",
  NETWORK_EVENT_STACKTRACE: "network-event-stacktrace",
  PLATFORM_MESSAGE: "platform-message",
  REFLOW: "reflow",
  SERVER_SENT_EVENT: "server-sent-event",
  SESSION_HISTORY: "session-history",
  SOURCE: "source",
  STYLESHEET: "stylesheet",
  THREAD_STATE: "thread-state",
  JSTRACER_TRACE: "jstracer-trace",
  JSTRACER_STATE: "jstracer-state",
  WEBSOCKET: "websocket",
  WEBTRANSPORT: "webtransport",

  // storage types
  CACHE_STORAGE: "Cache",
  COOKIE: "cookies",
  EXTENSION_STORAGE: "extension-storage",
  INDEXED_DB: "indexed-db",
  LOCAL_STORAGE: "local-storage",
  SESSION_STORAGE: "session-storage",

  // root types
  EXTENSIONS_BGSCRIPT_STATUS: "extensions-backgroundscript-status",
};
exports.TYPES = TYPES;

// Helper dictionaries, which will contain data specific to each resource type.
// - `path` is the absolute path to the module defining the Resource Watcher class.
//
// Also see the attributes added by `augmentResourceDictionary` for each type:
// - `watchers` is a weak map which will store Resource Watchers
//    (i.e. devtools/server/actors/resources/ class instances)
//    keyed by target actor -or- watcher actor.
// - `WatcherClass` is a shortcut to the Resource Watcher module.
//    Each module exports a Resource Watcher class.
//
// These are several dictionaries, which depend how the resource watcher classes are instantiated.

// Frame target resources are spawned via a BrowsingContext Target Actor.
// Their watcher class receives a target actor as first argument.
// They are instantiated for each observed BrowsingContext, from the content process where it runs.
// They are meant to observe all resources related to a given Browsing Context.
const FrameTargetResources = augmentResourceDictionary({
  [TYPES.CACHE_STORAGE]: {
    path: "devtools/server/actors/resources/storage-cache",
  },
  [TYPES.CONSOLE_MESSAGE]: {
    path: "devtools/server/actors/resources/console-messages",
  },
  [TYPES.CSS_CHANGE]: {
    path: "devtools/server/actors/resources/css-changes",
  },
  [TYPES.CSS_MESSAGE]: {
    path: "devtools/server/actors/resources/css-messages",
  },
  [TYPES.CSS_REGISTERED_PROPERTIES]: {
    path: "devtools/server/actors/resources/css-registered-properties",
  },
  [TYPES.DOCUMENT_EVENT]: {
    path: "devtools/server/actors/resources/document-event",
  },
  [TYPES.ERROR_MESSAGE]: {
    path: "devtools/server/actors/resources/error-messages",
  },
  [TYPES.JSTRACER_STATE]: {
    path: "devtools/server/actors/resources/jstracer-state",
  },
  [TYPES.JSTRACER_TRACE]: {
    path: "devtools/server/actors/resources/jstracer-trace",
  },
  [TYPES.LOCAL_STORAGE]: {
    path: "devtools/server/actors/resources/storage-local-storage",
  },
  [TYPES.PLATFORM_MESSAGE]: {
    path: "devtools/server/actors/resources/platform-messages",
  },
  [TYPES.SESSION_STORAGE]: {
    path: "devtools/server/actors/resources/storage-session-storage",
  },
  [TYPES.STYLESHEET]: {
    path: "devtools/server/actors/resources/stylesheets",
  },
  [TYPES.NETWORK_EVENT]: {
    path: "devtools/server/actors/resources/network-events-content",
  },
  [TYPES.NETWORK_EVENT_DECODED_BODY_SIZE]: {
    path: "devtools/server/actors/resources/network-events-decoded-body-size",
  },
  [TYPES.NETWORK_EVENT_STACKTRACE]: {
    path: "devtools/server/actors/resources/network-events-stacktraces",
  },
  [TYPES.REFLOW]: {
    path: "devtools/server/actors/resources/reflow",
  },
  [TYPES.SOURCE]: {
    path: "devtools/server/actors/resources/sources",
  },
  [TYPES.THREAD_STATE]: {
    path: "devtools/server/actors/resources/thread-states",
  },
  [TYPES.SERVER_SENT_EVENT]: {
    path: "devtools/server/actors/resources/server-sent-events",
  },
  [TYPES.WEBSOCKET]: {
    path: "devtools/server/actors/resources/websockets",
  },
  [TYPES.WEBTRANSPORT]: {
    path: "devtools/server/actors/resources/webtransport",
  },
});

// Process target resources are spawned via a Process Target Actor.
// Their watcher class receives a process target actor as first argument.
// They are instantiated for each observed Process (parent and all content processes).
// They are meant to observe all resources related to a given process.
const ProcessTargetResources = augmentResourceDictionary({
  [TYPES.CONSOLE_MESSAGE]: {
    path: "devtools/server/actors/resources/console-messages",
  },
  [TYPES.JSTRACER_TRACE]: {
    path: "devtools/server/actors/resources/jstracer-trace",
  },
  [TYPES.JSTRACER_STATE]: {
    path: "devtools/server/actors/resources/jstracer-state",
  },
  [TYPES.ERROR_MESSAGE]: {
    path: "devtools/server/actors/resources/error-messages",
  },
  [TYPES.PLATFORM_MESSAGE]: {
    path: "devtools/server/actors/resources/platform-messages",
  },
  [TYPES.SOURCE]: {
    path: "devtools/server/actors/resources/sources",
  },
  [TYPES.THREAD_STATE]: {
    path: "devtools/server/actors/resources/thread-states",
  },
});

// Worker target resources are spawned via a Worker Target Actor.
// Their watcher class receives a worker target actor as first argument.
// They are instantiated for each observed worker, from the worker thread.
// They are meant to observe all resources related to a given worker.
//
// We'll only support a few resource types in Workers (console-message, source,
// thread state, …) as error and platform messages are not supported since we need access
// to Ci, which isn't available in worker context.
// Errors are emitted from the content process main thread so the user would still get them.
const WorkerTargetResources = augmentResourceDictionary({
  [TYPES.CONSOLE_MESSAGE]: {
    path: "devtools/server/actors/resources/console-messages",
  },
  [TYPES.JSTRACER_TRACE]: {
    path: "devtools/server/actors/resources/jstracer-trace",
  },
  [TYPES.JSTRACER_STATE]: {
    path: "devtools/server/actors/resources/jstracer-state",
  },
  [TYPES.SOURCE]: {
    path: "devtools/server/actors/resources/sources",
  },
  [TYPES.THREAD_STATE]: {
    path: "devtools/server/actors/resources/thread-states",
  },
});

// Web Extension Content Script resources are spawn for each content script,
// from the web page's main thread it relates to.
//
// Similarly to workers, it doesn't relate to any DOM Document,
// but only a JavaScript context. So it only expose a subset of resources.
const WebExtensionContentScriptTargetResources = augmentResourceDictionary({
  [TYPES.CONSOLE_MESSAGE]: {
    path: "devtools/server/actors/resources/console-messages",
  },
  [TYPES.JSTRACER_TRACE]: {
    path: "devtools/server/actors/resources/jstracer-trace",
  },
  [TYPES.JSTRACER_STATE]: {
    path: "devtools/server/actors/resources/jstracer-state",
  },
  [TYPES.SOURCE]: {
    path: "devtools/server/actors/resources/sources",
  },
  [TYPES.THREAD_STATE]: {
    path: "devtools/server/actors/resources/thread-states",
  },
});

// Parent process resources are spawned via the Watcher Actor.
// Their watcher class receives the watcher actor as first argument.
// They are instantiated once per watcher from the parent process.
// They are meant to observe all resources related to a given context designated by the Watcher (and its sessionContext)
// they should be observed from the parent process.
const ParentProcessResources = augmentResourceDictionary({
  [TYPES.NETWORK_EVENT]: {
    path: "devtools/server/actors/resources/network-events",
  },
  [TYPES.COOKIE]: {
    path: "devtools/server/actors/resources/storage-cookie",
  },
  [TYPES.EXTENSION_STORAGE]: {
    path: "devtools/server/actors/resources/storage-extension",
  },
  [TYPES.INDEXED_DB]: {
    path: "devtools/server/actors/resources/storage-indexed-db",
  },
  [TYPES.DOCUMENT_EVENT]: {
    path: "devtools/server/actors/resources/parent-process-document-event",
  },
  [TYPES.LAST_PRIVATE_CONTEXT_EXIT]: {
    path: "devtools/server/actors/resources/last-private-context-exit",
  },
  [TYPES.SESSION_HISTORY]: {
    path: "devtools/server/actors/resources/session-history",
  },
});

// Root resources are spawned via the Root Actor.
// Their watcher class receives the root actor as first argument.
// They are instantiated only once from the parent process.
// They are meant to observe anything easily observable from the parent process
// that isn't related to any particular context/target.
// This is especially useful when you need to observe something without having to instantiate a Watcher actor.
const RootResources = augmentResourceDictionary({
  [TYPES.EXTENSIONS_BGSCRIPT_STATUS]: {
    path: "devtools/server/actors/resources/extensions-backgroundscript-status",
  },
});
exports.RootResources = RootResources;

function augmentResourceDictionary(dict) {
  for (const resource of Object.values(dict)) {
    resource.watchers = new WeakMap();

    loader.lazyRequireGetter(resource, "WatcherClass", resource.path);
  }
  return dict;
}

/**
 * For a given actor, return the related dictionary defined just before,
 * that contains info about how to listen for a given resource type, from a given actor.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        Either a RootActor or WatcherActor or a TargetActor which can be listening to a resource.
 */
function getResourceTypeDictionary(rootOrWatcherOrTargetActor) {
  const { typeName } = rootOrWatcherOrTargetActor;
  if (typeName == "root") {
    return RootResources;
  }
  if (typeName == "watcher") {
    return ParentProcessResources;
  }
  const { targetType } = rootOrWatcherOrTargetActor;
  return getResourceTypeDictionaryForTargetType(targetType);
}

/**
 * For a targetType, return the related dictionary.
 *
 * @param String targetType
 *        A targetType string (See Targets.TYPES)
 */
function getResourceTypeDictionaryForTargetType(targetType) {
  switch (targetType) {
    case Targets.TYPES.FRAME:
      return FrameTargetResources;
    case Targets.TYPES.PROCESS:
      return ProcessTargetResources;
    case Targets.TYPES.WORKER:
      return WorkerTargetResources;
    case Targets.TYPES.SERVICE_WORKER:
      return WorkerTargetResources;
    case Targets.TYPES.SHARED_WORKER:
      return WorkerTargetResources;
    case Targets.TYPES.CONTENT_SCRIPT:
      return WebExtensionContentScriptTargetResources;
    default:
      throw new Error(`Unsupported target actor typeName '${targetType}'`);
  }
}

/**
 * For a given actor, return the object stored in one of the previous dictionary
 * that contains info about how to listen for a given resource type, from a given actor.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        Either a RootActor or WatcherActor or a TargetActor which can be listening to a resource.
 * @param String resourceType
 *        The resource type to be observed.
 */
function getResourceTypeEntry(rootOrWatcherOrTargetActor, resourceType) {
  const dict = getResourceTypeDictionary(rootOrWatcherOrTargetActor);
  if (!(resourceType in dict)) {
    throw new Error(
      `Unsupported resource type '${resourceType}' for ${rootOrWatcherOrTargetActor.typeName}`
    );
  }
  return dict[resourceType];
}

/**
 * Start watching for a new list of resource types.
 * This will also emit all already existing resources before resolving.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        Either a RootActor or WatcherActor or a TargetActor which can be listening to a resource:
 *        * RootActor will be used for resources observed from the parent process and aren't related to any particular
 *        context/descriptor. They can be observed right away when connecting to the RDP server
 *        without instantiating any actor other than the root actor.
 *        * WatcherActor will be used for resources listened from the parent process.
 *        * TargetActor will be used for resources listened from the content process.
 *        This actor:
 *          - defines what context to observe (browsing context, process, worker, ...)
 *            Via browsingContextID, windows, docShells attributes for the target actor.
 *            Via the `sessionContext` object for the watcher actor.
 *            (only for Watcher and Target actors. Root actor is context-less.)
 *          - exposes `notifyResources` method to be notified about all the resources updates
 *            This method will receive two arguments:
 *            - {String} updateType, which can be "available", "updated", or "destroyed"
 *            - {Array<Object>} resources, which will be the list of resource's forms
 *              or special update object for "updated" scenario.
 * @param Array<String> resourceTypes
 *        List of all type of resource to listen to.
 */
async function watchResources(rootOrWatcherOrTargetActor, resourceTypes) {
  // If we are given a target actor, filter out the resource types supported by the target.
  // When using sharedData to pass types between processes, we are passing them for all target types.
  const { targetType } = rootOrWatcherOrTargetActor;
  // Only target actors usecase will have a target type.
  // For Root and Watcher we process the `resourceTypes` list unfiltered.
  if (targetType) {
    resourceTypes = getResourceTypesForTargetType(resourceTypes, targetType);
  }
  const promises = [];
  for (const resourceType of resourceTypes) {
    const { watchers, WatcherClass } = getResourceTypeEntry(
      rootOrWatcherOrTargetActor,
      resourceType
    );

    // Ignore resources we're already listening to
    if (watchers.has(rootOrWatcherOrTargetActor)) {
      continue;
    }

    // Don't watch for console messages from the worker target if worker messages are still
    // being cloned to the main process, otherwise we'll get duplicated messages in the
    // console output (See Bug 1778852).
    if (
      resourceType == TYPES.CONSOLE_MESSAGE &&
      rootOrWatcherOrTargetActor.workerConsoleApiMessagesDispatchedToMainThread
    ) {
      continue;
    }

    const watcher = new WatcherClass();
    promises.push(
      watcher.watch(rootOrWatcherOrTargetActor, {
        onAvailable: rootOrWatcherOrTargetActor.notifyResources.bind(
          rootOrWatcherOrTargetActor,
          "available",
          resourceType
        ),
        onUpdated: rootOrWatcherOrTargetActor.notifyResources.bind(
          rootOrWatcherOrTargetActor,
          "updated",
          resourceType
        ),
        onDestroyed: rootOrWatcherOrTargetActor.notifyResources.bind(
          rootOrWatcherOrTargetActor,
          "destroyed",
          resourceType
        ),
      })
    );
    watchers.set(rootOrWatcherOrTargetActor, watcher);
  }
  await Promise.all(promises);

  // Force sending resources to the client before we resolve.
  // So that resources are received by the client
  // before WatcherActor.watchResources resolves.
  // This is important when ResourceCommand.watchResources's `ignoreExistingResources` flag is set to false (default behavior).
  // The client code expects all resources to be emitted when this server method resolves.
  if (rootOrWatcherOrTargetActor.emitResources) {
    rootOrWatcherOrTargetActor.emitResources();
  }
}
exports.watchResources = watchResources;

function getParentProcessResourceTypes(resourceTypes) {
  return resourceTypes.filter(resourceType => {
    return resourceType in ParentProcessResources;
  });
}
exports.getParentProcessResourceTypes = getParentProcessResourceTypes;

function getResourceTypesForTargetType(resourceTypes, targetType) {
  const resourceDictionnary =
    getResourceTypeDictionaryForTargetType(targetType);
  return resourceTypes.filter(resourceType => {
    return resourceType in resourceDictionnary;
  });
}
exports.getResourceTypesForTargetType = getResourceTypesForTargetType;

function hasResourceTypesForTargets(resourceTypes) {
  return resourceTypes.some(resourceType => {
    return resourceType in FrameTargetResources;
  });
}
exports.hasResourceTypesForTargets = hasResourceTypesForTargets;

/**
 * Stop watching for a list of resource types.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        The related actor, already passed to watchResources.
 * @param Array<String> resourceTypes
 *        List of all type of resource to stop listening to.
 */
function unwatchResources(rootOrWatcherOrTargetActor, resourceTypes) {
  // If we are given a target actor, filter out the resource types supported by the target.
  // When using sharedData to pass types between processes, we are passing them for all target types.
  const { targetType } = rootOrWatcherOrTargetActor;
  // Only target actors usecase will have a target type.
  // For Root and Watcher we process the `resourceTypes` list unfiltered.
  if (targetType) {
    resourceTypes = getResourceTypesForTargetType(resourceTypes, targetType);
  }
  for (const resourceType of resourceTypes) {
    // Pull all info about this resource type from `Resources` global object
    const { watchers } = getResourceTypeEntry(
      rootOrWatcherOrTargetActor,
      resourceType
    );

    const watcher = watchers.get(rootOrWatcherOrTargetActor);
    if (watcher) {
      watcher.destroy();
      watchers.delete(rootOrWatcherOrTargetActor);
    }
  }
}
exports.unwatchResources = unwatchResources;

/**
 * Clear resources for a list of resource types.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        The related actor, already passed to watchResources.
 * @param Array<String> resourceTypes
 *        List of all type of resource to clear.
 */
function clearResources(rootOrWatcherOrTargetActor, resourceTypes) {
  // If we are given a target actor, filter out the resource types supported by the target.
  // When using sharedData to pass types between processes, we are passing them for all target types.
  const { targetType } = rootOrWatcherOrTargetActor;
  // Only target actors usecase will have a target type.
  // For Root and Watcher we process the `resourceTypes` list unfiltered.
  if (targetType) {
    resourceTypes = getResourceTypesForTargetType(resourceTypes, targetType);
  }
  for (const resourceType of resourceTypes) {
    const { watchers } = getResourceTypeEntry(
      rootOrWatcherOrTargetActor,
      resourceType
    );

    const watcher = watchers.get(rootOrWatcherOrTargetActor);
    if (watcher && typeof watcher.clear == "function") {
      watcher.clear();
    }
  }
}

exports.clearResources = clearResources;

/**
 * Stop watching for all watched resources on a given actor.
 *
 * @param Actor rootOrWatcherOrTargetActor
 *        The related actor, already passed to watchResources.
 */
function unwatchAllResources(rootOrWatcherOrTargetActor) {
  for (const { watchers } of Object.values(
    getResourceTypeDictionary(rootOrWatcherOrTargetActor)
  )) {
    const watcher = watchers.get(rootOrWatcherOrTargetActor);
    if (watcher) {
      watcher.destroy();
      watchers.delete(rootOrWatcherOrTargetActor);
    }
  }
}
exports.unwatchAllResources = unwatchAllResources;

/**
 * If we are watching for the given resource type,
 * return the current ResourceWatcher instance used by this target actor
 * in order to observe this resource type.
 *
 * @param Actor watcherOrTargetActor
 *        Either a WatcherActor or a TargetActor which can be listening to a resource.
 *        WatcherActor will be used for resources listened from the parent process,
 *        and TargetActor will be used for resources listened from the content process.
 * @param String resourceType
 *        The resource type to query
 * @return ResourceWatcher
 *         The resource watcher instance, defined in devtools/server/actors/resources/
 */
function getResourceWatcher(watcherOrTargetActor, resourceType) {
  const { watchers } = getResourceTypeEntry(watcherOrTargetActor, resourceType);

  return watchers.get(watcherOrTargetActor);
}
exports.getResourceWatcher = getResourceWatcher;
