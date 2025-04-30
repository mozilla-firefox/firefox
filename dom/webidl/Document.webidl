/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * https://dom.spec.whatwg.org/#interface-document
 * https://html.spec.whatwg.org/multipage/dom.html#the-document-object
 * https://html.spec.whatwg.org/multipage/obsolete.html#other-elements%2C-attributes-and-apis
 * https://fullscreen.spec.whatwg.org/#api
 * https://w3c.github.io/pointerlock/#extensions-to-the-document-interface
 * https://w3c.github.io/pointerlock/#extensions-to-the-documentorshadowroot-mixin
 * https://w3c.github.io/page-visibility/#extensions-to-the-document-interface
 * https://drafts.csswg.org/cssom/#extensions-to-the-document-interface
 * https://drafts.csswg.org/cssom-view/#extensions-to-the-document-interface
 * https://wicg.github.io/feature-policy/#policy
 * https://wicg.github.io/scroll-to-text-fragment/#feature-detectability
 */

interface ContentSecurityPolicy;
interface Principal;
interface WindowProxy;
interface nsISupports;
interface URI;
interface nsIDocShell;
interface nsILoadGroup;
interface nsIReferrerInfo;
interface nsICookieJarSettings;
interface nsIPermissionDelegateHandler;
interface XULCommandDispatcher;

enum VisibilityState { "hidden", "visible" };

/* https://dom.spec.whatwg.org/#dictdef-elementcreationoptions */
dictionary ElementCreationOptions {
  DOMString is;

  [ChromeOnly]
  DOMString pseudo;
};

/* https://dom.spec.whatwg.org/#interface-document */
[Exposed=Window]
interface Document : Node {
  [Throws]
  constructor();

  [Throws]
  readonly attribute DOMImplementation implementation;
  [Pure, Throws, BinaryName="documentURIFromJS", NeedsCallerType]
  readonly attribute DOMString URL;
  [Pure, Throws, BinaryName="documentURIFromJS", NeedsCallerType]
  readonly attribute DOMString documentURI;
  [Pure]
  readonly attribute DOMString compatMode;
  [Pure]
  readonly attribute DOMString characterSet;
  [Pure,BinaryName="characterSet"]
  readonly attribute DOMString charset; // legacy alias of .characterSet
  [Pure,BinaryName="characterSet"]
  readonly attribute DOMString inputEncoding; // legacy alias of .characterSet
  [Pure]
  readonly attribute DOMString contentType;

  [Pure]
  readonly attribute DocumentType? doctype;
  [Pure]
  readonly attribute Element? documentElement;
  [Pure]
  HTMLCollection getElementsByTagName(DOMString localName);
  [Pure, Throws]
  HTMLCollection getElementsByTagNameNS(DOMString? namespace, DOMString localName);
  [Pure]
  HTMLCollection getElementsByClassName(DOMString classNames);

  // These DOM methods cannot be accessed by UA Widget scripts
  // because the DOM element reflectors will be in the content scope,
  // instead of the desired UA Widget scope.
  [CEReactions, NewObject, Throws, Func="IsNotUAWidget"]
  Element createElement(DOMString localName, optional (ElementCreationOptions or DOMString) options = {});
  [CEReactions, NewObject, Throws, Func="IsNotUAWidget"]
  Element createElementNS(DOMString? namespace, DOMString qualifiedName, optional (ElementCreationOptions or DOMString) options = {});
  [NewObject]
  DocumentFragment createDocumentFragment();
  [NewObject, Func="IsNotUAWidget"]
  Text createTextNode(DOMString data);
  [NewObject, Func="IsNotUAWidget"]
  Comment createComment(DOMString data);
  [NewObject, Throws]
  ProcessingInstruction createProcessingInstruction(DOMString target, DOMString data);

  [CEReactions, Throws, Func="IsNotUAWidget"]
  Node importNode(Node node, optional boolean deep = false);
  [CEReactions, Throws, Func="IsNotUAWidget"]
  Node adoptNode(Node node);

  [NewObject, Throws, NeedsCallerType]
  Event createEvent(DOMString interface);

  [NewObject, Throws]
  Range createRange();

