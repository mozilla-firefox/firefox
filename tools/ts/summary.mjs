/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Run tsc on one project and summarize its errors, or compare them with a
// saved run. `./mach ts summary` runs this script:
//
//   summary.mjs PROJECT [--save FILE] [--members TYPE] [--full-lists]
//
// PROJECT is a directory with a tsconfig.json, or a tsconfig file.
//
// This prints the error total, then the counts per file, per error
// code with TypeScript's message for it, per unresolved name (TS2304 and
// TS2552) and per type that lacks a property (TS2339 and TS2551). --members
// TYPE adds the missing members of that one type. Lists stop at 25 entries
// unless --full-lists. --save FILE writes tsc's raw output for a later --diff,
// unless the run failed or hit a setup error.
//
//   summary.mjs PROJECT [--save FILE] --diff BEFORE
//
// --diff compares the errors in the output saved in BEFORE with this run's,
// as two sets. Errors match without their (line,col) positions, so a line
// shift is no change. Equal totals with a fix and a new error show as one
// error removed and one added.
/* eslint-disable no-console */

import { spawnSync } from "child_process";
import fs from "fs";
import { createRequire } from "module";
import path from "path";

const LIST_LIMIT = 25;

function usage() {
  console.error(
    "usage: mach ts summary PROJECT [--save FILE] [--members TYPE] [--full-lists]\n" +
      "       mach ts summary PROJECT [--save FILE] --diff BEFORE"
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

const CLOSING_BRACKETS = { "<": ">", "(": ")", "[": "]", "{": "}" };

/**
 * Sorts the members of every union in a type as tsc prints it, at any depth,
 * so that two unions with the same members compare equal.
 *
 * @param {string} type
 * @returns {string}
 *   A string to compare, not a valid type.
 */
function sortUnions(type) {
  let pos = 0;

  // Reads from `pos` up to `closingBracket` or the end of the type, and
  // returns that part with each union in it sorted. A union ends at a
  // delimiter (`,`, `;`, `:` or `=>`) or at the end of the part.
  const readUntil = closingBracket => {
    let result = "";
    let unionMembers = [];
    let currentMember = "";
    const endUnion = () => {
      unionMembers.push(currentMember.trim());
      result += unionMembers.sort().join(" | ");
      unionMembers = [];
      currentMember = "";
    };

    while (pos < type.length && type[pos] != closingBracket) {
      const char = type[pos];
      if (char == '"' || char == "`") {
        let stringEnd = pos + 1;
        while (stringEnd < type.length && type[stringEnd] != char) {
          stringEnd += type[stringEnd] == "\\" ? 2 : 1;
        }
        currentMember += type.slice(pos, stringEnd + 1);
        pos = stringEnd + 1;
      } else if (char in CLOSING_BRACKETS) {
        pos++;
        const inner = readUntil(CLOSING_BRACKETS[char]);
        currentMember += char + inner + (type[pos] ?? "");
        pos++;
      } else if (type.startsWith(" | ", pos)) {
        unionMembers.push(currentMember.trim());
        currentMember = "";
        pos += 3;
      } else if (",;:".includes(char) || type.startsWith("=>", pos)) {
        const delimiter = char == "=" ? "=>" : char;
        endUnion();
        result += delimiter;
        pos += delimiter.length;
      } else {
        currentMember += char;
        pos++;
      }
    }

    endUnion();
    return result;
  };

  return readUntil();
}

/**
 * Returns the string that identifies an error across two runs: its first
 * line, without the (line,col) position, with the unions in each quoted type
 * sorted, since tsc orders a union's members by when their types were
 * created, which an unrelated edit can change.
 *
 * @param {string} error
 */
function matchKey(error) {
  return error
    .split("\n", 1)[0]
    .replace(/\(\d+,\d+\)/, "")
    .replace(/(?<!\w)'(.*?)'(?!\w)/g, (_, type) => `'${sortUnions(type)}'`);
}

/**
 * Reads the errors from tsc output saved with --save. Accepts a file that a
 * Windows shell re-encoded as UTF-16 or with CRLF line ends, and exits with 2
 * if the file holds text but no errors.
 *
 * @param {string} file
 * @returns {string[]}
 */
function readSavedErrors(file) {
  let bytes;
  try {
    bytes = fs.readFileSync(file);
  } catch (e) {
    console.error(`cannot read ${file}: ${e.message}`);
    process.exit(2);
  }

  const isUtf16 = bytes[0] == 0xff && bytes[1] == 0xfe;
  const text = bytes
    .toString(isUtf16 ? "utf16le" : "utf8")
    .replace(/^\uFEFF/, "")
    .replace(/\r\n/g, "\n");
  const errors = parseErrors(text);
  if (!errors.length && text.trim()) {
    console.error(
      /^\d+ errors?$/.test(text.split("\n", 1)[0])
        ? `${file} holds the printed summary, not tsc's output; rerun with --save`
        : `${file} holds no tsc errors; write it with --save`
    );
    process.exit(2);
  }
  return errors;
}

/**
 * Prints the errors of the earlier run that the later one lacks, and the
 * errors of the later run that the earlier one lacks, matched by matchKey().
 *
 * @param {string[]} beforeErrors
 * @param {string[]} afterErrors
 * @returns {boolean}
 *   Whether the two runs have the same errors.
 */
function printDifferences(beforeErrors, afterErrors) {
  const unmatchedAfter = new Map();
  for (const error of afterErrors) {
    const key = matchKey(error);
    let sameKeyErrors = unmatchedAfter.get(key);
    if (!sameKeyErrors) {
      sameKeyErrors = [];
      unmatchedAfter.set(key, sameKeyErrors);
    }
    sameKeyErrors.push(error);
  }

  const removed = [];
  for (const error of beforeErrors) {
    const sameKeyErrors = unmatchedAfter.get(matchKey(error));
    if (sameKeyErrors?.length) {
      sameKeyErrors.pop();
    } else {
      removed.push(error);
    }
  }
  const added = [...unmatchedAfter.values()].flat();

  const identical = !removed.length && !added.length;
  console.log(
    `${beforeErrors.length} before, ${afterErrors.length} after, ` +
      (identical
        ? "identical sets"
        : `${removed.length} removed, ${added.length} added`)
  );
  for (const error of removed.sort()) {
    console.log(`- ${error}`);
  }
  for (const error of added.sort()) {
    console.log(`+ ${error}`);
  }
  return identical;
}

/**
 * @returns {{projectPath: string, saveFile?: string, beforeFile?: string,
 *   membersOfType?: string, fullLists: boolean}}
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
    if (arg == "--save") {
      options.saveFile = optionValue();
    } else if (arg == "--diff") {
      options.beforeFile = optionValue();
    } else if (arg == "--members") {
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
  if (options.beforeFile && (options.membersOfType || options.fullLists)) {
    console.error(
      "--diff lists the changed errors instead of the summary, so it can't" +
        " be combined with --members or --full-lists."
    );
    process.exit(2);
  }
  return options;
}

/**
 * Exits with 2 unless `projectPath` is a directory with a tsconfig.json or a
 * tsconfig file, and `saveFile`, if given, is a writable file name.
 *
 * @param {string} projectPath
 * @param {string} [saveFile]
 */
function checkPaths(projectPath, saveFile) {
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

  if (!saveFile) {
    return;
  }
  const saveDir = path.dirname(saveFile);
  if (!fs.statSync(saveDir, { throwIfNoEntry: false })?.isDirectory()) {
    console.error(
      `${saveDir} is not a directory, so --save cannot write there.`
    );
    process.exit(2);
  }
  if (fs.statSync(saveFile, { throwIfNoEntry: false })?.isDirectory()) {
    console.error(`${saveFile} is a directory; give --save a file name.`);
    process.exit(2);
  }
  try {
    fs.accessSync(
      fs.existsSync(saveFile) ? saveFile : saveDir,
      fs.constants.W_OK
    );
  } catch {
    console.error(`${saveFile} is not writable, so --save cannot write there.`);
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
  const saveFile = options.saveFile && path.resolve(options.saveFile);
  checkPaths(projectPath, saveFile);

  // The errors of the saved run that --diff compares this run with.
  const beforeErrors =
    options.beforeFile && readSavedErrors(options.beforeFile);

  const topsrcdir = path.join(import.meta.dirname, "..", "..");
  const tscOutput = runTsc(topsrcdir, projectPath);
  const errors = parseErrors(tscOutput);
  exitOnSetupError(errors);

  if (saveFile) {
    fs.writeFileSync(saveFile, tscOutput);
  }

  if (beforeErrors) {
    process.exitCode = printDifferences(beforeErrors, errors) ? 0 : 1;
    return;
  }
  printSummary(
    errors,
    options.membersOfType,
    options.fullLists,
    readErrorMessages(topsrcdir)
  );
}

main();
