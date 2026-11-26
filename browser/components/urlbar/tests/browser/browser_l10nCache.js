/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Browser test for L10nCache.mjs.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  L10nCache: "chrome://browser/content/urlbar/L10nCache.mjs",
});

let gL10n;

add_setup(async function () {
  // Set up a mock `Localization`.
  let { l10n, cleanup } = initL10n({
    args0: "Zero args value",
    args0Markup: "Another <strong>zero args</strong> value",
    args1: "One arg value is { $arg1 }",
    args1Markup: "Another one arg value <strong>is { $arg1 }</strong>",
    args2: "First { $arg1 } then { $arg2 }",
    args1Title: "Value is { $arg1 }\n  .title = Title",
    args2Markup:
      'First <em>{ $arg1 }</em> then <a href="https://example.com/">{ $arg2 }</a>',
    attrs0: [".label = attrs0 label has zero args"],
    attrs0Markup: [
      ".label = Markup <strong>should not be parsed</strong> in attributes",
    ],
    attrs1: [
      ".label = attrs1 label has zero args",
      ".tooltiptext = attrs1 tooltiptext arg value is { $arg1 }",
    ],
  });
  gL10n = l10n;
  registerCleanupFunction(cleanup);
});

add_task(async function comprehensive() {
  // This task runs a bunch of subtests that call `setElementL10n()` on a span a
  // few times. Each subtest object has the following properties:
  //
  // {object} l10n
  //   An l10n object to pass to `L10nCache.setElementL10n()` and other
  //   `L10nCache` methods.
  // {Function} assert
  //   This function is called as `await assert(span)` at the end of each
  //   subtest and should check final state of the span after the cached string
  //   specified by `l10n` has been set on it. It should check the span's child
  //   nodes or attributes as appropriate for the subtest.
  let tests = [
    {
      l10n: {
        id: "args0",
      },
      assert: span => {
        checkChildren(span, [
          { name: "#text", textContent: "Zero args value" },
        ]);
      },
    },

    {
      l10n: {
        id: "args0Markup",
        // no `parseMarkup: true`
      },
      assert: span => {
        checkChildren(span, [
          {
            name: "#text",
            textContent: "Another <strong>zero args</strong> value",
          },
        ]);
      },
    },

    {
      l10n: {
        id: "args0Markup",
        parseMarkup: true,
      },
      assert: span => {
        checkChildren(span, [
          { name: "#text", textContent: "Another " },
          { name: "strong", textContent: "zero args" },
          { name: "#text", textContent: " value" },
        ]);
      },
    },

    {
      l10n: {
        id: "args1",
        args: { arg1: "foo" },
      },
      assert: span => {
        checkChildren(span, [
          { name: "#text", textContent: "One arg value is foo" },
        ]);
      },
    },

    {
      l10n: {
        id: "args1Markup",
        args: { arg1: "bar" },
        // no `parseMarkup: true`
      },
      assert: span => {
        checkChildren(span, [
          {
            name: "#text",
            textContent: "Another one arg value <strong>is bar</strong>",
          },
        ]);
      },
    },

    {
      l10n: {
        id: "args1Markup",
        args: { arg1: "bar" },
        parseMarkup: true,
      },
      assert: span => {
        checkChildren(span, [
          { name: "#text", textContent: "Another one arg value " },
          { name: "strong", textContent: "is bar" },
        ]);
      },
    },

    {
      l10n: {
        id: "attrs0",
        attribute: "label",
      },
      assert: span => {
        checkAttributes(span, {
          label: "attrs0 label has zero args",
        });
      },
    },

    {
      l10n: {
        id: "attrs0Markup",
        attribute: "label",
        // no `parseMarkup: true`
      },
      assert: span => {
        checkAttributes(span, {
          label: "Markup <strong>should not be parsed</strong> in attributes",
        });
      },
    },

    {
      l10n: {
        id: "attrs0Markup",
        attribute: "label",
        // `parseMarkup` should be ignored since `attribute` is defined
        parseMarkup: true,
      },
      assert: span => {
        checkAttributes(span, {
          label: "Markup <strong>should not be parsed</strong> in attributes",
        });
      },
    },

    {
      l10n: {
        id: "attrs1",
        attribute: "label",
        args: { arg1: "foo" },
      },
      assert: span => {
        checkAttributes(span, {
          label: "attrs1 label has zero args",
          tooltiptext: "attrs1 tooltiptext arg value is foo",
        });
      },
    },

    {
      l10n: {
        id: "attrs1",
        attribute: "tooltiptext",
        args: { arg1: "foo" },
        // Highlights are ignored for attributes, which can't contain markup.
        argsHighlights: { arg1: [[0, 3]] },
      },
      assert: span => {
        checkAttributes(span, {
          label: "attrs1 label has zero args",
          tooltiptext: "attrs1 tooltiptext arg value is foo",
        });
      },
    },
  ];

  let cache = new L10nCache(gL10n);

  for (let { l10n, assert } of tests) {
    info("Doing subtest: " + JSON.stringify(l10n));

    let span = document.createElement("span");

    // Set the span's l10n.
    await cache.setElementL10n(span, l10n);

    Assert.ok(
      cache.get(l10n),
      "String should be cached after awaiting setElementL10n"
    );

    // The cache should have set the l10n attributes.
    checkL10n(span, l10n);

    // Clear the text content and attributes so we can verify they're set by the
    // next `setElementL10n()`.
    span.textContent = "";
    for (let name of span.getAttributeNames()) {
      span.removeAttribute(name);
    }

    // Set the span l10n again but don't await the call. Since the string is now
    // cached, `setElementL10n()` should synchronously update the span by
    // setting its text content and attributes directly.
    let cachePromise = cache.setElementL10n(span, l10n);

    Assert.ok(cache.get(l10n), "String should still be cached");
    await assert(span);

    // The l10n attributes should still be set.
    checkL10n(span, l10n);

    await cachePromise;

    cache.clear();
  }
});

