/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyRequireGetter(
  this,
  "colorUtils",
  "resource://devtools/shared/css/color.js",
  true
);
loader.lazyRequireGetter(
  this,
  "AsyncUtils",
  "resource://devtools/shared/async-utils.js"
);
loader.lazyRequireGetter(this, "flags", "resource://devtools/shared/flags.js");
loader.lazyRequireGetter(
  this,
  "DevToolsUtils",
  "resource://devtools/shared/DevToolsUtils.js"
);
loader.lazyRequireGetter(
  this,
  "nodeFilterConstants",
  "resource://devtools/shared/dom-node-filter-constants.js"
);
loader.lazyRequireGetter(
  this,
  "getAdjustedQuads",
  "resource://devtools/shared/layout/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "CssLogic",
  "resource://devtools/server/actors/inspector/css-logic.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getBackgroundFor",
  "resource://devtools/server/actors/accessibility/audit/contrast.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["loadSheetForBackgroundCalculation", "removeSheetForBackgroundCalculation"],
  "resource://devtools/server/actors/utils/accessibility.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getTextProperties",
  "resource://devtools/shared/accessibility.js",
  true
);

const XHTML_NS = "http://www.w3.org/1999/xhtml";
const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
const IMAGE_FETCHING_TIMEOUT = 500;

/**
 * Returns the properly cased version of the node's tag name, which can be
 * used when displaying said name in the UI.
 *
 * @param  {Node} rawNode
 *         Node for which we want the display name
 * @return {string}
 *         Properly cased version of the node tag name
 */
const getNodeDisplayName = function (rawNode) {
  const { implementedPseudoElement } = rawNode;
  if (implementedPseudoElement) {
    if (
      (implementedPseudoElement.startsWith("::view-transition") ||
        implementedPseudoElement === "::picker") &&
      rawNode.hasAttribute("name")
    ) {
      return `${implementedPseudoElement}(${rawNode.getAttribute("name")})`;
    }

    return implementedPseudoElement;
  }

  if (rawNode.nodeName && !rawNode.localName) {
    // The localName & prefix APIs have been moved from the Node interface to the Element
    // interface. Use Node.nodeName as a fallback.
    return rawNode.nodeName;
  }
  return (rawNode.prefix ? rawNode.prefix + ":" : "") + rawNode.localName;
};

/**
 * Returns flex and grid information about a DOM node.
 * In particular is it a grid flex/container and/or item?
 *
 * @param  {DOMNode} node
 *         The node for which then information is required
 * @return {object}
 *         An object like { grid: { isContainer, isItem }, flex: { isContainer, isItem } }
 */
function getNodeGridFlexType(node) {
  return {
    grid: getNodeGridType(node),
    flex: getNodeFlexType(node),
  };
}

function getNodeFlexType(node) {
  return {
    isContainer: node.getAsFlexContainer && !!node.getAsFlexContainer(),
    isItem: !!node.parentFlexElement,
  };
}

function getNodeGridType(node) {
  return {
    isContainer: node.hasGridFragments && node.hasGridFragments(),
    isItem: !!findGridParentContainerForNode(node),
  };
}

function nodeDocument(node) {
  if (Cu.isDeadWrapper(node)) {
    return null;
  }
  return (
    node.ownerDocument || (node.nodeType == Node.DOCUMENT_NODE ? node : null)
  );
}

function isNodeDead(node) {
  return !node || !node.rawNode || Cu.isDeadWrapper(node.rawNode);
}

function isInXULDocument(el) {
  const doc = nodeDocument(el);
  return doc?.documentElement && doc.documentElement.namespaceURI === XUL_NS;
}

/**
 * Returns a tree walker filter function that is used to filter the nodes of a nodes tree.
 * Whitespace nodes that do not impact the layout are always skipped.
 *
 * @param {object} options
 *        - {boolean} includeNativeAnonymousContent - Include nodes of native anonymous
 *                    content (excluding pseudo-elements).
 *        - {boolean} includePseudoElements: Include pseudo-elements like ::marker,
 *                    ::before, and ::after, plus anonymous content in XUL document.
 *        - {boolean} includeComments: Include comment nodes.
 *
 * @return {Function} A tree walker filter function that takes a node as argument and returns a
 *                    node filter constant.
 */
