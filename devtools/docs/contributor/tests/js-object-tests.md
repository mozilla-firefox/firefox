# JavaScript Objects test framework

`JSObjectsTestUtils.sys.mjs` exposes xpcshell and mochitest test helpers to easily test all the
arbitrary JavaScript object types that Gecko can spawn in JavaScript.

This includes:

* any JavaScript type that Spidermonkey supports,
* any JavaScript type exposed to Web Page from Gecko (All the DOM APIs),
* any JavaScript type only exposed to Worker threads,
* any privileged JavaScript type used in parent or content processes,
* ...

This test framework consists in:

* a manifest file, [AllJavascriptTypes.mjs](https://searchfox.org/firefox-main/source/devtools/shared/tests/objects/AllJavascriptTypes.mjs) which defines all the JS objects that gecko can spawn
* one xpcshell or one mochitest file, using [JSObjectsTestUtils](https://searchfox.org/firefox-main/source/devtools/shared/tests/objects/JSObjectsTestUtils.sys.mjs) helper to evaluate all the JS Objects and generate a value for each of them.
* a snapshot file, read and written by JSObjectsTestUtils, specific to each xpcshell/mochitest and storing all its the generated values.

You can run your test to execute the assertions:

```bash
./mach test my/browser_test.js
```

And you can update the snapshot by running:

```bash
./mach test my/browser_test.js --setenv UPDATE_SNAPSHOT=true
```

You also can run all the tests with:

```bash
./mach xpcshell-test --tag object-snapshots
./mach mochitest --tag object-snapshots
```

And update all the snapshots with:

```bash
./mach xpcshell-test --tag object-snapshots --setenv UPDATE_SNAPSHOT=true
./mach mochitest --tag object-snapshots --setenv UPDATE_SNAPSHOT=true
```

(you may use `./mach test`, but it will run some unexpected tests, be slower and report unrelated errors)

## JSObjectsTestUtils APIs

This test helper is available to all xpcshell and mochitest tests from `resource://testing-common/JSObjectsTestUtils.sys.mjs`.
It exposes only two methods:

* `JSOBjectsTestUtils.init(testScope)`

  Which is meant to be called early in the test run, before the test page is loaded.
  This will update all preferences which helps enable all the experimental types
  or ease instantiating them.

  The global scope for the current test should be passed as argument.
  This will be used to retrieve the current test location in order to
  load the expected values from an ES Module located in the same folder as the current test.

* `JSOBjectsTestUtils.runTest(expectedValuesFileName, testFunction)`

  This is the main method, which will call the `testFunction` for each JavaScript object example.

  This method will receive a single argument which is an object with two attributes:
  * `context`
    A string whose value can be one of [AllJavascriptTypes.mjs](https://searchfox.org/firefox-main/source/devtools/shared/tests/objects/AllJavascriptTypes.mjs) `CONTEXTS` dictionary:
    * "js": Basic JS value available from any possible JavaScript context (worker, page, chrome scopes)
    * "page": Values only available from a Web page global
    * "chrome": Privileged values, only available from a chrome, privileged scope

  * `expression`
    A string which should be evaled in order to instantiate the object example to cover.

  For example, it may receive as argument:
  `{ type: "js", expression: "42" }`
  `{ type: "js", expression: "[42]" }`
  `{ type: "js", expression: "let a = new BigInt64Array(1); a[0] = BigInt(42); a;" }`
  `{ type: "page", expression: "document.body" }`
  `{ type: "chrome", expression: "ChromeUtils.domProcessChild" }`

  This method should return the value which represents the given object example.

### Mochitest Example

```js
const { JSObjectsTestUtils, CONTEXTS } = ChromeUtils.importESModule(
  "resource://testing-common/JSObjectsTestUtils.sys.mjs"
);
// We have to manually initialize the test helper module from the parent process
JSObjectsTestUtils.init(this);

// Name of your snapshot file, next to this test, to be registered in the .toml file in a support-files rule
const EXPECTED_VALUES_FILE = "browser_mytest.snapshot.mjs";

add_task(async function () {
  // Open the test page suitable to spawn all the expected JS Objects in a new tab
  //
  // nsHttpServer does not support https
  // eslint-disable-next-line sdl/no-insecure-url
  const tab = await addTab("http://example.com");

  await JSObjectsTestUtils.runTest(
    EXPECTED_VALUES_FILE,
    async function ({ context, expression }) {
      // In this test, we ignore the privileged objects
      if (context == CONTEXTS.CHROME) {
        return;
      }

      // Then, execute the key runTest method from the tab's content process
      await SpecialPowsers.spawn(tab.linkedBrowser, [expression], async function (expressionString) {

        // This is the most important part, where we evaluate the `expression` provided by the test framework
        // in the test page and return the value we would like to assert over time.
        // We have to ensure handling exception, which we expect to interpret as a returned value.
        let value;
        try {
          value = content.eval(expressionString);
        } catch(e) {
          value = e;
        }

        // Here we cover the native stringification of all the JS Objects.
        return String(value);
    }
  );
});
```

## AllJavaScriptTypes manifest

All the JavaScript object examples are stored in a manifest file located in the current folder: AllJavascriptTypes.mjs.

This module exports an array of objects descriptions, which are objects with the two following attributes:

* `context`:
  A String to designate the context into which this expression could be evaluated.
  See the first paragraph for the list of all contexts.
* `expression`:
  The JavaScript expression to evaluate, which can either be:
  * a string representing a piece of JavaScript value.
  * a function, which would be stringified and evaluated in many scopes.
    This is to be used when you need intermediate value before spawning another specific JS Value.
  If the expression throws, the thrown exception will be considered as the value to assert.

The object descriptions can also have a couple of optional attributes:

* `prefs`: An array of arrays. The nested array are made of two elements: a string and a value.
  The string represents a preference name and the value, the preference value.
  This is used to set preference before starting the test.
  This helps enable as well as ease instantiating the related JS value.
* `disabled`: A boolean to be set to true if the value can't be instantiated on the current runtime.
