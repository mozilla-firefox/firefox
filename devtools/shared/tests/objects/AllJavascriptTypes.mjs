/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-disable object-shorthand */

// eslint-disable-next-line mozilla/reject-import-system-module-from-non-system

// Try replicating real world environment, by using
// * a true HTML document
// * served from http (https isn't yet supported by nsHttpServer)
// * with a regular domain name (example.com)
export const TEST_PAGE_HTML = String.raw`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title></title>
</head>
<body>
  <span id="span">Hello there.</span>
  <script>
    globalThis.myPolicy = trustedTypes.createPolicy("myPolicy", {
      createHTML: s => "<my-policy>" + s + "</my-policy>",
      createScript: s => "/* myPolicy */ " + s,
      createScriptURL: s => s + "?myPolicy",
    });
  </script>
</body>
</html>`;

export const CONTEXTS = {
   JS: "js",
   PAGE: "page",
   CHROME: "chrome"
};

/**
 * Manifest covering all the possible JavaScript value types that gecko may create.
 * This consist in JavaScript code instantiating one of more example values for all of these types.
 *
 * See README.md
 */
const BasicPrimitives = [
  {
    context: CONTEXTS.JS,
    expression: "undefined",
  },
  {
    context: CONTEXTS.JS,
    expression: "null",
  },
  {
    context: CONTEXTS.JS,
    expression: "true",
  },
  {
    context: CONTEXTS.JS,
    expression: "false",
  },
  {
    context: CONTEXTS.JS,
    expression: "NaN",
  },
];

const Strings = [
  {
    context: CONTEXTS.JS,
    expression: `"abc"`,
  },
  {
    context: CONTEXTS.JS,
    expression: `"\u9f2c\xFA"`,
  },
];

const Numbers = [
  {
    context: CONTEXTS.JS,
    expression: "42",
  },
  {
    context: CONTEXTS.JS,
    expression: "-42",
  },
  {
    context: CONTEXTS.JS,
    expression: "-0",
  },
  {
    context: CONTEXTS.JS,
    expression: "Infinity",
  },
  {
    context: CONTEXTS.JS,
    expression: "BigInt(1000000000000000000)",
  },
  {
    context: CONTEXTS.JS,
    expression: "1n",
  },
  {
    context: CONTEXTS.JS,
    expression: "-2n",
  },
  {
    context: CONTEXTS.JS,
    expression: "0n",
  },
];

const Primitives = [...BasicPrimitives, ...Strings, ...Numbers];

const PlainObjects = [
  {
    context: CONTEXTS.JS,
    expression: "({})"
  },
  {
    context: CONTEXTS.JS,
    expression: `({ foo: "bar"})`
  }
];

const Arrays = [
  {
    context: CONTEXTS.JS,
    expression: "[]"
  },
  {
    context: CONTEXTS.JS,
    expression: "[1]"
  },
  {
    context: CONTEXTS.JS,
    expression: '["foo"]'
  }
];

const TypedArrays = [
  {
    context: CONTEXTS.JS,
    expression: "new BigInt64Array()"
  },
  {
    context: CONTEXTS.JS,
    expression: `
      const a = new BigInt64Array(1);
      a[0] = BigInt(42);
      a;
    `,
  },
];

const Maps = [
  {
    context: CONTEXTS.JS,
    expression: `new Map(
          Array.from({ length: 2 }).map((el, i) => [
            { key: i },
            { object: 42 },
          ])
        )`,
  },
  {
    context: CONTEXTS.JS,
    expression: `new Map(Array.from({ length: 20 }).map((el, i) => [Symbol(i), i]))`,
  },
  {
    context: CONTEXTS.JS,
    expression: `new Map(Array.from({ length: 331 }).map((el, i) => [Symbol(i), i]))`,
  },
];

