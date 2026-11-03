/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  atom: "chrome://remote/content/marionette/atom.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  PollPromise: "chrome://remote/content/shared/Sync.sys.mjs",
});

const ORDERED_NODE_ITERATOR_TYPE = 5;
const FIRST_ORDERED_NODE_TYPE = 9;

const DOCUMENT_FRAGMENT_NODE = 11;
const ELEMENT_NODE = 1;

const MATHML_NS = "http://www.w3.org/1998/Math/MathML";
const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

/** XUL elements that support checked property. */
const XUL_CHECKED_ELS = new Set(["button", "checkbox", "toolbarbutton"]);

/** XUL elements that support selected property. */
const XUL_SELECTED_ELS = new Set([
  "menu",
  "menuitem",
  "menuseparator",
  "radio",
  "richlistitem",
  "tab",
]);

/**
 * This module provides shared functionality for dealing with DOM-
 * and web elements in Marionette.
 *
 * A web element is an abstraction used to identify an element when it
 * is transported across the protocol, between remote- and local ends.
 *
 * Each element has an associated web element reference (a UUID) that
 * uniquely identifies the the element across all browsing contexts. The
 * web element reference for every element representing the same element
 * is the same.
 *
 * @namespace
 */
export const dom = {};

dom.Strategy = {
  ClassName: "class name",
  Selector: "css selector",
  ID: "id",
  Name: "name",
  LinkText: "link text",
  PartialLinkText: "partial link text",
  TagName: "tag name",
  XPath: "xpath",
};

/**
 * Find a single element or a collection of elements starting at the
 * document root or a given node.
 *
 * If |timeout| is above 0, an implicit search technique is used.
 * This will wait for the duration of <var>timeout</var> for the
 * element to appear in the DOM.
 *
 * See the {@link dom.Strategy} enum for a full list of supported
 * search strategies that can be passed to <var>strategy</var>.
 *
 * @param {Record<string, WindowProxy>} container
 *     Window object.
 * @param {string} strategy
 *     Search strategy whereby to locate the element(s).
 * @param {string} selector
 *     Selector search pattern.  The selector must be compatible with
 *     the chosen search <var>strategy</var>.
 * @param {object=} options
 * @param {boolean=} options.all
 *     If true, all matching elements are searched for, otherwise only the first
 *     match is searched for. Defaults to false.
 * @param {Element=} options.startNode
 *     Element to use as the root of the search.
 * @param {number=} options.timeout
 *     Duration to wait before timing out the search.
 * @returns {Promise.<Array.<Element>>}
 *     Sequence of elements.
 * @throws InvalidSelectorError
 *     If <var>strategy</var> is unknown.
 * @throws InvalidSelectorError
 *     If <var>selector</var> is malformed.
 */
dom.find = function (container, strategy, selector, options = {}) {
  const { all = false, startNode, timeout = 0 } = options;

  let searchFn;
  if (all) {
    searchFn = findElements.bind(this);
  } else {
    searchFn = findElement.bind(this);
  }

  return new lazy.PollPromise(
    async (resolve, reject) => {
      try {
        let res = await find_(container, strategy, selector, searchFn, {
          all,
          startNode,
        });
        if (res.length) {
          resolve(Array.from(res));
        } else {
          reject([]);
        }
      } catch (e) {
        reject(e);
      }
    },
    { timeout, throws: null }
  );
};

async function find_(
  container,
  strategy,
  selector,
  searchFn,
  { startNode = null, all = false } = {}
) {
  let rootNode;

  if (dom.isShadowRoot(startNode)) {
    rootNode = startNode.ownerDocument;
  } else {
    rootNode = container.frame.document;
  }

  if (!startNode) {
    startNode = rootNode;
  }

  let res;
  try {
    res = await searchFn(strategy, selector, rootNode, startNode);
  } catch (e) {
    throw new lazy.error.InvalidSelectorError(
      `Given ${strategy} expression "${selector}" is invalid: ${e}`
    );
  }

  if (res) {
    if (all) {
      return res;
    }
    return [res];
  }
  return [];
}

/**
 * Find a single element by XPath expression.
 *
 * @param {Document} document
 *     Document root.
 * @param {Element} startNode
 *     Where in the DOM hierarchy to begin searching.
 * @param {string} expression
 *     XPath search expression.
 *
 * @returns {Node}
 *     First element matching <var>expression</var>.
 */
