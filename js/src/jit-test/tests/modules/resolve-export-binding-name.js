load(libdir + "asserts.js");

function assertBinding(module, exportName, bindingName) {
  let binding = module.resolveExport(exportName);
  assertEq(typeof binding, "object");
  assertEq(JSON.stringify(binding.bindingName), JSON.stringify(bindingName));
}

registerModule("target", parseModule(`export let f = 42;`));

let m = registerModule("m", parseModule(`
  let x = 1;
  export { x as "*namespace*" };
  export { x as "*deferred-namespace*" };
  export * as ns from "target";
  export * as "*namespace-star*" from "target";
  export { f as viaF } from "target";
`));
moduleLoadAndLink(m);

assertBinding(m, "*namespace*", "x");
assertBinding(m, "*deferred-namespace*", "x");
assertBinding(m, "viaF", "f");
assertBinding(m, "ns", { namespace: "evaluation" });
assertBinding(m, "*namespace-star*", { namespace: "evaluation" });
assertEq(m.resolveExport("missing"), null);

let ambiguous = registerModule("ambiguous", parseModule(`
  export * from "m";
  export * from "other";
`));
registerModule("other", parseModule(`export let viaF = 1;`));
moduleLoadAndLink(ambiguous);
assertEq(ambiguous.resolveExport("viaF"), "ambiguous");

assertThrowsInstanceOf(() => m.resolveExport(1), Error);
assertThrowsInstanceOf(
  () => parseModule(`export let y = 1;`).resolveExport("y"), Error);
