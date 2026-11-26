/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Report how much of each module tsc can type from its JSDoc. Under
// `noImplicitAny: false` an untyped parameter is a silent `any`, so the error
// count says nothing about the code that uses one. `./mach ts coverage` runs
// this script:
//
//   coverage.mjs [--list] PATH...
//
// --list prints every function with an untyped parameter.
//
// PATH is a .js or .mjs file, or a directory. For a directory, only the .js
// and .mjs files directly in it are examined; subdirectories are not entered.
//
// Only parameters are measured, not return types. A parameter counts as typed
// when:
// - tsc finds a JSDoc type for it (@param {T}, an inline /** @type */, or an
//   @type or @satisfies on the function),
// - it has a default value,
// - it belongs to a setter with a getter of the same name, or
// - it belongs to a method of an object literal that is held by a declaration
//   or cast with an @type or @satisfies, or returned by a function with an
//   @returns or @type.
//
// Callbacks and object-literal methods passed as arguments are skipped: tsc
// types them from the callee's signature, but leaves them `any` when the
// callee itself is `any`, which this cannot tell.
//
// A method of an object literal assigned to a property with a declared type
// (`this.handler = {…}`) counts as untyped although tsc types it, since
// telling that needs tsc's type checker.
/* eslint-disable no-console */

import { createRequire } from "module";
import fs from "fs";
import path from "path";

const topsrcdir = path.join(import.meta.dirname, "..", "..");

let ts;
try {
  ts = createRequire(path.join(topsrcdir, "package.json"))("typescript");
} catch (e) {
  if (e.code != "MODULE_NOT_FOUND") {
    throw e;
  }
  console.error(
    "`typescript` not found: run this through `./mach ts coverage`."
  );
  process.exit(2);
}

/**
 * Prints the usage and exits with 2.
 */
function usage() {
  console.error("usage: mach ts coverage [--list] PATH...");
  process.exit(2);
}

const args = process.argv.slice(2);
const list = args.includes("--list");
const paths = args.filter(a => a != "--list");
if (!paths.length || paths.some(p => p.startsWith("-"))) {
  usage();
}

for (const p of paths) {
  if (!fs.existsSync(p)) {
    console.error(`no such file or directory: ${p}`);
    process.exit(2);
  }
}
const isModule = f => /\.m?js$/.test(f);
const found = paths.flatMap(p => {
  if (!fs.statSync(p).isDirectory()) {
    if (!isModule(p)) {
      console.error(`not a .js or .mjs file: ${p}`);
      process.exit(2);
    }
    return [p];
  }
  const inDir = fs
    .readdirSync(p, { withFileTypes: true })
    .filter(e => e.isFile() && isModule(e.name))
    .map(e => path.join(p, e.name))
    .sort();
  if (!inDir.length) {
    console.error(`no .js or .mjs files directly in ${p}`);
    process.exit(2);
  }
  return inDir;
});
// A file named twice, e.g. by `dir dir/`, counts once.
const files = [...new Map(found.map(f => [path.resolve(f), f])).values()];
// Label rows relative to the directory all the files share.
const common = files
  .map(f => path.dirname(path.resolve(f)))
  .reduce((a, b) => {
    while (a != b && !b.startsWith(a + path.sep) && a != path.dirname(a)) {
      a = path.dirname(a);
    }
    return a;
  });
const label = f => path.relative(common, path.resolve(f));

/**
 * Whether a node is an argument or the callee of a call or `new`, ignoring
 * parentheses. A callback passed as an argument and an immediately invoked
 * function both take their parameter types from the call.
 *
 * @param {import("typescript").Node} node
 */
function isCallOperand(node) {
  let p = node.parent;
  while (ts.isParenthesizedExpression(p)) {
    p = p.parent;
  }
  return ts.isCallExpression(p) || ts.isNewExpression(p);
}

/**
 * Tells how tsc types a function that is a method of an object literal. The
 * literal gives its methods a type by context when a declaration or cast with
 * an @type or @satisfies holds it, or a function with an @returns or @type
 * returns it. When the literal is a call argument, the callee types its
 * methods, as it types a callback.
 *
 * @param {import("typescript").SignatureDeclaration} node
 * @returns {"typed" | "argument" | null}
 *   "typed" for a literal typed by context, "argument" for a call argument,
 *   and null when the function is not such a method or nothing types it.
 */
function literalContext(node) {
  if (!ts.isMethodDeclaration(node) && !ts.isPropertyAssignment(node.parent)) {
    return null;
  }
  let p = node.parent;
  if (ts.isPropertyAssignment(p)) {
    p = p.parent;
  }
  while (ts.isObjectLiteralExpression(p)) {
    let holder = p.parent;
    while (ts.isParenthesizedExpression(holder)) {
      if (ts.getJSDocType(holder) || ts.getJSDocSatisfiesTag(holder)) {
        return "typed";
      }
      holder = holder.parent;
    }
    if (ts.isPropertyAssignment(holder)) {
      p = holder.parent;
      continue;
    }
    if (isCallOperand(p)) {
      return "argument";
    }
    if (ts.isReturnStatement(holder) || ts.isArrowFunction(holder)) {
      const fn = ts.isArrowFunction(holder)
        ? holder
        : ts.getContainingFunction(holder);
      return fn && (ts.getJSDocReturnType(fn) || hasFunctionType(fn))
        ? "typed"
        : null;
    }
    const decl =
      ts.isBinaryExpression(holder) && ts.isExpressionStatement(holder.parent)
        ? holder.parent
        : holder;
    return ts.getJSDocType(decl) || ts.getJSDocSatisfiesTag(decl)
      ? "typed"
      : null;
  }
  return null;
}