dom.findByXPath = function (document, startNode, expression) {
  let iter = document.evaluate(
    expression,
    startNode,
    null,
    FIRST_ORDERED_NODE_TYPE,
    null
  );
  return iter.singleNodeValue;
};

/**
 * Find elements by XPath expression.
 *
 * @param {Document} document
 *     Document root.
 * @param {Element} startNode
 *     Where in the DOM hierarchy to begin searching.
 * @param {string} expression
 *     XPath search expression.
 *
 * @returns {Iterable.<Node>}
 *     Iterator over nodes matching <var>expression</var>.
 */
dom.findByXPathAll = function* (document, startNode, expression) {
  let iter = document.evaluate(
    expression,
    startNode,
    null,
    ORDERED_NODE_ITERATOR_TYPE,
    null
  );
  let el = iter.iterateNext();
  while (el) {
    yield el;
    el = iter.iterateNext();
  }
};

/**
 * Find all hyperlinks descendant of <var>startNode</var> which
 * link text is <var>linkText</var>.
 *
 * @param {Element} startNode
 *     Where in the DOM hierarchy to begin searching.
 * @param {string} linkText
 *     Link text to search for.
 *
 * @returns {Iterable.<HTMLAnchorElement>}
 *     Sequence of link elements which text is <var>s</var>.
 */
dom.findByLinkText = function (startNode, linkText) {
  return filterLinks(startNode, async link => {
    const visibleText = await lazy.atom.getVisibleText(
      link,
      link.documentGlobal
    );
    return visibleText.trim() === linkText;
  });
};

/**
 * Find all hyperlinks descendant of <var>startNode</var> which
 * link text contains <var>linkText</var>.
 *
 * @param {Element} startNode
 *     Where in the DOM hierarchy to begin searching.
 * @param {string} linkText
 *     Link text to search for.
 *
 * @returns {Iterable.<HTMLAnchorElement>}
 *     Iterator of link elements which text containins
 *     <var>linkText</var>.
 */
dom.findByPartialLinkText = function (startNode, linkText) {
  return filterLinks(startNode, async link => {
    const visibleText = await lazy.atom.getVisibleText(
      link,
      link.documentGlobal
    );

    return visibleText.includes(linkText);
  });
};

/**
 * Filters all hyperlinks that are descendant of <var>startNode</var>
 * by <var>predicate</var>.
 *
 * @param {Element} startNode
 *     Where in the DOM hierarchy to begin searching.
 * @param {function(HTMLAnchorElement): boolean} predicate
 *     Function that determines if given link should be included in
 *     return value or filtered away.
 *
 * @returns {Array.<HTMLAnchorElement>}
 *     Array of link elements matching <var>predicate</var>.
 */
async function filterLinks(startNode, predicate) {
  const links = [];

  for (const link of getLinks(startNode)) {
    if (await predicate(link)) {
      links.push(link);
    }
  }

  return links;
}

/**
 * Finds a single element.
 *
 * @param {dom.Strategy} strategy
 *     Selector strategy to use.
 * @param {string} selector
 *     Selector expression.
 * @param {Document} document
 *     Document root.
 * @param {Element=} startNode
 *     Optional Element from which to start searching.
 *
 * @returns {Element}
 *     Found element.
 *
 * @throws {InvalidSelectorError}
 *     If strategy <var>using</var> is not recognised.
 * @throws {Error}
 *     If selector expression <var>selector</var> is malformed.
 */
async function findElement(
  strategy,
  selector,
  document,
  startNode = undefined
) {
  switch (strategy) {
    case dom.Strategy.ID: {
      if (startNode.getElementById) {
        return startNode.getElementById(selector);
      }
      let expr = `.//*[@id="${selector}"]`;
      return dom.findByXPath(document, startNode, expr);
    }

    case dom.Strategy.Name: {
      if (startNode.getElementsByName) {
        return startNode.getElementsByName(selector)[0];
      }
      let expr = `.//*[@name="${selector}"]`;
      return dom.findByXPath(document, startNode, expr);
    }

    case dom.Strategy.ClassName:
      return startNode.getElementsByClassName(selector)[0];

    case dom.Strategy.TagName:
      return startNode.getElementsByTagName(selector)[0];

    case dom.Strategy.XPath:
      return dom.findByXPath(document, startNode, selector);

    case dom.Strategy.LinkText: {
      const links = getLinks(startNode);
      for (const link of links) {
        const visibleText = await lazy.atom.getVisibleText(
          link,
          link.documentGlobal
        );
        if (visibleText.trim() === selector) {
          return link;
        }
      }
      return undefined;
    }

    case dom.Strategy.PartialLinkText: {
      const links = getLinks(startNode);
      for (const link of links) {
        const visibleText = await lazy.atom.getVisibleText(
          link,
          link.documentGlobal
        );
        if (visibleText.includes(selector)) {
          return link;
        }
      }
      return undefined;
    }

    case dom.Strategy.Selector:
      try {
        return startNode.querySelector(selector);
      } catch (e) {
        throw new lazy.error.InvalidSelectorError(
          `${e.message}: "${selector}"`
        );
      }
  }

  throw new lazy.error.InvalidSelectorError(`No such strategy: ${strategy}`);
}

