# Writing tests with Mochia

Mochia is a BDD-style API wrapper for `add_task` that provides `describe`,
`beforeEach`, `afterEach`, and `it` as test globals in
[browser chrome mochitests](../browser-chrome/index.md) and
[xpcshell tests](../xpcshell/index.md). Use Mochia to group
related scenarios and tests that share per-test setup and teardown.

Use `add_setup` and `registerCleanupFunction` for file-wide
setup and cleanup. Use `beforeEach` and `afterEach` for per-test
setup and cleanup.

## API

| Function | What it does |
| --- | --- |
| `describe(description, suite)` | Defines a named suite. Calls `suite` immediately to register hooks, tests, and nested suites. |
| `beforeEach(setup)` | Registers setup for each test in the current suite and its nested suites. |
| `afterEach(teardown)` | Registers teardown for each test in the current suite and its nested suites. |
| `it(description, test)` | Registers a test through `add_task`. |

You can call `it` outside a `describe`. Hooks registered at the top
level apply to tests and suites declared after them. Keep `describe`
callbacks synchronous so registration completes before the next suite
or test is declared.

### Selecting tests

- `it.only(description, test)` selects a single test in the file for
  local debugging. Remove `.only` before submitting the patch.
- `it.skip(description, test)` registers a test and marks it as skipped.
- `it.skipIf(description, shouldSkip, test)` skips the test if `shouldSkip()`
  returns a truthy value.

## Lifecycle

Declare hooks before the `it` and nested `describe` calls that need
them. A nested suite inherits the hooks already registered in its parent
when `describe` is called. Each `it` captures the hooks available in
its suite at the time of the `it` call.

Each test that runs follows this order.

1. Run `beforeEach` hooks from the outermost suite to the innermost.
   Within a suite, run setup hooks in registration order.
2. Run the `it` callback.
3. Run `afterEach` hooks from the innermost suite to the outermost.
   Within a suite, run teardown hooks in reverse registration order.

Mochia waits for promises that are returned by hooks or tests. If setup
throws or rejects Mochia skips remaining setup hooks and the test body.
Teardown still runs when setup or the test fails, so teardown hooks should
account for partial setup state. If a teardown hook fails
Mochia reports the failure and runs the remaining teardown hooks.

## Browser chrome example

```js
describe("Tab pinning", () => {
  let tab;

  beforeEach(async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
  });

  afterEach(async () => {
    if (tab) {
      await BrowserTestUtils.removeTab(tab);
      tab = null;
    }
  });

  describe("when the tab is pinned", () => {
    beforeEach(() => {
      gBrowser.pinTab(tab);
    });

    it("can be unpinned", () => {
      gBrowser.unpinTab(tab);
      Assert.ok(!tab.pinned, "The tab is unpinned");
    });
  });
});
```

```{mermaid}
:caption: Mochia runs these callbacks for the tab pinning test above.

flowchart TD
    accTitle: Callback order for the tab pinning test
    accDescr: Open a tab, pin it, verify that it can be unpinned, and remove it.
    outerSetup["Outer beforeEach<br/>Open a tab"]
    innerSetup["Inner beforeEach<br/>Pin the tab"]
    test(["it(#quot;can be unpinned#quot;, () => { ... })"])
    outerTeardown["Outer afterEach<br/>Remove the tab"]

    outerSetup --> innerSetup --> test --> outerTeardown
```

The log name is
`Tab pinning / when the tab is pinned / can be unpinned`.

See the {searchfox}`Mochia implementation <testing/modules/Mochia.js>`
and its {searchfox}`lifecycle tests <testing/modules/tests/xpcshell/test_mochia.js>`.
