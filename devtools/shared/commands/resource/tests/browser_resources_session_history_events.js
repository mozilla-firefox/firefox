/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function testSessionHistory() {
  const iframeUrl = "https://example.org/document-builder.sjs?html=iframe";
  const html = `top-document<iframe src="${iframeUrl}"></iframe>`;
  const pageUrl =
    "https://example.org/document-builder.sjs?html=" + encodeURIComponent(html);

  // Session history state snapshots delivered in order:
  // 1) about:blank (via onAvailable)
  // 2) top-level page only (via onAvailable, cross-document navigation)
  // 3) top-level + iframe (via onUpdated, same-document iframe commit)
  const expectedEntries = [
    [{ url: "about:blank" }],
    [{ url: pageUrl }],
    [{ url: pageUrl }, { url: iframeUrl }],
  ];

  // Title is updated when setting title from script
  const expectedUpdates = [
    { title: pageUrl },
    { title: "updated document title" },
  ];

  await assertSessionHistoryResourcesOnPage(
    pageUrl,
    expectedEntries,
    expectedUpdates
  );
});

async function assertSessionHistoryResourcesOnPage(
  testURL,
  expectedEntries,
  expectedUpdates
) {
  const tab = await addTab("about:blank");

  const commands = await CommandsFactory.forTab(tab);
  await commands.targetCommand.startListening();
  const { resourceCommand } = commands;

  const actualEntries = [];
  const actualUpdates = [];

  function collectDiagrams(resource) {
    for (const diagram of resource.diagrams) {
      const entry = [];
      for (const row of diagram.rows) {
        for (const cell of row) {
          const { url } = resource.entriesByKey[cell.key];
          entry.push({ url });
        }
      }
      actualEntries.push(entry);
    }
  }

  const onAvailable = resources => {
    for (const resource of resources) {
      collectDiagrams(resource);
    }
  };

  const onUpdated = updates => {
    for (const { update } of updates) {
      const { sessionHistory, sessionHistoryEntry } = update.resourceUpdates;
      if (sessionHistory) {
        // Same-document navigation (e.g. iframe commit): full session history snapshot.
        collectDiagrams(sessionHistory);
      } else if (sessionHistoryEntry) {
        actualUpdates.push({ title: sessionHistoryEntry.title });
        // Resolve when we get the title update from setting the document title.
        // This allows us to clearly determine when all test steps are done.
      }
    }
  };

  await resourceCommand.watchResources(
    [resourceCommand.TYPES.SESSION_HISTORY],
    {
      onAvailable,
      onUpdated,
    }
  );

  // Load the test page that populates session history.
  const onLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, testURL);
  await onLoaded;

  SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    content.document.title = "updated document title";
  });

  // There should be two title updates, one for load and one for setting the
  // title
  await waitFor(() => actualUpdates.length == 2);

  Assert.equal(
    actualEntries.length,
    expectedEntries.length,
    "correct actual entries received"
  );
  Assert.deepEqual(
    actualEntries,
    expectedEntries,
    "expected entries should be received"
  );

  Assert.equal(
    actualUpdates.length,
    expectedUpdates.length,
    "correct number of updates received"
  );
  Assert.deepEqual(
    actualUpdates,
    expectedUpdates,
    "expected updates should be received"
  );

  resourceCommand.unwatchResources([resourceCommand.TYPES.SESSION_HISTORY], {
    onAvailable,
    onUpdated,
  });

  await commands.destroy();
  BrowserTestUtils.removeTab(tab);
}
