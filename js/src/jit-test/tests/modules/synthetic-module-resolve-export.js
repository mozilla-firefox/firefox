load(libdir + "asserts.js");

function assertBinding(module, exportName, bindingName) {
  let binding = module.resolveExport(exportName);
  assertEq(typeof binding, "object");
  assertEq(JSON.stringify(binding.bindingName), JSON.stringify(bindingName));
}

let synth = registerModule("synth", createSyntheticModule({
  "*namespace*": 10,
  "*deferred-namespace*": 20,
  other: 30,
}));

assertBinding(synth, "*namespace*", "*namespace*");
assertBinding(synth, "*deferred-namespace*", "*deferred-namespace*");
assertBinding(synth, "other", "other");
assertEq(synth.resolveExport("missing"), null);

let m = registerModule("m", parseModule(`
  export * as synthNs from "synth";
  export { "*namespace*" as synthStr } from "synth";
  export { other as synthOther } from "synth";
`));
moduleLoadAndLink(m);

assertBinding(m, "synthNs", { namespace: "evaluation" });
assertBinding(m, "synthStr", "*namespace*");
assertBinding(m, "synthOther", "other");
