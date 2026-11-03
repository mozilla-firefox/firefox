/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const XHTML_NS = "http://www.w3.org/1999/xhtml";

const DEBUG_CONTRACTID = "@mozilla.org/xpcom/debug;1";
const PRINTSETTINGS_CONTRACTID = "@mozilla.org/gfx/printsettings-service;1";
const NS_OBSERVER_SERVICE_CONTRACTID = "@mozilla.org/observer-service;1";
const NS_GFXINFO_CONTRACTID = "@mozilla.org/gfx/info;1";

// "<!--CLEAR-->"
const BLANK_URL_FOR_CLEARING =
  "data:text/html;charset=UTF-8,%3C%21%2D%2DCLEAR%2D%2D%3E";

const { setTimeout, clearTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const { onSpellCheck } = ChromeUtils.importESModule(
  "resource://reftest/AsyncSpellCheckTestHelper.sys.mjs"
);

// This will load chrome Custom Elements inside chrome documents:
ChromeUtils.importESModule(
  "resource://gre/modules/CustomElementsListener.sys.mjs"
);

var gBrowserIsRemote;
var gHaveCanvasSnapshot = false;
var gCurrentURL;
var gCurrentURLRecordResults;
var gCurrentURLTargetType;
var gCurrentTestType;
var gTimeoutHook = null;
var gFailureTimeout = null;
var gFailureReason;
var gAssertionCount = 0;
var gUpdateCanvasPromiseResolver = null;

var gDebug;
var gVerbose = false;

var gCurrentTestStartTime;
var gClearingForAssertionCheck = false;

const TYPE_LOAD = "load"; // test without a reference (just test that it does
// not assert, crash, hang, or leak)
const TYPE_SCRIPT = "script"; // test contains individual test results
const TYPE_PRINT = "print"; // test and reference will be printed to PDF's and
// compared structurally

// keep this in sync with globals.sys.mjs
const URL_TARGET_TYPE_TEST = 0; // first url
const URL_TARGET_TYPE_REFERENCE = 1; // second url, if any

function webNavigation() {
  return docShell.QueryInterface(Ci.nsIWebNavigation);
}

function webProgress() {
  return docShell
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIWebProgress);
}

function windowUtilsForWindow(w) {
  return w.windowUtils;
}

function windowUtils() {
  return windowUtilsForWindow(content);
}

function IDForEventTarget(event) {
  try {
    return "'" + event.target.getAttribute("id") + "'";
  } catch (ex) {
    return "<unknown>";
  }
}

var progressListener = {
  onStateChange(webprogress, request, flags) {
    let uri;
    try {
      request.QueryInterface(Ci.nsIChannel);
      uri = request.originalURI.spec;
    } catch (ex) {
      return;
    }
    const WPL = Ci.nsIWebProgressListener;
    const endFlags =
      WPL.STATE_STOP | WPL.STATE_IS_WINDOW | WPL.STATE_IS_NETWORK;
    if ((flags & endFlags) == endFlags) {
      OnDocumentLoad(uri);
    }
  },
  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]),
};

function OnInitialLoad() {
  removeEventListener("load", OnInitialLoad, true);

  gDebug = Cc[DEBUG_CONTRACTID].getService(Ci.nsIDebug2);
  if (gDebug.isDebugBuild) {
    gAssertionCount = gDebug.assertionCount;
  }
  gVerbose = !!Services.env.get("MOZ_REFTEST_VERBOSE");

  RegisterMessageListeners();

  var initInfo = SendContentReady();
  gBrowserIsRemote = initInfo.remote;

  webProgress().addProgressListener(
    progressListener,
    Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
  );

  LogInfo("Using browser remote=" + gBrowserIsRemote + "\n");
}

function SetFailureTimeout(cb, timeout, uri) {
  var targetTime = Date.now() + timeout;

  var wrapper = function () {
    // Timeouts can fire prematurely in some cases (e.g. in chaos mode). If this
    // happens, set another timeout for the remaining time.
    let remainingMs = targetTime - Date.now();
    if (remainingMs > 0) {
      SetFailureTimeout(cb, remainingMs);
    } else {
      cb();
    }
  };

  // Once OnDocumentLoad is called to handle the 'load' event it will update
  // this error message to reflect what stage of the processing it has reached
  // as it advances to each stage in turn.
  gFailureReason =
    "timed out after " + timeout + " ms waiting for 'load' event for " + uri;
  gFailureTimeout = setTimeout(wrapper, timeout);
}

function StartTestURI(type, uri, uriTargetType, timeout) {
  // The GC is only able to clean up compartments after the CC runs. Since
  // the JS ref tests disable the normal browser chrome and do not otherwise
  // create substatial DOM garbage, the CC tends not to run enough normally.
  windowUtils().runNextCollectorTimer();

  gCurrentTestType = type;
  gCurrentURL = uri;
  gCurrentURLTargetType = uriTargetType;
  gCurrentURLRecordResults = 0;

  gCurrentTestStartTime = Date.now();
  if (gFailureTimeout != null) {
    SendException("program error managing timeouts\n");
  }
  SetFailureTimeout(LoadFailed, timeout, uri);

  LoadURI(gCurrentURL);
}

function setupTextZoom(contentRootElement) {
  if (
    !contentRootElement ||
    !contentRootElement.hasAttribute("reftest-text-zoom")
  ) {
    return;
  }
  docShell.browsingContext.textZoom =
    contentRootElement.getAttribute("reftest-text-zoom");
}

function setupFullZoom(contentRootElement) {
  if (!contentRootElement || !contentRootElement.hasAttribute("reftest-zoom")) {
    return;
  }
  docShell.browsingContext.fullZoom =
    contentRootElement.getAttribute("reftest-zoom");
}

function resetZoomAndTextZoom() {
  docShell.browsingContext.fullZoom = 1.0;
  docShell.browsingContext.textZoom = 1.0;
}

function doPrintMode(contentRootElement) {
  // use getAttribute because className works differently in HTML and SVG
  if (contentRootElement && contentRootElement.hasAttribute("class")) {
    var classList = contentRootElement.getAttribute("class").split(/\s+/);
    if (classList.includes("reftest-print")) {
      SendException("reftest-print is obsolete, use reftest-paged instead");
      return false;
    }
    return classList.includes("reftest-paged");
  }
  return false;
}

function setupPrintMode(contentRootElement) {
  var PSSVC = Cc[PRINTSETTINGS_CONTRACTID].getService(
    Ci.nsIPrintSettingsService
  );
  var ps = PSSVC.createNewPrintSettings();
  ps.paperWidth = 5;
  ps.paperHeight = 3;

  // Override any os-specific unwriteable margins
  ps.unwriteableMarginTop = 0;
  ps.unwriteableMarginLeft = 0;
  ps.unwriteableMarginBottom = 0;
  ps.unwriteableMarginRight = 0;

  ps.headerStrLeft = "";
  ps.headerStrCenter = "";
  ps.headerStrRight = "";
  ps.footerStrLeft = "";
  ps.footerStrCenter = "";
  ps.footerStrRight = "";

  const printBackgrounds = (() => {
    const attr = contentRootElement.getAttribute("reftest-paged-backgrounds");
    return !attr || attr != "false";
  })();
  ps.printBGColors = printBackgrounds;
  ps.printBGImages = printBackgrounds;

  docShell.docViewer.setPageModeForTesting(/* aPageMode */ true, ps);
}

