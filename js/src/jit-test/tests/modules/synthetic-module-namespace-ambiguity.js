load(libdir + "asserts.js");

registerModule("synth", createSyntheticModule({
  "*namespace*": 10,
}));

let namespaceReexport = registerModule("namespace-reexport", parseModule(`
  export * as shared from "synth";
`));

let stringReexport = registerModule("string-reexport", parseModule(`
  export { "*namespace*" as shared } from "synth";
`));

let star = registerModule("star", parseModule(`
  export * from "namespace-reexport";
  export * from "string-reexport";
`));

let importer = registerModule("importer", parseModule(`
  import { shared } from "star";
`));
assertThrowsInstanceOf(() => moduleLoadAndLink(importer), SyntaxError);
assertEq(namespaceReexport.resolveExport("shared").bindingName.namespace,
         "evaluation");
assertEq(stringReexport.resolveExport("shared").bindingName, "*namespace*");
assertEq(star.resolveExport("shared"), "ambiguous");

let starSame = registerModule("star-same", parseModule(`
  export * from "namespace-reexport";
  export * from "namespace-reexport";
`));
let sameImporter = registerModule("same-importer", parseModule(`
  import { shared } from "star-same";
  import * as ns from "synth";
  assertEq(shared, ns);
`));
moduleLoadAndLink(sameImporter);
moduleEvaluate(sameImporter);
assertEq(starSame.resolveExport("shared").bindingName.namespace, "evaluation");