/**
 * Find multiple elements.
 *
 * @param {dom.Strategy} strategy
 *     Selector strategy to use.
 * @param {string} selector
 *     Selector expression.
 * @param {Document} document
 *     Document root.
 * @param {Element=} startNode
 *     Optional Element from which to start searching.
 *
 * @returns {Array.<Element>}
 *     Found elements.
 *
 * @throws {InvalidSelectorError}
 *     If strategy <var>strategy</var> is not recognised.
 * @throws {Error}
 *     If selector expression <var>selector</var> is malformed.
 */
async function findElements(
  strategy,
  selector,
  document,
  startNode = undefined
) {
  switch (strategy) {
    case dom.Strategy.ID:
      selector = `.//*[@id="${selector}"]`;

    // fall through
    case dom.Strategy.XPath:
      return [...dom.findByXPathAll(document, startNode, selector)];

    case dom.Strategy.Name:
      if (startNode.getElementsByName) {
        return startNode.getElementsByName(selector);
      }
      return [
        ...dom.findByXPathAll(document, startNode, `.//*[@name="${selector}"]`),
      ];

    case dom.Strategy.ClassName:
      return startNode.getElementsByClassName(selector);

    case dom.Strategy.TagName:
      return startNode.getElementsByTagName(selector);

    case dom.Strategy.LinkText:
      return [...(await dom.findByLinkText(startNode, selector))];

    case dom.Strategy.PartialLinkText:
      return [...(await dom.findByPartialLinkText(startNode, selector))];

    case dom.Strategy.Selector:
      return startNode.querySelectorAll(selector);

    default:
      throw new lazy.error.InvalidSelectorError(
        `No such strategy: ${strategy}`
      );
  }
}

function getLinks(startNode) {
  // DocumentFragment doesn't have `getElementsByTagName` so using `querySelectorAll`.
  if (dom.isShadowRoot(startNode)) {
    return startNode.querySelectorAll("a");
  }
  return startNode.getElementsByTagName("a");
}

/**
 * Finds the closest parent node of <var>startNode</var> matching a CSS
 * <var>selector</var> expression.
 *
 * @param {Node} startNode
 *     Cycle through <var>startNode</var>'s parent nodes in tree-order
 *     and return the first match to <var>selector</var>.
 * @param {string} selector
 *     CSS selector expression.
 *
 * @returns {Node=}
 *     First match to <var>selector</var>, or null if no match was found.
 */
dom.findClosest = function (startNode, selector) {
  let node = startNode;
  while (node.parentNode && node.parentNode.nodeType == ELEMENT_NODE) {
    node = node.parentNode;
    if (node.matches(selector)) {
      return node;
    }
  }
  return null;
};

/**
 * Determines if <var>obj<var> is an HTML or JS collection.
 *
 * @param {object} seq
 *     Type to determine.
 *
 * @returns {boolean}
 *     True if <var>seq</va> is a collection.
 */
dom.isCollection = function (seq) {
  switch (Object.prototype.toString.call(seq)) {
    case "[object Arguments]":
    case "[object Array]":
    case "[object DOMTokenList]":
    case "[object FileList]":
    case "[object HTMLAllCollection]":
    case "[object HTMLCollection]":
    case "[object HTMLFormControlsCollection]":
    case "[object HTMLOptionsCollection]":
    case "[object NodeList]":
      return true;

    default:
      return false;
  }
};

/**
 * Determines if <var>shadowRoot</var> is detached.
 *
 * A ShadowRoot is detached if its node document is not the active document
 * or if the element node referred to as its host is stale.
 *
 * @param {ShadowRoot} shadowRoot
 *     ShadowRoot to check for detached state.
 *
 * @returns {boolean}
 *     True if <var>shadowRoot</var> is detached, false otherwise.
 */