  // NodeFilter.SHOW_ALL = 0xFFFFFFFF
  [NewObject, Throws]
  NodeIterator createNodeIterator(Node root, optional unsigned long whatToShow = 0xFFFFFFFF, optional NodeFilter? filter = null);
  [NewObject, Throws]
  TreeWalker createTreeWalker(Node root, optional unsigned long whatToShow = 0xFFFFFFFF, optional NodeFilter? filter = null);

  // NEW
  // No support for prepend/append yet
  // undefined prepend((Node or DOMString)... nodes);
  // undefined append((Node or DOMString)... nodes);

  // These are not in the spec, but leave them for now for backwards compat.
  // So sort of like Gecko extensions
  [NewObject, Throws]
  CDATASection createCDATASection(DOMString data);
  [NewObject, Throws]
  Attr createAttribute(DOMString name);
  [NewObject, Throws]
  Attr createAttributeNS(DOMString? namespace, DOMString name);
};

// https://html.spec.whatwg.org/multipage/dom.html#the-document-object
partial interface Document {
  [Pref="dom.webcomponents.shadowdom.declarative.enabled", Throws]
  static Document parseHTMLUnsafe((TrustedHTML or DOMString) html);

  [PutForwards=href, LegacyUnforgeable] readonly attribute Location? location;
  [SetterThrows]                           attribute DOMString domain;
  readonly attribute UTF8String referrer;
  [Throws] attribute DOMString cookie;
  readonly attribute DOMString lastModified;
  readonly attribute DOMString readyState;

  // DOM tree accessors
  //(Not proxy yet)getter object (DOMString name);
  [CEReactions, SetterThrows, Pure]
           attribute DOMString title;
  [CEReactions, Pure]
           attribute DOMString dir;
  [CEReactions, Pure, SetterThrows]
           attribute HTMLElement? body;
  [Pure]
  readonly attribute HTMLHeadElement? head;
  [SameObject] readonly attribute HTMLCollection images;
  [SameObject] readonly attribute HTMLCollection embeds;
  [SameObject] readonly attribute HTMLCollection plugins;
  [SameObject] readonly attribute HTMLCollection links;
  [SameObject] readonly attribute HTMLCollection forms;
  [SameObject] readonly attribute HTMLCollection scripts;
  [Pure]
  NodeList getElementsByName(DOMString elementName);
  //(Not implemented)readonly attribute DOMElementMap cssElementMap;

  // dynamic markup insertion
  [CEReactions, Throws]
  Document open(optional DOMString unused1, optional DOMString unused2); // both arguments are ignored
  [CEReactions, Throws]
  WindowProxy? open(UTF8String url, DOMString name, DOMString features);
  [CEReactions, Throws]
  undefined close();
  [CEReactions, Throws]
  undefined write((TrustedHTML or DOMString)... text);
  [CEReactions, Throws]
  undefined writeln((TrustedHTML or DOMString)... text);

  // user interaction
  [Pure]
  readonly attribute WindowProxy? defaultView;
  [Throws]
  boolean hasFocus();
  [CEReactions, SetterThrows, SetterNeedsSubjectPrincipal]
           attribute DOMString designMode;
  [CEReactions, Throws, NeedsSubjectPrincipal]
  boolean execCommand(DOMString commandId, optional boolean showUI = false,
                      optional (TrustedHTML or DOMString) value = "");
  [Throws, NeedsSubjectPrincipal]
  boolean queryCommandEnabled(DOMString commandId);
  [Throws]
  boolean queryCommandIndeterm(DOMString commandId);
  [Throws]
  boolean queryCommandState(DOMString commandId);
  [Throws, NeedsCallerType]
  boolean queryCommandSupported(DOMString commandId);
  [Throws]
  DOMString queryCommandValue(DOMString commandId);
  //(Not implemented)readonly attribute HTMLCollection commands;

  // special event handler IDL attributes that only apply to Document objects
  [LegacyLenientThis] attribute EventHandler onreadystatechange;

