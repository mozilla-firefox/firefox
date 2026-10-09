/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/*
 * Tests for link opening from <ai-chat-message> through AIChatContentParent.
 *
 * Exercises the full click-to-tab flow: user clicks a rendered markdown link
 * inside the <ai-chat-message> shadow DOM -> click handler dispatches
 * AIChatContent:OpenLink -> AIChatContentChild forwards to parent ->
 * AIChatContentParent#handleOpenLink uses BrowserUtils.whereToOpenLink to
 * determine where to open the link, switching to an existing tab or navigating
 * the current tab. Non-http(s) and empty URLs are silently ignored.
 */

const TEST_URL = "https://example.com/";
let chatTab;
let existingTab;
let containerTab;
let win;
let sidebarBrowser;

/**
 * Creates an <ai-chat-message> with a markdown link, waits for it to render,
 * and clicks the link. This exercises the real click handler inside the
 * component's shadow DOM.
 *
 * @param {MozBrowser|BrowsingContext} browserOrBC - Browser or BrowsingContext
 *   loaded with about:aichatcontent
 * @param {string} url - The URL to embed in the markdown link
 * @param {object} [eventOptions] - Mouse event options. Supports `accelKey`
 *   (maps to metaKey+ctrlKey for cross-platform accelerator behavior),
 *   plus any standard MouseEvent init dict properties.
 */
async function clickRenderedLink(browserOrBC, url, eventOptions = {}) {
  await SpecialPowers.spawn(
    browserOrBC,
    [url, eventOptions],
    async (linkUrl, evtOpts) => {
      if (content.document.readyState !== "complete") {
        await ContentTaskUtils.waitForEvent(content, "load");
      }
      await content.customElements.whenDefined("ai-chat-message");

      const doc = content.document;
      const el = doc.createElement("ai-chat-message");
      doc.body.appendChild(el);

      const elJS = el.wrappedJSObject || el;
      // `role` reflects to the `data-message-role` attribute: the bare `role`
      // attribute is the native ARIA role and collides with the reactive
      // property, so it's set via the data-* attribute instead.
      // TODO: rename the reactive property off `role` so callers can set it
      // directly (`elJS.role = "assistant"`).
      el.setAttribute("data-message-role", "assistant");
      elJS.trustedUrls = Cu.cloneInto([linkUrl], content);
      const md = `Click [here](${linkUrl}) for more`;
      elJS.message = md;
      el.setAttribute("message", md);

      await ContentTaskUtils.waitForCondition(() => {
        const msg = el.shadowRoot?.querySelector(".message-assistant");
        return msg?.querySelector("a[href]");
      }, "Rendered markdown link should appear in shadow DOM");

      const link = el.shadowRoot.querySelector(".message-assistant a[href]");

      const { accelKey = false, ...rest } = evtOpts;
      EventUtils.sendMouseEvent(
        {
          type: "click",
          metaKey: accelKey || !!rest.metaKey,
          ctrlKey: accelKey || !!rest.ctrlKey,
          shiftKey: !!rest.shiftKey,
          altKey: !!rest.altKey,
          button: rest.button ?? 0,
        },
        link,
        content
      );
      el.remove();
    }
  );
}

describe("aichat container tab behavior", () => {
  let mockEngineManager;

  beforeEach(async () => {
    mockEngineManager = new MockEngineManager();

    await SpecialPowers.pushPrefEnv({
      set: [["privacy.userContext.enabled", true]],
    });
    ({ win, sidebarBrowser } = await openAIWindowWithSidebar());
    containerTab = BrowserTestUtils.addTab(win.gBrowser, "about:blank", {
      userContextId: 1,
    });
    await BrowserTestUtils.browserLoaded(
      containerTab.linkedBrowser,
      false,
      "about:blank"
    );
    win.gBrowser.selectedTab = containerTab;
  });

  afterEach(async () => {
    mockEngineManager.rejectAllRequests();
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
    mockEngineManager.cleanupMocks();

    win = null;
    sidebarBrowser = null;
    containerTab = null;
    mockEngineManager = null;
  });

  it("should open link in the container of the selected tab", async () => {
    const newTabPromise = BrowserTestUtils.waitForNewTab(win.gBrowser);

    const innerBC = await SpecialPowers.spawn(sidebarBrowser, [], async () => {
      await content.customElements.whenDefined("ai-window");
      const aiWindow = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForCondition(() => {
        const chatBrowser =
          aiWindow.shadowRoot?.querySelector("#aichat-browser");
        return !!chatBrowser?.browsingContext;
      }, "Nested aichat-browser should exist");

      return aiWindow.shadowRoot.querySelector("#aichat-browser")
        .browsingContext;
    });

    if (innerBC.currentURI.spec !== "about:aichatcontent") {
      await BrowserTestUtils.browserLoaded(innerBC.embedderElement, {
        wantLoad: "about:aichatcontent",
      });
    }

    await clickRenderedLink(innerBC, TEST_URL, { accelKey: true });

    const newTab = await newTabPromise;
    Assert.equal(
      newTab.userContextId,
      1,
      "New tab should open in the same container as the selected tab"
    );
    BrowserTestUtils.removeTab(newTab);
  });
});

