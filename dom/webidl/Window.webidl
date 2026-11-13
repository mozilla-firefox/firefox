/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is:
 * http://www.whatwg.org/specs/web-apps/current-work/
 * https://dvcs.w3.org/hg/editing/raw-file/tip/editing.html
 * https://dvcs.w3.org/hg/IndexedDB/raw-file/tip/Overview.html
 * http://dev.w3.org/csswg/cssom/
 * http://dev.w3.org/csswg/cssom-view/
 * https://dvcs.w3.org/hg/webperf/raw-file/tip/specs/RequestAnimationFrame/Overview.html
 * https://dvcs.w3.org/hg/webperf/raw-file/tip/specs/NavigationTiming/Overview.html
 * https://dvcs.w3.org/hg/webcrypto-api/raw-file/tip/spec/Overview.html
 * http://dvcs.w3.org/hg/speech-api/raw-file/tip/speechapi.html
 * https://w3c.github.io/webappsec-secure-contexts/#monkey-patching-global-object
 * https://w3c.github.io/requestidlecallback/
 * https://drafts.css-houdini.org/css-paint-api-1/#dom-window-paintworklet
 * https://wicg.github.io/visual-viewport/#the-visualviewport-interface
 * https://wicg.github.io/cookie-store/#Window
 */

interface Principal;
interface nsIBrowserDOMWindow;
interface XULControllers;
interface nsIDOMWindowUtils;
interface nsIPrintSettings;

// http://www.whatwg.org/specs/web-apps/current-work/
[Global=Window, LegacyUnenumerableNamedProperties, NeedResolve,
 Exposed=Window,
 InstrumentedProps=(AbsoluteOrientationSensor,
                    Accelerometer,
                    BackgroundFetchManager,
                    BackgroundFetchRecord,
                    BackgroundFetchRegistration,
                    BeforeInstallPromptEvent,
                    Bluetooth,
                    BluetoothCharacteristicProperties,
                    BluetoothDevice,
                    BluetoothRemoteGATTCharacteristic,
                    BluetoothRemoteGATTDescriptor,
                    BluetoothRemoteGATTServer,
                    BluetoothRemoteGATTService,
                    BluetoothUUID,
                    CanvasCaptureMediaStreamTrack,
                    chrome,
                    ClipboardItem,
                    CSSImageValue,
                    CSSKeywordValue,
                    CSSMathClamp,
                    CSSMathInvert,
                    CSSMathMax,
                    CSSMathMin,
                    CSSMathNegate,
                    CSSMathProduct,
                    CSSMathSum,
                    CSSMathValue,
                    CSSMatrixComponent,
                    CSSNumericArray,
                    CSSNumericValue,
                    CSSPerspective,
                    CSSPositionValue,
                    CSSPropertyRule,
                    CSSRotate,
                    CSSScale,
                    CSSSkew,
                    CSSSkewX,
                    CSSSkewY,
                    CSSStyleValue,
                    CSSTransformComponent,
                    CSSTransformValue,
                    CSSTranslate,
                    CSSUnitValue,
                    CSSUnparsedValue,
                    CSSVariableReferenceValue,
                    defaultStatus,
                    // Unfortunately, our telemetry histogram name generator
                    // (the one that generates TelemetryHistogramEnums.h) can't
                    // handle two DOM methods with names that only differ in
                    // case, because it forces everything to uppercase.
                    //defaultstatus,
                    DeviceMotionEventAcceleration,
                    DeviceMotionEventRotationRate,
                    DOMError,
                    EnterPictureInPictureEvent,
                    External,
                    FederatedCredential,
                    Gyroscope,
                    HTMLContentElement,
                    HTMLShadowElement,
                    ImageCapture,
                    InputDeviceCapabilities,
                    InputDeviceInfo,
                    Keyboard,
                    KeyboardLayoutMap,
                    LinearAccelerationSensor,
                    MediaSettingsRange,
                    NetworkInformation,
                    offscreenBuffering,
                    onbeforeinstallprompt,
                    oncancel,
                    onmousewheel,
                    onsearch,
                    onselectionchange,
                    openDatabase,
                    OrientationSensor,
                    OverconstrainedError,
                    PasswordCredential,
                    PaymentAddress,
                    PaymentInstruments,
                    PaymentManager,
                    PaymentMethodChangeEvent,
                    PaymentRequest,
                    PaymentRequestUpdateEvent,
                    PaymentResponse,
                    PerformanceLongTaskTiming,
                    PhotoCapabilities,
                    PictureInPictureEvent,
                    PictureInPictureWindow,
                    Presentation,
                    PresentationAvailability,
                    PresentationConnection,
                    PresentationConnectionAvailableEvent,
                    PresentationConnectionCloseEvent,
                    PresentationConnectionList,
                    PresentationReceiver,
                    PresentationRequest,
                    RelativeOrientationSensor,
                    RemotePlayback,
                    RTCError,
                    RTCErrorEvent,
                    RTCIceTransport,
                    RTCPeerConnectionIceErrorEvent,
                    Sensor,
                    SensorErrorEvent,
                    SpeechRecognitionAlternative,
                    SpeechRecognitionResult,
                    SpeechRecognitionResultList,
                    styleMedia,
                    StylePropertyMap,
                    StylePropertyMapReadOnly,
                    SVGDiscardElement,
                    SyncManager,
                    TaskAttributionTiming,
                    TextEvent,
                    Touch,
                    TouchEvent,
                    TouchList,
                    USB,
                    USBAlternateInterface,
                    USBConfiguration,
                    USBConnectionEvent,
                    USBDevice,
                    USBEndpoint,
                    USBInterface,
                    USBInTransferResult,
                    USBIsochronousInTransferPacket,
                    USBIsochronousInTransferResult,
                    USBIsochronousOutTransferPacket,
                    USBIsochronousOutTransferResult,
                    USBOutTransferResult,
                    WakeLockSentinel,
                    webkitCancelAnimationFrame,
                    webkitMediaStream,
                    WebKitMutationObserver,
                    webkitRequestAnimationFrame,
                    webkitRequestFileSystem,
                    webkitResolveLocalFileSystemURL,
                    webkitRTCPeerConnection,
                    webkitSpeechGrammar,
                    webkitSpeechGrammarList,
                    webkitSpeechRecognition,
                    webkitSpeechRecognitionError,
                    webkitSpeechRecognitionEvent,
                    webkitStorageInfo)]
