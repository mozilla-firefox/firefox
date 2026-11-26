/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Actor } = require("resource://devtools/shared/protocol.js");
const { walkerSpec } = require("resource://devtools/shared/specs/walker.js");

const {
  LongStringActor,
} = require("resource://devtools/server/actors/string.js");
const {
  EXCLUDED_LISTENER,
} = require("resource://devtools/server/actors/inspector/constants.js");

loader.lazyRequireGetter(
  this,
  "nodeFilterConstants",
  "resource://devtools/shared/dom-node-filter-constants.js"
);

loader.lazyRequireGetter(
  this,
  [
    "getFrameElement",
    "isDirectShadowHostChild",
    "isFrameBlockedByCSP",
    "isFrameWithChildTarget",
    "isShadowHost",
    "isShadowRoot",
    "loadSheet",
  ],
  "resource://devtools/shared/layout/utils.js",
  true
);

loader.lazyRequireGetter(
  this,
  "throttle",
  "resource://devtools/shared/throttle.js",
  true
);

loader.lazyRequireGetter(
  this,
  [
    "findGridParentContainerForNode",
    "isNodeDead",
    "isViewTransitionSnapshotContainingBlockNode",
    "nodeDocument",
    "getTreeWalkerFilter",
  ],
  "resource://devtools/server/actors/inspector/utils.js",
  true
);

loader.lazyRequireGetter(
  this,
  "CustomElementWatcher",
  "resource://devtools/server/actors/inspector/custom-element-watcher.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["DocumentWalker", "SKIP_TO_SIBLING"],
  "resource://devtools/server/actors/inspector/document-walker.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["NodeActor", "NodeListActor"],
  "resource://devtools/server/actors/inspector/node.js",
  true
);
loader.lazyRequireGetter(
  this,
  "NodePicker",
  "resource://devtools/server/actors/inspector/node-picker.js",
  true
);
loader.lazyRequireGetter(
  this,
  "LayoutActor",
  "resource://devtools/server/actors/layout.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["getLayoutChangesObserver", "releaseLayoutChangesObserver"],
  "resource://devtools/server/actors/reflow.js",
  true
);
loader.lazyRequireGetter(
  this,
  "WalkerSearch",
  "resource://devtools/server/actors/utils/walker-search.js",
  true
);

// ContentDOMReference requires ChromeUtils, which isn't available in worker context.
const lazy = {};
if (!isWorker) {
  loader.lazyGetter(
    lazy,
    "ContentDOMReference",
    () =>
      ChromeUtils.importESModule(
        "resource://gre/modules/ContentDOMReference.sys.mjs",
        // ContentDOMReference needs to be retrieved from the shared global
        // since it is a shared singleton.
        { global: "shared" }
      ).ContentDOMReference
  );
}

loader.lazyServiceGetter(
  this,
  "eventListenerService",
  "@mozilla.org/eventlistenerservice;1",
  "nsIEventListenerService"
);

// Minimum delay between two "new-mutations" events.
const MUTATIONS_THROTTLING_DELAY = 100;
// List of mutation types that should -not- be throttled.
const IMMEDIATE_MUTATIONS = ["pseudoClassLock"];

const HIDDEN_CLASS = "__fx-devtools-hide-shortcut__";

// The possible completions to a ':'
const PSEUDO_SELECTORS = [
  "::marker",
  "::selection",
  ":active",
  ":after",
  ":before",
  ":checked",
  ":disabled",
  ":empty",
  ":enabled",
  ":first-child",
  ":first-letter",
  ":first-of-type",
  ":focus",
  ":hover",
  ":lang(",
  ":last-child",
  ":last-of-type",
  ":link",
  ":not(",
  ":nth-child(",
  ":nth-last-child(",
  ":nth-last-of-type(",
  ":nth-of-type(",
  ":only-child",
  ":only-of-type",
  ":root",
  ":target",
  ":visited",
];

const HELPER_SHEET =
  "data:text/css;charset=utf-8," +
  encodeURIComponent(`
  .__fx-devtools-hide-shortcut__ {
    visibility: hidden !important;
  }
`);

/**
 * We only send nodeValue up to a certain size by default.  This stuff
 * controls that size.
 */
exports.DEFAULT_VALUE_SUMMARY_LENGTH = 50;
var gValueSummaryLength = exports.DEFAULT_VALUE_SUMMARY_LENGTH;

exports.getValueSummaryLength = function () {
  return gValueSummaryLength;
};

exports.setValueSummaryLength = function (val) {
  gValueSummaryLength = val;
};

/**
 * Server side of the DOM walker.
 */
class WalkerActor extends Actor {
  /**
   * Create the WalkerActor
   *
   * @param {DevToolsServerConnection} conn
   *        The server connection.
   * @param {TargetActor} targetActor
   *        The top-level Actor for this tab.
   * @param {object} options
   *        - {Boolean} showAllAnonymousContent: Show all native anonymous content
   *        - {Boolean} showComments: Show comment nodes
   */
  constructor(conn, targetActor, options) {
    super(conn, walkerSpec);
    this.targetActor = targetActor;
    this.rootWin = targetActor.window;
    this.rootDoc = this.rootWin.document;

    // Map of already created node actors, keyed by their corresponding DOMNode.
    this._nodeActorsMap = new Map();

    this._pendingMutations = [];
    this._activePseudoClassLocks = new Set();
    this._mutationBreakpoints = new WeakMap();
    this._anonParents = new WeakMap();
    this.customElementWatcher = new CustomElementWatcher(
      targetActor.chromeEventHandler
    );

    // In this map, the key-value pairs are the overflow causing elements and their
    // respective ancestor scrollable node actor.
    this.overflowCausingElementsMap = new Map();

    this.showAllAnonymousContent = options.showAllAnonymousContent;
    this.showComments = options.showComments;
    this.documentWalkerFilter = getTreeWalkerFilter({
      // Allow native anonymous content (like <video> controls) if preffed on
      includeNativeAnonymousContent: this.showAllAnonymousContent,
      includePseudoElements: true,
      // Allow comment nodes if preffed on
      includeComments: this.showComments,
    });
    this.noAnonymousContentDocumentWalkerFilter = getTreeWalkerFilter({
      includeNativeAnonymousContent: false,
      includePseudoElements: false,
      // Allow comment nodes if preffed on
      includeComments: this.showComments,
    });

    this.walkerSearch = new WalkerSearch(this);

    // Nodes which have been removed from the client's known
    // ownership tree are considered "orphaned", and stored in
    // this set.
    this._orphaned = new Set();

    // The client can tell the walker that it is interested in a node
    // even when it is orphaned with the `retainNode` method.  This
    // list contains orphaned nodes that were so retained.
    this._retainedOrphans = new Set();

    this.onSubtreeModified = this.onSubtreeModified.bind(this);
    this.onSubtreeModified[EXCLUDED_LISTENER] = true;
    this.onNodeRemoved = this.onNodeRemoved.bind(this);
    this.onNodeRemoved[EXCLUDED_LISTENER] = true;
    this.onAttributeModified = this.onAttributeModified.bind(this);
    this.onAttributeModified[EXCLUDED_LISTENER] = true;

    this.onMutations = this.onMutations.bind(this);
    this.onSlotchange = this.onSlotchange.bind(this);
    this.onShadowrootattached = this.onShadowrootattached.bind(this);
    this.onAnonymousrootcreated = this.onAnonymousrootcreated.bind(this);
    this.onAnonymousrootremoved = this.onAnonymousrootremoved.bind(this);
    this.onFrameLoad = this.onFrameLoad.bind(this);
    this.onFrameUnload = this.onFrameUnload.bind(this);
    this.onCustomElementDefined = this.onCustomElementDefined.bind(this);
    this._throttledEmitNewMutations = throttle(
      this._emitNewMutations.bind(this),
      MUTATIONS_THROTTLING_DELAY
    );

    targetActor.on("will-navigate", this.onFrameUnload);
    targetActor.on("window-ready", this.onFrameLoad);

    this.customElementWatcher.on(
      "element-defined",
      this.onCustomElementDefined
    );

    // Keep a reference to the chromeEventHandler for the current targetActor, to make
    // sure we will be able to remove the listener during the WalkerActor destroy().
    this.chromeEventHandler = targetActor.chromeEventHandler;
    // shadowrootattached is a chrome-only event. We enable it below.
    this.chromeEventHandler.addEventListener(
      "shadowrootattached",
      this.onShadowrootattached
    );
    // anonymousrootcreated is a chrome-only event. We enable it below.
    this.chromeEventHandler.addEventListener(
      "anonymousrootcreated",
      this.onAnonymousrootcreated
    );
    this.chromeEventHandler.addEventListener(
      "anonymousrootremoved",
      this.onAnonymousrootremoved
    );
    for (const { document } of this.targetActor.windows) {
      document.devToolsAnonymousAndShadowEventsEnabled = true;
    }

    // Ensure that the root document node actor is ready and
    // managed.
    this.rootNode = this.document();

    this.layoutChangeObserver = getLayoutChangesObserver(this.targetActor);
    this._onReflows = this._onReflows.bind(this);
    this.layoutChangeObserver.on("reflows", this._onReflows);
    this._onResize = this._onResize.bind(this);
    this.layoutChangeObserver.on("resize", this._onResize);

    this._onEventListenerChange = this._onEventListenerChange.bind(this);
    eventListenerService.addListenerChangeListener(this._onEventListenerChange);
  }

  get nodePicker() {
    if (!this._nodePicker) {
      this._nodePicker = new NodePicker(this, this.targetActor);
    }

    return this._nodePicker;
  }

  watchRootNode() {
    if (this.rootNode) {
      this.emit("root-available", this.rootNode);
    }
  }

  /**
   * Callback for eventListenerService.addListenerChangeListener
   *
   * @param nsISimpleEnumerator changesEnum
   *    enumerator of nsIEventListenerChange
   */
  _onEventListenerChange(changesEnum) {
    for (const current of changesEnum.enumerate(Ci.nsIEventListenerChange)) {
      const target = current.target;

      if (this._nodeActorsMap.has(target)) {
        const actor = this.getNode(target);
        const mutation = {
          type: "events",
          target: actor.actorID,
          hasEventListeners: actor.hasEventListeners(/* refreshCache */ true),
        };
        this.queueMutation(mutation);
      }
    }
  }

  // Returns the JSON representation of this object over the wire.
  form() {
    return {
      actor: this.actorID,
      root: this.rootNode.form(),
      rfpCSSColorScheme: ChromeUtils.shouldResistFingerprinting(
        "CSSPrefersColorScheme",
        null
      ),
      traits: {},
    };
  }

