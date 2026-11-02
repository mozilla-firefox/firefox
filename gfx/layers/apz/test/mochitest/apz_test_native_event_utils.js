// documentGlobal isn't defined in content privileged windows.
/* eslint-disable mozilla/use-documentGlobal */

// Utilities for synthesizing of native events.

async function getResolution() {
  let resolution = -1; // bogus value in case DWU fails us
  // Use window.top to get the root content window which is what has
  // the resolution.
  resolution = await SpecialPowers.spawn(window.top, [], () => {
    return SpecialPowers.getDOMWindowUtils(content.window).getResolution();
  });
  return resolution;
}

function getPlatform() {
  if (navigator.platform.indexOf("Win") == 0) {
    return "windows";
  }
  if (navigator.platform.indexOf("Mac") == 0) {
    return "mac";
  }
  // Check for Android before Linux
  if (navigator.appVersion.includes("Android")) {
    return "android";
  }
  if (navigator.platform.indexOf("Linux") == 0) {
    return "linux";
  }
  return "unknown";
}

function nativeVerticalWheelEventMsg() {
  switch (getPlatform()) {
    case "windows":
      return 0x020a; // WM_MOUSEWHEEL
    case "mac":
      var useWheelCodepath = SpecialPowers.getBoolPref(
        "apz.test.mac.synth_wheel_input",
        false
      );
      // Default to 1 (kCGScrollPhaseBegan) to trigger PanGestureInput events
      // from widget code. Allow setting a pref to override this behaviour and
      // trigger ScrollWheelInput events instead.
      return useWheelCodepath ? 0 : 1;
    case "linux":
      return 4; // value is unused, pass GDK_SCROLL_SMOOTH anyway
  }
  throw new Error(
    "Native wheel events not supported on platform " + getPlatform()
  );
}

function nativeHorizontalWheelEventMsg() {
  switch (getPlatform()) {
    case "windows":
      return 0x020e; // WM_MOUSEHWHEEL
    case "mac":
      return 0; // value is unused, can be anything
    case "linux":
      return 4; // value is unused, pass GDK_SCROLL_SMOOTH anyway
  }
  throw new Error(
    "Native wheel events not supported on platform " + getPlatform()
  );
}

function nativeArrowDownKey() {
  switch (getPlatform()) {
    case "windows":
      return WIN_VK_DOWN;
    case "mac":
      return MAC_VK_DownArrow;
  }
  throw new Error(
    "Native key events not supported on platform " + getPlatform()
  );
}

function nativeArrowUpKey() {
  switch (getPlatform()) {
    case "windows":
      return WIN_VK_UP;
    case "mac":
      return MAC_VK_UpArrow;
  }
  throw new Error(
    "Native key events not supported on platform " + getPlatform()
  );
}

function targetIsWindow(aTarget) {
  return aTarget.Window && aTarget instanceof aTarget.Window;
}

function targetIsTopWindow(aTarget) {
  if (!targetIsWindow(aTarget)) {
    return false;
  }
  return aTarget == aTarget.top;
}

// Given an event target which may be a window or an element, get the associated window.
function windowForTarget(aTarget) {
  if (targetIsWindow(aTarget)) {
    return aTarget;
  }
  return aTarget.ownerDocument.defaultView;
}

// Given an event target which may be a window or an element, get the associated element.
function elementForTarget(aTarget) {
  if (targetIsWindow(aTarget)) {
    return aTarget.document.documentElement;
  }
  return aTarget;
}

// Given an event target which may be a window or an element, get the associatd nsIDOMWindowUtils.
function utilsForTarget(aTarget) {
  return SpecialPowers.getDOMWindowUtils(windowForTarget(aTarget));
}

// Given a pixel scrolling delta, converts it to the platform's native units.
function nativeScrollUnits(aTarget, aDimen) {
  switch (getPlatform()) {
    case "linux": {
      // GTK deltas are treated as line height divided by 3 by gecko.
      var targetWindow = windowForTarget(aTarget);
      var targetElement = elementForTarget(aTarget);
      var lineHeight =
        targetWindow.getComputedStyle(targetElement)["font-size"];
      return aDimen / (parseInt(lineHeight) * 3);
    }
  }
  return aDimen;
}

function parseNativeModifiers(aModifiers, aWindow = window) {
  let modifiers = 0;
  if (aModifiers.capsLockKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_CAPS_LOCK;
  }
  if (aModifiers.numLockKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_NUM_LOCK;
  }
  if (aModifiers.shiftKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_SHIFT_LEFT;
  }
  if (aModifiers.shiftRightKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_SHIFT_RIGHT;
  }
  if (aModifiers.ctrlKey) {
    modifiers |=
      SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_CONTROL_LEFT;
  }
  if (aModifiers.ctrlRightKey) {
    modifiers |=
      SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_CONTROL_RIGHT;
  }
  if (aModifiers.altKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_ALT_LEFT;
  }
  if (aModifiers.altRightKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_ALT_RIGHT;
  }
  if (aModifiers.metaKey) {
    modifiers |=
      SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_COMMAND_LEFT;
  }
  if (aModifiers.metaRightKey) {
    modifiers |=
      SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_COMMAND_RIGHT;
  }
  if (aModifiers.helpKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_HELP;
  }
  if (aModifiers.fnKey) {
    modifiers |= SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_FUNCTION;
  }
  if (aModifiers.numericKeyPadKey) {
    modifiers |=
      SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_NUMERIC_KEY_PAD;
  }

  if (aModifiers.accelKey) {
    modifiers |= _EU_isMac(aWindow)
      ? SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_COMMAND_LEFT
      : SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_CONTROL_LEFT;
  }
  if (aModifiers.accelRightKey) {
    modifiers |= _EU_isMac(aWindow)
      ? SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_COMMAND_RIGHT
      : SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_CONTROL_RIGHT;
  }
  if (aModifiers.altGrKey) {
    modifiers |= _EU_isMac(aWindow)
      ? SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_ALT_LEFT
      : SpecialPowers.Ci.nsIDOMWindowUtils.NATIVE_MODIFIER_ALT_GRAPH;
  }
  return modifiers;
}

// Several event sythesization functions below (and their helpers) take a "target"
// parameter which may be either an element or a window. For such functions,
// the target's "bounding rect" refers to the bounding client rect for an element,
// and the window's origin for a window.
// Not all functions have been "upgraded" to allow a window argument yet; feel
// free to upgrade others as necessary.

// Get the origin of |aTarget| relative to the root content document's
// visual viewport in CSS coordinates.
// |aTarget| may be an element (contained in the root content document or
// a subdocument) or, as a special case, the root content window.
// FIXME: Support iframe windows as targets.
function _getTargetRect(aTarget, atCenter) {
  let rect = { left: 0, top: 0, width: 0, height: 0 };

  aTarget = SpecialPowers.wrap(aTarget);
  let containingWindow = null;
  if (
    aTarget instanceof Window ||
    (aTarget.Window && aTarget instanceof aTarget.Window)
  ) {
    // If the target is the root content window, its origin relative
    // to the visual viewport is (0, 0).

    // FIXME: Compute proper rect against the root content window

    // leave rect as all 0's. The top/left is correct, but the width/height is
    // not necessarily correct, just assert that we are not sending event to the
    // center of the target so that we are not using the width/height.
    ok(!atCenter, "atCenter not supported with window targets, todo");
    containingWindow = aTarget;
  } else {
    // Otherwise, we have an element. Start with the origin of
    // its bounding client rect which is relative to the enclosing
    // document's layout viewport. Note that for iframes, the
    // layout viewport is also the visual viewport.

    const boundingClientRect = aTarget.getBoundingClientRect();
    rect.left = boundingClientRect.left;
    rect.top = boundingClientRect.top;
    rect.width = boundingClientRect.width;
    rect.height = boundingClientRect.height;
    containingWindow = aTarget.ownerDocument.defaultView;
  }

  // Iterate up the window hierarchy until we reach the root
  // content window, adding the offsets of any iframe windows
  // relative to their parent window.
  while (containingWindow.browsingContext.embedderElement) {
    const iframe = containingWindow.browsingContext.embedderElement;
    // The offset of the iframe window relative to the parent window
    // includes the iframe's border, and the iframe's origin in its
    // containing document.
    const style = iframe.ownerDocument.defaultView.getComputedStyle(iframe);
    const borderLeft = parseFloat(style.borderLeftWidth) || 0;
    const borderTop = parseFloat(style.borderTopWidth) || 0;
    const borderRight = parseFloat(style.borderRightWidth) || 0;
    const borderBottom = parseFloat(style.borderBottomWidth) || 0;
    const paddingLeft = parseFloat(style.paddingLeft) || 0;
    const paddingTop = parseFloat(style.paddingTop) || 0;
    const paddingRight = parseFloat(style.paddingRight) || 0;
    const paddingBottom = parseFloat(style.paddingBottom) || 0;
    const iframeRect = iframe.getBoundingClientRect();
    rect.left += iframeRect.left + borderLeft + paddingLeft;
    rect.top += iframeRect.top + borderTop + paddingTop;
    if (
      rect.left + rect.width >
      iframeRect.right - borderRight - paddingRight
    ) {
      rect.width = Math.max(
        iframeRect.right - borderRight - paddingRight - rect.left,
        0
      );
    }
    if (
      rect.top + rect.height >
      iframeRect.bottom - borderBottom - paddingBottom
    ) {
      rect.height = Math.max(
        iframeRect.bottom - borderBottom - paddingBottom - rect.top,
        0
      );
    }
    aTarget = iframe;
    containingWindow = aTarget.ownerDocument.defaultView;
  }

  return { rect, window: containingWindow };
}

