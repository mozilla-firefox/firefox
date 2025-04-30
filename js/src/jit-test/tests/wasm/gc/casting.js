// Test all possible casting combinations of the following graph:
//
//  A1       A2
//  |        |
//  B1       B2
//  | \      |
//  C1  C2   C3
//  | \      |
//  D1  D2   D3
//  | \      |
//  E1  E2   E3
//  | \      |
//  F1  F2   F3
//  | \      |
//  G1  G2   G3
//  | \      |
//  H1  H2   H3
//  | \      |
//  I1  I2   I3
//  | \      |
//  J1  J2   J3
//
// NOTE: this object needs to be ordered such that parent definitions come
// before children.  Note also, to be properly effective, these trees need to
// have a depth of at least MinSuperTypeVectorLength as defined in
// wasm/WasmCodegenConstants.h; keep it in sync with that.
const TYPES = {
  'A1': { super: null },
  'A2': { super: null },
  'B1': { super: 'A1' },
  'B2': { super: 'A2' },
  'C1': { super: 'B1' },
  'C2': { super: 'B1', final: true },
  'C3': { super: 'B2' },
  'D1': { super: 'C1' },
  'D2': { super: 'C1', final: true },
  'D3': { super: 'C3' },
  'E1': { super: 'D1' },
  'E2': { super: 'D1', final: true },
  'E3': { super: 'D3' },
  'F1': { super: 'E1' },
  'F2': { super: 'E1', final: true },
  'F3': { super: 'E3' },
  'G1': { super: 'F1' },
  'G2': { super: 'F1', final: true },
  'G3': { super: 'F3' },
  'H1': { super: 'G1' },
  'H2': { super: 'G1', final: true },
  'H3': { super: 'G3' },
  'I1': { super: 'H1' },
  'I2': { super: 'H1', final: true },
  'I3': { super: 'H3' },
  'J1': { super: 'I1' },
  'J2': { super: 'I1', final: true },
  'J3': { super: 'I3' },
};

// The oracle method for testing the declared subtype relationship.
function manualIsSubtype(types, subType, superType) {
  while (subType !== superType && subType.super !== null) {
    subType = types[subType.super];
  }
  return subType === superType;
}

function makeAnyModule(types) {
  let typeSection = ``;
  let funcSection = ``;
  for (let name in types) {
    let type = types[name];
    typeSection += `(type \$${name} (sub ${type.final ? "final" : ""} ${type.super ? "$" + type.super : ""} (struct)))\n`;
    funcSection += `(func (export "new${name}") (result externref)
      struct.new_default \$${name}
      extern.convert_any
    )\n`;
    funcSection += `(func (export "is${name}") (param externref) (result i32)
      local.get 0
      any.convert_extern
      ref.test (ref \$${name})
    )\n`;
  }
  // NOTE: we place all types in a single recursion group to prevent
  // canonicalization from combining them into a single type.
  return `(module
    (rec ${typeSection})
    ${funcSection}
  )`;
}

function makeFuncModule(types) {
  let typeSection = ``;
  let funcSection = ``;
  for (let name in types) {
    let type = types[name];
    typeSection += `(type \$${name} (sub ${type.final ? "final" : ""} ${type.super ? "$" + type.super : ""} (func)))\n`;
    funcSection += `(func \$f${name} (type \$${name}))\n`;
    funcSection += `(func (export "new${name}") (result funcref)
      ref.func \$f${name}
    )\n`;
    funcSection += `(func (export "is${name}") (param funcref) (result i32)
      local.get 0
      ref.test (ref \$${name})
    )\n`;
  }

  const elemSection = `(elem declare func ${Object.keys(types).map(name => `\$f${name}`).join(" ")})`;

  // NOTE: we place all types in a single recursion group to prevent
  // canonicalization from combining them into a single type.
  return `(module
    (rec ${typeSection})
    ${elemSection}
    ${funcSection}
  )`;
}

function testAllCasts(types, moduleText) {
  // Instantiate the module and acquire the testing methods
  let exports = wasmEvalText(moduleText).exports;
  for (let name in types) {
    let type = types[name];
    type['new'] = exports[`new${name}`];
    type['is'] = exports[`is${name}`];
  }

  // Test every combination of types, comparing the oracle method against the
  // JIT'ed method.
  for (let subTypeName in types) {
    let subType = types[subTypeName];
    for (let superTypeName in types) {
      let superType = types[superTypeName];
      assertEq(
        manualIsSubtype(types, subType, superType) ? 1 : 0,
        superType['is'](subType['new']()));
    }
  }
}
testAllCasts(TYPES, makeAnyModule(TYPES));
testAllCasts(TYPES, makeFuncModule(TYPES));

// Test that combinations of ref.test and ref.cast compile correctly.
// (These can be optimized together.)
{
  const { make, test1, test2, test3, test4 } = wasmEvalText(`(module
    (type $a (array i32))
    (func (export "make") (param i32) (result anyref)
      local.get 0
      local.get 0
      array.new_fixed $a 2
    )
    (func (export "test1") (param anyref) (result i32)
      (if (ref.test (ref $a) (local.get 0))
        (then
          (ref.cast (ref $a) (local.get 0))
          (array.get $a (i32.const 0))
          return
        )
      )
      i32.const -1
    )
    (func (export "test2") (param anyref) (result i32)
      (if (ref.test (ref $a) (local.get 0))
        (then)
        (else
          (ref.cast (ref $a) (local.get 0))
          (array.get $a (i32.const 0))
          return
        )
      )
      i32.const -1
    )
    (func (export "test3") (param anyref) (result i32)
      (if (ref.test (ref $a) (local.get 0))
        (then
          (if (ref.test (ref $a) (local.get 0))
            (then)
            (else
              (ref.cast (ref $a) (local.get 0))
              (array.get $a (i32.const 0))
              return
            )
          )
        )
      )
      i32.const -1
    )
    (func (export "test4") (param anyref) (result i32)
      (if (ref.test (ref $a) (local.get 0))
        (then
          (if (ref.test (ref $a) (local.get 0))
            (then
              local.get 0
              ref.cast (ref $a)
              ref.cast (ref $a)
              (array.get $a (i32.const 0))
              return
            )
          )
        )
      )
      i32.const -1
    )
  )`).exports;
  assertEq(test1(make(99)), 99);
  assertEq(test2(make(99)), -1);
  assertEq(test3(make(99)), -1);
  assertEq(test4(make(99)), 99);
}