  toString() {
    return "[WalkerActor " + this.actorID + "]";
  }

  getDocumentWalker(node, skipTo) {
    return new DocumentWalker(node, this.rootWin, {
      filter: this.documentWalkerFilter,
      skipTo,
      showAnonymousContent: true,
    });
  }

  destroy() {
    if (this._destroyed) {
      return;
    }
    this._destroyed = true;
    super.destroy();
    try {
      this.clearPseudoClassLocks();
      this._activePseudoClassLocks = null;

      this.overflowCausingElementsMap.clear();
      this.overflowCausingElementsMap = null;

      this._hoveredNode = null;
      this.rootWin = null;
      this.rootDoc = null;
      this.rootNode = null;
      this.layoutHelpers = null;
      this._orphaned = null;
      this._retainedOrphans = null;

      this.targetActor.off("will-navigate", this.onFrameUnload);
      this.targetActor.off("window-ready", this.onFrameLoad);
      this.customElementWatcher.off(
        "element-defined",
        this.onCustomElementDefined
      );

      this.chromeEventHandler.removeEventListener(
        "shadowrootattached",
        this.onShadowrootattached
      );
      this.chromeEventHandler.removeEventListener(
        "anonymousrootcreated",
        this.onAnonymousrootcreated
      );
      this.chromeEventHandler.removeEventListener(
        "anonymousrootremoved",
        this.onAnonymousrootremoved
      );

      // This attribute is just for devtools, so we can unset once we're done.
      for (const { document } of this.targetActor.windows) {
        document.devToolsAnonymousAndShadowEventsEnabled = false;
      }

      this.onFrameLoad = null;
      this.onFrameUnload = null;

      this.customElementWatcher.destroy();
      this.customElementWatcher = null;

      this.walkerSearch.destroy();

      if (this._nodePicker) {
        this._nodePicker.destroy();
        this._nodePicker = null;
      }

      this.layoutChangeObserver.off("reflows", this._onReflows);
      this.layoutChangeObserver.off("resize", this._onResize);
      this.layoutChangeObserver = null;
      releaseLayoutChangesObserver(this.targetActor);

      eventListenerService.removeListenerChangeListener(
        this._onEventListenerChange
      );

      // Only nullify some key attributes after having removed all the listeners
      // as they may still be used in the related listeners.
      this._nodeActorsMap = null;
      this.onMutations = null;

      this.layoutActor = null;
      this.targetActor = null;
      this.chromeEventHandler = null;
      this.documentWalkerFilter = null;

      this.emit("destroyed");
    } catch (e) {
      console.error(e);
    }
  }

  release() {}

  unmanage(actor) {
    if (actor instanceof NodeActor) {
      if (
        this._activePseudoClassLocks &&
        this._activePseudoClassLocks.has(actor)
      ) {
        this.clearPseudoClassLocks(actor);
      }

      this.customElementWatcher.unmanageNode(actor);

      this._nodeActorsMap.delete(actor.rawNode);
    }
    super.unmanage(actor);
  }

  /**
   * Determine if the walker has come across this DOM node before.
   *
   * @param {DOMNode} rawNode
   * @return {boolean}
   */
  hasNode(rawNode) {
    return this._nodeActorsMap.has(rawNode);
  }

  /**
   * If the walker has come across this DOM node before, then get the
   * corresponding node actor.
   *
   * @param {DOMNode} rawNode
   * @return {NodeActor}
   */
  getNode(rawNode) {
    return this._nodeActorsMap.get(rawNode);
  }

  /**
   * Internal helper that will either retrieve the existing NodeActor for the
   * provided node or create the actor on the fly if it doesn't exist.
   * This method should only be called when we are sure that the node should be
   * known by the client and that the parent node is already known.
   *
   * Otherwise prefer `getNode` to only retrieve known actors or `attachElement`
   * to create node actors recursively.
   *
   * @param  {DOMNode} node
   *         The node for which we want to create or get an actor
   * @return {NodeActor} The corresponding NodeActor
   */
  _getOrCreateNodeActor(node) {
    let actor = this.getNode(node);
    if (actor) {
      return actor;
    }

    actor = new NodeActor(this, node);

    // Add the node actor as a child of this walker actor, assigning
    // it an actorID.
    this.manage(actor);
    this._nodeActorsMap.set(node, actor);

    if (node.nodeType === Node.DOCUMENT_NODE) {
      actor.watchDocument(node, this.onMutations);
    }

    if (isShadowRoot(actor.rawNode)) {
      actor.watchDocument(node.ownerDocument, this.onMutations);
      actor.watchSlotchange(this.onSlotchange);
    }

    this.customElementWatcher.manageNode(actor);

    return actor;
  }

  /**
   * When a custom element is defined, send a customElementDefined mutation for all the
   * NodeActors using this tag name.
   */
  onCustomElementDefined({ actors }) {
    actors.forEach(actor =>
      this.queueMutation({
        target: actor.actorID,
        type: "customElementDefined",
        customElementLocation: actor.getCustomElementLocation(),
      })
    );
  }

  _onReflows() {
    // Going through the nodes the walker knows about, see which ones have had their
    // containerType, display, scrollable or overflow state changed and send events if any.
    const containerTypeChanges = [];
    const displayTypeChanges = [];
    const scrollableStateChanges = [];
    const anchorNameChanges = [];

    const currentOverflowCausingElementsMap = new Map();

    for (const [node, actor] of this._nodeActorsMap) {
      if (Cu.isDeadWrapper(node)) {
        continue;
      }

      const displayType = actor.displayType;
      const isDisplayed = actor.isDisplayed;

      if (
        displayType !== actor.currentDisplayType ||
        isDisplayed !== actor.wasDisplayed
      ) {
        displayTypeChanges.push(actor);

        // Updating the original value
        actor.currentDisplayType = displayType;
        actor.wasDisplayed = isDisplayed;
      }

      const isScrollable = actor.isScrollable;
      if (isScrollable !== actor.wasScrollable) {
        scrollableStateChanges.push(actor);
        actor.wasScrollable = isScrollable;
      }

      if (isScrollable) {
        this.updateOverflowCausingElements(
          actor,
          currentOverflowCausingElementsMap
        );
      }

      const containerType = actor.containerType;
      if (containerType !== actor.currentContainerType) {
        containerTypeChanges.push(actor);
        actor.currentContainerType = containerType;
      }

      const anchorName = actor.anchorName;
      if (anchorName !== actor.currentAnchorName) {
        anchorNameChanges.push(actor);
        actor.currentAnchorName = anchorName;
      }
    }

    // Get the NodeActor for each node in the symmetric difference of
    // currentOverflowCausingElementsMap and this.overflowCausingElementsMap
    const overflowStateChanges = [...currentOverflowCausingElementsMap.keys()]
      .filter(node => !this.overflowCausingElementsMap.has(node))
      .concat(
        [...this.overflowCausingElementsMap.keys()].filter(
          node => !currentOverflowCausingElementsMap.has(node)
        )
      )
      .filter(node => this.hasNode(node))
      .map(node => this.getNode(node));

    this.overflowCausingElementsMap = currentOverflowCausingElementsMap;

    if (overflowStateChanges.length) {
      this.emit("overflow-change", overflowStateChanges);
    }

    if (displayTypeChanges.length) {
      this.emit("display-change", displayTypeChanges);
    }

    if (scrollableStateChanges.length) {
      this.emit("scrollable-change", scrollableStateChanges);
    }

    if (containerTypeChanges.length) {
      this.emit("container-type-change", containerTypeChanges);
    }

    if (anchorNameChanges.length) {
      this.emit("anchor-name-change", anchorNameChanges);
    }
  }

  /**
   * When the browser window gets resized, relay the event to the front.
   */
  _onResize() {
    this.emit("resize");
  }

  /**
   * Ensures that the node is attached and it can be accessed from the root.
   *
   * @param {(Node|NodeActor)} nodes The nodes
   * @return {object} An object compatible with the disconnectedNode type.
   */
  attachElement(node) {
    const { nodes, newParents } = this.attachElements([node]);
    return {
      node: nodes[0],
      newParents,
    };
  }

  /**
   * Ensures that the nodes are attached and they can be accessed from the root.
   *
   * @param {(Node[]|NodeActor[])} nodes The nodes
   * @return {object} An object compatible with the disconnectedNodeArray type.
   */
  attachElements(nodes) {
    const nodeActors = [];
    const newParents = new Set();
    for (let node of nodes) {
      if (!(node instanceof NodeActor)) {
        // If the provided node doesn't match the filter, use the closest ancestor
        while (
          node &&
          this.documentWalkerFilter(node) != nodeFilterConstants.FILTER_ACCEPT
        ) {
          node = this.rawParentNode(node);
        }
        if (!node) {
          continue;
        }

        node = this._getOrCreateNodeActor(node);
      }

      this.ensurePathToRoot(node, newParents);
      // If nodes may be an array of raw nodes, we're sure to only have
      // NodeActors with the following array.
      nodeActors.push(node);
    }

    return {
      nodes: nodeActors,
      newParents: [...newParents],
    };
  }

  /**
   * Return the document node that contains the given node,
   * or the root node if no node is specified.
   *
   * @param NodeActor node
   *        The node whose document is needed, or null to
   *        return the root.
   */
  document(node) {
    const doc = isNodeDead(node) ? this.rootDoc : nodeDocument(node.rawNode);
    return this._getOrCreateNodeActor(doc);
  }

  /**
   * Return the documentElement for the document containing the
   * given node, or null if the document has no root element.
   *
   * @param NodeActor node
   *        The node whose documentElement is requested, or null
   *        to use the root document.
   */
  documentElement(node) {
    const elt = isNodeDead(node)
      ? this.rootDoc.documentElement
      : nodeDocument(node.rawNode).documentElement;
    if (!elt) {
      return null;
    }
    return this._getOrCreateNodeActor(elt);
  }

  parentNode(node) {
    const parent = this.rawParentNode(node);
    if (parent) {
      return this._getOrCreateNodeActor(parent);
    }

    return null;
  }

  rawParentNode(node) {
    const rawNode = node instanceof NodeActor ? node.rawNode : node;
    if (rawNode == this.rootDoc) {
      return null;
    }
    const parentNode = InspectorUtils.getParentForNode(
      rawNode,
      /* anonymous = */ true
    );

    if (!parentNode) {
      return null;
    }

    // If the parent node is one we should ignore (e.g. :-moz-snapshot-containing-block,
    // which is the root node for ::view-transition pseudo elements), we want to return
    // the closest non-ignored parent.
    if (
      this.documentWalkerFilter(parentNode) ===
      nodeFilterConstants.FILTER_ACCEPT_CHILDREN
    ) {
      return this.rawParentNode(parentNode);
    }

    return parentNode;
  }

