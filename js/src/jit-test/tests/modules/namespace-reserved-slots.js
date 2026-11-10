// |jit-test| skip-if: getBuildConfiguration("release_or_beta"); --enable-defer-import-eval

registerModule("empty", createSyntheticModule({}));

registerModule("reexporter", parseModule(`
  export * as evaluated from "empty";
  import defer * as deferred from "empty";
  export { deferred };
`));

registerModule("target", parseModule(`export let x = 1;`));

registerModule("source-reexporter", parseModule(`
  export * as ns from "target";
`));

let main = registerModule("main", parseModule(`
  import { evaluated, deferred } from "reexporter";
  import { ns } from "source-reexporter";
  import * as targetNs from "target";

  assertEq(Object.keys(evaluated).length, 0);
  assertEq(typeof deferred, "object");
  assertEq(deferred === evaluated, false);
  assertEq(ns, targetNs);
  assertEq(eval("ns"), targetNs);
  assertEq(eval("evaluated"), evaluated);
`));
moduleLoadAndLink(main);
moduleEvaluate(main);

assertEq(getModuleEnvironmentNames(registerModule("plain", parseModule(`
  export let a = 1;
`))).join(), "a");
