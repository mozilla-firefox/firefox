
# Server side overview

## Connecting to backend

The DevTools backend exposes a RDP server that the client can query. See Client API <client-api.md>

## Picking a particular context to debug

The client will typically query the Root Actor for one particular Descriptor Actor which will designate what piece of the browser to debug. See <actor-hierarchy.md>.

The typical scenario is for the client to query a [TabDescriptorActor](https://searchfox.org/firefox-main/source/devtools/server/actors/descriptors/tab.js) in order to debug one particular tab.

## The Watcher Actor

Then, once you set a particular context to debug, you are retrieving one [WatcherActor](https://searchfox.org/firefox-main/source/devtools/server/actors/watcher.js) instance.
This actor is a pillar of DevTools backend as it will coordinate the observation of everything.

### Debuggable contexts / DevTools Targets

First you will use `WatcherActor.watchTarget(String targetType)` method to define the child debugging contexts you are interested in.
This method will only resolve after having been notified of all the existing debuggable contexts.
Then a `target-available-form` RDP event is emitted on the WatcherActor for each new debuggable context being created later.
As well as a `target-destroyed-form` when any debuggable context gets destroyed.

The debugging contexts are called Targets in DevTools jargon and can be:

* "frame"

  Any document instance running anywhere in the browser.
  Each very specific document instance will be notified to the client via a dedicated [WindowGlobalTargetActor](https://searchfox.org/firefox-main/source/devtools/server/actors/targets/window-global.js) instance.
  This means that if you reload the page, you will have as many target actors as you reload.
  Also, if there is \<iframe\>s, you will get one frame targets for each iframe.

* "worker", "service_worker", "shared_worker"

  Any worker instance running anywhere in the browser.
  Each worker instance will ne notified to the client via a dedicated [WorkerTargetActor](https://searchfox.org/firefox-main/source/devtools/server/actors/targets/worker.js) instance.

* "process"

  Any process running in the browser.
  This is only used for debugging Firefox and not when debugging web pages.
  Each process will be notified to the client via a dedicated [ProcessTargetActor](https://searchfox.org/firefox-main/source/devtools/server/actors/targets/content-process.js).
  You will be notified of as many process targets as there are content processes running.

The target type strings are defined in [devtools/server/actors/targets/index.js](https://searchfox.org/firefox-main/source/devtools/server/actors/targets/index.js).

Target Actors are exposing many other important actors, like Inspector, WebConsole, Thread Actors. See <actor-hierarchy.md>.

### Resources

Once you started watching for some target types, you can use `WatcherActor.watchResources(Array<String> resourceTypes)` method to be notified about resources for each active Target/Debuggable context.
This method will only resolve after having been notified of all the existing resources.
Resources aren't returned by the watchResources method. Instead they are notified to the client via `resources-available-array` RDP events emitted, either on the Watcher Actor or one of the many Target Actors.
Some resources may also support:

* a `resources-updated-array` RDP event, when any resource gets updated (like stylesheets or network events),
* a `resources-destroyed-array` RDP event, when any resource gets destroyed (like stylesheets).

The resources can be:

* "console-message"

  Any `console.log()`, `console.error()`, ... method call, in any of the debuggable context, will be notified to the client via such "console-message" resource.

* "source"

  Any JavaScript or Wasm source in any of the debuggable context will be notified to the client via such "source" resource.

* "stylesheet"

  Any StyleSheet, defined by any "frame" debuggable context will be notified to the client via such "stylesheet" resource.

* Many other things.

  You can find the list of all resources in [devtools/server/resources/](https://searchfox.org/firefox-main/source/devtools/server/actors/resources) folder.

The resource type strings are defined in [devtools/server/actors/resources/index.js](https://searchfox.org/firefox-main/source/devtools/server/actors/resources/index.js).

A resource will be a JSON object with attributes specific to each resource type.
Each resource has a dedicated `ResourceWatcher` class in [devtools/server/actors/resources/](https://searchfox.org/firefox-main/source/devtools/server/actors/resources/) folder.

Depending on the resource type, they may be observed either in:

* parent process (regardless of where targets run)
* main thread of the target's process
* worker thread (if we have a worker target)

This behavior is also defined in [devtools/server/actors/resources/index.js](https://searchfox.org/firefox-main/source/devtools/server/actors/resources/index.js) via:

* `ParentProcessResources`

  The ResourceWatcher will be instantiated in the parent process.

* `FrameTargetResources`

  The ResourceWatcher will be instantiated for "frame" targets, in the main thread of where the frame runs.

* `ProcessTargetResources`

  The ResourceWatcher will be instantiated for "process" targets, in the main thread of it.

* `WorkerTargetResources`

  The ResourceWatcher will be instantiated for "worker" targets, in the worker thread.

Each resource should then implement the following interface:

```
class MyResourceWatcher {
  /**
   * Start watching for my resource for a given context.
   *
   * This class will be instantiated only once when registered in `ParentProcessResources` and running in the parent process.
   * Also, the first argument `watcherOrTargetActor` will be a reference to the WatcherActor instance.
   * In all the other cases, this class will be instantiated once per active Target instance.
   * In any other case, it will be a reference to a TargetActor.
   *
   * Then, the onAvailable, onUpdated and onDestroyed should be called according to their names
   * for each resource.
   *
   * /!\ This method should only resolve **after** having notified via onAvailable about all the existing resource instances.
   */
  async watch(watcherOrTargetActor, { onAvailable, onUpdated, onDestroyed }) {

    // Each method except a list of updates

    // onAvailable expects a list of JSON object being Resources Object being passed as-is to the client
    // There must be a `resourceType` attribute.
    const {
      TYPES: { MY_RESOURCE },
    } = require("devtools/server/actors/resources/index");
    onAvailable([
      {
        resourceType: MY_RESOURCE,

        resourceId: 123, // Mandatory when using onUpdated and/or onDestroyed, otherwise optional

        myResourceSpecificData: 42,

        some: { nested : { object : "foo" } },
      },
    ]);

    // onUpdated expects a list of updates against many resource objects previously notified via onAvailable
    onUpdated([
      {
        resourceType: MY_RESOURCE,
        resourceId, // Same id passed to onAvailable

        // This field allows to update top attributes of your resource object
        resourceUpdates: {
          myResourceSpecificData: 43,
        },

        // This advanced field allows to update any nested object attribute
        nestedResourceUpdates: [
          {
            path: ["some", "nested", "object"],
            value: "bar",
          },
        ],
      },
    ]);

    // onDestroyed expects a list of resource to be notified as destroyed to the client
    onDestroyed([
      {
        resourceType: MY_RESOURCE,
        resourceId, // Same id passed to onAvailable
      }
    ]);
  }

  /**
   * Stop observing these resources. Unregister any listener to prevent any leak.
   */
  destroy() {

  }
```

## How the Watcher Actor handles processes/threads and instantiates the targets actors?

The Watcher Actor is running in the parent process and needs to reach all content processes and threads
in order to interact with all the debuggable contexts.
In order to reach all the content processes, the Watcher Actor uses the JS Process Actor API. See <dom/docs/ipc/jsactors.rst>.
It registers one JS Process Actor called "DevToolsProcess".
For each `watchTargets` and `watchResources` method call, a new query will be sent through the JS Process Actor to all the processes.
The JS Process Actor API consists of two distinct modules:

* One running in the parent process. [DevToolsProcessParent.sys.mjs](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/DevToolsProcessParent.sys.mjs)

  This module is simple. It will mostly forward the queries to the content process when the watcher actor calls some of its methods.
  It will also receive messages from the content process, and uses the [ParentProcessWatcherRegistry](https://searchfox.org/firefox-main/source/devtools/server/actors/watcher/ParentProcessWatcherRegistry.sys.mjs) in order to retrieve the related Watcher Actor instance and notify it about the incoming message.

* One running in the content process. [DevToolsProcessChild.sys.mjs](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/DevToolsProcessChild.sys.mjs)

  This module contains some more logic.
  When receiving requests to watch for new targets and resources types, this will delegate these requests to many Target Watcher classes.
  These classes are specific to each target type:
  * [WindowGlobal](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/target-watchers/window-global.sys.mjs),
  * [Process](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/target-watchers/process.sys.mjs),
  * [Worker](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/target-watchers/worker.sys.mjs),
  * [ServiceWorker](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/target-watchers/service_worker.sys.mjs),

    These classes will:
  * Instantiate a new Target Actor each time a new matching debuggable contexts gets created.
  * Destroy the related Target Actor when the contexts gets destroyed.
  * Dispatch SessionData updates to the target actor. (this includes the watched resources, which are a SessionData attribute)
  * For workers, this class will also reach the distinct worker thread in order to do all these 3 first bullet points, but from the worker thread.

    Similarly to the parent process codebase, the [ContentProcessWatcherRegistry](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/ContentProcessWatcherRegistry.sys.mjs) maintains an list of objects which represents each active watcher actor. This helps store state in the content process which will be specific to each watcher.

## Session Data

Each Watcher Actor maintains a dedicated Session Data object.
This is a JSON-serializable object that is meant to be shared across all processes and threads.
This is easily accessible from any server side code. Modifications are only been done from the parent process,
but its state is synced across processes and threads.

The list of supported attributes is maintained in [SessionDataHelpers.sys.mjs](https://searchfox.org/firefox-main/source/devtools/server/actors/watcher/SessionDataHelpers.sys.mjs)'s `SUPPORTED_DATA` variable.
Values stored in the session data object are arrays of objects.
But these arrays are meant to behave like Sets.
Primitive values (strings, numbers,...) can only exists once per array.
For objects, `DATA_KEY_FUNCTION` variable in SessionDataHelper module will provide a unique key identifying each element of the arrays.

The Session Data object is maintained in the parent process by [ParentProcessWatcherRegistry](https://searchfox.org/firefox-main/source/devtools/server/actors/watcher/ParentProcessWatcherRegistry.sys.mjs). This module will store each Watcher Actor's Session Data object.
Then, the Watcher Actor will use the JS Process Actor in order to communicate updates made to the Session Data Object to all the content processes.
The Worker Target Watchers are also going to relay the updates to all worker threads.

The Session Data objects of all the watcher actors are also going to be stored in [GlobalProcessScript.sharedData](https://searchfox.org/firefox-main/source/dom/chrome-webidl/MessageManager.webidl#452). This is meant to provide the Session Data to the [DevToolsProcessChild.sys.mjs](https://searchfox.org/firefox-main/source/devtools/server/connectors/js-process-actor/DevToolsProcessChild.sys.mjs) when a new content process starts. We need to have access to the Session Data the earliest during the startup sequence in order to setup breakpoints. We can't wait for a JS Process Actor query and need immediate access to it. On Process startup, we will read the Session Data from `sharedData` and then maintain the copy via JS Process Actor queries.