  /**
   * If the given NodeActor only has a single text node as a child with a text
   * content small enough to be inlined, return that child's NodeActor.
   *
   * @param Element rawNode
   */
  inlineTextChild(rawNode) {
    // Quick checks to prevent creating a new walker if possible.
    if (
      !!rawNode.implementedPseudoElement ||
      isShadowHost(rawNode) ||
      rawNode.nodeType != Node.ELEMENT_NODE ||
      !!rawNode.children.length ||
      isFrameWithChildTarget(this.targetActor, rawNode) ||
      isFrameBlockedByCSP(rawNode)
    ) {
      return undefined;
    }

    const children = this._rawChildren(rawNode, /* includeAssigned = */ true);
    const firstChild = children[0];

    // Bail out if:
    // - more than one child
    // - unique child is not a text node
    // - unique child is a text node, but is too long to be inlined
    // - we are a slot -> these are always represented on their own lines with
    //                    a link to the original node.
    // - we are a flex item -> these are always shown on their own lines so they can be
    //                         selected by the flexbox inspector.
    const isAssignedToSlot =
      firstChild &&
      rawNode.nodeName === "SLOT" &&
      isDirectShadowHostChild(firstChild);

    const isFlexItem = !!firstChild?.parentFlexElement;

    if (
      !firstChild ||
      children.length > 1 ||
      firstChild.nodeType !== Node.TEXT_NODE ||
      firstChild.nodeValue.length > gValueSummaryLength ||
      isAssignedToSlot ||
      isFlexItem
    ) {
      return undefined;
    }

    return this._getOrCreateNodeActor(firstChild);
  }

  /**
   * Mark a node as 'retained'.
   *
   * A retained node is not released when `releaseNode` is called on its
   * parent, or when a parent is released with the `cleanup` option to
   * `getMutations`.
   *
   * When a retained node's parent is released, a retained mode is added to
   * the walker's "retained orphans" list.
   *
   * Retained nodes can be deleted by providing the `force` option to
   * `releaseNode`.  They will also be released when their document
   * has been destroyed.
   *
   * Retaining a node makes no promise about its children;  They can
   * still be removed by normal means.
   */
  retainNode(node) {
    node.retained = true;
  }

  /**
   * Remove the 'retained' mark from a node.  If the node was a
   * retained orphan, release it.
   */
  unretainNode(node) {
    node.retained = false;
    if (this._retainedOrphans.has(node)) {
      this._retainedOrphans.delete(node);
      this.releaseNode(node);
    }
  }

  /**
   * Release actors for a node and all child nodes.
   */
  releaseNode(node, options = {}) {
    if (isNodeDead(node)) {
      return;
    }

    if (node.retained && !options.force) {
      this._retainedOrphans.add(node);
      return;
    }

    if (node.retained) {
      // Forcing a retained node to go away.
      this._retainedOrphans.delete(node);
    }

    for (const child of this._rawChildren(node.rawNode)) {
      const childActor = this.getNode(child);
      if (childActor) {
        this.releaseNode(childActor, options);
      }
    }

    node.destroy();
  }

  /**
   * Add any nodes between `node` and the walker's root node that have not
   * yet been seen by the client.
   */
  ensurePathToRoot(node, newParents = new Set()) {
    if (!node) {
      return newParents;
    }
    let parent = this.rawParentNode(node);
    while (parent) {
      let parentActor = this.getNode(parent);
      if (parentActor) {
        // This parent did exist, so the client knows about it.
        return newParents;
      }
      // This parent didn't exist, so hasn't been seen by the client yet.
      parentActor = this._getOrCreateNodeActor(parent);
      newParents.add(parentActor);
      parent = this.rawParentNode(parentActor);
    }
    return newParents;
  }

  /**
   * Return the number of children under the provided NodeActor.
   *
   * @param NodeActor node
   *    See JSDoc for children()
   * @param object options
   *    See JSDoc for children()
   * @return Number the number of children
   */
  countChildren(node, options = {}) {
    return this._getChildren(node, options).nodes.length;
  }

  /**
   * Return children of the given node.  By default this method will return
   * all children of the node, but there are options that can restrict this
   * to a more manageable subset.
   *
   * @param NodeActor node
   *    The node whose children you're curious about.
   * @param object options
   *    Named options:
   *    `maxNodes`: The set of nodes returned by the method will be no longer
   *       than maxNodes.
   *    `start`: If a node is specified, the list of nodes will start
   *       with the given child.  Mutally exclusive with `center`.
   *    `center`: If a node is specified, the given node will be as centered
   *       as possible in the list, given how close to the ends of the child
   *       list it is.  Mutually exclusive with `start`.
   *
   * @returns an object with three items:
   *    hasFirst: true if the first child of the node is included in the list.
   *    hasLast: true if the last child of the node is included in the list.
   *    nodes: Array of NodeActor representing the nodes returned by the request.
   */
  children(node, options = {}) {
    const { hasFirst, hasLast, nodes } = this._getChildren(node, options);
    return {
      hasFirst,
      hasLast,
      nodes: nodes.map(n => this._getOrCreateNodeActor(n)),
    };
  }

  /**
   * Returns the raw children of the DOM node, with anonymous content filtered as needed
   *
   * @param Node rawNode.
   * @param boolean includeAssigned
   *   Whether <slot> assigned children should be returned. See
   *   HTMLSlotElement.assignedNodes().
   * @returns Array<Node> the list of children.
   */
  _rawChildren(rawNode, includeAssigned) {
    const ret = [];
    const children = InspectorUtils.getChildrenForNode(
      rawNode,
      /* anonymous = */ true,
      includeAssigned
    );
    for (const child of children) {
      const filterResult = this.documentWalkerFilter(child);
      if (filterResult == nodeFilterConstants.FILTER_ACCEPT) {
        ret.push(child);
      } else if (filterResult == nodeFilterConstants.FILTER_ACCEPT_CHILDREN) {
        // In some cases, we want to completly ignore a node, and display its children
        // instead (e.g. for `<div type="::-moz-snapshot-containing-block">`,
        // we don't want it displayed in the markup view,
        // but we do want to have its `::view-transition` child)
        ret.push(...this._rawChildren(child, includeAssigned));
      }
    }
    return ret;
  }

  /**
   * Return chidlren of the given node. Contrary to children children(), this method only
   * returns DOMNodes. Therefore it will not create NodeActor wrappers and will not
   * update the nodeActors map for the discovered nodes either. This makes this method
   * safe to call when you are not sure if the discovered nodes will be communicated to
   * the client.
   *
   * @param NodeActor node
   *    See JSDoc for children()
   * @param object options
   *    See JSDoc for children()
   * @return  an object with three items:
   *    hasFirst: true if the first child of the node is included in the list.
   *    hasLast: true if the last child of the node is included in the list.
   *    nodes: Array of DOMNodes.
   */
  // eslint-disable-next-line complexity
  _getChildren(node, options = {}) {
    if (isNodeDead(node) || isFrameBlockedByCSP(node.rawNode)) {
      return { hasFirst: true, hasLast: true, nodes: [] };
    }

    if (options.center && options.start) {
      throw Error("Can't specify both 'center' and 'start' options.");
    }

    let maxNodes = options.maxNodes || -1;
    if (maxNodes == -1) {
      maxNodes = Number.MAX_VALUE;
    }

    let nodes = this._rawChildren(node.rawNode, /* includeAssigned = */ true);
    let hasFirst = true;
    let hasLast = true;
    if (nodes.length > maxNodes) {
      let startIndex;
      if (options.center) {
        const centerIndex = nodes.indexOf(options.center.rawNode);
        const backwardCount = Math.floor(maxNodes / 2);
        // If centering would hit the end, just read the last maxNodes nodes.
        if (centerIndex - backwardCount + maxNodes >= nodes.length) {
          startIndex = nodes.length - maxNodes;
        } else {
          startIndex = Math.max(0, centerIndex - backwardCount);
        }
      } else if (options.start) {
        startIndex = Math.max(0, nodes.indexOf(options.start.rawNode));
      } else {
        startIndex = 0;
      }
      const endIndex = Math.min(startIndex + maxNodes, nodes.length);
      hasFirst = startIndex == 0;
      hasLast = endIndex >= nodes.length;
      nodes = nodes.slice(startIndex, endIndex);
    }

    return { hasFirst, hasLast, nodes };
  }

  /**
   * Get the next sibling of a given node.  Getting nodes one at a time
   * might be inefficient, be careful.
   */
  nextSibling(node) {
    if (isNodeDead(node)) {
      return null;
    }

    const walker = this.getDocumentWalker(node.rawNode);
    const sibling = walker.nextSibling();
    return sibling ? this._getOrCreateNodeActor(sibling) : null;
  }

  /**
   * Get the previous sibling of a given node.  Getting nodes one at a time
   * might be inefficient, be careful.
   */
  previousSibling(node) {
    if (isNodeDead(node)) {
      return null;
    }

    const walker = this.getDocumentWalker(node.rawNode);
    const sibling = walker.previousSibling();
    return sibling ? this._getOrCreateNodeActor(sibling) : null;
  }

  /**
   * Helper function for the `children` method: Read forward in the sibling
   * list into an array with `count` items, including the current node.
   */
  _readForward(walker, count) {
    const ret = [];

    let node = walker.currentNode;
    do {
      if (!walker.isSkippedNode(node)) {
        // The walker can be on a node that would be filtered out if it didn't find any
        // other node to fallback to.
        ret.push(node);
      }
      node = walker.nextSibling();
    } while (node && --count);
    return ret;
  }

  /**
   * Return the first node in the document that matches the given selector.
   * See https://developer.mozilla.org/en-US/docs/Web/API/Element.querySelector
   *
   * @param NodeActor baseNode
   * @param string selector
   */
  querySelector(baseNode, selector) {
    if (isNodeDead(baseNode)) {
      return {};
    }

    const node = baseNode.rawNode.querySelector(selector);
    if (!node) {
      return {};
    }

    return this.attachElement(node);
  }

  /**
   * Return a NodeListActor with all nodes that match the given selector.
   * See https://developer.mozilla.org/en-US/docs/Web/API/Element.querySelectorAll
   *
   * @param NodeActor baseNode
   * @param string selector
   */
  querySelectorAll(baseNode, selector) {
    let nodeList = null;

    try {
      nodeList = baseNode.rawNode.querySelectorAll(selector);
    } catch (e) {
      // Bad selector. Do nothing as the selector can come from a searchbox.
    }

    return new NodeListActor(this, nodeList);
  }

