// |reftest| shell-option(--enable-regexp-buffer-boundaries) skip-if(!xulRuntime.shell) -- requires shell-options
// Copyright 2026 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Ron Buckton
description: >
  `\A` matches start of buffer in v-mode.
info: |
  Runtime Semantics: CompileAssertion
  
  Assertion :: \A
    1. Return a new Matcher with parameters (matchState, continue) that captures nothing and performs the following steps when called:
      a. Assert: matchState is a MatchState.
      b. Assert: continue is a MatcherContinuation.
      c. Let e be matchState.[[EndIndex]].
      d. If e = 0, return continue(matchState).
      e. Return ~failure~.

esid: sec-patterns
features: [regexp-buffer-boundaries, regexp-v-flag]
---*/

assert(/\Ax/v.test("x"), "Expected \\A to match start-of-buffer outside of multiline mode (literal)");
assert(/\Ax/vm.test("x"), "Expected \\A to match start-of-buffer inside of multiline mode (literal)");
assert(!/\Ax/v.test("yx"), "Expected \\A to not match when not at start-of-buffer outside of multiline mode (literal)");
assert(!/\Ax/vm.test("yx"), "Expected \\A to not match when not at start-of-buffer inside of multiline mode (literal)");
assert(!/\Ax/v.test("\nx"), "Expected \\A to not match when at start-of-new-line outside of multiline mode (literal)");
assert(!/\Ax/vm.test("\nx"), "Expected \\A to not match when at start-of-new-line inside of multiline mode (literal)");

assert(new RegExp("\\Ax", "v").test("x"), "Expected \\A to match start-of-buffer outside of multiline mode (constructed)");
assert(new RegExp("\\Ax", "vm").test("x"), "Expected \\A to match start-of-buffer inside of multiline mode (constructed)");
assert(!new RegExp("\\Ax", "v").test("yx"), "Expected \\A to not match when not at start-of-buffer outside of multiline mode (constructed)");
assert(!new RegExp("\\Ax", "vm").test("yx"), "Expected \\A to not match when not at start-of-buffer inside of multiline mode (constructed)");
assert(!new RegExp("\\Ax", "v").test("\nx"), "Expected \\A to not match when at start-of-new-line outside of multiline mode (constructed)");
assert(!new RegExp("\\Ax", "vm").test("\nx"), "Expected \\A to not match when at start-of-new-line inside of multiline mode (constructed)");

reportCompare(0, 0);
