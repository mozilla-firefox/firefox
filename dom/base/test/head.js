const { SpecialPowersForProcess } = ChromeUtils.importESModule(
  "resource://testing-common/SpecialPowersProcessActor.sys.mjs"
);

const scope = this;

async function newFocusedWindow(trigger, isInitialBlank = false) {
  let winPromise = BrowserTestUtils.domWindowOpenedAndLoaded();
  let delayedStartupPromise = BrowserTestUtils.waitForNewWindow({
    waitForAnyURLLoaded: !isInitialBlank,
  });

  await trigger();

  let win = await winPromise;
  // New windows get focused after the first paint, see bug 1262946,
  // but this is racy for the initial about:blank
  if (!isInitialBlank) {
    await BrowserTestUtils.waitForContentEvent(
      win.gBrowser.selectedBrowser,
      "MozAfterPaint"
    );
  } else {
    await new Promise(res =>
      win.requestAnimationFrame(() => win.requestAnimationFrame(res))
    );
  }
  await delayedStartupPromise;
  return win;
}

const JS_CACHE_BASE_URL = "http://mochi.test:8888/browser/dom/base/test/";

function ev(event, file, hasElement = !!file) {
  return {
    event,
    url: file ? JS_CACHE_BASE_URL + file : undefined,
    hasElement,
  };
}

function unordered(list) {
  return {
    unordered: list,
  };
}

function optional_ev(...args) {
  const event = ev(...args);
  event.optional = true;
  return event;
}

async function jsCacheContentProcessTask(test, item) {
  const defaultSkippedEvents = test.skippedEvents ?? [
    // The compilation is not target of this test.
    "compile:main thread",
    // This is triggered by 'invalidateMemory' below, but we don't have to
    // track it.
    "memorycache:invalidate",
    // The disk cache handling for the in-memory cache can happen multiple
    // times depending on the scheduling and speed
    // (e.g. debug vs opt, verify mode).
    "diskcache:noschedule",
  ];
  const nonSkippedEvents = test.nonSkippedEvents ?? [];
  const skippedEvents = defaultSkippedEvents.filter(
    ev => !nonSkippedEvents.includes(ev)
  );

  function match(param, event) {
    if (event.event !== param.event) {
      return false;
    }

    if (param.url && event.url !== param.url) {
      return false;
    }

    if (event.hasElement === "dontcare") {
      // Preloads can have no element yet.
    } else if (event.hasElement) {
      if (param.id !== "watchme") {
        return false;
      }
    } else {
      if (param.id) {
        return false;
      }
    }

    return true;
  }

  function consumeIfMatched(param, events) {
    while ("optional" in events[0]) {
      if (match(param, events[0])) {
        events.shift();
        return true;
      }
      dump("@@@ Skip optional event: " + events[0].event + "\n");
      events.shift();
    }

    if ("unordered" in events[0]) {
      const unordered = events[0].unordered;
      for (let i = 0; i < unordered.length; i++) {
        if (match(param, unordered[i])) {
          unordered.splice(i, 1);
          if (!unordered.length) {
            events.shift();
          }
          return true;
        }
      }

      return false;
    }

    if (match(param, events[0])) {
      events.shift();
      return true;
    }

    return false;
  }

  let result = true;

  const { promise, resolve, reject } = Promise.withResolvers();
  const observer = function (subject, topic, data) {
    const param = {};
    for (const line of data.split("\n")) {
      const m = line.match(/^([^:]+):(.*)/);
      param[m[1]] = m[2];
    }

    if (consumeIfMatched(param, item.events)) {
      dump("@@@ Got expected event: " + data + "\n");
      if (item.events.length === 0) {
        resolve();
      }
    } else if (skippedEvents.includes(param.event)) {
      dump("@@@ Ignoring: " + data + "\n");
    } else {
      dump("@@@ Got unexpected event: " + data + "\n");
      dump("@@@ Expected: " + JSON.stringify(item.events[0]) + "\n");
      result = false;
    }
  };
  Services.obs.addObserver(observer, "ScriptLoaderTest");

  await promise;

  Services.obs.removeObserver(observer, "ScriptLoaderTest");

  return result;
}

function jsCacheContentJumpTask(test, item) {
  const link = content.document.createElement("a");
  link.textContent = "link";
  link.href = item.page;
  content.document.body.appendChild(link);

  link.click();
}

