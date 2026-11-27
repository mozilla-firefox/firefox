// Imports externalScript.js, which the policy of this worker, given in the
// response header, blocks, and posts the members of the violation event which
// are relevant for url hashes to the document.
self.addEventListener('securitypolicyviolation', e => {
  const msg = {
    blockedURI: e.blockedURI,
    urlHash: e.urlHash,
  };

  if (typeof SharedWorkerGlobalScope !== 'undefined' &&
      self instanceof SharedWorkerGlobalScope) {
    self.sharedPorts.forEach(port => port.postMessage(msg));
  } else if (typeof ServiceWorkerGlobalScope !== 'undefined' &&
             self instanceof ServiceWorkerGlobalScope) {
    self.clients.matchAll({ includeUncontrolled: true }).then(clients => {
      clients.forEach(client => client.postMessage(msg));
    });
  } else {
    self.postMessage(msg);
  }
});

// The import is blocked by the CSP of this worker. Catch the resulting
// NetworkError, so that the worker's script evaluation doesn't fail; for a
// service worker that would make its registration fail, which can tear down
// the worker before it has reported the violation. The script is imported with
// an absolute URL string, like in the document, so that the reported url
// source is the same whether it's computed from the URL string or the URL.
function importBlockedScript() {
  try {
    importScripts(new URL('externalScript.js', location.href).href);
  } catch (e) {
  }
}

if (typeof SharedWorkerGlobalScope !== 'undefined' &&
    self instanceof SharedWorkerGlobalScope) {
  self.sharedPorts = [];
  self.addEventListener('connect', e => {
    self.sharedPorts.push(e.ports[0]);
    importBlockedScript();
  });
} else {
  importBlockedScript();
}