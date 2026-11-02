/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* Helper methods for testing sending reports with
 * the Report Broken Site feature.
 */

/* import-globals-from head.js */

add_setup(setupBuiltInTestTrackers);

function setClipboard(string) {
  Cc["@mozilla.org/widget/clipboardhelper;1"]
    .getService(Ci.nsIClipboardHelper)
    .copyString(string);
}

function getClipboardAsString() {
  let trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(window.docShell.QueryInterface(Ci.nsILoadContext));
  trans.addDataFlavor("text/plain");
  Services.clipboard.getData(
    trans,
    Ci.nsIClipboard.kGlobalClipboard,
    SpecialPowers.wrap(window).browsingContext.currentWindowContext
  );
  let data = {};
  trans.getTransferData("text/plain", data);
  data = data.value.QueryInterface(Ci.nsISupportsString);
  return data.toString();
}

function adjustForWrapping(value) {
  // match what ReportBrokenSite.sys.mjs does to strings when generating the preview markup.
  return JSON.stringify(value)?.replace(/[,:]/g, "$& ") ?? "";
}

async function getExpectedReportData(win, basic) {
  const rawReportData = structuredClone(
    await ViewState.get(win.document).currentTabWebcompatDetailsPromise
  );

  const out = {};
  for (const [category, values] of Object.entries(rawReportData)) {
    out[category] = Object.fromEntries(
      Object.entries(values)
        .filter(
          ([key, { doNotPreview }]) => !doNotPreview && key != "isTabSpecific"
        )
        .map(([name, { value }]) => [name, adjustForWrapping(value)])
    );
  }

  out.basic = Object.fromEntries(
    Object.entries(basic).map(([name, value]) => [name, JSON.stringify(value)])
  );

  const { screenshot } = rawReportData.tabInfo;
  out.basic.screenshot = screenshot.value;

  return out;
}

async function checkPreviewPanelData(rbs, basic) {
  const allDetails = rbs.previewItems.querySelectorAll("details");

  const previewData = {};
  for (const details of allDetails) {
    details.click();
    const section = details.querySelector("summary").innerText;
    previewData[section] = {};
    for (const data of details.querySelectorAll(".data .entry")) {
      const name = data.firstElementChild.textContent.slice(0, -1); // drop the :
      const value = data.querySelector(".value");
      const img = value.querySelector("img")?.src;
      if (img) {
        previewData[section][name] = img;
      } else {
        previewData[section][name] = value.textContent;
      }
    }
  }

  const expected = await getExpectedReportData(rbs.win, basic);
  ok(
    areObjectsEqual(previewData, expected),
    "Preview had the expected information"
  );
  return [previewData, expected];
}

async function checkPreviewPanelUX(rbs) {
  const allDetails = rbs.previewItems.querySelectorAll("details");

  for (const [idx, details] of allDetails.entries()) {
    is(
      details.open,
      !idx,
      `Next preview item starts off ${idx ? "closed" : "open"}`
    );

    const summary = details.querySelector("summary");

    rbs.click(summary);
    await TestUtils.waitForCondition(
      () => details.open == !!idx,
      `Next preview item properly ${idx ? "opens" : "closes"}`
    );

    rbs.click(summary);
    await TestUtils.waitForCondition(
      () => details.open == !idx,
      `Next preview item properly ${idx ? "closes" : "opens"} again`
    );
  }
}

add_task(async function testPreview() {
  ensureReportBrokenSitePreffedOn();

  for (const test of [
    {
      // Test when all data is to be shown on page without tracking info.
      url: URL.parse(REPORTABLE_PAGE_URL).href,
      description: "Video does not play",
      reason: "media",
    },
    {
      // Test when all data is to be shown on page with tracking info.
      url: URL.parse(REPORTABLE_PAGE_URL3).href,
      description: "Site says to disable my ad-blocker",
      reason: "adblocker",
    },
  ]) {
    const { description, reason, url } = test;

    await withNewTab(REPORTABLE_PAGE_URL3, async win => {
      const basicInfo = {
        url,
        description: description ?? "",
        reason: reason ?? "load",
      };

      const menu = AppMenu(win);
      let rbs = await menu.openReportBrokenSiteToDetailsPanel(test);
      await rbs.clickPreview();
      await checkPreviewPanelData(rbs, basicInfo);
      await checkPreviewPanelUX(rbs);

      if (win.browsingContext.usePrivateBrowsing) {
        if (rbs.blockedTrackersToggle.pressed) {
          rbs.blockedTrackersToggle.click();
          await isNotPressed(rbs.blockedTrackersToggle);
        }
        const [data] = await checkPreviewPanelData(rbs, basicInfo);
        await checkPreviewPanelUX(rbs);
        ok(
          areObjectsEqual(
            data.antitracking.blockedOrigins,
            `["https: //trackertest.org"]`,
            "Reporting the expected tracking data"
          ),
          "Preview had the expected information"
        );
      }

      await rbs.close();
    });
  }
});
