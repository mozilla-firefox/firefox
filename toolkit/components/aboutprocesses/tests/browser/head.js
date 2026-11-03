/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A bunch of assumptions we make about the behavior of the parent process,
// and which we use as sanity checks. If Firefox evolves, we will need to
// update these values.
// Note that Test Verify can really stress the cpu durations.
const HARDCODED_ASSUMPTIONS_PROCESS = {
  minimalNumberOfThreads: 6,
  maximalNumberOfThreads: 1000,
  minimalCPUPercentage: 0,
  maximalCPUPercentage: 1000,
  minimalCPUTotalDurationMS: 10,
  maximalCPUTotalDurationMS: 10000000,
  minimalRAMBytesUsage: 1024 * 1024 /* 1 Megabyte */,
  maximalRAMBytesUsage: 1024 * 1024 * 1024 * 1024 * 1 /* 1 Tb */,
};

const HARDCODED_ASSUMPTIONS_THREAD = {
  minimalCPUPercentage: 0,
  maximalCPUPercentage: 100,
  minimalCPUTotalDurationMS: 0,
  maximalCPUTotalDurationMS: 10000000,
};

// How close we accept our rounding up/down.
const APPROX_FACTOR = 1.51;
const MS_PER_NS = 1000000;

// Wait for `about:processes` to be updated.
async function promiseAboutProcessesUpdated({ doc, force, tabAboutProcesses }) {
  let startTime = ChromeUtils.now();

  let updatePromise = new Promise(resolve => {
    doc.addEventListener("AboutProcessesUpdated", resolve, { once: true });
  });

  if (force) {
    await SpecialPowers.spawn(tabAboutProcesses.linkedBrowser, [], async () => {
      info("Forcing about:processes refresh");
      await content.Control.update(/* force = */ true);
    });
  }

  await updatePromise;

  // Fluent will update the visible table content during the next
  // refresh driver tick, wait for it.
  // requestAnimationFrame calls us at the begining of the tick, we use
  // dispatchToMainThread to execute our code after the end of it.
  //XXX: Replace with proper wait for l10n completion once bug 1520659 is fixed.
  await new Promise(doc.defaultView.requestAnimationFrame);
  await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

  ChromeUtils.addProfilerMarker(
    "promiseAboutProcessesUpdated",
    { startTime, category: "Test" },
    force ? "force" : undefined
  );
}

function promiseProcessDied({ childID }) {
  return new Promise(resolve => {
    let observer = properties => {
      properties.QueryInterface(Ci.nsIPropertyBag2);
      let subjectChildID = properties.get("childID");
      if (subjectChildID == childID) {
        Services.obs.removeObserver(observer, "ipc:content-shutdown");
        resolve();
      }
    };
    Services.obs.addObserver(observer, "ipc:content-shutdown");
  });
}

function isCloseEnough(value, expected) {
  if (value < 0 || expected < 0) {
    throw new Error(`Invalid isCloseEnough(${value}, ${expected})`);
  }
  if (Math.round(value) == Math.round(expected)) {
    return true;
  }
  if (expected == 0) {
    return false;
  }
  let ratio = value / expected;
  if (ratio <= APPROX_FACTOR && ratio >= 1 / APPROX_FACTOR) {
    return true;
  }
  return false;
}

function getMemoryMultiplier(unit, sign = "+") {
  let multiplier;
  switch (sign) {
    case "+":
      multiplier = 1;
      break;
    case "-":
      multiplier = -1;
      break;
    default:
      throw new Error("Invalid sign: " + sign);
  }
  switch (unit) {
    case "B":
      break;
    case "KB":
      multiplier *= 1024;
      break;
    case "MB":
      multiplier *= 1024 * 1024;
      break;
    case "GB":
      multiplier *= 1024 * 1024 * 1024;
      break;
    case "TB":
      multiplier *= 1024 * 1024 * 1024 * 1024;
      break;
    default:
      throw new Error("Invalid memory unit: " + unit);
  }
  return multiplier;
}

function getTimeMultiplier(unit) {
  switch (unit) {
    case "ns":
      return 1 / (1000 * 1000);
    case "µs":
      return 1 / 1000;
    case "ms":
      return 1;
    case "s":
      return 1000;
    case "m":
      return 60000;
  }
  throw new Error("Invalid time unit: " + unit);
}
// We produce localized numbers, with "," as a thousands separator in en-US
// builds, but `parseFloat` doesn't understand the ",", so we need to remove it
// before parsing.
function parseLocalizedFloat(str) {
  return Number.parseFloat(str.replace(/,/g, ""));
}

