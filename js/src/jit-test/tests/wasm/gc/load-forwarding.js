// Store-to-load forwarding and redundant load elimination of wasm GC struct
// fields.

const jitspew = getBuildConfiguration("jitspew");

function optimizedMIR(text) {
  return wasmIonGetLastMIRPass(wasmGetIon(wasmTextToBinary(text), 0));
}

// Returns the exports of `text`. In jitspew builds, also checks the number of
// WasmLoadField and WasmLoadInternalField left in the optimized MIR of
// function 0. An undefined count is not checked.
function compile(text, fieldLoads, internalLoads) {
  if (jitspew) {
    const opcodes = wasmIonGetAllOpcodes(optimizedMIR(text));
    const count = name => opcodes.filter(op => op.split(" ")[0] === name).length;
    if (fieldLoads !== undefined) {
      assertEq(count("WasmLoadField"), fieldLoads, text);
    }
    if (internalLoads !== undefined) {
      assertEq(count("WasmLoadInternalField"), internalLoads, text);
    }
  }
  return wasmEvalText(text).exports;
}

// Expressions that all evaluate to $p.
const aliases = [
  "(local.get $p)",
  "(ref.as_non_null (local.get $p))",
  "(ref.cast (ref $s) (local.get $p))",
  "(ref.cast (ref $s) (ref.cast (ref struct) (local.get $p)))",
  "(ref.cast (ref $s) (any.convert_extern (extern.convert_any (local.get $p))))",
];

function forEachAliasPair(f) {
  for (const A of aliases) {
    for (const B of aliases) {
      f(A, B);
    }
  }
}

// Struct layouts. `pad` and `padArgs` are prepended to the fields and to the
// struct.new operands, and `f` is the index of the first field after them. In
// the OOL layout, field `f` is stored out-of-line, and accessing it loads the
// data pointer, which is expected to be loaded only once.
const layouts = [
  { pad: "", padArgs: "", f: 0, dataLoads: 0 },
  { pad: "(field i32)".repeat(40), padArgs: "(i32.const 0)".repeat(40), f: 40,
    dataLoads: 1 },
];

// Two loads of a field become one, whether it is mutable or not. The nullable
// base makes the loads guards, and null must still trap.
for (const { pad, padArgs, f, dataLoads } of layouts) {
  for (const type of ["i32", "(mut i32)"]) {
    forEachAliasPair((A, B) => {
      const { test, make } = compile(`(module
        (type $s (struct ${pad} (field ${type})))
        (func (export "test") (param $p (ref null $s)) (result i32)
          (i32.add (struct.get $s ${f} ${A}) (struct.get $s ${f} ${B})))
        (func (export "make") (result (ref $s))
          (struct.new $s ${padArgs} (i32.const 21))))`,
        1, dataLoads);
      assertEq(test(make()), 42);
      assertErrorMessage(() => test(null), WebAssembly.RuntimeError, /./);
    });
  }
}

// A load following a store to the same field is replaced by the stored value.
for (const { pad, padArgs, f, dataLoads } of layouts) {
  forEachAliasPair((A, B) => {
    const { test, make } = compile(`(module
      (type $s (struct ${pad} (field (mut i32))))
      (func (export "test") (param $p (ref null $s)) (result i32)
        (struct.set $s ${f} ${A} (i32.const 42))
        (struct.get $s ${f} ${B}))
      (func (export "make") (result (ref $s))
        (struct.new $s ${padArgs} (i32.const 7))))`,
      0, dataLoads);
    assertEq(test(make()), 42);
    assertErrorMessage(() => test(null), WebAssembly.RuntimeError, /./);
  });
}