// Message the parent process to ask it to print the current page to a PDF file.
function printToPdf() {
  let currentDoc = content.document;
  let isPrintSelection = false;
  let printRange = "";

  if (currentDoc) {
    let contentRootElement = currentDoc.documentElement;
    printRange = contentRootElement.getAttribute("reftest-print-range") || "";
  }

  if (printRange) {
    if (printRange === "selection") {
      isPrintSelection = true;
    } else if (
      !printRange.split(",").every(range => /^[1-9]\d*-[1-9]\d*$/.test(range))
    ) {
      SendException("invalid value for reftest-print-range");
      return;
    }
  }

  SendStartPrint(isPrintSelection, printRange);
}

function attrOrDefault(element, attr, def) {
  return element.hasAttribute(attr) ? Number(element.getAttribute(attr)) : def;
}

function setupViewport(contentRootElement) {
  if (!contentRootElement) {
    return;
  }

  var sw = attrOrDefault(contentRootElement, "reftest-scrollport-w", 0);
  var sh = attrOrDefault(contentRootElement, "reftest-scrollport-h", 0);
  if (sw !== 0 || sh !== 0) {
    LogInfo("Setting viewport to <w=" + sw + ", h=" + sh + ">");
    windowUtils().setVisualViewportSize(sw, sh);
  }

  var res = attrOrDefault(contentRootElement, "reftest-resolution", 1);
  if (res !== 1) {
    LogInfo("Setting resolution to " + res);
    windowUtils().setResolutionAndScaleTo(res);
  }

  // XXX support viewconfig when needed
}

function setupDisplayport() {
  let promise = content.windowGlobalChild
    .getActor("ReftestFission")
    .SetupDisplayportRoot();
  return promise.then(
    function (result) {
      for (let errorString of result.errorStrings) {
        LogError(errorString);
      }
      for (let infoString of result.infoStrings) {
        LogInfo(infoString);
      }
    },
    function (reason) {
      LogError("SetupDisplayportRoot returned promise rejected: " + reason);
    }
  );
}

// Returns whether any offsets were updated
function setupAsyncScrollOffsets(options) {
  let currentDoc = content.document;
  let contentRootElement = currentDoc ? currentDoc.documentElement : null;

  if (
    !contentRootElement ||
    !contentRootElement.hasAttribute("reftest-async-scroll")
  ) {
    return Promise.resolve(false);
  }

  let allowFailure = options.allowFailure;
  let promise = content.windowGlobalChild
    .getActor("ReftestFission")
    .sendQuery("SetupAsyncScrollOffsets", { allowFailure });
  return promise.then(
    function (result) {
      for (let errorString of result.errorStrings) {
        LogError(errorString);
      }
      for (let infoString of result.infoStrings) {
        LogInfo(infoString);
      }
      return result.updatedAny;
    },
    function (reason) {
      LogError(
        "SetupAsyncScrollOffsets SendQuery to parent promise rejected: " +
          reason
      );
      return false;
    }
  );
}

function setupAsyncZoom(options) {
  var currentDoc = content.document;
  var contentRootElement = currentDoc ? currentDoc.documentElement : null;

  if (
    !contentRootElement ||
    !contentRootElement.hasAttribute("reftest-async-zoom")
  ) {
    return false;
  }

  var zoom = attrOrDefault(contentRootElement, "reftest-async-zoom", 1);
  if (zoom != 1) {
    try {
      windowUtils().setAsyncZoom(contentRootElement, zoom);
      return true;
    } catch (e) {
      if (!options.allowFailure) {
        throw e;
      }
    }
  }
  return false;
}

function resetDisplayportAndViewport() {
  // XXX currently the displayport configuration lives on the
  // presshell and so is "reset" on nav when we get a new presshell.
}

function shouldWaitForPendingPaints() {
  // if gHaveCanvasSnapshot is false, we're not taking snapshots so
  // there is no need to wait for pending paints to be flushed.
  return gHaveCanvasSnapshot && windowUtils().isMozAfterPaintPending;
}

function shouldWaitForReftestWaitRemoval(contentRootElement) {
  // use getAttribute because className works differently in HTML and SVG
  return (
    contentRootElement &&
    contentRootElement.hasAttribute("class") &&
    contentRootElement
      .getAttribute("class")
      .split(/\s+/)
      .includes("reftest-wait")
  );
}

function shouldSnapshotWholePage(contentRootElement) {
  // use getAttribute because className works differently in HTML and SVG
  return (
    contentRootElement &&
    contentRootElement.hasAttribute("class") &&
    contentRootElement
      .getAttribute("class")
      .split(/\s+/)
      .includes("reftest-snapshot-all")
  );
}

function shouldNotFlush(contentRootElement) {
  // use getAttribute because className works differently in HTML and SVG
  return (
    contentRootElement &&
    contentRootElement.hasAttribute("class") &&
    contentRootElement
      .getAttribute("class")
      .split(/\s+/)
      .includes("reftest-no-flush")
  );
}

function shouldCheckNoWRRaster(contentRootElement) {
  // use getAttribute because className works differently in HTML and SVG
  return (
    contentRootElement &&
    contentRootElement.hasAttribute("class") &&
    contentRootElement
      .getAttribute("class")
      .split(/\s+/)
      .includes("reftest-no-wr-raster")
  );
}

function getNoPaintElements(contentRootElement) {
  return contentRootElement.getElementsByClassName("reftest-no-paint");
}
function getNoDisplayListElements(contentRootElement) {
  return contentRootElement.getElementsByClassName("reftest-no-display-list");
}
function getDisplayListElements(contentRootElement) {
  return contentRootElement.getElementsByClassName("reftest-display-list");
}

function getOpaqueLayerElements(contentRootElement) {
  return contentRootElement.getElementsByClassName("reftest-opaque-layer");
}

function getAssignedLayerMap(contentRootElement) {
  var layerNameToElementsMap = {};
  var elements = contentRootElement.querySelectorAll(
    "[reftest-assigned-layer]"
  );
  for (var i = 0; i < elements.length; ++i) {
    var element = elements[i];
    var layerName = element.getAttribute("reftest-assigned-layer");
    if (!(layerName in layerNameToElementsMap)) {
      layerNameToElementsMap[layerName] = [];
    }
    layerNameToElementsMap[layerName].push(element);
  }
  return layerNameToElementsMap;
}

const FlushMode = {
  ALL: 0,
  IGNORE_THROTTLED_ANIMATIONS: 1,
};