/*sealed*/ interface Window : EventTarget {
  // the current browsing context
  [LegacyUnforgeable, Constant, StoreInSlot,
   CrossOriginReadable] readonly attribute WindowProxy window;
  [Replaceable, Constant, StoreInSlot,
   CrossOriginReadable] readonly attribute WindowProxy self;
  [LegacyUnforgeable, StoreInSlot, Pure] readonly attribute Document? document;
  [Throws] attribute DOMString name;
  [PutForwards=href, LegacyUnforgeable, CrossOriginReadable,
   CrossOriginWritable] readonly attribute Location location;
  [Throws] readonly attribute History history;
  [Replaceable, Func="Navigation::IsAPIEnabled"] readonly attribute Navigation navigation;
  readonly attribute CustomElementRegistry customElements;
  [Replaceable, Throws] readonly attribute BarProp locationbar;
  [Replaceable, Throws] readonly attribute BarProp menubar;
  [Replaceable, Throws] readonly attribute BarProp personalbar;
  [Replaceable, Throws] readonly attribute BarProp scrollbars;
  [Replaceable, Throws] readonly attribute BarProp statusbar;
  [Replaceable, Throws] readonly attribute BarProp toolbar;
  [Throws] attribute DOMString status;
  [Throws, CrossOriginCallable, NeedsCallerType] undefined close();
  [Throws, CrossOriginReadable] readonly attribute boolean closed;
  [Throws] undefined stop();
  [Throws, CrossOriginCallable, NeedsCallerType] undefined focus();
  [Throws, CrossOriginCallable, NeedsCallerType] undefined blur();
  [Replaceable] readonly attribute (Event or undefined) event;

  // other browsing contexts
  [Replaceable, Throws, CrossOriginReadable] readonly attribute WindowProxy frames;
  [Replaceable, CrossOriginReadable] readonly attribute unsigned long length;
  //[Unforgeable, Throws, CrossOriginReadable] readonly attribute WindowProxy top;
  [LegacyUnforgeable, Throws, CrossOriginReadable] readonly attribute WindowProxy? top;
  [Throws, CrossOriginReadable] attribute any opener;
  //[Throws] readonly attribute WindowProxy parent;
  [Replaceable, Throws, CrossOriginReadable] readonly attribute WindowProxy? parent;
  [Throws, NeedsSubjectPrincipal] readonly attribute Element? frameElement;
  //[Throws] WindowProxy? open(optional USVString url = "about:blank", optional DOMString target = "_blank", [TreatNullAs=EmptyString] optional DOMString features = "");
  [Throws] WindowProxy? open(optional USVString url = "", optional DOMString target = "", optional [LegacyNullToEmptyString] DOMString features = "");
  getter object (DOMString name);

  // the user agent
  readonly attribute Navigator navigator;
  [Replaceable, BinaryName="Navigator"]
  readonly attribute Navigator clientInformation;

  [Replaceable] readonly attribute External external;

  // user prompts
  [Throws, NeedsSubjectPrincipal] undefined alert();
  [Throws, NeedsSubjectPrincipal] undefined alert(DOMString message);
  [Throws, NeedsSubjectPrincipal] boolean confirm(optional DOMString message = "");
  [Throws, NeedsSubjectPrincipal] DOMString? prompt(optional DOMString message = "", optional DOMString default = "");
  [Throws]
  undefined print();

  // Returns a window that you can use for a print preview.
  //
  // This may reuse an existing window if this window is already a print
  // preview document, or if you pass a docshell explicitly.
  [Throws, Func="nsContentUtils::IsCallerChromeOrFuzzingEnabled"]
  WindowProxy? printPreview(optional nsIPrintSettings? settings = null,
                            optional nsIWebProgressListener? listener = null,
                            optional nsIDocShell? docShellToPreviewInto = null);

  [Throws, CrossOriginCallable, NeedsSubjectPrincipal,
   BinaryName="postMessageMoz"]
  undefined postMessage(any message, DOMString targetOrigin, optional sequence<object> transfer = []);
  [Throws, CrossOriginCallable, NeedsSubjectPrincipal,
   BinaryName="postMessageMoz"]
  undefined postMessage(any message, optional WindowPostMessageOptions options = {});

  // also has obsolete members
};
Window includes GlobalEventHandlers;
Window includes WindowEventHandlers;