// A store between loads of two fields: the loads of the other field become one,
// and the second load of the stored field is forwarded from the store, not from
// the first load.
forEachAliasPair((A, B) => {
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32)) (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (result i32)
      (local $x i32)
      (local.set $x (i32.add (struct.get $s 0 ${A}) (struct.get $s 1 ${A})))
      (struct.set $s 1 ${B} (i32.const 42))
      (i32.add (local.get $x)
               (i32.add (struct.get $s 0 ${A}) (struct.get $s 1 ${A}))))
    (func (export "make") (result (ref $s))
      (struct.new $s (i32.const 7) (i32.const 5))))`,
    2);
  assertEq(test(make()), 61);
});

// A call may write the mutable field, so it is loaded again, but cannot write
// the immutable one, so its two loads become one.
for (const { pad, padArgs, f, dataLoads } of layouts) {
  forEachAliasPair((A, B) => {
    const { test, make } = compile(`(module
      (type $s (struct ${pad} (field (mut i32)) (field i32)))
      (func (export "test") (param $p (ref $s)) (result i32)
        (local $x i32)
        (local.set $x
          (i32.add (struct.get $s ${f} ${A}) (struct.get $s ${f + 1} ${A})))
        (call $set ${A})
        (i32.add (local.get $x)
          (i32.add (struct.get $s ${f} ${B}) (struct.get $s ${f + 1} ${B}))))
      (func $set (param (ref $s))
        (struct.set $s ${f} (local.get 0) (i32.const 42)))
      (func (export "make") (result (ref $s))
        (struct.new $s ${padArgs} (i32.const 7) (i32.const 21))))`,
      3, dataLoads);
    assertEq(test(make()), 91);
  });
}

// A store that does not dominate the load is not forwarded.
forEachAliasPair((A, B) => {
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (param $c i32) (result i32)
      (if (local.get $c) (then (struct.set $s 0 ${A} (i32.const 42))))
      (struct.get $s 0 ${B}))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`,
    1);
  assertEq(test(make(), 1), 42);
  assertEq(test(make(), 0), 7);
});