// Returns the in-process root window for the given |aWindow|.
function getInProcessRootWindow(aWindow) {
  let window = SpecialPowers.wrap(aWindow);
  while (window.browsingContext.embedderElement) {
    window = window.browsingContext.embedderElement.ownerDocument.defaultView;
  }
  return window;
}

// Convert (offsetX, offsetY) of target or center of it, in CSS pixels to device
// pixels relative to the screen.
// TODO: this function currently does not incorporate some CSS transforms on
// elements enclosing target, e.g. scale transforms.
async function coordinatesRelativeToScreen(aParams) {
  const {
    target, // The target element or window
    offsetX, // X offset relative to `target`
    offsetY, // Y offset relative to `target`
    atCenter, // Instead of offsetX/offsetY, return center of `target`
  } = aParams;
  // Note that |window| might not be the root content window, for two
  // possible reasons:
  //  1. The mochitest that's calling into this function is not using a mechanism
  //     like runSubtestsSeriallyInFreshWindows() to load the test page in
  //     a top-level context, so it's loaded into an iframe by the mochitest
  //     harness.
  //  2. The mochitest itself creates an iframe and calls this function from
  //     script running in the context of the iframe.
  // Since the resolution applies to the top level content document, below we
  // use the mozInnerScreen{X,Y} of the top level content window (window.top)
  // only for the case where this function gets called in the top level content
  // document. In other cases we use nsIDOMWindowUtils.toScreenRect().

  // We do often specify `window` as the target, if it's the top level window,
  // `nsIDOMWindowUtils.toScreenRect` isn't suitable because the function is
  // supposed to be called with values in the document coords, so for example
  // if desktop zoom is being applied, (0, 0) in the document coords might be
  // outside of the visual viewport, i.e. it's going to be negative with the
  // `toScreenRect` conversion, whereas the call sites with `window` of this
  // function expect (0, 0) position should be the visual viport's offset. So
  // in such cases we simply use mozInnerScreen{X,Y} to convert the given value
  // to the screen coords.
  if (target instanceof Window && window.parent == window) {
    const resolution = await getResolution();
    const deviceScale = window.devicePixelRatio;
    return {
      x:
        window.mozInnerScreenX * deviceScale +
        (atCenter ? 0 : offsetX) * resolution * deviceScale,
      y:
        window.mozInnerScreenY * deviceScale +
        (atCenter ? 0 : offsetY) * resolution * deviceScale,
    };
  }

  const rectAndWindow = _getTargetRect(target, atCenter);

  const inProcessRootWindow = getInProcessRootWindow(window);

  if (
    !(inProcessRootWindow.location.href === rectAndWindow.window.location.href)
  ) {
    info(
      "warning: coordinatesRelativeToScreen using coords based on one window in another, this will likely produce incorrect results"
    );
    info(
      "inProcessRootWindow.location.href " + inProcessRootWindow.location.href
    );
    info(
      "rectAndWindow.window.location.href " + rectAndWindow.window.location.href
    );
  }
  // This doesn't hold yet.
  //ok(inProcessRootWindow.location.href === rectAndWindow.window.location.href, "same root window");

  const utils = SpecialPowers.wrap(
    SpecialPowers.getDOMWindowUtils(inProcessRootWindow)
  );
  const positionInScreenCoords = utils.toScreenRect(
    rectAndWindow.rect.left +
      (atCenter ? rectAndWindow.rect.width / 2 : offsetX),
    rectAndWindow.rect.top +
      (atCenter ? rectAndWindow.rect.height / 2 : offsetY),
    0,
    0
  );

  return {
    x: positionInScreenCoords.x,
    y: positionInScreenCoords.y,
  };
}

// Get the bounding box of aElement, and return it in device pixels
// relative to the screen.
// TODO: This function should probably take into account the resolution and
//       the relative viewport rect like coordinatesRelativeToScreen() does.
function rectRelativeToScreen(aElement) {
  var targetWindow = aElement.ownerDocument.defaultView;
  var scale = targetWindow.devicePixelRatio;
  var rect = aElement.getBoundingClientRect();
  return {
    x: (targetWindow.mozInnerScreenX + rect.left) * scale,
    y: (targetWindow.mozInnerScreenY + rect.top) * scale,
    width: rect.width * scale,
    height: rect.height * scale,
  };
}

// Synthesizes a native mousewheel event and returns immediately. This does not
// guarantee anything; you probably want to use one of the other functions below
// which actually wait for results.
// aX and aY are relative to the top-left of |aTarget|'s bounding rect.
// aDeltaX and aDeltaY are pixel deltas, and aObserver can be left undefined
// if not needed.
async function synthesizeNativeWheel(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aObserver
) {
  var pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  if (aDeltaX && aDeltaY) {
    throw new Error(
      "Simultaneous wheeling of horizontal and vertical is not supported on all platforms."
    );
  }
  aDeltaX = nativeScrollUnits(aTarget, aDeltaX);
  aDeltaY = nativeScrollUnits(aTarget, aDeltaY);
  var msg = aDeltaX
    ? nativeHorizontalWheelEventMsg()
    : nativeVerticalWheelEventMsg();
  var utils = utilsForTarget(aTarget);
  var element = elementForTarget(aTarget);
  utils.sendNativeMouseScrollEvent(
    pt.x,
    pt.y,
    msg,
    aDeltaX,
    aDeltaY,
    0,
    0,
    // Specify MOUSESCROLL_SCROLL_LINES if the test wants to run through wheel
    // input code path on Mac since it's normal mouse wheel inputs.
    SpecialPowers.getBoolPref("apz.test.mac.synth_wheel_input", false)
      ? SpecialPowers.DOMWindowUtils.MOUSESCROLL_SCROLL_LINES
      : 0,
    element,
    aObserver
  );
  return true;
}

// Synthesizes a native pan gesture event and returns immediately.
// NOTE: This works only on Mac.
// You can specify kCGScrollPhaseBegan = 1, kCGScrollPhaseChanged = 2 and
// kCGScrollPhaseEnded = 4 for |aPhase|.
async function synthesizeNativePanGestureEvent(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aPhase,
  aObserver
) {
  if (getPlatform() != "mac") {
    throw new Error(
      `synthesizeNativePanGestureEvent doesn't work on ${getPlatform()}`
    );
  }

  var pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  if (aDeltaX && aDeltaY) {
    throw new Error(
      "Simultaneous panning of horizontal and vertical is not supported."
    );
  }

  aDeltaX = nativeScrollUnits(aTarget, aDeltaX);
  aDeltaY = nativeScrollUnits(aTarget, aDeltaY);

  var element = elementForTarget(aTarget);
  var utils = utilsForTarget(aTarget);
  utils.sendNativeMouseScrollEvent(
    pt.x,
    pt.y,
    aPhase,
    aDeltaX,
    aDeltaY,
    0 /* deltaZ */,
    0 /* modifiers */,
    0 /* scroll event unit pixel */,
    element,
    aObserver
  );

  return true;
}