// http://www.whatwg.org/specs/web-apps/current-work/
interface mixin WindowSessionStorage {
  //[Throws] readonly attribute Storage sessionStorage;
  [Throws] readonly attribute Storage? sessionStorage;
};
Window includes WindowSessionStorage;

// http://www.whatwg.org/specs/web-apps/current-work/
interface mixin WindowLocalStorage {
  [Throws] readonly attribute Storage? localStorage;
};
Window includes WindowLocalStorage;

// http://www.whatwg.org/specs/web-apps/current-work/
partial interface Window {
  [Deprecated=UseOfCaptureEvents]
  undefined captureEvents();
  [Deprecated=UseOfReleaseEvents]
  undefined releaseEvents();
};

// https://dvcs.w3.org/hg/editing/raw-file/tip/editing.html
partial interface Window {
  //[Throws] Selection getSelection();
  [Throws] Selection? getSelection();
};

// https://drafts.csswg.org/cssom/#extensions-to-the-window-interface
partial interface Window {
  //[NewObject, Throws] CSSStyleProperties getComputedStyle(Element elt, optional DOMString? pseudoElt = "");
  [NewObject, Throws] CSSStyleProperties? getComputedStyle(Element elt, optional DOMString? pseudoElt = "");
};

// http://dev.w3.org/csswg/cssom-view/
enum ScrollBehavior { "auto", "instant", "smooth" };

dictionary ScrollOptions {
  ScrollBehavior behavior = "auto";
};

dictionary ScrollToOptions : ScrollOptions {
  unrestricted double left;
  unrestricted double top;
};

partial interface Window {
  //[Throws, NewObject, NeedsCallerType] MediaQueryList matchMedia(DOMString query);
  [Throws, NewObject, NeedsCallerType] MediaQueryList? matchMedia(UTF8String query);

  [Replaceable] readonly attribute Screen screen;

  // browsing context
  //[Throws] undefined moveTo(double x, double y);
  //[Throws] undefined moveBy(double x, double y);
  //[Throws] undefined resizeTo(double x, double y);
  //[Throws] undefined resizeBy(double x, double y);
  [Throws, NeedsCallerType] undefined moveTo(long x, long y);
  [Throws, NeedsCallerType] undefined moveBy(long x, long y);
  [Throws, NeedsCallerType] undefined resizeTo(long x, long y);
  [Throws, NeedsCallerType] undefined resizeBy(long x, long y);
  [Throws, ChromeOnly] undefined moveResize(long x, long y, long w, long h);

  // viewport
  [Replaceable, Throws, NeedsCallerType] readonly attribute double innerWidth;
  [Replaceable, Throws, NeedsCallerType] readonly attribute double innerHeight;

  // viewport scrolling
  undefined scroll(unrestricted double x, unrestricted double y);
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
  // The four properties below are double per spec at the moment, but whether
  // that will continue is unclear.
  [Replaceable, Throws] readonly attribute double scrollX;
  [Replaceable, Throws] readonly attribute double pageXOffset;
  [Replaceable, Throws] readonly attribute double scrollY;
  [Replaceable, Throws] readonly attribute double pageYOffset;

  // Aliases for screenX / screenY.
  [Replaceable, Throws, NeedsCallerType] readonly attribute double screenLeft;
  [Replaceable, Throws, NeedsCallerType] readonly attribute double screenTop;

