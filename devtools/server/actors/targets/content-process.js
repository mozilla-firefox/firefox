/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/*
 * Target actor for all resources in a content process of Firefox (chrome sandboxes, frame
 * scripts, documents, etc.)
 *
 * See devtools/docs/contributor/backend/actor-hierarchy.md for more details about all the targets.
 */

const { ThreadActor } = require("resource://devtools/server/actors/thread.js");
const {
  WebConsoleActor,
} = require("resource://devtools/server/actors/webconsole.js");
const makeDebugger = require("resource://devtools/server/actors/utils/make-debugger.js");
const { Pool } = require("resource://devtools/shared/protocol.js");
const { assert } = require("resource://devtools/shared/DevToolsUtils.js");
const {
  SourcesManager,
} = require("resource://devtools/server/actors/utils/sources-manager.js");
const {
  contentProcessTargetSpec,
} = require("resource://devtools/shared/specs/targets/content-process.js");
const Targets = require("resource://devtools/server/actors/targets/index.js");
const Resources = require("resource://devtools/server/actors/resources/index.js");
const {
  BaseTargetActor,
} = require("resource://devtools/server/actors/targets/base-target-actor.js");
const { TargetActorRegistry } = ChromeUtils.importESModule(
  "resource://devtools/server/actors/targets/target-actor-registry.sys.mjs",
  { global: "shared" }
);

loader.lazyRequireGetter(
  this,
  "WorkerDescriptorActorList",
  "resource://devtools/server/actors/worker/worker-descriptor-actor-list.js",
  true
);
loader.lazyRequireGetter(
  this,
  "MemoryActor",
  "resource://devtools/server/actors/memory.js",
  true
);
loader.lazyRequireGetter(
  this,
  "TracerActor",
  "resource://devtools/server/actors/tracer.js",
  true
);
loader.lazyRequireGetter(
  this,
  "ObjectsManagerActor",
  "resource://devtools/server/actors/objects-manager.js",
  true
);

class ContentProcessTargetActor extends BaseTargetActor {
  constructor(conn, { isXpcShellTarget = false, sessionContext } = {}) {
    super(conn, Targets.TYPES.PROCESS, contentProcessTargetSpec);

    this.threadActor = null;
    this.isXpcShellTarget = isXpcShellTarget;
    this.sessionContext = sessionContext;

    // Use a see-everything debugger
    this.makeDebugger = makeDebugger.bind(null, {
      findDebuggees: dbg =>
        dbg.findAllGlobals().map(g => g.unsafeDereference()),
      shouldAddNewGlobalAsDebuggee: () => true,
    });

    const sandboxPrototype = {
      get tabs() {
        return Array.from(
          Services.ww.getWindowEnumerator(),
          win => win.docShell.messageManager
        );
      },
    };

    // Scope into which the webconsole executes:
    // A sandbox with chrome privileges with a `tabs` getter.
    const systemPrincipal = Cc["@mozilla.org/systemprincipal;1"].createInstance(
      Ci.nsIPrincipal
    );
    const sandbox = Cu.Sandbox(systemPrincipal, {
      sandboxPrototype,
      wantGlobalProperties: ["ChromeUtils"],
    });
    this._consoleScope = sandbox;

    this._workerList = null;
    this._workerDescriptorActorPool = null;
    this._onWorkerListChanged = this._onWorkerListChanged.bind(this);

    // Try to destroy the Content Process Target when the content process shuts down.
    // The parent process can't communicate during shutdown as the communication channel
    // is already down (message manager or JS Window Actor API).
    // So that we have to observe to some event fired from this process.
    // While such cleanup doesn't sound ultimately necessary (the process will be completely destroyed)
    // mochitests are asserting that there is no leaks during process shutdown.
    // Do not override destroy as Protocol.js may override it when calling destroy,
    // and we won't be able to call removeObserver correctly.
    this.destroyObserver = this.destroy.bind(this);
    Services.obs.addObserver(this.destroyObserver, "xpcom-shutdown");
    if (this.isXpcShellTarget) {
      TargetActorRegistry.registerXpcShellTargetActor(this);
    }
  }

  get isRootActor() {
    return true;
  }

  get url() {
    return undefined;
  }