dom.isDetached = function (shadowRoot) {
  return !shadowRoot.ownerDocument.isActive() || dom.isStale(shadowRoot.host);
};

/**
 * Determines if <var>el</var> is stale.
 *
 * An element is stale if its node document is not the active document
 * or if it is not connected.
 *
 * @param {Element} el
 *     Element to check for staleness.
 *
 * @returns {boolean}
 *     True if <var>el</var> is stale, false otherwise.
 */
dom.isStale = function (el) {
  return !el.ownerDocument.isActive() || !el.isConnected;
};

/**
 * Determine if <var>el</var> is selected or not.
 *
 * This operation only makes sense on
 * <tt>&lt;input type=checkbox&gt;</tt>,
 * <tt>&lt;input type=radio&gt;</tt>,
 * and <tt>&gt;option&gt;</tt> elements.
 *
 * @param {Element} el
 *     Element to test if selected.
 *
 * @returns {boolean}
 *     True if element is selected, false otherwise.
 */
dom.isSelected = function (el) {
  if (!el) {
    return false;
  }

  if (dom.isXULElement(el)) {
    if (XUL_CHECKED_ELS.has(el.tagName)) {
      return el.checked;
    } else if (XUL_SELECTED_ELS.has(el.tagName)) {
      return el.selected;
    }
  } else if (dom.isDOMElement(el)) {
    if (el.localName == "input" && ["checkbox", "radio"].includes(el.type)) {
      return el.checked;
    } else if (el.localName == "option") {
      return el.selected;
    }
  }

  return false;
};

/**
 * An element is considered read only if it is an
 * <code>&lt;input&gt;</code> or <code>&lt;textarea&gt;</code>
 * element whose <code>readOnly</code> content IDL attribute is set.
 *
 * @param {Element} el
 *     Element to test is read only.
 *
 * @returns {boolean}
 *     True if element is read only.
 */
dom.isReadOnly = function (el) {
  return (
    dom.isDOMElement(el) &&
    ["input", "textarea"].includes(el.localName) &&
    el.readOnly
  );
};

/**
 * An element is considered disabled if it is a an element
 * that can be disabled, or it belongs to a container group which
 * <code>disabled</code> content IDL attribute affects it.
 *
 * @param {Element} el
 *     Element to test for disabledness.
 *
 * @returns {boolean}
 *     True if element, or its container group, is disabled.
 */
dom.isDisabled = function (el) {
  if (!dom.isDOMElement(el)) {
    return false;
  }

  // Selenium expects that even an enabled "option" element that is a child
  // of a disabled "optgroup" or "select" element to be disabled.
  if (["optgroup", "option"].includes(el.localName) && !el.disabled) {
    const parent = dom.findClosest(el, "optgroup,select");
    return dom.isDisabled(parent);
  }

  return el.matches(":disabled");
};

/**
 * Denotes elements that can be used for typing and clearing.
 *
 * Elements that are considered WebDriver-editable are non-readonly
 * and non-disabled <code>&lt;input&gt;</code> elements in the Text,
 * Search, URL, Telephone, Email, Password, Date, Month, Date and
 * Time Local, Number, Range, Color, and File Upload states, and
 * <code>&lt;textarea&gt;</code> elements.
 *
 * @param {Element} el
 *     Element to test.
 *
 * @returns {boolean}
 *     True if editable, false otherwise.
 */
dom.isMutableFormControl = function (el) {
  if (!dom.isDOMElement(el)) {
    return false;
  }
  if (dom.isReadOnly(el) || dom.isDisabled(el)) {
    return false;
  }

  if (el.localName == "textarea") {
    return true;
  }

  if (el.localName != "input") {
    return false;
  }

  switch (el.type) {
    case "color":
    case "date":
    case "datetime-local":
    case "email":
    case "file":
    case "month":
    case "number":
    case "password":
    case "range":
    case "search":
    case "tel":
    case "text":
    case "time":
    case "url":
    case "week":
      return true;

    default:
      return false;
  }
};

/**
 * An editing host is a node that is either an HTML element with a
 * <code>contenteditable</code> attribute, or the HTML element child
 * of a document whose <code>designMode</code> is enabled.
 *
 * @param {Element} el
 *     Element to determine if is an editing host.
 *
 * @returns {boolean}
 *     True if editing host, false otherwise.
 */
