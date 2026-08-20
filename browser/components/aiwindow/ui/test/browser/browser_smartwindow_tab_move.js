/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);

async function addSmartTab(win) {
  const tab = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  return tab;
}

async function simulateActiveConversation(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    aiWindowElement.classList.add("chat-active");
  });
}

async function adoptTabToWindow(tab, targetWin) {
  const tabOpenPromise = BrowserTestUtils.waitForEvent(
    targetWin.gBrowser.tabContainer,
    "TabOpen"
  );
  targetWin.gBrowser.adoptTab(tab);
  const { target: adoptedTab } = await tabOpenPromise;
  return adoptedTab;
}

async function assertToggleButtonVisibility(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(() => {
      const el = content.document.querySelector("ai-window");
      const btn = el?.shadowRoot?.querySelector("#smartbar-toggle-button");
      return btn && !btn.hidden;
    }, "Toggle button should be visible");
  });
}

async function assertSmartBarVisibility(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(() => {
      const el = content.document.querySelector("ai-window");
      const smartbar = el?.shadowRoot?.querySelector("#ai-window-smartbar");
      return smartbar && !smartbar.hidden;
    }, "Smartbar should be visible");
  });
}

// Drag a tab out of a smart window to detach it into a new window.
// The detached window should still be a smart window.
add_task(async function test_smartwindow_detach_tab() {
  let smartWin, detachedWin;
  registerCleanupFunction(async () => {
    if (detachedWin && !detachedWin.closed) {
      await BrowserTestUtils.closeWindow(detachedWin);
    }
    if (smartWin && !smartWin.closed) {
      await BrowserTestUtils.closeWindow(smartWin);
    }
  });
  smartWin = await openAIWindow();
  const tab1 = BrowserTestUtils.addTab(
    smartWin.gBrowser,
    "https://example.com"
  );
  await BrowserTestUtils.browserLoaded(tab1.linkedBrowser);
  const tab2 = BrowserTestUtils.addTab(smartWin.gBrowser, "about:newtab");
  await BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
  await BrowserTestUtils.switchTab(smartWin.gBrowser, tab1);

  let prevBrowser = tab1.linkedBrowser;

  let delayedStartupPromise = BrowserTestUtils.waitForNewWindow();
  detachedWin = smartWin.gBrowser.replaceTabsWithWindow(tab1);
  await delayedStartupPromise;

  ok(
    !prevBrowser.frameLoader,
    "The swapped-from browser's frameloader has been destroyed"
  );

  Assert.ok(
    AIWindow.isAIWindowActive(detachedWin),
    "The detached window should still be a smart window"
  );

  let detachedBrowser = detachedWin.gBrowser;
  is(smartWin.gBrowser.visibleTabs.length, 2, "Two tabs now in the old window");
  is(detachedBrowser.visibleTabs.length, 1, "One tab in the detached window");

  let detachedTab = detachedBrowser.visibleTabs[0];
  await TabStateFlusher.flush(detachedTab.linkedBrowser);

  await BrowserTestUtils.closeWindow(detachedWin);
  await BrowserTestUtils.closeWindow(smartWin);
});

// Move a smart tab without conversation to a classic window.
// The tab should be redirected to the classic new tab page.
add_task(async function test_smarttab_no_conversation_becomes_classic() {
  let classicWin, smartWin;
  registerCleanupFunction(async () => {
    if (smartWin && !smartWin.closed) {
      await BrowserTestUtils.closeWindow(smartWin);
    }
    if (classicWin && !classicWin.closed) {
      await BrowserTestUtils.closeWindow(classicWin);
    }
  });
  classicWin = await BrowserTestUtils.openNewBrowserWindow({
    aiWindow: false,
  });
  smartWin = await openAIWindow();
  const smartTab = await addSmartTab(smartWin);
  const adoptedTab = await adoptTabToWindow(smartTab, classicWin);

  Assert.ok(
    !AIWindow.isAIWindowActive(classicWin),
    "Classic window should remain classic after adopting a smart tab"
  );

  await TestUtils.waitForCondition(
    () => adoptedTab.linkedBrowser.currentURI.spec === "about:newtab"
  );

  Assert.equal(
    adoptedTab.linkedBrowser.currentURI.spec,
    "about:newtab",
    "Smart tab without conversation should redirect to classic new tab"
  );

  await BrowserTestUtils.closeWindow(smartWin);
  await BrowserTestUtils.closeWindow(classicWin);
});