  get window() {
    return this._consoleScope;
  }

  get targetGlobal() {
    return this._consoleScope;
  }

  get sourcesManager() {
    if (!this._sourcesManager) {
      assert(
        this.threadActor,
        "threadActor should exist when creating SourcesManager."
      );
      this._sourcesManager = new SourcesManager(this.threadActor);
    }
    return this._sourcesManager;
  }

  /*
   * Return a Debugger instance or create one if there is none yet
   */
  get dbg() {
    if (!this._dbg) {
      this._dbg = this.makeDebugger();
    }
    return this._dbg;
  }

  form() {
    if (!this._consoleActor) {
      this._consoleActor = new WebConsoleActor(this.conn, this);
      this.manage(this._consoleActor);
    }

    if (!this.threadActor) {
      this.threadActor = new ThreadActor(this);
      this.manage(this.threadActor);
    }
    if (!this.memoryActor) {
      this.memoryActor = new MemoryActor(this.conn, this);
      this.manage(this.memoryActor);
    }
    if (!this.tracerActor) {
      this.tracerActor = new TracerActor(this.conn, this);
      this.manage(this.tracerActor);
    }
    if (!this.objectsManagerActor) {
      this.objectsManagerActor = new ObjectsManagerActor(this.conn, this);
      this.manage(this.objectsManagerActor);
    }

    return {
      actor: this.actorID,
      targetType: this.targetType,

      isXpcShellTarget: this.isXpcShellTarget,
      processID: Services.appinfo.processID,
      remoteType: Services.appinfo.remoteType,

      consoleActor: this._consoleActor.actorID,
      memoryActor: this.memoryActor.actorID,
      threadActor: this.threadActor.actorID,
      tracerActor: this.tracerActor.actorID,
      objectsManagerActor: this.objectsManagerActor.actorID,

      traits: {
        networkMonitor: false,
        // See trait description in browsing-context.js
        supportsTopLevelTargetFlag: false,
      },
    };
  }

  ensureWorkerList() {
    if (!this._workerList) {
      this._workerList = new WorkerDescriptorActorList(this.conn, {});
    }
    return this._workerList;
  }

  listWorkers() {
    return this.ensureWorkerList()
      .getList()
      .then(actors => {
        const pool = new Pool(this.conn, "workers");
        for (const actor of actors) {
          pool.manage(actor);
        }

        // Do not destroy the pool before transfering ownership to the newly created
        // pool, so that we do not accidentally destroy actors that are still in use.
        if (this._workerDescriptorActorPool) {
          this._workerDescriptorActorPool.destroy();
        }

        this._workerDescriptorActorPool = pool;
        this._workerList.onListChanged = this._onWorkerListChanged;

        return { workers: actors };
      });
  }

  _onWorkerListChanged() {
    this.conn.send({ from: this.actorID, type: "workerListChanged" });
    this._workerList.onListChanged = null;
  }

  pauseMatchingServiceWorkers(request) {
    this.ensureWorkerList().workerPauser.setPauseServiceWorkers(request.origin);
  }

  destroy({ isModeSwitching } = {}) {
    // Avoid reentrancy. We will destroy the Transport when emitting "destroyed",
    // which will force destroying all actors.
    if (this.destroying) {
      return;
    }
    this.destroying = true;

    // Unregistering watchers first is important
    // otherwise you might have leaks reported when running browser_browser_toolbox_netmonitor.js in debug builds
    Resources.unwatchAllResources(this);

    this.emit("destroyed", { isModeSwitching });

    super.destroy();

    if (this.threadActor) {
      this.threadActor = null;
    }

    // Tell the live lists we aren't watching any more.
    if (this._workerList) {
      this._workerList.destroy();
      this._workerList = null;
    }

    if (this._sourcesManager) {
      this._sourcesManager.destroy();
      this._sourcesManager = null;
    }

    if (this._dbg) {
      this._dbg.disable();
      this._dbg = null;
    }

    Services.obs.removeObserver(this.destroyObserver, "xpcom-shutdown");

    if (this.isXpcShellTarget) {
      TargetActorRegistry.unregisterXpcShellTargetActor(this);
    }
  }
}

exports.ContentProcessTargetActor = ContentProcessTargetActor;