  /**
   * Return the node in the baseNode rootNode matching the passed id referenced in a
   * idref/idreflist attribute, as those are scoped within a shadow root.
   *
   * @param NodeActor baseNode
   * @param string id
   */
  getIdrefNode(baseNode, id) {
    if (isNodeDead(baseNode)) {
      return {};
    }

    // Get the document or the shadow root for baseNode
    const rootNode = baseNode.rawNode.getRootNode({ composed: false });
    if (!rootNode) {
      return {};
    }

    const node = rootNode.getElementById(id);
    if (!node) {
      return {};
    }

    return this.attachElement(node);
  }

  /**
   * Get a list of nodes that match the given selector in all known frames of
   * the current content page.
   *
   * @param {string} selector.
   * @return {Array}
   */
  _multiFrameQuerySelectorAll(selector) {
    let nodes = [];

    for (const { document } of this.targetActor.windows) {
      try {
        nodes = [...nodes, ...document.querySelectorAll(selector)];
      } catch (e) {
        // Bad selector. Do nothing as the selector can come from a searchbox.
      }
    }

    return nodes;
  }

  /**
   * Get a list of nodes that match the given XPath in all known frames of
   * the current content page.
   *
   * @param {string} xPath.
   * @return {Array}
   */
  _multiFrameXPath(xPath) {
    const nodes = [];

    for (const window of this.targetActor.windows) {
      const document = window.document;
      try {
        const result = document.evaluate(
          xPath,
          document.documentElement,
          null,
          window.XPathResult.ORDERED_NODE_SNAPSHOT_TYPE,
          null
        );

        for (let i = 0; i < result.snapshotLength; i++) {
          nodes.push(result.snapshotItem(i));
        }
      } catch (e) {
        // Bad XPath. Do nothing as the XPath can come from a searchbox.
      }
    }

    return nodes;
  }

  /**
   * Return a NodeListActor with all nodes that match the given XPath in all
   * frames of the current content page.
   *
   * @param {string} xPath
   */
  multiFrameXPath(xPath) {
    return new NodeListActor(this, this._multiFrameXPath(xPath));
  }

  /**
   * Search the document for a given string.
   * Results will be searched with the walker-search module (searches through
   * tag names, attribute names and values, and text contents).
   *
   * @returns {searchresult}
   *            - {NodeList} list
   *            - {Array<Object>} metadata. Extra information with indices that
   *                              match up with node list.
   */
  search(query) {
    const results = this.walkerSearch.search(query);

    // For now, only return each node once, since the frontend doesn't have a way to
    // highlight each result individually (This would change if we fix Bug 1976634).
    const seenNodes = new Set();
    for (const { node } of results) {
      const isInlinedTextNode =
        node.nodeType === Node.TEXT_NODE &&
        node.parentElement &&
        this.inlineTextChild(node.parentElement);

      // If this is a text node that will be inlined in its parent element, let's directly
      // return the parent element already so we don't get multiple results for the same
      // Markup view tree node.
      seenNodes.add(isInlinedTextNode ? node.parentElement : node);
    }

    const nodeList = new NodeListActor(this, Array.from(seenNodes));

    return {
      list: nodeList,
      metadata: [],
    };
  }

  /**
   * Returns a list of matching results for CSS selector autocompletion.
   *
   * @param string query
   *        The selector query being completed
   * @param string completing
   *        The exact token being completed out of the query
   * @param string selectorState
   *        One of "pseudo", "id", "tag", "class", "null"
   */
  // eslint-disable-next-line complexity
  getSuggestionsForQuery(query, completing, selectorState) {
    const sugs = {
      classes: new Set(),
      tags: new Set(),
      ids: new Set(),
    };
    let result = [];
    let nodes = null;
    // Filtering and sorting the results so that protocol transfer is minimal.
    switch (selectorState) {
      case "pseudo": {
        const colonPrefixedCompleting = ":" + completing;
        for (const pseudo of PSEUDO_SELECTORS) {
          if (pseudo.startsWith(colonPrefixedCompleting)) {
            result.push([pseudo]);
          }
        }
        break;
      }

      case "class":
        if (!query) {
          nodes = this._multiFrameQuerySelectorAll("[class]");
        } else {
          nodes = this._multiFrameQuerySelectorAll(query);
        }
        for (const node of nodes) {
          for (const className of node.classList) {
            sugs.classes.add(className);
          }
        }
        sugs.classes.delete("");
        sugs.classes.delete(HIDDEN_CLASS);
        for (const className of sugs.classes) {
          if (className.startsWith(completing)) {
            result.push(["." + CSS.escape(className), selectorState]);
          }
        }
        break;

      case "id":
        if (!query) {
          nodes = this._multiFrameQuerySelectorAll("[id]");
        } else {
          nodes = this._multiFrameQuerySelectorAll(query);
        }
        for (const node of nodes) {
          sugs.ids.add(node.id);
        }
        for (const id of sugs.ids) {
          if (id.startsWith(completing) && id !== "") {
            result.push(["#" + CSS.escape(id), selectorState]);
          }
        }
        break;

      case "tag":
        if (!query) {
          nodes = this._multiFrameQuerySelectorAll("*");
        } else {
          nodes = this._multiFrameQuerySelectorAll(query);
        }
        for (const node of nodes) {
          const tag = node.localName;
          sugs.tags.add(tag);
        }
        for (const tag of sugs.tags) {
          if (new RegExp("^" + completing + ".*", "i").test(tag)) {
            result.push([tag, selectorState]);
          }
        }

        // For state 'tag' (no preceding # or .) and when there's no query (i.e.
        // only one word) then search for the matching classes and ids
        if (!query) {
          result = [
            ...result,
            ...this.getSuggestionsForQuery(null, completing, "class")
              .suggestions,
            ...this.getSuggestionsForQuery(null, completing, "id").suggestions,
          ];
        }

        break;

      case "null":
        nodes = this._multiFrameQuerySelectorAll(query);
        for (const node of nodes) {
          sugs.ids.add(node.id);
          const tag = node.localName;
          sugs.tags.add(tag);
          for (const className of node.classList) {
            sugs.classes.add(className);
          }
        }
        for (const tag of sugs.tags) {
          tag && result.push([tag]);
        }
        for (const id of sugs.ids) {
          id && result.push(["#" + id]);
        }
        sugs.classes.delete("");
        sugs.classes.delete(HIDDEN_CLASS);
        for (const className of sugs.classes) {
          className && result.push(["." + className]);
        }
    }

    // Sort by type (id, class, tag) and name (asc)
    result = result.sort((a, b) => {
      // Prefixing ids, classes and tags, to group results
      const firstA = a[0].substring(0, 1);
      const firstB = b[0].substring(0, 1);

      const getSortKeyPrefix = firstLetter => {
        if (firstLetter === "#") {
          return "2";
        }
        if (firstLetter === ".") {
          return "1";
        }
        return "0";
      };

      const sortA = getSortKeyPrefix(firstA) + a[0];
      const sortB = getSortKeyPrefix(firstB) + b[0];

      // String compare
      return sortA.localeCompare(sortB);
    });

    result = result.slice(0, 25);

    return {
      query,
      suggestions: result,
    };
  }

  /**
   * Add a pseudo-class lock to a node.
   *
   * @param NodeActor node
   * @param string pseudo
   *    A pseudoclass: ':hover', ':active', ':focus', ':focus-within'
   * @param options
   *    Options object:
   *    `parents`: True if the pseudo-class should be added
   *      to parent nodes.
   *    `enabled`: False if the pseudo-class should be locked
   *      to 'off'. Defaults to true.
   *
   * @returns An empty packet.  A "pseudoClassLock" mutation will
   *    be queued for any changed nodes.
   */
  addPseudoClassLock(node, pseudo, options = {}) {
    if (isNodeDead(node)) {
      return;
    }

    // There can be only one node locked per pseudo, so dismiss all existing
    // ones
    for (const locked of this._activePseudoClassLocks) {
      if (InspectorUtils.hasPseudoClassLock(locked.rawNode, pseudo)) {
        this._removePseudoClassLock(locked, pseudo);
      }
    }

    const enabled = options.enabled === undefined || options.enabled;
    this._addPseudoClassLock(node, pseudo, enabled);

    if (!options.parents) {
      return;
    }

    const walker = this.getDocumentWalker(node.rawNode);
    let cur;
    while ((cur = walker.parentNode())) {
      const curNode = this._getOrCreateNodeActor(cur);
      this._addPseudoClassLock(curNode, pseudo, enabled);
    }
  }

  _queuePseudoClassMutation(node) {
    this.queueMutation({
      target: node.actorID,
      type: "pseudoClassLock",
      pseudoClassLocks: node.writePseudoClassLocks(),
    });
  }

  _addPseudoClassLock(node, pseudo, enabled) {
    if (node.rawNode.nodeType !== Node.ELEMENT_NODE) {
      return false;
    }
    InspectorUtils.addPseudoClassLock(node.rawNode, pseudo, enabled);
    this._activePseudoClassLocks.add(node);
    this._queuePseudoClassMutation(node);
    return true;
  }

  hideNode(node) {
    if (isNodeDead(node)) {
      return;
    }

    loadSheet(node.rawNode.documentGlobal, HELPER_SHEET);
    node.rawNode.classList.add(HIDDEN_CLASS);
  }

  unhideNode(node) {
    if (isNodeDead(node)) {
      return;
    }

    node.rawNode.classList.remove(HIDDEN_CLASS);
  }

  /**
   * Remove a pseudo-class lock from a node.
   *
   * @param NodeActor node
   * @param string pseudo
   *    A pseudoclass: ':hover', ':active', ':focus', ':focus-within'
   * @param options
   *    Options object:
   *    `parents`: True if the pseudo-class should be removed
   *      from parent nodes.
   *
   * @returns An empty response.  "pseudoClassLock" mutations
   *    will be emitted for any changed nodes.
   */
  removePseudoClassLock(node, pseudo, options = {}) {
    if (isNodeDead(node)) {
      return;
    }

    this._removePseudoClassLock(node, pseudo);

    // Remove pseudo class for children as we don't want to allow
    // turning it on for some childs without setting it on some parents
    for (const locked of this._activePseudoClassLocks) {
      if (
        node.rawNode.contains(locked.rawNode) &&
        InspectorUtils.hasPseudoClassLock(locked.rawNode, pseudo)
      ) {
        this._removePseudoClassLock(locked, pseudo);
      }
    }

    if (!options.parents) {
      return;
    }

    const walker = this.getDocumentWalker(node.rawNode);
    let cur;
    while ((cur = walker.parentNode())) {
      const curNode = this._getOrCreateNodeActor(cur);
      this._removePseudoClassLock(curNode, pseudo);
    }
  }