// A store to $q is not forwarded to a load from $p, and does not let two loads
// from $p become one, since $q may be $p.
forEachAliasPair((A, B) => {
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (param $q (ref $s)) (result i32)
      (local $x i32)
      (local.set $x (struct.get $s 0 ${A}))
      (struct.set $s 0 (local.get $q) (i32.const 99))
      (i32.add (local.get $x) (struct.get $s 0 ${B})))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`,
    2);
  assertEq(test(make(), make()), 14);
  const o = make();
  assertEq(test(o, o), 106);
});

// A store through a cast to a supertype is forwarded to a load through a cast
// to a subtype, since both resolve to the same object. The subtype cast must
// survive the forwarding and still trap.
{
  const { test, makeSuper, makeSub } = compile(`(module
    (type $super (sub (struct (field (mut i32)))))
    (type $sub (sub $super (struct (field (mut i32)) (field i32))))
    (func (export "test") (param anyref) (result i32)
      (struct.set $super 0 (ref.cast (ref $super) (local.get 0)) (i32.const 42))
      (struct.get $sub 0 (ref.cast (ref $sub) (local.get 0))))
    (func (export "makeSuper") (result anyref) (struct.new $super (i32.const 7)))
    (func (export "makeSub") (result anyref)
      (struct.new $sub (i32.const 7) (i32.const 0))))`,
    0);
  assertEq(test(makeSub()), 42);
  assertErrorMessage(() => test(makeSuper()), WebAssembly.RuntimeError,
                     /bad cast/);
}

// An inline field and an OOL field can have the same offset. A load of the
// inline field must not be merged with a load of the OOL field, and a store to
// the inline field must not be forwarded to a load of the OOL field.
{
  const n = 20;
  let funcs = "";
  for (let k = 1; k < n; k++) {
    funcs += `
      (func (export "load${k}") (param (ref $s)) (result i64)
        (i64.add (struct.get $s 0 (local.get 0))
                 (struct.get $s ${k} (local.get 0))))
      (func (export "store${k}") (param (ref $s)) (result i64)
        (struct.set $s 0 (local.get 0) (i64.const 1000))
        (struct.get $s ${k} (local.get 0)))`;
  }
  const args = Array.from({ length: n }, (_, i) => `(i64.const ${i})`).join("");
  const exports = wasmEvalText(`(module
    (type $s (struct ${"(field (mut i64))".repeat(n)}))
    (func (export "make") (result (ref $s)) (struct.new $s ${args}))
    ${funcs})`).exports;
  for (let k = 1; k < n; k++) {
    assertEq(exports[`load${k}`](exports.make()), BigInt(k));
    assertEq(exports[`store${k}`](exports.make()), BigInt(k));
  }
}

// A packed field narrows on the way in and widens on the way out, so the
// stored value must not be forwarded as-is, and loads with different
// widenings are distinct.
{
  const { make, testS8, testU8, testS16, testSU } = compile(`(module
    (type $s (struct (field (mut i8)) (field (mut i16))))
    (func (export "testS8") (param (ref $s)) (result i32)
      (struct.set $s 0 (local.get 0) (i32.const 0x1ff))
      (struct.get_s $s 0 (local.get 0)))
    (func (export "testU8") (param (ref $s)) (result i32)
      (struct.set $s 0 (local.get 0) (i32.const 0x1ff))
      (struct.get_u $s 0 (local.get 0)))
    (func (export "testS16") (param (ref $s)) (result i32)
      (struct.set $s 1 (local.get 0) (i32.const 0x1ffff))
      (struct.get_s $s 1 (local.get 0)))
    (func (export "testSU") (param (ref $s)) (result i32)
      (i32.add (struct.get_s $s 0 (local.get 0))
               (struct.get_u $s 0 (local.get 0))))
    (func (export "make") (result (ref $s))
      (struct.new $s (i32.const -1) (i32.const 0))))`, 1);
  assertEq(testS8(make()), -1);
  assertEq(testU8(make()), 0xff);
  assertEq(testS16(make()), -1);
  assertEq(testSU(make()), 254);
}
compile(`(module
  (type $s (struct (field i8)))
  (func (param (ref $s)) (result i32)
    (i32.add (struct.get_s $s 0 (local.get 0))
             (struct.get_u $s 0 (local.get 0)))))`, 2);

// A stored reference is forwarded to a load of the same field, and the load
// from the struct it points to is forwarded from its initializing store.
{
  const { make, test } = compile(`(module
    (type $t (struct (field i32)))
    (type $s (struct (field (mut (ref null $t)))))
    (func (export "test") (param (ref $s)) (result i32)
      (struct.set $s 0 (local.get 0) (struct.new $t (i32.const 42)))
      (struct.get $t 0 (ref.as_non_null (struct.get $s 0 (local.get 0)))))
    (func (export "make") (result (ref $s))
      (struct.new $s (ref.null $t))))`, 0);
  assertEq(test(make()), 42);
}

// Two unrelated struct types with field 0 at the same offset but of a
// different value type, reached through casts of the same anyref. Both loads
// resolve to the same object, so only their result type keeps them apart. The
// second cast traps.
{
  const { test, make } = compile(`(module
    (type $a (struct (field i32)))
    (type $b (struct (field f32)))
    (func (export "test") (param anyref) (result f32)
      (f32.add
        (f32.convert_i32_s (struct.get $a 0 (ref.cast (ref $a) (local.get 0))))
        (struct.get $b 0 (ref.cast (ref $b) (local.get 0)))))
    (func (export "make") (result anyref) (struct.new $a (i32.const 1))))`, 2);
  assertErrorMessage(() => test(make()), WebAssembly.RuntimeError, /bad cast/);
}

// Two unrelated struct types with a mutable field at the same offset: a store
// through one does not prevent two loads through the other from becoming one.
// The types must differ structurally, otherwise they are canonicalized to the
// same type.
{
  const { test, makeA, makeB } = compile(`(module
    (type $a (struct (field (mut i32)) (field i32)))
    (type $b (struct (field (mut i32))))
    (func (export "test") (param (ref $a)) (param (ref $b)) (result i32)
      (local $x i32)
      (local.set $x (struct.get $b 0 (local.get 1)))
      (struct.set $a 0 (local.get 0) (i32.const 99))
      (i32.add (local.get $x) (struct.get $b 0 (local.get 1))))
    (func (export "makeA") (result (ref $a))
      (struct.new $a (i32.const 1) (i32.const 0)))
    (func (export "makeB") (result (ref $b)) (struct.new $b (i32.const 7))))`, 1);
  assertEq(test(makeA(), makeB()), 14);
}

// A subtype can narrow the type of a field. The anyref stored through $parent
// must not be forwarded to the (ref i31) load through $child, since the stored
// value is not a subtype of the type of the load. A stored (ref i31) can be
// forwarded.
{
  const types = `
    (type $parent (sub (struct (field anyref))))
    (type $child (sub $parent (struct (field (ref i31)))))`;
  const { test } = compile(`(module ${types}
    (func (export "test") (param $p externref) (result i32)
      (local $conv anyref)
      (local $tmp anyref)
      (local.set $conv (any.convert_extern (local.get $p)))
      (local.set $tmp (struct.new $parent (local.get $conv)))
      (if (ref.test (ref $child) (local.get $tmp))
        (then
          (return (i31.get_s
            (struct.get $child 0 (ref.cast (ref $child) (local.get $tmp)))))))
      (ref.test (ref i31) (local.get $conv))))`, 1);
  assertEq(test({}), 0);
  assertEq(test(null), 0);
  assertEq(test(5), 1);

  compile(`(module ${types}
    (func (param $p i32) (result i32)
      (local $tmp anyref)
      (local.set $tmp (struct.new $parent (ref.i31 (local.get $p))))
      (if (ref.test (ref $child) (local.get $tmp))
        (then
          (return (i31.get_s
            (struct.get $child 0 (ref.cast (ref $child) (local.get $tmp)))))))
      (i32.const 0)))`, 0);

  // Loads of the same field through $parent and $child have different wasm ref
  // types, so they are not merged and the narrower type of the load through
  // $child is kept.
  compile(`(module ${types}
    (func (param $p (ref $parent)) (result i32)
      (drop (ref.test (ref i31) (struct.get $parent 0 (local.get $p))))
      (i31.get_s (struct.get $child 0 (ref.cast (ref $child) (local.get $p))))))`,
    2);
}

// The store and the final load reach the object through two different casts
// of the same anyref. The store is still forwarded to the final load, which is
// not merged with the earlier load.
{
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param anyref) (result i32)
      (local $r (ref $s))
      (local.set $r (ref.cast (ref $s) (local.get 0)))
      (drop (struct.get $s 0 (local.get $r)))
      (struct.set $s 0 (local.get $r) (i32.const 42))
      (struct.get $s 0 (ref.cast (ref $s) (local.get 0))))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 0))))`, 1);
  assertEq(test(make()), 42);
}

