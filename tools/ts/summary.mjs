/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Run tsc on one project and summarize its errors. `./mach ts summary` runs
// this script:
//
//   summary.mjs PROJECT [--members TYPE] [--full-lists]
//
// PROJECT is a directory with a tsconfig.json, or a tsconfig file.
//
// It prints the error total, then the counts per file, per error code with
// TypeScript's message for it, per unresolved name (TS2304 and TS2552) and per
// type that lacks a property (TS2339 and TS2551). --members TYPE adds the
// missing members of that one type. Lists stop at 25 entries unless
// --full-lists.
/* eslint-disable no-console */

import { spawnSync } from "child_process";
import fs from "fs";
import { createRequire } from "module";
import path from "path";

const LIST_LIMIT = 25;

function usage() {
  console.error(
    "usage: mach ts summary PROJECT [--members TYPE] [--full-lists]"
  );
  process.exit(2);
}

/**
 * Splits tsc's output into its errors. tsc prints each error as a
 * `path(line,col): error TSnnnn: message` line, or `error TSnnnn: message` for
 * one without a location, followed by indented lines that elaborate it.
 *
 * @param {string} tscOutput
 * @returns {string[]}
 *   One string per error, its elaboration lines joined to its first line with
 *   newlines.
 */
function parseErrors(tscOutput) {
  const errors = [];
  for (const line of tscOutput.split(/\r?\n/)) {
    if (/error TS\d+/.test(line) && !/^\s/.test(line)) {
      errors.push(line);
    } else if (errors.length && /^\s+\S/.test(line)) {
      errors[errors.length - 1] += "\n" + line;
    }
  }
  return errors;
}

/**
 * Counts how often each distinct value occurs.
 *
 * @param {string[]} values
 * @returns {[string, number][]}
 *   Each distinct value with its count, the most frequent first and ties in
 *   alphabetical order.
 */
function countOccurrences(values) {
  const counts = new Map();
  for (const value of values) {
    counts.set(value, (counts.get(value) ?? 0) + 1);
  }
  return [...counts].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1));
}

/**
 * Prints a heading and a line per distinct value with its count. Prints
 * nothing when there are no values.
 *
 * @param {string} heading
 * @param {string[]} values
 * @param {boolean} fullLists
 *   Whether to list every value rather than the LIST_LIMIT most frequent.
 */
function printCounts(heading, values, fullLists) {
  const rows = countOccurrences(values);
  if (!rows.length) {
    return;
  }

  console.log(`\n${heading}:`);
  for (const [value, count] of fullLists ? rows : rows.slice(0, LIST_LIMIT)) {
    console.log(`${String(count).padStart(6)}  ${value}`);
  }
  if (!fullLists && rows.length > LIST_LIMIT) {
    console.log(
      `        ... ${rows.length - LIST_LIMIT} more (--full-lists shows them)`
    );
  }
}

/**
 * @param {string[]} errors
 * @param {RegExp} regex
 *   A global regular expression with one capture group.
 * @returns {string[]}
 *   The captured text of every match in every error.
 */
function collectCaptures(errors, regex) {
  return errors.flatMap(error => [...error.matchAll(regex)].map(m => m[1]));
}

/**
 * Reads TypeScript's message for each error code from its package.
 *
 * @param {string} topsrcdir
 * @returns {Map<number, string>}
 */
function readErrorMessages(topsrcdir) {
  const ts = createRequire(path.join(topsrcdir, "package.json"))("typescript");
  return new Map(
    Object.values(ts.Diagnostics ?? {}).map(d => [d.code, d.message])
  );
}

function plural(count, noun) {
  return `${count} ${noun}${count == 1 ? "" : "s"}`;
}

/**
 * Whether an error is about the project's configuration rather than its code:
 * an option or tsconfig error (TS5xxx), no input files (TS18003), a missing
 * type library (TS2688), or any error located in a .json file.
 *
 * @param {string} error
 */
