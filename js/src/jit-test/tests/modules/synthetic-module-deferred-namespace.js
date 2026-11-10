// |jit-test| skip-if: getBuildConfiguration("release_or_beta"); --enable-defer-import-eval

load(libdir + "asserts.js");

registerModule("synth", createSyntheticModule({
  "*namespace*": 10,
  "*deferred-namespace*": 20,
  other: 30,
}));

registerModule("reexporter", parseModule(`
  import defer * as deferred from "synth";
  import * as evaluated from "synth";
  export { deferred, evaluated };
`));

let main = registerModule("main", parseModule(`
  import { deferred, evaluated } from "reexporter";
  import { "*namespace*" as g, "*deferred-namespace*" as d } from "synth";

  assertEq(g, 10);
  assertEq(d, 20);
  assertEq(deferred.other, 30);
  assertEq(deferred["*namespace*"], 10);
  assertEq(deferred["*deferred-namespace*"], 20);
  assertEq(evaluated["*namespace*"], 10);
  assertEq(deferred === evaluated, false);
`));
moduleLoadAndLink(main);
moduleEvaluate(main);

let reexporter = registerModule("reexporter2", parseModule(`
  import defer * as deferred from "synth";
  export { deferred };
`));
moduleLoadAndLink(reexporter);
let binding = reexporter.resolveExport("deferred");
assertEq(binding.bindingName.namespace, "defer");
