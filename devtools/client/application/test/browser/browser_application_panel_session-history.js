/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TAB_URL = URL_ROOT + "resources/session-history/index.html";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["devtools.application.sessionHistory.enabled", true]],
  });
});

add_task(async function () {
  await enableApplicationPanel();

  const {
    panel,
    tab,
    commands: { resourceCommand },
  } = await openNewTabAndApplicationPanel(TAB_URL);
  const onAvailable = () => {};
  let updated = 0;
  const onUpdated = () => {
    updated++;
  };

  resourceCommand.watchResources([resourceCommand.TYPES.SESSION_HISTORY], {
    onAvailable,
    onUpdated,
  });

  selectPage(panel, "session-history");

  const doc = panel.panelWin.document;

  const { sessionHistory } = tab.linkedBrowser.browsingContext;

  info("Navigate the frames.");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    for (let index = 0; index < 2; ++index) {
      const frame = content.document.querySelectorAll("iframe")[index];
      const { promise: onLoadFinishedPromise, resolve: onLoadFinished } =
        Promise.withResolvers();
      frame.addEventListener("load", onLoadFinished, { once: true });
      frame.contentWindow.location.href = `${frame.contentWindow.location.href}?${index}`;
      await onLoadFinishedPromise;
    }
  });

  // Wait for the updates for the frames to arrive
  await waitFor(() => updated == 2);

  // We're navigating to a very specific session history setup, which should in
  // diagram form look like this:
  // +------------------+------------------+------------------+
  // |        0         |        1         |        2         |
  // +------------------+------------------+------------------+
  // | index.html                                             |
  // +------------------+------------------+------------------+
  // | iframe.html      | iframe.html?0                       |
  // +------------------+------------------+------------------+
  // | iframe.html                         | iframe.html?1    |
  // +------------------+------------------+------------------+
  //
  // Session History is represented as a single flat table. Diagrams for
  // different same-document navigation groups are placed side by side,
  // separated by spacer columns. Within each group, rows capture the iframe
  // hierarchy.

  info("Start checking.");
  let table = doc.querySelector("#diagram-container-table");
  Assert.equal(4, table.rows.length);
  Assert.equal(1, table.tHead.rows.length);
  Assert.equal(1, table.tBodies.length);
  const tBody = table.tBodies[0];
  Assert.ok(HTMLTableSectionElement.isInstance(tBody));
  let tBodyRows = tBody.rows;
  Assert.equal(3, tBodyRows.length);
  Assert.ok(HTMLTableRowElement.isInstance(tBodyRows[0]));
  Assert.equal(1, tBodyRows[0].cells.length);
  Assert.equal(3, +tBodyRows[0].cells[0].colSpan);
  Assert.ok(HTMLButtonElement.isInstance(tBodyRows[0].cells[0].firstChild));
  Assert.ok(tBodyRows[0].cells[0].firstChild.innerText.endsWith("index.html"));

  Assert.ok(HTMLTableRowElement.isInstance(tBodyRows[1]));
  Assert.equal(2, tBodyRows[1].cells.length);
  Assert.equal(2, tBodyRows[1].cells[1].colSpan);
  Assert.ok(HTMLButtonElement.isInstance(tBodyRows[1].cells[0].firstChild));
  Assert.ok(HTMLButtonElement.isInstance(tBodyRows[1].cells[1].firstChild));
  Assert.ok(tBodyRows[1].cells[0].firstChild.innerText.endsWith("iframe.html"));
  Assert.ok(
    tBodyRows[1].cells[1].firstChild.innerText.endsWith("iframe.html?0")
  );

  Assert.ok(HTMLTableRowElement.isInstance(tBodyRows[2]));
  Assert.equal(2, tBodyRows[2].cells.length);
  Assert.equal(2, tBodyRows[2].cells[0].colSpan);
  Assert.ok(HTMLButtonElement.isInstance(tBodyRows[2].cells[0].firstChild));
  Assert.ok(HTMLButtonElement.isInstance(tBodyRows[2].cells[1].firstChild));
  Assert.ok(tBodyRows[2].cells[0].firstChild.innerText.endsWith("iframe.html"));
  Assert.ok(
    tBodyRows[2].cells[1].firstChild.innerText.endsWith("iframe.html?1")
  );

  info("Click on a button to bring up entry info");
  tBodyRows[2].cells[1].firstChild.click();

  let popover = doc.querySelector(":popover-open");
  Assert.ok(HTMLDivElement.isInstance(popover));
  Assert.ok(HTMLDListElement.isInstance(popover.firstChild));

  const expectedTerms = [
    "url",
    "title",
    "name",
    "id",
    "key",
    "bfcache",
    "parent",
  ];
  const actualTerms = [];
  for (const child of popover.firstChild.getElementsByTagName("dt")) {
    actualTerms.push(child.innerText);
  }

  Assert.deepEqual(expectedTerms, actualTerms);

  const entry = sessionHistory.getEntryAtIndex(2).GetChildAt(1);
  const { URI, title, name, isInBFCache, navigationId, navigationKey, parent } =
    entry;

  const expectedDetails = [
    URI.spec,
    title,
    name,
    navigationId,
    navigationKey,
    isInBFCache,
    parent.navigationId,
  ].map(expected => `${expected}`);
  const actualDetails = [];
  for (const child of popover.firstChild.getElementsByTagName("dd")) {
    actualDetails.push(child.innerText);
  }

  Assert.deepEqual(expectedDetails, actualDetails);

  info(
    "Check that hitting Escape when a detail popover is displayed does not open the split console"
  );
  // sanity check
  ok(popover.matches(":popover-open"), "The popover is still open");
  const onPopoverToggle = new Promise(resolve =>
    popover.addEventListener("toggle", resolve, { once: true })
  );
  EventUtils.sendKey("ESCAPE", panel.toolbox.win);
  await onPopoverToggle;
  ok(
    !popover.matches(":popover-open"),
    "The popover is no longer open after hitting escape"
  );

  // wait for a bit to check the split console wasn't opened
  await wait(500);
  ok(!panel.toolbox.splitConsole, "Split console did not open");

  info("Navigate to a new top-level document with title");
  const uri = new URL("index_with_title.html", TAB_URL);

  await BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString: uri.href,
  });

  table = doc.querySelector("#diagram-container-table");
  tBodyRows = table.tBodies[0].rows;

  // When navigating to a new top level we're going to add a spacer between the
  // entries. Because of that the diagram contains five columns. four for
  // entries and one for spacing.
  //
  // +-----+-----+------------------+   +------------------+
  // | ... | ... |        2         |   |        3         |
  // +-----+-----+------------------+   +------------------+
  // | ...                          |   | index_with_...   |
  // +-----+-----+------------------+   +------------------+
  // | ... | ...                    |
  // +-----+-----+------------------+
  // | ...       | iframe.html?1    |
  // +-----+-----+------------------+
  //                                  ^
  //                                  spacing column
  //
  // It should also be noted that column three has a borderless cell below its
  // entry.

  await waitFor(() => tBodyRows[0].cells.length == 3);

  Assert.equal(3, tBodyRows[0].cells.length);
  Assert.equal(uri.pathname, tBodyRows[0].cells[2].innerText);
  Assert.ok(tBodyRows[0].cells[1].getAttribute("aria-hidden"));
  Assert.equal(
    2,
    tBodyRows[1].cells[2].getAttribute("rowspan"),
    "empty cell below entry 3"
  );

  // Finally, when we have an interesting colgroup, make sure that it
  // corresponds to the table depicted above.
  const colgroup = table.querySelector("colgroup");
  Assert.equal(3, colgroup.children.length);
  Assert.equal(3, colgroup.children[0].getAttribute("span"));
  Assert.equal("diagram-spacer", colgroup.children[1].getAttribute("class"));

  info("Click on a button to bring up entry info");
  tBodyRows[0].cells[2].firstChild.click();

  popover = doc.querySelector(":popover-open");
  Assert.equal(
    "title",
    popover.firstChild.getElementsByTagName("dd")[1].innerText
  );

  info("Set a new title");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.document.title = "new title";
  });

  info("Wait until the title field in the popover is not the original one");
  // We don't need to close and reopen the popover, because it should be updated dynamically.
  await waitFor(() => {
    return (
      popover.firstChild.getElementsByTagName("dd")[1].innerText !== "title"
    );
  });

  Assert.equal(
    "new title",
    popover.firstChild.getElementsByTagName("dd")[1].innerText
  );

  info("Navigate to a fragment");
  updated = 0;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.location.hash = "#1";
  });

  await waitFor(() => updated == 1);

  table = doc.querySelector("table");
  tBodyRows = table.tBodies[0].rows;

  Assert.stringContains(
    tBodyRows[0].cells[3].getAttribute("class"),
    "same-document-nav"
  );

  updated = 0;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.history.replaceState(null, null, "#2");
  });

  await waitFor(() => updated == 1);

  table = doc.querySelector("table");
  tBodyRows = table.tBodies[0].rows;
  tBodyRows[0].cells[3].firstChild.click();
  popover = doc.querySelector(":popover-open");
  Assert.equal(
    "#2",
    new URL(popover.firstChild.getElementsByTagName("dd")[0].innerText).hash,
    "hash should be updated by replaceState"
  );

  resourceCommand.unwatchResources([resourceCommand.TYPES.SESSION_HISTORY], {
    onAvailable,
    onUpdated,
  });

  info("Closing the tab.");
  await BrowserTestUtils.removeTab(tab);
});