  /**
   * True if this document is synthetic : stand alone image, video, audio file,
   * etc.
   */
  [Func="IsChromeOrUAWidget"] readonly attribute boolean mozSyntheticDocument;
  /**
   * Returns the script element whose script is currently being processed.
   *
   * @see <https://developer.mozilla.org/en/DOM/document.currentScript>
   */
  [Pure]
  readonly attribute Element? currentScript;
  /**
   * Release the current mouse capture if it is on an element within this
   * document.
   *
   * @see <https://developer.mozilla.org/en/DOM/document.releaseCapture>
   */
  [Deprecated=DocumentReleaseCapture, Pref="dom.mouse_capture.enabled"]
  undefined releaseCapture();
  /**
   * Use the given DOM element as the source image of target |-moz-element()|.
   *
   * This function introduces a new special ID (called "image element ID"),
   * which is only used by |-moz-element()|, and associates it with the given
   * DOM element.  Image elements ID's have the higher precedence than general
   * HTML id's, so if |document.mozSetImageElement(<id>, <element>)| is called,
   * |-moz-element(#<id>)| uses |<element>| as the source image even if there
   * is another element with id attribute = |<id>|.  To unregister an image
   * element ID |<id>|, call |document.mozSetImageElement(<id>, null)|.
   *
   * Example:
   * <script>
   *   canvas = document.createElement("canvas");
   *   canvas.setAttribute("width", 100);
   *   canvas.setAttribute("height", 100);
   *   // draw to canvas
   *   document.mozSetImageElement("canvasbg", canvas);
   * </script>
   * <div style="background-image: -moz-element(#canvasbg);"></div>
   *
   * @param aImageElementId an image element ID to associate with
   * |aImageElement|
   * @param aImageElement a DOM element to be used as the source image of
   * |-moz-element(#aImageElementId)|. If this is null, the function will
   * unregister the image element ID |aImageElementId|.
   *
   * @see <https://developer.mozilla.org/en/DOM/document.mozSetImageElement>
   */
  [UseCounter]
  undefined mozSetImageElement(DOMString aImageElementId,
                               Element? aImageElement);

  [ChromeOnly]
  readonly attribute URI? documentURIObject;

  /**
   * Current referrer policy - one of the referrer policy value from
   * ReferrerPolicy.webidl.
   */
  [ChromeOnly]
  readonly attribute ReferrerPolicy referrerPolicy;

    /**
   * Current referrer info, which holds all referrer related information
   * including referrer policy and raw referrer of document.
   */
  [ChromeOnly]
  readonly attribute nsIReferrerInfo referrerInfo;

};

// https://html.spec.whatwg.org/multipage/obsolete.html#other-elements%2C-attributes-and-apis
partial interface Document {
  [CEReactions] attribute [LegacyNullToEmptyString] DOMString fgColor;
  [CEReactions] attribute [LegacyNullToEmptyString] DOMString linkColor;
  [CEReactions] attribute [LegacyNullToEmptyString] DOMString vlinkColor;
  [CEReactions] attribute [LegacyNullToEmptyString] DOMString alinkColor;
  [CEReactions] attribute [LegacyNullToEmptyString] DOMString bgColor;

  [SameObject] readonly attribute HTMLCollection anchors;
  [SameObject] readonly attribute HTMLCollection applets;

  undefined clear();
  // @deprecated These are old Netscape 4 methods. Do not use,
  //             the implementation is no-op.
  // XXXbz do we actually need these anymore?
  undefined captureEvents();
  undefined releaseEvents();

  [SameObject] readonly attribute HTMLAllCollection all;
};

// https://fullscreen.spec.whatwg.org/#api
partial interface Document {
  // Note: Per spec the 'S' in these two is lowercase, but the "Moz"
  // versions have it uppercase.
  [LegacyLenientSetter, Unscopable]
  readonly attribute boolean fullscreen;
  [BinaryName="fullscreen"]
  readonly attribute boolean mozFullScreen;
  [LegacyLenientSetter, NeedsCallerType]
  readonly attribute boolean fullscreenEnabled;
  [BinaryName="fullscreenEnabled", NeedsCallerType]
  readonly attribute boolean mozFullScreenEnabled;

  [NewObject]
  Promise<undefined> exitFullscreen();
  [NewObject, BinaryName="exitFullscreen"]
  Promise<undefined> mozCancelFullScreen();

  // Events handlers
  attribute EventHandler onfullscreenchange;
  attribute EventHandler onfullscreenerror;
};

// https://w3c.github.io/pointerlock/#extensions-to-the-document-interface
// https://w3c.github.io/pointerlock/#extensions-to-the-documentorshadowroot-mixin
partial interface Document {
  undefined exitPointerLock();