  _removePseudoClassLock(node, pseudo) {
    if (node.rawNode.nodeType != Node.ELEMENT_NODE) {
      return false;
    }
    InspectorUtils.removePseudoClassLock(node.rawNode, pseudo);
    if (!node.writePseudoClassLocks()) {
      this._activePseudoClassLocks.delete(node);
    }

    this._queuePseudoClassMutation(node);
    return true;
  }

  /**
   * Clear all the pseudo-classes on a given node or all nodes.
   *
   * @param {NodeActor} node Optional node to clear pseudo-classes on
   */
  clearPseudoClassLocks(node) {
    if (node && isNodeDead(node)) {
      return;
    }

    if (node) {
      InspectorUtils.clearPseudoClassLocks(node.rawNode);
      this._activePseudoClassLocks.delete(node);
      this._queuePseudoClassMutation(node);
    } else {
      for (const locked of this._activePseudoClassLocks) {
        InspectorUtils.clearPseudoClassLocks(locked.rawNode);
        this._activePseudoClassLocks.delete(locked);
        this._queuePseudoClassMutation(locked);
      }
    }
  }

  /**
   * Get a node's innerHTML property.
   */
  innerHTML(node) {
    let html = "";
    if (!isNodeDead(node)) {
      html = node.rawNode.innerHTML;
    }
    return new LongStringActor(this.conn, html);
  }

  /**
   * Set a node's innerHTML property.
   *
   * @param {NodeActor} node The node.
   * @param {string} value The piece of HTML content.
   */
  setInnerHTML(node, value) {
    if (isNodeDead(node)) {
      return;
    }

    const rawNode = node.rawNode;
    if (
      rawNode.nodeType !== rawNode.ownerDocument.ELEMENT_NODE &&
      rawNode.nodeType !== rawNode.ownerDocument.DOCUMENT_FRAGMENT_NODE
    ) {
      throw new Error("Can only change innerHTML to element or fragment nodes");
    }
    // eslint-disable-next-line no-unsanitized/property
    rawNode.innerHTML = value;
  }

  /**
   * Get a node's outerHTML property.
   *
   * @param {NodeActor} node The node.
   */
  outerHTML(node) {
    let outerHTML = "";
    if (!isNodeDead(node)) {
      outerHTML = node.rawNode.outerHTML;
    }
    return new LongStringActor(this.conn, outerHTML);
  }

  /**
   * Set a node's outerHTML property.
   *
   * @param {NodeActor} node The node.
   * @param {string} value The piece of HTML content.
   */
  setOuterHTML(node, value) {
    if (isNodeDead(node)) {
      return;
    }

    const rawNode = node.rawNode;
    const doc = nodeDocument(rawNode);
    const win = doc.defaultView;
    let parser;
    if (!win) {
      throw new Error("The window object shouldn't be null");
    } else {
      // We create DOMParser under window object because we want a content
      // DOMParser, which means all the DOM objects created by this DOMParser
      // will be in the same DocGroup as rawNode.parentNode. Then the newly
      // created nodes can be adopted into rawNode.parentNode.
      parser = new win.DOMParser();
    }

    const mimeType = rawNode.tagName === "svg" ? "image/svg+xml" : "text/html";
    const parsedDOM = parser.parseFromString(value, mimeType);
    const parentNode = rawNode.parentNode;

    // Special case for head and body.  Setting document.body.outerHTML
    // creates an extra <head> tag, and document.head.outerHTML creates
    // an extra <body>.  So instead we will call replaceChild with the
    // parsed DOM, assuming that they aren't trying to set both tags at once.
    if (rawNode.tagName === "BODY") {
      if (parsedDOM.head.innerHTML === "") {
        parentNode.replaceChild(parsedDOM.body, rawNode);
      } else {
        // eslint-disable-next-line no-unsanitized/property
        rawNode.outerHTML = value;
      }
    } else if (rawNode.tagName === "HEAD") {
      if (parsedDOM.body.innerHTML === "") {
        parentNode.replaceChild(parsedDOM.head, rawNode);
      } else {
        // eslint-disable-next-line no-unsanitized/property
        rawNode.outerHTML = value;
      }
    } else if (node.isDocumentElement()) {
      // Unable to set outerHTML on the document element. Fall back by
      // setting attributes manually. Then replace all the child nodes.
      const finalAttributeModifications = [];
      const attributeModifications = {};
      for (const attribute of rawNode.attributes) {
        attributeModifications[attribute.name] = null;
      }
      for (const attribute of parsedDOM.documentElement.attributes) {
        attributeModifications[attribute.name] = attribute.value;
      }
      for (const key in attributeModifications) {
        finalAttributeModifications.push({
          attributeName: key,
          newValue: attributeModifications[key],
        });
      }
      node.modifyAttributes(finalAttributeModifications);

      rawNode.replaceChildren(...parsedDOM.firstElementChild.childNodes);
    } else {
      // eslint-disable-next-line no-unsanitized/property
      rawNode.outerHTML = value;
    }
  }

  /**
   * Insert adjacent HTML to a node.
   *
   * @param {Node} node
   * @param {string} position One of "beforeBegin", "afterBegin", "beforeEnd",
   *                          "afterEnd" (see Element.insertAdjacentHTML).
   * @param {string} value The HTML content.
   */
  insertAdjacentHTML(node, position, value) {
    if (isNodeDead(node)) {
      return { node: [], newParents: [] };
    }

    const rawNode = node.rawNode;
    const isInsertAsSibling =
      position === "beforeBegin" || position === "afterEnd";

    // Don't insert anything adjacent to the document element.
    if (isInsertAsSibling && node.isDocumentElement()) {
      throw new Error("Can't insert adjacent element to the root.");
    }

    const rawParentNode = rawNode.parentNode;
    if (!rawParentNode && isInsertAsSibling) {
      throw new Error("Can't insert as sibling without parent node.");
    }

    // We can't use insertAdjacentHTML, because we want to return the nodes
    // being created (so the front can remove them if the user undoes
    // the change). So instead, use Range.createContextualFragment().
    const range = rawNode.ownerDocument.createRange();
    if (position === "beforeBegin" || position === "afterEnd") {
      range.selectNode(rawNode);
    } else {
      range.selectNodeContents(rawNode);
    }
    // eslint-disable-next-line no-unsanitized/method
    const docFrag = range.createContextualFragment(value);
    const newRawNodes = Array.from(docFrag.childNodes);
    switch (position) {
      case "beforeBegin":
        rawParentNode.insertBefore(docFrag, rawNode);
        break;
      case "afterEnd":
        // Note: if the second argument is null, rawParentNode.insertBefore
        // behaves like rawParentNode.appendChild.
        rawParentNode.insertBefore(docFrag, rawNode.nextSibling);
        break;
      case "afterBegin":
        rawNode.insertBefore(docFrag, rawNode.firstChild);
        break;
      case "beforeEnd":
        rawNode.appendChild(docFrag);
        break;
      default:
        throw new Error(
          "Invalid position value. Must be either " +
            "'beforeBegin', 'beforeEnd', 'afterBegin' or 'afterEnd'."
        );
    }

    return this.attachElements(newRawNodes);
  }

  /**
   * Duplicate a specified node
   *
   * @param {NodeActor} node The node to duplicate.
   */
  duplicateNode({ rawNode }) {
    const clonedNode = rawNode.cloneNode(true);
    rawNode.parentNode.insertBefore(clonedNode, rawNode.nextSibling);
  }

  /**
   * Test whether a node is a document or a document element.
   *
   * @param {NodeActor} node The node to remove.
   * @return {boolean} True if the node is a document or a document element.
   */
  isDocumentOrDocumentElementNode(node) {
    return (
      (node.rawNode.ownerDocument &&
        node.rawNode.ownerDocument.documentElement === this.rawNode) ||
      node.rawNode.nodeType === Node.DOCUMENT_NODE
    );
  }

  /**
   * Removes a node from its parent node.
   *
   * @param {NodeActor} node The node to remove.
   * @returns The node's nextSibling before it was removed.
   */
  removeNode(node) {
    if (isNodeDead(node) || this.isDocumentOrDocumentElementNode(node)) {
      throw Error("Cannot remove document, document elements or dead nodes.");
    }

    const nextSibling = this.nextSibling(node);
    node.rawNode.remove();
    // Mutation events will take care of the rest.
    return nextSibling;
  }

  /**
   * Removes an array of nodes from their parent node.
   *
   * @param {NodeActor[]} nodes The nodes to remove.
   */
  removeNodes(nodes) {
    // Check that all nodes are valid before processing the removals.
    for (const node of nodes) {
      if (isNodeDead(node) || this.isDocumentOrDocumentElementNode(node)) {
        throw Error("Cannot remove document, document elements or dead nodes");
      }
    }

    for (const node of nodes) {
      node.rawNode.remove();
      // Mutation events will take care of the rest.
    }
  }

  /**
   * Insert a node into the DOM.
   */
  insertBefore(node, parent, sibling) {
    if (
      isNodeDead(node) ||
      isNodeDead(parent) ||
      (sibling && isNodeDead(sibling))
    ) {
      return;
    }

    const rawNode = node.rawNode;
    const rawParent = parent.rawNode;
    const rawSibling = sibling ? sibling.rawNode : null;

    // Don't bother inserting a node if the document position isn't going
    // to change. This prevents needless iframes reloading and mutations.
    if (rawNode.parentNode === rawParent) {
      let currentNextSibling = this.nextSibling(node);
      currentNextSibling = currentNextSibling
        ? currentNextSibling.rawNode
        : null;

      if (rawNode === rawSibling || currentNextSibling === rawSibling) {
        return;
      }
    }

    rawParent.insertBefore(rawNode, rawSibling);
  }

  /**
   * Editing a node's tagname actually means creating a new node with the same
   * attributes, removing the node and inserting the new one instead.
   * This method does not return anything as mutation events are taking care of
   * informing the consumers about changes.
   */
  editTagName(node, tagName) {
    if (isNodeDead(node)) {
      return null;
    }

    const oldNode = node.rawNode;

    // Create a new element with the same attributes as the current element and
    // prepare to replace the current node with it.
    let newNode;
    try {
      newNode = nodeDocument(oldNode).createElement(tagName);
    } catch (x) {
      // Failed to create a new element with that tag name, ignore the change,
      // and signal the error to the front.
      return Promise.reject(
        new Error("Could not change node's tagName to " + tagName)
      );
    }

    const attrs = oldNode.attributes;
    for (let i = 0; i < attrs.length; i++) {
      newNode.setAttribute(attrs[i].name, attrs[i].value);
    }

    // Insert the new node, and transfer the old node's children.
    oldNode.parentNode.insertBefore(newNode, oldNode);
    while (oldNode.firstChild) {
      newNode.appendChild(oldNode.firstChild);
    }

    oldNode.remove();
    return null;
  }