async function testCpu(element, total, slope, assumptions) {
  info(
    `Testing CPU display ${element.textContent} - ${element.title} vs total ${total}, slope ${slope}`
  );
  let barWidth = getComputedStyle(element).getPropertyValue("--bar-width");
  if (slope) {
    Assert.greater(
      Number.parseFloat(barWidth),
      0,
      "The bar width should be > 0 when there is some CPU use"
    );
  } else {
    Assert.equal(barWidth, "-0.5", "There should be no CPU bar displayed");
  }

  if (element.textContent == "(measuring)") {
    info("Still measuring");
    return;
  }

  const CPU_TEXT_CONTENT_REGEXP = /\~0%|idle|[0-9.,]+%|[?]/;
  let extractedPercentage = CPU_TEXT_CONTENT_REGEXP.exec(
    element.textContent
  )[0];
  switch (extractedPercentage) {
    case "idle":
      Assert.equal(slope, 0, "Idle means exactly 0%");
      // Nothing else to do here.
      return;
    case "~0%":
      Assert.ok(slope > 0 && slope < 0.0001);
      break;
    case "?":
      Assert.equal(slope, null);
      // Nothing else to do here.
      return;
    default: {
      // `parseLocalizedFloat("99%")` returns `99`.
      let computedPercentage = parseLocalizedFloat(extractedPercentage);
      Assert.ok(
        isCloseEnough(computedPercentage, slope * 100),
        `The displayed approximation of the slope is reasonable: ${computedPercentage} vs ${
          slope * 100
        }`
      );
      // Also, sanity checks.
      Assert.greaterOrEqual(
        computedPercentage / 100,
        assumptions.minimalCPUPercentage,
        `Not too little: ${computedPercentage / 100} >=? ${
          assumptions.minimalCPUPercentage
        } `
      );
      Assert.lessOrEqual(
        computedPercentage / 100,
        assumptions.maximalCPUPercentage,
        `Not too much: ${computedPercentage / 100} <=? ${
          assumptions.maximalCPUPercentage
        } `
      );
      break;
    }
  }

  const CPU_TOOLTIP_REGEXP = /(?:.*: ([0-9.,]+) ?(ns|µs|ms|s|m|h|d))/;
  // Example: "Total CPU time: 4,470ms"

  let [, extractedTotal, extractedUnit] = CPU_TOOLTIP_REGEXP.exec(
    element.title
  );

  let totalMS = total / MS_PER_NS;
  let computedTotal =
    parseLocalizedFloat(extractedTotal) * getTimeMultiplier(extractedUnit);
  Assert.ok(
    isCloseEnough(computedTotal, totalMS),
    `The displayed approximation of the total duration is reasonable: ${computedTotal} vs ${totalMS}`
  );
  Assert.ok(
    totalMS <= assumptions.maximalCPUTotalDurationMS &&
      totalMS >= assumptions.minimalCPUTotalDurationMS,
    `The total number of MS is reasonable ${totalMS}: [${assumptions.minimalCPUTotalDurationMS}, ${assumptions.maximalCPUTotalDurationMS}]`
  );
}

async function testMemory(element, total, delta, assumptions) {
  info(
    `Testing memory display ${element.textContent} - ${element.title} vs total ${total}, delta ${delta}`
  );
  const MEMORY_TEXT_CONTENT_REGEXP = /([0-9.,]+)(TB|GB|MB|KB|B)/;
  // Example: "383.55MB"
  let extracted = MEMORY_TEXT_CONTENT_REGEXP.exec(element.textContent);
  Assert.notEqual(
    extracted,
    null,
    `Can we parse ${element.textContent} with ${MEMORY_TEXT_CONTENT_REGEXP}?`
  );
  let [, extractedTotal, extractedUnit] = extracted;

  let extractedTotalNumber = parseLocalizedFloat(extractedTotal);
  Assert.greater(
    extractedTotalNumber,
    0,
    `Unitless total memory use is greater than 0: ${extractedTotal}`
  );
  if (extractedUnit != "GB") {
    Assert.lessOrEqual(
      extractedTotalNumber,
      1024,
      `Unitless total memory use is less than 1024: ${extractedTotal}`
    );
  }

  // Now check that the conversion was meaningful.
  let computedTotal = getMemoryMultiplier(extractedUnit) * extractedTotalNumber;
  Assert.ok(
    isCloseEnough(computedTotal, total),
    `The displayed approximation of the total amount of memory is reasonable: ${computedTotal} vs ${total}`
  );
  if (!AppConstants.ASAN) {
    // ASAN plays tricks with RAM (e.g. allocates the entirety of virtual memory),
    // which makes this test unrealistic.
    Assert.ok(
      assumptions.minimalRAMBytesUsage <= computedTotal &&
        computedTotal <= assumptions.maximalRAMBytesUsage,
      `The total amount amount of memory is reasonable: ${computedTotal} in [${assumptions.minimalRAMBytesUsage}, ${assumptions.maximalRAMBytesUsage}]`
    );
  }

  const MEMORY_TOOLTIP_REGEXP = /(?:.*: ([-+]?)([0-9.,]+)(GB|MB|KB|B))?/;
  // Example: "Evolution: -12.5MB"
  extracted = MEMORY_TOOLTIP_REGEXP.exec(element.title);
  Assert.notEqual(
    extracted,
    null,
    `Can we parse ${element.title} with ${MEMORY_TOOLTIP_REGEXP}?`
  );
  let [, extractedDeltaSign, extractedDeltaTotal, extractedDeltaUnit] =
    extracted;
  if (extractedDeltaSign == null) {
    Assert.equal(delta || 0, 0);
    return;
  }
  let deltaTotalNumber = parseLocalizedFloat(extractedDeltaTotal);
  // Note: displaying 1024KB can happen if the value is slightly less than
  // 1024*1024B but rounded to 1024KB.
  Assert.ok(
    deltaTotalNumber > 0 && deltaTotalNumber <= 1024,
    `Unitless delta memory use is in (0, 1024): ${extractedDeltaTotal}`
  );
  Assert.ok(
    ["B", "KB", "MB"].includes(extractedDeltaUnit),
    `Delta unit is reasonable: ${extractedDeltaUnit}`
  );

  // Now check that the conversion was meaningful.
  // Let's just check that the number displayed is within 10% of `delta`.
  let computedDelta =
    getMemoryMultiplier(extractedDeltaUnit, extractedDeltaSign) *
    deltaTotalNumber;
  Assert.equal(
    computedDelta >= 0,
    delta >= 0,
    `Delta has the right sign: ${computedDelta} vs ${delta}`
  );
}