  // client
  [Replaceable, Throws, NeedsCallerType] readonly attribute double screenX;
  [Replaceable, Throws, NeedsCallerType] readonly attribute double screenY;
  [Replaceable, Throws, NeedsCallerType] readonly attribute double outerWidth;
  [Replaceable, Throws, NeedsCallerType] readonly attribute double outerHeight;
};

// https://html.spec.whatwg.org/multipage/imagebitmap-and-animations.html#animation-frames
Window includes AnimationFrameProvider;

// https://dvcs.w3.org/hg/webperf/raw-file/tip/specs/NavigationTiming/Overview.html
partial interface Window {
  [Replaceable, Pure, StoreInSlot] readonly attribute Performance? performance;
};

// https://dvcs.w3.org/hg/webcrypto-api/raw-file/tip/spec/Overview.html
Window includes GlobalCrypto;

dictionary SizeToContentConstraints {
  long maxWidth = 0;
  long maxHeight = 0;
  long prefWidth = 0;
};

#ifdef MOZ_WEBSPEECH
// http://dvcs.w3.org/hg/speech-api/raw-file/tip/speechapi.html
interface mixin SpeechSynthesisGetter {
  [Throws, Pref="media.webspeech.synth.enabled"] readonly attribute SpeechSynthesis speechSynthesis;
};

Window includes SpeechSynthesisGetter;
#endif

// Mozilla-specific stuff
dictionary SynthesizeEventData {
  // A unique identifier for the pointer causing the event, defaulting to 0.
  unsigned long identifier = 0;
  // Input pressure (0.0 -> 1.0).
  // If unspecified, synthesized events use the default value defined at
  // https://w3c.github.io/pointerevents/#dom-pointerevent-pressure.
  float pressure;
};

// Mozilla-specific stuff
dictionary SynthesizeMouseEventData : SynthesizeEventData {
  // Indicates which mouse button is pressed/released when a mouse event is triggered.
  long button = 0;
  // Indicates which mouse buttons are pressed when a mouse event is triggered.
  // If not specified, the value is generated from the button property.
  long buttons;
  // Number of clicks that have been performed. If not specified, a default value
  // is generated based on the event type, e.g. 1 for mousedown and mouseup events,
  // and 0 for others.
  long clickCount;
  // Input source, see MouseEvent for values. Defaults to MouseEvent.MOZ_SOURCE_MOUSE.
  short inputSource = 1;
  // Modifiers pressed, using constants defined as MODIFIER_* in nsIDOMWindowUtils.
  long modifiers = 0;
  // Raw movement delta, standing in for the raw delta a platform widget
  // reports while the native pointer is locked. Must be specified together.
  long movementX;
  long movementY;
  // Command (for WidgetCommandEvent) to execute as the default action for the click.
  // Can only be specified from the parent process.
  DOMString command = "";
};

// Mozilla-specific stuff
dictionary SynthesizeTouchEventData : SynthesizeEventData {
  // X offset in CSS pixels.
  required long offsetX;
  // Y offset in CSS pixels.
  required long offsetY;
  // X radii in CSS pixels.
  unsigned long radiiX = 1;
  // Y radii in CSS pixels.
  unsigned long radiiY = 1;
  // Rotation angle in degrees.
  float rotationAngle = 0;
  // X tilt in degrees (-90 -> 90). If altitudeAngle and azimuthAngle are
  // specified, tiltX is ignored.
  long tiltX = 0;
  // Y tilt in degrees (-90 -> 90). If altitudeAngle and azimuthAngle are
  // specified, tiltY is ignored.
  long tiltY = 0;
  // Twist in degrees (0 -> 360).
  long twist = 0;
  // Altitude angle in radians (0 -> π/2). If altitudeAngle is specified,
  // azimuthAngle must also be specified.
  double altitudeAngle;
  // Azimuth angle in radians (0 -> 2π). If azimuthAngle is specified,
  // altitudeAngle must also be specified.
  double azimuthAngle;
};

// Mozilla-specific stuff
dictionary SynthesizeEventOptions {
  // If true the event is dispatched to the parent process through APZ,
  // without being injected into the OS event queue.
  boolean isAsyncEnabled = false;
  // Set this to true to ensure that the event is dispatched to this DOM window
  // or one of its children.
  boolean toWindow = false;
  // Controls Event.isSynthesized value that helps identifying test related events.
  boolean isDOMEventSynthesized = true;
};

// Mozilla-specific stuff
dictionary SynthesizeMouseEventOptions : SynthesizeEventOptions {
  // Indicates whether the event should ignore viewport bounds during dispatch.
  boolean ignoreRootScrollFrame = false;
  // Controls WidgetMouseEvent.mReason value.
  boolean isWidgetEventSynthesized = false;
};