// Initial state. When the document has loaded and all MozAfterPaint events and
// all explicit paint waits are flushed, we can fire the MozReftestInvalidate
// event and move to the next state.
const STATE_WAITING_TO_FIRE_INVALIDATE_EVENT = 0;
// When reftest-wait has been removed from the root element, we can move to the
// next state.
const STATE_WAITING_FOR_REFTEST_WAIT_REMOVAL = 1;
// When spell checking is done on all spell-checked elements, we can move to the
// next state.
const STATE_WAITING_FOR_SPELL_CHECKS = 2;
// When any pending compositor-side repaint requests have been flushed, we can
// move to the next state.
const STATE_WAITING_FOR_APZ_FLUSH = 3;
// When all MozAfterPaint events and all explicit paint waits are flushed, we're
// done and can move to the COMPLETED state.
const STATE_WAITING_TO_FINISH = 4;
const STATE_COMPLETED = 5;

async function FlushRendering(aFlushMode) {
  let browsingContext = content.docShell.browsingContext;
  let ignoreThrottledAnimations =
    aFlushMode === FlushMode.IGNORE_THROTTLED_ANIMATIONS;
  // Ensure the refresh driver ticks at least once, this ensures some
  // preference changes take effect.
  let needsAnimationFrame = IsSnapshottableTestType();
  try {
    let result = await content.windowGlobalChild
      .getActor("ReftestFission")
      .sendQuery("FlushRendering", {
        browsingContext,
        ignoreThrottledAnimations,
        needsAnimationFrame,
      });
    for (let errorString of result.errorStrings) {
      LogError(errorString);
    }
    for (let warningString of result.warningStrings) {
      LogWarning(warningString);
    }
    for (let infoString of result.infoStrings) {
      LogInfo(infoString);
    }
  } catch (reason) {
    // We expect actors to go away causing sendQuery's to fail, so
    // just note it.
    LogInfo("FlushRendering sendQuery to parent rejected: " + reason);
  }
}