// Sends a native touchpad pan event and resolve the returned promise once the
// request has been successfully made to the OS.
// NOTE: This works only on Windows and Linux.
// You can specify nsIDOMWindowUtils.PHASE_BEGIN, PHASE_UPDATE and PHASE_END
// for |aPhase|.
async function promiseNativeTouchpadPanEventAndWaitForObserver(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aPhase
) {
  if (getPlatform() != "windows" && getPlatform() != "linux") {
    throw new Error(
      `promiseNativeTouchpadPanEventAndWaitForObserver doesn't work on ${getPlatform()}`
    );
  }

  let pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });

  const utils = utilsForTarget(aTarget);

  return new Promise(resolve => {
    utils.sendNativeTouchpadPan(
      aPhase,
      pt.x,
      pt.y,
      aDeltaX,
      aDeltaY,
      0,
      resolve
    );
  });
}

async function synthesizeSimpleGestureEvent(
  aElement,
  aType,
  aX,
  aY,
  aDirection,
  aDelta,
  aModifiers,
  aClickCount
) {
  let pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aElement,
  });

  let utils = utilsForTarget(aElement);
  utils.sendSimpleGestureEvent(
    aType,
    pt.x,
    pt.y,
    aDirection,
    aDelta,
    aModifiers,
    aClickCount
  );
}

// Synthesizes a native pan gesture event and resolve the returned promise once the
// request has been successfully made to the OS.
function promiseNativePanGestureEventAndWaitForObserver(
  aElement,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aPhase
) {
  return new Promise(resolve => {
    synthesizeNativePanGestureEvent(
      aElement,
      aX,
      aY,
      aDeltaX,
      aDeltaY,
      aPhase,
      resolve
    );
  });
}

// Synthesizes a native mousewheel event and resolve the returned promise once the
// request has been successfully made to the OS. This does not necessarily
// guarantee that the OS generates the event we requested. See
// synthesizeNativeWheel for details on the parameters.
function promiseNativeWheelAndWaitForObserver(
  aElement,
  aX,
  aY,
  aDeltaX,
  aDeltaY
) {
  return new Promise(resolve => {
    synthesizeNativeWheel(aElement, aX, aY, aDeltaX, aDeltaY, resolve);
  });
}

// Synthesizes a native mousewheel event and resolve the returned promise once the
// wheel event is dispatched to |aTarget|'s containing window. If the event
// targets content in a subdocument, |aTarget| should be inside the
// subdocument (or the subdocument's window). See synthesizeNativeWheel for
// details on the other parameters.
function promiseNativeWheelAndWaitForWheelEvent(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY
) {
  return new Promise((resolve, reject) => {
    var targetWindow = windowForTarget(aTarget);
    targetWindow.addEventListener(
      "wheel",
      function () {
        setTimeout(resolve, 0);
      },
      { once: true }
    );
    try {
      synthesizeNativeWheel(aTarget, aX, aY, aDeltaX, aDeltaY);
    } catch (e) {
      reject(e);
    }
  });
}

// Synthesizes a native mousewheel event and resolves the returned promise once the
// first resulting scroll event is dispatched to |aTarget|'s containing window.
// If the event targets content in a subdocument, |aTarget| should be inside
// the subdocument (or the subdocument's window).  See synthesizeNativeWheel
// for details on the other parameters.
function promiseNativeWheelAndWaitForScrollEvent(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY
) {
  return new Promise((resolve, reject) => {
    var targetWindow = windowForTarget(aTarget);
    targetWindow.addEventListener(
      "scroll",
      function () {
        setTimeout(resolve, 0);
      },
      { capture: true, once: true }
    ); // scroll events don't always bubble
    try {
      synthesizeNativeWheel(aTarget, aX, aY, aDeltaX, aDeltaY);
    } catch (e) {
      reject(e);
    }
  });
}

async function synthesizeTouchpadPinch(scales, focusX, focusY, options) {
  var scalesAndFoci = [];

  for (let i = 0; i < scales.length; i++) {
    scalesAndFoci.push([scales[i], focusX, focusY]);
  }

  await synthesizeTouchpadGesture(scalesAndFoci, options);
}

// scalesAndFoci is an array of [scale, focusX, focuxY] tuples.
async function synthesizeTouchpadGesture(scalesAndFoci, options) {
  // Check for options, fill in defaults if appropriate.
  let waitForTransformEnd =
    options.waitForTransformEnd !== undefined
      ? options.waitForTransformEnd
      : true;
  let waitForFrames =
    options.waitForFrames !== undefined ? options.waitForFrames : false;

  // Register the listener for the TransformEnd observer topic
  let transformEndPromise = promiseTransformEnd();

  var modifierFlags = 0;
  var utils = utilsForTarget(document.body);
  for (let i = 0; i < scalesAndFoci.length; i++) {
    var pt = await coordinatesRelativeToScreen({
      offsetX: scalesAndFoci[i][1],
      offsetY: scalesAndFoci[i][2],
      target: document.body,
    });
    var phase;
    if (i === 0) {
      phase = SpecialPowers.DOMWindowUtils.PHASE_BEGIN;
    } else if (i === scalesAndFoci.length - 1) {
      phase = SpecialPowers.DOMWindowUtils.PHASE_END;
    } else {
      phase = SpecialPowers.DOMWindowUtils.PHASE_UPDATE;
    }
    utils.sendNativeTouchpadPinch(
      phase,
      scalesAndFoci[i][0],
      pt.x,
      pt.y,
      modifierFlags
    );
    if (waitForFrames) {
      await promiseFrame();
    }
  }

  // Wait for TransformEnd to fire.
  if (waitForTransformEnd) {
    await transformEndPromise;
  }
}

async function synthesizeTouchpadPan(
  focusX,
  focusY,
  deltaXs,
  deltaYs,
  options
) {
  // Check for options, fill in defaults if appropriate.
  let waitForTransformEnd =
    options.waitForTransformEnd !== undefined
      ? options.waitForTransformEnd
      : true;
  let waitForFrames =
    options.waitForFrames !== undefined ? options.waitForFrames : false;

  // Register the listener for the TransformEnd observer topic
  let transformEndPromise = promiseTransformEnd();

  var modifierFlags = 0;
  var pt = await coordinatesRelativeToScreen({
    offsetX: focusX,
    offsetY: focusY,
    target: document.body,
  });
  var utils = utilsForTarget(document.body);
  for (let i = 0; i < deltaXs.length; i++) {
    var phase;
    if (i === 0) {
      phase = SpecialPowers.DOMWindowUtils.PHASE_BEGIN;
    } else if (i === deltaXs.length - 1) {
      phase = SpecialPowers.DOMWindowUtils.PHASE_END;
    } else {
      phase = SpecialPowers.DOMWindowUtils.PHASE_UPDATE;
    }
    utils.sendNativeTouchpadPan(
      phase,
      pt.x,
      pt.y,
      deltaXs[i],
      deltaYs[i],
      modifierFlags
    );
    if (waitForFrames) {
      await promiseFrame();
    }
  }

  // Wait for TransformEnd to fire.
  if (waitForTransformEnd) {
    await transformEndPromise;
  }
}

// Synthesizes a native touch event and dispatches it. aX and aY in CSS pixels
// relative to the top-left of |aTarget|'s bounding rect.
async function synthesizeNativeTouch(
  aTarget,
  aX,
  aY,
  aType,
  aObserver = null,
  aTouchId = 0
) {
  var pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  var utils = utilsForTarget(aTarget);
  utils.sendNativeTouchPoint(
    aTouchId,
    aType,
    pt.x,
    pt.y,
    1,
    90,
    aObserver,
    aTarget instanceof Element ? aTarget : null
  );
  return true;
}