  /**
   * Gets the state of the mutation breakpoint types for this actor.
   *
   * @param {NodeActor} node The node to get breakpoint info for.
   */
  getMutationBreakpoints(node) {
    let bps;
    if (!isNodeDead(node)) {
      bps = this._breakpointInfoForNode(node.rawNode);
    }

    return (
      bps || {
        subtree: false,
        removal: false,
        attribute: false,
      }
    );
  }

  /**
   * Set the state of some subset of mutation breakpoint types for this actor.
   *
   * @param {NodeActor} node The node to set breakpoint info for.
   * @param {object} bps A subset of the breakpoints for this actor that
   *                            should be updated to new states.
   */
  setMutationBreakpoints(node, bps) {
    if (isNodeDead(node)) {
      return;
    }
    const rawNode = node.rawNode;

    if (
      rawNode.ownerDocument &&
      rawNode.getRootNode({ composed: true }) != rawNode.ownerDocument
    ) {
      // We only allow watching for mutations on nodes that are attached to
      // documents. That allows us to clean up our mutation listeners when all
      // of the watched nodes have been removed from the document.
      return;
    }

    // This argument has nullable fields so we want to only update boolean
    // field values.
    const bpsForNode = Object.keys(bps).reduce((obj, bp) => {
      if (typeof bps[bp] === "boolean") {
        obj[bp] = bps[bp];
      }
      return obj;
    }, {});

    this._updateMutationBreakpointState("api", rawNode, {
      ...this.getMutationBreakpoints(node),
      ...bpsForNode,
    });
  }

  /**
   * Update the mutation breakpoint state for the given DOM node.
   *
   * @param {Node} rawNode The DOM node.
   * @param {object} bpsForNode The state of each mutation bp type we support.
   */
  _updateMutationBreakpointState(mutationReason, rawNode, bpsForNode) {
    const rawDoc = rawNode.ownerDocument || rawNode;

    const docMutationBreakpoints = this._mutationBreakpointsForDoc(
      rawDoc,
      true /* createIfNeeded */
    );
    let originalBpsForNode = this._breakpointInfoForNode(rawNode);

    if (!bpsForNode && !originalBpsForNode) {
      return;
    }

    bpsForNode = bpsForNode || {};
    originalBpsForNode = originalBpsForNode || {};

    if (Object.values(bpsForNode).some(Boolean)) {
      docMutationBreakpoints.nodes.set(rawNode, bpsForNode);
    } else {
      docMutationBreakpoints.nodes.delete(rawNode);
    }
    if (originalBpsForNode.subtree && !bpsForNode.subtree) {
      docMutationBreakpoints.counts.subtree -= 1;
    } else if (!originalBpsForNode.subtree && bpsForNode.subtree) {
      docMutationBreakpoints.counts.subtree += 1;
    }

    if (originalBpsForNode.removal && !bpsForNode.removal) {
      docMutationBreakpoints.counts.removal -= 1;
    } else if (!originalBpsForNode.removal && bpsForNode.removal) {
      docMutationBreakpoints.counts.removal += 1;
    }

    if (originalBpsForNode.attribute && !bpsForNode.attribute) {
      docMutationBreakpoints.counts.attribute -= 1;
    } else if (!originalBpsForNode.attribute && bpsForNode.attribute) {
      docMutationBreakpoints.counts.attribute += 1;
    }

    this._updateDocumentMutationListeners(rawDoc);

    const actor = this.getNode(rawNode);
    if (actor) {
      this.queueMutation({
        target: actor.actorID,
        type: "mutationBreakpoint",
        mutationBreakpoints: this.getMutationBreakpoints(actor),
        mutationReason,
      });
    }
  }

  /**
   * Controls whether this DOM document has event listeners attached for
   * handling of DOM mutation breakpoints.
   *
   * @param {Document} rawDoc The DOM document.
   */
  _updateDocumentMutationListeners(rawDoc) {
    const docMutationBreakpoints = this._mutationBreakpointsForDoc(rawDoc);
    if (!docMutationBreakpoints) {
      rawDoc.devToolsWatchingDOMMutations = false;
      return;
    }

    const anyBreakpoint =
      docMutationBreakpoints.counts.subtree > 0 ||
      docMutationBreakpoints.counts.removal > 0 ||
      docMutationBreakpoints.counts.attribute > 0;

    rawDoc.devToolsWatchingDOMMutations = anyBreakpoint;

    if (docMutationBreakpoints.counts.subtree > 0) {
      this.chromeEventHandler.addEventListener(
        "devtoolschildinserted",
        this.onSubtreeModified,
        true /* capture */
      );
    } else {
      this.chromeEventHandler.removeEventListener(
        "devtoolschildinserted",
        this.onSubtreeModified,
        true /* capture */
      );
    }

    if (anyBreakpoint) {
      this.chromeEventHandler.addEventListener(
        "devtoolschildremoved",
        this.onNodeRemoved,
        true /* capture */
      );
    } else {
      this.chromeEventHandler.removeEventListener(
        "devtoolschildremoved",
        this.onNodeRemoved,
        true /* capture */
      );
    }

    if (docMutationBreakpoints.counts.attribute > 0) {
      this.chromeEventHandler.addEventListener(
        "devtoolsattrmodified",
        this.onAttributeModified,
        true /* capture */
      );
    } else {
      this.chromeEventHandler.removeEventListener(
        "devtoolsattrmodified",
        this.onAttributeModified,
        true /* capture */
      );
    }
  }

  _breakOnMutation(mutationType, targetNode, ancestorNode, action) {
    this.targetActor.threadActor.pauseForMutationBreakpoint(
      mutationType,
      targetNode,
      ancestorNode,
      action
    );
  }

  _mutationBreakpointsForDoc(rawDoc, createIfNeeded = false) {
    let docMutationBreakpoints = this._mutationBreakpoints.get(rawDoc);
    if (!docMutationBreakpoints && createIfNeeded) {
      docMutationBreakpoints = {
        counts: {
          subtree: 0,
          removal: 0,
          attribute: 0,
        },
        nodes: new Map(),
      };
      this._mutationBreakpoints.set(rawDoc, docMutationBreakpoints);
    }
    return docMutationBreakpoints;
  }

  _breakpointInfoForNode(target) {
    const docMutationBreakpoints = this._mutationBreakpointsForDoc(
      target.ownerDocument || target
    );
    return (
      (docMutationBreakpoints && docMutationBreakpoints.nodes.get(target)) ||
      null
    );
  }

  onNodeRemoved(evt) {
    const mutationBpInfo = this._breakpointInfoForNode(evt.target);
    const hasNodeRemovalEvent = mutationBpInfo?.removal;

    this._clearMutationBreakpointsFromSubtree(evt.target);
    if (hasNodeRemovalEvent) {
      this._breakOnMutation("nodeRemoved", evt.target);
    } else {
      this.onSubtreeModified(evt);
    }
  }

  onAttributeModified(evt) {
    const mutationBpInfo = this._breakpointInfoForNode(evt.target);
    if (mutationBpInfo?.attribute) {
      this._breakOnMutation("attributeModified", evt.target);
    }
  }

  onSubtreeModified(evt) {
    const action = evt.type === "devtoolschildinserted" ? "add" : "remove";
    let node = evt.target;
    if (node.isNativeAnonymous && !this.showAllAnonymousContent) {
      return;
    }
    while ((node = node.parentNode) !== null) {
      const mutationBpInfo = this._breakpointInfoForNode(node);
      if (mutationBpInfo?.subtree) {
        this._breakOnMutation("subtreeModified", evt.target, node, action);
        break;
      }
    }
  }

  _clearMutationBreakpointsFromSubtree(targetNode) {
    const targetDoc = targetNode.ownerDocument || targetNode;
    const docMutationBreakpoints = this._mutationBreakpointsForDoc(targetDoc);
    if (!docMutationBreakpoints || docMutationBreakpoints.nodes.size === 0) {
      // Bail early for performance. If the doc has no mutation BPs, there is
      // no reason to iterate through the children looking for things to detach.
      return;
    }

    // The walker is not limited to the subtree of the argument node, so we
    // need to ensure that we stop walking when we leave the subtree.
    const nextWalkerSibling = this._getNextTraversalSibling(targetNode);

    const walker = new DocumentWalker(targetNode, this.rootWin, {
      filter: this.noAnonymousContentDocumentWalkerFilter,
      skipTo: SKIP_TO_SIBLING,
    });

    do {
      this._updateMutationBreakpointState("detach", walker.currentNode, null);
    } while (walker.nextNode() && walker.currentNode !== nextWalkerSibling);
  }

  _getNextTraversalSibling(targetNode) {
    const walker = new DocumentWalker(targetNode, this.rootWin, {
      filter: this.noAnonymousContentDocumentWalkerFilter,
      skipTo: SKIP_TO_SIBLING,
    });

    while (!walker.nextSibling()) {
      if (!walker.parentNode()) {
        // If we try to step past the walker root, there is no next sibling.
        return null;
      }
    }
    return walker.currentNode;
  }

  /**
   * Get any pending mutation records.  Must be called by the client after
   * the `new-mutations` notification is received.  Returns an array of
   * mutation records.
   *
   * Mutation records have a basic structure:
   *
   * {
   *   type: attributes|characterData|childList,
   *   target: <domnode actor ID>,
   * }
   *
   * And additional attributes based on the mutation type:
   *
   * `attributes` type:
   *   attributeName: <string> - the attribute that changed
   *   attributeNamespace: <string> - the attribute's namespace URI, if any.
   *   newValue: <string> - The new value of the attribute, if any.
   *
   * `characterData` type:
   *   newValue: <string> - the new nodeValue for the node
   *
   * `childList` type is returned when the set of children for a node
   * has changed.  Includes extra data, which can be used by the client to
   * maintain its ownership subtree.
   *
   *   added: array of <domnode actor ID> - The list of actors *previously
   *     seen by the client* that were added to the target node.
   *   removed: array of <domnode actor ID> The list of actors *previously
   *     seen by the client* that were removed from the target node.
   *   inlineTextChild: If the node now has a single text child, it will
   *     be sent here.
   *
   * Actors that are included in a MutationRecord's `removed` but
   * not in an `added` have been removed from the client's ownership
   * tree (either by being moved under a node the client has seen yet
   * or by being removed from the tree entirely), and is considered
   * 'orphaned'.
   *
   * Keep in mind that if a node that the client hasn't seen is moved
   * into or out of the target node, it will not be included in the
   * removedNodes and addedNodes list, so if the client is interested
   * in the new set of children it needs to issue a `children` request.
   */
  getMutations(options = {}) {
    const pending = this._pendingMutations || [];
    this._pendingMutations = [];
    this._waitingForGetMutations = false;

    if (options.cleanup) {
      for (const node of this._orphaned) {
        // Release the orphaned node.  Nodes or children that have been
        // retained will be moved to this._retainedOrphans.
        this.releaseNode(node);
      }
      this._orphaned = new Set();
    }

    return pending;
  }