function WaitForTestEnd(
  contentRootElement,
  inPrintMode,
  spellCheckedElements,
  forURL
) {
  // WaitForTestEnd works via the MakeProgress function below. It is responsible for
  // moving through the states listed above and calling FlushRendering. We also listen
  // for a number of events, the most important of which is the AfterPaintListener,
  // which is responsible for updating the canvas after paints. In a fission world
  // FlushRendering and updating the canvas must necessarily be async operations.
  // During these async operations we want to wait for them to finish and we don't
  // want to try to do anything else (what would we even want to do while only some of
  // the processes involved have flushed layout or updated their layer trees?). So
  // we call OperationInProgress whenever we are about to go back to the event loop
  // during one of these calls, and OperationCompleted when it finishes. This prevents
  // anything else from running while we wait and getting us into a confused state. We
  // then record anything that happens while we are waiting to make sure that the
  // right actions are triggered. The possible actions are basically calling
  // MakeProgress from a setTimeout, and updating the canvas for an after paint event.
  // The after paint listener just stashes the rects and we update them after a
  // completed MakeProgress call. This is handled by
  // HandlePendingTasksAfterMakeProgress, which also waits for any pending after paint
  // events. The general sequence of events is:
  //   - MakeProgress
  //   - HandlePendingTasksAfterMakeProgress
  //     - wait for after paint event if one is pending
  //     - update canvas for after paint events we have received
  //   - MakeProgress
  //   etc

  function CheckForLivenessOfContentRootElement() {
    if (contentRootElement && Cu.isDeadWrapper(contentRootElement)) {
      contentRootElement = null;
    }
  }

  var setTimeoutCallMakeProgressWhenComplete = false;

  var operationInProgress = false;
  function OperationInProgress() {
    if (operationInProgress) {
      LogWarning("Nesting atomic operations?");
    }
    operationInProgress = true;
  }
  function OperationCompleted() {
    if (!operationInProgress) {
      LogWarning("Mismatched OperationInProgress/OperationCompleted calls?");
    }
    operationInProgress = false;
    if (setTimeoutCallMakeProgressWhenComplete) {
      setTimeoutCallMakeProgressWhenComplete = false;
      setTimeout(CallMakeProgress, 0);
    }
  }
  function AssertNoOperationInProgress() {
    if (operationInProgress) {
      LogWarning("AssertNoOperationInProgress but operationInProgress");
    }
  }

  var updateCanvasPending = false;
  var updateCanvasRects = [];

  var currentDoc = content.document;
  var state = STATE_WAITING_TO_FIRE_INVALIDATE_EVENT;

  var setTimeoutMakeProgressPending = false;

  function CallSetTimeoutMakeProgress() {
    if (setTimeoutMakeProgressPending) {
      return;
    }
    setTimeoutMakeProgressPending = true;
    setTimeout(CallMakeProgress, 0);
  }

  // This should only ever be called from a timeout.
  function CallMakeProgress() {
    if (operationInProgress) {
      setTimeoutCallMakeProgressWhenComplete = true;
      return;
    }
    setTimeoutMakeProgressPending = false;
    MakeProgress();
  }

  var waitingForAnAfterPaint = false;

  // Updates the canvas if there are pending updates for it. Checks if we
  // need to call MakeProgress.
  function HandlePendingTasksAfterMakeProgress() {
    AssertNoOperationInProgress();

    if (
      (state == STATE_WAITING_TO_FIRE_INVALIDATE_EVENT ||
        state == STATE_WAITING_TO_FINISH) &&
      shouldWaitForPendingPaints()
    ) {
      LogInfo(
        "HandlePendingTasksAfterMakeProgress waiting for a MozAfterPaint"
      );
      // We are in a state where we wait for MozAfterPaint to clear and a
      // MozAfterPaint event is pending, give it a chance to fire, but don't
      // let anything else run.
      waitingForAnAfterPaint = true;
      OperationInProgress();
      return;
    }

    if (updateCanvasPending) {
      LogInfo("HandlePendingTasksAfterMakeProgress updating canvas");
      updateCanvasPending = false;
      let rects = updateCanvasRects;
      updateCanvasRects = [];
      OperationInProgress();
      CheckForLivenessOfContentRootElement();
      let promise = SendUpdateCanvasForEvent(forURL, rects, contentRootElement);
      promise.then(function () {
        OperationCompleted();
        // After paint events are fired immediately after a paint (one
        // of the things that can call us). Don't confuse ourselves by
        // firing synchronously if we triggered the paint ourselves.
        CallSetTimeoutMakeProgress();
      });
    }
  }

  // true if rectA contains rectB
  function Contains(rectA, rectB) {
    return (
      rectA.left <= rectB.left &&
      rectB.right <= rectA.right &&
      rectA.top <= rectB.top &&
      rectB.bottom <= rectA.bottom
    );
  }
  // true if some rect in rectList contains rect
  function ContainedIn(rectList, rect) {
    for (let i = 0; i < rectList.length; ++i) {
      if (Contains(rectList[i], rect)) {
        return true;
      }
    }
    return false;
  }

  function AfterPaintListener(event) {
    LogInfo("AfterPaintListener in " + event.target.document.location.href);
    if (event.target.document != currentDoc) {
      // ignore paint events for subframes or old documents in the window.
      // Invalidation in subframes will cause invalidation in the toplevel document anyway.
      return;
    }

    updateCanvasPending = true;
    for (let r of event.clientRects) {
      if (ContainedIn(updateCanvasRects, r)) {
        continue;
      }

      // Copy the rect; it's content and we are chrome, which means if the
      // document goes away (and it can in some crashtests) our reference
      // to it will be turned into a dead wrapper that we can't acccess.
      updateCanvasRects.push({
        left: r.left,
        top: r.top,
        right: r.right,
        bottom: r.bottom,
      });
    }

    if (waitingForAnAfterPaint) {
      waitingForAnAfterPaint = false;
      OperationCompleted();
    }

    if (!operationInProgress) {
      HandlePendingTasksAfterMakeProgress();
    }
    // Otherwise we know that eventually after the operation finishes we
    // will get a MakeProgress and/or HandlePendingTasksAfterMakeProgress
    // call, so we don't need to do anything.
  }

  function FromChildAfterPaintListener(event) {
    LogInfo(
      "FromChildAfterPaintListener from " + event.detail.originalTargetUri
    );

    updateCanvasPending = true;
    for (let r of event.detail.rects) {
      if (ContainedIn(updateCanvasRects, r)) {
        continue;
      }

      // Copy the rect; it's content and we are chrome, which means if the
      // document goes away (and it can in some crashtests) our reference
      // to it will be turned into a dead wrapper that we can't acccess.
      updateCanvasRects.push({
        left: r.left,
        top: r.top,
        right: r.right,
        bottom: r.bottom,
      });
    }

    if (!operationInProgress) {
      HandlePendingTasksAfterMakeProgress();
    }
    // Otherwise we know that eventually after the operation finishes we
    // will get a MakeProgress and/or HandlePendingTasksAfterMakeProgress
    // call, so we don't need to do anything.
  }

  let attrModifiedObserver;
  function AttrModifiedListener() {
    LogInfo("AttrModifiedListener fired");
    // Wait for the next return-to-event-loop before continuing --- for
    // example, the attribute may have been modified in an subdocument's
    // load event handler, in which case we need load event processing
    // to complete and unsuppress painting before we check isMozAfterPaintPending.
    CallSetTimeoutMakeProgress();
  }

  function RemoveListeners() {
    // OK, we can end the test now.
    removeEventListener("MozAfterPaint", AfterPaintListener, false);
    removeEventListener(
      "Reftest:MozAfterPaintFromChild",
      FromChildAfterPaintListener,
      false
    );
    CheckForLivenessOfContentRootElement();
    if (attrModifiedObserver) {
      if (!Cu.isDeadWrapper(attrModifiedObserver)) {
        attrModifiedObserver.disconnect();
      }
      attrModifiedObserver = null;
    }
    gTimeoutHook = null;
    // Make sure we're in the COMPLETED state just in case
    // (this may be called via the test-timeout hook)
    state = STATE_COMPLETED;
  }

  // Everything that could cause shouldWaitForXXX() to
  // change from returning true to returning false is monitored via some kind
  // of event listener which eventually calls this function.
  function MakeProgress() {
    if (state >= STATE_COMPLETED) {
      LogInfo("MakeProgress: STATE_COMPLETED");
      return;
    }

    LogInfo("MakeProgress");

    // We don't need to flush styles any more when we are in the state
    // after reftest-wait has removed.
    OperationInProgress();
    let promise = Promise.resolve(undefined);
    if (state != STATE_WAITING_TO_FINISH) {
      // If we are waiting for the MozReftestInvalidate event we don't want
      // to flush throttled animations. Flushing throttled animations can
      // continue to cause new MozAfterPaint events even when all the
      // rendering we're concerned about should have ceased. Since
      // MozReftestInvalidate won't be sent until we finish waiting for all
      // MozAfterPaint events, we should avoid flushing throttled animations
      // here or else we'll never leave this state.
      let flushMode =
        state === STATE_WAITING_TO_FIRE_INVALIDATE_EVENT
          ? FlushMode.IGNORE_THROTTLED_ANIMATIONS
          : FlushMode.ALL;
      promise = FlushRendering(flushMode);
    }
    promise.then(function () {
      OperationCompleted();
      MakeProgress2();
      // If there is an operation in progress then we know there will be
      // a MakeProgress call is will happen after it finishes.
      if (!operationInProgress) {
        HandlePendingTasksAfterMakeProgress();
      }
    });
  }

  // eslint-disable-next-line complexity
  function MakeProgress2() {
    switch (state) {
      case STATE_WAITING_TO_FIRE_INVALIDATE_EVENT: {
        LogInfo("MakeProgress: STATE_WAITING_TO_FIRE_INVALIDATE_EVENT");
        if (shouldWaitForPendingPaints() || updateCanvasPending) {
          gFailureReason =
            "timed out waiting for pending paint count to reach zero";
          if (shouldWaitForPendingPaints()) {
            gFailureReason += " (waiting for MozAfterPaint)";
            LogInfo("MakeProgress: waiting for MozAfterPaint");
          }
          if (updateCanvasPending) {
            gFailureReason += " (waiting for updateCanvasPending)";
            LogInfo("MakeProgress: waiting for updateCanvasPending");
          }
          return;
        }

        state = STATE_WAITING_FOR_REFTEST_WAIT_REMOVAL;
        CheckForLivenessOfContentRootElement();
        var hasReftestWait =
          shouldWaitForReftestWaitRemoval(contentRootElement);
        // Notify the test document that now is a good time to test some invalidation
        LogInfo("MakeProgress: dispatching MozReftestInvalidate");
        if (contentRootElement) {
          let elements = getNoPaintElements(contentRootElement);
          for (let i = 0; i < elements.length; ++i) {
            windowUtils().checkAndClearPaintedState(elements[i]);
          }
          elements = getNoDisplayListElements(contentRootElement);
          for (let i = 0; i < elements.length; ++i) {
            windowUtils().checkAndClearDisplayListState(elements[i]);
          }
          elements = getDisplayListElements(contentRootElement);
          for (let i = 0; i < elements.length; ++i) {
            windowUtils().checkAndClearDisplayListState(elements[i]);
          }
          // Clear WR rasterization state before MozReftestInvalidate
          if (shouldCheckNoWRRaster(contentRootElement)) {
            windowUtils().checkAndClearWRDidRasterize();
          }
          var notification = content.document.createEvent("Events");
          notification.initEvent("MozReftestInvalidate", true, false);
          contentRootElement.dispatchEvent(notification);
        } else {
          LogInfo(
            "MakeProgress: couldn't send MozReftestInvalidate event because content root element does not exist"
          );
        }

        CheckForLivenessOfContentRootElement();
        if (!inPrintMode && doPrintMode(contentRootElement)) {
          LogInfo("MakeProgress: setting up print mode");
          setupPrintMode(contentRootElement);
        }

        CheckForLivenessOfContentRootElement();
        if (
          hasReftestWait &&
          !shouldWaitForReftestWaitRemoval(contentRootElement)
        ) {
          // MozReftestInvalidate handler removed reftest-wait.
          // We expect something to have been invalidated...
          OperationInProgress();
          let promise = FlushRendering(FlushMode.ALL);
          promise.then(function () {
            OperationCompleted();
            if (!updateCanvasPending && !shouldWaitForPendingPaints()) {
              LogWarning("MozInvalidateEvent didn't invalidate");
            }
            MakeProgress();
          });
          return;
        }
        // Try next state
        MakeProgress();
        return;
      }

      case STATE_WAITING_FOR_REFTEST_WAIT_REMOVAL:
        LogInfo("MakeProgress: STATE_WAITING_FOR_REFTEST_WAIT_REMOVAL");
        CheckForLivenessOfContentRootElement();
        if (shouldWaitForReftestWaitRemoval(contentRootElement)) {
          gFailureReason = "timed out waiting for reftest-wait to be removed";
          LogInfo("MakeProgress: waiting for reftest-wait to be removed");
          return;
        }

        if (shouldNotFlush(contentRootElement)) {
          // If reftest-no-flush is specified, we need to set
          // updateCanvasPending explicitly to take the latest snapshot
          // since animation changes on the compositor thread don't invoke
          // any MozAfterPaint events at all.
          // NOTE: We don't add any rects to updateCanvasRects here since
          // SendUpdateCanvasForEvent() will handle this case properly
          // without any rects.
          updateCanvasPending = true;
        }
        // Try next state
        state = STATE_WAITING_FOR_SPELL_CHECKS;
        MakeProgress();
        return;

      case STATE_WAITING_FOR_SPELL_CHECKS:
        LogInfo("MakeProgress: STATE_WAITING_FOR_SPELL_CHECKS");
        if (numPendingSpellChecks) {
          gFailureReason = "timed out waiting for spell checks to end";
          LogInfo("MakeProgress: waiting for spell checks to end");
          return;
        }

        state = STATE_WAITING_FOR_APZ_FLUSH;
        LogInfo("MakeProgress: STATE_WAITING_FOR_APZ_FLUSH");
        gFailureReason = "timed out waiting for APZ flush to complete";

        var flushWaiter = function (aSubject, aTopic) {
          if (aTopic) {
            LogInfo("MakeProgress: apz-repaints-flushed fired");
          }
          Services.obs.removeObserver(flushWaiter, "apz-repaints-flushed");
          state = STATE_WAITING_TO_FINISH;
          if (operationInProgress) {
            CallSetTimeoutMakeProgress();
          } else {
            MakeProgress();
          }
        };
        Services.obs.addObserver(flushWaiter, "apz-repaints-flushed");

        var willSnapshot = IsSnapshottableTestType();
        CheckForLivenessOfContentRootElement();
        var noFlush = !shouldNotFlush(contentRootElement);
        if (noFlush && willSnapshot && windowUtils().flushApzRepaints()) {
          LogInfo("MakeProgress: done requesting APZ flush");
        } else {
          LogInfo("MakeProgress: APZ flush not required");
          flushWaiter(null, null, null);
        }
        return;

      case STATE_WAITING_FOR_APZ_FLUSH:
        LogInfo("MakeProgress: STATE_WAITING_FOR_APZ_FLUSH");
        // Nothing to do here; once we get the apz-repaints-flushed event
        // we will go to STATE_WAITING_TO_FINISH
        return;

      case STATE_WAITING_TO_FINISH:
        LogInfo("MakeProgress: STATE_WAITING_TO_FINISH");
        if (shouldWaitForPendingPaints() || updateCanvasPending) {
          gFailureReason =
            "timed out waiting for pending paint count to " +
            "reach zero (after reftest-wait removed and switch to print mode)";
          if (shouldWaitForPendingPaints()) {
            gFailureReason += " (waiting for MozAfterPaint)";
            LogInfo("MakeProgress: waiting for MozAfterPaint");
          }
          if (updateCanvasPending) {
            gFailureReason += " (waiting for updateCanvasPending)";
            LogInfo("MakeProgress: waiting for updateCanvasPending");
          }
          return;
        }
        CheckForLivenessOfContentRootElement();
        if (contentRootElement) {
          let elements = getNoPaintElements(contentRootElement);
          for (let i = 0; i < elements.length; ++i) {
            if (windowUtils().checkAndClearPaintedState(elements[i])) {
              SendFailedNoPaint();
            }
          }
          // We only support retained display lists in the content process
          // right now, so don't fail reftest-no-display-list tests when
          // we don't have e10s.
          if (gBrowserIsRemote) {
            elements = getNoDisplayListElements(contentRootElement);
            for (let i = 0; i < elements.length; ++i) {
              if (windowUtils().checkAndClearDisplayListState(elements[i])) {
                SendFailedNoDisplayList();
              }
            }
            elements = getDisplayListElements(contentRootElement);
            for (let i = 0; i < elements.length; ++i) {
              if (!windowUtils().checkAndClearDisplayListState(elements[i])) {
                SendFailedDisplayList();
              }
            }
          }
          // Check if WebRender rasterized any tiles when it shouldn't have.
          if (shouldCheckNoWRRaster(contentRootElement)) {
            if (windowUtils().checkAndClearWRDidRasterize()) {
              SendFailedNoWRRaster();
            }
          }
        }

        if (!IsSnapshottableTestType()) {
          // If we're not snapshotting the test, at least do a sync round-trip
          // to the compositor to ensure that all the rendering messages
          // related to this test get processed. Otherwise problems triggered
          // by this test may only manifest as failures in a later test.
          LogInfo("MakeProgress: Doing sync flush to compositor");
          gFailureReason = "timed out while waiting for sync compositor flush";
          windowUtils().syncFlushCompositor();
        }

        LogInfo("MakeProgress: Completed");
        state = STATE_COMPLETED;
        gFailureReason = "timed out while taking snapshot (bug in harness?)";
        RemoveListeners();
        CheckForLivenessOfContentRootElement();
        CheckForProcessCrashExpectation(contentRootElement);
        setTimeout(RecordResult, 0, forURL);
    }
  }

  LogInfo("WaitForTestEnd: Adding listeners");
  addEventListener("MozAfterPaint", AfterPaintListener, false);
  addEventListener(
    "Reftest:MozAfterPaintFromChild",
    FromChildAfterPaintListener,
    false
  );

  // If contentRootElement is null then shouldWaitForReftestWaitRemoval will
  // always return false so we don't need a listener anyway
  CheckForLivenessOfContentRootElement();
  if (contentRootElement?.hasAttribute("class")) {
    attrModifiedObserver =
      // documentGlobal doesn't exist in content windows.
      // eslint-disable-next-line mozilla/use-documentGlobal
      new contentRootElement.ownerDocument.defaultView.MutationObserver(
        AttrModifiedListener
      );
    attrModifiedObserver.observe(contentRootElement, { attributes: true });
  }
  gTimeoutHook = RemoveListeners;

  // Listen for spell checks on spell-checked elements.
  var numPendingSpellChecks = spellCheckedElements.length;
  function decNumPendingSpellChecks() {
    --numPendingSpellChecks;
    if (operationInProgress) {
      CallSetTimeoutMakeProgress();
    } else {
      MakeProgress();
    }
  }
  for (let editable of spellCheckedElements) {
    try {
      onSpellCheck(editable, decNumPendingSpellChecks);
    } catch (err) {
      // The element may not have an editor, so ignore it.
      setTimeout(decNumPendingSpellChecks, 0);
    }
  }

  // Take a full snapshot now that all our listeners are set up. This
  // ensures it's impossible for us to miss updates between taking the snapshot
  // and adding our listeners.
  OperationInProgress();
  let promise = SendInitCanvasWithSnapshot(forURL);
  promise.then(function () {
    OperationCompleted();
    MakeProgress();
  });
}