function sendBasicNativePointerInput(
  utils,
  aId,
  aPointerType,
  aState,
  aX,
  aY,
  aObserver,
  aElement,
  { pressure = 1, twist = 0, tiltX = 0, tiltY = 0, button = 0 } = {}
) {
  switch (aPointerType) {
    case "touch":
      utils.sendNativeTouchPoint(
        aId,
        aState,
        aX,
        aY,
        pressure,
        90,
        aObserver,
        aElement
      );
      break;
    case "pen":
      utils.sendNativePenInput(
        aId,
        aState,
        aX,
        aY,
        pressure,
        twist,
        tiltX,
        tiltY,
        button,
        aObserver,
        aElement
      );
      break;
    default:
      throw new Error(`Not supported: ${aPointerType}`);
  }
}

async function promiseNativePointerInput(
  aTarget,
  aPointerType,
  aState,
  aX,
  aY,
  options
) {
  const pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  const utils = utilsForTarget(aTarget);
  return new Promise(resolve => {
    sendBasicNativePointerInput(
      utils,
      options?.pointerId ?? 0,
      aPointerType,
      aState,
      pt.x,
      pt.y,
      resolve,
      aTarget instanceof Element ? aTarget : null,
      options
    );
  });
}

/**
 * Function to generate native pointer events as a sequence.
 *
 * @param aTarget is the element or window whose bounding rect the coordinates are
 *   relative to.
 * @param aPointerType "touch" or "pen".
 * @param aPositions is a 2D array of position data. It is indexed as [row][column],
 *   where advancing the row counter moves forward in time, and each column
 *   represents a single pointer. Each row must have exactly
 *   the same number of columns, and the number of columns must match the length
 *   of the aPointerIds parameter.
 *   For each row, each entry is either an object with x and y fields,
 *   or a null. A null value indicates that the pointer should be "lifted"
 *   (i.e. send a touchend for that touch input). A non-null value therefore
 *   indicates the position of the pointer input.
 *   This function takes care of the state tracking necessary to send
 *   pointerup/pointerdown inputs as necessary as the pointers go up and down.
 * @param aObserver is the observer that will get registered on the very last
 *   native pointer synthesis call this function makes.
 * @param aPointerIds is an array holding the pointer ID values.
 */
async function synthesizeNativePointerSequences(
  aTarget,
  aPointerType,
  aPositions,
  aObserver = null,
  aPointerIds = [0],
  options
) {
  // We use lastNonNullValue to figure out which synthesizeNativeTouch call
  // will be the last one we make, so that we can register aObserver on it.
  var lastNonNullValue = -1;
  for (let i = 0; i < aPositions.length; i++) {
    if (aPositions[i] == null) {
      throw new Error(`aPositions[${i}] was unexpectedly null`);
    }
    if (aPositions[i].length != aPointerIds.length) {
      throw new Error(
        `aPositions[${i}] did not have the expected number of positions; ` +
          `expected ${aPointerIds.length} pointers but found ${aPositions[i].length}`
      );
    }
    for (let j = 0; j < aPointerIds.length; j++) {
      if (aPositions[i][j] != null) {
        lastNonNullValue = i * aPointerIds.length + j;
        // Do the conversion to screen space before actually synthesizing
        // the events, otherwise the screen space may change as a result of
        // the touch inputs and the conversion may not work as intended.
        aPositions[i][j] = await coordinatesRelativeToScreen({
          offsetX: aPositions[i][j].x,
          offsetY: aPositions[i][j].y,
          target: aTarget,
        });
      }
    }
  }
  if (lastNonNullValue < 0) {
    throw new Error("All values in positions array were null!");
  }

  // Insert a row of nulls at the end of aPositions, to ensure that all
  // touches get removed. If the touches have already been removed this will
  // just add an extra no-op iteration in the aPositions loop below.
  var allNullRow = new Array(aPointerIds.length);
  allNullRow.fill(null);
  aPositions.push(allNullRow);

  // The last sendNativeTouchPoint call will be the TOUCH_REMOVE which happens
  // one iteration of aPosition after the last non-null value.
  var lastSynthesizeCall = lastNonNullValue + aPointerIds.length;

  // track which touches are down and which are up. start with all up
  var currentPositions = new Array(aPointerIds.length);
  currentPositions.fill(null);

  var utils = utilsForTarget(aTarget);
  // Iterate over the position data now, and generate the touches requested
  for (let i = 0; i < aPositions.length; i++) {
    for (let j = 0; j < aPointerIds.length; j++) {
      if (aPositions[i][j] == null) {
        // null means lift the finger
        if (currentPositions[j] == null) {
          // it's already lifted, do nothing
        } else {
          // synthesize the touch-up. If this is the last call we're going to
          // make, pass the observer as well
          var thisIndex = i * aPointerIds.length + j;
          var observer = lastSynthesizeCall == thisIndex ? aObserver : null;
          sendBasicNativePointerInput(
            utils,
            aPointerIds[j],
            aPointerType,
            SpecialPowers.DOMWindowUtils.TOUCH_REMOVE,
            currentPositions[j].x,
            currentPositions[j].y,
            observer,
            aTarget instanceof Element ? aTarget : null,
            options
          );
          currentPositions[j] = null;
        }
      } else {
        sendBasicNativePointerInput(
          utils,
          aPointerIds[j],
          aPointerType,
          SpecialPowers.DOMWindowUtils.TOUCH_CONTACT,
          aPositions[i][j].x,
          aPositions[i][j].y,
          null,
          aTarget instanceof Element ? aTarget : null,
          options
        );
        currentPositions[j] = aPositions[i][j];
      }
    }
  }
  return true;
}

async function synthesizeNativeTouchSequences(
  aTarget,
  aPositions,
  aObserver = null,
  aTouchIds = [0]
) {
  await synthesizeNativePointerSequences(
    aTarget,
    "touch",
    aPositions,
    aObserver,
    aTouchIds
  );
}

async function synthesizeNativePointerDrag(
  aTarget,
  aPointerType,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aObserver = null,
  aPointerId = 0,
  options
) {
  var steps = Math.max(Math.abs(aDeltaX), Math.abs(aDeltaY));
  var positions = [[{ x: aX, y: aY }]];
  for (var i = 1; i < steps; i++) {
    var dx = i * (aDeltaX / steps);
    var dy = i * (aDeltaY / steps);
    var pos = { x: aX + dx, y: aY + dy };
    positions.push([pos]);
  }
  positions.push([{ x: aX + aDeltaX, y: aY + aDeltaY }]);
  return synthesizeNativePointerSequences(
    aTarget,
    aPointerType,
    positions,
    aObserver,
    [aPointerId],
    options
  );
}

// Note that when calling this function you'll want to make sure that the pref
// "apz.touch_start_tolerance" is set to 0, or some of the touchmove will get
// consumed to overcome the panning threshold.
async function synthesizeNativeTouchDrag(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aObserver = null,
  aTouchId = 0
) {
  return synthesizeNativePointerDrag(
    aTarget,
    "touch",
    aX,
    aY,
    aDeltaX,
    aDeltaY,
    aObserver,
    aTouchId
  );
}

function promiseNativePointerDrag(
  aTarget,
  aPointerType,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aPointerId = 0,
  options
) {
  return new Promise(resolve => {
    synthesizeNativePointerDrag(
      aTarget,
      aPointerType,
      aX,
      aY,
      aDeltaX,
      aDeltaY,
      resolve,
      aPointerId,
      options
    );
  });
}

// Promise-returning variant of synthesizeNativeTouchDrag
function promiseNativeTouchDrag(
  aTarget,
  aX,
  aY,
  aDeltaX,
  aDeltaY,
  aTouchId = 0
) {
  return new Promise(resolve => {
    synthesizeNativeTouchDrag(
      aTarget,
      aX,
      aY,
      aDeltaX,
      aDeltaY,
      resolve,
      aTouchId
    );
  });
}

// Tapping is essentially a dragging with no move
function promiseNativePointerTap(aTarget, aPointerType, aX, aY, options) {
  return promiseNativePointerDrag(
    aTarget,
    aPointerType,
    aX,
    aY,
    0,
    0,
    options?.pointerId ?? 0,
    options
  );
}