add_task(async function argsHighlights() {
  let cache = new L10nCache(gL10n);
  let span = document.createElement("span");

  let l10n = {
    id: "args2",
    args: { arg1: "foo <b>bar</b>", arg2: "baz" },
    argsHighlights: { arg1: [[0, 3]] },
  };
  await cache.setElementL10n(span, l10n);
  let { args } = document.l10n.getAttributes(span);
  Assert.equal(args.arg2, "baz", "The other arg should be passed as is");
  let parsed = new DOMParser().parseFromString(args.arg1, "text/html").body;
  checkChildren(parsed, [
    { name: "STRONG", textContent: "foo" },
    { name: "#text", textContent: " <b>bar</b>" },
  ]);

  // The cached string should be applied synchronously with the highlights.
  span.textContent = "";
  let promise = cache.setElementL10n(span, l10n);
  checkChildren(span, [
    { name: "#text", textContent: "First " },
    { name: "strong", textContent: "foo" },
    { name: "#text", textContent: " <b>bar</b> then baz" },
  ]);
  await promise;

  // Different highlights aren't cached yet.
  span.textContent = "";
  await cache.setElementL10n(span, {
    ...l10n,
    argsHighlights: { arg1: [[4, 3]] },
  });
  Assert.equal(span.textContent, "", "The uncached string isn't applied");
});

add_task(async function argsHighlightsWithoutOverlays() {
  let cache = new L10nCache(gL10n);
  let span = document.createElement("span");

  let l10n = {
    id: "args2",
    args: { arg1: "foo", arg2: "baz" },
    argsHighlights: { arg1: [[0, 3]] },
  };
  await cache.setElementL10n(span, l10n);

  // `L10nOverlays` isn't available in unprivileged documents, so cached strings
  // with highlights should be left to `DOMLocalization`.
  Object.defineProperty(span, "documentGlobal", { value: {} });
  span.textContent = "";
  await cache.setElementL10n(span, l10n);
  Assert.equal(span.textContent, "", "The cached string isn't applied");
  Assert.equal(
    document.l10n.getAttributes(span).id,
    "args2",
    "The l10n attributes should be set"
  );
});