// Mozilla-specific stuff
dictionary SynthesizeTouchEventOptions : SynthesizeEventOptions {
  // If true, the event is synthesized as a pen input.
  boolean isPen = false;
};

// Mozilla-specific stuff
partial interface Window {
  //[NewObject, Throws] CSSStyleDeclaration getDefaultComputedStyle(Element elt, optional DOMString pseudoElt = "");
  [NewObject, Throws] CSSStyleDeclaration? getDefaultComputedStyle(Element elt, optional DOMString pseudoElt = "");

  // Mozilla extensions
  /**
   * Method for scrolling this window by a number of lines.
   */
  undefined                 scrollByLines(long numLines, optional ScrollOptions options = {});

  /**
   * Method for scrolling this window by a number of pages.
   */
  undefined                 scrollByPages(long numPages, optional ScrollOptions options = {});


  /**
   * Chrome-only method for sizing to content with an optional
   * maximum-size constraint on either (or both) directions.
   */
  [Throws, ChromeOnly] undefined sizeToContent(optional SizeToContentConstraints constraints = {});

  [ChromeOnly, Replaceable, Throws] readonly attribute XULControllers controllers;

  [ChromeOnly, Throws] readonly attribute Element? realFrameElement;

  [ChromeOnly] readonly attribute nsIDocShell? docShell;

  [ChromeOnly, Constant, CrossOriginReadable, BinaryName="getBrowsingContext"]
  readonly attribute BrowsingContext browsingContext;

  [Throws, NeedsCallerType]
  readonly attribute float mozInnerScreenX;
  [Throws, NeedsCallerType]
  readonly attribute float mozInnerScreenY;
  [Replaceable, Throws, NeedsCallerType]
  readonly attribute double devicePixelRatio;

  // Allows chrome code to convert desktop pixels to device pixels and vice
  // versa. Useful for interacting with the screen manager.
  [ChromeOnly, Throws]
  readonly attribute double desktopToDeviceScale;

  // Returns the amount of CSS pixels relative to this window we're allowed to
  // go out of the screen. This is needed so that SessionRestore is able to
  // position windows that use client-side decorations correctly, but still
  // pull mispositioned windows into the screen.
  [ChromeOnly]
  readonly attribute double screenEdgeSlopX;
  [ChromeOnly]
  readonly attribute double screenEdgeSlopY;


  /* The maximum offset that the window can be scrolled to
     (i.e., the document width/height minus the scrollport width/height) */
  [ChromeOnly, Throws]  readonly attribute long   scrollMinX;
  [ChromeOnly, Throws]  readonly attribute long   scrollMinY;
  [Replaceable, Throws] readonly attribute long   scrollMaxX;
  [Replaceable, Throws] readonly attribute long   scrollMaxY;

  // XXX This should be a ChromeOnly attribute
  [Throws, NeedsCallerType] attribute boolean fullScreen;

  undefined                 updateCommands(DOMString action);

  /* Find in page.
   * @param str: the search pattern
   * @param caseSensitive: is the search caseSensitive
   * @param backwards: should we search backwards
   * @param wrapAround: should we wrap the search
   * @param wholeWord: should we search only for whole words
   * @param searchInFrames: should we search through all frames
   * @param showDialog: should we show the Find dialog
   */
  [Throws] boolean    find(optional DOMString str = "",
                           optional boolean caseSensitive = false,
                           optional boolean backwards = false,
                           optional boolean wrapAround = false,
                           optional boolean wholeWord = false,
                           optional boolean searchInFrames = false,
                           optional boolean showDialog = false);

           attribute EventHandler ondevicemotion;
           attribute EventHandler ondeviceorientation;
           attribute EventHandler ondeviceorientationabsolute;
           [Pref="device.sensors.proximity.enabled"]
           attribute EventHandler onuserproximity;
           [Pref="device.sensors.ambientLight.enabled"]
           attribute EventHandler ondevicelight;

  undefined                 dump(DOMString str);

  /**
   * This method is here for backwards compatibility with 4.x only,
   * its implementation is a no-op
   */
  undefined                 setResizable(boolean resizable);

  /**
   * This is the scriptable version of
   * nsPIDOMWindow::OpenDialog() that takes 3 optional
   * arguments, plus any additional arguments are passed on as
   * arguments on the dialog's window object (window.arguments).
   */
  [Throws, ChromeOnly] WindowProxy? openDialog(optional DOMString url = "",
                                               optional DOMString name = "",
                                               optional DOMString options = "",
                                               any... extraArguments);

  [ChromeOnly,
   NonEnumerable, Replaceable, Throws, NeedsCallerType]
  readonly attribute object? content;

  [Throws, ChromeOnly] any getInterface(any iid);

