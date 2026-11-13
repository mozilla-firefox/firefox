// |reftest| shell-option(--enable-regexp-buffer-boundaries) skip-if(!xulRuntime.shell) -- requires shell-options
// Copyright 2026 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Ron Buckton
description: >
  `\A`, `\z`, and `\Z` are treated as IdentityEscape in Annex B outside any unicode mode
info: |
  Patterns
  
  Assertion[UnicodeMode, UnicodeSetsMode, NamedCaptureGroups] :
    ...
    [+UnicodeMode] `\` `A`
    [+UnicodeMode] `\` `z`
    ...

  IdentityEscape[UnicodeMode, NamedCaptureGroups] :
    [+UnicodeMode] SyntaxCharacter
    [+UnicodeMode] `/`
    [~UnicodeMode] SourceCharacterIdentityEscape[?NamedCaptureGroups]

  SourceCharacterIdentityEscape[NamedCaptureGroups] ::
    [~NamedCaptureGroups] SourceCharacter but not `c`
    [+NamedCaptureGroups] SourceCharacter but not one of `c` or `k`

esid: sec-regular-expressions-patterns
features: [regexp-buffer-boundaries]
---*/

// `\A` is |IdentityEscape| outside of any unicode mode
assert(/^\A$/.test("A"), "Expected \\A to match literal 'A' outside of unicode mode. (literal)");
assert(!/\Ax/.test("x"), "Expected \\A to not match start-of-buffer outside of unicode mode. (literal)");
assert(new RegExp("^\\A$").test("A"), "Expected \\A to match literal 'A' outside of unicode mode. (constructed)");
assert(!new RegExp("\\Ax").test("x"), "Expected \\A to not match at start-of-buffer outside of unicode mode. (constructed)");

// `\z` is |IdentityEscape| outside of any unicode mode
assert(/^\z$/.test("z"), "Expected \\z to match literal 'z' outside of unicode mode. (literal)");
assert(!/x\z/.test("x"), "Expected \\z to not match end-of-buffer outside of unicode mode. (literal)");
assert(new RegExp("^\\z$").test("z"), "Expected \\z to match literal 'z' outside of unicode mode. (constructed)");
assert(!new RegExp("x\\z").test("x"), "Expected \\z to not match at end-of-buffer outside of unicode mode. (constructed)");

// `\Z` is |IdentityEscape| outside of any unicode mode
assert(/^\Z$/.test("Z"), "Expected \\Z to match literal 'Z' outside of unicode mode. (literal)");
assert(!/^x\Z$/.test("x"), "Expected \\Z to not match end-of-buffer outside of unicode mode. (literal)");
assert(new RegExp("^\\Z$").test("Z"), "Expected \\Z to match literal 'Z' outside of unicode mode. (constructed)");
assert(!new RegExp("^x\\Z$").test("x"), "Expected \\Z to not match at end-of-buffer outside of unicode mode. (constructed)");

var terminators = [
  ["\n", "<LF>"],
  ["\r", "<CR>"],
  ["\u2028", "<LS>"],
  ["\u2029", "<PS>"],
  ["\r\n", "<CRLF>"],
];

for (var i = 0; i < terminators.length; i++) {
  var lts = terminators[i][0], desc = terminators[i][1];
  assert(!/^x\Z$/.test("x" + lts), "Expected \\Z to not match at " + desc + " before end-of-buffer outside of unicode mode. (literal)");
  assert(!new RegExp("^x\\Z$").test("x" + lts), "Expected \\Z to not match at " + desc + " before end-of-buffer outside of unicode mode. (constructed)");
}

reportCompare(0, 0);
