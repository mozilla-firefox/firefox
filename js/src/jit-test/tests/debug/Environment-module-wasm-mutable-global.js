// |jit-test| skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// The debugger must observe the current value of an imported Wasm mutable
// global, and must never see the internal binding object behind it.

var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);
var gDO = dbg.addDebuggee(g);

const m = g.parseModule(`
  import { g as value, set } from "exports-mutable-global.wasm";
  import * as ns from "exports-mutable-global.wasm";
  export function check() {
    debugger;
    return value;
  }
  globalThis.check = check;
  globalThis.set = set;
  globalThis.ns = ns;
`);
moduleLoadAndLink(m);
moduleEvaluate(m);
drainJobQueue();

let expected;
let hits = 0;
dbg.onDebuggerStatement = function (frame) {
  hits++;
  const env = frame.environment.find("value");
  assertEq(env.getVariable("value"), expected);
  assertEq(frame.eval("value").return, expected);
  assertEq(frame.eval("ns.g").return, expected);
};

const nsDO = gDO.getOwnPropertyDescriptor("ns").value;

for (const v of [1, 42, -7, 0x7fffffff]) {
  g.set(v);
  expected = v;
  assertEq(g.check(), v);
  assertEq(nsDO.getProperty("g").return, v);
  assertEq(nsDO.getOwnPropertyDescriptor("g").value, v);
}
assertEq(hits, 4);