  // Event handlers
  attribute EventHandler onpointerlockchange;
  attribute EventHandler onpointerlockerror;
};

// Mozilla-internal document extensions specific to error pages.
partial interface Document {
  [Func="Document::CallerIsTrustedAboutCertError", NewObject]
  Promise<any> addCertException(boolean isTemporary);

  [Func="Document::CallerIsTrustedAboutHttpsOnlyError"]
  undefined reloadWithHttpsOnlyException();

  [Func="Document::CallerIsTrustedAboutCertError", Throws]
  FailedCertSecurityInfo getFailedCertSecurityInfo();

  [Func="Document::CallerIsTrustedAboutNetError", Throws]
  NetErrorInfo getNetErrorInfo();
};

// https://w3c.github.io/page-visibility/#extensions-to-the-document-interface
partial interface Document {
  readonly attribute boolean hidden;
  readonly attribute VisibilityState visibilityState;
           attribute EventHandler onvisibilitychange;
};

// https://drafts.csswg.org/cssom/#extensions-to-the-document-interface
partial interface Document {
    attribute DOMString? selectedStyleSheetSet;
    readonly attribute DOMString? lastStyleSheetSet;
    readonly attribute DOMString? preferredStyleSheetSet;
    [Constant]
    readonly attribute DOMStringList styleSheetSets;
    undefined enableStyleSheetsForSet (DOMString? name);
};

dictionary CaretPositionFromPointOptions {
  [Pref="dom.shadowdom.new_caretPositionFromPoint_behavior.enabled"]
  sequence<ShadowRoot> shadowRoots = [];
};

// https://drafts.csswg.org/cssom-view/#extensions-to-the-document-interface
partial interface Document {
    CaretPosition? caretPositionFromPoint(float x, float y, optional CaretPositionFromPointOptions options = {});

    readonly attribute Element? scrollingElement;
};

// https://drafts.csswg.org/web-animations/#extensions-to-the-document-interface
partial interface Document {
  readonly attribute DocumentTimeline timeline;
};

// https://svgwg.org/svg2-draft/struct.html#InterfaceDocumentExtensions
partial interface Document {
  [BinaryName="SVGRootElement"]
  readonly attribute SVGSVGElement? rootElement;
};

//  Mozilla extensions of various sorts
partial interface Document {
  // @deprecated We are going to remove these (bug 1584269).
  [Pref="dom.events.script_execute.enabled"]
  attribute EventHandler onbeforescriptexecute;
  [Pref="dom.events.script_execute.enabled"]
  attribute EventHandler onafterscriptexecute;

  // Creates a new XUL element regardless of the document's default type.
  [ChromeOnly, CEReactions, NewObject, Throws]
  Element createXULElement(DOMString localName, optional (ElementCreationOptions or DOMString) options = {});
  // Wether the document was loaded using a nsXULPrototypeDocument.
  [ChromeOnly]
  readonly attribute boolean loadedFromPrototype;

  // Whether we're in android's Picture-in-Picture mode.
  // Top level document only (for now, if we want to deal with iframes, please
  // also fix bug 1959448 while at it).
  [Func="Document::CallerIsSystemPrincipalOrWebCompatAddon"]
  readonly attribute boolean inAndroidPipMode;

  // The principal to use for the storage area of this document
  [ChromeOnly]
  readonly attribute Principal effectiveStoragePrincipal;

  // You should probably not be using this principal getter since it performs
  // no checks to ensure that the partitioned principal should really be used
  // here.  It is only designed to be used in very specific circumstances, such
  // as when inheriting the document/storage principal.
  [ChromeOnly]
  readonly attribute Principal partitionedPrincipal;

  // The cookieJarSettings of this document
  [ChromeOnly]
  readonly attribute nsICookieJarSettings cookieJarSettings;

