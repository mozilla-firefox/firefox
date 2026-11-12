/* Shared helpers for the test_preload_linked_*.html tests. */

/* exported waitForResponse, addPreload, addImportMap, addModule */

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
