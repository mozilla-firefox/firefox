/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

const gDOMWindowUtils = EventUtils._getDOMWindowUtils(window);

const kURL =
  "https://example.com/browser/toolkit/content/tests/browser/file_empty.html";

const kIframeHTML = `<div id="div1" tabindex="0">div1</div>
<div id="div2" tabindex="0">div2</div>
<input id="text">`;

registerCleanupFunction(async () => {
  if (Services.profiler?.IsActive()) {
    await Services.profiler.StopProfiler();
  }
});

function promiseIMEStateSetByRemote(aIMEStatus) {
  return TestUtils.waitForCondition(
    () =>
      gDOMWindowUtils.IMEStatus === aIMEStatus &&
      gDOMWindowUtils.inputContextOrigin ===
        Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_CONTENT,
    `Waiting for IME status ${aIMEStatus} set by the remote process`
  );
}

/**
 * Build the test document in aBrowser, then focus #button in it.
 *
 * @param {Element} aBrowser The remote browser.
 */
async function setUpRemoteContent(aBrowser) {
  ok(aBrowser.isRemoteBrowser, "This test passes only in e10s mode");
  await SpecialPowers.spawn(aBrowser, [], () => {
    content.document.body.innerHTML = `
      <input id="text">
      <button id="button">button</button>
      <select id="select"><option>option</option></select>
      <a id="link" href="#">link</a>
      <div id="div" tabindex="0">div</div>
      <input id="checkbox" type="checkbox">
      <input id="checkbox2" type="checkbox">
      <input id="readonly" type="url" readonly value="https://example.com/">
      <form>
        <input id="checkboxInForm" type="checkbox">
        <button type="submit">submit</button>
      </form>`;
    content.window.focus();
    content.document.getElementById("text").focus();
  });
  await promiseIMEStateSetByRemote(Ci.nsIDOMWindowUtils.IME_STATUS_ENABLED);
  await focusElementInContent(aBrowser, "button");
  await promiseIMEStateSetByRemote(Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED);
}

/**
 * Append an <iframe srcdoc> which has #div1, #div2 and #text to the document
 * in aBrowser.  The <iframe> is put first so that it is visible for clicks.
 *
 * @param {Element} aBrowser The remote browser.
 * @returns {Promise<BrowsingContext>} The browsing context of the <iframe>.
 */
function addIframeInRemoteContent(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [kIframeHTML], async aHTML => {
    const iframe = content.document.createElement("iframe");
    iframe.srcdoc = aHTML;
    const loaded = new Promise(resolve =>
      iframe.addEventListener("load", resolve, { once: true })
    );
    content.document.body.prepend(iframe);
    await loaded;
    return iframe.browsingContext;
  });
}

/**
 * Focus the element whose id is aId, or blur the focused element if aId is
 * null, then wait for a couple of animation frames so that IME state updates
 * deferred to a later refresh are counted too.
 *
 * @param {Element|BrowsingContext} aTarget Where the element is.
 * @param {string|null} aId The id of the element to focus.
 */
function focusElementInContent(aTarget, aId) {
  return SpecialPowers.spawn(aTarget, [aId], async id => {
    if (id) {
      content.document.getElementById(id).focus();
    } else {
      Assert.notEqual(
        content.document.activeElement,
        content.document.body,
        "An element should have focus before blurring it"
      );
      content.document.activeElement.blur();
    }
    Assert.ok(content.document.hasFocus(), "The document should have focus");
    Assert.equal(
      content.document.activeElement.id,
      id ?? "",
      id ? `#${id} should have focus` : "No element should have focus"
    );
    await new Promise(resolve =>
      content.requestAnimationFrame(() =>
        content.requestAnimationFrame(resolve)
      )
    );
  });
}

/**
 * Click the element whose id is aId, then wait for a couple of animation frames
 * like focusElementInContent().
 *
 * @param {BrowsingContext} aBrowsingContext Where the element is.
 * @param {string} aId The id of the element to click.
 */