async function synthesizeNativeTap(aTarget, aX, aY, aObserver = null) {
  var pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  let utils = utilsForTarget(aTarget);
  utils.sendNativeTouchTap(pt.x, pt.y, false, aObserver);
  return true;
}

// only currently implemented on macOS
async function synthesizeNativeTouchpadDoubleTap(aTarget, aX, aY) {
  ok(
    getPlatform() == "mac",
    "only implemented on mac. implement sendNativeTouchpadDoubleTap for this platform," +
      " see bug 1696802 for how it was done on macOS"
  );
  let pt = await coordinatesRelativeToScreen({
    offsetX: aX,
    offsetY: aY,
    target: aTarget,
  });
  let utils = utilsForTarget(aTarget);
  utils.sendNativeTouchpadDoubleTap(pt.x, pt.y, 0);
  return true;
}

// If the event targets content in a subdocument, |aTarget| should be inside the
// subdocument (or the subdocument window).
async function synthesizeNativeMouseEventWithAPZ(aParams, aObserver = null) {
  if (aParams.win !== undefined) {
    throw Error(
      "Are you trying to use EventUtils' API? `win` won't be used with synthesizeNativeMouseClickWithAPZ."
    );
  }
  if (aParams.scale !== undefined) {
    throw Error(
      "Are you trying to use EventUtils' API? `scale` won't be used with synthesizeNativeMouseClickWithAPZ."
    );
  }
  if (aParams.elementOnWidget !== undefined) {
    throw Error(
      "Are you trying to use EventUtils' API? `elementOnWidget` won't be used with synthesizeNativeMouseClickWithAPZ."
    );
  }
  const {
    type, // "click", "mousedown", "mouseup" or "mousemove"
    target, // Origin of offsetX and offsetY, must be an element
    offsetX, // X offset in `target` in CSS Pixels
    offsetY, // Y offset in `target` in CSS pixels
    atCenter, // Instead of offsetX/Y, synthesize the event at center of `target`
    screenX, // X offset in screen in device pixels, offsetX/Y nor atCenter must not be set if this is set
    screenY, // Y offset in screen in device pixels, offsetX/Y nor atCenter must not be set if this is set
    button = 0, // if "click", "mousedown", "mouseup", set same value as DOM MouseEvent.button
    modifiers = {}, // Active modifiers, see `parseNativeModifiers`
  } = aParams;
  if (atCenter) {
    if (offsetX != undefined || offsetY != undefined) {
      throw Error(
        `atCenter is specified, but offsetX (${offsetX}) and/or offsetY (${offsetY}) are also specified`
      );
    }
    if (screenX != undefined || screenY != undefined) {
      throw Error(
        `atCenter is specified, but screenX (${screenX}) and/or screenY (${screenY}) are also specified`
      );
    }
  } else if (offsetX != undefined && offsetY != undefined) {
    if (screenX != undefined || screenY != undefined) {
      throw Error(
        `offsetX/Y are specified, but screenX (${screenX}) and/or screenY (${screenY}) are also specified`
      );
    }
  } else if (screenX != undefined && screenY != undefined) {
    if (offsetX != undefined || offsetY != undefined) {
      throw Error(
        `screenX/Y are specified, but offsetX (${offsetX}) and/or offsetY (${offsetY}) are also specified`
      );
    }
  }
  const pt = await (async () => {
    if (screenX != undefined) {
      return { x: screenX, y: screenY };
    }
    return coordinatesRelativeToScreen({
      offsetX,
      offsetY,
      atCenter,
      target,
    });
  })();
  const utils = utilsForTarget(target);
  const element = elementForTarget(target);
  const modifierFlags = parseNativeModifiers(modifiers);
  if (type === "click") {
    utils.sendNativeMouseEvent(
      pt.x,
      pt.y,
      utils.NATIVE_MOUSE_MESSAGE_BUTTON_DOWN,
      button,
      modifierFlags,
      element,
      function () {
        utils.sendNativeMouseEvent(
          pt.x,
          pt.y,
          utils.NATIVE_MOUSE_MESSAGE_BUTTON_UP,
          button,
          modifierFlags,
          element,
          aObserver
        );
      }
    );
    return;
  }

  utils.sendNativeMouseEvent(
    pt.x,
    pt.y,
    (() => {
      switch (type) {
        case "mousedown":
          return utils.NATIVE_MOUSE_MESSAGE_BUTTON_DOWN;
        case "mouseup":
          return utils.NATIVE_MOUSE_MESSAGE_BUTTON_UP;
        case "mousemove":
          return utils.NATIVE_MOUSE_MESSAGE_MOVE;
        default:
          throw Error(`Invalid type is specified: ${type}`);
      }
    })(),
    button,
    modifierFlags,
    element,
    aObserver
  );
}

function promiseNativeMouseEventWithAPZ(aParams) {
  return new Promise(resolve =>
    synthesizeNativeMouseEventWithAPZ(aParams, resolve)
  );
}

// See synthesizeNativeMouseEventWithAPZ for the detail of aParams.
function promiseNativeMouseEventWithAPZAndWaitForEvent(aParams) {
  return new Promise(resolve => {
    const targetWindow = windowForTarget(aParams.target);
    const eventType = aParams.eventTypeToWait || aParams.type;
    targetWindow.addEventListener(eventType, resolve, {
      once: true,
    });
    synthesizeNativeMouseEventWithAPZ(aParams);
  });
}

// Move the mouse to (dx, dy) relative to |target|, and scroll the wheel
// at that location.
// Moving the mouse is necessary to avoid wheel events from two consecutive
// promiseMoveMouseAndScrollWheelOver() calls on different elements being incorrectly
// considered as part of the same wheel transaction.
// We also wait for the mouse move event to be processed before sending the
// wheel event, otherwise there is a chance they might get reordered, and
// we have the transaction problem again.
// This function returns a promise that is resolved when the resulting wheel
// (if waitForScroll = false) or scroll (if waitForScroll = true) event is
// received.
function promiseMoveMouseAndScrollWheelOver(
  target,
  dx,
  dy,
  waitForScroll = true,
  scrollDelta = 10
) {
  let p = promiseNativeMouseEventWithAPZAndWaitForEvent({
    type: "mousemove",
    target,
    offsetX: dx,
    offsetY: dy,
  });
  if (waitForScroll) {
    p = p.then(() => {
      info(
        "Printing something here to avoid failure; see https://bugzilla.mozilla.org/show_bug.cgi?id=1776963"
      );
      return promiseNativeWheelAndWaitForScrollEvent(
        target,
        dx,
        dy,
        0,
        -scrollDelta
      );
    });
  } else {
    p = p.then(() => {
      return promiseNativeWheelAndWaitForWheelEvent(
        target,
        dx,
        dy,
        0,
        -scrollDelta
      );
    });
  }
  return p;
}

async function scrollbarDragStart(aTarget, aScaleFactor) {
  var targetElement = elementForTarget(aTarget);
  var w = {},
    h = {};
  utilsForTarget(aTarget).getScrollbarSizes(targetElement, w, h);
  var verticalScrollbarWidth = w.value;
  if (verticalScrollbarWidth == 0) {
    return null;
  }

  var upArrowHeight = verticalScrollbarWidth; // assume square scrollbar buttons
  var startX = targetElement.clientWidth + verticalScrollbarWidth / 2;
  var startY = upArrowHeight + 5; // start dragging somewhere in the thumb
  startX *= aScaleFactor;
  startY *= aScaleFactor;

  // targetElement.clientWidth is unaffected by the zoom, but if the target
  // is the root content window, the distance from the window origin to the
  // scrollbar in CSS pixels does decrease proportionally to the zoom,
  // so the CSS coordinates we return need to be scaled accordingly.
  if (targetIsTopWindow(aTarget)) {
    var resolution = await getResolution();
    startX /= resolution;
    startY /= resolution;
  }

  return { x: startX, y: startY };
}

