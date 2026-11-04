/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

let { EventEmitter } = ChromeUtils.importESModule(
  "resource://gre/modules/EventEmitter.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const {
  DEFAULT_UNLOAD_TIMEOUT,
  getUnloadTimeoutMultiplier,
  ProgressListener,
  waitForInitialNavigationCompleted,
} = ChromeUtils.importESModule(
  "chrome://remote/content/shared/Navigate.sys.mjs"
);

const { isInitialDocument, isUncommittedInitialDocument } =
  ChromeUtils.importESModule(
    "chrome://remote/content/shared/BrowsingContextUtils.sys.mjs"
  );

const LOAD_FLAG_ERROR_PAGE = 0x10000;

// Force the initialization of NSS, without which PR_ErrorToName() cannot
// resolve NSS error codes and ChromeUtils.getXPCOMErrorName() asserts.
Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);

const SEC_ERROR_EXPIRED_CERTIFICATE = Cc["@mozilla.org/nss_errors_service;1"]
  .getService(Ci.nsINSSErrorsService)
  .getXPCOMFromNSSError(Ci.nsINSSErrorsService.NSS_SEC_ERROR_BASE + 11);

const BLOCKED_PAGE_URI = Services.io.newURI(
  "about:blocked?e=deceptiveBlocked&u=http%3A//foo.cheese/"
);
const CURRENT_URI = Services.io.newURI("http://foo.bar/");
const ERROR_PAGE_URI = Services.io.newURI(
  "about:neterror?e=customErrorMessage"
);
const INITIAL_URI = Services.io.newURI("about:blank");
const PREVIOUS_LOAD_URI = Services.io.newURI("http://foo.previous/");
const TARGET_URI = Services.io.newURI("http://foo.cheese/");
const TARGET_URI_ERROR_PAGE = Services.io.newURI("doesnotexist://");
const TARGET_URI_WITH_HASH = Services.io.newURI("http://foo.cheese/#foo");
const TARGET_URI_FROM_NAVIGATION_COMMITTED = Services.io.newURI(
  "http://foo.cheese/#bar"
);

function wait(time) {
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  return new Promise(resolve => setTimeout(resolve, time));
}

async function getRejectionReason(promise) {
  try {
    await promise;
  } catch (e) {
    return e;
  }

  return null;
}

class MockRequest {
  constructor(uri) {
    this.originalURI = uri;
  }

  QueryInterface = ChromeUtils.generateQI(["nsIRequest", "nsIChannel"]);
}

class MockWebProgress {
  constructor(browsingContext) {
    this.browsingContext = browsingContext;

    this.documentRequest = null;
    this.isLoadingDocument = false;
    this.listener = null;
    this.loadType = 0;
    this.progressListenerRemoved = false;
  }

  addProgressListener(listener) {
    if (this.listener) {
      throw new Error("Cannot register listener twice");
    }

    this.listener = listener;
  }

  removeProgressListener(listener) {
    if (listener === this.listener) {
      this.listener = null;
      this.progressListenerRemoved = true;
    } else {
      throw new Error("Unknown listener");
    }
  }

  sendLocationChange(options = {}) {
    const { errorPageURI = ERROR_PAGE_URI, flag = 0 } = options;

    this.documentRequest = null;

    if (flag & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      this.browsingContext.updateURI(TARGET_URI_WITH_HASH);
    } else if (flag & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE) {
      this.browsingContext.updateURI(TARGET_URI_ERROR_PAGE, errorPageURI);
    }

    this.listener?.onLocationChange(
      this,
      this.documentRequest,
      this.browsingContext.currentURI,
      flag
    );

    return new Promise(executeSoon);
  }

