// |reftest| shell-option(--enable-regexp-buffer-boundaries) skip-if(!xulRuntime.shell) -- requires shell-options
// Copyright 2026 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Ron Buckton
description: >
  `\Z` (upper-case z) matches end of buffer after optional LineTerminatorSequence in u-mode.
info: |
  Runtime Semantics: CompileAssertion
  
  Assertion :: \Z
    1. Return a new Matcher with parameters (matchState, continue) that captures nothing and performs the following steps when called:
      a. Assert: matchState is a MatchState.
      b. Assert: continue is a MatcherContinuation.
      c. Let input be matchState.[[Input]].
      d. Let e be matchState.[[EndIndex]].
      e. Let inputLength be the number of elements in input.
      f. If e = inputLength, return continue(matchState).
      g. If e = inputLength - 1 and the character input[e] is matched by |LineTerminator|, then
          i. Return continue(matchState).
      h. If e = inputLength - 2, the character input[e] is matched by <CR>; and the character input[e + 1] is matched by <LF>;, then
          i. Return continue(matchState).
      i. Return failure.

esid: sec-patterns
features: [regexp-buffer-boundaries]
---*/

var terminators = [
  ["\n", "<LF>"],
  ["\r", "<CR>"],
  ["\u2028", "<LS>"],
  ["\u2029", "<PS>"],
  ["\r\n", "<CRLF>"],
];

assert(/x\Z/u.test("x"), "Expected \\Z to match end-of-buffer outside of multiline mode (literal)");
assert(/x\Z/um.test("x"), "Expected \\Z to match end-of-buffer inside of multiline mode (literal)");
assert(!/x\Z/u.test("xy"), "Expected \\Z to not match when not at end-of-buffer outside of multiline mode (literal)");
assert(!/x\Z/um.test("xy"), "Expected \\Z to not match when not at end-of-buffer inside of multiline mode (literal)");

for (var i = 0; i < terminators.length; i++) {
  var lts = terminators[i][0], desc = terminators[i][1];
  assert(/x\Z/u.test("x" + lts), "Expected \\Z to match when at " + desc + " before end-of-buffer outside of multiline mode (literal)");
  assert(/x\Z/um.test("x" + lts), "Expected \\Z to match when at " + desc + " before end-of-buffer inside of multiline mode (literal)");
  assert(!/x\Z/u.test("x" + lts + "y"), "Expected \\Z to not match when at " + desc + " but not before end-of-buffer outside of multiline mode (literal)");
  assert(!/x\Z/um.test("x" + lts + "y"), "Expected \\Z to not match when at " + desc + " but not before end-of-buffer inside of multiline mode (literal)");
}

assert(new RegExp("x\\Z", "u").test("x"), "Expected \\Z to match end-of-buffer outside of multiline mode (constructed)");
assert(new RegExp("x\\Z", "um").test("x"), "Expected \\Z to match end-of-buffer inside of multiline mode (constructed)");
assert(!new RegExp("x\\Z", "u").test("xy"), "Expected \\Z to not match when not at end-of-buffer outside of multiline mode (constructed)");
assert(!new RegExp("x\\Z", "um").test("xy"), "Expected \\Z to not match when not at end-of-buffer inside of multiline mode (constructed)");

for (var i = 0; i < terminators.length; i++) {
  var lts = terminators[i][0], desc = terminators[i][1];
  assert(new RegExp("x\\Z", "u").test("x" + lts), "Expected \\Z to match when at " + desc + " before end-of-buffer outside of multiline mode (constructed)");
  assert(new RegExp("x\\Z", "um").test("x" + lts), "Expected \\Z to match when at " + desc + " before end-of-buffer inside of multiline mode (constructed)");
  assert(!new RegExp("x\\Z", "u").test("x" + lts + "y"), "Expected \\Z to not match when at " + desc + " but not before end-of-buffer outside of multiline mode (constructed)");
  assert(!new RegExp("x\\Z", "um").test("x" + lts + "y"), "Expected \\Z to not match when at " + desc + " but not before end-of-buffer inside of multiline mode (constructed)");
}

reportCompare(0, 0);
