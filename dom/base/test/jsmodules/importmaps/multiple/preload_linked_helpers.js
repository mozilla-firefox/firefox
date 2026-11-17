/* Shared helpers for the preload tests in this directory. */

/* exported waitForResponse, waitForRequest, addPreload, addImportMap, addModule */

// Resolves once a resource whose file name is |name| has been fetched.
function waitForResponse(name) {
  return new Promise(resolve => {
    const po = new PerformanceObserver(list => {
      if (list.getEntries().some(e => e.name.split("/").pop() == name)) {
        po.disconnect();
        resolve();
      }
    });
    po.observe({ type: "resource", buffered: true });
  });
}

const requestWaiters = new Map();

// Resolves with the channel once a resource whose file name is |name| is
// requested. When hold is true, the channel is suspended to simulate an
// ongoing fetch.
function waitForRequest(name, { hold = false } = {}) {
  return new Promise(resolve => requestWaiters.set(name, { resolve, hold }));
}

const requestObserver = {
  observe(subject) {
    const channel = subject.QueryInterface(SpecialPowers.Ci.nsIChannel);
    const name = channel.URI.spec.split("?")[0].split("/").pop();
    const waiter = requestWaiters.get(name);
    if (!waiter) {
      return;
    }
    requestWaiters.delete(name);
    if (waiter.hold) {
      channel.suspend();
    }
    waiter.resolve(channel);
  },
};
SpecialPowers.addObserver(requestObserver, "http-on-opening-request");
SimpleTest.registerCleanupFunction(() => {
  SpecialPowers.removeObserver(requestObserver, "http-on-opening-request");
});

function addPreload(href) {
  return new Promise((resolve, reject) => {
    const link = document.createElement("link");
    link.rel = "modulepreload";
    link.href = href;
    link.onload = resolve;
    link.onerror = () => reject(new Error(`Failed to preload ${href}`));
    document.head.appendChild(link);
  });
}

function addImportMap(imports) {
  const s = document.createElement("script");
  s.type = "importmap";
  s.textContent = JSON.stringify({ imports });
  document.head.appendChild(s);
}

// Resolves once the module script has loaded or failed. A failure is reported
// through the window's error event rather than by rejecting.
function addModule(href) {
  return new Promise(resolve => {
    const s = document.createElement("script");
    s.type = "module";
    s.src = href;
    s.onload = s.onerror = resolve;
    document.body.appendChild(s);
  });
}