dom.isEditingHost = function (el) {
  return (
    dom.isDOMElement(el) &&
    (el.isContentEditable || el.ownerDocument.designMode == "on")
  );
};

/**
 * Determines if an element is editable according to WebDriver.
 *
 * An element is considered editable if it is not read-only or
 * disabled, and one of the following conditions are met:
 *
 * <ul>
 * <li>It is a <code>&lt;textarea&gt;</code> element.
 *
 * <li>It is an <code>&lt;input&gt;</code> element that is not of
 * the <code>checkbox</code>, <code>radio</code>, <code>hidden</code>,
 * <code>submit</code>, <code>button</code>, or <code>image</code> types.
 *
 * <li>It is content-editable.
 *
 * <li>It belongs to a document in design mode.
 * </ul>
 *
 * @param {Element} el
 *     Element to test if editable.
 *
 * @returns {boolean}
 *     True if editable, false otherwise.
 */
dom.isEditable = function (el) {
  if (!dom.isDOMElement(el)) {
    return false;
  }

  if (dom.isReadOnly(el) || dom.isDisabled(el)) {
    return false;
  }

  return dom.isMutableFormControl(el) || dom.isEditingHost(el);
};

/**
 * This function generates a pair of coordinates relative to the viewport
 * given a target element and coordinates relative to that element's
 * top-left corner.
 *
 * @param {Node} node
 *     Target node.
 * @param {number=} xOffset
 *     Horizontal offset relative to target's top-left corner.
 *     Defaults to the centre of the target's bounding box.
 * @param {number=} yOffset
 *     Vertical offset relative to target's top-left corner.  Defaults to
 *     the centre of the target's bounding box.
 *
 * @returns {Record<string, number>}
 *     X- and Y coordinates.
 *
 * @throws TypeError
 *     If <var>xOffset</var> or <var>yOffset</var> are not numbers.
 */
dom.coordinates = function (node, xOffset = undefined, yOffset = undefined) {
  let box = node.getBoundingClientRect();

  if (typeof xOffset == "undefined" || xOffset === null) {
    xOffset = box.width / 2.0;
  }
  if (typeof yOffset == "undefined" || yOffset === null) {
    yOffset = box.height / 2.0;
  }

  if (typeof yOffset != "number" || typeof xOffset != "number") {
    throw new TypeError("Offset must be a number");
  }

  return {
    x: box.left + xOffset,
    y: box.top + yOffset,
  };
};

/**
 * This function returns true if the node is in the viewport.
 *
 * @param {Element} el
 *     Target element.
 * @param {number=} x
 *     Horizontal offset relative to target.  Defaults to the centre of
 *     the target's bounding box.
 * @param {number=} y
 *     Vertical offset relative to target.  Defaults to the centre of
 *     the target's bounding box.
 *
 * @returns {boolean}
 *     True if if <var>el</var> is in viewport, false otherwise.
 */
dom.inViewport = function (el, x = undefined, y = undefined) {
  let win = el.documentGlobal;
  let c = dom.coordinates(el, x, y);
  let vp = {
    top: win.pageYOffset,
    left: win.pageXOffset,
    bottom: win.pageYOffset + win.innerHeight,
    right: win.pageXOffset + win.innerWidth,
  };

  return (
    vp.left <= c.x + win.pageXOffset &&
    c.x + win.pageXOffset <= vp.right &&
    vp.top <= c.y + win.pageYOffset &&
    c.y + win.pageYOffset <= vp.bottom
  );
};

/**
 * Gets the element's container element.
 *
 * An element container is defined by the WebDriver
 * specification to be an <tt>&lt;option&gt;</tt> element in a
 * <a href="https://html.spec.whatwg.org/#concept-element-contexts">valid
 * element context</a>, meaning that it has an ancestral element
 * that is either <tt>&lt;datalist&gt;</tt> or <tt>&lt;select&gt;</tt>.
 *
 * If the element does not have a valid context, its container element
 * is itself.
 *
 * @param {Element} el
 *     Element to get the container of.
 *
 * @returns {Element}
 *     Container element of <var>el</var>.
 */
dom.getContainer = function (el) {
  // Does <option> or <optgroup> have a valid context,
  // meaning is it a child of <datalist> or <select>?
  if (["option", "optgroup"].includes(el.localName)) {
    return dom.findClosest(el, "datalist,select") || el;
  }

  return el;
};

