/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");

const BROWSERTOOLBOX_SCOPE_PREF = "devtools.browsertoolbox.scope";
// Possible values of the previous pref:
const BROWSERTOOLBOX_SCOPE_EVERYTHING = "everything";
const BROWSERTOOLBOX_SCOPE_PARENTPROCESS = "parent-process";

const SHOW_CONTENT_SCRIPTS_PREF = "devtools.debugger.show-content-scripts";

// eslint-disable-next-line mozilla/reject-some-requires
const createStore = require("resource://devtools/client/shared/redux/create-store.js");
const reducer = require("resource://devtools/shared/commands/target/reducers/targets.js");

loader.lazyRequireGetter(
  this,
  ["refreshTargets", "registerTarget", "unregisterTarget"],
  "resource://devtools/shared/commands/target/actions/targets.js",
  true
);

class TargetCommand extends EventEmitter {
  #selectedTargetFront;
  /**
   * This class helps managing, iterating over and listening for Targets.
   *
   * It exposes:
   *  - the top level target, typically the main process target for the browser toolbox
   *    or the browsing context target for a regular web toolbox
   *  - target of remoted iframe, in case Fission is enabled and some <iframe>
   *    are running in a distinct process
   *  - target switching. If the top level target changes for a new one,
   *    all the targets are going to be declared as destroyed and the new ones
   *    will be notified to the user of this API.
   *
   * @fires target-tread-wrong-order-on-resume : An event that is emitted when resuming
   *        the thread throws with the "wrongOrder" error.
   *
   * @param {DescriptorFront} descriptorFront
   *        The context to inspector identified by this descriptor.
   * @param {WatcherFront} watcherFront
   *        If available, a reference to the related Watcher Front.
   * @param {object} commands
   *        The commands object with all interfaces defined from devtools/shared/commands/
   */
  constructor({ descriptorFront, watcherFront, commands }) {
    super();

    this.commands = commands;
    this.descriptorFront = descriptorFront;
    this.watcherFront = watcherFront;
    this.rootFront = descriptorFront.client.mainRoot;

    this.store = createStore(reducer);
    // Name of the store used when calling createProvider.
    this.storeId = "target-store";

    this._updateBrowserToolboxScope =
      this._updateBrowserToolboxScope.bind(this);
    this._updateContentScriptListening =
      this._updateContentScriptListening.bind(this);

    Services.prefs.addObserver(
      BROWSERTOOLBOX_SCOPE_PREF,
      this._updateBrowserToolboxScope
    );
    Services.prefs.addObserver(
      SHOW_CONTENT_SCRIPTS_PREF,
      this._updateContentScriptListening
    );
    // Until Watcher actor notify about new top level target when navigating to another process
    // we have to manually switch to a new target from the client side
    this.onLocalTabRemotenessChange =
      this.onLocalTabRemotenessChange.bind(this);
    if (this.descriptorFront.isTabDescriptor) {
      this.descriptorFront.on(
        "remoteness-change",
        this.onLocalTabRemotenessChange
      );
    }

    if (this.isServerTargetSwitchingEnabled()) {
      // XXX: Will only be used for local tab server side target switching if
      // the first target is generated from the server.
      this._onFirstTarget = new Promise(r => (this._resolveOnFirstTarget = r));
    }

    // Reports if we have at least one listener for the given target type
    this._listenersStarted = new Set();

    // List of all the target fronts
    this._targets = new Set();
    // {Map<Function, Set<targetFront>>} A Map keyed by `onAvailable` function passed to
    // `watchTargets`, whose initial value is a Set of the existing target fronts at the
    // time watchTargets is called.
    this._pendingWatchTargetInitialization = new Map();

    // Listeners for target creation, destruction and selection
    this._createListeners = new EventEmitter();
    this._destroyListeners = new EventEmitter();
    this._selectListeners = new EventEmitter();

    this._onTargetAvailable = this._onTargetAvailable.bind(this);
    this._onTargetDestroyed = this._onTargetDestroyed.bind(this);
    this._onTargetSelected = this._onTargetSelected.bind(this);
    // Bug 1675763: Watcher actor is not available in all situations yet.
    if (this.watcherFront) {
      this.watcherFront.on("target-available", this._onTargetAvailable);
      this.watcherFront.on("target-destroyed", this._onTargetDestroyed);
    }

    this.legacyImplementation = {};

    // Public flag to allow listening for workers even if the fission pref is off
    // This allows listening for workers in the content toolbox outside of fission contexts
    // For now, this is only toggled by tests.
    this.listenForWorkers =
      this.rootFront.traits.workerConsoleApiMessagesDispatchedToMainThread ===
      false;
    this.listenForServiceWorkers = false;
    this.listenForContentScripts = false;

    // Tells us if we received the first top level target.
    // If target switching is done on:
    // * client side, this is done from startListening => _createFirstTarget
    //                and pull from the Descriptor front.
    // * server side, this is also done from startListening,
    //                but we wait for the watcher actor to notify us about it
    //                via target-available-form avent.
    this._gotFirstTopLevelTarget = false;
    this._onResourceAvailable = this._onResourceAvailable.bind(this);
  }

  get selectedTargetFront() {
    return this.#selectedTargetFront || this.targetFront;
  }

