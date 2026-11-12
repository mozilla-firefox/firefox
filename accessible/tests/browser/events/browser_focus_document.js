/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that switching from a remote document to a non-remote document fires
 * focus correctly.
 */
addAccessibleTask(``, async function testRemoteThenLocal(browser) {
  info("Loading about:support into same tab");
  let focused = waitForEvent(EVENT_FOCUS, event => {
    const acc = event.accessible;
    acc.QueryInterface(nsIAccessibleDocument);
    return acc.URL == "about:support";
  });
  browser.loadURI(Services.io.newURI("about:support"), {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
  await focused;
});

/**
 * Test that switching from a non-remote document to a remote document fires
 * focus correctly.
 */
addAccessibleTask(
  ``,
  async function testLocalThenRemote(browser) {
    info("Loading example.com into same tab");
    let focused = waitForEvent(EVENT_FOCUS, event => {
      const acc = event.accessible;
      acc.QueryInterface(nsIAccessibleDocument);
      return acc.URL == "https://example.com/";
    });
    // The accessibility test harness removes maychangeremoteness when we run a
    // chrome test, but we explicitly want to change remoteness now.
    browser.setAttribute("maychangeremoteness", "true");
    browser.loadURI(Services.io.newURI("https://example.com/"), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
    await focused;
    is(Services.focus.focusedElement, browser, "<browser> should be focused");
    is(
      Services.focus.focusedWindow,
      window,
      "window should be properly focused"
    );
  },
  { chrome: true, topLevel: false }
);

/**
 * Test that switching from a non-remote document to a remote document fires
 * focus correctly.
 */
addAccessibleTask(
  ``,
  async function testLocalThenRemoteWithAutofocus(browser) {
    info("Loading example.com with autofocus into same tab");
    // The accessibility test harness removes maychangeremoteness when we run a
    // chrome test, but we explicitly want to change remoteness now.
    browser.setAttribute("maychangeremoteness", "true");
    const url =
      "https://example.com/document-builder.sjs?html=" +
      encodeURIComponent(`<!doctype html><input autofocus>`);
    let focused = waitForEvent(
      EVENT_FOCUS,
      event => event.accessible.role == nsIAccessibleRole.ROLE_ENTRY
    );
    browser.loadURI(Services.io.newURI(url), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
    await focused;
    // In the content process, the input is focused. In the parent process
    // the focused element should be the browser element that contains the
    // example.com (remote) doc.
    is(Services.focus.focusedElement, browser, "<browser> should be focused");
    is(
      Services.focus.focusedWindow,
      window,
      "window should be properly focused"
    );
    await SpecialPowers.spawn(browser, [], async () => {
      is(
        content.windowUtils.IMEStatus,
        content.windowUtils.IME_STATUS_ENABLED,
        "IME should be enabled"
      );
    });
  },
  { chrome: true, topLevel: false }
);