/**
 * An element is in view if it is a member of its own pointer-interactable
 * paint tree.
 *
 * This means an element is considered to be in view, but not necessarily
 * pointer-interactable, if it is found somewhere in the
 * <code>elementsFromPoint</code> list at <var>el</var>'s in-view
 * centre coordinates.
 *
 * Before running the check, we change <var>el</var>'s pointerEvents
 * style property to "auto", since elements without pointer events
 * enabled do not turn up in the paint tree we get from
 * document.elementsFromPoint.  This is a specialisation that is only
 * relevant when checking if the element is in view.
 *
 * @param {Element} el
 *     Element to check if is in view.
 *
 * @returns {boolean}
 *     True if <var>el</var> is inside the viewport, or false otherwise.
 */
dom.isInView = function (el) {
  let originalPointerEvents = el.style.pointerEvents;
  let originalStyleAttrValue = el.getAttribute("style");

  try {
    el.style.pointerEvents = "auto";
    const tree = dom.getPointerInteractablePaintTree(el);

    // Bug 1413493 - <tr> is not part of the returned paint tree yet. As
    // workaround check the visibility based on the first contained cell.
    if (el.localName === "tr" && el.cells && el.cells.length) {
      return tree.includes(el.cells[0]);
    }
    // Same for MathML <mtr>, but we can't use HTMLTableRowElement.cells.
    if (
      el.namespaceURI === MATHML_NS &&
      el.localName === "mtr" &&
      el.children &&
      el.children.length
    ) {
      return tree.includes(el.children[0]);
    }

    return tree.includes(el);
  } finally {
    el.style.pointerEvents = originalPointerEvents;
    if (originalStyleAttrValue === null) {
      el.removeAttribute("style");
    } else if (el.getAttribute("style") != originalStyleAttrValue) {
      el.setAttribute("style", originalStyleAttrValue);
    }
  }
};

/**
 * This function throws the visibility of the element error if the element is
 * not displayed or the given coordinates are not within the viewport.
 *
 * @param {Element} el
 *     Element to check if visible.
 * @param {number=} x
 *     Horizontal offset relative to target.  Defaults to the centre of
 *     the target's bounding box.
 * @param {number=} y
 *     Vertical offset relative to target.  Defaults to the centre of
 *     the target's bounding box.
 *
 * @returns {boolean}
 *     True if visible, false otherwise.
 */
dom.isVisible = async function (el, x = undefined, y = undefined) {
  let win = el.documentGlobal;
  if (!(await lazy.atom.isElementDisplayed(el, win))) {
    return false;
  }

  if (el.tagName.toLowerCase() == "body") {
    return true;
  }

  if (!dom.inViewport(el, x, y)) {
    dom.scrollIntoView(el);
    if (!dom.inViewport(el)) {
      return false;
    }
  }
  return true;
};

/**
 * A pointer-interactable element is defined to be the first
 * non-transparent element, defined by the paint order found at the centre
 * point of its rectangle that is inside the viewport, excluding the size
 * of any rendered scrollbars.
 *
 * An element is obscured if the pointer-interactable paint tree at its
 * centre point is empty, or the first element in this tree is not an
 * inclusive descendant of itself.
 *
 * @param {DOMElement} el
 *     Element determine if is pointer-interactable.
 *
 * @returns {boolean}
 *     True if element is obscured, false otherwise.
 */
dom.isObscured = function (el) {
  let tree = dom.getPointerInteractablePaintTree(el);
  return !el.contains(tree[0]);
};

/**
 * Returns the first rect from a DOMRectList with non-zero width and height,
 * falling back to the first rect if all are zero-size.
 *
 * Inline elements wrapping block-level children produce zero-size line-box
 * rects at the start/end of their getClientRects() list. Using such a rect
 * to compute a click point or check visibility leads to incorrect results.
 *
 * Note: this diverges from the Webdriver spec. Indeed the Webdriver spec
 * mentions using the first rect in the list.
 * See https://github.com/w3c/webdriver/issues/1961
 *
 * @param {DOMRectList} rects
 *     List of rects to search through.
 *
 * @returns {DOMRect}
 *     First rect with non-zero width and height, or rects[0] if all are zero-size.
 */
dom.getFirstNonZeroRect = function (rects) {
  return (
    Array.prototype.find.call(rects, r => r.width > 0 && r.height > 0) ||
    rects[0]
  );
};