function extractProcessDetails(row) {
  let children = row.children;
  let name = children[0];
  let memory = children[1];
  let cpu = children[2];
  if (Services.prefs.getBoolPref("toolkit.aboutProcesses.showProfilerIcons")) {
    name = name.firstChild;
    Assert.ok(
      name.nextSibling.classList.contains("profiler-icon"),
      "The profiler icon should be shown"
    );
  }
  let fluentArgs = row.ownerDocument.l10n.getAttributes(name).args;
  let threadDetailsRow = row.nextSibling;
  while (threadDetailsRow) {
    if (threadDetailsRow.classList.contains("process")) {
      threadDetailsRow = null;
      break;
    }
    if (threadDetailsRow.classList.contains("thread-summary")) {
      break;
    }
    threadDetailsRow = threadDetailsRow.nextSibling;
  }

  return {
    memory,
    cpu,
    pidContent: fluentArgs.pid,
    threads: threadDetailsRow,
  };
}

function findTabRowByName(doc, name) {
  for (let row of doc.getElementsByClassName("name")) {
    if (!row.parentNode.classList.contains("window")) {
      continue;
    }
    let foundName = document.l10n.getAttributes(row).args.name;
    if (foundName != name) {
      continue;
    }
    return row.parentNode;
  }
  return null;
}

function findProcessRowByOrigin(doc, origin) {
  for (let row of doc.getElementsByClassName("process")) {
    if (row.process.origin == origin) {
      return row;
    }
  }
  return null;
}

async function setupTabWithOriginAndTitle(origin, title, win = window) {
  let tab = BrowserTestUtils.addTab(win.gBrowser, origin, {
    skipAnimation: true,
  });
  tab.testTitle = title;
  tab.testOrigin = origin;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await SpecialPowers.spawn(tab.linkedBrowser, [title], async title => {
    content.document.title = title;
  });
  // Rows are matched by title, which the view reads from the tab's label.
  await BrowserTestUtils.waitForMutationCondition(
    tab,
    { attributeFilter: ["label"] },
    () => tab.label == title
  );
  return tab;
}

async function setupAudioTab() {
  let origin = "about:blank";
  let title = "utility audio";
  let tab = BrowserTestUtils.addTab(gBrowser, origin, { skipAnimation: true });
  tab.testTitle = title;
  tab.testOrigin = origin;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, { wantLoad: origin });
  await SpecialPowers.spawn(tab.linkedBrowser, [title], async title => {
    content.document.title = title;
    const ROOT =
      "https://example.com/browser/toolkit/components/aboutprocesses/tests/browser";
    let audio = content.document.createElement("audio");
    audio.setAttribute("controls", "true");
    audio.setAttribute("loop", true);
    audio.src = `${ROOT}/small-shot.mp3`;
    content.document.body.appendChild(audio);

    // play() sometimes never settles on Windows, which would stall the test
    // until the harness kills it after several minutes with no output. Bound
    // the wait so the failure is reported with the element's state instead.
    const PLAY_TIMEOUT_MS = 10000;
    let timer;
    await Promise.race([
      audio.play().then(() => content.clearTimeout(timer)),
      new Promise((resolve, reject) => {
        timer = content.setTimeout(
          () =>
            reject(
              new Error(
                `audio.play() did not resolve within ${PLAY_TIMEOUT_MS}ms ` +
                  `(readyState=${audio.readyState}, networkState=${audio.networkState}, ` +
                  `paused=${audio.paused}, currentTime=${audio.currentTime})`
              )
            ),
          PLAY_TIMEOUT_MS
        );
      }),
    ]);
  });
  return tab;
}

