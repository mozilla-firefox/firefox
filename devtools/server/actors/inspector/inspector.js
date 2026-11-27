/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Here's the server side of the remote inspector.
 *
 * The WalkerActor is the client's view of the debuggee's DOM.  It's gives
 * the client a tree of NodeActor objects.
 *
 * The walker presents the DOM tree mostly unmodified from the source DOM
 * tree, but with a few key differences:
 *
 *  - Empty text nodes are ignored.  This is pretty typical of developer
 *    tools, but maybe we should reconsider that on the server side.
 *  - iframes with documents loaded have the loaded document as the child,
 *    the walker provides one big tree for the whole document tree.
 *
 * There are a few ways to get references to NodeActors:
 *
 *   - When you first get a WalkerActor reference, it comes with a free
 *     reference to the root document's node.
 *   - Given a node, you can ask for children, siblings, and parents.
 *   - You can issue querySelector and querySelectorAll requests to find
 *     other elements.
 *   - Requests that return arbitrary nodes from the tree (like querySelector
 *     and querySelectorAll) will also return any nodes the client hasn't
 *     seen in order to have a complete set of parents.
 *
 * Once you have a NodeFront, you should be able to answer a few questions
 * without further round trips, like the node's name, namespace/tagName,
 * attributes, etc.  Other questions (like a text node's full nodeValue)
 * might require another round trip.
 *
 * The protocol guarantees that the client will always know the parent of
 * any node that is returned by the server.  This means that some requests
 * (like querySelector) will include the extra nodes needed to satisfy this
 * requirement.  The client keeps track of this parent relationship, so the
 * node fronts form a tree that is a subset of the actual DOM tree.
 *
 *
 * We maintain this guarantee to support the ability to release subtrees on
 * the client - when a node is disconnected from the DOM tree we want to be
 * able to free the client objects for all the children nodes.
 *
 * So to be able to answer "all the children of a given node that we have
 * seen on the client side", we guarantee that every time we've seen a node,
 * we connect it up through its parents.
 */

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  inspectorSpec,
} = require("resource://devtools/shared/specs/inspector.js");

const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs",
  { global: "contextual" }
);
const {
  LongStringActor,
} = require("resource://devtools/server/actors/string.js");

loader.lazyRequireGetter(
  this,
  "InspectorActorUtils",
  "resource://devtools/server/actors/inspector/utils.js"
);
loader.lazyRequireGetter(
  this,
  "WalkerActor",
  "resource://devtools/server/actors/inspector/walker.js",
  true
);
loader.lazyRequireGetter(
  this,
  "EyeDropper",
  "resource://devtools/server/actors/highlighters/eye-dropper.js",
  true
);
loader.lazyRequireGetter(
  this,
  "PageStyleActor",
  "resource://devtools/server/actors/page-style.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["CustomHighlighterActor", "isTypeRegistered", "HighlighterEnvironment"],
  "resource://devtools/server/actors/highlighters.js",
  true
);
loader.lazyRequireGetter(
  this,
  "CompatibilityActor",
  "resource://devtools/server/actors/compatibility/compatibility.js",
  true
);

const SVG_NS = "http://www.w3.org/2000/svg";
const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

/**
 * Server side of the inspector actor, which is used to create
 * inspector-related actors, including the walker.
 */
class InspectorActor extends Actor {
  constructor(conn, targetActor) {
    super(conn, inspectorSpec);
    this.targetActor = targetActor;

    this._onColorPicked = this._onColorPicked.bind(this);
    this._onColorPickCanceled = this._onColorPickCanceled.bind(this);
    this.destroyEyeDropper = this.destroyEyeDropper.bind(this);
  }

  highlightersState = {
    fadingViewportSizeHiglighter: null,
  };

  destroy() {
    super.destroy();
    this.destroyEyeDropper();

    this.#highlighters.clear();
    this._compatibility = null;
    this._pageStylePromise = null;
    this._walkerPromise = null;
    this.walker = null;
    this.targetActor = null;
  }

