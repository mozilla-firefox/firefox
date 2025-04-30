/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Check that about:debugging uses the favicon of tab targets as the icon of their debug
 * target item, and doesn't always use the default globe icon.
 */

// PlaceUtils will not store any favicon for data: uris so we need to use a dedicated page
// here.
const TAB_URL =
  "https://example.com/browser/devtools/client/aboutdebugging/" +
  "test/browser/test-tab-favicons.html";

// This is the same png data-url as the one used in test-tab-favicons.html.
const EXPECTED_FAVICON =
  "data:image/png;base64," +
  "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAATklEQVRYhe3SIQ4AI" +
  "BADwf7/04elBAtrVlSduGnSTDJ7cuT1PQJwwO+Hl7sAGAA07gjAAfgIBeAAoH" +
  "FHAA7ARygABwCNOwJwAD5CATRgAYXh+kypw86nAAAAAElFTkSuQmCC";

add_task(async function () {
  const faviconTab = await addTab(TAB_URL, { background: true });
  const { document, tab, window } = await openAboutDebugging();
  await selectThisFirefoxPage(document, window.AboutDebugging.store);

  await waitUntil(() => {
    const target = findDebugTargetByText("Favicon tab", document);
    if (!target) {
      return false;
    }
    // We may get a default globe.svg icon for a short period of time while
    // the target tab is still loading.
    return target
      .querySelector(".qa-debug-target-item-icon")
      .src.includes("data:");
  });
  const faviconTabTarget = findDebugTargetByText("Favicon tab", document);
  const faviconTabIcon = faviconTabTarget.querySelector(
    ".qa-debug-target-item-icon"
  );

  // Note this relies on PlaceUtils.favicons.getFaviconForPage() returning the
  // favicon that has same data-url as the one provided in the test page. If the
  // implementation changes and PlaceUtils returns a different base64 from the
  // one we defined, we can instead load the image and check a few pixels to
  // verify it matches the expected icon.
  is(
    faviconTabIcon.src,
    EXPECTED_FAVICON,
    "The debug target item for the tab shows the favicon of the tab"
  );

  await removeTab(tab);
  await removeTab(faviconTab);
});