// A load from a null of the bottom type has no struct type to take the field
// mutability from.
{
  const { test, make } = wasmEvalText(`(module
    (type $s (struct (field (mut i32)) (field i32)))
    (func (export "test") (param (ref $s)) (result i32)
      (drop (struct.get $s 1 (ref.null none)))
      (struct.set $s 0 (local.get 0) (i32.const 1))
      (struct.get $s 1 (ref.null none)))
    (func (export "make") (result (ref $s))
      (struct.new $s (i32.const 0) (i32.const 0))))`).exports;
  assertErrorMessage(() => test(make()), WebAssembly.RuntimeError, /null/);
}

// $b is a phi of $a and a new struct. A store through either must be seen by
// a load through the other.
for (const [store, load, ifNew] of [["$a", "$b", 99], ["$b", "$a", 7]]) {
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $a (ref $s)) (param $cond i32) (result i32)
      (local $b (ref null $s))
      (if (local.get $cond)
        (then (local.set $b (local.get $a)))
        (else (local.set $b (struct.new $s (i32.const 99)))))
      (drop (struct.get $s 0 (local.get ${load})))
      (struct.set $s 0 (local.get ${store}) (i32.const 42))
      (struct.get $s 0 (local.get ${load})))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`, 2);
  assertEq(test(make(), 1), 42);
  assertEq(test(make(), 0), ifNew);
}

// A call with no ref-typed operands can still mutate struct fields through a
// global.
{
  const { test, init } = compile(`(module
    (type $s (struct (field (mut i32))))
    (global $g (mut (ref null $s)) (ref.null $s))
    (func (export "test") (result i32)
      (local $a (ref $s))
      (local.set $a (ref.as_non_null (global.get $g)))
      (drop (struct.get $s 0 (local.get $a)))
      (call $modify)
      (struct.get $s 0 (local.get $a)))
    (func $modify
      (struct.set $s 0 (ref.as_non_null (global.get $g)) (i32.const 42)))
    (func (export "init") (global.set $g (struct.new $s (i32.const 7)))))`, 2);
  init();
  assertEq(test(), 42);
}

// A load in a loop reads what the previous iteration stored.
{
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (param $n i32) (result i32)
      (local $i i32)
      (block $done
        (loop $loop
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (struct.set $s 0 (local.get $p)
            (i32.add (struct.get $s 0 (local.get $p)) (i32.const 1)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $loop)))
      (struct.get $s 0 (local.get $p)))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 0))))`);
  assertEq(test(make(), 5), 5);
  assertEq(test(make(), 0), 0);
}