  /**
   * Same as nsIDOMWindow.windowRoot, useful for event listener targeting.
   */
  [ChromeOnly, Throws]
  readonly attribute WindowRoot? windowRoot;

  /**
   * ChromeOnly method to determine if a particular window should see console
   * reports from service workers of the given scope.
   */
  [ChromeOnly]
  boolean shouldReportForServiceWorkerScope(USVString aScope);

  /**
   * InstallTrigger was an interface for installing extensions. It was disabled
   * in bug 1772901 and the implementation was removed in bug 1776426.
   *
   * We maintain this stub to avoid breaking websites that do
   * "typeof InstallTrigger !== 'undefined" to detect Firefox
   */
  [Replaceable, Deprecated="InstallTriggerDeprecated", Pref="extensions.InstallTrigger.enabled"]
  readonly attribute object? InstallTrigger;

  /**
   * Get the nsIDOMWindowUtils for this window.
   */
  [Constant, Throws, ChromeOnly]
  readonly attribute nsIDOMWindowUtils windowUtils;

  /**
   * Synthesize a mouse event. The event types supported are:
   *    mousedown, mouseup, mousemove, mouseover, mouseout, mousecancel,
   *    contextmenu, MozMouseHittest
   *
   * Events are sent in coordinates offset by offsetX and offsetY from the window.
   *
   * Note that additional events may be fired as a result of this call. For
   * instance, typically a click event will be fired as a result of a
   * mousedown and mouseup in sequence.
   *
   * Normally at this level of events, the mouseover and mouseout events are
   * only fired when the window is entered or exited. For inter-element
   * mouseover and mouseout events, a movemove event fired on the new element
   * should be sufficient to generate the correct over and out events as well.
   *
   * The event is dispatched via the toplevel window, so it could go to any
   * window under the toplevel window, in some cases it could never reach this
   * window at all.
   *
   * NOTE: mousecancel is used to represent the vanishing of an input device
   * such as a pen leaving its digitizer by synthesizing a WidgetMouseEvent,
   * whose mMessage is eMouseExitFromWidget and mExitFrom is
   * WidgetMouseEvent::eTopLevel.
   *
   * @param type            Event type.
   * @param offsetX         X offset in CSS pixels.
   * @param offsetY         Y offset in CSS pixels.
   * @param mouseEventData  A SynthesizeMouseEventData dictionary containing mouse event data.
   * @param options         A SynthesizeMouseEventOptions dictionary containing options
   *                        for the event dispatching.
   * @param callback        A function to call when the synthesized mouse event
   *                        has been dispatched.
   *                        XXX: This is currently not supported in the content
   *                        process, simply because we don't have a use case for
   *                        it yet. The same applies when the synthesized event
   *                        might be coalesced, such as when
   *                        `isDOMEventSynthesized = false`.
   *                        In such cases, passing callback will throw an
   *                        exception.
   *
   * @return true if someone called prevent default on this event.
   */
  [ChromeOnly, Throws]
  boolean synthesizeMouseEvent(DOMString type, float offsetX, float offsetY,
                               optional SynthesizeMouseEventData mouseEventData = {},
                               optional SynthesizeMouseEventOptions options = {},
                               optional VoidFunction callback);

  /**
   * Synthesize a touch event. The event types supported are:
   *    touchstart, touchend, touchmove, and touchcancel
   *
   * The event is dispatched via the toplevel window, so it could go to any
   * window under the toplevel window, in some cases it could never reach this
   * window at all. (Set SynthesizeTouchEventOptions.toWindow to true to ensure
   * that the event is dispatched to this window or one of its children.)
   *
   * @param type       Event type.
   * @param touches    An array of SynthesizeTouchEventData dictionaries containing
   *                   touch event data.
   * @param modifiers  Modifiers pressed, using constants defined as MODIFIER_*
   *                   in nsIDOMWindowUtils.
   * @param options    A SynthesizeTouchEventOptions dictionary containing options
   *                   for the event dispatching.
   * @param callback   A function to call when the synthesized touch event has
   *                   been dispatched.
   *                   XXX: This is currently not supported in the content process,
   *                   simply because we don't have a use case for it yet. The same
   *                   applies when the synthesized event might be coalesced,
   *                   such as when `isDOMEventSynthesized = false`. In such cases,
   *                   passing callback will throw an exception.
   *
   * @return true if someone called prevent default on this event.
   */
  [ChromeOnly, Throws]
  boolean synthesizeTouchEvent(DOMString type, sequence<SynthesizeTouchEventData> touches,
                               optional long modifiers = 0,
                               optional SynthesizeTouchEventOptions options = {},
                               optional VoidFunction callback);

  [Pure, ChromeOnly]
  readonly attribute WindowGlobalChild? windowGlobalChild;

