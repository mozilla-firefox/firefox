// Scope used to relaunch the tests with |ctypes| opened in a limited scope.
var ctypes = ctypes;

function run_test() {
  // Launch the test with regular loading of ctypes.sys.mjs
  main_test();

  // Relaunch the test with exotic loading of ctypes.sys.mjs
  let scope = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
  );
  ctypes = scope.ctypes;
  main_test();
}

function main_test() {
  "use strict";
  let library = open_ctypes_test_lib();
  let set_errno = library.declare(
    "set_errno",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.int
  );
  let get_errno = library.declare("get_errno", ctypes.default_abi, ctypes.int);

  for (let i = 50; i >= 0; --i) {
    set_errno(i);
    let status = ctypes.errno;
    Assert.equal(status, i);

    status = get_errno();
    Assert.equal(status, 0);

    status = ctypes.errno;
    Assert.equal(status, 0);
  }

  let set_last_error, get_last_error;
  try {
    // The following test is Windows-specific
    set_last_error = library.declare(
      "set_last_error",
      ctypes.default_abi,
      ctypes.void_t,
      ctypes.int
    );
    get_last_error = library.declare(
      "get_last_error",
      ctypes.default_abi,
      ctypes.int
    );
  } catch (x) {
    Assert.equal(ctypes.winLastError, undefined);
  }

  if (set_last_error) {
    Assert.notEqual(ctypes.winLastError, undefined);
    for (let i = 0; i < 50; ++i) {
      set_last_error(i);
      let status = ctypes.winLastError;
      Assert.equal(status, i);

      status = get_last_error();
      Assert.equal(status, 0);

      status = ctypes.winLastError;
      Assert.equal(status, 0);
    }
  }

  library.close();
}