// A new struct is distinct from a parameter and from another new struct, so
// a store to either does not stop the load from the new struct from being
// forwarded from its initializing store. The structs escape after the load,
// otherwise the allocations are removed outright. In the OOL layout, the data
// pointer of each struct is loaded once.
for (const { pad, padArgs, f, dataLoads } of layouts) {
  for (const other of ["(local.get $p)",
                       `(struct.new $s ${padArgs} (i32.const 0))`]) {
    const { test, make } = compile(`(module
      (type $s (struct ${pad} (field (mut i32))))
      (func (export "test") (param $p (ref $s)) (result i32)
        (local $n (ref $s))
        (local $o (ref $s))
        (local $r i32)
        (local.set $n (struct.new $s ${padArgs} (i32.const 7)))
        (local.set $o ${other})
        (struct.set $s ${f} (local.get $o) (i32.const 99))
        (local.set $r (struct.get $s ${f} (local.get $n)))
        (call $escape (local.get $n))
        (call $escape (local.get $o))
        (local.get $r))
      (func $escape (param (ref $s)))
      (func (export "make") (result (ref $s))
        (struct.new $s ${padArgs} (i32.const 0))))`, 0, 2 * dataLoads);
    assertEq(test(make()), 7);
  }
}

// A new struct cannot be a parameter, so a store to a new struct does not
// prevent two loads from a parameter from becoming one.
{
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (result i32)
      (local $n (ref $s))
      (local $x i32)
      (local.set $n (struct.new $s (i32.const 0)))
      (local.set $x (struct.get $s 0 (local.get $p)))
      (struct.set $s 0 (local.get $n) (i32.const 99))
      (local.set $x (i32.add (local.get $x) (struct.get $s 0 (local.get $p))))
      (call $escape (local.get $n))
      (local.get $x))
    (func $escape (param (ref $s)))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`,
    1);
  assertEq(test(make()), 14);
}

// An immutable field of a new struct is forwarded from its initializing store,
// even across a call.
for (const { pad, padArgs, f } of layouts) {
  const { test } = compile(`(module
    (type $s (struct ${pad} (field i32)))
    (func (export "test") (param i32) (result i32)
      (local $n (ref $s))
      (local.set $n (struct.new $s ${padArgs} (local.get 0)))
      (call $escape (local.get $n))
      (struct.get $s ${f} (local.get $n)))
    (func $escape (param (ref $s))))`, 0);
  assertEq(test(42), 42);
}

// Loads of the same field from two different objects are not merged.
for (const { pad, padArgs, f, dataLoads } of layouts) {
  const { test, make } = compile(`(module
    (type $s (struct ${pad} (field i32)))
    (func (export "test") (param $p (ref $s)) (param $q (ref $s)) (result i32)
      (i32.sub (struct.get $s ${f} (local.get $p))
               (struct.get $s ${f} (local.get $q))))
    (func (export "make") (param i32) (result (ref $s))
      (struct.new $s ${padArgs} (local.get 0))))`, 2, 2 * dataLoads);
  assertEq(test(make(10), make(3)), 7);
}

// A store is forwarded to a load in a block it dominates.
{
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (param $c i32) (result i32)
      (struct.set $s 0 (local.get $p) (i32.const 42))
      (if (local.get $c)
        (then (return (struct.get $s 0 (local.get $p)))))
      (i32.const 0))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`,
    0);
  assertEq(test(make(), 1), 42);
  assertEq(test(make(), 0), 0);
}

// After stores in both branches, the load must not be forwarded from either.
{
  const { test, make } = compile(`(module
    (type $s (struct (field (mut i32))))
    (func (export "test") (param $p (ref $s)) (param $c i32) (result i32)
      (if (local.get $c)
        (then (struct.set $s 0 (local.get $p) (i32.const 1)))
        (else (struct.set $s 0 (local.get $p) (i32.const 2))))
      (struct.get $s 0 (local.get $p)))
    (func (export "make") (result (ref $s)) (struct.new $s (i32.const 7))))`,
    1);
  assertEq(test(make(), 1), 1);
  assertEq(test(make(), 0), 2);
}

// A store shadowed by a later store at the same offset to a different object.
// The two objects are only provably distinct once the ref-typed load feeding
// the object operand has itself been forwarded.
{
  const { test } = compile(`(module
    (type $a (struct (field i32)))
    (type $s (struct (field (ref $a)) (field (ref $a))))
    (func (export "test") (result i32)
      (local $s (ref null $s))
      (local.set $s (struct.new $s (struct.new $a (i32.const 100))
                                   (struct.new $a (i32.const 200))))
      (call $escape (local.get $s))
      (struct.get $a 0 (struct.get $s 0 (ref.as_non_null (local.get $s)))))
    (func $escape (param anyref)))`);
  assertEq(test(), 100);
}