async function testAboutProcessesWithConfig({ showAllFrames, showThreads }) {
  const isFission = gFissionBrowser;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["toolkit.aboutProcesses.showAllSubframes", showAllFrames],
      ["toolkit.aboutProcesses.showThreads", showThreads],
      // Force same-origin tabs to share a single process, to properly test
      // functionality involving multiple tabs within a single process with Fission.
      ["dom.ipc.processCount.webIsolated", 1],
    ],
  });

  // Install a test extension to also cover processes and sub-frames related to the
  // extension process.
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: { id: "test-aboutprocesses@mochi.test" },
      },
    },
    background() {
      // Creates an about:blank iframe in the extension process to make sure that
      // Bug 1665099 doesn't regress.
      document.body.appendChild(document.createElement("iframe"));

      this.browser.test.sendMessage("bg-page-loaded");
    },
  });

  await extension.startup();
  await extension.awaitMessage("bg-page-loaded");

  // Setup tabs asynchronously.

  // The about:processes tab.
  info("Setting up about:processes");
  let promiseTabAboutProcesses = BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:processes",
    waitForLoad: true,
  });

  info("Setting up example.com");
  // Another tab that we'll pretend is hung.
  let promiseTabHung = (async function () {
    let tab = BrowserTestUtils.addTab(gBrowser, "http://example.com", {
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    let p = BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
      includeSubFrames: true,
      wantLoad: "about:blank",
    });
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      // Open an in-process iframe to test toolkit.aboutProcesses.showAllSubframes
      let frame = content.document.createElement("iframe");
      content.document.body.appendChild(frame);
    });
    await p;
    return tab;
  })();

  let promiseAudioPlayback = setupAudioTab();

  let promiseUserContextTab = (async function () {
    let tab = BrowserTestUtils.addTab(gBrowser, "http://example.com", {
      userContextId: 1,
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.document.title = "Tab with User Context";
    });
    return tab;
  })();

  info("Setting up tabs we intend to close");

  // The two following tabs share the same domain.
  // We use them to check that closing one doesn't close the other.
  let promiseTabCloseSeparately1 = setupTabWithOriginAndTitle(
    "http://example.org",
    "Close me 1 (separately)"
  );
  let promiseTabCloseSeparately2 = setupTabWithOriginAndTitle(
    "http://example.org",
    "Close me 2 (separately)"
  );

  // The two following tabs share the same domain.
  // We use them to check that closing the process kills them both.
  let promiseTabCloseProcess1 = setupTabWithOriginAndTitle(
    "http://example.net",
    "Close me 1 (process)"
  );

  let promiseTabCloseProcess2 = setupTabWithOriginAndTitle(
    "http://example.net",
    "Close me 2 (process)"
  );

  // The two following tabs share the same domain.
  // We use them to check that closing the process kills them both.
  let promiseTabCloseTogether1 = setupTabWithOriginAndTitle(
    "https://example.org",
    "Close me 1 (together)"
  );

  let promiseTabCloseTogether2 = setupTabWithOriginAndTitle(
    "https://example.org",
    "Close me 2 (together)"
  );

  // Wait for initialization to finish.
  let tabAboutProcesses = await promiseTabAboutProcesses;
  let tabHung = await promiseTabHung;
  let audioPlayback = await promiseAudioPlayback;
  let tabUserContext = await promiseUserContextTab;
  let tabCloseSeparately1 = await promiseTabCloseSeparately1;
  let tabCloseSeparately2 = await promiseTabCloseSeparately2;
  let tabCloseProcess1 = await promiseTabCloseProcess1;
  let tabCloseProcess2 = await promiseTabCloseProcess2;
  let tabCloseTogether1 = await promiseTabCloseTogether1;
  let tabCloseTogether2 = await promiseTabCloseTogether2;

  let doc = tabAboutProcesses.linkedBrowser.contentDocument;
  let tbody = doc.getElementById("process-tbody");
  Assert.ok(!!tbody, "Found the #process-tbody element");

  if (isFission) {
    // We're going to kill this process later, so tell it to add an
    // annotation so the leak checker knows it is okay there is no
    // leak log.
    await SpecialPowers.spawn(tabCloseProcess1.linkedBrowser, [], () => {
      ChromeUtils.privateNoteIntentionalCrash();
    });
  }

  info("Setting up fake process hang detector");
  let hungChildID = tabHung.linkedBrowser.frameLoader.childID;

  // Keep informing about:processes that `tabHung` is hung.
  // Note: this is a background task, do not `await` it.
  let fakeProcessHangMonitor = async function () {
    for (let i = 0; i < 100; ++i) {
      if (!tabHung.linkedBrowser) {
        // Let's stop spamming as soon as we can.
        return;
      }

      Services.obs.notifyObservers(
        {
          childID: hungChildID,
          scriptBrowser: tabHung.linkedBrowser,
          scriptFileName: "chrome://browser/content/browser.js",
          QueryInterface: ChromeUtils.generateQI(["nsIHangReport"]),
        },
        "process-hang-report"
      );

      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(resolve => setTimeout(resolve, 300));
    }
  };
  fakeProcessHangMonitor();

  // about:processes will take a little time to appear and be populated.
  await promiseAboutProcessesUpdated({ doc, tabAboutProcesses });
  Assert.ok(tbody.childElementCount, "The table should be populated");
  Assert.ok(
    !!tbody.getElementsByClassName("hung").length,
    "The hung process should appear"
  );

  info("Looking at the contents of about:processes");
  let processesToBeFound = [
    // The browser process.
    {
      name: "browser",
      predicate: row => row.process.type == "browser",
    },
    // The hung process.
    {
      name: "hung",
      predicate: row =>
        row.classList.contains("hung") &&
        row.classList.contains("process") &&
        ["web", "webIsolated"].includes(row.process.type),
    },
    // Any non-hung process
    {
      name: "non-hung",
      predicate: row =>
        !row.classList.contains("hung") &&
        row.classList.contains("process") &&
        ["web", "webIsolated"].includes(row.process.type),
    },
    // A utility process with at least one actor.
    {
      name: "utility",
      predicate: row =>
        row.process &&
        row.process.type == "utility" &&
        row.classList.contains("process") &&
        row.nextSibling &&
        row.nextSibling.classList.contains("actor"),
    },
  ];
  for (let finder of processesToBeFound) {
    info(`Running sanity tests on ${finder.name}`);
    let row = tbody.firstChild;
    while (row) {
      if (finder.predicate(row)) {
        break;
      }
      row = row.nextSibling;
    }
    Assert.ok(!!row, `found a table row for ${finder.name}`);
    let { memory, cpu, pidContent, threads } = extractProcessDetails(row);

    info("Sanity checks: pid");
    let pid = Number.parseInt(pidContent);
    Assert.greater(pid, 0, `Checking pid ${pidContent}`);
    Assert.equal(pid, row.process.pid);

    info("Sanity checks: memory resident");
    await testMemory(
      memory,
      row.process.totalRamSize,
      row.process.deltaRamSize,
      HARDCODED_ASSUMPTIONS_PROCESS
    );

    info("Sanity checks: CPU (Total)");
    await testCpu(
      cpu,
      row.process.totalCpu,
      row.process.slopeCpu,
      HARDCODED_ASSUMPTIONS_PROCESS
    );

    // Testing threads.
    if (!showThreads) {
      info("In this mode, we shouldn't display any threads");
      Assert.equal(
        threads,
        null,
        "In hidden threads mode, we shouldn't have any thread summary"
      );
    } else {
      Assert.ok(threads, "We have a thread summary row");

      let {
        number,
        active = 0,
        list,
      } = doc.l10n.getAttributes(threads.children[0].children[1]).args;

      info("Sanity checks: number of threads");
      Assert.greaterOrEqual(
        number,
        HARDCODED_ASSUMPTIONS_PROCESS.minimalNumberOfThreads
      );
      Assert.lessOrEqual(
        number,
        HARDCODED_ASSUMPTIONS_PROCESS.maximalNumberOfThreads
      );
      Assert.equal(
        number,
        row.process.threads.length,
        "The number we display should be the number of threads"
      );

      info("Sanity checks: number of active threads");
      Assert.greaterOrEqual(
        active,
        0,
        "The number of active threads should never be negative"
      );
      Assert.lessOrEqual(
        active,
        number,
        "The number of active threads should not exceed the total number of threads"
      );
      let activeThreads = row.process.threads.filter(t => t.active);
      Assert.equal(
        active,
        activeThreads.length,
        "The displayed number of active threads should be correct"
      );

      let activeSet = new Set();
      for (let t of activeThreads) {
        activeSet.add(t.name.replace(/ ?#[0-9]+$/, ""));
      }
      info("Sanity checks: thread list");
      Assert.equal(
        list ? list.split(", ").length : 0,
        activeSet.size,
        "The thread summary list of active threads should have the expected length"
      );

      info("Testing that we can open the list of threads");
      let twisty = threads.getElementsByClassName("twisty")[0];
      twisty.click();

      // Fluent will update the text content of new rows during the
      // next refresh driver tick, wait for it.
      // requestAnimationFrame calls us at the begining of the tick, we use
      // dispatchToMainThread to execute our code after the end of it.
      //XXX: Replace with proper wait for l10n completion once bug 1520659 is fixed.
      await new Promise(doc.defaultView.requestAnimationFrame);
      await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

      let numberOfThreadsFound = 0;
      for (
        let threadRow = threads.nextSibling;
        threadRow && threadRow.classList.contains("thread");
        threadRow = threadRow.nextSibling
      ) {
        numberOfThreadsFound++;
      }
      Assert.equal(
        numberOfThreadsFound,
        number,
        `We should see ${number} threads, found ${numberOfThreadsFound}`
      );
      let threadIds = [];
      for (
        let threadRow = threads.nextSibling;
        threadRow && threadRow.classList.contains("thread");
        threadRow = threadRow.nextSibling
      ) {
        Assert.ok(
          threadRow.children.length >= 3 && threadRow.children[1].textContent,
          "The thread row should be populated"
        );
        let children = threadRow.children;
        let cpu = children[1];
        let l10nArgs = doc.l10n.getAttributes(children[0]).args;

        // Sanity checks: name
        Assert.ok(threadRow.thread.name, "Thread name is not empty");
        Assert.equal(
          l10nArgs.name,
          threadRow.thread.name,
          "Displayed thread name is correct"
        );

        // Sanity checks: tid
        let tidContent = l10nArgs.tid;
        let tid = Number.parseInt(tidContent);
        threadIds.push(tid);
        Assert.notEqual(tid, 0, "The tid should be set");
        Assert.equal(tid, threadRow.thread.tid, "Displayed tid is correct");

        // Sanity checks: CPU (per thread)
        await testCpu(
          cpu,
          threadRow.thread.totalCpu,
          threadRow.thread.slopeCpu,
          HARDCODED_ASSUMPTIONS_THREAD
        );
      }
      // By default, threads are sorted by tid.
      let threadList = threadIds.join(",");
      Assert.equal(
        threadList,
        threadIds.sort((a, b) => a - b).join(","),
        "The thread rows are in the default sort order."
      );
    }
  }

  await promiseAboutProcessesUpdated({
    doc,
    force: true,
    tabAboutProcesses,
  });

  // Testing subframes.
  info("Testing subframes");
  let foundAtLeastOneInProcessSubframe = false;
  for (let row of doc.getElementsByClassName("window")) {
    let subframe = row.win;
    if (subframe.tab) {
      continue;
    }
    let url = doc.l10n.getAttributes(row.children[0]).args.url;
    Assert.equal(url, subframe.documentURI.spec);
    if (!subframe.isProcessRoot) {
      foundAtLeastOneInProcessSubframe = true;
    }
  }
  if (showAllFrames) {
    Assert.ok(
      foundAtLeastOneInProcessSubframe,
      "Found at least one about:blank in-process subframe"
    );
  } else {
    Assert.ok(
      !foundAtLeastOneInProcessSubframe,
      "We shouldn't have any about:blank in-process subframe"
    );
  }

  info("Double-clicking on a tab");
  let whenTabSwitchedToWeb = BrowserTestUtils.switchTab(gBrowser, () => {
    // We pass a function to use `BrowserTestUtils.switchTab` not in its
    // role as a tab switcher but rather in its role as a function that
    // waits until something else has switched the tab.
    // We'll actually cause tab switching below, by doucle-clicking
    // in `about:processes`.
  });
  await SpecialPowers.spawn(tabAboutProcesses.linkedBrowser, [], async () => {
    // Locate and double-click on the representation of `tabHung`.
    let tbody = content.document.getElementById("process-tbody");
    for (let row of tbody.getElementsByClassName("tab")) {
      if (row.parentNode.win.documentURI.spec != "http://example.com/") {
        continue;
      }
      // Simulate double-click.
      let evt = new content.window.MouseEvent("dblclick", {
        bubbles: true,
        cancelable: true,
        view: content.window,
      });
      row.dispatchEvent(evt);
      return;
    }
    Assert.ok(false, "We should have found the hung tab");
  });

  info("Waiting for tab switch");
  await whenTabSwitchedToWeb;
  Assert.equal(
    gBrowser.selectedTab.linkedBrowser.currentURI.spec,
    tabHung.linkedBrowser.currentURI.spec,
    "We should have focused the hung tab"
  );

  await BrowserTestUtils.switchTab(gBrowser, tabAboutProcesses);

  info("Double-clicking on the extensions process");
  let tabPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:addons");
  await SpecialPowers.spawn(tabAboutProcesses.linkedBrowser, [], async () => {
    let extensionsRow =
      content.document.getElementsByClassName("extensions")[0];
    Assert.ok(!!extensionsRow, "We should have found the extensions process");
    let evt = new content.window.MouseEvent("dblclick", {
      bubbles: true,
      cancelable: true,
      view: content.window,
    });
    extensionsRow.dispatchEvent(evt);
  });
  info("Waiting for about:addons to open");
  await tabPromise;
  Assert.equal(
    gBrowser.selectedTab.linkedBrowser.currentURI.spec,
    "about:addons",
    "We should now see the addon tab"
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  info("Testing tab closing");

  // A list of processes we have killed and for which we're waiting
  // death confirmation. Only used in Fission.
  let waitForProcessesToDisappear = [];
  await promiseAboutProcessesUpdated({
    doc,
    force: true,
    tabAboutProcesses,
  });
  if (isFission) {
    // Before closing, all our origins should be present
    for (let origin of [
      "http://example.com", // tabHung
      "http://example.net", // tabCloseProcess*
      "http://example.org", // tabCloseSeparately*
      "https://example.org", // tabCloseTogether*
    ]) {
      Assert.ok(
        findProcessRowByOrigin(doc, origin),
        `There is a process for origin ${origin}`
      );
    }

    // Verify that the user context id has been correctly displayed.
    let userContextProcessRow = findProcessRowByOrigin(
      doc,
      "http://example.com^userContextId=1"
    );
    Assert.ok(
      userContextProcessRow,
      "There is a separate process for the tab with a different user context"
    );
    let name = userContextProcessRow.firstChild;
    if (
      Services.prefs.getBoolPref("toolkit.aboutProcesses.showProfilerIcons")
    ) {
      name = name.firstChild;
      Assert.ok(
        name.nextSibling.classList.contains("profiler-icon"),
        "The profiler icon should be shown"
      );
    }
    Assert.equal(
      doc.l10n.getAttributes(name).args.origin,
      "http://example.com — " +
        ContextualIdentityService.getUserContextLabel(1),
      "The user context ID should be replaced with the localized container name"
    );

    // These origins will disappear.
    for (let origin of [
      "http://example.net", // tabCloseProcess*
      "https://example.org", // tabCloseTogether*
    ]) {
      let row = findProcessRowByOrigin(doc, origin);
      let childID = row.process.childID;
      waitForProcessesToDisappear.push(promiseProcessDied({ childID }));
    }
  }

  // Close a few tabs.
  for (let tab of [tabCloseSeparately1, tabCloseTogether1, tabCloseTogether2]) {
    info("Closing a tab through about:processes");
    let found = findTabRowByName(doc, tab.linkedBrowser.contentTitle);
    Assert.ok(
      found,
      `We should have found tab ${tab.linkedBrowser.contentTitle} to close it`
    );
    let closeIcons = found.getElementsByClassName("close-icon");
    Assert.equal(
      closeIcons.length,
      1,
      "This tab should have exactly one close icon"
    );
    closeIcons[0].click();
    Assert.ok(
      found.classList.contains("killing"),
      "We should have marked the row as dying"
    );
  }

  //...and a process, if we're in Fission.
  if (isFission) {
    info("Closing an entire process through about:processes");
    let found = findProcessRowByOrigin(doc, "http://example.net");
    let closeIcons = found.getElementsByClassName("close-icon");
    Assert.equal(
      closeIcons.length,
      1,
      "This process should have exactly one close icon"
    );
    closeIcons[0].click();
    Assert.ok(
      found.classList.contains("killing"),
      "We should have marked the row as dying"
    );

    info("Closing the audio utility process through about:processes");
    let utilityRow;
    for (let row of doc.getElementsByClassName("process")) {
      if (row.process.type == "utility") {
        utilityRow = row;
        break;
      }
    }
    Assert.ok(utilityRow, "Should have found audio utility row.");
    closeIcons = utilityRow.getElementsByClassName("close-icon");
    Assert.equal(
      closeIcons.length,
      1,
      "The utility process should have exactly one close icon"
    );
    closeIcons[0].click();
    Assert.ok(
      utilityRow.classList.contains("killing"),
      "We should have marked the utility process as dying"
    );
  }

  // Give Firefox a little time to close the tabs and update about:processes.
  // This might take two updates as we're racing between collecting data and
  // processes actually being killed.
  await promiseAboutProcessesUpdated({
    doc,
    force: true,
    tabAboutProcesses,
  });

  // The tabs we have closed directly or indirectly should now be (closed or crashed) and invisible in about:processes.
  for (let { origin, tab } of [
    { origin: "http://example.org", tab: tabCloseSeparately1 },
    { origin: "https://example.org", tab: tabCloseTogether1 },
    { origin: "https://example.org", tab: tabCloseTogether2 },
    ...(isFission
      ? [
          { origin: "http://example.net", tab: tabCloseProcess1 },
          { origin: "http://example.net", tab: tabCloseProcess2 },
        ]
      : []),
  ]) {
    // Tab shouldn't show up anymore in about:processes
    Assert.ok(
      !findTabRowByName(doc, origin),
      `Tab for ${origin} shouldn't show up anymore in about:processes`
    );
    // ...and should be unloaded.
    Assert.ok(
      !tab.getAttribute("linkedPanel"),
      `The tab should now be unloaded (${tab.testOrigin} - ${tab.testTitle})`
    );
  }

  // On the other hand, tabs we haven't closed should still be open and visible in about:processes.
  Assert.ok(
    tabCloseSeparately2.linkedBrowser,
    "Killing one tab in the domain should not have closed the other tab"
  );
  let foundtabCloseSeparately2 = findTabRowByName(
    doc,
    tabCloseSeparately2.linkedBrowser.contentTitle
  );
  Assert.ok(
    foundtabCloseSeparately2,
    "The second tab is still visible in about:processes"
  );

  if (isFission) {
    // After closing, we must have closed some of our origins.
    for (let origin of [
      "http://example.com", // tabHung
      "http://example.org", // tabCloseSeparately*
    ]) {
      Assert.ok(
        findProcessRowByOrigin(doc, origin),
        `There should still be a process row for origin ${origin}`
      );
    }

    info("Waiting for processes to die");
    await Promise.all(waitForProcessesToDisappear);

    info("Waiting for about:processes to be updated");
    await promiseAboutProcessesUpdated({
      doc,
      force: true,
      tabAboutProcesses,
    });

    for (let origin of [
      "http://example.net", // tabCloseProcess*
      "https://example.org", // tabCloseTogether*
    ]) {
      Assert.ok(
        !findProcessRowByOrigin(doc, origin),
        `Process ${origin} should disappear from about:processes`
      );
    }
  }

  info("Additional sanity check for all processes");
  for (let row of doc.getElementsByClassName("process")) {
    let { pidContent } = extractProcessDetails(row);
    Assert.equal(Number.parseInt(pidContent), row.process.pid);
  }
  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabHung);
  BrowserTestUtils.removeTab(tabUserContext);
  BrowserTestUtils.removeTab(tabCloseSeparately2);

  // We still need to remove these tabs.
  // We killed the process, but we don't want to leave zombie tabs lying around.
  BrowserTestUtils.removeTab(tabCloseProcess1);
  BrowserTestUtils.removeTab(tabCloseProcess2);
  BrowserTestUtils.removeTab(audioPlayback);

  await SpecialPowers.popPrefEnv();

  await extension.unload();
}

// Helpers for the about:processes?groupby=tab view.

async function forceTabViewUpdate(tabAboutProcesses) {
  await SpecialPowers.spawn(tabAboutProcesses.linkedBrowser, [], async () => {
    await content.TabControl.update();
  });
}

// Stops the page's own poll so the test decides when polls run. Returns a
// function that runs one real poll.
function pauseTabViewPolling(doc) {
  let control = doc.defaultView.TabControl;
  let update = control.update.bind(control);
  control.update = async () => {};
  return update;
}

// Unload/reload buttons ignore clicks until their row's action is done.
function promiseRowActionDone(row) {
  return BrowserTestUtils.waitForMutationCondition(
    row,
    { attributeFilter: ["aria-busy"] },
    () => !row.hasAttribute("aria-busy")
  );
}

function promiseTabViewUpdated(doc) {
  return new Promise(resolve =>
    doc.addEventListener("AboutProcessesUpdated", resolve, { once: true })
  );
}

// Rows hidden by a search are skipped; collapsed rows aren't.
function tabRowTitles(doc) {
  return [...doc.querySelectorAll("tr.tab-row:not([hidden])")].map(
    row => row.querySelector(".name").textContent
  );
}

function findTabRow(doc, title) {
  return [...doc.querySelectorAll("tr.tab-row")].find(row =>
    row.querySelector(".name").textContent.includes(title)
  );
}

function findSiteHeaderRow(doc, siteKey) {
  return [...doc.querySelectorAll("tr.site-header-row")].find(
    row => row.querySelector(".site-label").textContent == siteKey
  );
}

// Child rows are the header's immediate <tr> siblings, up to the next header/solo row.
function childRowsOf(headerRow) {
  let rows = [];
  for (
    let sibling = headerRow.nextElementSibling;
    sibling?.classList.contains("child-row");
    sibling = sibling.nextElementSibling
  ) {
    rows.push(sibling);
  }
  return rows;
}

// Not a raw dispatchEvent: that skips the layout/a11y flush real input goes through.
function clickRow(target, options = {}) {
  EventUtils.synthesizeMouseAtCenter(target, options, target.documentGlobal);
}

function clickCheckbox(doc, title) {
  clickRow(findTabRow(doc, title).querySelector(".select-checkbox"));
}

// Any recent mouse event freezes existing row order until the next real sort.
function startMouseFreeze(doc) {
  EventUtils.synthesizeMouse(
    doc.getElementById("process-tbody"),
    5,
    5,
    { type: "mousemove" },
    doc.defaultView
  );
}

async function openTabView() {
  let tabAboutProcesses = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:processes?groupby=tab",
    waitForLoad: true,
  });
  await forceTabViewUpdate(tabAboutProcesses);
  let doc = tabAboutProcesses.linkedBrowser.contentDocument;
  await TestUtils.waitForCondition(
    () => doc.querySelector("tr.tab-row"),
    "Waiting for at least one row to exist"
  );
  return { tabAboutProcesses, doc };
}

// Checks that metric recorded exactly one event with these extra values, and
// returns it (undefined otherwise) for any further checks.
function assertOneEvent(metric, extra, description) {
  let events = metric.testGetValue();
  Assert.equal(events?.length, 1, description);
  let event = events?.length == 1 ? events[0] : undefined;
  for (let [key, value] of Object.entries(extra)) {
    Assert.equal(event?.extra[key], value, `${description}: ${key}`);
  }
  return event;
}

// Replaces any earlier query and waits for the search box's debounced event.
async function enterTabSearch(doc, query) {
  let search = doc.getElementById("tab-search-input");
  await doc.defaultView.customElements.whenDefined("moz-input-search");
  await search.updateComplete;
  let searched = BrowserTestUtils.waitForEvent(search, "MozInputSearch:search");
  clickRow(search.inputEl);
  EventUtils.synthesizeKey("a", { accelKey: true }, doc.defaultView);
  EventUtils.sendString(query, doc.defaultView);
  await searched;
}