// Synthesizes events to drag |target|'s vertical scrollbar by the distance
// specified, synthesizing a mousemove for each increment as specified.
// Returns null if the element doesn't have a vertical scrollbar. Otherwise,
// returns an async function that should be invoked after the mousemoves have been
// processed by the widget code, to end the scrollbar drag. Mousemoves being
// processed by the widget code can be detected by listening for the mousemove
// events in the caller, or for some other event that is triggered by the
// mousemove, such as the scroll event resulting from the scrollbar drag.
// The aScaleFactor argument should be provided if the scrollframe has been
// scaled by an enclosing CSS transform. (TODO: this is a workaround for the
// fact that coordinatesRelativeToScreen is supposed to do this automatically
// but it currently does not).
// Note: helper_scrollbar_snap_bug1501062.html contains a copy of this code
// with modifications. Fixes here should be copied there if appropriate.
// |target| can be an element (for subframes) or a window (for root frames).
async function promiseVerticalScrollbarDrag(
  aTarget,
  aDistance = 20,
  aIncrement = 5,
  aScaleFactor = 1
) {
  var startPoint = await scrollbarDragStart(aTarget, aScaleFactor);
  var targetElement = elementForTarget(aTarget);
  if (startPoint == null) {
    return null;
  }

  dump(
    "Starting drag at " +
      startPoint.x +
      ", " +
      startPoint.y +
      " from top-left of #" +
      targetElement.id +
      "\n"
  );

  // Move the mouse to the scrollbar thumb and drag it down
  await promiseNativeMouseEventWithAPZ({
    target: aTarget,
    offsetX: startPoint.x,
    offsetY: startPoint.y,
    type: "mousemove",
  });
  // mouse down
  await promiseNativeMouseEventWithAPZ({
    target: aTarget,
    offsetX: startPoint.x,
    offsetY: startPoint.y,
    type: "mousedown",
  });
  // drag vertically by |aIncrement| until we reach the specified distance
  for (var y = aIncrement; y < aDistance; y += aIncrement) {
    await promiseNativeMouseEventWithAPZ({
      target: aTarget,
      offsetX: startPoint.x,
      offsetY: startPoint.y + y,
      type: "mousemove",
    });
  }
  await promiseNativeMouseEventWithAPZ({
    target: aTarget,
    offsetX: startPoint.x,
    offsetY: startPoint.y + aDistance,
    type: "mousemove",
  });

  // and return an async function to call afterwards to finish up the drag
  return async function () {
    dump("Finishing drag of #" + targetElement.id + "\n");
    await promiseNativeMouseEventWithAPZ({
      target: aTarget,
      offsetX: startPoint.x,
      offsetY: startPoint.y + aDistance,
      type: "mouseup",
    });
  };
}

// This is similar to promiseVerticalScrollbarDrag except this triggers
// the vertical scrollbar drag with a touch drag input. This function
// returns true if a scrollbar was present and false if no scrollbar
// was found for the given element.
async function promiseVerticalScrollbarTouchDrag(
  aTarget,
  aDistance = 20,
  aScaleFactor = 1
) {
  var startPoint = await scrollbarDragStart(aTarget, aScaleFactor);
  var targetElement = elementForTarget(aTarget);
  if (startPoint == null) {
    return false;
  }

  dump(
    "Starting touch drag at " +
      startPoint.x +
      ", " +
      startPoint.y +
      " from top-left of #" +
      targetElement.id +
      "\n"
  );

  await promiseNativeTouchDrag(
    aTarget,
    startPoint.x,
    startPoint.y,
    0,
    aDistance
  );

  return true;
}

// Synthesizes a native mouse drag, starting at offset (mouseX, mouseY) from
// the given target. The drag occurs in the given number of steps, to a final
// destination of (mouseX + distanceX, mouseY + distanceY) from the target.
// Returns a promise (wrapped in a function, so it doesn't execute immediately)
// that should be awaited after the mousemoves have been processed by the widget
// code, to end the drag. This is important otherwise the OS can sometimes
// reorder the events and the drag doesn't have the intended effect (see
// bug 1368603).
// Example usage:
//   let dragFinisher = await promiseNativeMouseDrag(myElement, 0, 0);
//   await myIndicationThatDragHadAnEffect;
//   await dragFinisher();
async function promiseNativeMouseDrag(
  target,
  mouseX,
  mouseY,
  distanceX = 20,
  distanceY = 20,
  steps = 20
) {
  var targetElement = elementForTarget(target);
  dump(
    "Starting drag at " +
      mouseX +
      ", " +
      mouseY +
      " from top-left of #" +
      targetElement.id +
      "\n"
  );

  // Move the mouse to the target position
  await promiseNativeMouseEventWithAPZ({
    target,
    offsetX: mouseX,
    offsetY: mouseY,
    type: "mousemove",
  });
  // mouse down
  await promiseNativeMouseEventWithAPZ({
    target,
    offsetX: mouseX,
    offsetY: mouseY,
    type: "mousedown",
  });
  // drag vertically by |increment| until we reach the specified distance
  for (var s = 1; s <= steps; s++) {
    let dx = distanceX * (s / steps);
    let dy = distanceY * (s / steps);
    dump(`Dragging to ${mouseX + dx}, ${mouseY + dy} from target\n`);
    await promiseNativeMouseEventWithAPZ({
      target,
      offsetX: mouseX + dx,
      offsetY: mouseY + dy,
      type: "mousemove",
    });
  }

  // and return a function-wrapped promise to call afterwards to finish the drag
  return function () {
    return promiseNativeMouseEventWithAPZ({
      target,
      offsetX: mouseX + distanceX,
      offsetY: mouseY + distanceY,
      type: "mouseup",
    });
  };
}

// Synthesizes a native touch sequence of events corresponding to a pinch-zoom-in
// at the given focus point. The focus point must be specified in CSS coordinates
// relative to the document body.
async function pinchZoomInTouchSequence(focusX, focusY) {
  // prettier-ignore
  var zoom_in = [
      [ { x: focusX - 25, y: focusY - 50 }, { x: focusX + 25, y: focusY + 50 } ],
      [ { x: focusX - 30, y: focusY - 80 }, { x: focusX + 30, y: focusY + 80 } ],
      [ { x: focusX - 35, y: focusY - 110 }, { x: focusX + 40, y: focusY + 110 } ],
      [ { x: focusX - 40, y: focusY - 140 }, { x: focusX + 45, y: focusY + 140 } ],
      [ { x: focusX - 45, y: focusY - 170 }, { x: focusX + 50, y: focusY + 170 } ],
      [ { x: focusX - 50, y: focusY - 200 }, { x: focusX + 55, y: focusY + 200 } ],
  ];

  var touchIds = [0, 1];
  return synthesizeNativeTouchSequences(document.body, zoom_in, null, touchIds);
}

// Returns a promise that is resolved when the observer service dispatches a
// message with the given topic.
function promiseTopic(aTopic) {
  return new Promise((resolve, reject) => {
    SpecialPowers.Services.obs.addObserver(function observer(
      subject,
      topic,
      data
    ) {
      try {
        SpecialPowers.Services.obs.removeObserver(observer, topic);
        resolve([subject, data]);
      } catch (ex) {
        SpecialPowers.Services.obs.removeObserver(observer, topic);
        reject(ex);
      }
    }, aTopic);
  });
}

// Returns a promise that is resolved when a APZ transform ends.
function promiseTransformEnd() {
  return promiseTopic("APZ:TransformEnd");
}

// Returns a promise that resolves after the indicated number
// of touchend events have fired on the given target element.
function promiseTouchEnd(element, count = 1) {
  return new Promise(resolve => {
    var eventCount = 0;
    var counterFunction = function () {
      eventCount++;
      if (eventCount == count) {
        element.removeEventListener("touchend", counterFunction, {
          passive: true,
        });
        resolve();
      }
    };
    element.addEventListener("touchend", counterFunction, { passive: true });
  });
}