  /**
   * Called fired when BROWSERTOOLBOX_SCOPE_PREF pref changes.
   * This will enable/disable the full multiprocess debugging.
   * When enabled we will watch for content process targets and debug all the processes.
   * When disabled we will only watch for FRAME and WORKER and restrict ourself to parent process resources.
   */
  _updateBrowserToolboxScope() {
    const browserToolboxScope = Services.prefs.getCharPref(
      BROWSERTOOLBOX_SCOPE_PREF
    );
    if (browserToolboxScope == BROWSERTOOLBOX_SCOPE_EVERYTHING) {
      // Force listening to new additional target types
      this.startListening();
    } else if (browserToolboxScope == BROWSERTOOLBOX_SCOPE_PARENTPROCESS) {
      const disabledTargetTypes = [
        TargetCommand.TYPES.FRAME,
        TargetCommand.TYPES.PROCESS,
      ];
      // Force unwatching for additional targets types
      // (we keep listening to workers)
      // The related targets will be destroyed by the server
      // and reported as destroyed to the frontend.
      for (const type of disabledTargetTypes) {
        this.stopListeningForType(type, {
          isTargetSwitching: false,
          isModeSwitching: true,
        });
      }
    }
  }

  /**
   * Toggle listening to CONTENT_SCRIPT target type based on SHOW_CONTENT_SCRIPTS_PREF pref changes.
   */
  _updateContentScriptListening() {
    const showContentScripts = Services.prefs.getBoolPref(
      SHOW_CONTENT_SCRIPTS_PREF,
      false
    );
    if (showContentScripts) {
      // `_computeTargetTypes`, which is used by `startListening` to compute the types to listen to,
      // will read the pref and force listening to CONTENT_SCRIPT type
      this.startListening();
    } else {
      this.stopListeningForType(TargetCommand.TYPES.CONTENT_SCRIPT, {
        isTargetSwitching: false,
        isModeSwitching: false,
      });
    }
  }