  sendStartState(options = {}) {
    const { coop = false, isInitial = false, uri } = options;

    if (coop) {
      this.browsingContext = new MockTopContext(this);
    }

    if (!this.browsingContext.currentWindowGlobal) {
      this.browsingContext.currentWindowGlobal = {};
    }

    this.browsingContext.currentWindowGlobal.isInitialDocument = isInitial;
    // Start is sent for the initial about:blank if and only if we commit to it
    this.browsingContext.currentWindowGlobal.isUncommittedInitialDocument = false;

    this.isLoadingDocument = true;
    this.loadType = 0;
    this.documentRequest = new MockRequest(
      uri ?? (isInitial ? INITIAL_URI : TARGET_URI)
    );

    this.listener?.onStateChange(
      this,
      this.documentRequest,
      Ci.nsIWebProgressListener.STATE_START,
      0
    );

    return new Promise(executeSoon);
  }

  sendStopState(options = {}) {
    const { errorPageURI, flag = 0, loadType = 0, uri } = options;

    // A custom URI simulates a stop state for a different request than the
    // one which is currently loading.
    const request = uri ? new MockRequest(uri) : this.documentRequest;

    this.browsingContext.currentURI = request.originalURI;

    this.isLoadingDocument = false;
    this.documentRequest = null;

    if (loadType & LOAD_FLAG_ERROR_PAGE) {
      // loadType must be set before sending the stop state.
      this.loadType = 0x10000;
    }

    this.listener?.onStateChange(
      this,
      request,
      Ci.nsIWebProgressListener.STATE_STOP,
      flag
    );

    if (loadType & LOAD_FLAG_ERROR_PAGE) {
      return this.sendLocationChange({
        errorPageURI,
        flag: Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE,
      });
    }

    return new Promise(executeSoon);
  }
}

class MockTopContext {
  constructor(webProgress = null) {
    this.currentURI = CURRENT_URI;
    this.currentWindowGlobal = {
      isInitialDocument: true,
      isUncommittedInitialDocument: true,
      documentURI: CURRENT_URI,
    };
    this.id = 7;
    this.top = this;
    this.webProgress = webProgress || new MockWebProgress(this);
  }

  updateURI(uri, errorPageURI = null) {
    this.currentURI = uri;
    if (errorPageURI) {
      this.currentWindowGlobal.documentURI = errorPageURI;
    } else {
      this.currentWindowGlobal.documentURI = uri;
    }
  }
}

