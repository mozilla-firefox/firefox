/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A property documented with `@type` is conventionally written in this tree
// with its prose under the tag, but jsdoc reserves the description for the
// text *above* the first tag, so it discards that prose with a "@type tag does
// not permit a description" warning and the property renders with its type and
// nothing else. Move the prose above the tag, where jsdoc reads it, permuting
// whole lines so that nothing in the file is renumbered.

const DOC_COMMENT_RE = /\/\*\*[\s\S]*?\*\//g;

function tagOn(line) {
  return /^[ \t]*\*?[ \t]*@(\w+)/.exec(line)?.[1];
}

function rewriteComment(comment) {
  let lines = comment.split("\n");
  let tagIndex = lines.findIndex(tagOn);
  // Only a leading `@type` can be handled: the moved prose has to land in the
  // description, and past another tag it would be read as that tag's text.
  if (tagIndex < 1 || tagOn(lines[tagIndex]) !== "type") {
    return comment;
  }

  // The type expression can span lines, and text trailing it on its last line
  // is left alone so that a partial line never has to move.
  let endIndex = tagIndex;
  let depth = 0;
  do {
    for (let char of lines[endIndex]) {
      depth += (char === "{") - (char === "}");
    }
  } while (depth > 0 && ++endIndex < lines.length);
  if (depth > 0 || !/\}[ \t]*(\*\/)?[ \t]*$/.test(lines[endIndex])) {
    return comment;
  }

  let descStart = endIndex + 1;
  let descEnd = descStart;
  while (descEnd < lines.length && !tagOn(lines[descEnd])) {
    descEnd++;
  }
  // Leave the closing `*/`, and any blank line padding it, where they are.
  if (descEnd === lines.length) {
    descEnd--;
    // Prose sharing that line cannot move with the rest, and moving the lines
    // above it alone would leave it under the tag, still discarded.
    if (!/^[ \t]*(\*[ \t]*)?\*\/[ \t]*$/.test(lines[descEnd])) {
      return comment;
    }
  }
  while (descEnd > descStart && !lines[descEnd - 1].replace(/[ \t*]/g, "")) {
    descEnd--;
  }
  if (descEnd <= descStart) {
    return comment;
  }

  return [
    ...lines.slice(0, tagIndex),
    ...lines.slice(descStart, descEnd),
    ...lines.slice(tagIndex, descStart),
    ...lines.slice(descEnd),
  ].join("\n");
}

export const handlers = {
  beforeParse(e) {
    e.source = e.source.replace(DOC_COMMENT_RE, rewriteComment);
  },
};