  // Called whenever a new Target front is available.
  // Either because a target was already available as we started calling startListening
  // or if it has just been created
  // eslint-disable-next-line complexity
  async _onTargetAvailable(targetFront) {
    // We put the `commands` on the targetFront so it can be retrieved from any front easily.
    // Without this, protocol.js fronts won't have any easy access to it.
    // Ideally, Fronts would all be migrated to commands and we would no longer need this hack.
    targetFront.commands = this.commands;

    // If the new target is a top level target, we are target switching.
    // Target-switching is only triggered for "local-tab" browsing-context
    // targets which should always have the topLevelTarget flag initialized
    // on the server.
    const isTargetSwitching = targetFront.isTopLevel;
    const isFirstTarget =
      targetFront.isTopLevel && !this._gotFirstTopLevelTarget;

    if (this._targets.has(targetFront)) {
      // The top level target front can be reported via listProcesses in the
      // case of the BrowserToolbox. For any other target, log an error if it is
      // already registered.
      if (targetFront != this.targetFront) {
        console.error(
          "Target is already registered in the TargetCommand",
          targetFront.actorID
        );
      }
      return;
    }

    if (this.isDestroyed() || targetFront.isDestroyedOrBeingDestroyed()) {
      return;
    }

    // Handle top level target switching (when debugging a tab, navigation of the tab to a new document)
    if (targetFront.isTopLevel) {
      // First report that all existing targets are destroyed
      if (!isFirstTarget) {
        this._destroyExistingTargetsOnTargetSwitching();
      }

      // Update the reference to the memoized top level target
      this.targetFront = targetFront;
      this.descriptorFront.setTarget(targetFront);

      // When reloading a Web Extension, the fallback document, which is the top level may be notified *after* the background page.
      // So avoid resetting it being selected on late arrival of the fallback document.
      if (!this.descriptorFront.isWebExtensionDescriptor) {
        this.#selectedTargetFront = null;
      }

      if (isFirstTarget && this.isServerTargetSwitchingEnabled()) {
        this._gotFirstTopLevelTarget = true;
        this._resolveOnFirstTarget();
      }
    }

    this._targets.add(targetFront);
    try {
      await targetFront.attachAndInitThread(this);
    } catch (e) {
      console.error("Error when attaching target:", e);
      this._targets.delete(targetFront);
      return;
    }

    for (const targetFrontsSet of this._pendingWatchTargetInitialization.values()) {
      targetFrontsSet.delete(targetFront);
    }

    if (this.isDestroyed() || targetFront.isDestroyedOrBeingDestroyed()) {
      return;
    }

    this.store.dispatch(registerTarget(targetFront));

    // Then, once the target is attached, notify the target front creation listeners
    await this._createListeners.emitAsync(targetFront.targetType, {
      targetFront,
      isTargetSwitching,
    });

    // Re-register the listeners as the top level target changed
    // and some targets are fetched from it
    if (targetFront.isTopLevel && !isFirstTarget) {
      await this.startListening({ isTargetSwitching: true });
    }

    // These two events are used by tests using the production codepath (i.e. disabling flags.testing)
    // To be consumed by tests triggering frame navigations, spawning workers...
    this.emit("processed-available-target", targetFront);

    if (isTargetSwitching) {
      this.emit("switched-target", targetFront);
    }

    const autoSelectTarget =
      // When we navigate to a new top level document, automatically select that new document's target,
      // so that tools can start selecting and displaying this new document.
      isTargetSwitching ||
      // When debugging Web Extension, workaround the fallback document by automatically selected any incoming target
      // as soon as we are currently selecting that fallback document.
      (this.descriptorFront.isWebExtensionDescriptor &&
        this.#selectedTargetFront?.isFallbackExtensionDocument);

    if (autoSelectTarget) {
      await this.selectTarget(targetFront);
    }
  }

  _destroyExistingTargetsOnTargetSwitching() {
    const destroyedTargets = [];
    for (const target of this._targets) {
      // We only consider the top level target to be switched
      const isDestroyedTargetSwitching = target == this.targetFront;
      const isServiceWorker = target.targetType === this.TYPES.SERVICE_WORKER;
      const isPopup = target.targetForm.isPopup;

      // Never destroy the popup targets when the top level target is destroyed
      // as the popup follow a different lifecycle.
      // Also avoid destroying service worker targets for similar reason.
      if (!isPopup && !isServiceWorker) {
        this._onTargetDestroyed(target, {
          isTargetSwitching: isDestroyedTargetSwitching,
          // Do not destroy service worker front as we may want to keep using it.
          shouldDestroyTargetFront: !isServiceWorker,
        });
        destroyedTargets.push(target);
      }
    }

    // Stop listening to legacy listeners as we now have to listen
    // on the new target.
    this.stopListening({ isTargetSwitching: true });

    // Remove destroyed target from the cached target list. We don't simply clear the
    // Map as SW targets might not have been destroyed.
    for (const target of destroyedTargets) {
      this._targets.delete(target);
    }
  }

  /**
   * Function fired everytime a target is destroyed.
   *
   * This is called either:
   * - via target-destroyed event fired by the WatcherFront,
   *   event which is a simple translation of the target-destroyed-form emitted by the WatcherActor.
   *   Watcher Actor emits this is various condition when the debugged target is meant to be destroyed:
   *   - the related target context is destroyed (tab closed, worker shut down, content process destroyed, ...),
   *   - when the DevToolsServerConnection used on the server side to communicate to the client is closed.

   * - by TargetCommand._onTargetAvailable, when a top level target switching happens and all previously
   *   registered target fronts should be destroyed.

   * - by the legacy Targets listeners, calling this method directly.
   *   This usecase is meant to be removed someday when all target targets are supported by the Watcher.
   *   (bug 1687459)
   *
   * @param {TargetFront} targetFront
   *        The target that just got destroyed.
   * @param {object} options
   * @param {boolean} [options.isTargetSwitching]
   *        To be set to true when this is about the top level target which is being replaced
   *        by a new one.
   *        The passed target should be still the one store in TargetCommand.targetFront
   *        and will be replaced via a call to onTargetAvailable with a new target front.
   * @param {boolean} [options.isModeSwitching]
   *        To be set to true when the target was destroyed was called as the result of a
   *        change to the devtools.browsertoolbox.scope pref.
   * @param {boolean} [options.shouldDestroyTargetFront]
   *        By default, the passed target front will be destroyed. But in some cases like
   *        legacy listeners for service workers we want to keep the front alive.
   */
  _onTargetDestroyed(
    targetFront,
    {
      isModeSwitching = false,
      isTargetSwitching = false,
      shouldDestroyTargetFront = true,
    } = {}
  ) {
    // The watcher actor may notify us about the destruction of the top level target.
    // But second argument to this method, isTargetSwitching is only passed from the frontend.
    // So automatically toggle the isTargetSwitching flag for server side destructions
    // only if that's about the existing top level target.
    if (targetFront == this.targetFront) {
      isTargetSwitching = true;
    }
    this._destroyListeners.emit(targetFront.targetType, {
      targetFront,
      isTargetSwitching,
      isModeSwitching,
    });
    this._targets.delete(targetFront);

    this.store.dispatch(unregisterTarget(targetFront));

    // If the destroyed target was the selected one, we need to do some cleanup
    if (this.#selectedTargetFront == targetFront) {
      // If we're doing a targetSwitch, simply nullify #selectedTargetFront
      if (isTargetSwitching) {
        this.#selectedTargetFront = null;
      } else {
        // Otherwise we want to select the top level target
        let fallbackTarget = this.targetFront;

        // When debugging Web Extension we don't want to immediately fallback to the top level target, which is the fallback document.
        // Instead, try to lookup for the background page.
        if (this.descriptorFront.isWebExtensionDescriptor) {
          const backgroundPageTargetFront = this._targets
            .values()
            .find(target => !target.isFallbackExtensionDocument);
          if (backgroundPageTargetFront) {
            fallbackTarget = backgroundPageTargetFront;
          }
        }

        this.selectTarget(fallbackTarget);
      }
    }

    if (shouldDestroyTargetFront) {
      // When calling targetFront.destroy(), we will first call TargetFrontMixin.destroy,
      // which will try to call `detach` RDP method.
      // Unfortunately, this request will never complete in some cases like bfcache navigations.
      // Because of that, the target front will never be completely destroy as it will prevent
      // calling super.destroy and Front.destroy.
      // Workaround that by manually calling Front class destroy method:
      targetFront.baseFrontClassDestroy();

      targetFront.destroy();

      // Delete the attribute we set from _onTargetAvailable so that we avoid leaking commands
      // if any target front is leaked.
      delete targetFront.commands;
    }
  }

  /**
   *
   * @param {TargetFront} targetFront
   */
  async _onTargetSelected(targetFront) {
    if (this.#selectedTargetFront == targetFront) {
      // Target is already selected, we can bail out.
      return;
    }

    this.#selectedTargetFront = targetFront;
    await this._selectListeners.emitAsync(targetFront.targetType, {
      targetFront,
    });
  }

  _setListening(type, value) {
    if (value) {
      this._listenersStarted.add(type);
    } else {
      this._listenersStarted.delete(type);
    }
  }

  _isListening(type) {
    return this._listenersStarted.has(type);
  }

  /**
   * Check if the watcher is currently supported.
   *
   * When no typeOrTrait is provided, we will only check that the watcher is
   * available.
   *
   * When a typeOrTrait is provided, we will check for an explicit trait on the
   * watcherFront that indicates either that:
   *   - a target type is supported
   *   - or that a custom trait is true
   *
   * @param {string} [targetTypeOrTrait]
   *        Optional target type or trait.
   * @return {boolean} true if the watcher is available and supports the
   *          optional targetTypeOrTrait
   */
  hasTargetWatcherSupport(targetTypeOrTrait) {
    if (targetTypeOrTrait) {
      // Target types are also exposed as traits, where resource types are
      // exposed under traits.resources (cf hasResourceWatcherSupport
      // implementation).
      return !!this.watcherFront?.traits[targetTypeOrTrait];
    }

    return !!this.watcherFront;
  }

  /**
   * Start listening for targets from the server
   *
   * Interact with the actors in order to start listening for new types of targets.
   * This will fire the _onTargetAvailable function for all already-existing targets,
   * as well as the next one to be created. It will also call _onTargetDestroyed
   * everytime a target is reported as destroyed by the actors.
   * By the time this function resolves, all the already-existing targets will be
   * reported to _onTargetAvailable.
   *
   * @param Object options
   * @param Boolean options.isTargetSwitching
   *        Set to true when this is called while a target switching happens. In such case,
   *        we won't register listener set on the Watcher Actor, but still register listeners
   *        set via Legacy Listeners.
   */
  async startListening({ isTargetSwitching = false } = {}) {
    // The first time we call this method, we pull the current top level target from the descriptor
    if (
      !this.isServerTargetSwitchingEnabled() &&
      !this._gotFirstTopLevelTarget
    ) {
      await this._createFirstTarget();
    }

    // If no pref are set to true, nor is listenForWorkers set to true,
    // we won't listen for any additional target. Only the top level target
    // will be managed. We may still do target-switching.
    const types = this._computeTargetTypes();

    for (const type of types) {
      if (this._isListening(type)) {
        continue;
      }
      this._setListening(type, true);

      // Only a few top level targets support the watcher actor at the moment (see WatcherActor
      // traits in the _form method). Bug 1675763 tracks watcher actor support for all targets.
      if (this.hasTargetWatcherSupport(type)) {
        // When we switch to a new top level target, we don't have to stop and restart
        // Watcher listener as it is independant from the top level target.
        // This isn't the case for some Legacy Listeners, which fetch targets from the top level target
        if (!isTargetSwitching) {
          await this.watcherFront.watchTargets(type);
        }
      } else if (LegacyTargetWatchers[type]) {
        // Instantiate the legacy listener only once for each TargetCommand, and reuse it if we stop and restart listening
        if (!this.legacyImplementation[type]) {
          this.legacyImplementation[type] = new LegacyTargetWatchers[type](
            this,
            this._onTargetAvailable,
            this._onTargetDestroyed,
            this.commands
          );
        }
        await this.legacyImplementation[type].listen();
      } else {
        throw new Error(`Unsupported target type '${type}'`);
      }
    }

    if (!this._watchingDocumentEvent && !this.isDestroyed()) {
      // We want to watch DOCUMENT_EVENT in order to update the url and title of target fronts,
      // as the initial value that is set in them might be erroneous (if the target was
      // created so early that the document url is still pointing to about:blank and the
      // html hasn't be parsed yet, so we can't know the <title> content).

      this._watchingDocumentEvent = true;
      await this.commands.resourceCommand.watchResources(
        [this.commands.resourceCommand.TYPES.DOCUMENT_EVENT],
        {
          onAvailable: this._onResourceAvailable,
        }
      );
    }

    if (this.isServerTargetSwitchingEnabled()) {
      await this._onFirstTarget;
    }
  }

  async _createFirstTarget() {
    // Note that this is a public attribute, used outside of this class
    // and helps knowing what is the current top level target we debug.
    this.targetFront = await this.descriptorFront.getTarget();
    this.targetFront.setIsTopLevel(true);
    this._gotFirstTopLevelTarget = true;

    // See _onTargetAvailable. As this target isn't going through that method
    // we have to replicate doing that here.
    this.targetFront.commands = this.commands;

    // Add the top-level target to the list of targets.
    this._targets.add(this.targetFront);
    this.store.dispatch(registerTarget(this.targetFront));
  }

  _computeTargetTypes() {
    let types = [];

    // We also check for watcher support as some xpcshell tests uses legacy APIs and don't support frames.
    if (
      this.descriptorFront.isTabDescriptor &&
      this.hasTargetWatcherSupport(TargetCommand.TYPES.FRAME)
    ) {
      types = [TargetCommand.TYPES.FRAME];
      const showContentScripts = Services.prefs.getBoolPref(
        SHOW_CONTENT_SCRIPTS_PREF,
        false
      );
      if (
        showContentScripts &&
        this.hasTargetWatcherSupport(TargetCommand.TYPES.CONTENT_SCRIPT)
      ) {
        types.push(TargetCommand.TYPES.CONTENT_SCRIPT);
      }
    } else if (
      this.descriptorFront.isWebExtensionDescriptor &&
      this.descriptorFront.isServerTargetSwitchingEnabled()
    ) {
      types = [TargetCommand.TYPES.FRAME];
    } else if (this.descriptorFront.isBrowserProcessDescriptor) {
      const browserToolboxScope = Services.prefs.getCharPref(
        BROWSERTOOLBOX_SCOPE_PREF
      );
      if (browserToolboxScope == BROWSERTOOLBOX_SCOPE_EVERYTHING) {
        // Listen for all target types when the browser toolbox is in multiprocess mode.
        //
        // Except for CONTENT_SCRIPT targets, as their scripts are already debuggable
        // via the content process targets.
        if (!this.listenForContentScripts) {
          types = TargetCommand.ALL_TYPES.filter(
            t => t != TargetCommand.TYPES.CONTENT_SCRIPT
          );
        } else {
          types = TargetCommand.ALL_TYPES;
        }
      }
    }
    if (this.listenForWorkers && !types.includes(TargetCommand.TYPES.WORKER)) {
      types.push(TargetCommand.TYPES.WORKER);
    }

    // Bug 1607778 - For now, shared workers are only displayed in the Browser Toolbox.
    // The server doesn't expose them yet. See `getWatcherSupportedTargets()` and
    // `WorkerTargetWatcherClass.shouldHandleWorker`.
    if (
      this.listenForWorkers &&
      this.descriptorFront.isBrowserProcessDescriptor &&
      !types.includes(TargetCommand.TYPES.SHARED_WORKER)
    ) {
      types.push(TargetCommand.TYPES.SHARED_WORKER);
    }

    if (
      this.listenForServiceWorkers &&
      !types.includes(TargetCommand.TYPES.SERVICE_WORKER)
    ) {
      types.push(TargetCommand.TYPES.SERVICE_WORKER);
    }

    return types;
  }

  /**
   * Stop listening for targets from the server
   *
   * @param Object options
   * @param Boolean options.isTargetSwitching
   *        Set to true when this is called while a target switching happens. In such case,
   *        we won't unregister listener set on the Watcher Actor, but still unregister
   *        listeners set via Legacy Listeners.
   */
  stopListening({ isTargetSwitching = false } = {}) {
    // As DOCUMENT_EVENT isn't using legacy listener,
    // there is no need to stop and restart it in case of target switching.
    if (this._watchingDocumentEvent && !isTargetSwitching) {
      this.commands.resourceCommand.unwatchResources(
        [this.commands.resourceCommand.TYPES.DOCUMENT_EVENT],
        {
          onAvailable: this._onResourceAvailable,
        }
      );
      this._watchingDocumentEvent = false;
    }

    for (const type of TargetCommand.ALL_TYPES) {
      this.stopListeningForType(type, { isTargetSwitching });
    }
  }

  /**
   * Stop listening for targets of a given type from the server
   *
   * @param String type
   *        target type we want to stop listening for
   * @param Object options
   * @param Boolean options.isTargetSwitching
   *        Set to true when this is called while a target switching happens. In such case,
   *        we won't unregister listener set on the Watcher Actor, but still unregister
   *        listeners set via Legacy Listeners.
   * @param Boolean options.isModeSwitching
   *        Set to true when this is called as the result of a change to the
   *        devtools.browsertoolbox.scope pref.
   */
  stopListeningForType(type, { isTargetSwitching, isModeSwitching }) {
    if (!this._isListening(type)) {
      return;
    }
    this._setListening(type, false);

    // Only a few top level targets support the watcher actor at the moment (see WatcherActor
    // traits in the _form method). Bug 1675763 tracks watcher actor support for all targets.
    if (this.hasTargetWatcherSupport(type)) {
      // When we switch to a new top level target, we don't have to stop and restart
      // Watcher listener as it is independant from the top level target.
      // This isn't the case for some Legacy Listeners, which fetch targets from the top level target
      // Also, TargetCommand.destroy may be called after the client is closed.
      // So avoid calling the RDP method in that situation.
      if (!isTargetSwitching && !this.watcherFront.isDestroyed()) {
        this.watcherFront.unwatchTargets(type, { isModeSwitching });
      }
    } else if (this.legacyImplementation[type]) {
      this.legacyImplementation[type].unlisten({
        isTargetSwitching,
        isModeSwitching,
      });
    } else {
      throw new Error(`Unsupported target type '${type}'`);
    }
  }

  _matchTargetType(type, target) {
    return type === target.targetType;
  }

  _onResourceAvailable(resources) {
    for (const resource of resources) {
      if (
        resource.resourceType ===
        this.commands.resourceCommand.TYPES.DOCUMENT_EVENT
      ) {
        const { targetFront } = resource;
        if (resource.title !== undefined && targetFront?.setTitle) {
          targetFront.setTitle(resource.title);
        }
        if (resource.url !== undefined && targetFront?.setUrl) {
          targetFront.setUrl(resource.url);
        }
        if (resource.name == "dom-loading" && targetFront?.setIsErrorPage) {
          targetFront.setIsErrorPage(!!resource.isErrorPage);
        }
        if (
          !resource.isFrameSwitching &&
          // `url` is set on the targetFront when we receive dom-loading, and `title` when
          // `dom-interactive` is received. Here we're only updating the window title in
          // the "newer" event.
          resource.name === "dom-interactive"
        ) {
          // We just updated the targetFront title and url, force a refresh
          // so that the EvaluationContext selector update them.
          this.store.dispatch(refreshTargets());

          // Emit an helpful event to keep track of all target's url/title changes
          this.emit("target-location-updated", targetFront);
        }
      }
    }
  }

  /**
   * Listen for the creation and/or destruction of target fronts matching one of the provided types.
   *
   * @param {object} options
   * @param {Array<string>} options.types
   *        The type of target to listen for. Constant of TargetCommand.TYPES.
   * @param {Function} options.onAvailable
   *        Mandatory callback fired when a target has been just created or was already available.
   *        The function is called with a single object argument containing the following properties:
   *        - {TargetFront} targetFront: The target Front
   *        - {Boolean} isTargetSwitching: Is this target relates to a navigation and
   *                    this replaced a previously available target, this flag will be true
   * @param {Function} options.onDestroyed
   *        Optional callback fired in case of target front destruction.
   *        The function is called with the same arguments than onAvailable.
   * @param {Function} options.onSelected
   *        Optional callback fired for any new top level target (on startup and navigations),
   *        as well as when the user select a new target from the console context selector,
   *        debugger threads list and toolbox iframe dropdown.
   *        The function is called with a single object argument containing the following properties:
   *        - {TargetFront} targetFront: The target Front
   */
  async watchTargets(options = {}) {
    const availableOptions = [
      "types",
      "onAvailable",
      "onDestroyed",
      "onSelected",
    ];
    const unsupportedKeys = Object.keys(options).filter(
      key => !availableOptions.includes(key)
    );
    if (unsupportedKeys.length) {
      throw new Error(
        `TargetCommand.watchTargets does not expect the following options: ${unsupportedKeys.join(
          ", "
        )}`
      );
    }

    const { types, onAvailable, onDestroyed, onSelected } = options;
    if (typeof onAvailable != "function") {
      throw new Error(
        "TargetCommand.watchTargets expects a function for the onAvailable option"
      );
    }

    for (const type of types) {
      if (!this._isValidTargetType(type)) {
        throw new Error(
          `TargetCommand.watchTargets invoked with an unknown type: "${type}"`
        );
      }
    }

    // Notify about already existing target of these types
    const targetFronts = this._targets
      .values()
      .filter(targetFront => types.includes(targetFront.targetType))
      .toArray();
    this._pendingWatchTargetInitialization.set(
      onAvailable,
      new Set(targetFronts)
    );
    const promises = targetFronts.map(async targetFront => {
      // Attach the targets that aren't attached yet (e.g. the initial top-level target),
      // and wait for the other ones to be fully attached.
      try {
        await targetFront.attachAndInitThread(this);
      } catch (e) {
        console.error("Error when attaching target:", e);
        return;
      }

      // It can happen that onAvailable was already called with this targetFront at
      // this time (via _onTargetAvailable). If that's the case, we don't want to call
      // onAvailable a second time.
      if (
        this._pendingWatchTargetInitialization &&
        this._pendingWatchTargetInitialization.has(onAvailable) &&
        !this._pendingWatchTargetInitialization
          .get(onAvailable)
          .has(targetFront)
      ) {
        return;
      }

      try {
        // Ensure waiting for eventual async create listeners
        // which may setup things regarding the existing targets
        // and listen callsite may care about the full initialization
        await onAvailable({
          targetFront,
          isTargetSwitching: false,
        });
      } catch (e) {
        // Prevent throwing when onAvailable handler throws on one target
        // so that it can try to register the other targets
        console.error(
          "Exception when calling onAvailable handler",
          e.message,
          e
        );
      }
    });

    for (const type of types) {
      this._createListeners.on(type, onAvailable);
      if (onDestroyed) {
        this._destroyListeners.on(type, onDestroyed);
      }
      if (onSelected) {
        this._selectListeners.on(type, onSelected);
      }
    }

    await Promise.all(promises);
    this._pendingWatchTargetInitialization.delete(onAvailable);

    try {
      if (
        onSelected &&
        this.selectedTargetFront &&
        types.includes(this.selectedTargetFront.targetType)
      ) {
        await onSelected({
          targetFront: this.selectedTargetFront,
        });
      }
    } catch (e) {
      // Prevent throwing when onSelected handler throws on one target
      // (this may make test to fail when closing the toolbox quickly after opening)
      console.error("Exception when calling onSelected handler", e.message, e);
    }
  }

  /**
   * Stop listening for the creation and/or destruction of a given type of target fronts.
   * See `watchTargets()` for documentation of the arguments.
   */
  unwatchTargets(options = {}) {
    const availableOptions = [
      "types",
      "onAvailable",
      "onDestroyed",
      "onSelected",
    ];
    const unsupportedKeys = Object.keys(options).filter(
      key => !availableOptions.includes(key)
    );
    if (unsupportedKeys.length) {
      throw new Error(
        `TargetCommand.unwatchTargets does not expect the following options: ${unsupportedKeys.join(
          ", "
        )}`
      );
    }

    const { types, onAvailable, onDestroyed, onSelected } = options;
    if (typeof onAvailable != "function") {
      throw new Error(
        "TargetCommand.unwatchTargets expects a function for the onAvailable option"
      );
    }

    for (const type of types) {
      if (!this._isValidTargetType(type)) {
        throw new Error(
          `TargetCommand.unwatchTargets invoked with an unknown type: "${type}"`
        );
      }

      this._createListeners.off(type, onAvailable);
      if (onDestroyed) {
        this._destroyListeners.off(type, onDestroyed);
      }
      if (onSelected) {
        this._selectListeners.off(type, onSelected);
      }
    }
    this._pendingWatchTargetInitialization.delete(onAvailable);
  }

  /**
   * Retrieve all the current target fronts of a given type.
   *
   * @param {Array<string>} types
   *        The types of target to retrieve. Array of TargetCommand.TYPES
   * @return {Array<TargetFront>} Array of target fronts matching any of the
   *         provided types.
   */
  getAllTargets(types) {
    if (!types?.length) {
      throw new Error("getAllTargets expects a non-empty array of types");
    }

    const targets = Array.from(
      this._targets
        .values()
        .filter(target =>
          types.some(type => this._matchTargetType(type, target))
        )
    );

    return targets;
  }

  /**
   * Retrieve all the target fronts in the selected target tree (including the selected
   * target itself).
   *
   * @param {Array<string>} types
   *        The types of target to retrieve. Array of TargetCommand.TYPES
   * @return {Promise<Array<TargetFront>>} Promise that resolves to an array of target fronts.
   */
  async getAllTargetsInSelectedTargetTree(types) {
    const allTargets = this.getAllTargets(types);
    if (this.isTopLevelTargetSelected()) {
      return allTargets;
    }

    const targets = [this.selectedTargetFront];
    for (const target of allTargets) {
      const isInSelectedTree = await target.isTargetAnAncestor(
        this.selectedTargetFront
      );

      if (isInSelectedTree) {
        targets.push(target);
      }
    }
    return targets;
  }

  /**
   * For all the target fronts of given types, retrieve all the target-scoped fronts of the given types.
   *
   * @param {Array<string>} targetTypes
   *        The types of target to iterate over. Constant of TargetCommand.TYPES.
   * @param {string} frontType
   *        The type of target-scoped front to retrieve. It can be "inspector", "console", "thread",...
   * @param {object} options
   * @param {boolean} options.onlyInSelectedTargetTree
   *        Set to true to only get the fronts for targets who are in the "targets tree"
   *        of the selected target.
   */
  async getAllFronts(
    targetTypes,
    frontType,
    { onlyInSelectedTargetTree = false } = {}
  ) {
    if (!Array.isArray(targetTypes) || !targetTypes?.length) {
      throw new Error("getAllFronts expects a non-empty array of target types");
    }
    const promises = [];
    const targets = !onlyInSelectedTargetTree
      ? this.getAllTargets(targetTypes)
      : await this.getAllTargetsInSelectedTargetTree(targetTypes);
    for (const target of targets) {
      // For still-attaching worker targets, the thread or console front may not yet be available,
      // whereas TargetMixin.getFront will throw if the actorID isn't available in targetForm.
      // Also ignore destroyed targets. For some reason the previous methods fetching targets
      // can sometime return destroyed targets.
      if (
        (frontType == "thread" && !target.targetForm.threadActor) ||
        (frontType == "console" && !target.targetForm.consoleActor) ||
        target.isDestroyed()
      ) {
        continue;
      }

      promises.push(target.getFront(frontType));
    }
    return Promise.all(promises);
  }

  /**
   * This function is triggered by an event sent by the TabDescriptor when
   * the tab navigates to a distinct process.
   *
   * @param TargetFront targetFront
   *        The WindowGlobalTargetFront instance that navigated to another process
   */
  async onLocalTabRemotenessChange(targetFront) {
    if (this.isServerTargetSwitchingEnabled()) {
      // For server-side target switching, everything will be handled by the
      // _onTargetAvailable callback.
      return;
    }

    // TabDescriptor may emit the event with a null targetFront, interpret that as if the previous target
    // has already been destroyed
    if (targetFront) {
      // Wait for the target to be destroyed so that LocalTabCommandsFactory clears its memoized target for this tab
      await targetFront.once("target-destroyed");
    }

    // Fetch the new target from the descriptor.
    const newTarget = await this.descriptorFront.getTarget();

    // If a navigation happens while we try to get the target for the page that triggered
    // the remoteness change, `getTarget` will return null. In such case, we'll get the
    // "next" target through onTargetAvailable so it's safe to bail here.
    if (!newTarget) {
      console.warn(
        `Couldn't get the target for descriptor ${this.descriptorFront.actorID}`
      );
      return;
    }

    this.switchToTarget(newTarget);
  }

  /**
   * Reload the current top level target.
   * This only works for targets inheriting from WindowGlobalTarget.
   *
   * @param {boolean} bypassCache
   *        If true, the reload will be forced to bypass any cache.
   */
  async reloadTopLevelTarget(bypassCache = false) {
    if (!this.descriptorFront.traits.supportsReloadDescriptor) {
      throw new Error("The top level target doesn't support being reloaded");
    }

    // Wait for the next DOCUMENT_EVENT's dom-complete event
    // Wait for waitForNextResource completion before reloading, otherwise we might miss the dom-complete event.
    // This can happen if `ResourceCommand.watchResources` made by `waitForNextResource` is still pending
    // while the reload already started and finished loading the document early.
    const { onResource: onReloaded } =
      await this.commands.resourceCommand.waitForNextResource(
        this.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
        {
          ignoreExistingResources: true,
          predicate(resource) {
            return resource.name == "dom-complete";
          },
        }
      );

    await this.descriptorFront.reloadDescriptor({ bypassCache });

    await onReloaded;
  }

  /**
   * Navigate the top level document to a new URL.
   *
   * @param {string} url
   * @param {boolean} waitForLoad
   *        Default to true and wait for the document to be fully loaded before resolving.
   * @return Promise
   *        Promise resolved once the navigation has been proceeded by the remote runtime,
   *        and if waitForLoad is true, resolved only once the target url is fully loaded.
   */
  navigateTo(url, waitForLoad = true) {
    if (!this.descriptorFront.traits.supportsNavigation) {
      throw new Error("Descriptor doesn't support navigation");
    }

    return this.descriptorFront.navigateTo(url, waitForLoad);
  }

  goBack() {
    if (!this.descriptorFront.traits.supportsNavigation) {
      throw new Error("Descriptor doesn't support navigation");
    }

    return this.descriptorFront.goBack();
  }

  goForward() {
    if (!this.descriptorFront.traits.supportsNavigation) {
      throw new Error("Descriptor doesn't support navigation");
    }
    return this.descriptorFront.goForward();
  }

  /**
   * Called when the top level target is replaced by a new one.
   * Typically when we navigate to another domain which requires to be loaded in a distinct process.
   *
   * @param {TargetFront} newTarget
   *        The new top level target to debug.
   */
  async switchToTarget(newTarget) {
    // Notify about this new target to creation listeners
    // _onTargetAvailable will also destroy all previous target before notifying about this new one.
    await this._onTargetAvailable(newTarget);
  }

  /**
   * Called when the user selects a frame in the iframe picker.
   *
   * @param {WindowGlobalTargetFront} targetFront
   *        The target front we want the toolbox to focus on.
   */
  async selectTarget(targetFront) {
    // Ignore any target which we may try to select, but is already being destroyed
    if (targetFront.isDestroyedOrBeingDestroyed()) {
      return;
    }
    await this._onTargetSelected(targetFront);
  }

  /**
   * Returns true if the top-level frame is the selected one
   *
   * @returns {boolean}
   */
  isTopLevelTargetSelected() {
    return this.selectedTargetFront === this.targetFront;
  }

  /**
   * Returns true if a non top-level frame is the selected one in the iframe picker.
   *
   * @returns {boolean}
   */
  isNonTopLevelTargetSelected() {
    return this.selectedTargetFront !== this.targetFront;
  }

  isTargetRegistered(targetFront) {
    return this._targets.has(targetFront);
  }

  getParentTarget(targetFront) {
    // Note that there are edgecases:
    // * Until bug 1741927 is fixed and we remove non-EFT codepath entirely,
    //   we may receive a `parentInnerWindowId` that doesn't relate to any target.
    //   This happens when the parent document of the targetFront is a document loaded in the
    //   same process as its parent document. In such scenario, and only when EFT is disabled,
    //   we won't instantiate a target for the parent document of the targetFront.
    // * `parentInnerWindowId` could be null in some case like for tabs in the MBT
    //   we should report the top level target as parent. That's what `getParentWindowGlobalTarget` does.
    //   Once we can stop using getParentWindowGlobalTarget for the other edgecase we will be able to
    //   replace it with such fallback: `return this.targetFront;`.
    //   browser_target_command_frames.js will help you get things right.
    const { parentInnerWindowId } = targetFront.targetForm;
    if (parentInnerWindowId) {
      const targets = this.getAllTargets([TargetCommand.TYPES.FRAME]);
      const parent = targets.find(
        target => target.innerWindowId == parentInnerWindowId
      );
      // Until EFT is the only codepath supported (bug 1741927), we will fallback to `getParentWindowGlobalTarget`
      // as we may not have a target if the parent is an iframe running in the same process as its parent.
      if (parent) {
        return parent;
      }
    }

    // Note that all callsites which care about FRAME additional target
    // should all have a toolbox using the watcher actor.
    // It should be: MBT, regular tab toolbox and web extension.
    // The others which still don't support watcher don't spawn FRAME targets:
    // browser content toolbox and service workers.

    return this.watcherFront.getParentWindowGlobalTarget(
      targetFront.browsingContextID
    );
  }

  isDestroyed() {
    return this._isDestroyed;
  }

  isServerTargetSwitchingEnabled() {
    if (this.descriptorFront.isServerTargetSwitchingEnabled) {
      return this.descriptorFront.isServerTargetSwitchingEnabled();
    }
    return false;
  }

  _isValidTargetType(type) {
    return this.ALL_TYPES.includes(type);
  }

  destroy() {
    this.stopListening();
    this._createListeners.off();
    this._destroyListeners.off();
    this._selectListeners.off();

    this.#selectedTargetFront = null;
    this._isDestroyed = true;

    Services.prefs.removeObserver(
      BROWSERTOOLBOX_SCOPE_PREF,
      this._updateBrowserToolboxScope
    );
    Services.prefs.removeObserver(
      SHOW_CONTENT_SCRIPTS_PREF,
      this._updateContentScriptListening
    );
  }
}

/**
 * All types of target:
 */
TargetCommand.TYPES = TargetCommand.prototype.TYPES = {
  PROCESS: "process",
  FRAME: "frame",
  WORKER: "worker",
  SHARED_WORKER: "shared_worker",
  SERVICE_WORKER: "service_worker",
  CONTENT_SCRIPT: "content_script",
};
TargetCommand.ALL_TYPES = TargetCommand.prototype.ALL_TYPES = Object.values(
  TargetCommand.TYPES
);

const LegacyTargetWatchers = {};
loader.lazyRequireGetter(
  LegacyTargetWatchers,
  TargetCommand.TYPES.PROCESS,
  "resource://devtools/shared/commands/target/legacy-target-watchers/legacy-processes-watcher.js"
);
loader.lazyRequireGetter(
  LegacyTargetWatchers,
  TargetCommand.TYPES.WORKER,
  "resource://devtools/shared/commands/target/legacy-target-watchers/legacy-workers-watcher.js"
);
loader.lazyRequireGetter(
  LegacyTargetWatchers,
  TargetCommand.TYPES.SHARED_WORKER,
  "resource://devtools/shared/commands/target/legacy-target-watchers/legacy-sharedworkers-watcher.js"
);
loader.lazyRequireGetter(
  LegacyTargetWatchers,
  TargetCommand.TYPES.SERVICE_WORKER,
  "resource://devtools/shared/commands/target/legacy-target-watchers/legacy-serviceworkers-watcher.js"
);

module.exports = TargetCommand;
