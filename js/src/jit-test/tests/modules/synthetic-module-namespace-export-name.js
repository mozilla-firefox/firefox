load(libdir + "asserts.js");

registerModule("synth", createSyntheticModule({
  "*namespace*": 10,
  "*deferred-namespace*": 20,
  other: 30,
}));

registerModule("reexporter", parseModule(`
  export * as inner from "synth";
`));

let main = parseModule(`
  import { "*namespace*" as g, "*deferred-namespace*" as d, other } from "synth";
  import * as ns from "synth";
  import { inner } from "reexporter";

  assertEq(typeof g, "number");
  assertEq(g, 10);
  assertEq(d, 20);
  assertEq(other, 30);

  assertEq(ns["*namespace*"], 10);
  assertEq(ns["*deferred-namespace*"], 20);
  assertEq(Object.keys(ns).sort().join(), "*deferred-namespace*,*namespace*,other");

  assertEq(typeof inner, "object");
  assertEq(inner === ns, true);
  assertEq(inner["*namespace*"], 10);
  assertEq(inner["*deferred-namespace*"], 20);
`);
registerModule("main", main);
moduleLoadAndLink(main);
moduleEvaluate(main);
