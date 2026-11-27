/* global ChromeUtils */

try {
  // We might be running without privileges, in which case it's up to the
  // harness to give us the 'ctypes' object.
  var { ctypes } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
  );
} catch (e) {}

function open_ctypes_test_lib() {
  return ctypes.open(do_get_file(ctypes.libraryName("jsctypes-test")).path);
}

/**
 * A weak set of CDataFinalizer values that need to be cleaned up before
 * proceeding to the next test.
 */
function ResourceCleaner() {
  this._map = new WeakMap();
}
ResourceCleaner.prototype = {
  add: function ResourceCleaner_add(v) {
    this._map.set(v);
    return v;
  },
  cleanup: function ResourceCleaner_cleanup() {
    let keys = ChromeUtils.nondeterministicGetWeakMapKeys(this._map);
    keys.forEach(k => {
      try {
        k.dispose();
      } catch (x) {
        // This can fail if |forget|/|dispose| has been called manually
        // during the test. This is normal.
      }
      this._map.delete(k);
    });
  },
};

/**
 * Simple wrapper for tests that require cleanup.
 */
function ResourceTester(start, stop) {
  this._start = start;
  this._stop = stop;
}
ResourceTester.prototype = {
  launch(size, test, args) {
    trigger_gc();
    let cleaner = new ResourceCleaner();
    this._start(size);
    try {
      test(size, args, cleaner);
    } catch (x) {
      cleaner.cleanup();
      this._stop();
      throw x;
    }
    trigger_gc();
    cleaner.cleanup();
    this._stop();
  },
};

function structural_check_eq(a, b) {
  // 1. If objects can be "toSource()-ed", use this.

  let result;
  let finished = false;
  let asource, bsource;
  try {
    asource = a.toSource();
    bsource = b.toSource();
    finished = true;
  } catch (x) {}
  if (finished) {
    Assert.equal(asource, bsource);
    return;
  }

  // 2. Otherwise, perform slower comparison

  try {
    structural_check_eq_aux(a, b);
    result = true;
  } catch (x) {
    dump(x);
    result = false;
  }
  Assert.ok(result);
}
function structural_check_eq_aux(a, b) {
  let ak;
  try {
    ak = Object.keys(a);
  } catch (x) {
    if (a != b) {
      throw new Error("Distinct values " + a, b);
    }
    return;
  }
  ak.forEach(function (k) {
    let av = a[k];
    let bv = b[k];
    structural_check_eq_aux(av, bv);
  });
}

function trigger_gc() {
  dump("Triggering garbage-collection");
  Cu.forceGC();
}

function must_throw(f, expected) {
  let has_thrown = false;
  try {
    f();
  } catch (x) {
    if (expected) {
      Assert.equal(x.toString(), expected);
    }
    has_thrown = true;
  }
  Assert.ok(has_thrown);
}

function get_os() {
  return Services.appinfo.OS;
}
