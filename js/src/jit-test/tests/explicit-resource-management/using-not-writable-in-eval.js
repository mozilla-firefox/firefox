load(libdir + "asserts.js");

{
  assertThrowsInstanceOf(() => {
    using x = { [Symbol.dispose]() {} };
    (function () {
      eval("x = 3");
    })();
  }, TypeError);
}

{
  let reason;
  async function f() {
    await using x = { [Symbol.asyncDispose]() {} };
    eval("x = 4");
  }
  f().catch(e => {
    reason = e;
  });
  drainJobQueue();
  assertEq(reason instanceof TypeError, true);
}