async function OnDocumentLoad(uri) {
  if (gClearingForAssertionCheck) {
    if (uri == BLANK_URL_FOR_CLEARING) {
      DoAssertionCheck();
      return;
    }

    // It's likely the previous test document reloads itself and causes the
    // attempt of loading blank page fails. In this case we should retry
    // loading the blank page.
    LogInfo("Retry loading a blank page");
    setTimeout(LoadURI, 0, BLANK_URL_FOR_CLEARING);
    return;
  }

  if (uri != gCurrentURL) {
    LogInfo("OnDocumentLoad fired for previous document");
    // Ignore load events for previous documents.
    return;
  }

  var currentDoc = content && content.document;

  // Collect all editable, spell-checked elements.  It may be the case that
  // not all the elements that match this selector will be spell checked: for
  // example, a textarea without a spellcheck attribute may have a parent with
  // spellcheck=false, or script may set spellcheck=false on an element whose
  // markup sets it to true.  But that's OK since onSpellCheck detects the
  // absence of spell checking, too.
  var querySelector =
    '*[class~="spell-checked"],' +
    'textarea:not([spellcheck="false"]),' +
    'input[spellcheck]:-moz-any([spellcheck=""],[spellcheck="true"]),' +
    '*[contenteditable]:-moz-any([contenteditable=""],[contenteditable="true"])';
  var spellCheckedElements = currentDoc
    ? currentDoc.querySelectorAll(querySelector)
    : [];

  var contentRootElement = currentDoc ? currentDoc.documentElement : null;
  currentDoc = null;
  setupFullZoom(contentRootElement);
  setupTextZoom(contentRootElement);
  setupViewport(contentRootElement);
  await setupDisplayport(contentRootElement);
  var inPrintMode = false;

  async function AfterOnLoadScripts() {
    // Regrab the root element, because the document may have changed.
    var contentRootElement = content.document
      ? content.document.documentElement
      : null;

    // Flush the document in case it got modified in a load event handler.
    await FlushRendering(FlushMode.ALL);

    // Take a snapshot now.
    let painted = await SendInitCanvasWithSnapshot(uri);

    if (contentRootElement && Cu.isDeadWrapper(contentRootElement)) {
      contentRootElement = null;
    }

    if (
      (!inPrintMode && doPrintMode(contentRootElement)) ||
      // If we didn't force a paint above, in
      // InitCurrentCanvasWithSnapshot, so we should wait for a
      // paint before we consider them done.
      !painted
    ) {
      LogInfo("AfterOnLoadScripts belatedly entering WaitForTestEnd");
      // Go into reftest-wait mode belatedly.
      WaitForTestEnd(contentRootElement, inPrintMode, [], uri);
    } else {
      CheckForProcessCrashExpectation(contentRootElement);
      RecordResult(uri);
    }
  }

  if (
    shouldWaitForReftestWaitRemoval(contentRootElement) ||
    spellCheckedElements.length
  ) {
    // Go into reftest-wait mode immediately after painting has been
    // unsuppressed, after the onload event has finished dispatching.
    gFailureReason =
      "timed out waiting for test to complete (trying to get into WaitForTestEnd)";
    LogInfo("OnDocumentLoad triggering WaitForTestEnd");
    setTimeout(function () {
      WaitForTestEnd(
        contentRootElement,
        inPrintMode,
        spellCheckedElements,
        uri
      );
    }, 0);
  } else {
    if (doPrintMode(contentRootElement)) {
      LogInfo("OnDocumentLoad setting up print mode");
      setupPrintMode(contentRootElement);
      inPrintMode = true;
    }

    // Since we can't use a bubbling-phase load listener from chrome,
    // this is a capturing phase listener.  So do setTimeout twice, the
    // first to get us after the onload has fired in the content, and
    // the second to get us after any setTimeout(foo, 0) in the content.
    gFailureReason =
      "timed out waiting for test to complete (waiting for onload scripts to complete)";
    LogInfo("OnDocumentLoad triggering AfterOnLoadScripts");
    setTimeout(function () {
      setTimeout(AfterOnLoadScripts, 0);
    }, 0);
  }
}

