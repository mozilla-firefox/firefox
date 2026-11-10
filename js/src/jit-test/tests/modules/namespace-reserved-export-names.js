load(libdir + "asserts.js");

registerModule("target", parseModule(`
  export let f = 42;
`));

registerModule("exporter", parseModule(`
  let x = 1;
  export { x as "*namespace*" };
  export * as "*deferred-namespace*" from "target";
  export function bump() { x++; }
`));

registerModule("star-a", parseModule(`export * as shared from "target";`));
registerModule("star-b", parseModule(`export * as shared from "target";`));
registerModule("star-other", parseModule(`
  export * as shared from "exporter";
`));
registerModule("star-ok", parseModule(`
  export * from "star-a";
  export * from "star-b";
`));
registerModule("star-ambiguous", parseModule(`
  export * from "star-a";
  export * from "star-other";
`));

let main = registerModule("main", parseModule(`
  import { "*namespace*" as g, "*deferred-namespace*" as d, bump } from "exporter";
  import * as exporterNs from "exporter";
  import { shared } from "star-ok";
  import * as targetNs from "target";

  assertEq(typeof g, "number");
  assertEq(g, 1);
  bump();
  assertEq(g, 2);
  assertEq(d, targetNs);
  assertEq(d.f, 42);
  assertEq(shared, targetNs);
  assertEq(exporterNs["*namespace*"], 2);
  assertEq(exporterNs["*deferred-namespace*"], targetNs);
`));
moduleLoadAndLink(main);
moduleEvaluate(main);

let ambiguous = parseModule(`import { shared } from "star-ambiguous";`);
registerModule("ambiguous", ambiguous);
assertThrowsInstanceOf(() => {
  moduleLoadAndLink(ambiguous);
}, SyntaxError);
