/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Converts Markdown code fences, list items, code spans and JSDoc link tags in
 * doclet descriptions to reST.
 */

const LINK_TAG_RE =
  /(?:\[([^\]]+)\])?\{@link(?:code|plain)?\s+([^\s|}]+)\s*\|?\s*([^}]*)\}/g;
const CODE_SPAN_RE = /(?<=^|[\s(\[{<"'])`([^`\n]+)`(?=$|[\s.,;:!?)\]}>"'])/gm;
const FENCE_RE = /^\s*```(.*)$/;
const LIST_ITEM_RE = /^(?:[-*+]|\d+[.)]) /;
// A line starting with a private member, like `#field`, is not a heading.
const HEADING_RE = /^#{1,6}(?:\s|$)/;
// The rustdoc attributes a code fence of the Rust component bindings can carry
// instead of a language.
const RUSTDOC_ATTRIBUTE_RE =
  /^(?:rust|no_run|ignore(?:-\S+)?|should_panic|compile_fail|test_harness|standalone_crate|edition\d+)$/;

function fenceLanguage(info) {
  let language = info.trim().split(/[,\s]+/)[0];
  return !language || RUSTDOC_ATTRIBUTE_RE.test(language) ? "rust" : language;
}

/**
 * Turns code fences into code-block directives, and indents the continuation
 * lines of list items under their text, which Markdown allows to be flush.
 * reST also needs a blank line before a list or a heading that follows text.
 *
 * @param {string} text
 */
function convert(text) {
  // Link tags can wrap onto the next line, so convert them before splitting.
  text = text.replace(LINK_TAG_RE, (_, before, target, after) => {
    let label = (before || after).trim() || target.split("/").pop();
    return `\`\`${label}\`\``;
  });
  let lines = [];
  let inCode = false;
  let listIndent = 0;
  let separate = () => {
    if (lines.length && lines.at(-1).trim()) {
      lines.push("");
    }
  };
  for (let line of text.split("\n")) {
    let fenceMatch = line.match(FENCE_RE);
    let trimmed = line.trimStart();
    let item = trimmed.match(LIST_ITEM_RE);
    if (fenceMatch) {
      if (inCode) {
        lines.push("");
      } else {
        separate();
        lines.push(`.. code-block:: ${fenceLanguage(fenceMatch[1])}`, "");
        listIndent = 0;
      }
      inCode = !inCode;
    } else if (inCode) {
      lines.push(line && `    ${line}`);
    } else if (!trimmed) {
      listIndent = 0;
      lines.push(line);
    } else if (HEADING_RE.test(trimmed)) {
      separate();
      listIndent = 0;
      lines.push(line.replace(CODE_SPAN_RE, "``$1``"));
    } else if (item) {
      if (!listIndent) {
        separate();
      }
      listIndent = line.length - trimmed.length + item[0].length;
      lines.push(line.replace(CODE_SPAN_RE, "``$1``"));
    } else {
      let padding = Math.max(0, listIndent - (line.length - trimmed.length));
      lines.push(" ".repeat(padding) + line.replace(CODE_SPAN_RE, "``$1``"));
    }
  }
  return lines.join("\n");
}

function convertDescription(item) {
  if (typeof item?.description == "string") {
    item.description = convert(item.description);
  }
}

export const handlers = {
  newDoclet({ doclet }) {
    convertDescription(doclet);
    for (let field of ["classdesc", "summary"]) {
      if (typeof doclet[field] == "string") {
        doclet[field] = convert(doclet[field]);
      }
    }
    for (let field of ["params", "properties", "returns", "exceptions"]) {
      doclet[field]?.forEach(convertDescription);
    }
  },
};
