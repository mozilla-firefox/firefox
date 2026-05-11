/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://dom.spec.whatwg.org/#interface-element
 * https://domparsing.spec.whatwg.org/
 * https://drafts.csswg.org/cssom-view/
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

interface nsIScreen;

[Exposed=Window]
interface Element : Node {
  [Constant]
  readonly attribute DOMString? namespaceURI;
  [Constant]
  readonly attribute DOMString? prefix;
  [Constant]
  readonly attribute DOMString localName;

  // Not [Constant] because it depends on which document we're in
  [Pure]
  readonly attribute DOMString tagName;

  [CEReactions, Pure]
           attribute DOMString id;
  [CEReactions, Pure]
           attribute DOMString className;
  [Constant, PutForwards=value]
  readonly attribute DOMTokenList classList;

  // https://drafts.csswg.org/css-shadow-parts/#idl
  [SameObject, PutForwards=value]
  readonly attribute DOMTokenList part;

  [SameObject]
  readonly attribute NamedNodeMap attributes;
  [Pure]
  sequence<DOMString> getAttributeNames();
  [Pure]
  DOMString? getAttribute(DOMString name);
  [Pure]
  DOMString? getAttributeNS(DOMString? namespace, DOMString localName);
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  boolean toggleAttribute(DOMString name, optional boolean force);
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  undefined setAttribute(DOMString name, (TrustedType or DOMString) value);
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  undefined setAttributeNS(DOMString? namespace, DOMString name, (TrustedType or DOMString) value);
  [CEReactions, Throws]
  undefined removeAttribute(DOMString name);
  [CEReactions, Throws]
  undefined removeAttributeNS(DOMString? namespace, DOMString localName);
  [Pure]
  boolean hasAttribute(DOMString name);
  [Pure]
  boolean hasAttributeNS(DOMString? namespace, DOMString localName);
  [Pure]
  boolean hasAttributes();

  [Throws, Pure]
  Element? closest(UTF8String selector);

  [Throws, Pure]
  boolean matches(UTF8String selector);
  [Throws, Pure, BinaryName="matches"]
  boolean webkitMatchesSelector(UTF8String selector);

  [Pure]
  HTMLCollection getElementsByTagName(DOMString localName);
  [Throws, Pure]
  HTMLCollection getElementsByTagNameNS(DOMString? namespace, DOMString localName);
  [Pure]
  HTMLCollection getElementsByClassName(DOMString classNames);

  [CEReactions, Throws]
  Element? insertAdjacentElement(DOMString where, Element element); // historical

  [Throws]
  undefined insertAdjacentText(DOMString where, DOMString data); // historical

  /**
   * The ratio of font-size-inflated text font size to computed font
   * size for this element. This will query the element for its primary frame,
   * and then use this to get font size inflation information about the frame.
   * This will be 1.0 if font size inflation is not enabled, and -1.0 if an
   * error occurred during the retrieval of the font size inflation.
   *
   * @note The font size inflation ratio that is returned is actually the
   *       font size inflation data for the element's _primary frame_, not the
   *       element itself, but for most purposes, this should be sufficient.
   */
  [ChromeOnly]
  readonly attribute float fontSizeInflation;

  /**
   * Returns the pseudo-element string if this element represents a
   * pseudo-element, or null otherwise.
   */
  [ChromeOnly]
  readonly attribute DOMString? implementedPseudoElement;

  // Selectors API
  /**
   * Returns whether this element would be selected by the given selector
   * string.
   *
   * https://dom.spec.whatwg.org/#dom-element-matches
   */
  [Throws, Pure, BinaryName="matches"]
  boolean mozMatchesSelector(UTF8String selector);

  // Pointer events methods.
  [UseCounter, Throws]
  undefined setPointerCapture(long pointerId);
  [UseCounter, Throws]
  undefined releasePointerCapture(long pointerId);
  boolean hasPointerCapture(long pointerId);

  // Proprietary extensions
  /**
   * Set this during a mousedown event to grab and retarget all mouse events
   * to this element until the mouse button is released or releaseCapture is
   * called. If retargetToElement is true, then all events are targetted at
   * this element. If false, events can also fire at descendants of this
   * element.
   *
   */
  [Deprecated=ElementSetCapture, Pref="dom.mouse_capture.enabled"]
  undefined setCapture(optional boolean retargetToElement = false);

