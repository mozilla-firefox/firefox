/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file defines these globals on the window object.
// Define them here so that ESLint can find them:
/* globals MozXULElement, MozHTMLElement, MozElements */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
(() => {
  // Handle customElements.js being loaded as a script in addition to the subscriptLoader
  // from MainProcessSingleton, to handle pages that can open both before and after
  // MainProcessSingleton starts. See Bug 1501845.
  if (window.MozXULElement) {
    return;
  }

  const MozElements = {};
  window.MozElements = MozElements;

  const { AppConstants } = ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs"
  );
  const instrumentClasses = Services.env.get("MOZ_INSTRUMENT_CUSTOM_ELEMENTS");
  const instrumentedClasses = instrumentClasses ? new Set() : null;
  const instrumentedBaseClasses = instrumentClasses ? new WeakSet() : null;

  // If requested, wrap the normal customElements.define to give us a chance
  // to modify the class so we can instrument function calls in local development:
  if (instrumentClasses) {
    let define = window.customElements.define;
    window.customElements.define = function (name, c, opts) {
      instrumentCustomElementClass(c);
      return define.call(this, name, c, opts);
    };
    window.addEventListener(
      "load",
      () => {
        MozElements.printInstrumentation(true);
      },
      { once: true, capture: true }
    );
  }

  MozElements.printInstrumentation = function (collapsed) {
    let summaries = [];
    let totalCalls = 0;
    let totalTime = 0;
    for (let c of instrumentedClasses) {
      // Allow passing in something like MOZ_INSTRUMENT_CUSTOM_ELEMENTS=MozXULElement,Button to filter
      let includeClass =
        instrumentClasses == 1 ||
        instrumentClasses
          .split(",")
          .some(n => c.name.toLowerCase().includes(n.toLowerCase()));
      let summary = c.__instrumentation_summary;
      if (includeClass && summary) {
        summaries.push(summary);
        totalCalls += summary.totalCalls;
        totalTime += summary.totalTime;
      }
    }
    if (summaries.length) {
      let groupName = `Instrumentation data for custom elements in ${document.documentURI}`;
      console[collapsed ? "groupCollapsed" : "group"](groupName);
      console.log(
        `Total function calls ${totalCalls} and total time spent inside ${totalTime.toFixed(
          2
        )}`
      );
      for (let summary of summaries) {
        console.log(`${summary.name} (# instances: ${summary.instances})`);
        if (Object.keys(summary.data).length > 1) {
          console.table(summary.data);
        }
      }
      console.groupEnd(groupName);
    }
  };

  function instrumentCustomElementClass(c) {
    // Climb up prototype chain to see if we inherit from a MozElement.
    // Keep track of classes to instrument, for example:
    //   MozMenuCaption->MozMenuBase->BaseText->BaseControl->MozXULElement
    let inheritsFromBase = instrumentedBaseClasses.has(c);
    let classesToInstrument = [c];
    let proto = Object.getPrototypeOf(c);
    while (proto) {
      classesToInstrument.push(proto);
      if (instrumentedBaseClasses.has(proto)) {
        inheritsFromBase = true;
        break;
      }
      proto = Object.getPrototypeOf(proto);
    }

    if (inheritsFromBase) {
      for (let c of classesToInstrument.reverse()) {
        instrumentIndividualClass(c);
      }
    }
  }

  function instrumentIndividualClass(c) {
    if (instrumentedClasses.has(c)) {
      return;
    }

    instrumentedClasses.add(c);
    let data = { instances: 0 };

    function wrapFunction(name, fn) {
      return function () {
        if (!data[name]) {
          data[name] = { time: 0, calls: 0 };
        }
        data[name].calls++;
        let n = performance.now();
        let r = fn.apply(this, arguments);
        data[name].time += performance.now() - n;
        return r;
      };
    }
    function wrapPropertyDescriptor(obj, name) {
      if (name == "constructor") {
        return;
      }
      let prop = Object.getOwnPropertyDescriptor(obj, name);
      if (prop.get) {
        prop.get = wrapFunction(`<get> ${name}`, prop.get);
      }
      if (prop.set) {
        prop.set = wrapFunction(`<set> ${name}`, prop.set);
      }
      if (prop.writable && prop.value && prop.value.apply) {
        prop.value = wrapFunction(name, prop.value);
      }
      Object.defineProperty(obj, name, prop);
    }

    // Handle static properties
    for (let name of Object.getOwnPropertyNames(c)) {
      wrapPropertyDescriptor(c, name);
    }

    // Handle instance properties
    for (let name of Object.getOwnPropertyNames(c.prototype)) {
      wrapPropertyDescriptor(c.prototype, name);
    }

    c.__instrumentation_data = data;
    Object.defineProperty(c, "__instrumentation_summary", {
      enumerable: false,
      configurable: false,
      get() {
        if (data.instances == 0) {
          return null;
        }

        let clonedData = JSON.parse(JSON.stringify(data));
        delete clonedData.instances;
        let totalCalls = 0;
        let totalTime = 0;
        for (let d in clonedData) {
          let { time, calls } = clonedData[d];
          time = parseFloat(time.toFixed(2));
          totalCalls += calls;
          totalTime += time;
          clonedData[d]["time (ms)"] = time;
          delete clonedData[d].time;
          clonedData[d].timePerCall = parseFloat((time / calls).toFixed(4));
        }

        let timePerCall = parseFloat((totalTime / totalCalls).toFixed(4));
        totalTime = parseFloat(totalTime.toFixed(2));

        // Add a spaced-out final row with summed up totals
        clonedData["\ntotals"] = {
          "time (ms)": `\n${totalTime}`,
          calls: `\n${totalCalls}`,
          timePerCall: `\n${timePerCall}`,
        };
        return {
          instances: data.instances,
          data: clonedData,
          name: c.name,
          totalCalls,
          totalTime,
        };
      },
    });
  }

  // The listener of DOMContentLoaded must be set on window, rather than
  // document, because the window can go away before the event is fired.
  // In that case, we don't want to initialize anything, otherwise we
  // may be leaking things because they will never be destroyed after.
  let gIsDOMContentLoaded = false;
  const gElementsPendingConnection = new Set();
  window.addEventListener(
    "DOMContentLoaded",
    () => {
      gIsDOMContentLoaded = true;
      for (let element of gElementsPendingConnection) {
        try {
          if (element.isConnected) {
            element.isRunningDelayedConnectedCallback = true;
            element.connectedCallback();
          }
        } catch (ex) {
          console.error(ex);
        }
        element.isRunningDelayedConnectedCallback = false;
      }
      gElementsPendingConnection.clear();
    },
    { once: true, capture: true }
  );

  const gXULDOMParser = new DOMParser();
  gXULDOMParser.forceEnableXULXBL();

  MozElements.MozElementMixin = Base => {
    let MozElementBase = class extends Base {
      constructor() {
        super();

        if (instrumentClasses) {
          let proto = this.constructor;
          while (proto && proto != Base) {
            proto.__instrumentation_data.instances++;
            proto = Object.getPrototypeOf(proto);
          }
        }
      }
      /**
       * A declarative way to wire up attribute inheritance and automatically generate
       * the `observedAttributes` getter.  For example, if you returned:
       *    {
       *      ".foo": "bar,baz=bat"
       *    }
       *
       * Then the base class will automatically return ["bar", "bat"] from `observedAttributes`,
       * and set up an `attributeChangedCallback` to pass those attributes down onto an element
       * matching the ".foo" selector.
       *
       * See the `inheritAttribute` function for more details on the attribute string format.
       *
       * @return {Object<string selector, string attributes>}
       */
      static get inheritedAttributes() {
        return null;
      }

      static get flippedInheritedAttributes() {
        // Have to be careful here, if a subclass overrides inheritedAttributes
        // and its parent class is instantiated first, then reading
        // this._flippedInheritedAttributes on the child class will return the
        // computed value from the parent.  We store it separately on each class
        // to ensure everything works correctly when inheritedAttributes is
        // overridden.
        if (!this.hasOwnProperty("_flippedInheritedAttributes")) {
          let { inheritedAttributes } = this;
          if (!inheritedAttributes) {
            this._flippedInheritedAttributes = null;
          } else {
            this._flippedInheritedAttributes = {};
            for (let selector in inheritedAttributes) {
              let attrRules = inheritedAttributes[selector].split(",");
              for (let attrRule of attrRules) {
                let attrName = attrRule;
                let attrNewName = attrRule;
                let split = attrName.split("=");
                if (split.length == 2) {
                  attrName = split[1];
                  attrNewName = split[0];
                }

                if (!this._flippedInheritedAttributes[attrName]) {
                  this._flippedInheritedAttributes[attrName] = [];
                }
                this._flippedInheritedAttributes[attrName].push([
                  selector,
                  attrNewName,
                ]);
              }
            }
          }
        }

        return this._flippedInheritedAttributes;
      }
      /*
       * Generate this array based on `inheritedAttributes`, if any. A class is free to override
       * this if it needs to do something more complex or wants to opt out of this behavior.
       */
      static get observedAttributes() {
        return Object.keys(this.flippedInheritedAttributes || {});
      }

      /*
       * Provide default lifecycle callback for attribute changes that will inherit attributes
       * based on the static `inheritedAttributes` Object. This can be overridden by callers.
       */
      attributeChangedCallback(name, oldValue, newValue) {
        if (oldValue === newValue || !this.initializedAttributeInheritance) {
          return;
        }

        let list = this.constructor.flippedInheritedAttributes[name];
        if (list) {
          this.inheritAttribute(list, name);
        }
      }

      /*
       * After setting content, calling this will cache the elements from selectors in the
       * static `inheritedAttributes` Object. It'll also do an initial call to `this.inheritAttributes()`,
       * so in the simple case, this is the only function you need to call.
       *
       * This should be called any time the children that are inheriting attributes changes. For instance,
       * it's common in a connectedCallback to do something like:
       *
       *   this.textContent = "";
       *   this.append(MozXULElement.parseXULToFragment(`<label />`))
       *   this.initializeAttributeInheritance();
       *
       */
      initializeAttributeInheritance() {
        let { flippedInheritedAttributes } = this.constructor;
        if (!flippedInheritedAttributes) {
          return;
        }

        // Clear out any existing cached elements:
        this._inheritedElements = null;

        this.initializedAttributeInheritance = true;
        for (let attr in flippedInheritedAttributes) {
          if (this.hasAttribute(attr)) {
            this.inheritAttribute(flippedInheritedAttributes[attr], attr);
          }
        }
      }

      /**
       * Implements attribute value inheritance by child elements.
       *
       * @param {Array} list
       *        An array of (to-element-selector, to-attr) pairs.
       * @param {string} attr
       *        An attribute to propagate.
       */
      inheritAttribute(list, attr) {
        if (!this._inheritedElements) {
          this._inheritedElements = {};
        }

        let hasAttr = this.hasAttribute(attr);
        let attrValue = this.getAttribute(attr);

        for (let [selector, newAttr] of list) {
          if (!(selector in this._inheritedElements)) {
            this._inheritedElements[selector] =
              this.getElementForAttrInheritance(selector);
          }
          let el = this._inheritedElements[selector];
          if (el) {
            if (newAttr == "text") {
              el.textContent = hasAttr ? attrValue : "";
            } else if (hasAttr) {
              el.setAttribute(newAttr, attrValue);
            } else {
              el.removeAttribute(newAttr);
            }
          }
        }
      }

      /**
       * Used in setting up attribute inheritance. Takes a selector and returns
       * an element for that selector from shadow DOM if there is a shadowRoot,
       * or from the light DOM if not.
       *
       * Here's one problem this solves. ElementB extends ElementA which extends
       * MozXULElement. ElementA has a shadowRoot. ElementB tries to inherit
       * attributes in light DOM by calling `initializeAttributeInheritance`
       * but that fails because it defaults to inheriting from the shadow DOM
       * and not the light DOM. (See bug 1545824.)
       *
       * To solve this, ElementB can override `getElementForAttrInheritance` so
       * it queries the light DOM for some selectors as needed. For example:
       *
       *  class ElementA extends MozXULElement {
       *    static get inheritedAttributes() {
       *      return { ".one": "attr" };
       *    }
       *  }
       *
       *  class ElementB extends customElements.get("elementa") {
       *    static get inheritedAttributes() {
       *      return Object.assign({}, super.inheritedAttributes(), {
       *        ".two": "attr",
       *      });
       *    }
       *    getElementForAttrInheritance(selector) {
       *      if (selector == ".two") {
       *        return this.querySelector(selector)
       *      } else {
       *        return super.getElementForAttrInheritance(selector);
       *      }
       *    }
       *  }
       *
       * @param {string} selector
       *        A selector used to query an element.
       *
       * @return {Element} The element found by the selector.
       */
      getElementForAttrInheritance(selector) {
        let parent = this.shadowRoot || this;
        return parent.querySelector(selector);
      }

      /**
       * Sometimes an element may not want to run connectedCallback logic during
       * parse. This could be because we don't want to initialize the element before
       * the element's contents have been fully parsed, or for performance reasons.
       * If you'd like to opt-in to this, then add this to the beginning of your
       * `connectedCallback` and `disconnectedCallback`:
       *
       *    if (this.delayConnectedCallback()) { return }
       *
       * And this at the beginning of your `attributeChangedCallback`
       *
       *    if (!this.isConnectedAndReady) { return; }
       */
      delayConnectedCallback() {
        if (gIsDOMContentLoaded) {
          return false;
        }
        gElementsPendingConnection.add(this);
        return true;
      }

      get isConnectedAndReady() {
        return gIsDOMContentLoaded && this.isConnected;
      }

      /**
       * Passes DOM events to the on_<event type> methods.
       */
      handleEvent(event) {
        let methodName = "on_" + event.type;
        if (methodName in this) {
          this[methodName](event);
        } else {
          throw new Error("Unrecognized event: " + event.type);
        }
      }

      /**
       * Used by custom elements for caching fragments. We now would be
       * caching once per class while also supporting subclasses.
       *
       * If available, returns the cached fragment.
       * Otherwise, creates it.
       *
       * Example:
       *
       *  class ElementA extends MozXULElement {
       *    static get markup() {
       *      return `<hbox class="example"`;
       *    }
       *
       *    connectedCallback() {
       *      this.appendChild(this.constructor.fragment);
       *    }
       *  }
       *
       * @return {importedNode} The imported node that has not been
       * inserted into document tree.
       */
      static get fragment() {
        if (!this.hasOwnProperty("_fragment")) {
          let markup = this.markup;
          if (markup) {
            this._fragment = MozXULElement.parseXULToFragment(
              markup,
              this.entities
            );
          } else {
            throw new Error("Markup is null");
          }
        }
        return document.importNode(this._fragment, true);
      }

      /**
       * Allows eager deterministic construction of XUL elements with XBL attached, by
       * parsing an element tree and returning a DOM fragment to be inserted in the
       * document before any of the inner elements is referenced by JavaScript.
       *
       * This process is required instead of calling the createElement method directly
       * because bindings get attached when:
       *
       * 1. the node gets a layout frame constructed, or
       * 2. the node gets its JavaScript reflector created, if it's in the document,
       *
       * whichever happens first. The createElement method would return a JavaScript
       * reflector, but the element wouldn't be in the document, so the node wouldn't
       * get XBL attached. After that point, even if the node is inserted into a
       * document, it won't get XBL attached until either the frame is constructed or
       * the reflector is garbage collected and the element is touched again.
       *
       * @param {string} str
       *        String with the XML representation of XUL elements.
       * @param {string[]} [entities]
       *        An array of DTD URLs containing entity definitions.
       *
       * @return {DocumentFragment} `DocumentFragment` instance containing
       *         the corresponding element tree, including element nodes
       *         but excluding any text node.
       */
      static parseXULToFragment(str, entities = []) {
        let doc = gXULDOMParser.parseFromSafeString(
          `
      ${
        entities.length
          ? `<!DOCTYPE bindings [
        ${entities.reduce((preamble, url, index) => {
          return (
            preamble +
            `<!ENTITY % _dtd-${index} SYSTEM "${url}">
            %_dtd-${index};
            `
          );
        }, "")}
      ]>`
          : ""
      }
      <box xmlns="http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"
           xmlns:html="http://www.w3.org/1999/xhtml">
        ${str}
      </box>
    `,
          "application/xml"
        );

        if (doc.documentElement.localName === "parsererror") {
          throw new Error("not well-formed XML");
        }

        // The XUL/XBL parser is set to ignore all-whitespace nodes, whereas (X)HTML
        // does not do this. Most XUL code assumes that the whitespace has been
        // stripped out, so we simply remove all text nodes after using the parser.
        let nodeIterator = doc.createNodeIterator(doc, NodeFilter.SHOW_TEXT);
        let currentNode = nodeIterator.nextNode();
        while (currentNode) {
          // Remove whitespace-only nodes. Regex is taken from:
          // https://developer.mozilla.org/en-US/docs/Web/API/Document_Object_Model/Whitespace_in_the_DOM
          if (!/[^\t\n\r ]/.test(currentNode.textContent)) {
            currentNode.remove();
          }

          currentNode = nodeIterator.nextNode();
        }
        // We use a range here so that we don't access the inner DOM elements from
        // JavaScript before they are imported and inserted into a document.
        let range = doc.createRange();
        range.selectNodeContents(doc.querySelector("box"));
        return range.extractContents();
      }

      /**
       * Insert a localization link to an FTL file. This is used so that
       * a Custom Element can wait to inject the link until it's connected,
       * and so that consuming documents don't require the correct <link>
       * present in the markup.
       *
       * @param path
       *        The path to the FTL file
       */
      static insertFTLIfNeeded(path) {
        let container = document.head || document.querySelector("linkset");
        if (!container) {
          if (
            document.documentElement.namespaceURI ===
            "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"
          ) {
            container = document.createXULElement("linkset");
            document.documentElement.appendChild(container);
          } else if (document.documentURI == AppConstants.BROWSER_CHROME_URL) {
            // Special case for browser.xhtml. Here `document.head` is null, so
            // just insert the link at the end of the window.
            container = document.documentElement;
          } else {
            throw new Error(
              "Attempt to inject localization link before document.head is available"
            );
          }
        }

        for (let link of container.querySelectorAll("link")) {
          if (link.getAttribute("href") == path) {
            return;
          }
        }

        let link = document.createElementNS(
          "http://www.w3.org/1999/xhtml",
          "link"
        );
        link.setAttribute("rel", "localization");
        link.setAttribute("href", path);

        container.appendChild(link);
      }

      /**
       * Indicate that a class defining a XUL element implements one or more
       * XPCOM interfaces by adding a getCustomInterface implementation to it,
       * as well as an implementation of QueryInterface.
       *
       * The supplied class should implement the properties and methods of
       * all of the interfaces that are specified.
       *
       * @param cls
       *        The class that implements the interface.
       * @param names
       *        Array of interface names.
       */
      static implementCustomInterface(cls, ifaces) {
        if (cls.prototype.customInterfaces) {
          ifaces.push(...cls.prototype.customInterfaces);
        }
        cls.prototype.customInterfaces = ifaces;

        cls.prototype.QueryInterface = ChromeUtils.generateQI(ifaces);
        cls.prototype.getCustomInterfaceCallback =
          function getCustomInterfaceCallback(ifaceToCheck) {
            if (
              cls.prototype.customInterfaces.some(iface =>
                iface.equals(ifaceToCheck)
              )
            ) {
              return getInterfaceProxy(this);
            }
            return null;
          };
      }
    };

    // Rename the class so we can distinguish between MozXULElement and MozXULPopupElement, for example.
    Object.defineProperty(MozElementBase, "name", { value: `Moz${Base.name}` });
    if (instrumentedBaseClasses) {
      instrumentedBaseClasses.add(MozElementBase);
    }
    return MozElementBase;
  };

  const MozXULElement = MozElements.MozElementMixin(XULElement);
  const MozHTMLElement = MozElements.MozElementMixin(HTMLElement);

  /**
   * Given an object, add a proxy that reflects interface implementations
   * onto the object itself.
   */
  function getInterfaceProxy(obj) {
    /* globals MozQueryInterface */
    if (!obj._customInterfaceProxy) {
      obj._customInterfaceProxy = new Proxy(obj, {
        get(target, prop, receiver) {
          let propOrMethod = target[prop];
          if (typeof propOrMethod == "function") {
            if (MozQueryInterface.isInstance(propOrMethod)) {
              return Reflect.get(target, prop, receiver);
            }
            return function (...args) {
              return propOrMethod.apply(target, args);
            };
          }
          return propOrMethod;
        },
      });
    }

    return obj._customInterfaceProxy;
  }

  MozElements.BaseControlMixin = Base => {
    class BaseControl extends Base {
      get disabled() {
        return this.hasAttribute("disabled");
      }

      set disabled(val) {
        this.toggleAttribute("disabled", !!val);
      }

      get tabIndex() {
        return parseInt(this.getAttribute("tabindex")) || 0;
      }

      set tabIndex(val) {
        if (val) {
          this.setAttribute("tabindex", val);
        } else {
          this.removeAttribute("tabindex");
        }
      }
    }

    MozXULElement.implementCustomInterface(BaseControl, [
      Ci.nsIDOMXULControlElement,
    ]);
    return BaseControl;
  };
  MozElements.BaseControl = MozElements.BaseControlMixin(MozXULElement);

  const BaseTextMixin = Base =>
    class BaseText extends MozElements.BaseControlMixin(Base) {
      set label(val) {
        this.setAttribute("label", val);
      }

      get label() {
        return this.getAttribute("label") || "";
      }

      set image(val) {
        this.setAttribute("image", val);
      }

      get image() {
        return this.getAttribute("image");
      }

      set command(val) {
        this.setAttribute("command", val);
      }

      get command() {
        return this.getAttribute("command");
      }

      set accessKey(val) {
        // Always store on the control
        this.setAttribute("accesskey", val);
        // If there is a label, change the accesskey on the labelElement
        // if it's also set there
        if (this.labelElement) {
          this.labelElement.accessKey = val;
        }
      }

      get accessKey() {
        return this.labelElement?.accessKey || this.getAttribute("accesskey");
      }
    };
  MozElements.BaseTextMixin = BaseTextMixin;
  MozElements.BaseText = BaseTextMixin(MozXULElement);

  // The corner of the popup that an alignment position leaves nearest the
  // anchor, as [block keyword, is inline-start] pairs. Positions that anchor to
  // a point rather than to an edge are deliberately absent.
  const ALIGNMENT_CORNERS = {
    after_start: ["top", true],
    after_end: ["top", false],
    before_start: ["bottom", true],
    before_end: ["bottom", false],
    start_before: ["top", false],
    start_after: ["bottom", false],
    end_before: ["top", true],
    end_after: ["bottom", true],
  };

  /**
   * Reflects where an arrow popup sits relative to its anchor, as `side` and
   * `alignmentcorner`, when it receives popuppositioned.
   */
  MozElements.MozAnchoredPopupMixin = Base =>
    class MozAnchoredPopup extends Base {
      _setSideAttribute(event) {
        if (!event.isAnchored) {
          // Otherwise the popup would scale out of the corner it hung from the
          // last time it was opened anchored.
          this.removeAttribute("alignmentcorner");
          return;
        }

        let position = event.alignmentPosition;
        let isRTL = window.getComputedStyle(this).direction == "rtl";

        // The assigned side stays the same regardless of direction.
        if (position.startsWith("start_")) {
          this.setAttribute("side", isRTL ? "left" : "right");
        } else if (position.startsWith("end_")) {
          this.setAttribute("side", isRTL ? "right" : "left");
        } else if (position.startsWith("before_")) {
          this.setAttribute("side", "bottom");
        } else if (position.startsWith("after_")) {
          this.setAttribute("side", "top");
        }

        this.setPopupAlignmentCorner(position, isRTL);

        // This method isn't implemented here, but it can be added to
        // individual instances that need to show an arrow.
        this.setArrowPosition?.(event);
      }

      /**
       * Reflect the corner the popup hangs from, which popup.css scales it out
       * of as it opens. transform-origin takes no logical keywords, so the
       * corner is resolved to physical ones here, against the same direction
       * used to assign `side`, keeping the two consistent.
       *
       * A position that anchors to a point rather than to an edge leaves no
       * corner to hang from, and clears the attribute.
       *
       * @param {string} alignmentPosition - popuppositioned's alignmentPosition.
       * @param {boolean} isRTL - Whether the popup's direction is right to left.
       */
      setPopupAlignmentCorner(alignmentPosition, isRTL) {
        let corner = ALIGNMENT_CORNERS[alignmentPosition];
        if (!corner) {
          this.removeAttribute("alignmentcorner");
          return;
        }
        let [block, inlineStart] = corner;
        this.setAttribute(
          "alignmentcorner",
          block + (inlineStart != isRTL ? "left" : "right")
        );
      }
    };

  // Attach the base class to the window so other scripts can use it:
  window.MozXULElement = MozXULElement;
  window.MozHTMLElement = MozHTMLElement;

  customElements.setElementCreationCallback("browser", () => {
    ChromeUtils.importESModule(
      "chrome://global/content/elements/browser-custom-element.mjs",
      { global: "current" }
    );
  });

  // Skip loading any extra custom elements in the extension dummy document
  // and GeckoView windows.
  const loadExtraCustomElements = !(
    document.documentURI == "chrome://extensions/content/dummy.xhtml" ||
    document.documentURI == "chrome://geckoview/content/geckoview.xhtml"
  );
  if (loadExtraCustomElements) {
    // Lazily load the following elements
    for (let [tag, script] of [
      ["button-group", "chrome://global/content/elements/named-deck.js"],
      ["findbar", "chrome://global/content/elements/findbar.js"],
      ["menulist", "chrome://global/content/elements/menulist.js"],
      ["named-deck", "chrome://global/content/elements/named-deck.js"],
      ["named-deck-button", "chrome://global/content/elements/named-deck.js"],
      ["stringbundle", "chrome://global/content/elements/stringbundle.js"],
      [
        "printpreview-pagination",
        "chrome://global/content/printPreviewPagination.js",
      ],
      [
        "autocomplete-input",
        "chrome://global/content/elements/autocomplete-input.js",
      ],
      ["editor", "chrome://global/content/elements/editor.js"],
    ]) {
      customElements.setElementCreationCallback(tag, () => {
        Services.scriptloader.loadSubScript(script, window);
      });
    }

    let acornElements = [
      ["moz-badge", "chrome://global/content/elements/moz-badge.mjs"],
      ["moz-box-button", "chrome://global/content/elements/moz-box-button.mjs"],
      ["moz-box-group", "chrome://global/content/elements/moz-box-group.mjs"],
      ["moz-box-item", "chrome://global/content/elements/moz-box-item.mjs"],
      ["moz-box-link", "chrome://global/content/elements/moz-box-link.mjs"],
      [
        "moz-breadcrumb-group",
        "chrome://global/content/elements/moz-breadcrumb-group.mjs",
      ],
      ["moz-button", "chrome://global/content/elements/moz-button.mjs"],
      [
        "moz-button-group",
        "chrome://global/content/elements/moz-button-group.mjs",
      ],
      ["moz-card", "chrome://global/content/elements/moz-card.mjs"],
      ["moz-checkbox", "chrome://global/content/elements/moz-checkbox.mjs"],
      ["moz-fieldset", "chrome://global/content/elements/moz-fieldset.mjs"],
      ["moz-five-star", "chrome://global/content/elements/moz-five-star.mjs"],
      [
        "moz-input-color",
        "chrome://global/content/elements/moz-input-color.mjs",
      ],
      [
        "moz-input-email",
        "chrome://global/content/elements/moz-input-email.mjs",
      ],
      [
        "moz-input-folder",
        "chrome://global/content/elements/moz-input-folder.mjs",
      ],
      [
        "moz-input-number",
        "chrome://global/content/elements/moz-input-number.mjs",
      ],
      [
        "moz-input-password",
        "chrome://global/content/elements/moz-input-password.mjs",
      ],
      [
        "moz-input-search",
        "chrome://global/content/elements/moz-input-search.mjs",
      ],
      ["moz-input-tel", "chrome://global/content/elements/moz-input-tel.mjs"],
      ["moz-input-text", "chrome://global/content/elements/moz-input-text.mjs"],
      ["moz-input-url", "chrome://global/content/elements/moz-input-url.mjs"],
      ["moz-label", "chrome://global/content/elements/moz-label.mjs"],
      [
        "moz-message-bar",
        "chrome://global/content/elements/moz-message-bar.mjs",
      ],
      ["moz-option", "chrome://global/content/elements/moz-select.mjs"],
      [
        "moz-page-header",
        "chrome://global/content/elements/moz-page-header.mjs",
      ],
      ["moz-page-nav", "chrome://global/content/elements/moz-page-nav.mjs"],
      ["moz-promo", "chrome://global/content/elements/moz-promo.mjs"],
      ["moz-radio", "chrome://global/content/elements/moz-radio-group.mjs"],
      [
        "moz-radio-group",
        "chrome://global/content/elements/moz-radio-group.mjs",
      ],
      [
        "moz-reorderable-list",
        "chrome://global/content/elements/moz-reorderable-list.mjs",
      ],
      ["moz-select", "chrome://global/content/elements/moz-select.mjs"],
      [
        "moz-support-link",
        "chrome://global/content/elements/moz-support-link.mjs",
      ],
      ["moz-textarea", "chrome://global/content/elements/moz-textarea.mjs"],
      ["moz-toggle", "chrome://global/content/elements/moz-toggle.mjs"],
      [
        "moz-visual-picker",
        "chrome://global/content/elements/moz-visual-picker.mjs",
      ],
      [
        "moz-visual-picker-item",
        "chrome://global/content/elements/moz-visual-picker.mjs",
      ],
      [
        "moz-segmented-control",
        "chrome://global/content/elements/moz-segmented-control.mjs",
      ],
      ["panel-list", "chrome://global/content/elements/panel-list.mjs"],
      ["theme-picker", "chrome://global/content/elements/theme-picker.mjs"],
    ];
    document.addEventListener(
      "DOMContentLoaded",
      () => {
        // Only sync-import widgets once the document has loaded. If a widget is
        // used before DOMContentLoaded it will be imported and upgraded when
        // registering the customElements.setElementCreationCallback().
        for (let [tag, script] of acornElements) {
          if (!customElements.get(tag)) {
            customElements.setElementCreationCallback(
              tag,
              function customElementCreationCallback() {
                try {
                  ChromeUtils.importESModule(script, { global: "current" });
                } catch (e) {
                  // If the module is imported also by the regular module
                  // loader as part of <script> tags or the dynamic import
                  // before this point, and if the import failed due to, e.g.
                  // the channel get cancelled, the module record keeps the
                  // import error for the script URI, and
                  // ChromeUtils.importESModule fails to import the module.
                  //
                  // Given that the CustomElementRegistry expects the callback
                  // not to throw, and also it to define a custom element,
                  // report the error here and define an empty implementation,
                  // assuming the page is getting discarded anyway.
                  console.error("Failed to import custom element module", e);
                  customElements.define(tag, class extends MozHTMLElement {});
                }
              }
            );
          }
        }
      },
      { once: true }
    );

    // Immediately load the following elements
    for (let script of [
      "chrome://global/content/elements/arrowscrollbox.js",
      "chrome://global/content/elements/dialog.js",
      "chrome://global/content/elements/general.js",
      "chrome://global/content/elements/button.js",
      "chrome://global/content/elements/checkbox.js",
      "chrome://global/content/elements/menu.js",
      "chrome://global/content/elements/menupopup.js",
      "chrome://global/content/elements/notificationbox.js",
      "chrome://global/content/elements/panel.js",
      "chrome://global/content/elements/popupnotification.js",
      "chrome://global/content/elements/radio.js",
      "chrome://global/content/elements/richlistbox.js",
      "chrome://global/content/elements/autocomplete-popup.js",
      "chrome://global/content/elements/autocomplete-richlistitem.js",
      "chrome://global/content/elements/tabbox.js",
      "chrome://global/content/elements/text.js",
      "chrome://global/content/elements/toolbarbutton.js",
      "chrome://global/content/elements/tree.js",
      "chrome://global/content/elements/wizard.js",
    ]) {
      Services.scriptloader.loadSubScript(script, window);
    }
  }
})();