  // Touch bits
  // XXXbz I can't find the sane spec for this stuff, so just cribbing
  // from our xpidl for now.
  [NewObject, Func="nsGenericHTMLElement::LegacyTouchAPIEnabled"]
  Touch createTouch(optional Window? view = null,
                    optional EventTarget? target = null,
                    optional long identifier = 0,
                    optional long pageX = 0,
                    optional long pageY = 0,
                    optional long screenX = 0,
                    optional long screenY = 0,
                    optional long clientX = 0,
                    optional long clientY = 0,
                    optional long radiusX = 0,
                    optional long radiusY = 0,
                    optional float rotationAngle = 0,
                    optional float force = 0);
  // XXXbz a hack to get around the fact that we don't support variadics as
  // distinguishing arguments yet.  Once this hack is removed. we can also
  // remove the corresponding overload on Document, since Touch... and
  // sequence<Touch> look the same in the C++.
  [NewObject, Func="nsGenericHTMLElement::LegacyTouchAPIEnabled"]
  TouchList createTouchList(Touch touch, Touch... touches);
  // XXXbz and another hack for the fact that we can't usefully have optional
  // distinguishing arguments but need a working zero-arg form of
  // createTouchList().
  [NewObject, Func="nsGenericHTMLElement::LegacyTouchAPIEnabled"]
  TouchList createTouchList();
  [NewObject, Func="nsGenericHTMLElement::LegacyTouchAPIEnabled"]
  TouchList createTouchList(sequence<Touch> touches);

  [ChromeOnly]
  attribute boolean styleSheetChangeEventsEnabled;

  [ChromeOnly]
  attribute boolean devToolsAnonymousAndShadowEventsEnabled;

  [ChromeOnly, BinaryName="contentLanguageForBindings"] readonly attribute DOMString contentLanguage;

  [ChromeOnly] readonly attribute nsILoadGroup? documentLoadGroup;

  // Blocks the initial document parser until the given promise is settled.
  [ChromeOnly, NewObject]
  Promise<any> blockParsing(Promise<any> promise,
                            optional BlockParsingOptions options = {});

  [Func="nsContentUtils::IsSystemOrPDFJS", BinaryName="blockUnblockOnloadForSystemOrPDFJS"]
  undefined blockUnblockOnload(boolean block);

  // like documentURI, except that for error pages, it returns the URI we were
  // trying to load when we hit an error, rather than the error page's own URI.
  [ChromeOnly] readonly attribute URI? mozDocumentURIIfNotForErrorPages;

  // A promise that is resolved when we have both fired DOMContentLoaded and
  // are ready to start layout.
  // This is used for the  "document_idle" webextension script injection point.
  [ChromeOnly, Throws]
  readonly attribute Promise<undefined> documentReadyForIdle;

  // Lazily created command dispatcher, returns null if the document is not
  // chrome privileged.
  [ChromeOnly]
  readonly attribute XULCommandDispatcher? commandDispatcher;

  [ChromeOnly]
  attribute boolean devToolsWatchingDOMMutations;

  /**
   * Returns all the shadow roots connected to the document, in no particular
   * order, and without regard to open/closed-ness. Also returns UA widgets
   * (like <video> controls), which can be checked using
   * ShadowRoot.isUAWidget().
   */
  [ChromeOnly]
  sequence<ShadowRoot> getConnectedShadowRoots();

  /**
   * By default, we don't send resizes to inactive top browsers.
   * Some callers (as of this writing the window sizing code and puppeteer)
   * need to trigger these resizes.
   *
   * @param aIncludeInactive whether to include background tabs.
   */
  [ChromeOnly]
  undefined synchronouslyUpdateRemoteBrowserDimensions(optional boolean aIncludeInactive = false);
};

dictionary BlockParsingOptions {
  /**
   * If true, blocks script-created parsers (created via document.open()) in
   * addition to network-created parsers.
   */
  boolean blockScriptCreated = true;
};

// Extension to give chrome JS the ability to determine when a document was
// created to satisfy an iframe with srcdoc attribute.
partial interface Document {
  [ChromeOnly] readonly attribute boolean isSrcdocDocument;
};


// Extension to give chrome JS the ability to get the underlying
// sandbox flag attribute
partial interface Document {
  [ChromeOnly] readonly attribute DOMString? sandboxFlagsAsString;
};


/**
 * Chrome document anonymous content management.
 * This is a Chrome-only API that allows inserting fixed positioned anonymous
 * content on top of the current page displayed in the document.
 */
partial interface Document {
  /**
   * If aForce is true, tries to update layout to be able to insert the element
   * synchronously.
   */
  [ChromeOnly, NewObject, Throws]
  AnonymousContent insertAnonymousContent(optional boolean aForce = false);