function clickElementInContent(aBrowsingContext, aId) {
  return SpecialPowers.spawn(aBrowsingContext, [aId], async id => {
    const rect = content.document.getElementById(id).getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;
    let mouseDownTarget = null;
    const onMouseDown = event => {
      mouseDownTarget = event.target;
    };
    content.document.addEventListener("mousedown", onMouseDown, {
      capture: true,
    });
    content.synthesizeMouseEvent("mousedown", x, y);
    content.synthesizeMouseEvent("mouseup", x, y);
    content.document.removeEventListener("mousedown", onMouseDown, {
      capture: true,
    });
    Assert.equal(
      mouseDownTarget?.id,
      id,
      `#${id} should receive the mousedown event`
    );
    Assert.ok(content.document.hasFocus(), "The document should have focus");
    Assert.equal(
      content.document.activeElement.id,
      id,
      `#${id} should have focus after a click`
    );
    await new Promise(resolve =>
      content.requestAnimationFrame(() =>
        content.requestAnimationFrame(resolve)
      )
    );
  });
}

/**
 * Check that the document in aBrowser does not have focus, or wait for it to
 * get focus with #button focused if aHasFocus is true.
 *
 * @param {Element} aBrowser The remote browser.
 * @param {boolean} aHasFocus Whether the document should have focus.
 */
function checkFocusInContent(aBrowser, aHasFocus) {
  return SpecialPowers.spawn(aBrowser, [aHasFocus], async hasFocus => {
    if (!hasFocus) {
      Assert.ok(
        !content.document.hasFocus(),
        "The document should not have focus"
      );
      return;
    }
    await ContentTaskUtils.waitForCondition(
      () => content.document.hasFocus(),
      "Waiting for the document to get focus"
    );
    Assert.equal(
      content.document.activeElement.id,
      "button",
      "#button should get focus back"
    );
  });
}

/**
 * Run aTask while profiling IPC messages, then return how many
 * PBrowser::SetInputContext messages the parent process received.  The
 * messages sent by the content process before it replies to a
 * SpecialPowers.spawn call in aTask are counted since they use the same IPC
 * channel as the reply.
 *
 * @param {Function} aTask An async function.
 * @returns {Promise<number>}
 */
async function countSetInputContextReceivedByParent(aTask) {
  await ProfilerTestUtils.startProfiler({
    entries: 1024 * 1024,
    features: ["nostacksampling", "ipcmessages"],
  });
  let profile;
  try {
    await aTask();
  } finally {
    profile = await ProfilerTestUtils.stopNowAndGetProfile();
  }
  return ProfilerTestUtils.getPayloadsOfType(profile.threads[0], "IPC").filter(
    payload =>
      /^PBrowser::(Msg_)?SetInputContext$/.test(payload.messageType) &&
      payload.direction == "receiving" &&
      payload.phase == "endpoint"
  ).length;
}

/**
 * Check whether the parent process receives SetInputContext while aMove runs.
 *
 * @param {object} aOptions
 * @param {string} aOptions.description Completes "SetInputContext ...".
 * @param {Function} aOptions.move An async function which moves focus.
 * @param {boolean} aOptions.expectSent Whether SetInputContext should be sent.
 * @param {Function} [aOptions.waitForParent] A condition which becomes true
 *   when the parent process receives the new input context.  It must be false
 *   before aOptions.move runs.
 */
async function testFocusMove({ description, move, expectSent, waitForParent }) {
  info(`Testing ${description}`);
  if (waitForParent) {
    ok(
      !waitForParent(),
      `The parent process should not have the new input context before the move ${description}`
    );
  }
  const count = await countSetInputContextReceivedByParent(async () => {
    await move();
    if (waitForParent) {
      await TestUtils.waitForCondition(
        waitForParent,
        `Waiting for the parent process to receive the input context ${description}`
      );
    }
  });
  if (expectSent) {
    Assert.greater(
      count,
      0,
      `The parent process should receive SetInputContext ${description}`
    );
  } else {
    is(
      count,
      0,
      `The parent process should not receive SetInputContext ${description}`
    );
  }
}

add_task(async function test_focus_move_between_non_editable_elements() {
  if (!Services.profiler) {
    ok(true, "This test requires the Gecko profiler");
    return;
  }
  await BrowserTestUtils.withNewTab(kURL, async browser => {
    await setUpRemoteContent(browser);
    for (const [id, description] of [
      ["select", "when focus moves from <button> to <select>"],
      ["link", "when focus moves from <select> to <a href>"],
      ["div", "when focus moves from <a href> to <div tabindex>"],
      ["button", "when focus moves from <div tabindex> to <button>"],
      [null, "when focus moves from <button> to the document"],
      ["button", "when focus moves from the document to <button>"],
    ]) {
      await testFocusMove({
        description,
        move: () => focusElementInContent(browser, id),
        expectSent: false,
      });
    }
    is(
      gDOMWindowUtils.IMEStatus,
      Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED,
      "IME should stay disabled after moving focus between non-editable elements"
    );
  });
});