  unmanage(actor) {
    if (actor instanceof CustomHighlighterActor) {
      this.#highlighters.delete(actor.highlighterTypeName);
    }
    super.unmanage(actor);
  }

  get window() {
    return this.targetActor.window;
  }

  getWalker(options = {}) {
    if (this._walkerPromise) {
      return this._walkerPromise;
    }

    this._walkerPromise = new Promise(resolve => {
      const domReady = () => {
        const targetActor = this.targetActor;
        this.walker = new WalkerActor(this.conn, targetActor, options);
        this.manage(this.walker);
        this.walker.once("destroyed", () => {
          this._walkerPromise = null;
          this._pageStylePromise = null;
        });
        resolve(this.walker);
      };

      if (this.window.document.readyState === "loading") {
        // Expose an abort controller for DOMContentLoaded to remove the
        // listener unconditionally, even if the race hits the timeout.
        const abortController = new AbortController();
        Promise.race([
          new Promise(r => {
            this.window.addEventListener("DOMContentLoaded", r, {
              capture: true,
              once: true,
              signal: abortController.signal,
            });
          }),
          // The DOMContentLoaded event will never be emitted on documents stuck
          // in the loading state, for instance if document.write was called
          // without calling document.close.
          // TODO: It is not clear why we are waiting for the event overall, see
          // Bug 1766279 to actually stop listening to the event altogether.
          new Promise(r => setTimeout(r, 500)),
        ])
          .then(domReady)
          .finally(() => abortController.abort());
      } else {
        domReady();
      }
    });

    return this._walkerPromise;
  }

  getPageStyle() {
    if (this._pageStylePromise) {
      return this._pageStylePromise;
    }

    this._pageStylePromise = this.getWalker().then(() => {
      const pageStyle = new PageStyleActor(this);
      this.manage(pageStyle);
      return pageStyle;
    });
    return this._pageStylePromise;
  }

  getCompatibility() {
    if (this._compatibility) {
      return this._compatibility;
    }

    this._compatibility = new CompatibilityActor(this);
    this.manage(this._compatibility);
    return this._compatibility;
  }

  // Map of all already instantiated highlighter actors, keyed by type name (string).
  #highlighters = new Map();

  /**
   * If consumers need to display several highlighters at the same time or
   * different types of highlighters, then this method should be used, passing
   * the type name of the highlighter needed as argument.
   * A new instance will be created everytime the method is called, so it's up
   * to the consumer to release it when it is not needed anymore
   *
   * @param {string} typeName The type of highlighter to create
   * @param {boolean} forceName Optional boolean to force instantiating a new instance,
   *                            even if one was already created.
   *                            (this is used for grid highlighters where we may spawn
   *                             many of them)
   * @return {Highlighter} The highlighter actor instance or null if the
   * typeName passed doesn't match any available highlighter
   */
  async getHighlighterByType(typeName, forceNew = false) {
    let highlighterActor = this.#highlighters.get(typeName);
    if (highlighterActor && !forceNew) {
      return highlighterActor;
    }
    if (isTypeRegistered(typeName)) {
      highlighterActor = new CustomHighlighterActor(this, typeName);
      // When we force the instantiation of a new instance, do not cache this actor.
      if (!forceNew) {
        this.#highlighters.set(typeName, highlighterActor);
      }
      if (highlighterActor.instance.isReady) {
        await highlighterActor.instance.isReady;
      }

      return highlighterActor;
    }
    return null;
  }