add_task(async function argsHighlightsMarkup() {
  let cache = new L10nCache(gL10n);

  // Cached strings should be rendered like `DOMLocalization` does: links are
  // replaced with their text, and allowed elements in the translation are
  // kept but their content is flattened, so highlights inside them are lost.
  for (let { l10n, expected } of [
    {
      // Markup in args without highlights.
      l10n: {
        id: "args2",
        args: { arg1: "foo", arg2: '<a href="https://example.com/">bar</a>' },
        argsHighlights: { arg1: [[0, 3]] },
      },
      expected: [
        { name: "#text", textContent: "First " },
        { name: "strong", textContent: "foo" },
        { name: "#text", textContent: " then " },
        { name: "#text", textContent: "bar" },
      ],
    },
    {
      // Markup in the translation.
      l10n: {
        id: "args2Markup",
        args: { arg1: "foo", arg2: "bar" },
        argsHighlights: { arg1: [[0, 3]] },
      },
      expected: [
        { name: "#text", textContent: "First " },
        { name: "em", textContent: "foo" },
        { name: "#text", textContent: " then " },
        { name: "#text", textContent: "bar" },
      ],
    },
  ]) {
    info("Doing subtest: " + JSON.stringify(l10n));
    let span = document.createElement("span");
    await cache.setElementL10n(span, l10n);

    span.textContent = "";
    let promise = cache.setElementL10n(span, l10n);
    checkChildren(span, expected);
    Assert.ok(
      !span.querySelector("em > *"),
      "The content of allowed elements should be flattened"
    );

    await promise;
  }
});

add_task(async function argsHighlightsWithAttributes() {
  let cache = new L10nCache(gL10n);
  let span = document.createElement("span");

  let l10n = {
    id: "args1Title",
    args: { arg1: "foo" },
    argsHighlights: { arg1: [[0, 3]] },
  };
  await cache.setElementL10n(span, l10n);

  // Localized attributes should be kept when the highlighted value is applied.
  span.textContent = "";
  span.removeAttribute("title");
  let promise = cache.setElementL10n(span, l10n);
  checkChildren(span, [
    { name: "#text", textContent: "Value is " },
    { name: "strong", textContent: "foo" },
  ]);
  checkAttributes(span, { title: "Title" });
  await promise;
});

add_task(async function removeElementL10n() {
  let cache = new L10nCache(gL10n);
  let span = document.createElement("span");

  // Call `setElementL10n()`. It should set l10n attributes on the span since
  // the string isn't cached.
  let l10n = {
    id: "attrs1",
    attribute: "tooltiptext",
    args: { arg1: "foo" },
  };
  await cache.setElementL10n(span, l10n);

  // The cache should have set the l10n attributes.
  checkL10n(span, l10n);

  // Call `removeElementL10n()`. It should remove the l10n attributes.
  cache.removeElementL10n(span, l10n);
  Assert.equal(span.textContent, "", "textContent should be empty");
  for (let a of ["data-l10n-id", "data-l10n-attrs", "data-l10n-args"]) {
    Assert.ok(!span.hasAttribute(a), "Attribute should be unset: " + a);
  }
});