  queueMutation(mutation) {
    if (!this.actorID || this._destroyed) {
      // We've been destroyed, don't bother queueing this mutation.
      return;
    }

    // Add the mutation to the list of mutations to be retrieved next.
    this._pendingMutations.push(mutation);

    // Bail out if we already emitted a new-mutations event and are waiting for a client
    // to retrieve them.
    if (this._waitingForGetMutations) {
      return;
    }

    if (IMMEDIATE_MUTATIONS.includes(mutation.type)) {
      this._emitNewMutations();
    } else {
      /**
       * If many mutations are fired at the same time, clients might sequentially request
       * children/siblings for updated nodes, which can be costly. By throttling the calls
       * to getMutations, duplicated mutations will be ignored.
       */
      this._throttledEmitNewMutations();
    }
  }

  _emitNewMutations() {
    if (!this.actorID || this._destroyed) {
      // Bail out if the actor was destroyed after throttling this call.
      return;
    }

    if (this._waitingForGetMutations || !this._pendingMutations.length) {
      // Bail out if we already fired the new-mutation event or if no mutations are
      // waiting to be retrieved.
      return;
    }

    this._waitingForGetMutations = true;
    this.emit("new-mutations");
  }

  /**
   * Handles mutations from the DOM mutation observer API.
   *
   * @param array[MutationRecord] mutations
   *    See https://developer.mozilla.org/en-US/docs/Web/API/MutationObserver#MutationRecord
   */
  onMutations(mutations) {
    // Don't send a mutation event if the mutation target would be ignored by the walker
    // filter function.
    if (
      mutations.every(
        mutation =>
          this.documentWalkerFilter(mutation.target) ===
          nodeFilterConstants.FILTER_SKIP
      )
    ) {
      return;
    }

    // Notify any observers that want *all* mutations (even on nodes that aren't
    // referenced).  This is not sent over the protocol so can only be used by
    // scripts running in the server process.
    this.emit("any-mutation");

    for (const change of mutations) {
      const targetActor = this.getNode(change.target);
      if (!targetActor) {
        continue;
      }
      const targetNode = change.target;
      const type = change.type;
      const mutation = {
        type,
        target: targetActor.actorID,
      };

      if (type === "attributes") {
        mutation.attributeName = change.attributeName;
        mutation.attributeNamespace = change.attributeNamespace || undefined;
        mutation.newValue = targetNode.hasAttribute(mutation.attributeName)
          ? targetNode.getAttribute(mutation.attributeName)
          : null;
      } else if (type === "characterData") {
        mutation.newValue = targetNode.nodeValue;
        this._maybeQueueInlineTextChildMutation(change, targetNode);
      } else if (type === "childList") {
        // Get the list of removed and added actors that the client has seen
        // so that it can keep its ownership tree up to date.
        const removedActors = [];
        const addedActors = [];
        for (const removed of change.removedNodes) {
          this._onMutationsNode(removed, removedActors, "removed");
        }
        for (const added of change.addedNodes) {
          this._onMutationsNode(added, addedActors, "added");
        }

        mutation.numChildren = targetActor.numChildren;
        mutation.removed = removedActors;
        mutation.added = addedActors;

        const inlineTextChild = this.inlineTextChild(targetActor.rawNode);
        if (inlineTextChild) {
          mutation.inlineTextChild = inlineTextChild.form();
        }
      }
      this.queueMutation(mutation);
    }
  }

  /**
   * Handle a mutation on a node
   *
   * @param {Element} node
   *        The element that is added/removed in the mutation
   * @param {NodeActor[]} actors
   *        An array that will be populated by this function with the node actors that
   *        were added
   * @param {string} mutationType
   *        The type of mutation we're handlign ("added" or "removed")
   */
  _onMutationsNode(node, actors, mutationType) {
    if (mutationType !== "added" && mutationType !== "removed") {
      console.error("Unknown mutation type", mutationType);
      return;
    }

    const actor = this.getNode(node);
    if (actor) {
      actors.push(actor.actorID);
      if (mutationType === "added") {
        // The actor is reconnected to the ownership tree, unorphan
        // it and let the client know so that its ownership tree is up
        // to date.
        this._orphaned.delete(actor);
        return;
      }
      if (mutationType === "removed") {
        // While removed from the tree, nodes are saved as orphaned.
        this._orphaned.add(actor);
        return;
      }
    }

    // Here, we might be in a case where a node is remove/added for which we don't have an
    // actor for, but do have actors for its children.
    if (
      this.documentWalkerFilter(node) !==
      nodeFilterConstants.FILTER_ACCEPT_CHILDREN
    ) {
      // At this point, the client never encountered this actor and the node wasn't ignored,
      // so we don't need to tell it about this mutation.
      // For added node, if the client wants to see the new nodes it can ask for children.
      return;
    }

    // Otherwise, the node was ignored, so we need to go over its children to find
    // actor references we might have.
    for (const child of this._rawChildren(node)) {
      this._onMutationsNode(child, actors, mutationType);
    }
  }

  /**
   * Check if the provided mutation could change the way the target element is
   * inlined with its parent node. If it might, a custom mutation of type
   * "inlineTextChild" will be queued.
   *
   * @param {MutationRecord} mutation
   *        A characterData type mutation
   */
  _maybeQueueInlineTextChildMutation(mutation) {
    const { oldValue, target } = mutation;
    const newValue = target.nodeValue;
    const limit = gValueSummaryLength;

    if (
      (oldValue.length <= limit && newValue.length <= limit) ||
      (oldValue.length > limit && newValue.length > limit)
    ) {
      // Bail out if the new & old values are both below/above the size limit.
      return;
    }

    const parentActor = this.getNode(target.parentNode);
    if (!parentActor || parentActor.rawNode.children.length) {
      // If the parent node has other children, a character data mutation will
      // not change anything regarding inlining text nodes.
      return;
    }

    const inlineTextChild = this.inlineTextChild(parentActor.rawNode);
    this.queueMutation({
      type: "inlineTextChild",
      target: parentActor.actorID,
      inlineTextChild: inlineTextChild ? inlineTextChild.form() : undefined,
    });
  }

  onSlotchange(event) {
    const target = event.target;
    const targetActor = this.getNode(target);
    if (!targetActor) {
      return;
    }

    this.queueMutation({
      type: "slotchange",
      target: targetActor.actorID,
    });
  }

  /**
   * Fires when an anonymous root is created.
   * This is needed because regular mutation observers don't fire on some kinds
   * of NAC creation. We want to treat this like a regular insertion.
   */
  onAnonymousrootcreated(event) {
    const root = event.target;

    // Don't trigger a mutation if the document walker would filter out the element.
    if (this.documentWalkerFilter(root) === nodeFilterConstants.FILTER_SKIP) {
      return;
    }

    const parent = this.rawParentNode(root);
    if (!parent) {
      // These events are async. The node might have been removed already, in
      // which case there's nothing to do anymore.
      return;
    }
    // By the time onAnonymousrootremoved fires, the node is already detached
    // from its parent, so we need to remember it by hand.
    this._anonParents.set(root, parent);
    this.onMutations([
      {
        type: "childList",
        target: parent,
        addedNodes: [root],
        removedNodes: [],
      },
    ]);
  }

  /**
   * @see onAnonymousrootcreated
   */
  onAnonymousrootremoved(event) {
    const root = event.target;
    let parent = this._anonParents.get(root);

    // We can have cases where the anonymous root was created before we set the
    // onAnonymousrootcreated event listener (e.g. for cross-document view transition).
    // In such case, we won't have the root in `_anonParents`, but we still want to send
    // the mutation.
    if (!parent && isViewTransitionSnapshotContainingBlockNode(root)) {
      // The snapshot containing block is always a child of the document element.
      parent = root.ownerDocument.documentElement;
    }
    if (!parent) {
      return;
    }
    this._anonParents.delete(root);
    this.onMutations([
      {
        type: "childList",
        target: parent,
        addedNodes: [],
        removedNodes: [root],
      },
    ]);
  }

  onShadowrootattached(event) {
    const actor = this.getNode(event.target);
    if (!actor) {
      return;
    }

    const mutation = {
      type: "shadowRootAttached",
      target: actor.actorID,
      numChildren: actor.numChildren,
    };
    this.queueMutation(mutation);
  }

  onFrameLoad({ window, isTopLevel }) {
    // By the time we receive the DOMContentLoaded event, we might have been destroyed
    if (this._destroyed) {
      return;
    }
    const { readyState } = window.document;
    if (readyState != "interactive" && readyState != "complete") {
      // The document is not loaded, so we want to register to fire again when the
      // DOM has been loaded.
      window.addEventListener(
        "DOMContentLoaded",
        this.onFrameLoad.bind(this, { window, isTopLevel }),
        { once: true }
      );
      return;
    }

    window.document.devToolsAnonymousAndShadowEventsEnabled = true;

    if (isTopLevel) {
      // If we initialize the inspector while the document is loading,
      // we may already have a root document set in the constructor.
      if (
        this.rootDoc &&
        this.rootDoc !== window.document &&
        !Cu.isDeadWrapper(this.rootDoc) &&
        this.rootDoc.defaultView
      ) {
        this.onFrameUnload({ window: this.rootDoc.defaultView });
      }
      // Update all DOM objects references to target the new document.
      this.rootWin = window;
      this.rootDoc = window.document;
      this.rootNode = this.document();
      this.emit("root-available", this.rootNode);
    } else {
      const frame = getFrameElement(window);
      const frameActor = this.getNode(frame);
      if (frameActor) {
        // If the parent frame is in the map of known node actors, create the
        // actor for the new document and emit a root-available event.
        const documentActor = this._getOrCreateNodeActor(window.document);
        this.emit("root-available", documentActor);
      }
    }
  }