function isSetupError(error) {
  return (
    /^error TS(5\d+|18003|2688):/.test(error) ||
    /^[^(]*\.json\(\d+,\d+\): error/.test(error)
  );
}

/**
 * Exits with 1 after printing the setup errors, if there are any, since the
 * rest of tsc's errors then don't reflect the code.
 *
 * @param {string[]} errors
 */
function exitOnSetupError(errors) {
  const setupErrors = errors.filter(isSetupError);
  if (!setupErrors.length) {
    return;
  }

  console.log(`tsc reported a problem with the project setup. It printed:

${setupErrors.join("\n")}

Check for an \`extends\` path that does not resolve from the tsconfig's
directory (TS5083, with TS18003 when the \`include\` paths miss too), or a
missing or stale top-level node_modules (\`./mach eslint --setup\` reinstalls
it).`);
  process.exit(1);
}

/**
 * Prints the error total and the error counts per file, per error code, per
 * unresolved name and per type that lacks a property.
 *
 * @param {string[]} errors
 * @param {string} [membersOfType]
 *   A type to also list the missing properties of.
 * @param {boolean} fullLists
 *   Whether to list every entry.
 * @param {Map<number, string>} errorMessages
 *   TypeScript's message for each error code.
 */
function printSummary(errors, membersOfType, fullLists, errorMessages) {
  console.log(plural(errors.length, "error"));
  if (!errors.length) {
    return;
  }

  printCounts(
    "per file",
    errors.map(e => /^(.+?)\(\d+,\d+\): error/.exec(e)?.[1] ?? "(no file)"),
    fullLists
  );
  printCounts(
    "per error code",
    errors.map(e => {
      const code = Number(/error TS(\d+)/.exec(e)[1]);
      return `${`TS${code}`.padEnd(9)}${errorMessages.get(code) ?? ""}`.trimEnd();
    }),
    fullLists
  );
  printCounts(
    "unresolved names (TS2304, TS2552)",
    collectCaptures(
      errors,
      /error TS(?:2304|2552): Cannot find name '([^']+)'/g
    ),
    fullLists
  );
  printCounts(
    "types lacking a property (TS2339, TS2551)",
    collectCaptures(
      errors,
      /error TS(?:2339|2551): Property '[^']+' does not exist on type '([^']+)'/g
    ),
    fullLists
  );

  if (membersOfType) {
    const escapedType = membersOfType.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const missingMembers = collectCaptures(
      errors,
      new RegExp(
        `error TS(?:2339|2551): Property '([^']+)' does not exist on type '${escapedType}'`,
        "g"
      )
    );
    if (missingMembers.length) {
      printCounts(
        `members missing from ${membersOfType}`,
        missingMembers,
        true
      );
    } else {
      console.log(`\nNo TS2339/TS2551 errors name type '${membersOfType}'.`);
    }
  }
}

/**
 * @returns {{projectPath: string, membersOfType?: string,
 *   fullLists: boolean}}
 */
function parseArgs() {
  const args = process.argv.slice(2);
  const options = { fullLists: false };
  const optionValue = () => {
    const value = args.shift();
    if (value === undefined || value.startsWith("--")) {
      usage();
    }
    return value;
  };
  while (args.length) {
    const arg = args.shift();
    if (arg == "--members") {
      options.membersOfType = optionValue();
    } else if (arg == "--full-lists") {
      options.fullLists = true;
    } else if (!arg.startsWith("--") && !options.projectPath) {
      options.projectPath = arg;
    } else {
      usage();
    }
  }

  if (!options.projectPath) {
    usage();
  }
  return options;
}

/**
 * Exits with 2 unless `projectPath` is a directory with a tsconfig.json or a
 * tsconfig file.
 *
 * @param {string} projectPath
 */
function checkProjectPath(projectPath) {
  if (
    fs.statSync(projectPath, { throwIfNoEntry: false })?.isDirectory()
      ? !fs.existsSync(path.join(projectPath, "tsconfig.json"))
      : !projectPath.endsWith(".json") || !fs.existsSync(projectPath)
  ) {
    console.error(
      `${projectPath} is neither a directory with a tsconfig.json nor a tsconfig file.`
    );
    process.exit(2);
  }
}

/**
 * Runs tsc on the project and returns its output. Exits with 1 if tsc did not
 * finish, or failed without reporting an error, since its output then is not
 * a count.
 *
 * @param {string} topsrcdir
 * @param {string} projectPath
 * @returns {string}
 */
function runTsc(topsrcdir, projectPath) {
  const tsc = path.join(topsrcdir, "node_modules", "typescript", "bin", "tsc");
  if (!fs.existsSync(tsc)) {
    console.error(`${tsc} not found: run this through \`./mach ts summary\`.`);
    process.exit(2);
  }

  const tscProcess = spawnSync(
    process.execPath,
    [tsc, "--project", projectPath, "--noEmit", "--pretty", "false"],
    { cwd: topsrcdir, encoding: "utf8", maxBuffer: 1 << 28 }
  );
  const tscOutput = (tscProcess.stdout ?? "") + (tscProcess.stderr ?? "");

  if (tscProcess.error || tscProcess.signal) {
    console.log(
      `tsc did not finish (${tscProcess.error?.message ?? tscProcess.signal}),` +
        " so this is not a count."
    );
    process.exit(1);
  }

  if (tscProcess.status && !parseErrors(tscOutput).length) {
    console.log(
      `tsc exited with status ${tscProcess.status} without reporting an` +
        " error, so this is not a count. It printed:\n"
    );
    const lines = tscOutput.trimEnd().split("\n");
    const exceptionLine = lines.find(l => /^\w*Error\b/.test(l));
    console.log(exceptionLine ?? lines.slice(-8).join("\n"));
    if (tscOutput.includes("Cannot find module")) {
      console.log(
        "\nA plugin `transform` path that does not resolve from the" +
          " tsconfig's directory throws this."
      );
    }
    process.exit(1);
  }

  return tscOutput;
}

function main() {
  const options = parseArgs();
  const projectPath = path.resolve(options.projectPath);
  checkProjectPath(projectPath);

  const topsrcdir = path.join(import.meta.dirname, "..", "..");
  const tscOutput = runTsc(topsrcdir, projectPath);
  const errors = parseErrors(tscOutput);
  exitOnSetupError(errors);
  printSummary(
    errors,
    options.membersOfType,
    options.fullLists,
    readErrorMessages(topsrcdir)
  );
}

main();