add_task(
  async function test_waitForInitialNavigation_initialDocumentNoWindowGlobal() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    // In some cases there might be no window global yet.
    delete browsingContext.currentWindowGlobal;

    ok(!webProgress.isLoadingDocument, "Document is not loading");

    // without window global, we'll wait for start and stop
    const navigated = waitForInitialNavigationCompleted(webProgress);
    await webProgress.sendStartState({ isInitial: true });

    ok(
      !(await hasPromiseResolved(navigated)),
      "waitForInitialNavigationCompleted has not resolved yet"
    );

    await webProgress.sendStopState();
    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
      "Is initial document"
    );
    equal(
      currentURI.spec,
      INITIAL_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, INITIAL_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_initialDocumentNotLoaded() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      isUncommittedInitialDocument(webProgress.browsingContext),
      "Document is uncommitted initial"
    );

    // for uncommitted initial, we'll wait for start and stop
    const navigated = waitForInitialNavigationCompleted(webProgress);

    await webProgress.sendStartState({ isInitial: true });

    ok(
      !(await hasPromiseResolved(navigated)),
      "waitForInitialNavigationCompleted has not resolved yet"
    );

    await webProgress.sendStopState();
    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
      "Is initial document"
    );
    equal(
      currentURI.spec,
      INITIAL_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, INITIAL_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_initialDocumentLoadingAndNoAdditionalLoad() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    await webProgress.sendStartState({ isInitial: true });
    ok(webProgress.isLoadingDocument, "Document is loading");

    const navigated = waitForInitialNavigationCompleted(webProgress);

    ok(
      !(await hasPromiseResolved(navigated)),
      "waitForInitialNavigationCompleted has not resolved yet"
    );

    await webProgress.sendStopState();
    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
      "Is initial document"
    );
    equal(
      currentURI.spec,
      INITIAL_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, INITIAL_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_initialDocumentFinishedLoadingNoAdditionalLoad() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    await webProgress.sendStartState({ isInitial: true });
    await webProgress.sendStopState();

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(isInitialDocument(webProgress.browsingContext), "Document is initial");
    ok(
      !isUncommittedInitialDocument(webProgress.browsingContext),
      "Document is uncommitted"
    );

    const navigated = waitForInitialNavigationCompleted(webProgress);

    ok(
      await hasPromiseResolved(navigated),
      "waitForInitialNavigationCompleted resolves immediately"
    );

    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
      "Is initial document"
    );
    equal(
      currentURI.spec,
      INITIAL_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, INITIAL_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_notInitialDocumentNotLoading() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    ok(!webProgress.isLoadingDocument, "Document is not loading");

    const navigated = waitForInitialNavigationCompleted(webProgress);
    await webProgress.sendStartState({ isInitial: false });

    ok(
      !(await hasPromiseResolved(navigated)),
      "waitForInitialNavigationCompleted has not resolved yet"
    );

    await webProgress.sendStopState();
    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      !browsingContext.currentWindowGlobal.isInitialDocument,
      "Is not initial document"
    );
    equal(
      currentURI.spec,
      TARGET_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, TARGET_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_notInitialDocumentAlreadyLoading() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    await webProgress.sendStartState({ isInitial: false });
    ok(webProgress.isLoadingDocument, "Document is loading");

    const navigated = waitForInitialNavigationCompleted(webProgress);

    ok(
      !(await hasPromiseResolved(navigated)),
      "waitForInitialNavigationCompleted has not resolved yet"
    );

    await webProgress.sendStopState();
    const { currentURI, targetURI } = await navigated;

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      !browsingContext.currentWindowGlobal.isInitialDocument,
      "Is not initial document"
    );
    equal(
      currentURI.spec,
      TARGET_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, TARGET_URI.spec, "Expected target URI has been set");
  }
);

add_task(
  async function test_waitForInitialNavigation_notInitialDocumentFinishedLoading() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    await webProgress.sendStartState({ isInitial: false });
    await webProgress.sendStopState();

    ok(!webProgress.isLoadingDocument, "Document is not loading");

    const { currentURI, targetURI } =
      await waitForInitialNavigationCompleted(webProgress);

    ok(!webProgress.isLoadingDocument, "Document is not loading");
    ok(
      !webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
      "Is not initial document"
    );
    equal(
      currentURI.spec,
      TARGET_URI.spec,
      "Expected current URI has been set"
    );
    equal(targetURI.spec, TARGET_URI.spec, "Expected target URI has been set");
  }
);

add_task(async function test_waitForInitialNavigation_resolveWhenStarted() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  await webProgress.sendStartState({ isInitial: true });
  ok(webProgress.isLoadingDocument, "Document is already loading");

  const { currentURI, targetURI } = await waitForInitialNavigationCompleted(
    webProgress,
    {
      resolveWhenStarted: true,
    }
  );

  ok(webProgress.isLoadingDocument, "Document is still loading");
  ok(
    webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
    "Is initial document"
  );
  equal(currentURI.spec, CURRENT_URI.spec, "Expected current URI has been set");
  equal(targetURI.spec, INITIAL_URI.spec, "Expected target URI has been set");
});

