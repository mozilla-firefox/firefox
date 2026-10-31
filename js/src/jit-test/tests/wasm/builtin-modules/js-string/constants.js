function wasmEvalWithConstants(text, namespace) {
  return wasmEvalText(text, {}, { importedStringConstants: namespace }).exports;
}

// Type is not anyref
assertErrorMessage(() => wasmEvalWithConstants(`(module
  (global (import "'" "") anyref)
)`, "'"), WebAssembly.CompileError, /type mismatch/);

// Type must be immutable (ref extern) or subtypes
assertErrorMessage(() => wasmEvalWithConstants(`(module
  (global (import "'" "") (mut externref))
)`, "'"), WebAssembly.CompileError, /type mismatch/);
assertErrorMessage(() => wasmEvalWithConstants(`(module
  (global (import "'" "") (mut (ref extern)))
)`, "'"), WebAssembly.CompileError, /type mismatch/);

assertErrorMessage(() => wasmEvalWithConstants(`(module
  (func (import "'" ""))
)`, "'"), WebAssembly.CompileError, /type mismatch/);

function testString(type, literal, namespace) {
  return wasmEvalWithConstants(`(module
    (global (import "${namespace}" "${literal}") ${type})
    (export "constant" (global 0))
  )`, namespace).constant.value;
}

let tests = [
  '',
  ['\\00', '\0'],
  '0',
  '0'.repeat(100000),
  '\uD83D\uDE00',
];
let namespaces = [
  "",
  "'",
  "strings"
];

for (let namespace of namespaces) {
  for (let type of ['externref', '(ref extern)']) {
    for (let test of tests) {
      let input;
      let expected;
      if (Array.isArray(test)) {
        input = test[0];
        expected = test[1];
      } else {
        input = test;
        expected = test;
      }
      assertEq(testString(type, input, namespace), expected);
    }
  }
}