  // Returns true if domNode is in window or a subframe.
  _childOfWindow(window, domNode) {
    while (domNode) {
      const win = nodeDocument(domNode).defaultView;
      if (win === window) {
        return true;
      }
      domNode = getFrameElement(win);
    }
    return false;
  }

  onFrameUnload({ window }) {
    // Any retained orphans that belong to this document
    // or its children need to be released, and a mutation sent
    // to notify of that.
    const releasedOrphans = [];

    for (const retained of this._retainedOrphans) {
      if (
        Cu.isDeadWrapper(retained.rawNode) ||
        this._childOfWindow(window, retained.rawNode)
      ) {
        this._retainedOrphans.delete(retained);
        releasedOrphans.push(retained.actorID);
        this.releaseNode(retained, { force: true });
      }
    }

    if (releasedOrphans.length) {
      this.queueMutation({
        target: this.rootNode.actorID,
        type: "unretained",
        nodes: releasedOrphans,
      });
    }

    const doc = window.document;
    const documentActor = this.getNode(doc);
    if (!documentActor) {
      return;
    }

    // Removing a frame also removes any mutation breakpoints set on that
    // document so that clients can clear their set of active breakpoints.
    const mutationBps = this._mutationBreakpointsForDoc(doc);
    const nodes = mutationBps ? Array.from(mutationBps.nodes.keys()) : [];
    for (const node of nodes) {
      this._updateMutationBreakpointState("unload", node, null);
    }

    this.emit("root-destroyed", documentActor);

    // Cleanup root doc references if we just unloaded the top level root
    // document.
    if (this.rootDoc === doc) {
      this.rootDoc = null;
      this.rootNode = null;
    }

    // Release the actor for the unloaded document.
    this.releaseNode(documentActor, { force: true });
  }

  /**
   * Check if a node is attached to the DOM tree of the current page.
   *
   * @param {Node} rawNode
   * @return {boolean} false if the node is removed from the tree or within a
   * document fragment
   */
  _isInDOMTree(rawNode) {
    let walker;
    try {
      walker = this.getDocumentWalker(rawNode);
    } catch (e) {
      // The DocumentWalker may throw NS_ERROR_ILLEGAL_VALUE when the node isn't found as a legit children of its parent
      // ex: <iframe> manually added as immediate child of another <iframe>
      if (e.name == "NS_ERROR_ILLEGAL_VALUE") {
        return false;
      }
      throw e;
    }
    let current = walker.currentNode;

    // Reaching the top of tree
    while (walker.parentNode()) {
      current = walker.currentNode;
    }

    // The top of the tree is a fragment or is not rootDoc, hence rawNode isn't
    // attached
    if (
      current.nodeType === Node.DOCUMENT_FRAGMENT_NODE ||
      current !== this.rootDoc
    ) {
      return false;
    }

    // Otherwise the top of the tree is rootDoc, hence rawNode is in rootDoc
    return true;
  }

  /**
   * @see _isInDomTree
   */
  isInDOMTree(node) {
    if (isNodeDead(node)) {
      return false;
    }
    return this._isInDOMTree(node.rawNode);
  }

  /**
   * Given a windowID return the NodeActor for the corresponding frameElement,
   * unless it's the root window
   */
  getNodeActorFromWindowID(windowID) {
    let win;

    try {
      win = Services.wm.getOuterWindowWithId(windowID);
    } catch (e) {
      // ignore
    }

    if (!win) {
      return {
        error: "noWindow",
        message: "The related docshell is destroyed or not found",
      };
    } else if (!win.frameElement) {
      // the frame element of the root document is privileged & thus
      // inaccessible, so return the document body/element instead
      return this.attachElement(
        win.document.body || win.document.documentElement
      );
    }

    return this.attachElement(win.frameElement);
  }

  /**
   * Given a contentDomReference return the NodeActor for the corresponding frameElement.
   */
  getNodeActorFromContentDomReference(contentDomReference) {
    let rawNode = lazy.ContentDOMReference.resolve(contentDomReference);
    if (!rawNode || !this._isInDOMTree(rawNode)) {
      return null;
    }

    // This is a special case for the document object whereby it is considered
    // as document.documentElement (the <html> node)
    if (rawNode.defaultView && rawNode === rawNode.defaultView.document) {
      rawNode = rawNode.documentElement;
    }

    return this.attachElement(rawNode);
  }

  /**
   * Given a StyleSheet resource ID, commonly used in the style-editor, get its
   * ownerNode and return the corresponding walker's NodeActor.
   * Note that getNodeFromActor was added later and can now be used instead.
   */
  getStyleSheetOwnerNode(resourceId) {
    const manager = this.targetActor.getStyleSheetsManager();
    const ownerNode = manager.getOwnerNode(resourceId);
    return this.attachElement(ownerNode);
  }

  /**
   * This method can be used to retrieve NodeActor for DOM nodes from other
   * actors in a way that they can later be highlighted in the page, or
   * selected in the inspector.
   * If an actor has a reference to a DOM node, and the UI needs to know about
   * this DOM node (and possibly select it in the inspector), the UI should
   * first retrieve a reference to the walkerFront:
   *
   * // Make sure the inspector/walker have been initialized first.
   * const inspectorFront = await toolbox.target.getFront("inspector");
   * // Retrieve the walker.
   * const walker = inspectorFront.walker;
   *
   * And then call this method:
   *
   * // Get the nodeFront from my actor, passing the ID and properties path.
   * walker.getNodeFromActor(myActorID, ["element"]).then(nodeFront => {
   *   // Use the nodeFront, e.g. select the node in the inspector.
   *   toolbox.getPanel("inspector").selection.setNodeFront(nodeFront);
   * });
   *
   * @param {string} actorID The ID for the actor that has a reference to the
   * DOM node.
   * @param {Array} path Where, on the actor, is the DOM node stored. If in the
   * scope of the actor, the node is available as `this.data.node`, then this
   * should be ["data", "node"].
   * @return {NodeActor} The attached NodeActor, or null if it couldn't be
   * found.
   */
  getNodeFromActor(actorID, path) {
    const actor = this.conn.getActor(actorID);
    if (!actor) {
      return null;
    }

    let obj = actor;
    for (const name of path) {
      if (!(name in obj)) {
        return null;
      }
      obj = obj[name];
    }

    return this.attachElement(obj);
  }

  /**
   * Returns an instance of the LayoutActor that is used to retrieve CSS layout-related
   * information.
   *
   * @return {LayoutActor}
   */
  getLayoutInspector() {
    if (!this.layoutActor) {
      this.layoutActor = new LayoutActor(this.conn, this.targetActor, this);
    }

    return this.layoutActor;
  }

  /**
   * Returns the parent grid DOMNode of the given node if it exists, otherwise, it
   * returns null.
   */
  getParentGridNode(node) {
    if (isNodeDead(node)) {
      return null;
    }

    const parentGridNode = findGridParentContainerForNode(node.rawNode);
    return parentGridNode ? this._getOrCreateNodeActor(parentGridNode) : null;
  }

  /**
   * Returns the offset parent DOMNode of the given node if it exists, otherwise, it
   * returns null.
   */
  getOffsetParent(node) {
    if (isNodeDead(node)) {
      return null;
    }

    const offsetParent = node.rawNode.offsetParent;

    if (!offsetParent) {
      return null;
    }

    return this._getOrCreateNodeActor(offsetParent);
  }

  getEmbedderElement(browsingContextID) {
    const browsingContext = BrowsingContext.get(browsingContextID);
    let rawNode = browsingContext.embedderElement;
    if (!this._isInDOMTree(rawNode)) {
      return null;
    }

    // This is a special case for the document object whereby it is considered
    // as document.documentElement (the <html> node)
    if (rawNode.defaultView && rawNode === rawNode.defaultView.document) {
      rawNode = rawNode.documentElement;
    }

    return this.attachElement(rawNode);
  }

  pick(doFocus, isLocalTab) {
    this.nodePicker.pick(doFocus, isLocalTab);
  }

  cancelPick() {
    this.nodePicker.cancelPick();
  }

  clearPicker() {
    this.nodePicker.resetHoveredNodeReference();
  }

  /**
   * Given a scrollable node, find its descendants which are causing overflow in it and
   * add their raw nodes to the map as keys with the scrollable element as the values.
   *
   * @param {NodeActor} scrollableNode A scrollable node.
   * @param {Map} map The map to which the overflow causing elements are added.
   */
  updateOverflowCausingElements(scrollableNode, map) {
    if (
      isNodeDead(scrollableNode) ||
      scrollableNode.rawNode.nodeType !== Node.ELEMENT_NODE
    ) {
      return;
    }

    const overflowCausingChildren = [
      ...InspectorUtils.getOverflowingChildrenOfElement(scrollableNode.rawNode),
    ];

    for (let overflowCausingChild of overflowCausingChildren) {
      // overflowCausingChild is a Node, but not necessarily an Element.
      // So, get the containing Element
      if (overflowCausingChild.nodeType !== Node.ELEMENT_NODE) {
        overflowCausingChild = overflowCausingChild.parentElement;
      }
      map.set(overflowCausingChild, scrollableNode);
    }
  }

  /**
   * Returns an array of the overflow causing elements' NodeActor for the given node.
   *
   * @param {NodeActor} node The scrollable node.
   * @return {Array<NodeActor>} An array of the overflow causing elements.
   */
  getOverflowCausingElements(node) {
    if (
      isNodeDead(node) ||
      node.rawNode.nodeType !== Node.ELEMENT_NODE ||
      !node.isScrollable
    ) {
      return [];
    }

    const overflowCausingElements = [
      ...InspectorUtils.getOverflowingChildrenOfElement(node.rawNode),
    ].map(overflowCausingChild => {
      if (overflowCausingChild.nodeType !== Node.ELEMENT_NODE) {
        overflowCausingChild = overflowCausingChild.parentElement;
      }

      return overflowCausingChild;
    });

    return this.attachElements(overflowCausingElements);
  }

  /**
   * Return the scrollable ancestor node which has overflow because of the given node.
   *
   * @param {NodeActor} overflowCausingNode
   */
  getScrollableAncestorNode(overflowCausingNode) {
    if (
      isNodeDead(overflowCausingNode) ||
      !this.overflowCausingElementsMap.has(overflowCausingNode.rawNode)
    ) {
      return null;
    }

    return this.overflowCausingElementsMap.get(overflowCausingNode.rawNode);
  }
}

exports.WalkerActor = WalkerActor;
