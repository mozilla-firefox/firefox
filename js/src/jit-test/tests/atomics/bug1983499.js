// |jit-test| test-also=--riscv-ext=rv64g --ion-eager --ion-offthread-compile=off; skip-variant-if: --riscv-ext=rv64g, !getBuildConfiguration("riscv64")

function test() {
  var i8 = new Int8Array(4);
  var i16 = new Int16Array(i8.buffer, 0, 2);

  for (var i = 0; i < 100; ++i) {
    i8.set([
      0x11,
      0x22,
      0x33,
      0x44,
    ]);

    var oldval = 0x22_11;

    // Ensure initial state is correct. (Little endian order!)
    assertEq(i16[0], oldval);
    assertEq(i16[1], 0x44_33);

    // 0x88_99_55_66 will get truncated to 0x55_66.
    var newval = 0x88_99_55_66;

    oldval = Atomics.compareExchange(i16, 0, 0x22_11, newval);

    // Assert success.
    assertEq(oldval, 0x22_11);

    // Ensure high bits 0x33_44 weren't overwritten with 0x88_99.
    assertEq(i8[0], 0x66);
    assertEq(i8[1], 0x55);
    assertEq(i8[2], 0x33);
    assertEq(i8[3], 0x44);
  }
}
test();