// A more real-world test that triggers `DOMLocalization` in a document.
add_task(async function inDocument() {
  // This task is more of a real-world test than the others. It relies on the
  // test element being present in a document so that the automatic translation
  // behavior of `DOMLocalization` is triggered. We'll use the current browser
  // document and insert a span in the document element.
  let cache = new L10nCache(document.l10n);
  let span = document.createElement("span");
  document.documentElement.append(span);
  registerCleanupFunction(() => span.remove());

  // We'll also use a real string since that's easier than trying to inject mock
  // strings. Use a string with an argument to test that too.
  let id = "urlbar-result-action-search-w-engine";
  let arg = "engine";
  let value = a => `Search with ${a}`;

  // Call `setElementL10n()` with an initial arg value for the string.
  // `setElementL10n()` should set l10n attributes on the span.
  let l10n1 = {
    id,
    args: { [arg]: "aaa" },
  };
  await cache.setElementL10n(span, l10n1);
  checkL10n(span, l10n1);

  Assert.deepEqual(
    cache.get(l10n1),
    {
      attributes: null,
      value: value("aaa"),
    },
    "String should be cached with 'aaa' arg"
  );

  // Wait for `DOMLocalization` to update the `textContent`.
  await TestUtils.waitForCondition(() => {
    info("Waiting for new textContent, current is: " + span.textContent);
    return span.textContent == value("aaa");
  }, "Waiting for new textContent with 'aaa' arg value");

  // Call `setElementL10n()` again but with a different arg value. It should do
  // three things: (1) Cache the new value of the string but not wait for that
  // to finish, (2) immediately set the span's `textContent` to the currently
  // cached string, which remains the old value, and (3) set the span's l10n
  // attributes so that `DOMLocalization` will generate the string's new value
  // and assign it to the span's `textContent` when translation is done.
  let l10n2 = {
    id,
    args: { [arg]: "bbb" },
  };
  let cachePromise = cache.setElementL10n(span, l10n2);

  Assert.equal(
    span.textContent,
    value("aaa"),
    "span.textContent should be the old cached value with 'aaa'"
  );

  checkL10n(span, l10n2);

  // The new string value should be cached.
  await cachePromise;
  Assert.deepEqual(
    cache.get(l10n2),
    {
      attributes: null,
      value: value("bbb"),
    },
    "String should be cached with 'bbb' arg"
  );

  // `DOMLocalization` should update the span's `textContent` with a new string
  // value containing the new 'bbb' arg value.
  await TestUtils.waitForCondition(() => {
    info("Waiting for new textContent, current is: " + span.textContent);
    return span.textContent == value("bbb");
  }, "Waiting for new textContent with 'bbb' arg value");
  Assert.equal(
    span.textContent,
    value("bbb"),
    "span.textContent should have the new 'bbb' arg value"
  );

  span.remove();
});

/**
 * Sets up a mock localization.
 *
 * @param {object} pairs
 *   Fluent strings as key-value pairs.
 * @returns {object}
 *   An object `{ l10n, cleanup }`
 *
 *   {Localization} l10n
 *     The mock `Localization` object.
 *   {Function} cleanup
 *     This should be called when you're done with the l10n object.
 */
function initL10n(pairs) {
  let source = Object.entries(pairs)
    .map(([key, value]) => {
      if (Array.isArray(value)) {
        value = value.map(s => "  \n" + s).join("");
      }
      return `${key} = ${value}`;
    })
    .join("\n");

  let registry = new L10nRegistry();
  registry.registerSources([
    L10nFileSource.createMock(
      "test",
      "app",
      ["en-US"],
      "/localization/{locale}",
      [{ source, path: "/localization/en-US/test.ftl" }]
    ),
  ]);

  return {
    l10n: new Localization(["/test.ftl"], true, registry, ["en-US"]),
    cleanup: () => {
      registry.removeSources(["test"]);
    },
  };
}

function checkChildren(element, expected) {
  let children = [...element.childNodes].map(n => ({
    name: n.nodeName,
    textContent: n.textContent,
  }));
  Assert.deepEqual(children, expected, "Children should be correct");
}

function checkAttributes(element, expected) {
  let attrs = {};
  for (let i = 0; i < element.attributes.length; i++) {
    let a = element.attributes.item(i);

    // Ignore l10n-related attributes by skipping data attributes. The main
    // `comprehensive` task checks these attributes.
    if (!a.name.startsWith("data-")) {
      attrs[a.name] = a.value;
    }
  }
  Assert.deepEqual(attrs, expected, "Attributes should be correct");
}

function checkL10n(element, expectedL10n) {
  // `document.l10n.getAttributes` always returns an object with only `id` and
  // `args` entries, and `args` will be null if there are no args.
  // `element.dataset.l10nAttrs` is not included in this object.
  Assert.deepEqual(
    document.l10n.getAttributes(element),
    {
      id: expectedL10n.id,
      args: expectedL10n.args ?? null,
    },
    "The element should have the expected l10n attributes"
  );

  // Now check `dataset.l10nAttrs`.
  Assert.equal(
    element.dataset.l10nAttrs,
    expectedL10n.attribute,
    "element.dataset.l10nAttrs should be set or not as expected"
  );
}
