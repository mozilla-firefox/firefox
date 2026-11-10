// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration; --enable-defer-import-eval

// Re-exported namespaces of a wasm module don't collide with its exports, even
// when they are named "*namespace*" or "*deferred-namespace*".

import { wasmNs, deferredNs } from "export-namespace-wasm.js";
import * as ns from "exports-namespace-name.wasm";
import { "*namespace*" as n, "*deferred-namespace*" as d } from "exports-namespace-name.wasm";

assertEq(n(), 1);
assertEq(d(), 2);
assertEq(wasmNs, ns);
assertEq(ns["*namespace*"], n);
assertEq(ns["*deferred-namespace*"], d);
assertEq(JSON.stringify(Object.keys(ns).sort()),
         '["*deferred-namespace*","*namespace*","f"]');
assertEq(deferredNs === ns, false);
assertEq(deferredNs.f(), 3);
assertEq(deferredNs["*namespace*"], n);