// Move a smart tab with an active conversation to a classic window.
// The tab should keep its content and show the toggle button.
add_task(async function test_smarttab_with_conversation_becomes_classic() {
  let classicWin, smartWin;
  registerCleanupFunction(async () => {
    if (smartWin && !smartWin.closed) {
      await BrowserTestUtils.closeWindow(smartWin);
    }
    if (classicWin && !classicWin.closed) {
      await BrowserTestUtils.closeWindow(classicWin);
    }
  });
  classicWin = await BrowserTestUtils.openNewBrowserWindow({
    aiWindow: false,
  });
  smartWin = await openAIWindow();
  const smartTab = await addSmartTab(smartWin);
  await BrowserTestUtils.switchTab(smartWin.gBrowser, smartTab);
  await simulateActiveConversation(smartTab);

  const adoptedTab = await adoptTabToWindow(smartTab, classicWin);

  Assert.ok(
    !AIWindow.isAIWindowActive(classicWin),
    "Classic window should remain classic after adopting a smart tab with conversation"
  );

  Assert.equal(
    adoptedTab.linkedBrowser.currentURI.spec,
    AIWINDOW_URL,
    "Smart tab with conversation should keep its content"
  );

  await assertToggleButtonVisibility(adoptedTab);

  // Move the tab back to the smart window
  const reAdoptedTab = await adoptTabToWindow(adoptedTab, smartWin);

  Assert.ok(
    AIWindow.isAIWindowActive(smartWin),
    "Smart window should still be a smart window after re-adopting the tab"
  );

  await assertSmartBarVisibility(reAdoptedTab);

  Assert.equal(
    reAdoptedTab.linkedBrowser.currentURI.spec,
    AIWINDOW_URL,
    "Re-adopted tab should keep its AI window content"
  );

  // waiting for focus stabilizes flaky test here
  await SimpleTest.promiseFocus(smartWin);

  // Verify the CTA button is functional after re-adoption, testing that the
  // actor connection and SmartbarInput currentPage guard are both working.
  await typeInSmartbar(reAdoptedTab.linkedBrowser, "hello");

  await SpecialPowers.spawn(reAdoptedTab.linkedBrowser, [], async () => {
    const el = content.document.querySelector("ai-window");
    const smartbar = el.shadowRoot.querySelector("#ai-window-smartbar");
    await ContentTaskUtils.waitForCondition(
      () => smartbar.smartbarAction === "chat",
      "CTA button should show 'chat' action after typing"
    );
  });

  await BrowserTestUtils.closeWindow(smartWin);
  await BrowserTestUtils.closeWindow(classicWin);
});

// Drag a smart tab with an active conversation to a classic window and back to a smart window.
// The conversation should be restored in the re-adopted smart tab.
add_task(async function test_smarttab_conversation_restored_after_drag() {
  let classicWin, smartWin;
  const sb = sinon.createSandbox();

  registerCleanupFunction(async () => {
    sb.restore();
    if (classicWin && !classicWin.closed) {
      await BrowserTestUtils.closeWindow(classicWin);
    }
    if (smartWin && !smartWin.closed) {
      await BrowserTestUtils.closeWindow(smartWin);
    }
  });

  const fetchWithHistory = Promise.withResolvers();
  sb.stub(Chat, "fetchWithHistory").callsFake(({ conversation }) => {
    fetchWithHistory.resolve(ChatStore.updateConversation(conversation));
    return fetchWithHistory.promise;
  });
  sb.stub(openAIEngine, "build").resolves({});
  sb.stub(AIWindowAccountAuth, "ensureAIWindowAccess").resolves(true);

  smartWin = await openAIWindow();
  const smartTab = await addSmartTab(smartWin);
  await BrowserTestUtils.switchTab(smartWin.gBrowser, smartTab);

  await typeInSmartbar(smartTab.linkedBrowser, "hello");
  await submitSmartbar(smartTab.linkedBrowser);

  await fetchWithHistory.promise;

  const conversationId = smartTab.linkedBrowser.getAttribute(
    "data-conversation-id"
  );
  Assert.ok(
    conversationId,
    "Tab should have a conversation ID after submitting"
  );

  classicWin = await BrowserTestUtils.openNewBrowserWindow({ aiWindow: false });
  const adoptedTab = await adoptTabToWindow(smartTab, classicWin);

  const conversationRestoredPromise = BrowserTestUtils.waitForEvent(
    smartWin,
    "ai-window:opened-conversation"
  );
  await adoptTabToWindow(adoptedTab, smartWin);
  const restoredEvent = await conversationRestoredPromise;
  Assert.equal(
    restoredEvent.detail.conversationId,
    conversationId,
    "Re-adopted tab should restore the original conversation"
  );
  Assert.ok(
    restoredEvent.detail.conversation.messages.some(
      m => m.content?.body === "hello"
    ),
    "Restored conversation should contain the submitted 'hello' message"
  );
});

// Move a smart tab with an active conversation to a smart window and then a classic window.
// The tab should keep its content and show the toggle button in the classic window.
add_task(
  async function test_smarttab_with_conversation_becomes_smart_and_classic() {
    let smartWin, smartWin2, classicWin;
    registerCleanupFunction(async () => {
      if (smartWin && !smartWin.closed) {
        await BrowserTestUtils.closeWindow(smartWin);
      }
      if (smartWin2 && !smartWin2.closed) {
        await BrowserTestUtils.closeWindow(smartWin2);
      }
      if (classicWin && !classicWin.closed) {
        await BrowserTestUtils.closeWindow(classicWin);
      }
    });
    smartWin = await openAIWindow();
    smartWin2 = await openAIWindow();
    const smartTab = await addSmartTab(smartWin);
    await BrowserTestUtils.switchTab(smartWin.gBrowser, smartTab);
    await simulateActiveConversation(smartTab);

    const adoptedTab = await adoptTabToWindow(smartTab, smartWin2);

    Assert.equal(
      adoptedTab.linkedBrowser.currentURI.spec,
      AIWINDOW_URL,
      "Smart tab should continue being a smart tab when adopted by a smart window"
    );

    classicWin = await BrowserTestUtils.openNewBrowserWindow({
      aiWindow: false,
    });
    const adoptedTab2 = await adoptTabToWindow(adoptedTab, classicWin);

    Assert.ok(
      !AIWindow.isAIWindowActive(classicWin),
      "Classic window should remain classic after adopting a smart tab with conversation"
    );

    Assert.equal(
      adoptedTab2.linkedBrowser.currentURI.spec,
      AIWINDOW_URL,
      "Smart tab with conversation should keep its content"
    );

    await BrowserTestUtils.closeWindow(smartWin);
    await BrowserTestUtils.closeWindow(smartWin2);
    await BrowserTestUtils.closeWindow(classicWin);
  }
);