add_task(async function test_waitForInitialNavigation_crossOrigin() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  ok(!webProgress.isLoadingDocument, "Document is not loading");

  const navigated = waitForInitialNavigationCompleted(webProgress);
  await webProgress.sendStartState({ coop: true });

  ok(
    !(await hasPromiseResolved(navigated)),
    "waitForInitialNavigationCompleted has not resolved yet"
  );

  await webProgress.sendStopState();
  const { currentURI, targetURI } = await navigated;

  notEqual(
    browsingContext,
    webProgress.browsingContext,
    "Got new browsing context"
  );
  ok(!webProgress.isLoadingDocument, "Document is not loading");
  ok(
    !webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
    "Is not initial document"
  );
  equal(currentURI.spec, TARGET_URI.spec, "Expected current URI has been set");
  equal(targetURI.spec, TARGET_URI.spec, "Expected target URI has been set");
});

add_task(async function test_waitForInitialNavigation_unloadTimeout_default() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  // Document starts out as uncommitted initial and not loading
  ok(!webProgress.isLoadingDocument, "Document is not loading");

  const navigated = waitForInitialNavigationCompleted(webProgress);

  // Start a timer longer than the timeout which will be used by
  // waitForInitialNavigationCompleted, and check that navigated resolves first.
  const waitForMoreThanDefaultTimeout = wait(
    DEFAULT_UNLOAD_TIMEOUT * 1.5 * getUnloadTimeoutMultiplier()
  );
  await Promise.race([navigated, waitForMoreThanDefaultTimeout]);

  ok(
    await hasPromiseResolved(navigated),
    "waitForInitialNavigationCompleted has resolved"
  );

  ok(!webProgress.isLoadingDocument, "Document is not loading");
  ok(
    webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
    "Document is still on the initial document"
  );
});

add_task(async function test_waitForInitialNavigation_unloadTimeout_longer() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  // Document starts out as uncommitted initial and not loading
  ok(!webProgress.isLoadingDocument, "Document is not loading");

  const navigated = waitForInitialNavigationCompleted(webProgress, {
    unloadTimeout: DEFAULT_UNLOAD_TIMEOUT * 3,
  });

  // Start a timer longer than the default timeout of the Navigate module.
  // However here we used a custom timeout, so we expect that the navigation
  // will not be done yet by the time this timer is done.
  const waitForMoreThanDefaultTimeout = wait(
    DEFAULT_UNLOAD_TIMEOUT * 1.5 * getUnloadTimeoutMultiplier()
  );
  await Promise.race([navigated, waitForMoreThanDefaultTimeout]);

  // The promise should not have resolved because we didn't reached the custom
  // timeout which is 3 times the default one.
  ok(
    !(await hasPromiseResolved(navigated)),
    "waitForInitialNavigationCompleted has not resolved yet"
  );

  // The navigation should eventually resolve once we reach the custom timeout.
  await navigated;

  ok(!webProgress.isLoadingDocument, "Document is not loading");
  ok(
    webProgress.browsingContext.currentWindowGlobal.isInitialDocument,
    "Document is still on the initial document"
  );
});

add_task(async function test_ProgressListener_expectNavigation() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  const progressListener = new ProgressListener(webProgress, {
    expectNavigation: true,
    unloadTimeout: 10,
  });
  const navigated = progressListener.start();

  // Wait for unloadTimeout to finish in case it started
  await wait(30);

  ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved yet");

  await webProgress.sendStartState();
  await webProgress.sendStopState();

  ok(await hasPromiseResolved(navigated), "Listener has resolved");
});

add_task(
  async function test_ProgressListener_expectNavigation_initialDocument() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    const progressListener = new ProgressListener(webProgress, {
      expectNavigation: true,
      unloadTimeout: 10,
    });
    const navigated = progressListener.start();

    ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved yet");

    await webProgress.sendStartState({ isInitial: true });
    await webProgress.sendStopState();

    ok(await hasPromiseResolved(navigated), "Listener has resolved");
  }
);

add_task(async function test_ProgressListener_isStarted() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  const progressListener = new ProgressListener(webProgress);
  ok(!progressListener.isStarted);

  progressListener.start();
  ok(progressListener.isStarted);

  progressListener.stop();
  ok(!progressListener.isStarted);
});