  /**
   * Removes the element inserted into the CanvasFrame given an AnonymousContent
   * instance.
   */
  [ChromeOnly]
  undefined removeAnonymousContent(AnonymousContent aContent);
};

// http://w3c.github.io/selection-api/#extensions-to-document-interface
partial interface Document {
  [Throws]
  Selection? getSelection();
};

// https://github.com/whatwg/html/issues/3338
partial interface Document {
  [Pref="dom.storage_access.enabled", NewObject]
  Promise<boolean> hasStorageAccess();
  [Pref="dom.storage_access.enabled", NewObject]
  Promise<undefined> requestStorageAccess();
  // https://github.com/privacycg/storage-access/pull/100
  [Pref="dom.storage_access.forward_declared.enabled", NewObject]
  Promise<undefined> requestStorageAccessUnderSite(DOMString serializedSite);
  [Pref="dom.storage_access.forward_declared.enabled", NewObject]
  Promise<undefined> completeStorageAccessRequestFromSite(DOMString serializedSite);
};

// A privileged API to give chrome privileged code and the content script of the
// webcompat extension the ability to request the storage access for a given
// third party.
partial interface Document {
  [Func="Document::CallerIsSystemPrincipalOrWebCompatAddon", NewObject]
  Promise<undefined> requestStorageAccessForOrigin(DOMString thirdPartyOrigin, optional boolean requireUserInteraction = true);
};

// Extension to give chrome JS the ability to determine whether
// the user has interacted with the document or not.
partial interface Document {
  [ChromeOnly] readonly attribute boolean userHasInteracted;
};

// Extension to give chrome JS the ability to simulate activate the document
// by user gesture.
partial interface Document {
  [ChromeOnly]
  undefined notifyUserGestureActivation();
  // For testing only.
  [ChromeOnly]
  undefined clearUserGestureActivation();
  [ChromeOnly]
  readonly attribute boolean hasBeenUserGestureActivated;
  [ChromeOnly]
  readonly attribute boolean hasValidTransientUserGestureActivation;
  [ChromeOnly]
  readonly attribute DOMHighResTimeStamp lastUserGestureTimeStamp;
  [ChromeOnly]
  boolean consumeTransientUserGestureActivation();
};

// Extension to give chrome JS the ability to set an event handler which is
// called with certain events that happened while events were suppressed in the
// document or one of its subdocuments.
partial interface Document {
  [ChromeOnly]
  undefined setSuppressedEventListener(EventListener? aListener);
};

// Allows frontend code to query a CSP which needs to be passed for a
// new load into docshell. Further, allows to query the CSP in JSON
// format for testing purposes.
partial interface Document {
  [ChromeOnly] readonly attribute ContentSecurityPolicy? csp;
  [ChromeOnly] readonly attribute DOMString cspJSON;
};

partial interface Document {
  [Func="Document::DocumentSupportsL10n"] readonly attribute DocumentL10n? l10n;
  [Func="Document::DocumentSupportsL10n"] readonly attribute boolean hasPendingL10nMutations;
};

Document includes XPathEvaluatorMixin;
Document includes GlobalEventHandlers;
Document includes TouchEventHandlers;
Document includes ParentNode;
Document includes OnErrorEventHandlerForNodes;
Document includes GeometryUtils;
Document includes FontFaceSource;
Document includes DocumentOrShadowRoot;

// https://w3c.github.io/webappsec-feature-policy/#idl-index
partial interface Document {
    [SameObject, Pref="dom.security.featurePolicy.webidl.enabled"]
    readonly attribute FeaturePolicy featurePolicy;
};

// Extension to give chrome JS the ability to specify a non-default keypress
// event model.
partial interface Document {
  /**
   * setKeyPressEventModel() is called when we need to check whether the web
   * app requires specific keypress event model or not.
   *
   * @param aKeyPressEventModel  Proper keypress event model for the web app.
   *   KEYPRESS_EVENT_MODEL_DEFAULT:
   *     Use default keypress event model.  I.e., depending on
   *     "dom.keyboardevent.keypress.set_keycode_and_charcode_to_same_value"
   *     pref.
   *   KEYPRESS_EVENT_MODEL_SPLIT:
   *     Use split model.  I.e, if keypress event inputs a character,
   *     keyCode should be 0.  Otherwise, charCode should be 0.
   *   KEYPRESS_EVENT_MODEL_CONFLATED:
   *     Use conflated model.  I.e., keyCode and charCode values of each
   *     keypress event should be set to same value.
   */
  [ChromeOnly]
  const unsigned short KEYPRESS_EVENT_MODEL_DEFAULT = 0;
  [ChromeOnly]
  const unsigned short KEYPRESS_EVENT_MODEL_SPLIT = 1;
  [ChromeOnly]
  const unsigned short KEYPRESS_EVENT_MODEL_CONFLATED = 2;
  [ChromeOnly]
  undefined setKeyPressEventModel(unsigned short aKeyPressEventModel);
};