function CheckForProcessCrashExpectation(contentRootElement) {
  if (
    contentRootElement &&
    contentRootElement.hasAttribute("class") &&
    contentRootElement
      .getAttribute("class")
      .split(/\s+/)
      .includes("reftest-expect-process-crash")
  ) {
    SendExpectProcessCrash();
  }
}

async function RecordResult(forURL) {
  if (forURL != gCurrentURL) {
    LogInfo("RecordResult fired for previous document");
    return;
  }

  if (gCurrentURLRecordResults > 0) {
    LogInfo("RecordResult fired extra times");
    FinishTestItem();
    return;
  }
  gCurrentURLRecordResults++;

  LogInfo("RecordResult fired");

  var currentTestRunTime = Date.now() - gCurrentTestStartTime;

  clearTimeout(gFailureTimeout);
  gFailureReason = null;
  gFailureTimeout = null;
  gCurrentURL = null;
  gCurrentURLTargetType = undefined;

  if (gCurrentTestType == TYPE_PRINT) {
    printToPdf();
    return;
  }
  if (gCurrentTestType == TYPE_SCRIPT) {
    var error = "";
    var testwindow = content;

    if (testwindow.wrappedJSObject) {
      testwindow = testwindow.wrappedJSObject;
    }

    var testcases;
    if (
      !testwindow.getTestCases ||
      typeof testwindow.getTestCases != "function"
    ) {
      // Force an unexpected failure to alert the test author to fix the test.
      error = "test must provide a function getTestCases(). (SCRIPT)\n";
    } else if (!(testcases = testwindow.getTestCases())) {
      // Force an unexpected failure to alert the test author to fix the test.
      error =
        "test's getTestCases() must return an Array-like Object. (SCRIPT)\n";
    } else if (!testcases.length) {
      // This failure may be due to a JavaScript Engine bug causing
      // early termination of the test. If we do not allow silent
      // failure, the driver will report an error.
    }

    var results = [];
    if (!error) {
      // FIXME/bug 618176: temporary workaround
      for (var i = 0; i < testcases.length; ++i) {
        var test = testcases[i];
        results.push({
          passed: test.testPassed(),
          description: test.testDescription(),
        });
      }
      //results = testcases.map(function(test) {
      //        return { passed: test.testPassed(),
      //                 description: test.testDescription() };
    }

    SendScriptResults(currentTestRunTime, error, results);
    FinishTestItem();
    return;
  }

  // Setup async scroll offsets now in case SynchronizeForSnapshot is not
  // called (due to reftest-no-sync-layers being supplied, or in the single
  // process case).
  let changedAsyncScrollZoom = await setupAsyncScrollOffsets({
    allowFailure: true,
  });
  if (setupAsyncZoom({ allowFailure: true })) {
    changedAsyncScrollZoom = true;
  }
  if (changedAsyncScrollZoom && !gBrowserIsRemote) {
    sendAsyncMessage("reftest:UpdateWholeCanvasForInvalidation");
  }

  SendTestDone(currentTestRunTime);
  FinishTestItem();
}

