// |jit-test| skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration; --enable-defer-import-eval

// A wasm import that resolves to a namespace re-exported by a JS module throws
// a LinkError until the spec defines it, see
// https://github.com/WebAssembly/esm-integration/issues/127.

function assertLinkError(path) {
  let error = null;
  import(path).then(
    () => {
      throw new Error(`import(${path}) should have been rejected`);
    },
    (e) => {
      error = e;
    }
  );
  drainJobQueue();
  assertEq(error instanceof WebAssembly.LinkError, true);
}

assertLinkError("namespace-import-jsNs.wasm");
assertLinkError("namespace-import-wasmNs.wasm");
assertLinkError("namespace-import-deferredNs.wasm");
