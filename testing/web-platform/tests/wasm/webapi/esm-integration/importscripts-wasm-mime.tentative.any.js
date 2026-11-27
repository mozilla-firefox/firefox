// META: global=dedicatedworker,sharedworker
//
// importScripts() fetches classic scripts, which require a JavaScript MIME
// type even when WebAssembly/ES module integration is enabled. Blob URLs are
// used so that the worker script loader's MIME check is exercised directly.

test(() => {
  const url = URL.createObjectURL(new Blob([""], { type: "application/wasm" }));
  assert_throws_dom("NetworkError", () => importScripts(url));
}, "importScripts() rejects application/wasm");

test(() => {
  const url = URL.createObjectURL(new Blob([""], { type: "text/javascript" }));
  assert_equals(importScripts(url), undefined);
}, "importScripts() accepts text/javascript");