const Sets = [
  {
    context: CONTEXTS.JS,
    expression: `new Set(Array.from({ length: 2 }).map((el, i) => ({ value: i })))`,
  },
  {
    context: CONTEXTS.JS,
    expression: `new Set(Array.from({ length: 20 }).map((el, i) => i))`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Set(Array.from({ length: 222 }).map((el, i) => i))`
  },
];

const Temporals = [
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.Instant(355924804000000000n)`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.PlainDate(2021, 7, 1, "coptic")`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.PlainDateTime(2021, 7, 1, 0, 0, 0, 0, 0, 0, "gregory")`,
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.PlainMonthDay(7, 1, "chinese")`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.PlainTime(4, 20)`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.PlainYearMonth(2021, 7, "indian")`
  },
  {
    context: CONTEXTS.JS,
    expression: `new Temporal.ZonedDateTime(0n, "America/New_York")`
  },
  {
    context: CONTEXTS.JS,
    expression: `Temporal.Duration.from({ years: 1 })`
  },
];

const DOMAPIs = [
  {
    context: CONTEXTS.PAGE,
    expression: `myPolicy.createHTML("hello")`,
  },
  {
    context: CONTEXTS.PAGE,
    expression: `myPolicy.createScript("const hello = 'world'")`
  },
  {
    context: CONTEXTS.PAGE,
    expression: `myPolicy.createScriptURL("https://example.com/trusted")`
  },

  {
    context: CONTEXTS.PAGE,
    expression: `
      const formData = new FormData();
      formData.append("a", 1);
      formData.append("a", 2);
      formData.append("b", 3);
      formData;
    `,
  },
  /* midi API requires https (See Bug 1967917)
  {
    context: CONTEXTS.PAGE,
    expression: `
      const midiAccess = await navigator.requestMIDIAccess();
      midiAccess.inputs;
    `,
    prefs: [
      // This will make it so we'll have stable MIDI devices reported
      ["midi.testing", true],
      ["dom.webmidi.enabled", true],
      ["midi.prompt.testing", true],
      ["media.navigator.permission.disabled", true],
    ],
  },
  */
  {
    context: CONTEXTS.PAGE,
    expression: `
      customElements.define("fx-test", class extends HTMLElement {});
      const { states } = document.createElement("fx-test").attachInternals();
      states.add("custom-state");
      states.add("another-custom-state");
      states;
    `,
  },

  {
    context: CONTEXTS.PAGE,
    expression: `
      CSS.highlights.set("search", new Highlight());
      CSS.highlights.set("glow", new Highlight());
      CSS.highlights.set("anchor", new Highlight());
      CSS.highlights;
    `,
  },
  {
    context: CONTEXTS.PAGE,
    expression: `new URLSearchParams([
      ["a", 1],
      ["a", 2],
      ["b", 3],
      ["b", 3],
      ["b", 5],
      ["c", "this is 6"],
      ["d", 7],
      ["e", 8],
      ["f", 9],
      ["g", 10],
      ["h", 11],
    ])`,
  },
  {
    context: CONTEXTS.PAGE,
    expression: `
      const el = document.createElement("h1");
      el.setAttribute("class", "test-class-a test-class-b");
      el.setAttribute("id", "my-element");
      el.setAttribute("aria-collapsed", "false");
      el.setAttribute("hidden", "");
      el;
    `,
    prefs: [["dom.scoped-custom-element-registries.enabled", true]],
  },
  {
    context: CONTEXTS.PAGE,
    expression: `new Document()`,
    prefs: [["dom.scoped-custom-element-registries.enabled", true]],
  },
];

const Errors = [
  {
    context: CONTEXTS.JS,
    expression: `new Error("foo")`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw new Error("Long error ".repeat(10000));`,
  },
  {
    context: CONTEXTS.JS,
    expression: `
      throw \`“https://evil.com/?${"a".repeat(
        200
      )}“ is evil and “https://not-so-evil.com/?${"b".repeat(
        200
      )}“ is not good either\`;
    `,
  },
  {
    context: CONTEXTS.JS,
    expression: `Error("bar")`
  },
  {
    context: CONTEXTS.JS,
    expression: `
      function bar() {
        asdf();
      }
      function foo() {
        bar();
      }

      foo();
    `,
  },

  {
    context: CONTEXTS.JS,
    // Use nested `eval()` as syntax error would make the test framework throw on its own eval call
    expression: `eval("let a, a")`,
  },

  {
    context: CONTEXTS.JS,
    expression: `throw "";`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw false;`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw undefined;`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw 0;`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw { vegetable: "cucumber" };`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw Symbol("potato");`
  },

  {
    context: CONTEXTS.JS,
    expression: `
      var err = new Error("pineapple");
      err.name = "JuicyError";
      err.flavor = "delicious";
      throw err;
    `,
  },
  {
    context: CONTEXTS.JS,
    expression: `
      var originalError = new SyntaxError("original error");
      var err = new Error("something went wrong", {
        cause: originalError,
      });
      throw err;
    `,
  },

  {
    context: CONTEXTS.JS,
    expression: `
      var a = new Error("err-a");
      var b = new Error("err-b", { cause: a });
      var c = new Error("err-c", { cause: b });
      var d = new Error("err-d", { cause: c });
      throw d;
    `,
  },
  {
    context: CONTEXTS.JS,
    expression: `
      var a = new Error("err-a", { cause: b });
      var b = new Error("err-b", { cause: a });
      throw b;
    `,
  },
  {
    context: CONTEXTS.JS,
    expression: `throw new Error("null cause", { cause: null });`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw new Error("number cause", { cause: 0 });`
  },
  {
    context: CONTEXTS.JS,
    expression: `throw new Error("string cause", { cause: "cause message" });`
  },
  {
    context: CONTEXTS.JS,
    expression: `
      throw new Error("object cause", {
        cause: { code: 234, message: "ERR_234" },
      });
    `,
  },

  {
    context: CONTEXTS.JS,
    expression: `Promise.reject("")`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject("tomato")`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(false)`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(0)`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(null)`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(undefined)`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(Symbol("potato"))`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject({vegetable: "cucumber"})`
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.reject(new Error("pumpkin"))`
  },
  {
    context: CONTEXTS.JS,
    expression: `
      var err = new Error("pineapple");
      err.name = "JuicyError";
      err.flavor = "delicious";
      Promise.reject(err);
    `,
  },
  {
    context: CONTEXTS.JS,
    expression: `Promise.resolve().then(() => {
        try {
          unknownFunc();
        } catch(e) {
          throw new Error("something went wrong", { cause: e })
        }
      })`,
  },
];

const Privileged = [
  {
    context: CONTEXTS.CHROME,
    expression: `Components.Exception("foo")`
  }
];


export const AllObjects = [
  ...Primitives,
  ...PlainObjects,
  ...Arrays,
  ...TypedArrays,
  ...Maps,
  ...Sets,
  ...Temporals,
  ...DOMAPIs,
  ...Errors,
  ...Privileged,
];