  /**
   * If this element has captured the mouse, release the capture. If another
   * element has captured the mouse, this method has no effect.
   */
  [Deprecated=ElementReleaseCapture, Pref="dom.mouse_capture.enabled"]
  undefined releaseCapture();

  /*
   * Chrome-only version of setCapture that works outside of a mousedown event.
   */
  [ChromeOnly]
  undefined setCaptureAlways(optional boolean retargetToElement = false);

  // Mozilla extensions

  // Obsolete methods.
  Attr? getAttributeNode(DOMString name);
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  Attr? setAttributeNode(Attr newAttr);
  [CEReactions, Throws]
  Attr? removeAttributeNode(Attr oldAttr);
  Attr? getAttributeNodeNS(DOMString? namespaceURI, DOMString localName);
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  Attr? setAttributeNodeNS(Attr newAttr);

  [Func="nsContentUtils::IsCallerChromeOrElementTransformGettersEnabled"]
  DOMMatrixReadOnly getTransformToAncestor(Element ancestor);
  [Func="nsContentUtils::IsCallerChromeOrElementTransformGettersEnabled"]
  DOMMatrixReadOnly getTransformToParent();
  [Func="nsContentUtils::IsCallerChromeOrElementTransformGettersEnabled"]
  DOMMatrixReadOnly getTransformToViewport();
};

// https://html.spec.whatwg.org/#focus-management-apis
dictionary FocusOptions {
  boolean preventScroll = false;
  boolean focusVisible;
};

// https://html.spec.whatwg.org/multipage/dom.html#htmlorsvgelement
interface mixin HTMLOrSVGOrMathMLElement {
  [SameObject] readonly attribute DOMStringMap dataset;
  // See bug 1389421
  // attribute DOMString nonce; // intentionally no [CEReactions]

  [CEReactions, SetterThrows, Pure] attribute boolean autofocus;
  [CEReactions, SetterThrows, Pure] attribute long tabIndex;
  [Throws, NeedsCallerType] undefined focus(optional FocusOptions options = {});
  [Throws] undefined blur();
};

// https://drafts.csswg.org/cssom/#the-elementcssinlinestyle-mixin
interface mixin ElementCSSInlineStyle {
  [SameObject, PutForwards=cssText]
  readonly attribute CSSStyleProperties style;
};

// https://drafts.csswg.org/cssom-view/
enum ScrollLogicalPosition { "start", "center", "end", "nearest", "auto" };
dictionary ScrollIntoViewOptions : ScrollOptions {
  ScrollLogicalPosition block = "auto";
  ScrollLogicalPosition inline = "auto";
};

dictionary CheckVisibilityOptions {
  boolean checkOpacity = false;
  boolean checkVisibilityCSS = false;
  boolean contentVisibilityAuto = false;
  boolean opacityProperty = false;
  boolean visibilityProperty = false;
  [ChromeOnly] boolean flush = true;
};

// https://drafts.csswg.org/cssom-view/#extensions-to-the-element-interface
partial interface Element {
  DOMRectList getClientRects();
  DOMRect getBoundingClientRect();

  boolean checkVisibility(optional CheckVisibilityOptions options = {});

  // scrolling
  undefined scrollIntoView(optional (boolean or ScrollIntoViewOptions) arg = {});
  // None of the CSSOM attributes are [Pure], because they flush
           attribute unrestricted double scrollTop;   // scroll on setting
           attribute unrestricted double scrollLeft;  // scroll on setting

  // FIXME(emilio): A bit weird that these are long instead of doubles, see:
  // https://lists.w3.org/Archives/Public/www-style/2015Feb/0195.html
  // https://github.com/w3c/csswg-drafts/issues/5260
  readonly attribute long scrollWidth;
  readonly attribute long scrollHeight;

  [BinaryName="scrollTo"]
  undefined scroll(unrestricted double x, unrestricted double y);
  [BinaryName="scrollTo"]
  undefined scroll(optional ScrollToOptions options = {});
  undefined scrollTo(unrestricted double x, unrestricted double y);
  undefined scrollTo(optional ScrollToOptions options = {});
  undefined scrollBy(unrestricted double x, unrestricted double y);
  undefined scrollBy(optional ScrollToOptions options = {});
  // mozScrollSnap is used by chrome to perform scroll snapping after the
  // user performs actions that may affect scroll position
  // mozScrollSnap is deprecated, to be replaced by a web accessible API, such
  // as an extension to the ScrollOptions dictionary.  See bug 1137937.
  [ChromeOnly] undefined mozScrollSnap();

