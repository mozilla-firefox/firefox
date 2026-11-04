/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import mapExpression from "../mapExpression";
import { format } from "prettier";
import cases from "jest-in-case";

function test({
  expression,
  newExpression,
  bindings,
  mappings,
  shouldMapBindings,
  expectedMapped,
  parseExpression = true,
}) {
  const res = mapExpression(expression, mappings, bindings, shouldMapBindings);

  if (parseExpression) {
    expect(
      format(res.expression, {
        parser: "babel",
      })
    ).toEqual(format(newExpression, { parser: "babel" }));
  } else {
    expect(res.expression).toEqual(newExpression);
  }

  expect(res.mapped).toEqual(expectedMapped);
}

function formatAwait(body) {
  return `(async () => { ${body} })();`;
}

describe("mapExpression", () => {
  cases("mapExpressions", test, [
    {
      name: "await",
      expression: "await a()",
      newExpression: formatAwait("return a()"),
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (multiple statements)",
      expression: "const x = await a(); x + x",
      newExpression: `let x;
        (async () => {
          x = await a();
          return x + x;
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (inner)",
      expression: "async () => await a();",
      newExpression: "async () => await a();",
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (multiple awaits)",
      expression: "const x = await a(); await b(x)",
      newExpression: `let x;
        (async () => {
          x = await a();
          return b(x);
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (assignment)",
      expression: "let x = await sleep(100, 2)",
      newExpression: `let x;
        (async () => {
          return (x = await sleep(100, 2));
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (destructuring)",
      expression: "const { a, c: y } = await b()",
      newExpression: `let a, y;
        (async () => {
          return ({ a, c: y } = await b());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (array destructuring)",
      expression: "const [a, y] = await b();",
      newExpression: `let a, y;
        (async () => {
          return ([a, y] = await b());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (mixed destructuring)",
      expression: "const [{ a }] = await b();",
      newExpression: `let a;
        (async () => {
          return ([{ a }] = await b());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (destructuring, multiple statements)",
      expression: "const { a, c: y } = await b(), { x } = await y()",
      newExpression: `let a, y, x;
        (async () => {
          ({ a, c: y } = await b());
          return ({ x } = await y());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (destructuring, bindings)",
      expression: "const { a, c: y } = await b();",
      newExpression: `let a, y;
        (async () => {
          return ({ a, c: y } = await b());
        })();`,
      bindings: ["a", "y"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (array destructuring, bindings)",
      expression: "const [a, y] = await b();",
      newExpression: `let a, y;
        (async () => {
          return ([a, y] = await b());
        })();`,
      bindings: ["a", "y"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (mixed destructuring, bindings)",
      expression: "const [{ a }] = await b();",
      newExpression: `let a;
        (async () => {
          return ([{ a }] = await b());
        })();`,
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (destructuring with defaults, bindings)",
      expression: "const { c, a = 5 } = await b();",
      newExpression: `let c, a;
        (async () => {
          return ({ c, a = 5 } = await b());
        })();`,
      bindings: ["a", "y"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (array destructuring with defaults, bindings)",
      expression: "const [a, y = 10] = await b();",
      newExpression: `let a, y;
        (async () => {
          return ([a, y = 10] = await b());
        })();`,
      bindings: ["a", "y"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (mixed destructuring with defaults, bindings)",
      expression: "const [{ c = 5 }, a = 5] = await b();",
      newExpression: `let c, a;
        (async () => {
          return ([{ c = 5 }, a = 5] = await b());
        })();`,
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (nested destructuring, bindings)",
      expression: "const { a, c: { y } } = await b();",
      newExpression: `let a, y;
        (async () => {
          return ({
            a,
            c: { y },
          } = await b());
        })();`,
      bindings: ["a", "y"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (nested destructuring with defaults)",
      expression: "const { a, c: { y = 5 } = {} } = await b();",
      newExpression: `let a, y;
        (async () => {
          return ({ a, c: { y = 5 } = {} } = await b());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (very nested destructuring with defaults)",
      expression:
        "const { a, c: { y: { z = 10, b } = { b: 5 } } } = await b();",
      newExpression: `let a, z, b;
        (async () => {
          return ({
            a,
            c: {
              y: { z = 10, b } = {
                b: 5,
              },
            },
          } = await b());
        })();`,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (with SyntaxError)",
      expression: "await new Promise())",
      newExpression: formatAwait("await new Promise())"),
      parseExpression: false,
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, let assignment)",
      expression: "let a = await 123;",
      newExpression: `let a;
        (async () => {
          return a = await 123;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, var assignment)",
      expression: "var a = await 123;",
      newExpression: `var a;
        (async () => {
          return a = await 123;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, const assignment)",
      expression: "const a = await 123;",
      newExpression: `let a;
        (async () => {
          return a = await 123;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, multiple assignments)",
      expression: "let a = 1, b, c = 3; b = await 123; a + b + c",
      newExpression: `let a, b, c;
        (async () => {
          a = 1;
          c = 3;
          b = await 123;
          return a + b + c;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, object destructuring)",
      expression: "let {a, b, c} = await x;",
      newExpression: `let a, b, c;
        (async () => {
          return ({a, b, c} = await x);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, object destructuring with rest)",
      expression: "let {a, ...rest} = await x;",
      newExpression: `let a, rest;
        (async () => {
          return ({a, ...rest} = await x);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, object destructuring with renaming and default)",
      expression: "let {a: hello, b, c: world, d: $ = 4} = await x;",
      newExpression: `let hello, b, world, $;
        (async () => {
          return ({a: hello, b, c: world, d: $ = 4} = await x);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, nested object destructuring + renaming + default)",
      expression: `let {
          a: hello, c: { y: { z = 10, b: bill, d: [e, f = 20] }}
        } = await x; z;`,
      newExpression: `let hello, z, bill, e, f;
        (async () => {
          ({ a: hello, c: { y: { z = 10, b: bill, d: [e, f = 20] }}} = await x);
          return z;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, array destructuring)",
      expression: "let [a, b, c] = await x; c;",
      newExpression: `let a, b, c;
        (async () => {
          [a, b, c] = await x;
          return c;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, array destructuring with default)",
      expression: "let [a, b = 1, c = 2] = await x; c;",
      newExpression: `let a, b, c;
        (async () => {
          [a, b = 1, c = 2] = await x;
          return c;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, array destructuring with default and rest)",
      expression: "let [a, b = 1, c = 2, ...rest] = await x; rest;",
      newExpression: `let a, b, c, rest;
        (async () => {
          [a, b = 1, c = 2, ...rest] = await x;
          return rest;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, nested array destructuring with default)",
      expression: "let [a, b = 1, [c = 2, [d = 3, e = 4]]] = await x; c;",
      newExpression: `let a, b, c, d, e;
        (async () => {
          [a, b = 1, [c = 2, [d = 3, e = 4]]] = await x;
          return c;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (no bindings, dynamic import)",
      expression: `
        var {rainbowLog} = await import("./cool-module.js");
        rainbowLog("dynamic");`,
      newExpression: `var rainbowLog;
        (async () => {
          ({rainbowLog} = await import("./cool-module.js"));
          return rainbowLog("dynamic");
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (nullish coalesce operator)",
      expression: "await x; true ?? false",
      newExpression: `(async () => {
          await x;
          return true ?? false;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (optional chaining operator)",
      expression: "await x; x?.y?.z",
      newExpression: `(async () => {
          await x;
          return x?.y?.z;
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (async function declaration with nullish coalesce operator)",
      expression: "async function coalesce(x) { await x; return x ?? false; }",
      newExpression:
        "async function coalesce(x) { await x; return x ?? false; }",
      shouldMapBindings: false,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (async function declaration with optional chaining operator)",
      expression: "async function chain(x) { await x; return x?.y?.z; }",
      newExpression: "async function chain(x) { await x; return x?.y?.z; }",
      shouldMapBindings: false,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      // check that variable declaration in for loop is not put outside of the async iife
      name: "await (for loop)",
      expression: "for (let i=0;i<2;i++) {}; var b = await 1;",
      newExpression: `var b;
        (async () => {
          for (let i=0;i<2;i++) {}
          return (b = await 1);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      // check that variable declaration in for-in loop is not put outside of the async iife
      name: "await (for..in loop)",
      expression: "for (let i in {}) {}; var b = await 1;",
      newExpression: `var b;
        (async () => {
          for (let i in {}) {}
          return (b = await 1);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      // check that variable declaration in for-of loop is not put outside of the async iife
      name: "await (for..of loop)",
      expression: "for (let i of []) {}; var b = await 1;",
      newExpression: `var b;
        (async () => {
          for (let i of []) {}
          return (b = await 1);
        })()`,
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (if condition)",
      expression: "if (await true) console.log(1);",
      newExpression: formatAwait("if (await true) console.log(1);"),
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (non-expression final statement: bug 1851759)",
      expression: `j = { "foo": 1, "bar": 2 }; await 42; for (var k in j) { console.log(k); }`,
      newExpression: formatAwait(`
        j = {
          foo: 1,
          bar: 2,
        };
        await 42;
        for (var k in j) {
          console.log(k);
        }`),
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "simple",
      expression: "a",
      newExpression: "a",
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "mappings",
      expression: "a",
      newExpression: "_a",
      bindings: [],
      mappings: {
        a: "_a",
      },
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: true,
      },
    },
    {
      name: "declaration",
      expression: "var a = 3;",
      newExpression: "self.a = 3",
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "declaration + destructuring",
      expression: "var { a } = { a: 3 };",
      newExpression: "({ a: self.a } = {\n a: 3 \n})",
      bindings: [],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings",
      expression: "var a = 3;",
      newExpression: "a = 3",
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings + destructuring",
      expression: "var { a } = { a: 3 };",
      newExpression: "({ a } = { \n a: 3 \n })",
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings + destructuring + rest",
      expression: "var { a, ...foo } = {}",
      newExpression: "({ a, ...self.foo } = {})",
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings + array destructuring + rest",
      expression: "var [a, ...foo] = []",
      newExpression: "([a, ...self.foo] = [])",
      bindings: ["a"],
      mappings: {},
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings + mappings",
      expression: "a = 3;",
      newExpression: "self.a = 3",
      bindings: ["_a"],
      mappings: { a: "_a" },
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings + mappings + destructuring",
      expression: "var { a } = { a: 4 }",
      newExpression: "({ a: self.a } = {\n a: 4 \n})",
      bindings: ["_a"],
      mappings: { a: "_a" },
      shouldMapBindings: true,
      expectedMapped: {
        await: false,
        bindings: true,
        originalExpression: false,
      },
    },
    {
      name: "bindings without mappings",
      expression: "a = 3;",
      newExpression: "a = 3",
      bindings: [],
      mappings: { a: "_a" },
      shouldMapBindings: false,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "object destructuring + bindings without mappings",
      expression: "({ a } = {});",
      newExpression: "({ a: _a } = {})",
      bindings: [],
      mappings: { a: "_a" },
      shouldMapBindings: false,
      expectedMapped: {
        await: false,
        bindings: false,
        originalExpression: true,
      },
    },
    {
      name: "await (inside top-level block)",
      expression: "{ await 1 }",
      newExpression: formatAwait("{ await 1 }"),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (inside top-level try block)",
      expression: "try { await 1 } catch (e) {}",
      newExpression: formatAwait("try { await 1; } catch (e) {}"),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (inside top-level catch block)",
      expression: "try { throw 'Error' } catch (e) { await 2; }",
      newExpression: formatAwait(
        "try { throw 'Error' } catch (e) { await 2; }"
      ),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (inside top-level if-else block)",
      expression: "if (false) { await 1; } else { await 2; }",
      newExpression: formatAwait(`if (false) { await 1; } else { await 2; }`),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (in method name of a class)",
      expression: "class A { [await 0]() {} }",
      newExpression: formatAwait(`class A { [await 0]() {} }`),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
    {
      name: "await (for await)",
      expression: "for await (const num of [1,2,3]);",
      newExpression: formatAwait(`for await (const num of [1,2,3]);`),
      bindings: [],
      mappings: {},
      shouldMapBindings: false,
      expectedMapped: {
        await: true,
        bindings: false,
        originalExpression: false,
      },
    },
  ]);
});