add_task(async function test_focus_move_between_non_editable_inputs() {
  if (!Services.profiler) {
    ok(true, "This test requires the Gecko profiler");
    return;
  }
  await BrowserTestUtils.withNewTab(kURL, async browser => {
    await setUpRemoteContent(browser);
    for (const { id, description, waitForParent } of [
      {
        id: "checkbox",
        description:
          "when focus moves from <button> to <input type=checkbox> since its type differs",
        waitForParent: () => gDOMWindowUtils.focusedInputType === "checkbox",
      },
      {
        id: null,
        description:
          "when focus moves from <input type=checkbox> to the document since the document does not have the input type",
        waitForParent: () => gDOMWindowUtils.focusedInputType === "",
      },
      {
        id: "checkbox2",
        description:
          "when focus moves from the document to another <input type=checkbox> since its type differs",
        waitForParent: () => gDOMWindowUtils.focusedInputType === "checkbox",
      },
      {
        id: "checkboxInForm",
        description:
          "when focus moves from <input type=checkbox> to <input type=checkbox> in a <form> since its action hint differs",
        waitForParent: () => gDOMWindowUtils.focusedActionHint === "go",
      },
      {
        id: "readonly",
        description:
          "when focus moves from <input type=checkbox> in a <form> to <input type=url readonly> since its type differs",
        waitForParent: () => gDOMWindowUtils.focusedInputType === "url",
      },
      {
        id: "button",
        description:
          "when focus moves from <input type=url readonly> to <button> since its type differs",
        waitForParent: () => gDOMWindowUtils.focusedInputType === "",
      },
    ]) {
      await testFocusMove({
        description,
        move: () => focusElementInContent(browser, id),
        expectSent: true,
        waitForParent,
      });
    }
  });
});

add_task(async function test_focus_move_between_documents_whose_uri_differs() {
  if (!Services.profiler) {
    ok(true, "This test requires the Gecko profiler");
    return;
  }
  await BrowserTestUtils.withNewTab(kURL, async browser => {
    await setUpRemoteContent(browser);
    const iframeForURI = await addIframeInRemoteContent(browser);
    await testFocusMove({
      description:
        "when focus moves from <button> to <div tabindex> in an <iframe srcdoc> since the document URI differs",
      move: () => focusElementInContent(iframeForURI, "div1"),
      expectSent: true,
    });
    await testFocusMove({
      description:
        "when focus moves from <div tabindex> in an <iframe srcdoc> to <button> in the parent document since the document URI differs",
      move: () => focusElementInContent(browser, "button"),
      expectSent: true,
    });
  });
});