  readonly attribute long clientTop;
  readonly attribute long clientLeft;
  readonly attribute long clientWidth;
  readonly attribute long clientHeight;

  // Return the screen coordinates of the element, in CSS pixels relative to
  // the window's screen.
  [ChromeOnly] readonly attribute long screenX;
  [ChromeOnly] readonly attribute long screenY;
  [ChromeOnly] readonly attribute nsIScreen? screen;

  // Mozilla specific stuff
  /* The minimum/maximum offset that the element can be scrolled to
     (i.e., the value that scrollLeft/scrollTop would be clamped to if they were
     set to arbitrarily large values. */
  [ChromeOnly] readonly attribute unrestricted double scrollTopMin;
               readonly attribute unrestricted double scrollTopMax;
  [ChromeOnly] readonly attribute unrestricted double scrollLeftMin;
               readonly attribute unrestricted double scrollLeftMax;

  [Pref="layout.css.zoom.enabled"] readonly attribute double currentCSSZoom;
};

// https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#dom-parsing-and-serialization
partial interface Element {
  [CEReactions, SetterNeedsSubjectPrincipal=NonSystem, Pure, SetterThrows, GetterCanOOM]
  attribute (TrustedHTML or [LegacyNullToEmptyString] DOMString) innerHTML;
  [CEReactions, SetterNeedsSubjectPrincipal=NonSystem, Pure, SetterThrows]
  attribute (TrustedHTML or [LegacyNullToEmptyString] DOMString) outerHTML;
  [CEReactions, NeedsSubjectPrincipal=NonSystem, Throws]
  undefined insertAdjacentHTML(DOMString position, (TrustedHTML or DOMString) text);
};

// https://dom.spec.whatwg.org/#dictdef-shadowrootinit
dictionary ShadowRootInit {
  required ShadowRootMode mode;
  boolean delegatesFocus = false;
  SlotAssignmentMode slotAssignment = "named";
  boolean clonable = false;
  boolean serializable = false;

  // https://github.com/whatwg/dom/pull/1353
  [Pref="dom.shadowdom.referenceTarget.enabled"]
  DOMString? referenceTarget;
};

// https://dom.spec.whatwg.org/#element
partial interface Element {
  // Shadow DOM v1
  [Throws, UseCounter]
  ShadowRoot attachShadow(ShadowRootInit shadowRootInitDict);
  [BinaryName="shadowRootForBindings"]
  readonly attribute ShadowRoot? shadowRoot;

  [NeedsSubjectPrincipal, Func="Document::IsCallerChromeOrAddon"]
  readonly attribute ShadowRoot? openOrClosedShadowRoot;

  [BinaryName="assignedSlotByMode"]
  readonly attribute HTMLSlotElement? assignedSlot;

  [ChromeOnly, BinaryName="assignedSlot"]
  readonly attribute HTMLSlotElement? openOrClosedAssignedSlot;

  [CEReactions, Unscopable, SetterThrows]
           attribute DOMString slot;

  [Pref="dom.scoped-custom-element-registries.enabled"]
  readonly attribute CustomElementRegistry? customElementRegistry;
};

Element includes ChildNode;
Element includes NonDocumentTypeChildNode;
Element includes ParentNode;
Element includes Animatable;
Element includes GeometryUtils;
Element includes ARIAMixin;
Element includes ARIANotifyMixin;

// Fullscreen Keyboard Lock API
// See https://github.com/whatwg/fullscreen/issues/231
// and https://github.com/whatwg/fullscreen/pull/232
// for details of the as-of-yet unspecified API.
enum FullscreenKeyboardLock {
  "none",
  "browser",
};

// https://fullscreen.spec.whatwg.org/#dictdef-fullscreenoptions
dictionary FullscreenOptions {
  [Pref="dom.fullscreen.keyboard_lock.enabled"]
  FullscreenKeyboardLock keyboardLock = "none";
};

// https://fullscreen.spec.whatwg.org/#api
partial interface Element {
  [NewObject, NeedsCallerType, UseCounter]
  Promise<undefined> requestFullscreen(optional FullscreenOptions options = {});
  [NewObject, BinaryName="requestFullscreen", NeedsCallerType, Deprecated="MozRequestFullScreenDeprecatedPrefix"]
  Promise<undefined> mozRequestFullScreen(optional FullscreenOptions options = {});

