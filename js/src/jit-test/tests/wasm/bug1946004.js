let code = `
(module
  (type (array i32))
  (func (result i32)
    i64.const 0
    i64.const 0
    call 2
    drop
    drop
    i32.const 0
    i32.const 0
    call 1
    drop)
  (func (param i32 i32) (result i32 i32)
    loop (result i32)
      i32.const 3
    end
    loop (result i32)
      i32.const 4
    end)
  (func (param i64 i64) (result i64 i64)
    loop (result i64)
      i64.const 0xffff_ffff_ffff_ffff
    end
    loop (result i64)
      i64.const 0xffff_ffff_ffff_ffff
    end)
  (export "main" (func 0)))
`;
let bin = wasmTextToBinary(code);
let module = new WebAssembly.Module(bin);
let instance = new WebAssembly.Instance(module);
for (var i = 0; i < 10000; i++) {
  assertEq(instance.exports.main(), 3);
}