function LoadFailed() {
  if (gTimeoutHook) {
    gTimeoutHook();
  }
  gFailureTimeout = null;
  SendFailedLoad(gFailureReason);
}

function FinishTestItem() {
  gHaveCanvasSnapshot = false;
}

function DoAssertionCheck() {
  gClearingForAssertionCheck = false;

  var numAsserts = 0;
  if (gDebug.isDebugBuild) {
    var newAssertionCount = gDebug.assertionCount;
    numAsserts = newAssertionCount - gAssertionCount;
    gAssertionCount = newAssertionCount;
  }
  SendAssertionCount(numAsserts);
}

function LoadURI(uri) {
  let loadURIOptions = {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  };
  webNavigation().loadURI(Services.io.newURI(uri), loadURIOptions);
}

function LogError(str) {
  if (gVerbose) {
    sendSyncMessage("reftest:Log", { type: "error", msg: str });
  } else {
    sendAsyncMessage("reftest:Log", { type: "error", msg: str });
  }
}

function LogWarning(str) {
  if (gVerbose) {
    sendSyncMessage("reftest:Log", { type: "warning", msg: str });
  } else {
    sendAsyncMessage("reftest:Log", { type: "warning", msg: str });
  }
}

function LogInfo(str) {
  if (gVerbose) {
    sendSyncMessage("reftest:Log", { type: "info", msg: str });
  } else {
    sendAsyncMessage("reftest:Log", { type: "info", msg: str });
  }
}

function IsSnapshottableTestType() {
  // Script, load-only, and PDF-print tests do not need any snapshotting.
  return !(
    gCurrentTestType == TYPE_SCRIPT ||
    gCurrentTestType == TYPE_LOAD ||
    gCurrentTestType == TYPE_PRINT
  );
}

const SYNC_DEFAULT = 0x0;
const SYNC_ALLOW_DISABLE = 0x1;
// Returns a promise that resolve when the snapshot is done.
function SynchronizeForSnapshot(flags) {
  if (!IsSnapshottableTestType()) {
    return Promise.resolve(undefined);
  }

  if (flags & SYNC_ALLOW_DISABLE) {
    var docElt = content.document.documentElement;
    if (
      docElt &&
      (docElt.hasAttribute("reftest-no-sync-layers") || shouldNotFlush(docElt))
    ) {
      LogInfo("Test file chose to skip SynchronizeForSnapshot");
      return Promise.resolve(undefined);
    }
  }

  let browsingContext = content.docShell.browsingContext;
  let promise = content.windowGlobalChild
    .getActor("ReftestFission")
    .sendQuery("UpdateLayerTree", { browsingContext });
  return promise.then(
    function (result) {
      for (let errorString of result.errorStrings) {
        LogError(errorString);
      }
      for (let infoString of result.infoStrings) {
        LogInfo(infoString);
      }

      // Setup async scroll offsets now, because any scrollable layers should
      // have had their AsyncPanZoomControllers created.
      return setupAsyncScrollOffsets({ allowFailure: false }).then(function () {
        setupAsyncZoom({ allowFailure: false });
      });
    },
    function (reason) {
      // We expect actors to go away causing sendQuery's to fail, so
      // just note it.
      LogInfo("UpdateLayerTree sendQuery to parent rejected: " + reason);

      // Setup async scroll offsets now, because any scrollable layers should
      // have had their AsyncPanZoomControllers created.
      return setupAsyncScrollOffsets({ allowFailure: false }).then(function () {
        setupAsyncZoom({ allowFailure: false });
      });
    }
  );
}

function RegisterMessageListeners() {
  addMessageListener("reftest:Clear", function () {
    RecvClear();
  });
  addMessageListener("reftest:LoadScriptTest", function (m) {
    RecvLoadScriptTest(m.json.uri, m.json.timeout);
  });
  addMessageListener("reftest:LoadPrintTest", function (m) {
    RecvLoadPrintTest(m.json.uri, m.json.timeout);
  });
  addMessageListener("reftest:LoadTest", function (m) {
    RecvLoadTest(m.json.type, m.json.uri, m.json.uriTargetType, m.json.timeout);
  });
  addMessageListener("reftest:ResetRenderingState", function () {
    RecvResetRenderingState();
  });
  addMessageListener("reftest:PrintDone", function (m) {
    RecvPrintDone(m.json.status, m.json.fileName);
  });
  addMessageListener("reftest:UpdateCanvasWithSnapshotDone", function (m) {
    RecvUpdateCanvasWithSnapshotDone(m.json.painted);
  });
}

function RecvClear() {
  gClearingForAssertionCheck = true;
  LoadURI(BLANK_URL_FOR_CLEARING);
}

function RecvLoadTest(type, uri, uriTargetType, timeout) {
  StartTestURI(type, uri, uriTargetType, timeout);
}

function RecvLoadScriptTest(uri, timeout) {
  StartTestURI(TYPE_SCRIPT, uri, URL_TARGET_TYPE_TEST, timeout);
}

function RecvLoadPrintTest(uri, timeout) {
  StartTestURI(TYPE_PRINT, uri, URL_TARGET_TYPE_TEST, timeout);
}

function RecvResetRenderingState() {
  resetZoomAndTextZoom();
  resetDisplayportAndViewport();
}

function RecvPrintDone(status, fileName) {
  const currentTestRunTime = Date.now() - gCurrentTestStartTime;
  SendPrintResult(currentTestRunTime, status, fileName);
  FinishTestItem();
}

function RecvUpdateCanvasWithSnapshotDone(painted) {
  gUpdateCanvasPromiseResolver(painted);
}

function SendAssertionCount(numAssertions) {
  sendAsyncMessage("reftest:AssertionCount", { count: numAssertions });
}