add_task(async function test_ProgressListener_notWaitForExplicitStart() {
  // Create a webprogress and start it before creating the progress listener.
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;
  await webProgress.sendStartState();

  // Create the progress listener for a webprogress already in a navigation.
  const progressListener = new ProgressListener(webProgress, {
    waitForExplicitStart: false,
  });
  const navigated = progressListener.start();

  // Send stop state to complete the initial navigation
  await webProgress.sendStopState();
  ok(
    await hasPromiseResolved(navigated),
    "Listener has resolved after initial navigation"
  );
});

add_task(async function test_ProgressListener_waitForExplicitStart() {
  // Create a webprogress and start it before creating the progress listener.
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;
  await webProgress.sendStartState();

  // Create the progress listener for a webprogress already in a navigation.
  const progressListener = new ProgressListener(webProgress, {
    waitForExplicitStart: true,
  });
  const navigated = progressListener.start();

  // Send stop state to complete the initial navigation
  await webProgress.sendStopState();
  ok(
    !(await hasPromiseResolved(navigated)),
    "Listener has not resolved after initial navigation"
  );

  // Start a new navigation
  await webProgress.sendStartState();
  ok(
    !(await hasPromiseResolved(navigated)),
    "Listener has not resolved after starting new navigation"
  );

  // Finish the new navigation
  await webProgress.sendStopState();
  ok(
    await hasPromiseResolved(navigated),
    "Listener resolved after finishing the new navigation"
  );
});

add_task(
  async function test_ProgressListener_waitForExplicitStart_suppressedStartState() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;
    await webProgress.sendStartState({ uri: PREVIOUS_LOAD_URI });

    const progressListener = new ProgressListener(webProgress, {
      expectNavigation: true,
      waitForExplicitStart: true,
    });
    const navigated = progressListener.start();

    // The start state of the new navigation is not sent because the previous
    // load has not been finished yet.
    await webProgress.sendStopState({ uri: TARGET_URI });
    ok(
      await hasPromiseResolved(navigated),
      "Listener resolved after finishing the new navigation"
    );
    equal(
      progressListener.targetURI.spec,
      TARGET_URI.spec,
      "Expected target URI is set"
    );
  }
);

add_task(
  async function test_ProgressListener_waitForExplicitStart_suppressedStartStateErrorPage() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;
    await webProgress.sendStartState({ uri: PREVIOUS_LOAD_URI });

    const progressListener = new ProgressListener(webProgress, {
      expectNavigation: true,
      waitForExplicitStart: true,
    });
    const navigated = progressListener.start();

    await webProgress.sendStopState({
      errorPageURI: BLOCKED_PAGE_URI,
      flag: Cr.NS_ERROR_PHISHING_URI,
      loadType: LOAD_FLAG_ERROR_PAGE,
      uri: TARGET_URI,
    });
    ok(
      await hasPromiseRejected(navigated),
      "Listener has rejected for the error page"
    );

    const error = await getRejectionReason(navigated);
    ok(error.isNavigationError, "Rejected with a NavigationError");
    equal(error.message, "NS_ERROR_PHISHING_URI", "Expected error name is set");
  }
);

add_task(async function test_ProgressListener_start_waitForExplicitStart() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  const progressListener = new ProgressListener(webProgress, {
    waitForExplicitStart: true,
  });
  let navigated = progressListener.start();
  progressListener.stop();
  await navigated;

  await webProgress.sendStartState();

  // Override the option to accept the already started navigation.
  navigated = progressListener.start(undefined, {
    waitForExplicitStart: false,
  });
  ok(
    !(await hasPromiseResolved(navigated)),
    "Listener has not resolved while the navigation is in progress"
  );

  await webProgress.sendStopState();
  ok(
    await hasPromiseResolved(navigated),
    "Listener resolved after finishing the already started navigation"
  );
});