// This generates a touch-based pinch zoom-in gesture that is expected
// to succeed. It returns after APZ has completed the zoom and reaches the end
// of the transform. The focus point is expected to be in CSS coordinates
// relative to the document body.
async function pinchZoomInWithTouch(focusX, focusY) {
  // Register the listener for the TransformEnd observer topic
  let transformEndPromise = promiseTopic("APZ:TransformEnd");

  // Dispatch all the touch events
  await pinchZoomInTouchSequence(focusX, focusY);

  // Wait for TransformEnd to fire.
  await transformEndPromise;
}
// This generates a touchpad pinch zoom-in gesture that is expected
// to succeed. It returns after APZ has completed the zoom and reaches the end
// of the transform. The focus point is expected to be in CSS coordinates
// relative to the document body.
async function pinchZoomInWithTouchpad(focusX, focusY, options = {}) {
  var zoomIn = [
    1.0, 1.019531, 1.035156, 1.037156, 1.039156, 1.054688, 1.056688, 1.070312,
    1.072312, 1.089844, 1.091844, 1.109375, 1.128906, 1.144531, 1.160156,
    1.175781, 1.191406, 1.207031, 1.222656, 1.234375, 1.246094, 1.261719,
    1.273438, 1.285156, 1.296875, 1.3125, 1.328125, 1.347656, 1.363281,
    1.382812, 1.402344, 1.421875, 1.0,
  ];
  await synthesizeTouchpadPinch(zoomIn, focusX, focusY, options);
}

async function pinchZoomInAndPanWithTouchpad(options = {}) {
  var x = 584;
  var y = 347;
  var scalesAndFoci = [];
  // Zoom
  for (var scale = 1.0; scale <= 2.0; scale += 0.2) {
    scalesAndFoci.push([scale, x, y]);
  }
  // Pan (due to a limitation of the current implementation, events
  // for which the scale doesn't change are dropped, so vary the
  // scale slightly as well).
  for (var i = 1; i <= 20; i++) {
    x -= 4;
    y -= 5;
    scalesAndFoci.push([scale + 0.01 * i, x, y]);
  }
  await synthesizeTouchpadGesture(scalesAndFoci, options);
}

async function pinchZoomOutWithTouchpad(focusX, focusY, options = {}) {
  // The last item equal one to indicate scale end
  var zoomOut = [
    1.0, 1.375, 1.359375, 1.339844, 1.316406, 1.296875, 1.277344, 1.257812,
    1.238281, 1.21875, 1.199219, 1.175781, 1.15625, 1.132812, 1.101562,
    1.078125, 1.054688, 1.03125, 1.011719, 0.992188, 0.972656, 0.953125,
    0.933594, 1.0,
  ];
  await synthesizeTouchpadPinch(zoomOut, focusX, focusY, options);
}

async function pinchZoomInOutWithTouchpad(focusX, focusY, options = {}) {
  // Use the same scale for two events in a row to make sure the code handles this properly.
  var zoomInOut = [
    1.0, 1.082031, 1.089844, 1.097656, 1.101562, 1.109375, 1.121094, 1.128906,
    1.128906, 1.125, 1.097656, 1.074219, 1.054688, 1.035156, 1.015625, 1.0, 1.0,
  ];
  await synthesizeTouchpadPinch(zoomInOut, focusX, focusY, options);
}
// This generates a touch-based pinch gesture that is expected to succeed
// and trigger an APZ:TransformEnd observer notification.
// It returns after that notification has been dispatched.
// The coordinates of touch events in `touchSequence` are expected to be
// in CSS coordinates relative to the document body.
async function synthesizeNativeTouchAndWaitForTransformEnd(
  touchSequence,
  touchIds
) {
  // Register the listener for the TransformEnd observer topic
  let transformEndPromise = promiseTopic("APZ:TransformEnd");

  // Dispatch all the touch events
  await synthesizeNativeTouchSequences(
    document.body,
    touchSequence,
    null,
    touchIds
  );

  // Wait for TransformEnd to fire.
  await transformEndPromise;
}

// Returns a touch sequence for a pinch-zoom-out operation in the center
// of the visual viewport. The touch sequence returned is in CSS coordinates
// relative to the document body.
function pinchZoomOutTouchSequenceAtCenter() {
  // Divide the half of visual viewport size by 8, then cause touch events
  // starting from the 7th furthest away from the center towards the center.
  const deltaX = window.visualViewport.width / 16;
  const deltaY = window.visualViewport.height / 16;
  const centerX =
    window.visualViewport.pageLeft + window.visualViewport.width / 2;
  const centerY =
    window.visualViewport.pageTop + window.visualViewport.height / 2;
  // prettier-ignore
  var zoom_out = [
      [ { x: centerX - (deltaX * 6), y: centerY - (deltaY * 6) },
        { x: centerX + (deltaX * 6), y: centerY + (deltaY * 6) } ],
      [ { x: centerX - (deltaX * 5), y: centerY - (deltaY * 5) },
        { x: centerX + (deltaX * 5), y: centerY + (deltaY * 5) } ],
      [ { x: centerX - (deltaX * 4), y: centerY - (deltaY * 4) },
        { x: centerX + (deltaX * 4), y: centerY + (deltaY * 4) } ],
      [ { x: centerX - (deltaX * 3), y: centerY - (deltaY * 3) },
        { x: centerX + (deltaX * 3), y: centerY + (deltaY * 3) } ],
      [ { x: centerX - (deltaX * 2), y: centerY - (deltaY * 2) },
        { x: centerX + (deltaX * 2), y: centerY + (deltaY * 2) } ],
      [ { x: centerX - (deltaX * 1), y: centerY - (deltaY * 1) },
        { x: centerX + (deltaX * 1), y: centerY + (deltaY * 1) } ],
  ];
  return zoom_out;
}

// This generates a touch-based pinch zoom-out gesture that is expected
// to succeed. It returns after APZ has completed the zoom and reaches the end
// of the transform. The touch inputs are directed to the center of the
// current visual viewport.
async function pinchZoomOutWithTouchAtCenter() {
  var zoom_out = pinchZoomOutTouchSequenceAtCenter();
  var touchIds = [0, 1];
  await synthesizeNativeTouchAndWaitForTransformEnd(zoom_out, touchIds);
}

// useTouchpad is only currently implemented on macOS
async function synthesizeDoubleTap(element, x, y, useTouchpad) {
  if (useTouchpad) {
    await synthesizeNativeTouchpadDoubleTap(element, x, y);
  } else {
    await synthesizeNativeTap(element, x, y);
    await synthesizeNativeTap(element, x, y);
  }
}
// useTouchpad is only currently implemented on macOS
async function doubleTapOn(element, x, y, useTouchpad) {
  let transformEndPromise = promiseTransformEnd();

  await synthesizeDoubleTap(element, x, y, useTouchpad);

  // Wait for the APZ:TransformEnd to fire
  await transformEndPromise;

  // Flush state so we can query an accurate resolution
  await promiseApzFlushedRepaints();
}

const NativePanHandlerForLinux = {
  beginPhase: SpecialPowers.DOMWindowUtils.PHASE_BEGIN,
  updatePhase: SpecialPowers.DOMWindowUtils.PHASE_UPDATE,
  endPhase: SpecialPowers.DOMWindowUtils.PHASE_END,
  promiseNativePanEvent: promiseNativeTouchpadPanEventAndWaitForObserver,
  delta: -50,
};

const NativePanHandlerForWindows = {
  beginPhase: SpecialPowers.DOMWindowUtils.PHASE_BEGIN,
  updatePhase: SpecialPowers.DOMWindowUtils.PHASE_UPDATE,
  endPhase: SpecialPowers.DOMWindowUtils.PHASE_END,
  promiseNativePanEvent: promiseNativeTouchpadPanEventAndWaitForObserver,
  delta: 50,
};

const NativePanHandlerForMac = {
  // From https://developer.apple.com/documentation/coregraphics/cgscrollphase/kcgscrollphasebegan?language=occ , etc.
  beginPhase: 1, // kCGScrollPhaseBegan
  updatePhase: 2, // kCGScrollPhaseChanged
  endPhase: 4, // kCGScrollPhaseEnded
  promiseNativePanEvent: promiseNativePanGestureEventAndWaitForObserver,
  delta: -50,
};

const NativePanHandlerForHeadless = {
  beginPhase: SpecialPowers.DOMWindowUtils.PHASE_BEGIN,
  updatePhase: SpecialPowers.DOMWindowUtils.PHASE_UPDATE,
  endPhase: SpecialPowers.DOMWindowUtils.PHASE_END,
  promiseNativePanEvent: promiseNativeTouchpadPanEventAndWaitForObserver,
  delta: 50,
};