add_task(async function test_focus_move_before_and_after_user_input() {
  if (!Services.profiler) {
    ok(true, "This test requires the Gecko profiler");
    return;
  }
  await BrowserTestUtils.withNewTab(kURL, async browser => {
    await setUpRemoteContent(browser);

    info("Disabled element -> click -> another disabled element");
    const iframeForDisabled = await addIframeInRemoteContent(browser);
    await focusElementInContent(iframeForDisabled, "div1");
    await testFocusMove({
      description:
        "when the focused <div tabindex> in an <iframe srcdoc> is clicked",
      move: () => clickElementInContent(iframeForDisabled, "div1"),
      expectSent: false,
    });
    await testFocusMove({
      description:
        "when focus moves to another <div tabindex> after the first user input in the <iframe srcdoc>",
      move: () => focusElementInContent(iframeForDisabled, "div2"),
      expectSent: true,
    });
    await testFocusMove({
      description:
        "when focus moves back to the first <div tabindex> in the <iframe srcdoc> which has handled user input",
      move: () => focusElementInContent(iframeForDisabled, "div1"),
      expectSent: false,
    });

    info("Disabled element -> click -> enabled element");
    const iframeForEnabled = await addIframeInRemoteContent(browser);
    await focusElementInContent(iframeForEnabled, "div1");
    await testFocusMove({
      description:
        "when <input type=text> in an <iframe srcdoc> is clicked while <div tabindex> has focus",
      move: () => clickElementInContent(iframeForEnabled, "text"),
      expectSent: true,
      waitForParent: () =>
        gDOMWindowUtils.IMEStatus === Ci.nsIDOMWindowUtils.IME_STATUS_ENABLED &&
        gDOMWindowUtils.inputContextOrigin ===
          Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_CONTENT,
    });
    await testFocusMove({
      description:
        "when focus moves from <input type=text> to <div tabindex> in the <iframe srcdoc> which has handled user input",
      move: () => focusElementInContent(iframeForEnabled, "div1"),
      expectSent: true,
      waitForParent: () =>
        gDOMWindowUtils.IMEStatus ===
          Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED &&
        gDOMWindowUtils.inputContextOrigin ===
          Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_CONTENT,
    });
    await testFocusMove({
      description:
        "when focus moves between <div tabindex>s in the <iframe srcdoc> which has handled user input",
      move: () => focusElementInContent(iframeForEnabled, "div2"),
      expectSent: false,
    });

    info("Click -> disabled element -> another disabled element");
    const iframeForClick = await addIframeInRemoteContent(browser);
    await focusElementInContent(iframeForClick, "div2");
    await testFocusMove({
      description:
        "when <div tabindex> in an <iframe srcdoc> is clicked for the first user input in it",
      move: () => clickElementInContent(iframeForClick, "div1"),
      expectSent: true,
    });
    await testFocusMove({
      description:
        "when focus moves from the clicked <div tabindex> to another <div tabindex> in the <iframe srcdoc>",
      move: () => focusElementInContent(iframeForClick, "div2"),
      expectSent: false,
    });
    await testFocusMove({
      description:
        "when focus moves from <div tabindex> to the document of the <iframe srcdoc> which has handled user input",
      move: () => focusElementInContent(iframeForClick, null),
      expectSent: false,
    });
  });
});

add_task(async function test_focus_coming_back_to_remote_content() {
  if (!Services.profiler) {
    ok(true, "This test requires the Gecko profiler");
    return;
  }
  await BrowserTestUtils.withNewTab(kURL, async browser => {
    await setUpRemoteContent(browser);
    const tab = gBrowser.getTabForBrowser(browser);

    gURLBar.focus();
    await TestUtils.waitForCondition(
      () =>
        gDOMWindowUtils.IMEStatus === Ci.nsIDOMWindowUtils.IME_STATUS_ENABLED &&
        gDOMWindowUtils.inputContextOrigin ===
          Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_MAIN,
      "Waiting for IME to be enabled in the URL bar"
    );
    await checkFocusInContent(browser, false);
    await testFocusMove({
      description:
        "when focus comes back to <button> in the remote content from the URL bar",
      move: async () => {
        browser.focus();
        await checkFocusInContent(browser, true);
      },
      expectSent: true,
      waitForParent: () =>
        gDOMWindowUtils.IMEStatus ===
          Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED &&
        gDOMWindowUtils.inputContextOrigin ===
          Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_CONTENT,
    });

    const otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      kURL
    );
    try {
      await SpecialPowers.spawn(otherTab.linkedBrowser, [], () => {
        content.document.body.innerHTML = `<input id="text">`;
        content.window.focus();
        content.document.getElementById("text").focus();
      });
      await promiseIMEStateSetByRemote(Ci.nsIDOMWindowUtils.IME_STATUS_ENABLED);
      await checkFocusInContent(browser, false);
      await testFocusMove({
        description:
          "when the tab whose <button> has focus is selected again after another tab whose <input type=text> has focus",
        move: async () => {
          await BrowserTestUtils.switchTab(gBrowser, tab);
          await checkFocusInContent(browser, true);
        },
        expectSent: true,
        waitForParent: () =>
          gDOMWindowUtils.IMEStatus ===
            Ci.nsIDOMWindowUtils.IME_STATUS_DISABLED &&
          gDOMWindowUtils.inputContextOrigin ===
            Ci.nsIDOMWindowUtils.INPUT_CONTEXT_ORIGIN_CONTENT,
      });
    } finally {
      BrowserTestUtils.removeTab(otherTab);
    }
  });
});