async function jsCacheContentScriptTask(test, item) {
  const script = content.document.createElement("script");
  script.id = "watchme";
  if (test.module || item.module) {
    script.type = "module";
  }
  if (item.sri) {
    script.integrity = item.sri;
  }
  if (item.nonce) {
    script.nonce = item.nonce;
  }
  if (item.charset) {
    script.charset = item.charset;
  }
  script.src = item.file;
  const onLoadPromise = new Promise(resolve => {
    script.onload = () => {
      resolve();
    };
    script.onerror = () => {
      resolve();
    };
  });
  content.document.body.appendChild(script);

  await onLoadPromise;
}

async function runJSCacheTests(tests) {
  await BrowserTestUtils.withNewTab(
    JS_CACHE_BASE_URL + "empty.html",
    async browser => {
      const tab = gBrowser.getTabForBrowser(browser);

      for (const test of tests) {
        ChromeUtils.clearResourceCache();
        Services.cache2.clear();

        if (test.useServiceWorker) {
          await SpecialPowers.spawn(browser, [], async () => {
            const registration = await content.navigator.serviceWorker.register(
              "file_js_cache_sw.js",
              { scope: "./" }
            );

            const sw = registration.installing || registration.active;

            await new Promise(resolve => {
              function onStateChange() {
                if (sw.state === "activated") {
                  sw.removeEventListener("statechange", onStateChange);
                  resolve();
                }
              }
              sw.addEventListener("statechange", onStateChange);
              onStateChange();
            });
          });
        }

        for (let i = 0; i < test.items.length; i++) {
          const item = test.items[i];
          info(`start: ${test.title} (item ${i})`);

          if (!test.skipReload) {
            // Make sure the test starts in clean document.
            await BrowserTestUtils.loadURIString({
              browser: tab.linkedBrowser,
              uriString: JS_CACHE_BASE_URL + "empty.html",
            });
          }

          if (item.clearMemory) {
            info("clear memory cache");
            ChromeUtils.clearResourceCache();
          }
          if (item.invalidateMemory) {
            info("invalidate memory cache");
            ChromeUtils.invalidateResourceCache();
          }
          if (item.clearDisk) {
            info("clear disk cache");
            Services.cache2.clear();
          }
          if (item.memoryPressureLowMemory) {
            info("notifying memory pressure low-memory");
            await SpecialPowers.spawn(browser, [], () => {
              Services.obs.notifyObservers(
                null,
                "memory-pressure",
                "low-memory"
              );
            });
          }
          if (item.memoryPressureHeapMinimize) {
            info("notifying memory pressure heap-minimize");
            await SpecialPowers.spawn(browser, [], () => {
              Services.obs.notifyObservers(
                null,
                "memory-pressure",
                "heap-minimize"
              );
            });
          }
          if (item.memoryPressureStop) {
            info("notifying memory pressure stop");
            await SpecialPowers.spawn(browser, [], () => {
              Services.obs.notifyObservers(null, "memory-pressure-stop");
            });
          }

          // The content task can perform the top-level navigation,
          // which is not compatible with regular SpecialPowers.spawn.
          const proc = browser.browsingContext.currentWindowGlobal.domProcess;
          const processBoundSpecialPowers = new SpecialPowersForProcess(
            scope,
            proc
          );
          const eventsPromise = processBoundSpecialPowers.spawn(
            [test, item],
            jsCacheContentProcessTask
          );

          if (item.page) {
            await SpecialPowers.spawn(
              browser,
              [test, item],
              jsCacheContentJumpTask
            );
          } else {
            await SpecialPowers.spawn(
              browser,
              [test, item],
              jsCacheContentScriptTask
            );
          }

          const result = await eventsPromise;

          await processBoundSpecialPowers.destroy();

          ok(result, "Received expected events");
          if (item.verifyText) {
            const text = await SpecialPowers.spawn(browser, [], function () {
              return content.document.body.textContent.replace(
                /[ \r\n\t]*/g,
                ""
              );
            });
            is(text, item.verifyText);
          }
        }

        if (test.useServiceWorker) {
          await SpecialPowers.spawn(browser, [], async () => {
            const registration =
              await content.navigator.serviceWorker.getRegistration();
            registration.unregister();
          });
        }

        ok(true, "end: " + test.title);
      }
    }
  );

  ok(true, "Finished all tests");
}