function getTreeWalkerFilter(options) {
  return function (node) {
    // Ignore empty whitespace text nodes that do not impact the layout.
    if (isWhitespaceTextNode(node)) {
      return nodeHasSize(node)
        ? nodeFilterConstants.FILTER_ACCEPT
        : nodeFilterConstants.FILTER_SKIP;
    }

    // If comments should be excluded, ignore comment nodes.
    if (!options.includeComments && isCommentNode(node)) {
      return nodeFilterConstants.FILTER_SKIP;
    }

    // There are a few native anonymous content pseudo-elements that we want to show in markup
    // if pseudo-elements should be included.
    const { implementedPseudoElement } = node;
    if (
      options.includePseudoElements &&
      (options.includeNativeAnonymousContent ||
        implementedPseudoElement === "::after" ||
        implementedPseudoElement === "::backdrop" ||
        implementedPseudoElement === "::before" ||
        implementedPseudoElement === "::checkmark" ||
        implementedPseudoElement === "::marker" ||
        implementedPseudoElement === "::picker-icon")
    ) {
      return nodeFilterConstants.FILTER_ACCEPT;
    }

    if (
      !options.includeNativeAnonymousContent &&
      node.isNativeAnonymous &&
      !isInXULDocument(node)
    ) {
      // The ::view-transition pseudo element node has a <div type=":-moz-snapshot-containing-block">
      // parent element that we don't want to display in the markup view.
      // Instead, we want to directly display the ::view-transition pseudo-element.
      if (isViewTransitionSnapshotContainingBlockNode(node)) {
        // FILTER_ACCEPT_CHILDREN means that the node won't be returned, but its children
        // will be instead
        return nodeFilterConstants.FILTER_ACCEPT_CHILDREN;
      }

      // Display all the ::view-transition* nodes
      if (
        node.implementedPseudoElement &&
        node.implementedPseudoElement.startsWith("::view-transition")
      ) {
        return nodeFilterConstants.FILTER_ACCEPT;
      }

      // Ignore all other native anonymous roots inside a non-XUL document.
      // We need to do this to skip things like form controls, scrollbars,
      // video controls, etc. (see bug 1187482).
      return nodeFilterConstants.FILTER_SKIP;
    }

    return nodeFilterConstants.FILTER_ACCEPT;
  };
}

/**
 * Is the given node a text node composed of whitespace only?
 *
 * @param {DOMNode} node
 * @return {boolean}
 */
function isWhitespaceTextNode(node) {
  return node.nodeType == Node.TEXT_NODE && !/[^\s]/.exec(node.nodeValue);
}

/**
 * Is the given node a comment node?
 *
 * @param {DOMNode} node
 * @return {boolean}
 */
function isCommentNode(node) {
  return node.nodeType === Node.COMMENT_NODE;
}

function isViewTransitionSnapshotContainingBlockNode(node) {
  return node.implementedPseudoElement === "::-moz-snapshot-containing-block";
}

/**
 * Does the given node have non-0 width and height?
 *
 * @param {DOMNode} node
 * @return {boolean}
 */
function nodeHasSize(node) {
  if (!node.getBoxQuads) {
    return false;
  }

  const quads = node.getBoxQuads({
    createFramesForSuppressedWhitespace: false,
  });
  return quads.some(quad => {
    const bounds = quad.getBounds();
    return bounds.width && bounds.height;
  });
}

/**
 * Returns a promise that is settled once the given HTMLImageElement has
 * finished loading.
 *
 * @param {HTMLImageElement} image - The image element.
 * @param {number} timeout - Maximum amount of time the image is allowed to load
 * before the waiting is aborted. Ignored if flags.testing is set.
 *
 * @return {Promise} that is fulfilled once the image has loaded. If the image
 * fails to load or the load takes too long, the promise is rejected.
 */
function ensureImageLoaded(image, timeout) {
  const { HTMLImageElement } = image.documentGlobal;
  if (!(image instanceof HTMLImageElement)) {
    return Promise.reject("image must be an HTMLImageELement");
  }

  if (image.complete) {
    // The image has already finished loading.
    return Promise.resolve();
  }

  // This image is still loading.
  const onLoad = AsyncUtils.listenOnce(image, "load");

  // Reject if loading fails.
  const onError = AsyncUtils.listenOnce(image, "error").then(() => {
    return Promise.reject("Image '" + image.src + "' failed to load.");
  });

  // Don't timeout when testing. This is never settled.
  let onAbort = new Promise(() => {});

  if (!flags.testing) {
    // Tests are not running. Reject the promise after given timeout.
    onAbort = DevToolsUtils.waitForTime(timeout).then(() => {
      return Promise.reject("Image '" + image.src + "' took too long to load.");
    });
  }

  // See which happens first.
  return Promise.race([onLoad, onError, onAbort]);
}