  /**
   * Get the node's image data if any (for canvas and img nodes).
   * Returns an imageData object with the actual data being a LongStringActor
   * and a size json object.
   * The image data is transmitted as a base64 encoded png data-uri.
   * The method rejects if the node isn't an image or if the image is missing
   *
   * Accepts a maxDim request parameter to resize images that are larger. This
   * is important as the resizing occurs server-side so that image-data being
   * transfered in the longstring back to the client will be that much smaller
   */
  getImageDataFromURL(url, maxDim) {
    const img = new this.window.Image();
    img.src = url;

    // imageToImageData waits for the image to load.
    return InspectorActorUtils.imageToImageData(img, maxDim).then(imageData => {
      return {
        data: new LongStringActor(this.conn, imageData.data),
        size: imageData.size,
      };
    });
  }

  /**
   * Resolve a URL to its absolute form, in the scope of a given content window.
   *
   * @param {string} url.
   * @param {NodeActor} node If provided, the owner window of this node will be
   * used to resolve the URL. Otherwise, the top-level content window will be
   * used instead.
   * @return {string} url.
   */
  resolveRelativeURL(url, node) {
    const document = InspectorActorUtils.isNodeDead(node)
      ? this.window.document
      : InspectorActorUtils.nodeDocument(node.rawNode);

    if (!document) {
      return url;
    }

    const baseURI = Services.io.newURI(document.baseURI);
    return Services.io.newURI(url, null, baseURI).spec;
  }

  /**
   * Create an instance of the eye-dropper highlighter and store it on this._eyeDropper.
   * Note that for now, a new instance is created every time to deal with page navigation.
   */
  createEyeDropper() {
    this.destroyEyeDropper();
    this._highlighterEnv = new HighlighterEnvironment();
    this._highlighterEnv.initFromTargetActor(this.targetActor);
    this._eyeDropper = new EyeDropper(this._highlighterEnv);
    return this._eyeDropper.isReady;
  }

  /**
   * Destroy the current eye-dropper highlighter instance.
   */
  destroyEyeDropper() {
    if (this._eyeDropper) {
      this.cancelPickColorFromPage();
      this._eyeDropper.destroy();
      this._eyeDropper = null;
      this._highlighterEnv.destroy();
      this._highlighterEnv = null;
    }
  }

  /**
   * Pick a color from the page using the eye-dropper. This method doesn't return anything
   * but will cause events to be sent to the front when a color is picked or when the user
   * cancels the picker.
   *
   * @param {object} options
   */
  async pickColorFromPage(options) {
    await this.createEyeDropper();
    this._eyeDropper.once("selected", this._onColorPicked);
    this._eyeDropper.once("canceled", this._onColorPickCanceled);
    this.targetActor.once("will-navigate", this.destroyEyeDropper);
    await this._eyeDropper.show(this.window.document.documentElement, options);
  }

  /**
   * After the pickColorFromPage method is called, the only way to dismiss the eye-dropper
   * highlighter is for the user to click in the page and select a color. If you need to
   * dismiss the eye-dropper programatically instead, use this method.
   */
  cancelPickColorFromPage() {
    if (this._eyeDropper) {
      this._eyeDropper.hide();
      this._eyeDropper.off("selected", this._onColorPicked);
      this._eyeDropper.off("canceled", this._onColorPickCanceled);
      this.targetActor.off("will-navigate", this.destroyEyeDropper);
    }
  }

  /**
   * Check if the current document supports highlighters using a canvasFrame anonymous
   * content container.
   * It is impossible to detect the feature programmatically as some document types simply
   * don't render the canvasFrame without throwing any error.
   */
  supportsHighlighters() {
    const doc = this.targetActor.window.document;
    const ns = doc.documentElement.namespaceURI;

    // XUL documents do not support insertAnonymousContent().
    if (ns === XUL_NS) {
      return false;
    }

    // SVG documents do not render the canvasFrame (see Bug 1157592).
    if (ns === SVG_NS) {
      return false;
    }

    return true;
  }

  _onColorPicked(color) {
    this.emit("color-picked", color);
  }

  _onColorPickCanceled() {
    this.emit("color-pick-canceled");
  }
}

exports.InspectorActor = InspectorActor;