  // Events handlers
  attribute EventHandler onfullscreenchange;
  attribute EventHandler onfullscreenerror;
};

// https://w3c.github.io/pointerlock/#pointerlockoptions-dictionary
dictionary PointerLockOptions {
  [Pref="dom.pointer-lock.unadjusted-movement.enabled"]
  boolean unadjustedMovement = false;
};

// https://w3c.github.io/pointerlock/#extensions-to-the-element-interface
partial interface Element {
  [NewObject, NeedsCallerType, Pref="dom.pointer-lock.enabled"]
  Promise<undefined> requestPointerLock(optional PointerLockOptions options = {});
};

// Mozilla-specific additions to support devtools
partial interface Element {
  // Support reporting of Flexbox properties
  /**
   * If this element has a display:flex or display:inline-flex style,
   * this property returns an object with computed values for flex
   * properties, as well as a property that exposes the flex lines
   * in this container.
   */
  [ChromeOnly, Pure]
  Flex? getAsFlexContainer();

  // Support reporting of Grid properties
  /**
   * If this element has a display:grid or display:inline-grid style,
   * this property returns an object with computed values for grid
   * tracks and lines.
   */
  [ChromeOnly, Pure]
  sequence<Grid> getGridFragments();

  /**
   * Returns whether there are any grid fragments on this element.
   */
  [ChromeOnly, Pure]
  boolean hasGridFragments();

  /**
   * Returns a sequence of all the descendent elements of this element
   * that have display:grid or display:inline-grid style and generate
   * a frame.
   */
  [ChromeOnly, Pure]
  sequence<Element> getElementsWithGrid();

  /**
   * Set attribute on the Element with a customized Content-Security-Policy
   * appropriate to devtools, which includes:
   * style-src 'unsafe-inline'
   */
  [ChromeOnly, CEReactions, Throws]
  undefined setAttributeDevtools(DOMString name, DOMString value);
  [ChromeOnly, CEReactions, Throws]
  undefined setAttributeDevtoolsNS(DOMString? namespace, DOMString name, DOMString value);

  /**
   * Provide a direct way to determine if this Element has visible
   * scrollbars. Flushes layout.
   */
  [ChromeOnly]
  readonly attribute boolean hasVisibleScrollbars;
};

// These variables are used in vtt.js, they are used for positioning vtt cues.
partial interface Element {
  // These two attributes are a double version of the clientHeight and the
  // clientWidth.
  [ChromeOnly]
  readonly attribute double clientHeightDouble;
  [ChromeOnly]
  readonly attribute double clientWidthDouble;
  // This attribute returns the block size of the first line box under the different
  // writing directions. If the direction is horizontal, it represents box's
  // height. If the direction is vertical, it represents box's width.
  [ChromeOnly]
  readonly attribute double firstLineBoxBSize;
};


// Sanitizer API, https://wicg.github.io/sanitizer-api/
partial interface Element {
  [CEReactions, UseCounter, Throws, Pref="dom.security.sanitizer.enabled"]
  undefined setHTML(DOMString aInnerHTML, optional SetHTMLOptions options = {});
};

dictionary GetHTMLOptions {
  boolean serializableShadowRoots = false;
  sequence<ShadowRoot> shadowRoots = [];
};

partial interface Element {
  // https://html.spec.whatwg.org/#dom-element-sethtmlunsafe
  [NeedsSubjectPrincipal=NonSystem, Throws]
  undefined setHTMLUnsafe((TrustedHTML or DOMString) html, optional SetHTMLUnsafeOptions options = {});
  DOMString getHTML(optional GetHTMLOptions options = {});
};

// https://w3c.github.io/trusted-types/dist/spec/#integrations
typedef (TrustedHTML or TrustedScript or TrustedScriptURL) TrustedType;

// https://drafts.css-houdini.org/css-typed-om-1/#computed-stylepropertymapreadonly-objects
partial interface Element {
    [Pref="layout.css.typed-om.enabled"]
    StylePropertyMapReadOnly computedStyleMap();
};

// https://drafts.css-houdini.org/css-typed-om-1/#declared-stylepropertymap-objects
partial interface mixin ElementCSSInlineStyle {
  [SameObject, Pref="layout.css.typed-om.enabled"] readonly attribute StylePropertyMap attributeStyleMap;
};