add_task(async function test_invalid_resolveWhenCommitted() {
  // Create a webprogress and start it before creating the progress listener.
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;
  await webProgress.sendStartState();

  Assert.throws(
    () =>
      new ProgressListener(webProgress, {
        resolveWhenCommitted: true,
        resolveWhenStarted: true,
        navigationManager: {},
      }),
    /Cannot use both resolveWhenStarted and resolveWhenCommitted/,
    "Expected error was returned"
  );

  Assert.throws(
    () =>
      new ProgressListener(webProgress, {
        resolveWhenCommitted: true,
        navigationManager: null,
      }),
    /Cannot use resolveWhenCommitted without a navigationManager/,
    "Expected error was returned"
  );
});

class MockNavigationManager extends EventEmitter {}

add_task(async function test_ProgressListener_resolveWhenCommitted() {
  // Create a webprogress and start it before creating the progress listener.
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;
  await webProgress.sendStartState();
  const mockNavigationManager = new MockNavigationManager();

  // Create the progress listener for a webprogress already in a navigation.
  const progressListener = new ProgressListener(webProgress, {
    resolveWhenCommitted: true,
    navigationManager: mockNavigationManager,
  });

  // Setup two navigation ids, and start the progress listener with the first
  // one.
  const navigationId1 = "navigationId1";
  const navigationId2 = "navigationId2";

  const navigated = progressListener.start(navigationId1);

  // Start a new navigation
  await webProgress.sendStartState();
  ok(
    !(await hasPromiseResolved(navigated)),
    "Listener has not resolved after navigation has only started"
  );

  // Emit an unexpected navigation-committed for the other navigation id.
  mockNavigationManager.emit("navigation-committed", {
    navigationId: navigationId2,
    url: TARGET_URI_FROM_NAVIGATION_COMMITTED.spec,
  });
  ok(
    !(await hasPromiseResolved(navigated)),
    "Listener has not resolved after an unexpected navigation-committed"
  );
  notEqual(
    progressListener.targetURI.spec,
    TARGET_URI_FROM_NAVIGATION_COMMITTED.spec,
    "Expected target URI has not been set from unexpected navigation-committed"
  );

  // Emit the expected navigation-committed event.
  mockNavigationManager.emit("navigation-committed", {
    navigationId: navigationId1,
    url: TARGET_URI_FROM_NAVIGATION_COMMITTED.spec,
  });
  ok(
    await hasPromiseResolved(navigated),
    "Listener has resolved after receiving the correct navigation-committed"
  );
  equal(
    progressListener.targetURI.spec,
    TARGET_URI_FROM_NAVIGATION_COMMITTED.spec,
    "Expected target URI has been set from navigation-committed"
  );
});

add_task(
  async function test_ProgressListener_waitForExplicitStartAndResolveWhenStarted() {
    // Create a webprogress and start it before creating the progress listener.
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;
    await webProgress.sendStartState();

    // Create the progress listener for a webprogress already in a navigation.
    const progressListener = new ProgressListener(webProgress, {
      resolveWhenStarted: true,
      waitForExplicitStart: true,
    });
    const navigated = progressListener.start();

    // Send stop state to complete the initial navigation
    await webProgress.sendStopState();
    ok(
      !(await hasPromiseResolved(navigated)),
      "Listener has not resolved after initial navigation"
    );

    // Start a new navigation
    await webProgress.sendStartState();
    ok(
      await hasPromiseResolved(navigated),
      "Listener resolved after starting the new navigation"
    );
  }
);

add_task(
  async function test_ProgressListener_resolveWhenNavigatingInsideDocument() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    const progressListener = new ProgressListener(webProgress);
    const navigated = progressListener.start();

    ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved");

    // Send hash change location change notification to complete the navigation
    await webProgress.sendLocationChange({
      flag: Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT,
    });

    ok(await hasPromiseResolved(navigated), "Listener has resolved");

    const { currentURI, targetURI } = progressListener;
    equal(
      currentURI.spec,
      TARGET_URI_WITH_HASH.spec,
      "Expected current URI has been set"
    );
    equal(
      targetURI.spec,
      TARGET_URI_WITH_HASH.spec,
      "Expected target URI has been set"
    );
  }
);