function getPanHandler() {
  if (SpecialPowers.isHeadless) {
    return NativePanHandlerForHeadless;
  }

  switch (getPlatform()) {
    case "linux":
      return NativePanHandlerForLinux;
    case "windows":
      return NativePanHandlerForWindows;
    case "mac":
      return NativePanHandlerForMac;
    default:
      throw new Error(
        "There's no native pan handler on platform " + getPlatform()
      );
  }
}

// Lazily get `NativePanHandler` to avoid an exception where we don't support
// native pan events (e.g. Android).
if (!window.hasOwnProperty("NativePanHandler")) {
  Object.defineProperty(window, "NativePanHandler", {
    get() {
      return getPanHandler();
    },
  });
}

async function panRightToLeftBegin(aElement, aX, aY, aMultiplier) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    NativePanHandler.delta * aMultiplier,
    0,
    NativePanHandler.beginPhase
  );
}

async function panRightToLeftUpdate(aElement, aX, aY, aMultiplier) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    NativePanHandler.delta * aMultiplier,
    0,
    NativePanHandler.updatePhase
  );
}

async function panRightToLeftEnd(aElement, aX, aY) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    0,
    0,
    NativePanHandler.endPhase
  );
}

async function panRightToLeft(aElement, aX, aY, aMultiplier) {
  await panRightToLeftBegin(aElement, aX, aY, aMultiplier);
  await panRightToLeftUpdate(aElement, aX, aY, aMultiplier);
  await panRightToLeftEnd(aElement, aX, aY, aMultiplier);
}

async function panLeftToRight(aElement, aX, aY, aMultiplier) {
  await panLeftToRightBegin(aElement, aX, aY, aMultiplier);
  await panLeftToRightUpdate(aElement, aX, aY, aMultiplier);
  await panLeftToRightEnd(aElement, aX, aY, aMultiplier);
}

async function panLeftToRightBegin(aElement, aX, aY, aMultiplier) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    -NativePanHandler.delta * aMultiplier,
    0,
    NativePanHandler.beginPhase
  );
}

async function panLeftToRightUpdate(aElement, aX, aY, aMultiplier) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    -NativePanHandler.delta * aMultiplier,
    0,
    NativePanHandler.updatePhase
  );
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    -NativePanHandler.delta * aMultiplier,
    0,
    NativePanHandler.updatePhase
  );
}

async function panLeftToRightEnd(aElement, aX, aY) {
  await NativePanHandler.promiseNativePanEvent(
    aElement,
    aX,
    aY,
    0,
    0,
    NativePanHandler.endPhase
  );
}

// Close the context menu on desktop platforms.
// NOTE: This function doesn't work if the context menu isn't open.
async function closeContextMenu() {
  if (getPlatform() == "android") {
    return;
  }

  const contextmenuClosedPromise = SpecialPowers.spawnChrome([], async () => {
    const menu = this.browsingContext.topChromeWindow.document.getElementById(
      "contentAreaContextMenu"
    );
    ok(
      menu.state == "open" || menu.state == "showing",
      "This function is supposed to work only if the context menu is open or showing"
    );

    return new Promise(resolve => {
      menu.addEventListener(
        "popuphidden",
        () => {
          resolve();
        },
        { once: true }
      );
      menu.hidePopup();
    });
  });

  await contextmenuClosedPromise;
}

// Get a list of prefs which should be used for a subtest which wants to
// generate a smooth scroll animation using an input event. The smooth
// scroll animation is slowed down so the test can perform other actions
// while it's still in progress.
function getSmoothScrollPrefs(aInputType, aMsdPhysics) {
  let result = [];
  // Some callers just want the default and don't pass in aMsdPhysics.
  if (aMsdPhysics !== undefined) {
    result.push(["general.smoothScroll.msdPhysics.enabled", aMsdPhysics]);
  } else {
    aMsdPhysics = SpecialPowers.getBoolPref(
      "general.smoothScroll.msdPhysics.enabled"
    );
  }
  if (aInputType == "wheel") {
    // We want to test real wheel events rather than pan events.
    result.push(["apz.test.mac.synth_wheel_input", true]);
  } /* keyboard input */ else {
    // The default verticalScrollDistance (which is 3) is too small for native
    // keyboard scrolling, it sometimes produces same scroll offsets in the early
    // stages of the smooth animation.
    result.push(["toolkit.scrollbox.verticalScrollDistance", 5]);
  }
  // Use a longer animation duration to avoid the situation that the
  // animation stops accidentally in between each arrow input event.
  // If the situation happens, scroll offsets will not change at the moment.
  if (aMsdPhysics) {
    // Prefs for MSD physics (applicable to any input type).
    result.push(
      ...[
        ["general.smoothScroll.msdPhysics.motionBeginSpringConstant", 20],
        ["general.smoothScroll.msdPhysics.regularSpringConstant", 20],
        ["general.smoothScroll.msdPhysics.slowdownMinDeltaRatio", 0.1],
        ["general.smoothScroll.msdPhysics.slowdownSpringConstant", 20],
      ]
    );
  } else if (aInputType == "wheel") {
    // Prefs for Bezier physics with wheel input.
    result.push(
      ...[
        ["general.smoothScroll.mouseWheel.durationMaxMS", 1500],
        ["general.smoothScroll.mouseWheel.durationMinMS", 1500],
      ]
    );
  } else {
    // Prefs for Bezier physics with keyboard input.
    result.push(
      ...[
        ["general.smoothScroll.lines.durationMaxMS", 1500],
        ["general.smoothScroll.lines.durationMinMS", 1500],
      ]
    );
  }
  return result;
}

function buildRelativeScrollSmoothnessVariants(aInputType, aScrollMethods) {
  let subtests = [];
  for (let scrollMethod of aScrollMethods) {
    subtests.push({
      file: `helper_relative_scroll_smoothness.html?input-type=${aInputType}&scroll-method=${scrollMethod}&strict=true`,
      prefs: [
        ["apz.test.logging_enabled", true],
        ...getSmoothScrollPrefs(aInputType, /* Bezier physics */ false),
      ],
    });
    // For MSD physics, run the test with strict=false. The shape of the
    // animation curve is highly timing dependent, and we can't guarantee
    // that an animation will run long enough until the next input event
    // arrives.
    subtests.push({
      file: `helper_relative_scroll_smoothness.html?input-type=${aInputType}&scroll-method=${scrollMethod}&strict=false`,
      prefs: [
        ["apz.test.logging_enabled", true],
        ...getSmoothScrollPrefs(aInputType, /* MSD physics */ true),
      ],
    });
  }
  return subtests;
}

// Right now this is only meaningful on Linux.
async function getWindowProtocol() {
  if (getPlatform() != "linux") {
    return "";
  }

  return await SpecialPowers.spawnChrome([], () => {
    try {
      return Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo)
        .windowProtocol;
    } catch {
      return "";
    }
  });
}

// A utility function to synthesize a touch event in a way that a sequence of
// such events triggeres a fast fling animation.
async function synthesizeNativeTouchForFastFling(aTarget, aX, aY, aType) {
  // We use `synthesizeNativeTouch` directly here since with
  // `synthesizeNativeTouchSequences` we can't do fast fling because
  // every time stamp difference of each touch event becomes smaller than
  // `MIN_VELOCITY_SAMPLE_TIME` (50ms) on desktops.
  await synthesizeNativeTouch(aTarget, aX, aY, aType);
  // On Android if subsequent touch event don't happen within 40ms, any fast
  // fling animation is never triggered.
  // [1] https://searchfox.org/firefox-main/rev/6301b01791f9c98637ae8d5b7a81dd004d48d468/gfx/layers/apz/src/AndroidVelocityTracker.cpp#44-46
  // But Android touch event timestamps have millisecond resolution and the
  // velocity tracker merges samples less than 1ms apart, so the events still
  // need to be spaced out a bit to produce distinct samples.
  const delayMs = getPlatform() == "android" ? 5 : 50;
  await new Promise(resolve => setTimeout(resolve, delayMs));
}