// Extensions to return information about about the nodes blocked by the
// Safebrowsing API inside a document.
partial interface Document {
  /*
   * Number of nodes that have been blocked by the Safebrowsing API to prevent
   * tracking, cryptomining and so on. This method is for testing only.
   */
  [ChromeOnly, Pure]
  readonly attribute long blockedNodeByClassifierCount;

  /*
   * List of nodes that have been blocked by the Safebrowsing API to prevent
   * tracking, fingerprinting, cryptomining and so on. This method is for
   * testing only.
   */
  [ChromeOnly, Pure]
  readonly attribute NodeList blockedNodesByClassifier;
};

// Extension to programmatically simulate a user interaction on a document,
// used for testing.
partial interface Document {
  [ChromeOnly, BinaryName="setUserHasInteracted"]
  undefined userInteractionForTesting();
};

// Extension for permission delegation.
partial interface Document {
  [ChromeOnly, Pure]
  readonly attribute nsIPermissionDelegateHandler permDelegateHandler;
};

// Extension used by the password manager to infer form submissions.
partial interface Document {
  /*
   * Set whether the document notifies an event when a fetch or
   * XHR completes successfully.
   */
  [ChromeOnly]
  undefined setNotifyFetchSuccess(boolean aShouldNotify);

  /*
   * Set whether a form and a password field notify an event when it is
   * removed from the DOM tree.
   */
  [ChromeOnly]
  undefined setNotifyFormOrPasswordRemoved(boolean aShouldNotify);
};

// Extension to allow chrome code to detect initial about:blank documents.
partial interface Document {
  [ChromeOnly]
  readonly attribute boolean isInitialDocument;
};

// Extension to allow chrome code to get some wireframe-like structure.
enum WireframeRectType {
  "image",
  "background",
  "text",
  "unknown",
};
dictionary WireframeTaggedRect {
  unrestricted double x = 0;
  unrestricted double y = 0;
  unrestricted double width = 0;
  unrestricted double height = 0;
  unsigned long color = 0; // in nscolor format
  WireframeRectType type;
  Node? node;
};
[GenerateInit]
dictionary Wireframe {
  unsigned long canvasBackground = 0; // in nscolor format
  sequence<WireframeTaggedRect> rects;
  unsigned long version = 1; // Increment when the wireframe structure changes in backwards-incompatible ways
};
partial interface Document {
  [ChromeOnly]
  Wireframe? getWireframe(optional boolean aIncludeNodes = false);
};

partial interface Document {
  // Returns true if the document is the current active document in a browsing
  // context which isn't in bfcache.
  [ChromeOnly]
  boolean isActive();
};

Document includes NonElementParentNode;

/**
 * Extension to add the fragmentDirective property.
 * https://wicg.github.io/scroll-to-text-fragment/#feature-detectability
 */
partial interface Document {
    [Pref="dom.text_fragments.enabled", SameObject]
    readonly attribute FragmentDirective fragmentDirective;
};

// https://drafts.csswg.org/css-view-transitions-1/#additions-to-document-api
partial interface Document {
  [Pref="dom.viewTransitions.enabled"]
  ViewTransition startViewTransition(optional ViewTransitionUpdateCallback updateCallback);
};

// https://github.com/w3c/csswg-drafts/pull/10767 for the name divergence in the spec
callback ViewTransitionUpdateCallback = Promise<any> ();

// https://wicg.github.io/sanitizer-api/#sanitizer-api
partial interface Document {
  [Throws, Pref="dom.security.sanitizer.enabled"]
  static Document parseHTML(DOMString html, optional SetHTMLOptions options = {});
};