function SendContentReady() {
  let gfxInfo =
    NS_GFXINFO_CONTRACTID in Cc &&
    Cc[NS_GFXINFO_CONTRACTID].getService(Ci.nsIGfxInfo);

  let info = {};

  try {
    info.DWriteEnabled = gfxInfo.DWriteEnabled;
  } catch (e) {
    info.DWriteEnabled = false;
  }

  info.AzureCanvasBackend = gfxInfo.AzureCanvasBackend;
  info.AzureContentBackend = gfxInfo.AzureContentBackend;

  return sendSyncMessage("reftest:ContentReady", { gfx: info })[0];
}

function SendException(what) {
  sendAsyncMessage("reftest:Exception", { what });
}

function SendFailedLoad(why) {
  sendAsyncMessage("reftest:FailedLoad", { why });
}

function SendFailedNoPaint() {
  sendAsyncMessage("reftest:FailedNoPaint");
}

function SendFailedNoDisplayList() {
  sendAsyncMessage("reftest:FailedNoDisplayList");
}

function SendFailedDisplayList() {
  sendAsyncMessage("reftest:FailedDisplayList");
}

function SendFailedNoWRRaster() {
  sendAsyncMessage("reftest:FailedNoWRRaster");
}

function SendFailedOpaqueLayer(why) {
  sendAsyncMessage("reftest:FailedOpaqueLayer", { why });
}

function SendFailedAssignedLayer(why) {
  sendAsyncMessage("reftest:FailedAssignedLayer", { why });
}

// Returns a promise that resolves to a bool that indicates if a snapshot was taken.
async function SendInitCanvasWithSnapshot(forURL) {
  if (forURL != gCurrentURL) {
    LogInfo("SendInitCanvasWithSnapshot called for previous document");
    // Lie and say we painted because it doesn't matter, this is a test we
    // are already done with that is clearing out. Then AfterOnLoadScripts
    // should finish quicker if that is who is calling us.
    return Promise.resolve(true);
  }

  // If we're in the same process as the top-level XUL window, then
  // drawing that window will also update our layers, so no
  // synchronization is needed.
  //
  // NB: this is a test-harness optimization only, it must not
  // affect the validity of the tests.
  if (gBrowserIsRemote) {
    await SynchronizeForSnapshot(SYNC_DEFAULT);
    let promise = new Promise(resolve => {
      gUpdateCanvasPromiseResolver = resolve;
    });
    sendAsyncMessage("reftest:InitCanvasWithSnapshot");

    gHaveCanvasSnapshot = await promise;
    return gHaveCanvasSnapshot;
  }

  // For in-process browser, we have to make a synchronous request
  // here to make the above optimization valid, so that MozWaitPaint
  // events dispatched (synchronously) during painting are received
  // before we check the paint-wait counter.  For out-of-process
  // browser though, it doesn't wrt correctness whether this request
  // is sync or async.
  let promise = new Promise(resolve => {
    gUpdateCanvasPromiseResolver = resolve;
  });
  sendAsyncMessage("reftest:InitCanvasWithSnapshot");

  gHaveCanvasSnapshot = await promise;
  return Promise.resolve(gHaveCanvasSnapshot);
}

function SendScriptResults(runtimeMs, error, results) {
  sendAsyncMessage("reftest:ScriptResults", {
    runtimeMs,
    error,
    results,
  });
}

function SendStartPrint(isPrintSelection, printRange) {
  sendAsyncMessage("reftest:StartPrint", { isPrintSelection, printRange });
}

function SendPrintResult(runtimeMs, status, fileName) {
  sendAsyncMessage("reftest:PrintResult", {
    runtimeMs,
    status,
    fileName,
  });
}

function SendExpectProcessCrash() {
  sendAsyncMessage("reftest:ExpectProcessCrash");
}

function SendTestDone(runtimeMs) {
  sendAsyncMessage("reftest:TestDone", { runtimeMs });
}

function roundTo(x, fraction) {
  return Math.round(x / fraction) * fraction;
}

function elementDescription(element) {
  return (
    "<" +
    element.localName +
    [].slice
      .call(element.attributes)
      .map(attr => ` ${attr.nodeName}="${attr.value}"`)
      .join("") +
    ">"
  );
}

async function SendUpdateCanvasForEvent(forURL, rectList, contentRootElement) {
  if (forURL != gCurrentURL) {
    LogInfo("SendUpdateCanvasForEvent called for previous document");
    // This is a test we are already done with that is clearing out.
    // Don't do anything.
    return;
  }

  var scale = docShell.browsingContext.fullZoom;

  var rects = [];
  if (shouldSnapshotWholePage(contentRootElement)) {
    // See comments in SendInitCanvasWithSnapshot() re: the split
    // logic here.
    if (!gBrowserIsRemote) {
      sendSyncMessage("reftest:UpdateWholeCanvasForInvalidation");
    } else {
      await SynchronizeForSnapshot(SYNC_ALLOW_DISABLE);
      let promise = new Promise(resolve => {
        gUpdateCanvasPromiseResolver = resolve;
      });
      sendAsyncMessage("reftest:UpdateWholeCanvasForInvalidation");
      await promise;
    }
    return;
  }

  var message;

  if (!windowUtils().isMozAfterPaintPending) {
    // Webrender doesn't have invalidation, and animations on the compositor
    // don't invoke any MozAfterEvent which means we have no invalidated
    // rect so we just invalidate the whole screen once we don't have
    // anymore paints pending. This will force the snapshot.

    LogInfo("Sending update whole canvas for invalidation");
    message = "reftest:UpdateWholeCanvasForInvalidation";
  } else {
    LogInfo("SendUpdateCanvasForEvent with " + rectList.length + " rects");
    for (var i = 0; i < rectList.length; ++i) {
      var r = rectList[i];
      // Set left/top/right/bottom to "device pixel" boundaries
      var left = Math.floor(roundTo(r.left * scale, 0.001));
      var top = Math.floor(roundTo(r.top * scale, 0.001));
      var right = Math.ceil(roundTo(r.right * scale, 0.001));
      var bottom = Math.ceil(roundTo(r.bottom * scale, 0.001));
      LogInfo("Rect: " + left + " " + top + " " + right + " " + bottom);

      rects.push({ left, top, right, bottom });
    }

    message = "reftest:UpdateCanvasForInvalidation";
  }

  // See comments in SendInitCanvasWithSnapshot() re: the split
  // logic here.
  if (!gBrowserIsRemote) {
    sendSyncMessage(message, { rects });
  } else {
    await SynchronizeForSnapshot(SYNC_ALLOW_DISABLE);
    let promise = new Promise(resolve => {
      gUpdateCanvasPromiseResolver = resolve;
    });
    sendAsyncMessage(message, { rects });
    await promise;
  }
}

if (content.document.readyState == "complete") {
  // load event has already fired for content, get started
  OnInitialLoad();
} else {
  addEventListener("load", OnInitialLoad, true);
}