/**
 * Given an <img> or <canvas> element, return the image data-uri. If @param node
 * is an <img> element, the method waits a while for the image to load before
 * the data is generated. If the image does not finish loading in a reasonable
 * time (IMAGE_FETCHING_TIMEOUT milliseconds) the process aborts.
 *
 * @param {HTMLImageElement|HTMLCanvasElement} node - The <img> or <canvas>
 * element, or Image() object. Other types cause the method to reject.
 * @param {number} maxDim - Optionally pass a maximum size you want the longest
 * side of the image to be resized to before getting the image data.

 * @return {Promise} A promise that is fulfilled with an object containing the
 * data-uri and size-related information:
 * { data: "...",
 *   size: {
 *     naturalWidth: 400,
 *     naturalHeight: 300,
 *     resized: true }
 *  }.
 *
 * If something goes wrong, the promise is rejected.
 */
const imageToImageData = async function (node, maxDim) {
  const { HTMLCanvasElement, HTMLImageElement } = node.documentGlobal;

  const isImg = node instanceof HTMLImageElement;
  const isCanvas = node instanceof HTMLCanvasElement;

  if (!isImg && !isCanvas) {
    throw new Error("node is not a <canvas> or <img> element.");
  }

  if (isImg) {
    // Ensure that the image is ready.
    await ensureImageLoaded(node, IMAGE_FETCHING_TIMEOUT);
  }

  // Get the image resize ratio if a maxDim was provided
  let resizeRatio = 1;
  const imgWidth = node.naturalWidth || node.width;
  const imgHeight = node.naturalHeight || node.height;
  const imgMax = Math.max(imgWidth, imgHeight);
  if (maxDim && imgMax > maxDim) {
    resizeRatio = maxDim / imgMax;
  }

  // Extract the image data
  let imageData;
  // The image may already be a data-uri, in which case, save ourselves the
  // trouble of converting via the canvas.drawImage.toDataURL method, but only
  // if the image doesn't need resizing
  if (isImg && node.src.startsWith("data:") && resizeRatio === 1) {
    imageData = node.src;
  } else {
    // Create a canvas to copy the rawNode into and get the imageData from
    const canvas = node.ownerDocument.createElementNS(XHTML_NS, "canvas");
    canvas.width = imgWidth * resizeRatio;
    canvas.height = imgHeight * resizeRatio;
    const ctx = canvas.getContext("2d");

    // Copy the rawNode image or canvas in the new canvas and extract data
    ctx.drawImage(node, 0, 0, canvas.width, canvas.height);
    imageData = canvas.toDataURL("image/png");
  }

  return {
    data: imageData,
    size: {
      naturalWidth: imgWidth,
      naturalHeight: imgHeight,
      resized: resizeRatio !== 1,
    },
  };
};

/**
 * Finds the computed background color of the closest parent with a set background color.
 *
 * @param  {DOMNode}  node
 *         Node for which we want to find closest background color.
 * @return {string}
 *         String with the background color of the form rgba(r, g, b, a). Defaults to
 *         rgba(255, 255, 255, 1) if no background color is found.
 */
function getClosestBackgroundColor(node) {
  let current = node;

  while (current) {
    const computedStyle = CssLogic.getComputedStyle(current);
    if (computedStyle) {
      const currentStyle = computedStyle.getPropertyValue("background-color");
      if (InspectorUtils.isValidCSSColor(currentStyle)) {
        const currentCssColor = new colorUtils.CssColor(currentStyle);
        if (!currentCssColor.isTransparent()) {
          return currentCssColor.rgba;
        }
      }
    }

    current = current.parentNode;
  }

  return "rgba(255, 255, 255, 1)";
}

/**
 * Finds the background image of the closest parent where it is set.
 *
 * @param  {DOMNode}  node
 *         Node for which we want to find the background image.
 * @return {string}
 *         String with the value of the background iamge property. Defaults to "none" if
 *         no background image is found.
 */
function getClosestBackgroundImage(node) {
  let current = node;

  while (current) {
    const computedStyle = CssLogic.getComputedStyle(current);
    if (computedStyle) {
      const currentBackgroundImage =
        computedStyle.getPropertyValue("background-image");
      if (currentBackgroundImage !== "none") {
        return currentBackgroundImage;
      }
    }

    current = current.parentNode;
  }

  return "none";
}

/**
 * If the provided node is a grid item, then return its parent grid.
 *
 * @param  {DOMNode} node
 *         The node that is supposedly a grid item.
 * @return {DOMNode|null}
 *         The parent grid if found, null otherwise.
 */
function findGridParentContainerForNode(node) {
  try {
    while ((node = node.parentNode)) {
      const display = node.documentGlobal.getComputedStyle(node).display;

      if (display.includes("grid")) {
        return node;
      } else if (display === "contents") {
        // Continue walking up the tree since the parent node is a content element.
        continue;
      }

      break;
    }
  } catch (e) {
    // Getting the parentNode can fail when the supplied node is in shadow DOM.
  }

  return null;
}