add_task(async function test_ProgressListener_ignoreCacheError() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  const progressListener = new ProgressListener(webProgress);
  const navigated = progressListener.start();

  ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved");

  await webProgress.sendStartState();
  await webProgress.sendStopState({
    flag: Cr.NS_ERROR_PARSED_DATA_CACHED,
  });

  ok(await hasPromiseResolved(navigated), "Listener has resolved");
});

add_task(async function test_ProgressListener_navigationRejectedOnErrorPage() {
  const testCases = [
    {
      description: "error page",
      flag: Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE,
      expectedErrorName: "customErrorMessage",
    },
    {
      description: "blocked page",
      errorPageURI: BLOCKED_PAGE_URI,
      flag: Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE,
      expectedErrorName: "deceptiveBlocked",
    },
    {
      // For a same-document navigation the document URI is not replaced by an
      // error page URI, and no error name can be extracted from it.
      description: "same-document error page",
      flag:
        Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT |
        Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE,
      expectedErrorName: "Address rejected",
    },
  ];

  for (const {
    description,
    errorPageURI,
    flag,
    expectedErrorName,
  } of testCases) {
    info(`Checking location change for ${description}`);

    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    const progressListener = new ProgressListener(webProgress, {
      waitForExplicitStart: false,
    });
    const navigated = progressListener.start();

    await webProgress.sendStartState();
    await webProgress.sendLocationChange({ errorPageURI, flag });

    ok(
      await hasPromiseRejected(navigated),
      "Listener has rejected in location change for error page"
    );

    const error = await getRejectionReason(navigated);
    ok(error.isNavigationError, "Rejected with a NavigationError");
    equal(error.message, expectedErrorName, "Expected error name is set");
    ok(!error.isBindingAborted, "Error is not reported as aborted");
    ok(!error.isCertError, "Error is not reported as a certificate error");
  }
});

add_task(
  async function test_ProgressListener_navigationRejectedOnStopStateErrorPage() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    const progressListener = new ProgressListener(webProgress, {
      waitForExplicitStart: false,
    });
    const navigated = progressListener.start();

    await webProgress.sendStartState();
    await webProgress.sendStopState({
      flag: Cr.NS_ERROR_MALWARE_URI,
      loadType: LOAD_FLAG_ERROR_PAGE,
    });

    ok(
      await hasPromiseRejected(navigated),
      "Listener has rejected in stop state for erroneous navigation"
    );

    const error = await getRejectionReason(navigated);
    ok(error.isNavigationError, "Rejected with a NavigationError");
    equal(
      error.message,
      "NS_ERROR_MALWARE_URI",
      "Error name from the stop state was kept"
    );
    ok(!error.isBindingAborted, "Error is not reported as aborted");
    ok(!error.isCertError, "Error is not reported as a certificate error");
  }
);

add_task(
  async function test_ProgressListener_navigationRejectedOnStopStateCertErrorPage() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    const progressListener = new ProgressListener(webProgress, {
      waitForExplicitStart: false,
    });
    const navigated = progressListener.start();

    await webProgress.sendStartState();
    await webProgress.sendStopState({
      flag: SEC_ERROR_EXPIRED_CERTIFICATE,
      loadType: LOAD_FLAG_ERROR_PAGE,
    });

    ok(
      await hasPromiseRejected(navigated),
      "Listener has rejected in stop state for erroneous navigation"
    );

    const error = await getRejectionReason(navigated);
    ok(error.isNavigationError, "Rejected with a NavigationError");
    ok(error.isCertError, "Error is reported as a certificate error");
    ok(!error.isBindingAborted, "Error is not reported as aborted");
  }
);

