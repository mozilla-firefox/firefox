/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_set_tab_offline() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    // Set the tab to offline
    gBrowser.selectedBrowser.browsingContext.forceOffline = true;

    await SpecialPowers.spawn(browser, [], async () => {
      try {
        // Use a unique URL to avoid a cached response from a previous run.
        await content.fetch(`https://example.com/empty.html?${Math.random()}`);
        ok(false, "Should not load since tab is offline");
      } catch (err) {
        is(err.name, "TypeError", "Should fail since tab is offline");
      }
    });
  });
});

add_task(async function test_set_tab_online() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    // Set the tab to online
    gBrowser.selectedBrowser.browsingContext.forceOffline = false;

    await SpecialPowers.spawn(browser, [], async () => {
      try {
        await content.fetch(`https://example.com/empty.html?${Math.random()}`);
        ok(true, "Should load since tab is online");
      } catch (err) {
        ok(false, "Should not fail since tab is online");
      }
    });
  });
});

add_task(async function test_set_tab_offline_keepalive_fetch() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    gBrowser.selectedBrowser.browsingContext.forceOffline = true;

    await SpecialPowers.spawn(browser, [], async () => {
      try {
        await content.fetch(`https://example.com/empty.html?${Math.random()}`, {
          keepalive: true,
        });
        ok(false, "Keepalive fetch should not load since tab is offline");
      } catch (err) {
        is(
          err.name,
          "TypeError",
          "Keepalive fetch should fail since tab is offline"
        );
      }
    });

    gBrowser.selectedBrowser.browsingContext.forceOffline = false;

    await SpecialPowers.spawn(browser, [], async () => {
      try {
        await content.fetch(`https://example.com/empty.html?${Math.random()}`, {
          keepalive: true,
        });
        ok(true, "Keepalive fetch should load since tab is online");
      } catch (err) {
        ok(false, "Keepalive fetch should not fail since tab is online");
      }
    });
  });
});

add_task(async function test_set_tab_offline_worker_fetch() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    gBrowser.selectedBrowser.browsingContext.forceOffline = true;

    const result = await SpecialPowers.spawn(browser, [], async () => {
      const script = `
        fetch("https://example.com/empty.html?" + Math.random()).then(
          response => postMessage({ ok: response.ok }),
          err => postMessage({ error: err.name })
        );
      `;
      const url = content.URL.createObjectURL(
        new content.Blob([script], { type: "text/javascript" })
      );
      const worker = new content.Worker(url);
      return new Promise(resolve => {
        worker.onmessage = event => resolve(event.data);
      });
    });
    is(
      result.error,
      "TypeError",
      "Worker fetch should fail since tab is offline"
    );

    gBrowser.selectedBrowser.browsingContext.forceOffline = false;
  });
});

add_task(async function test_set_tab_offline_events() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      content.events = [];
      content.window.addEventListener("offline", () =>
        content.events.push("offline")
      );
      content.window.addEventListener("online", () =>
        content.events.push("online")
      );
    });
    gBrowser.selectedBrowser.browsingContext.forceOffline = true;
    Assert.deepEqual(
      await SpecialPowers.spawn(browser, [], async () => content.events),
      ["offline"],
      "events should be fired"
    );
    gBrowser.selectedBrowser.browsingContext.forceOffline = false;
    Assert.deepEqual(
      await SpecialPowers.spawn(browser, [], async () => content.events),
      ["offline", "online"],
      "events should be fired"
    );
  });
});