describe("aichat open link", () => {
  beforeEach(async () => {
    chatTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:aichatcontent"
    );
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(chatTab);
    chatTab = null;
  });

  describe("aichat link navigation behavior", () => {
    it("should navigate current tab if no existing tab has the URL", async () => {
      const initialTabCount = gBrowser.tabs.length;
      const loadPromise = BrowserTestUtils.browserLoaded(
        chatTab.linkedBrowser,
        false,
        TEST_URL
      );
      await clickRenderedLink(chatTab.linkedBrowser, TEST_URL);
      await loadPromise;

      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No new tab should be created"
      );
      Assert.equal(
        gBrowser.selectedTab,
        chatTab,
        "The chat tab should still be selected"
      );
      Assert.equal(
        chatTab.linkedBrowser.currentURI.spec,
        TEST_URL,
        "The current tab should navigate to the clicked URL"
      );
    });

    it("should switch to an already-open tab with the same URL", async () => {
      existingTab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        TEST_URL
      );
      gBrowser.selectedTab = chatTab;

      Assert.equal(
        gBrowser.selectedTab,
        chatTab,
        "Precondition: the chat tab is selected, not the existing one"
      );
      const initialTabCount = gBrowser.tabs.length;

      await clickRenderedLink(chatTab.linkedBrowser, TEST_URL);

      await TestUtils.waitForCondition(
        () => gBrowser.selectedTab === existingTab,
        "Browser should switch to the existing tab with the matching URL"
      );

      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No additional tab should be created"
      );

      BrowserTestUtils.removeTab(existingTab);
      existingTab = null;
    });
  });

  describe("aichat URL validation", () => {
    it("should block non-http(s) URLs and dispatch OpenLink event", async () => {
      const initialTabCount = gBrowser.tabs.length;

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        for (const url of [
          "javascript:alert(1)",
          "data:text/html,hi",
          "file:///etc/passwd",
        ]) {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url },
            })
          );
        }
      });

      // Give actor messaging a tick to deliver any would-be tab opens.
      await new Promise(r => Services.tm.dispatchToMainThread(r));

      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No tabs should be opened for non-http(s) URLs"
      );
    });

    it("should ignore empty URLs", async () => {
      const initialTabCount = gBrowser.tabs.length;

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        for (const url of ["", undefined]) {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url },
            })
          );
        }
      });

      await new Promise(r => Services.tm.dispatchToMainThread(r));

      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No tabs should be opened for empty/missing URLs"
      );
    });

    it("should open about:preferences links in a new tab", async () => {
      const newTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:preferences#manageMemories"
      );

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        content.document.dispatchEvent(
          new content.CustomEvent("AIChatContent:OpenLink", {
            bubbles: true,
            detail: { url: "about:preferences#manageMemories" },
          })
        );
      });

      const newTab = await newTabPromise;
      Assert.ok(newTab, "A new tab should open for about:preferences URLs");
      BrowserTestUtils.removeTab(newTab);
    });

    it("should open about:settings links in a new tab", async () => {
      const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        content.document.dispatchEvent(
          new content.CustomEvent("AIChatContent:OpenLink", {
            bubbles: true,
            detail: { url: "about:settings" },
          })
        );
      });

      const newTab = await newTabPromise;
      Assert.ok(newTab, "A new tab should open for about:settings URLs");
      BrowserTestUtils.removeTab(newTab);
    });

    it("should open about:smartwindowtasks links in a new tab", async () => {
      const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        content.document.dispatchEvent(
          new content.CustomEvent("AIChatContent:OpenLink", {
            bubbles: true,
            detail: { url: "about:smartwindowtasks" },
          })
        );
      });

      const newTab = await newTabPromise;
      Assert.ok(newTab, "A new tab should open for about:smartwindowtasks url");
      BrowserTestUtils.removeTab(newTab);
    });

    // TODO (bug 2071035): Replace the text link with the UI that lets users
    // choose whether to open the page in this tab or a new tab.
    it("should open about:smartpage links in a new tab", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.aitab.enabled", true]],
      });
      const url = "about:smartpage?page=dashboard";

      try {
        const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, url);
        await SpecialPowers.spawn(chatTab.linkedBrowser, [url], targetUrl => {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url: targetUrl },
            })
          );
        });

        const newTab = await newTabPromise;
        Assert.ok(newTab, "The smartpage opens in a new tab");
        BrowserTestUtils.removeTab(newTab);
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    });

    it("should switch to an existing about:preferences tab instead of opening a new one", async () => {
      const prefsTab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:preferences"
      );
      gBrowser.selectedTab = chatTab;
      const initialTabCount = gBrowser.tabs.length;

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        content.document.dispatchEvent(
          new content.CustomEvent("AIChatContent:OpenLink", {
            bubbles: true,
            detail: { url: "about:preferences" },
          })
        );
      });

      await TestUtils.waitForCondition(
        () => gBrowser.selectedTab === prefsTab,
        "Should switch to existing preferences tab"
      );
      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No new tab should open when preferences is already open"
      );
      BrowserTestUtils.removeTab(prefsTab);
    });

    it("should block other about: URLs", async () => {
      const initialTabCount = gBrowser.tabs.length;

      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        for (const url of [
          "about:config",
          "about:blank",
          "about:newtab",
          "about:smartpage",
          "about:smartpage?page=bad/path",
        ]) {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url },
            })
          );
        }
      });

      await new Promise(r => Services.tm.dispatchToMainThread(r));

      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No tabs should be opened for non-preferences about: URLs"
      );
    });
  });

  describe("aichat about:preferences link rendering", () => {
    it("should retain only seen about:smartpage links", async () => {
      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        await content.customElements.whenDefined("ai-chat-message");
        const url = "about:smartpage?page=dashboard";

        for (const seen of [false, true]) {
          const el = content.document.createElement("ai-chat-message");
          content.document.body.appendChild(el);
          el.setAttribute("data-message-role", "assistant");

          const message = el.wrappedJSObject || el;
          message.seenUrls = Cu.cloneInto(new Set(seen ? [url] : []), content);
          message.message = `View [Dashboard](${url})`;

          await ContentTaskUtils.waitForCondition(
            () => el.shadowRoot?.querySelector(".message-assistant a"),
            "The smartpage link renders"
          );
          const link = el.shadowRoot.querySelector(".message-assistant a");
          Assert.equal(
            link.getAttribute("href"),
            seen ? url : null,
            `The ${seen ? "seen" : "unseen"} smartpage link has the expected href`
          );
          el.remove();
        }
      });
    });

    it("should render about:preferences links as clickable", async () => {
      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        await content.customElements.whenDefined("ai-chat-message");

        const el = content.document.createElement("ai-chat-message");
        content.document.body.appendChild(el);

        const elJS = el.wrappedJSObject || el;
        // See note above: drive `role` via `data-message-role` (ARIA role
        // collision; TODO rename the reactive property off `role`).
        el.setAttribute("data-message-role", "assistant");
        elJS.trustedUrls = Cu.cloneInto([], content);
        const md = `Go to [Manage memories](about:preferences#manageMemories)`;
        elJS.message = md;

        await ContentTaskUtils.waitForCondition(() => {
          return el.shadowRoot?.querySelector(".message-assistant a[href]");
        }, "about:preferences link should render with href");

        const link = el.shadowRoot.querySelector(
          `.message-assistant a[href="about:preferences#manageMemories"]`
        );
        Assert.ok(link, "about:preferences link should keep its href");
        el.remove();
      });
    });

    it("should strip other about: URLs from rendered links", async () => {
      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        await content.customElements.whenDefined("ai-chat-message");

        const el = content.document.createElement("ai-chat-message");
        content.document.body.appendChild(el);

        const elJS = el.wrappedJSObject || el;
        // See note above: drive `role` via `data-message-role` (ARIA role
        // collision; TODO rename the reactive property off `role`).
        el.setAttribute("data-message-role", "assistant");
        elJS.trustedUrls = Cu.cloneInto([], content);
        elJS.message = `See [config](about:config) for details`;

        await ContentTaskUtils.waitForCondition(() => {
          const msg = el.shadowRoot?.querySelector(".message-assistant");
          return msg?.querySelector("a");
        }, "link element should exist");

        // Give rendering a tick to process links
        await new Promise(r => content.setTimeout(r, 50));

        const link = el.shadowRoot.querySelector(
          `.message-assistant a[href="about:config"]`
        );
        Assert.ok(!link, "about:config link should have href stripped");
        el.remove();
      });
    });

    it("should render a localized message link via data-l10n-name", async () => {
      await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
        await content.customElements.whenDefined("ai-chat-message");

        const el = content.document.createElement("ai-chat-message");
        content.document.body.appendChild(el);

        const elJS = el.wrappedJSObject || el;
        // See note above: drive `role` via `data-message-role` (ARIA role
        // collision; TODO rename the reactive property off `role`).
        el.setAttribute("data-message-role", "assistant");
        elJS.messageL10n = Cu.cloneInto(
          {
            id: "smartwindow-agent-monitor-watching",
            args: { monitorName: "Example", schedule: "daily at 9:00 AM" },
            link: { l10nName: "tasks", href: "about:smartwindowtasks" },
          },
          content
        );

        await ContentTaskUtils.waitForCondition(
          () =>
            el.shadowRoot?.querySelector(
              `.message-assistant a[href="about:smartwindowtasks"]`
            ),
          "Localized message link should render with its href"
        );

        const link = el.shadowRoot.querySelector(
          `.message-assistant a[href="about:smartwindowtasks"]`
        );
        Assert.ok(link, "Localized link keeps its href");
        Assert.ok(
          link.textContent.trim().length,
          "Localized link text is filled from the l10n message"
        );
        el.remove();
      });
    });
  });

  describe("aichat preferSwitchToTab", () => {
    it("should switch to an existing tab when one matches the URL", async () => {
      existingTab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        TEST_URL
      );
      gBrowser.selectedTab = chatTab;
      const initialTabCount = gBrowser.tabs.length;

      await SpecialPowers.spawn(
        chatTab.linkedBrowser,
        [TEST_URL],
        async url => {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url, preferSwitchToTab: true },
            })
          );
        }
      );

      await TestUtils.waitForCondition(
        () => gBrowser.selectedTab === existingTab,
        "Should switch to the existing tab with the matching URL"
      );
      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "No new tab should be created when an existing one matches"
      );

      BrowserTestUtils.removeTab(existingTab);
      existingTab = null;
    });

    it("should open a new tab on accelKey-click even when a matching tab is already open", async () => {
      existingTab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        TEST_URL
      );
      gBrowser.selectedTab = chatTab;

      Assert.equal(
        gBrowser.selectedTab,
        chatTab,
        "The chat tab is selected, not the existing one"
      );
      const initialTabCount = gBrowser.tabs.length;

      const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, TEST_URL);
      await clickRenderedLink(chatTab.linkedBrowser, TEST_URL, {
        accelKey: true,
      });
      const newTab = await newTabPromise;

      Assert.ok(
        newTab && newTab !== existingTab,
        "accelKey click should open a fresh tab instead of switching to the existing one"
      );
      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount + 1,
        "Exactly one new tab should be created"
      );

      BrowserTestUtils.removeTab(newTab);
      BrowserTestUtils.removeTab(existingTab);
      existingTab = null;
    });

    it("should open in a new tab when no existing tab matches", async () => {
      const noMatchUrl = "https://example.com/no-existing-tab-for-this-path";
      const initialTabCount = gBrowser.tabs.length;
      const newTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        noMatchUrl
      );

      await SpecialPowers.spawn(
        chatTab.linkedBrowser,
        [noMatchUrl],
        async url => {
          content.document.dispatchEvent(
            new content.CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              detail: { url, preferSwitchToTab: true },
            })
          );
        }
      );

      const newTab = await newTabPromise;
      Assert.ok(newTab, "A new tab should open when no existing tab matches");
      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount + 1,
        "Exactly one new tab should be created"
      );
      Assert.equal(
        gBrowser.selectedTab,
        chatTab,
        "The chat tab should not be navigated; the new tab opens alongside"
      );

      BrowserTestUtils.removeTab(newTab);
    });
  });
});