add_task(
  async function test_ProgressListener_navigationRejectedOnStopStateAndAborted() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;

    for (const flag of [Cr.NS_BINDING_ABORTED, Cr.NS_ERROR_ABORT]) {
      const progressListener = new ProgressListener(webProgress, {
        waitForExplicitStart: false,
      });
      const navigated = progressListener.start();

      await webProgress.sendStartState();
      await webProgress.sendStopState({ flag });

      ok(
        await hasPromiseRejected(navigated),
        "Listener has rejected in stop state for erroneous navigation"
      );

      const error = await getRejectionReason(navigated);
      ok(error.isNavigationError, "Rejected with a NavigationError");
      equal(
        error.isBindingAborted,
        flag === Cr.NS_BINDING_ABORTED,
        "Expected isBindingAborted value"
      );
    }
  }
);

add_task(
  async function test_ProgressListener_navigationRejectedWaitsForNavigationCommitted() {
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;
    const mockNavigationManager = new MockNavigationManager();

    const progressListener = new ProgressListener(webProgress, {
      navigationManager: mockNavigationManager,
    });

    const navigationId = "navigationId1";
    const navigated = progressListener.start(navigationId);

    await webProgress.sendStartState();

    // Emit a first navigation-committed before the location change
    mockNavigationManager.emit("navigation-committed", {
      navigationId,
      url: TARGET_URI.spec,
    });

    await webProgress.sendStopState({
      flag: Cr.NS_ERROR_MALWARE_URI,
      loadType: LOAD_FLAG_ERROR_PAGE,
    });

    ok(
      !(await hasPromiseRejected(navigated)),
      "Listener waiting for navigation-committed event before rejecting"
    );

    mockNavigationManager.emit("navigation-committed", {
      navigationId,
      url: TARGET_URI_ERROR_PAGE.spec,
    });

    ok(
      await hasPromiseRejected(navigated),
      "Listener rejected after receiving navigation-committed for error page"
    );
  }
);

add_task(async function test_ProgressListener_stopIfStarted() {
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;

  const progressListener = new ProgressListener(webProgress);
  const navigated = progressListener.start();

  progressListener.stopIfStarted();
  ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved");

  await webProgress.sendStartState();
  progressListener.stopIfStarted();
  ok(await hasPromiseResolved(navigated), "Listener has resolved");
});

add_task(async function test_ProgressListener_stopIfStarted_alreadyStarted() {
  // Create an already navigating browsing context.
  const browsingContext = new MockTopContext();
  const webProgress = browsingContext.webProgress;
  await webProgress.sendStartState();

  // Create a progress listener which accepts already ongoing navigations.
  const progressListener = new ProgressListener(webProgress, {
    waitForExplicitStart: false,
  });
  const navigated = progressListener.start();

  // stopIfStarted should stop the listener because of the ongoing navigation.
  progressListener.stopIfStarted();
  ok(await hasPromiseResolved(navigated), "Listener has resolved");
});

add_task(
  async function test_ProgressListener_stopIfStarted_alreadyStarted_waitForExplicitStart() {
    // Create an already navigating browsing context.
    const browsingContext = new MockTopContext();
    const webProgress = browsingContext.webProgress;
    await webProgress.sendStartState();

    // Create a progress listener which rejects already ongoing navigations.
    const progressListener = new ProgressListener(webProgress, {
      waitForExplicitStart: true,
    });
    const navigated = progressListener.start();

    // stopIfStarted will not stop the listener for the existing navigation.
    progressListener.stopIfStarted();
    ok(!(await hasPromiseResolved(navigated)), "Listener has not resolved");

    // stopIfStarted will stop the listener when called after starting a new
    // navigation.
    await webProgress.sendStartState();
    progressListener.stopIfStarted();
    ok(await hasPromiseResolved(navigated), "Listener has resolved");
  }
);