// TODO(ato): Only used by deprecated action API
// https://bugzil.la/1354578
/**
 * Calculates the in-view centre point of an element's client rect.
 *
 * The portion of an element that is said to be _in view_, is the
 * intersection of two squares: the first square being the initial
 * viewport, and the second a DOM element.  From this square we
 * calculate the in-view _centre point_ and convert it into CSS pixels.
 *
 * Although Gecko's system internals allow click points to be
 * given in floating point precision, the DOM operates in CSS pixels.
 * When the in-view centre point is later used to retrieve a coordinate's
 * paint tree, we need to ensure to operate in the same language.
 *
 * As a word of warning, there appears to be inconsistencies between
 * how `DOMElement.elementsFromPoint` and `DOMWindowUtils.sendMouseEvent`
 * internally rounds (ceils/floors) coordinates.
 *
 * @param {DOMRect} rect
 *     Element off a DOMRect sequence produced by calling
 *     `getClientRects` on an {@link Element}.
 * @param {WindowProxy} win
 *     Current window global.
 *
 * @returns {Map.<string, number>}
 *     X and Y coordinates that denotes the in-view centre point of
 *     `rect`.
 */
dom.getInViewCentrePoint = function (rect, win) {
  const { floor, max, min } = Math;

  // calculate the intersection of the rect that is inside the viewport
  let visible = {
    left: max(0, min(rect.x, rect.x + rect.width)),
    right: min(win.innerWidth, max(rect.x, rect.x + rect.width)),
    top: max(0, min(rect.y, rect.y + rect.height)),
    bottom: min(win.innerHeight, max(rect.y, rect.y + rect.height)),
  };

  // arrive at the centre point of the visible rectangle
  let x = (visible.left + visible.right) / 2.0;
  let y = (visible.top + visible.bottom) / 2.0;

  // convert to CSS pixels, as centre point can be float
  x = floor(x);
  y = floor(y);

  return { x, y };
};

/**
 * Produces a pointer-interactable elements tree from a given element.
 *
 * The tree is defined by the paint order found at the centre point of
 * the element's rectangle that is inside the viewport, excluding the size
 * of any rendered scrollbars.
 *
 * @param {DOMElement} el
 *     Element to determine if is pointer-interactable.
 *
 * @returns {Array.<DOMElement>}
 *     Sequence of elements in paint order.
 */
dom.getPointerInteractablePaintTree = function (el) {
  const win = el.documentGlobal;
  const rootNode = el.getRootNode();

  // pointer-interactable elements tree, step 1
  if (!el.isConnected) {
    return [];
  }

  // steps 2-3
  let rects = el.getClientRects();
  if (!rects.length) {
    return [];
  }

  // step 4
  let centre = dom.getInViewCentrePoint(dom.getFirstNonZeroRect(rects), win);

  // step 5
  return rootNode.elementsFromPoint(centre.x, centre.y);
};

// TODO(ato): Not implemented.
// In fact, it's not defined in the spec.
dom.isKeyboardInteractable = () => true;

/**
 * Attempts to scroll `el` into view.
 *
 * @param {DOMElement} el
 *     Element to scroll into view.
 */
dom.scrollIntoView = function (el) {
  if (el.scrollIntoView) {
    el.scrollIntoView({ behavior: "instant", block: "end", inline: "nearest" });
  }
};

/**
 * Ascertains whether <var>obj</var> is a DOM-, SVG-, or XUL element.
 *
 * @param {object} obj
 *     Object thought to be an <code>Element</code> or
 *     <code>XULElement</code>.
 *
 * @returns {boolean}
 *     True if <var>obj</var> is an element, false otherwise.
 */
dom.isElement = function (obj) {
  return dom.isDOMElement(obj) || dom.isXULElement(obj);
};

dom.isEnabled = function (el) {
  let enabled = false;

  if (el.ownerDocument.contentType !== "text/xml") {
    enabled = !dom.isDisabled(el);
  }

  return enabled;
};

/**
 * Returns the shadow root of an element.
 *
 * @param {Element} el
 *     Element thought to have a <code>shadowRoot</code>
 * @returns {ShadowRoot}
 *     Shadow root of the element.
 */
dom.getShadowRoot = function (el) {
  const shadowRoot = el.openOrClosedShadowRoot;
  if (!shadowRoot || shadowRoot.isUAWidget()) {
    throw new lazy.error.NoSuchShadowRootError();
  }
  return shadowRoot;
};