/**
 * @param {import("typescript").SignatureDeclaration} node
 * @param {import("typescript").SourceFile} sf
 * @returns {string}
 *   The function's own name, or else the name it is assigned to, or
 *   "<anonymous>".
 */
function functionName(node, sf) {
  if (node.name) {
    return node.name.getText(sf);
  }
  if (ts.isConstructorDeclaration(node)) {
    return "constructor";
  }
  const p = node.parent;
  if (
    (ts.isVariableDeclaration(p) ||
      ts.isPropertyAssignment(p) ||
      ts.isPropertyDeclaration(p)) &&
    p.name
  ) {
    return p.name.getText(sf);
  }
  if (ts.isBinaryExpression(p)) {
    return p.left.getText(sf);
  }
  return "<anonymous>";
}

/**
 * Whether a JSDoc comment documents the function.
 *
 * @param {import("typescript").SignatureDeclaration} node
 */
function hasJSDoc(node) {
  // An arrow or function expression documents through its declaration.
  const holder =
    ts.isFunctionExpression(node) || ts.isArrowFunction(node)
      ? node.parent
      : node;
  return (
    !!ts.getJSDocCommentsAndTags(holder).length ||
    !!ts.getJSDocCommentsAndTags(node).length
  );
}

/**
 * Whether an @type or @satisfies gives the whole function a type, on the
 * function or on the declaration that holds it.
 *
 * @param {import("typescript").SignatureDeclaration} node
 */
function hasFunctionType(node) {
  const holder =
    ts.isFunctionExpression(node) || ts.isArrowFunction(node)
      ? node.parent
      : node;
  return !!(
    ts.getJSDocType(holder) ||
    ts.getJSDocType(node) ||
    ts.getJSDocSatisfiesTag(holder) ||
    ts.getJSDocSatisfiesTag(node)
  );
}

/**
 * Whether the function is a setter with a getter of the same name, from which
 * tsc types the setter's parameter.
 *
 * @param {import("typescript").SignatureDeclaration} node
 * @param {import("typescript").SourceFile} sf
 */
function hasPairedGetter(node, sf) {
  if (!ts.isSetAccessor(node)) {
    return false;
  }
  const setterName = node.name.getText(sf);
  const members = node.parent.members ?? node.parent.properties ?? [];
  return members.some(
    m => ts.isGetAccessor(m) && m.name.getText(sf) == setterName
  );
}

/**
 * Counts the functions with parameters in a file and their parameters.
 *
 * @param {string} file
 * @returns {{fns: number, documented: number, params: number, typed: number,
 *   gaps: string[]}}
 *   The counted functions, those with JSDoc, their parameters, the typed
 *   parameters, and a `file:line: name(params)` line for each function with
 *   an untyped parameter.
 */
function measure(file) {
  const sf = ts.createSourceFile(
    file,
    fs.readFileSync(file, "utf8"),
    ts.ScriptTarget.Latest,
    true,
    ts.ScriptKind.JS
  );
  const r = { fns: 0, documented: 0, params: 0, typed: 0, gaps: [] };
  const visit = node => {
    const counted =
      ts.isFunctionLike(node) &&
      node.body &&
      node.parameters.length &&
      !isCallOperand(node);
    const context = counted && literalContext(node);
    if (counted && context != "argument") {
      r.fns++;
      if (hasJSDoc(node)) {
        r.documented++;
      }
      const whole =
        hasFunctionType(node) ||
        hasPairedGetter(node, sf) ||
        context == "typed";
      const untyped = [];
      for (const p of node.parameters) {
        r.params++;
        if (whole || p.initializer || ts.getJSDocType(p)) {
          r.typed++;
        } else {
          untyped.push(p.name.getText(sf));
        }
      }
      if (untyped.length) {
        const line =
          sf.getLineAndCharacterOfPosition(node.getStart(sf)).line + 1;
        r.gaps.push(
          `${file}:${line}: ${functionName(node, sf)}(${untyped.join(", ")})`
        );
      }
    }
    ts.forEachChild(node, visit);
  };
  visit(sf);
  return r;
}

const pct = (a, b) => (b ? `${Math.floor((100 * a) / b)}%` : "-");
const rows = files.map(f => [f, measure(f)]);
const total = { fns: 0, documented: 0, params: 0, typed: 0 };
const width = Math.max(5, ...files.map(f => label(f).length));
const cell = (a, b) => `${`${a}/${b}`.padStart(8)} ${pct(a, b).padStart(5)}`;
const line = (rowLabel, r) =>
  `${rowLabel.padEnd(width)}  ${cell(r.documented, r.fns)}  ${cell(r.typed, r.params)}`;
console.log(
  `${"file".padEnd(width)}  ${"documented fns".padStart(14)}  ${"typed params".padStart(14)}`
);
for (const [f, r] of rows) {
  for (const k in total) {
    total[k] += r[k];
  }
  console.log(line(label(f), r));
}
if (rows.length > 1) {
  console.log(line("total", total));
}
console.log(
  "(functions with parameters only; callbacks, object-literal methods passed" +
    " as arguments and immediately invoked functions skipped)"
);
if (list) {
  for (const [, r] of rows) {
    for (const g of r.gaps) {
      console.log(g);
    }
  }
}
