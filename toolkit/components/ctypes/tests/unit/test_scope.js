add_task(async function test_global_prop() {
  // Import the ctypes module, in order to test the effect on the shared system
  // global.
  const { ctypes } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
  );
  void ctypes;

  const sharedSystemGlobal = Cu.getGlobalForObject(Services);

  const desc = Object.getOwnPropertyDescriptor(sharedSystemGlobal, "ctypes");
  Assert.equal(
    desc,
    undefined,
    "ctypes property shouldn't leak the to the shared system global"
  );
});
