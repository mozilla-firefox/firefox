// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// Basic evaluation phase imports of a wasm module: a named import, a namespace
// import, a dynamic import, and a module with no exports.

import { fun } from "exports-fun.wasm";
import * as exportsFunNamespace from "exports-fun.wasm";

assertEq(typeof fun, "function");
assertEq(fun(3, 4), 7);

assertEq(exportsFunNamespace[Symbol.toStringTag], "Module");
assertEq(typeof exportsFunNamespace.fun, "function");
assertEq(exportsFunNamespace.fun(3, 4), 7);

const dynamicNamespace = await import("exports-fun.wasm");
assertEq(dynamicNamespace[Symbol.toStringTag], "Module");
assertEq(dynamicNamespace.fun(3, 4), 7);

const empty = await import("empty.wasm");
assertEq(empty[Symbol.toStringTag], "Module");
assertEq(Object.keys(empty).length, 0);
