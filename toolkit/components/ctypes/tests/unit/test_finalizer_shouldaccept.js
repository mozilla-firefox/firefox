try {
  // We might be running without privileges, in which case it's up to the
  // harness to give us the 'ctypes' object.
  var { ctypes } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
  );
} catch (e) {}

var acquire,
  dispose,
  reset_errno,
  dispose_errno,
  dispose_ptr,
  acquire_string,
  dispose_string;

function run_test() {
  let library = open_ctypes_test_lib();

  let start = library.declare(
    "test_finalizer_start",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.size_t
  );
  let stop = library.declare(
    "test_finalizer_stop",
    ctypes.default_abi,
    ctypes.void_t
  );
  let tester = new ResourceTester(start, stop);
  acquire = library.declare(
    "test_finalizer_acq_size_t",
    ctypes.default_abi,
    ctypes.size_t,
    ctypes.size_t
  );
  dispose = library.declare(
    "test_finalizer_rel_size_t",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.size_t
  );
  reset_errno = library.declare(
    "reset_errno",
    ctypes.default_abi,
    ctypes.void_t
  );
  dispose_errno = library.declare(
    "test_finalizer_rel_size_t_set_errno",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.size_t
  );
  dispose_ptr = library.declare(
    "test_finalizer_rel_int32_ptr_t",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.int32_t.ptr
  );
  acquire_string = library.declare(
    "test_finalizer_acq_string_t",
    ctypes.default_abi,
    ctypes.char.ptr,
    ctypes.size_t
  );
  dispose_string = library.declare(
    "test_finalizer_rel_string_t",
    ctypes.default_abi,
    ctypes.void_t,
    ctypes.char.ptr
  );

  tester.launch(10, test_to_string);
  tester.launch(10, test_to_source);
  tester.launch(10, test_to_int);
  tester.launch(10, test_errno);
  tester.launch(10, test_to_pointer);
  tester.launch(10, test_readstring);
}

/**
 * Check that toString succeeds before/after forget/dispose.
 */
function test_to_string() {
  info("Starting test_to_string");
  let a = ctypes.CDataFinalizer(acquire(0), dispose);
  Assert.equal(a.toString(), "0");

  a.forget();
  Assert.equal(a.toString(), "[CDataFinalizer - empty]");

  a = ctypes.CDataFinalizer(acquire(0), dispose);
  a.dispose();
  Assert.equal(a.toString(), "[CDataFinalizer - empty]");
}

/**
 * Check that toSource succeeds before/after forget/dispose.
 */
function test_to_source() {
  info("Starting test_to_source");
  let value = acquire(0);
  let a = ctypes.CDataFinalizer(value, dispose);
  Assert.equal(
    a.toSource(),
    "ctypes.CDataFinalizer(" +
      ctypes.size_t(value).toSource() +
      ", " +
      dispose.toSource() +
      ")"
  );
  value = null;

  a.forget();
  Assert.equal(a.toSource(), "ctypes.CDataFinalizer()");

  a = ctypes.CDataFinalizer(acquire(0), dispose);
  a.dispose();
  Assert.equal(a.toSource(), "ctypes.CDataFinalizer()");
}

/**
 * Test conversion to int32
 */
function test_to_int() {
  let value = 2;
  let wrapped, converted, finalizable;
  wrapped = ctypes.int32_t(value);
  finalizable = ctypes.CDataFinalizer(acquire(value), dispose);
  converted = ctypes.int32_t(finalizable);

  structural_check_eq(converted, wrapped);
  structural_check_eq(converted, ctypes.int32_t(finalizable.forget()));

  finalizable = ctypes.CDataFinalizer(acquire(value), dispose);
  wrapped = ctypes.int64_t(value);
  converted = ctypes.int64_t(finalizable);
  structural_check_eq(converted, wrapped);
  finalizable.dispose();
}

/**
 * Test that dispose can change errno but finalization cannot
 */
function test_errno(size, tc, cleanup) {
  reset_errno();
  Assert.equal(ctypes.errno, 0);

  let finalizable = ctypes.CDataFinalizer(acquire(3), dispose_errno);
  finalizable.dispose();
  Assert.equal(ctypes.errno, 10);
  reset_errno();

  Assert.equal(ctypes.errno, 0);
  for (let i = 0; i < size; ++i) {
    finalizable = ctypes.CDataFinalizer(acquire(i), dispose_errno);
    cleanup.add(finalizable);
  }

  trigger_gc();
  Assert.equal(ctypes.errno, 0);
}

/**
 * Check that a finalizable of a pointer can be used as a pointer
 */
function test_to_pointer() {
  let ptr = ctypes.int32_t(2).address();
  let finalizable = ctypes.CDataFinalizer(ptr, dispose_ptr);
  let unwrapped = ctypes.int32_t.ptr(finalizable);

  Assert.equal("" + ptr, "" + unwrapped);

  finalizable.forget(); // Do not dispose: This is not a real pointer.
}

/**
 * Test that readstring can be applied to a finalizer
 */
function test_readstring(size) {
  for (let i = 0; i < size; ++i) {
    let acquired = acquire_string(i);
    let finalizable = ctypes.CDataFinalizer(acquired, dispose_string);
    Assert.equal(finalizable.readString(), acquired.readString());
    finalizable.dispose();
  }
}
