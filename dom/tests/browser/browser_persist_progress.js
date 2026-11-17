/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SJS_URL =
  "https://example.com/browser/dom/tests/browser/file_browserPersist_progress.sjs";

/**
 * Saves a page containing the given resources and records the total progress
 * reported by nsWebBrowserPersist.
 *
 * @param {string[]} resources
 *   Resource types served by the sjs. The page has one image for each.
 * @param {object} [options]
 * @param {Function} [options.beforeSave]
 *   Awaited once the page is loaded, before saving it.
 * @param {Function} [options.onRequestStop]
 *   Called with the request name whenever a request stops.
 * @returns {Promise<{cur: number, max: number}[]>}
 */
async function persistAndRecordTotals(
  resources,
  { beforeSave = () => {}, onRequestStop = () => {} } = {}
) {
  let url = `${SJS_URL}?type=page&resources=${resources.join(",")}`;
  return BrowserTestUtils.withNewTab(url, async browser => {
    let doc = await new Promise((resolve, reject) => {
      browser.frameLoader.startPersistence(null, {
        onDocumentReady: resolve,
        onError: reject,
      });
    });

    let tmpFile = Services.dirsvc.get("TmpD", Ci.nsIFile);
    tmpFile.append("persist_progress_saved.html");
    tmpFile.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o600);
    let tmpDir = tmpFile.parent.clone();
    tmpDir.append(tmpFile.leafName + "_files");
    registerCleanupFunction(() => {
      tmpFile.remove(false);
      if (tmpDir.exists()) {
        tmpDir.remove(true);
      }
    });

    await beforeSave();

    let totals = [];
    let { promise, resolve } = Promise.withResolvers();
    let wbp = Cc[
      "@mozilla.org/embedding/browser/nsWebBrowserPersist;1"
    ].createInstance(Ci.nsIWebBrowserPersist);
    wbp.progressListener = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsIWebProgressListener2",
      ]),
      onProgressChange64(_wp, _req, _cur, _max, curTotal, maxTotal) {
        totals.push({ cur: curTotal, max: maxTotal });
      },
      onProgressChange() {},
      onLocationChange() {},
      onStatusChange() {},
      onSecurityChange() {},
      onContentBlockingEvent() {},
      onStateChange(_wp, request, state, status) {
        if (!(state & Ci.nsIWebProgressListener.STATE_STOP)) {
          return;
        }
        if (state & Ci.nsIWebProgressListener.STATE_IS_NETWORK) {
          resolve(status);
        } else if (request) {
          onRequestStop(request.name);
        }
      },
    };
    wbp.saveDocument(doc, tmpFile, tmpDir, null, 0, 0);
    let status = await promise;

    Assert.ok(Components.isSuccessCode(status), "The page was saved");
    Assert.greater(totals.length, 1, "Got progress notifications");
    for (let i = 1; i < totals.length; i++) {
      Assert.greaterOrEqual(
        totals[i].cur,
        totals[i - 1].cur,
        `Total progress should not decrease (notification ${i})`
      );
    }
    for (let { cur, max } of totals) {
      Assert.ok(
        max == -1 || cur <= max,
        `Total progress ${cur} should not exceed the known maximum ${max}`
      );
    }
    return totals;
  });
}

// The server holds the resource of unknown size until the big one has
// completed, so that progress is reported after a finished request has left
// the output map.
add_task(async function test_finished_request_with_unknown_size() {
  registerCleanupFunction(() => fetch(SJS_URL + "?type=release"));

  let released;
  let totals = await persistAndRecordTotals(["big", "unknown"], {
    beforeSave: () => fetch(SJS_URL + "?type=arm"),
    onRequestStop: name => {
      if (name.includes("type=big")) {
        released = fetch(SJS_URL + "?type=release");
      }
    },
  });
  Assert.ok(released, "The big resource completed");
  await released;

  Assert.ok(
    totals.every(t => t.max == -1),
    "The maximum is unknown while a resource of unknown size is pending"
  );
});

add_task(async function test_known_sizes() {
  let totals = await persistAndRecordTotals(["big", "small"]);

  Assert.equal(
    totals[0].max,
    -1,
    "The maximum is unknown until all the resources reported their size"
  );
  let firstKnown = totals.findIndex(t => t.max != -1);
  Assert.greater(firstKnown, 0, "The maximum becomes known");
  Assert.ok(
    totals.slice(firstKnown).every(t => t.max != -1),
    "The maximum stays known once known"
  );
});