// Array lengths and data pointers are commoned through casts.
{
  const { test, make } = compile(`(module
    (type $a (array (mut i32)))
    (func (export "test") (param $p (ref $a)) (result i32)
      (i32.add
        (i32.add (array.len (local.get $p))
                 (array.len (ref.cast (ref $a) (local.get $p))))
        (i32.add (array.get $a (local.get $p) (i32.const 1))
                 (array.get $a (ref.cast (ref $a) (local.get $p)) (i32.const 1)))))
    (func (export "make") (result (ref $a))
      (array.new $a (i32.const 5) (i32.const 4))))`, undefined, 2);
  assertEq(test(make()), 18);
}

// An array element store lives in a different alias set than a struct field.
{
  const { test, makeS, makeA } = compile(`(module
    (type $s (struct (field (mut i32))))
    (type $a (array (mut i32)))
    (func (export "test") (param $p (ref $s)) (param $q (ref $a)) (result i32)
      (i32.add (struct.get $s 0 (local.get $p))
        (block (result i32)
          (array.set $a (local.get $q) (i32.const 0) (i32.const 99))
          (struct.get $s 0 (local.get $p)))))
    (func (export "makeS") (result (ref $s)) (struct.new $s (i32.const 7)))
    (func (export "makeA") (result (ref $a))
      (array.new $a (i32.const 0) (i32.const 4))))`, 1);
  assertEq(test(makeS(), makeA()), 14);
}

// Each caught exception is a different object, so the data pointer loads of
// two catches are not merged. In a loop, the catch keeps its own field and data
// pointer loads.
{
  const { test } = compile(`(module
    (tag $t (param i32))
    (func (export "test") (param i32) (result i32)
      (i32.add
        (block $h (result i32)
          (try_table (catch $t $h) (call $throw (local.get 0)))
          (unreachable))
        (block $h (result i32)
          (try_table (catch $t $h) (call $throw (i32.const 10)))
          (unreachable))))
    (func $throw (param i32) (throw $t (local.get 0))))`, 2, 2);
  assertEq(test(1), 11);
}
{
  const { test } = compile(`(module
    (tag $t (param i32))
    (func (export "test") (param $n i32) (result i32)
      (local $sum i32)
      (loop $loop
        (block $h (result i32)
          (try_table (catch $t $h) (call $throw (local.get $n)))
          (unreachable))
        (local.set $sum (i32.add (local.get $sum)))
        (br_if $loop (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
      (local.get $sum))
    (func $throw (param i32) (throw $t (local.get 0))))`, 1, 1);
  assertEq(test(4), 10);
}

// The outline data pointer load after the cast is merged with the one before
// the loop. The field load, only valid on $b, must still depend on the cast
// through its wasmObject operand, so that it cannot be hoisted above it.
if (jitspew) {
  const text = `(module
    (type $a (sub (struct ${"(field i32)".repeat(60)})))
    (type $b (sub $a (struct ${"(field i32)".repeat(60)} (field i64))))
    (func (export "test") (param $p (ref $a)) (param $n i32) (result i64)
      (local $r i64)
      (drop (struct.get $a 59 (local.get $p)))
      loop
        (if (ref.test (ref $b) (local.get $p))
          (then
            (local.set $r
              (struct.get $b 60 (ref.cast (ref $b) (local.get $p))))))
        (local.tee $n (i32.sub (local.get $n) (i32.const 1)))
        br_if 0
      end
      (local.get $r)))`;
  const instructions = wasmIonGetAllInstructions(optimizedMIR(text));
  const opName = ins => ins.opcode.split(" ")[0];
  const byId = new Map(instructions.map(ins => [ins.id, ins]));
  const cast = instructions.find(ins => opName(ins) === "WasmRefCastInfallible");
  assertEq(cast !== undefined, true);
  const fieldLoad = instructions.find(ins =>
    opName(ins) === "WasmLoadField" &&
    opName(byId.get(ins.inputs[0])) === "WasmLoadInternalField" &&
    ins.inputs[1] === cast.id);
  assertEq(fieldLoad !== undefined, true);
  assertEq(byId.get(fieldLoad.inputs[0]).inputs[0] !== cast.id, true);
}