  /**
   * The principal of the client source of the window. This is supposed to be
   * used for the service worker.
   *
   * This is used for APIs like https://w3c.github.io/push-api/ that extend
   * ServiceWorkerRegistration and therefore need to operate consistently with
   * ServiceWorkers and its Clients API. The client principal is the appropriate
   * principal to pass to all nsIServiceWorkerManager APIs.
   *
   * Note that the client principal will be different from the node principal of
   * the window's document if the window is in a third-party context when dFPI
   * is enabled. In this case, the client principal will be the partitioned
   * principal to support the service worker partitioning.
   */
  [ChromeOnly]
  readonly attribute Principal? clientPrincipal;

  /**
   *  Whether the chrome window is currently in a full screen transition. This
   *  flag is updated from FullscreenTransitionTask.
   *  Always set to false for non-chrome windows.
   */
  [ChromeOnly]
  readonly attribute boolean isInFullScreenTransition;
};

Window includes TouchEventHandlers;

Window includes OnErrorEventHandlerForWindow;

#if defined(MOZ_WIDGET_ANDROID)
// https://compat.spec.whatwg.org/#windoworientation-interface
partial interface Window {
  [UseCounter, NeedsCallerType]
  readonly attribute short orientation;
           attribute EventHandler onorientationchange;
};
#endif

[MOZ_CAN_RUN_SCRIPT_BOUNDARY]
callback PromiseDocumentFlushedCallback = any ();

// Mozilla extensions for Chrome windows.
partial interface Window {
  // The STATE_* constants need to match the corresponding enum in nsGlobalWindow.cpp.
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  const unsigned short STATE_MAXIMIZED = 1;
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  const unsigned short STATE_MINIMIZED = 2;
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  const unsigned short STATE_NORMAL = 3;
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  const unsigned short STATE_FULLSCREEN = 4;

  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  readonly attribute unsigned short windowState;

  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  readonly attribute boolean isFullyOccluded;

  /**
   * On Windows, returns whether the window is cloaked, presumably
   * because it is on another virtual desktop. Per Microsoft, we
   * should not automatically switch to this window if we are looking
   * for an existing window to focus, and instead open a new window.
   * On non-Windows platforms, this is always false.
   */
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  readonly attribute boolean isCloaked;