/**
 * Ascertains whether <var>node</var> is a shadow root.
 *
 * @param {ShadowRoot} node
 *   The node that will be checked to see if it has a shadow root
 *
 * @returns {boolean}
 *     True if <var>node</var> is a shadow root, false otherwise.
 */
dom.isShadowRoot = function (node) {
  return (
    node &&
    node.nodeType === DOCUMENT_FRAGMENT_NODE &&
    node.containingShadowRoot == node
  );
};

/**
 * Ascertains whether <var>obj</var> is a DOM element.
 *
 * @param {object} obj
 *     Object to check.
 *
 * @returns {boolean}
 *     True if <var>obj</var> is a DOM element, false otherwise.
 */
dom.isDOMElement = function (obj) {
  return obj && obj.nodeType == ELEMENT_NODE && !dom.isXULElement(obj);
};

/**
 * Ascertains whether <var>obj</var> is a XUL element.
 *
 * @param {object} obj
 *     Object to check.
 *
 * @returns {boolean}
 *     True if <var>obj</var> is a XULElement, false otherwise.
 */
dom.isXULElement = function (obj) {
  return obj && obj.nodeType === ELEMENT_NODE && obj.namespaceURI === XUL_NS;
};

/**
 * Ascertains whether <var>node</var> is in a privileged document.
 *
 * @param {Node} node
 *     Node to check.
 *
 * @returns {boolean}
 *     True if <var>node</var> is in a privileged document,
 *     false otherwise.
 */
dom.isInPrivilegedDocument = function (node) {
  return !!node?.nodePrincipal?.isSystemPrincipal;
};

/**
 * Ascertains whether <var>obj</var> is a <code>WindowProxy</code>.
 *
 * @param {object} obj
 *     Object to check.
 *
 * @returns {boolean}
 *     True if <var>obj</var> is a DOM window.
 */
dom.isDOMWindow = function (obj) {
  // TODO(ato): This should use Object.prototype.toString.call(node)
  // but it's not clear how to write a good xpcshell test for that,
  // seeing as we stub out a WindowProxy.
  return (
    typeof obj == "object" &&
    obj !== null &&
    typeof obj.toString == "function" &&
    obj.toString() == "[object Window]" &&
    obj.self === obj
  );
};

const boolEls = {
  audio: ["autoplay", "controls", "loop", "muted"],
  button: ["autofocus", "disabled", "formnovalidate"],
  details: ["open"],
  dialog: ["open"],
  fieldset: ["disabled"],
  form: ["novalidate"],
  iframe: ["allowfullscreen"],
  img: ["ismap"],
  input: [
    "autofocus",
    "checked",
    "disabled",
    "formnovalidate",
    "multiple",
    "readonly",
    "required",
  ],
  keygen: ["autofocus", "disabled"],
  menuitem: ["checked", "default", "disabled"],
  ol: ["reversed"],
  optgroup: ["disabled"],
  option: ["disabled", "selected"],
  script: ["async", "defer"],
  select: ["autofocus", "disabled", "multiple", "required"],
  textarea: ["autofocus", "disabled", "readonly", "required"],
  track: ["default"],
  video: ["autoplay", "controls", "loop", "muted"],
};

/**
 * Tests if the attribute is a boolean attribute on element.
 *
 * @param {Element} el
 *     Element to test if <var>attr</var> is a boolean attribute on.
 * @param {string} attr
 *     Attribute to test is a boolean attribute.
 *
 * @returns {boolean}
 *     True if the attribute is boolean, false otherwise.
 */
dom.isBooleanAttribute = function (el, attr) {
  if (!dom.isDOMElement(el)) {
    return false;
  }

  // global boolean attributes that apply to all HTML elements,
  // except for custom elements
  const customElement = !el.localName.includes("-");
  if ((attr == "hidden" || attr == "itemscope") && customElement) {
    return true;
  }

  if (!boolEls.hasOwnProperty(el.localName)) {
    return false;
  }
  return boolEls[el.localName].includes(attr);
};

/**
 * Get the position and dimensions of the element.
 *
 * @param {Element} el
 *     Element to get the dimensions of.
 *
 * @returns {DOMRect}
 *     A DOMRect describing the element's position relative to the document.
 */
dom.getElementRect = function (el) {
  const win = el.documentGlobal;
  const rect = el.getBoundingClientRect();

  return new DOMRect(
    rect.x + win.pageXOffset,
    rect.y + win.pageYOffset,
    rect.width,
    rect.height
  );
};