/**
 * Finds the background color range for the parent of a single text node
 * (i.e. for multi-colored backgrounds with gradients, images) or a single
 * background color for single-colored backgrounds. Defaults to the closest
 * background color if an error is encountered.
 *
 * @param  {object}
 *         Node actor containing the following properties:
 *         {DOMNode} rawNode
 *         Node for which we want to calculate the color contrast.
 *         {WalkerActor} walker
 *         Walker actor used to check whether the node is the parent elm of a single text node.
 * @return {object}
 *         Object with one or more of the following properties:
 *         {Array|null} value
 *         RGBA array for single-colored background. Null for multi-colored backgrounds.
 *         {Array|null} min
 *         RGBA array for the min luminance color in a multi-colored background.
 *         Null for single-colored backgrounds.
 *         {Array|null} max
 *         RGBA array for the max luminance color in a multi-colored background.
 *         Null for single-colored backgrounds.
 */
async function getBackgroundColor({ rawNode: node, walker }) {
  // Fall back to calculating contrast against closest bg if:
  // - not element node
  // - more than one child
  // Avoid calculating bounds and creating doc walker by returning early.
  if (
    node.nodeType != Node.ELEMENT_NODE ||
    node.childNodes.length > 1 ||
    !node.firstChild
  ) {
    return {
      value: getClosestBackgroundColorInRGBA(node),
    };
  }

  const quads = getAdjustedQuads(
    node.documentGlobal,
    node.firstChild,
    "content"
  );

  // Fall back to calculating contrast against closest bg if there are no bounds for text node.
  // Avoid creating doc walker by returning early.
  if (quads.length === 0 || !quads[0].bounds) {
    return {
      value: getClosestBackgroundColorInRGBA(node),
    };
  }

  const bounds = quads[0].bounds;

  const docWalker = walker.getDocumentWalker(node);
  const firstChild = docWalker.firstChild();

  // Fall back to calculating contrast against closest bg if:
  // - more than one child
  // - unique child is not a text node
  if (
    !firstChild ||
    docWalker.nextSibling() ||
    firstChild.nodeType !== Node.TEXT_NODE
  ) {
    return {
      value: getClosestBackgroundColorInRGBA(node),
    };
  }

  // Try calculating complex backgrounds for node
  const win = node.documentGlobal;
  loadSheetForBackgroundCalculation(win);
  const computedStyle = CssLogic.getComputedStyle(node);
  const props = computedStyle ? getTextProperties(computedStyle) : null;

  // Fall back to calculating contrast against closest bg if there are no text props.
  if (!props) {
    return {
      value: getClosestBackgroundColorInRGBA(node),
    };
  }

  const bgColor = await getBackgroundFor(node, {
    bounds,
    win,
    convertBoundsRelativeToViewport: false,
    size: props.size,
    isBoldText: props.isBoldText,
  });
  removeSheetForBackgroundCalculation(win);

  return (
    bgColor || {
      value: getClosestBackgroundColorInRGBA(node),
    }
  );
}

/**
 *
 * @param {DOMNode} node: The node we want the background color of
 * @returns {Array[r,g,b,a]}
 */
function getClosestBackgroundColorInRGBA(node) {
  const { r, g, b, a } = InspectorUtils.colorToRGBA(
    getClosestBackgroundColor(node)
  );
  return [r, g, b, a];
}
/**
 * Indicates if a document is ready (i.e. if it's not loading anymore)
 *
 * @param {HTMLDocument} document: The document we want to check
 * @returns {boolean}
 */
function isDocumentReady(document) {
  if (!document) {
    return false;
  }

  const { readyState } = document;
  if (readyState == "interactive" || readyState == "complete") {
    return true;
  }

  // A document might stay forever in uninitialized state.
  // If the target actor is not currently loading a document,
  // assume the document is ready.
  const webProgress = document.defaultView.docShell.QueryInterface(
    Ci.nsIWebProgress
  );
  return !webProgress.isLoadingDocument;
}

module.exports = {
  isDocumentReady,
  isViewTransitionSnapshotContainingBlockNode,
  isWhitespaceTextNode,
  findGridParentContainerForNode,
  getBackgroundColor,
  getClosestBackgroundColor,
  getClosestBackgroundImage,
  getNodeDisplayName,
  getNodeGridFlexType,
  imageToImageData,
  isNodeDead,
  nodeDocument,
  getTreeWalkerFilter,
};