  /**
   * browserDOMWindow provides access to yet another layer of
   * utility functions implemented by chrome script. It will be null
   * for DOMWindows not corresponding to browsers.
   */
  [Throws, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
           attribute nsIBrowserDOMWindow? browserDOMWindow;

  [Throws, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 getAttention();

  [Throws, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 getAttentionWithCycleCount(long aCycleCount);

  [Throws, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 setCursor(UTF8String cursor);

  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 maximize();
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 minimize();
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 restore();
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  DOMString                 getWorkspaceID();
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined                 moveToWorkspace(DOMString workspaceID);

  /**
   * Notify a default button is loaded on a dialog or a wizard.
   * defaultButton is the default button.
   */
  [Throws, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  undefined notifyDefaultButtonLoaded(Element defaultButton);

  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  readonly attribute ChromeMessageBroadcaster messageManager;

  /**
   * Returns the message manager identified by the given group name that
   * manages all frame loaders belonging to that group.
   */
  [Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  ChromeMessageBroadcaster getGroupMessageManager(DOMString aGroup);

  /**
   * Calls the given function as soon as a style or layout flush for the
   * top-level document is not necessary, and returns a Promise which
   * resolves to the callback's return value after it executes.
   *
   * In the event that the window goes away before a flush can occur, the
   * callback will still be called and the Promise resolved as the window
   * tears itself down.
   *
   * The callback _must not modify the DOM for any window in any way_. If it
   * does, after finishing executing, the Promise returned by
   * promiseDocumentFlushed will reject with
   * NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR.
   *
   * Note that the callback can be called either synchronously or asynchronously
   * depending on whether or not flushes are pending:
   *
   *   The callback will be called synchronously when calling
   *   promiseDocumentFlushed when NO flushes are already pending. This is
   *   to ensure that no script has a chance to dirty the DOM before the callback
   *   is called.
   *
   *   The callback will be called asynchronously if a flush is pending.
   *
   * The expected execution order is that all pending callbacks will
   * be fired first (and in the order that they were queued) and then the
   * Promise resolution handlers will all be invoked later on during the
   * next microtask checkpoint.
   *
   * Using window.top.promiseDocumentFlushed in combination with a callback
   * that is querying items in a window that might be swapped out via
   * nsFrameLoader::SwapWithOtherLoader is highly discouraged. For example:
   *
   *   let result = await window.top.promiseDocumentFlushed(() => {
   *     return window.document.body.getBoundingClientRect();
   *   });
   *
   *   If "window" might get swapped out via nsFrameLoader::SwapWithOtherLoader
   *   at any time, then the callback might get called when the new host window
   *   will still incur layout flushes, since it's only the original host window
   *   that's being monitored via window.top.promiseDocumentFlushed.
   *
   *   See bug 1519407 for further details.
   *
   * promiseDocumentFlushed does not support re-entrancy - so calling it from
   * within a promiseDocumentFlushed callback will result in the inner call
   * throwing an NS_ERROR_FAILURE exception, and the outer Promise rejecting
   * with that exception.
   *
   * The callback function *must not make any changes which would require
   * a style or layout flush*.
   *
   * Also throws NS_ERROR_FAILURE if the window is not in a state where flushes
   * can be waited for (for example, the PresShell has not yet been created).
   *
   * @param {function} callback
   * @returns {Promise}
   */
  [NewObject, Func="nsGlobalWindowInner::IsPrivilegedChromeWindow"]
  Promise<any> promiseDocumentFlushed(PromiseDocumentFlushedCallback callback);

  [Func="IsChromeOrUAWidget"]
  readonly attribute boolean isChromeWindow;

  [Func="GleanWebidlEnabled"]
  readonly attribute GleanImpl Glean;
  [Func="GleanWebidlEnabled"]
  readonly attribute GleanPingsImpl GleanPings;
};

partial interface Window {
  [Pref="dom.vr.enabled"]
  attribute EventHandler onvrdisplayconnect;
  [Pref="dom.vr.enabled"]
  attribute EventHandler onvrdisplaydisconnect;
  [Pref="dom.vr.enabled"]
  attribute EventHandler onvrdisplayactivate;
  [Pref="dom.vr.enabled"]
  attribute EventHandler onvrdisplaydeactivate;
  [Pref="dom.vr.enabled"]
  attribute EventHandler onvrdisplaypresentchange;
};

#ifndef RELEASE_OR_BETA
// https://drafts.css-houdini.org/css-paint-api-1/#dom-window-paintworklet
partial interface Window {
    [Pref="dom.paintWorklet.enabled", Throws]
    readonly attribute Worklet paintWorklet;
};
#endif

Window includes WindowOrWorkerGlobalScope;

partial interface Window {
  [Throws]
  unsigned long requestIdleCallback(IdleRequestCallback callback,
                                    optional IdleRequestOptions options = {});
  undefined     cancelIdleCallback(unsigned long handle);
};

dictionary IdleRequestOptions {
  unsigned long timeout;
};

callback IdleRequestCallback = undefined (IdleDeadline deadline);

partial interface Window {
  /**
   * Returns a list of locales that the web content would know from the user.
   *
   * One of the fingerprinting technique is to recognize users from their locales
   * exposed to web content. For those components that would be fingerprintable
   * from the locale should call this API instead of |getRegionalPrefsLocales()|.
   *
   * If the pref is set to spoof locale setting, this function will return the
   * spoofed locale, otherwise it returns what |getRegionalPrefsLocales()| returns.
   *
   * This API always returns at least one locale.
   *
   * Example: ["en-US"]
   */
  [Func="IsChromeOrUAWidget"]
  sequence<DOMString> getWebExposedLocales();

  /**
   * Getter funcion for IntlUtils, which provides helper functions for
   * localization.
   */
  [Throws, Func="IsChromeOrUAWidget"]
  readonly attribute IntlUtils intlUtils;
};

partial interface Window {
  [SameObject, Replaceable]
  readonly attribute VisualViewport visualViewport;
};

// Used to assign marks to appear on the scrollbar when
// finding on a page.
partial interface Window {
  // The marks are values between 0 and scrollMax{X,Y} - scrollMin{X,Y}.
  [ChromeOnly]
  undefined setScrollMarks(sequence<unsigned long> marks,
                           optional boolean onHorizontalScrollbar = false);
};

dictionary WindowPostMessageOptions : StructuredSerializeOptions {
  USVString targetOrigin = "/";
};

// https://wicg.github.io/cookie-store/#Window
[SecureContext]
partial interface Window {
  [SameObject, Pref="dom.cookieStore.enabled"] readonly attribute CookieStore cookieStore;
};

// https://html.spec.whatwg.org/multipage/browsers.html#origin-keyed-agent-clusters
partial interface Window {
  [Pref="dom.origin_agent_cluster.enabled"] readonly attribute boolean originAgentCluster;
};

// https://wicg.github.io/document-picture-in-picture/#api
partial interface Window {
  [SameObject, SecureContext, Pref="dom.documentpip.enabled"]
  readonly attribute DocumentPictureInPicture documentPictureInPicture;
};

Window includes PushManagerAttribute;
