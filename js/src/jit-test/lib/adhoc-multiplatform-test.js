
// This file provides a version of the functions
//
//   codegenTestX64_adhoc   (src/jit-test/lib/codegen-x64-test.js)
//   codegenTestX86_adhoc   (src/jit-test/lib/codegen-x86-test.js)
//   codegenTestARM64_adhoc (src/jit-test/lib/codegen-arm64-test.js)
//   (and the equivalent arm(32) function)
//
// generalised so the output can be specified for all 4 targets in one place.
//
// Usage:
//   codegenTestMultiplatform_adhoc(module_text, export_name,
//                                  expectedAllTargets, options = {})
//
// where
//   `expectedAllTargets` states the expected-output regexps for each target,
//   thusly:
//
//   {x64: 'text', x86: 'text', arm64: 'text', arm: 'text'}
//
//   The arm(32) expected output is optional.  The other 3 must be present.
//
//   Each 'text' is a string that represents a regular expression, possibly
//   with newlines, representing the instruction or instructions we are looking
//   for for the operator.  Spaces in the expected-pattern are arbitrary, we
//   preprocess the pattern to replace any space string with \s+.  Lines are
//   separated by newlines and leading and trailing spaces are stripped.
//   Pattern strings may be empty, denoting "no instruction(s)".
//
//  options specifies options thusly:
//
//    instanceBox: if present, an object with a `value` property that will
//                 receive the constructed instance
//
//    log: for debugging -- print the disassembly and other info helpful to
//         resolving test failures.  This is also printed on a test failure
//         regardless of the log setting.
//
//    features: this is passed on verbatim to wasmEvalText,
//              as its third argument.
//
//    no_prefix: by default, the required pattern must be immediately preceded
//               by `<target>_prefix`, and this is checked.  Setting this to
//               true skips the check.  Try not to use this.
//
//    no_suffix: by default, the required pattern must be immediately followed
//               by `<target>_suffix`, and this is checked.  Setting this to
//               true skips the check.  Try not to use this.
//
//    no_prefix/no_suffix apply to all 4 targets.  Per-target overrides are
//    supported, by putting them in a suitably tagged sub-object, eg:
//    options = {x86: {no_prefix: true}}

load(libdir + "codegen-test-common.js");

// Architectures supported by this script.
const knownArchs = ["x64", "x86", "arm64", "arm"];

// Architectures for which `expectedAllTargets` must supply an expected result.
const requiredArchs = ["x64", "x86", "arm64"];

// These define the end-of-prologue ("prefix") and start-of-epilogue
// ("suffix") to be matched.
const archOptions =
      {x64: {
           encoding: `(?:${HEX}{2} )*`,
           // The move from r14 to rbp is writing the callee's wasm instance
           // into the frame for debug checks -- see WasmFrame.h.
           prefix: `mov %rsp, %rbp(
                    movq %r14, (0x10|0x30)\\(%rbp\\))?`,
           suffix: `pop %rbp`
       },
       x86: {
           encoding: `(?:${HEX}{2} )*`,
           // The move from esi to rbp is writing the callee's wasm instance
           // into the frame for debug checks -- see WasmFrame.h. The mov to
           // e[ac]x is debug code, inserted by the register allocator to
           // clobber e[ac]x before a move group.  But it is only present if
           // there is a move group there.
           prefix: `mov %esp, %ebp(
                    movl %esi, 0x08\\(%rbp\\))?(
                    mov \\$0xDEADBEEF, %e.x)?`,
           // `.bp` because zydis chooses `rbp` even on 32-bit systems.
           suffix: `pop %.bp`
       },
       arm64: {
           encoding: `${HEX}{8}`,
           // The move from x23 to x29 is writing the callee's wasm instance
           // into the frame for debug checks -- see WasmFrame.h.
           prefix: `mov x29, sp
                    mov x20, sp(
                    bti j)?(
                    str x23, \\[x29, #16\\])?`,
           suffix: `ldp x29, x30, \\[sp\\], #16`
       },
       arm: {
           encoding: `${HEX}{8}`,
           // The move from r9 to fp is writing the callee's wasm instance into
           // the frame for debug checks -- see WasmFrame.h.
           prefix: `str fp, \\[sp, #-4\\]!
                    mov fp, sp(
                    str r9, \\[fp, #\\+8\\])?`,
           suffix: `ldr fp, \\[sp\\], #\\+4`
       }
      };

// The options object may a mix of generic (all-targets) options and contain
// sub-objects containing arch-specific options, for example:
//
//   {a_generic_option: 1337, x86: {no_prefix:true}, arm64: {foo:4771}}
//
// promoteArchSpecificOptions lifts options for `archName` to the top level
// and deletes *all* arch-specific subobjects, hence producing the final
// to-be-used option set.  For the above example, if `archName` is "x86" we
// get:
//
//   {a_generic_option: 1337, no_prefix: true}
//
function promoteArchSpecificOptions(options, archName) {
    assertEq(knownArchs.includes(archName), true);
    if (options.hasOwnProperty(archName)) {
        let archOptions = options[archName];
        for (let optName in archOptions) {
            options[optName] = archOptions[optName];
            if (options.log) {
                print("---- adding " + archName + "-specific option {"
                      + optName + ":" + archOptions[optName] + "}");
            }
        }
    }
    for (let a of knownArchs) {
        delete options[a];
    }
    if (options.log) {
        print("---- final options");
        for (let optName in options) {
            print("{" + optName + ":" + options[optName] + "}");
        }
    }
    return options;
}

// Main test function.  See comments at top of this file.
function codegenTestMultiplatform_adhoc(module_text, export_name,
                                        expectedAllTargets, options = {}) {
    assertEq(hasDisassembler(), true);

    // Check that we've been provided with an expected result for at least
    // x64, x86 and arm64.
    assertEq(requiredArchs.every(a => expectedAllTargets.hasOwnProperty(a)),
             true);

    // Poke the build-configuration object to find out what target we're
    // generating code for.
    let genX64   = !!getBuildConfiguration("x64");
    let genX86   = !!getBuildConfiguration("x86");
    let genArm64 = !!getBuildConfiguration("arm64");
    let genArm   = !!getBuildConfiguration("arm");

    // Check we've definitively identified exactly one architecture to test.
    assertEq(genX64 + genX86 + genArm64 + genArm, 1);

    // Decide on the arch name for which we're testing.  Everything is keyed
    // off this.
    let archName = "";
    if (genX64) {
        archName = "x64";
    } else if (genX86) {
        archName = "x86";
    } else if (genArm64) {
        archName = "arm64";
    } else if (genArm) {
        archName = "arm";
    }
    if (options.log) {
        print("---- testing for architecture \"" + archName + "\"");
    }
    // If this fails, it means we're running on an "unknown" architecture.
    assertEq(archName.length > 0, true);

    // Finalise options, by promoting arch-specific ones to the top level of
    // the options object.
    options = promoteArchSpecificOptions(options, archName);

    // Get the architecture-specific strings for the target.
    assertEq(archOptions.hasOwnProperty(archName), true);
    let {encoding = "", prefix = "", suffix = ""} = archOptions[archName];
    assertEq(encoding.length > 0, true, `bad instruction encoding: ${encoding}`);
    assertEq(prefix.length > 0, true, `bad prefix: ${prefix}`);
    assertEq(suffix.length > 0, true, `bad suffix: ${suffix}`);

    // Get the expected output string, or skip the test if no expected output
    // has been provided.  Note, because of the assertion near the top of this
    // file, this will currently only allow arm(32) tests to be skipped.
    let expected = "";
    if (expectedAllTargets.hasOwnProperty(archName)) {
        expected = expectedAllTargets[archName];
    } else {
        // Paranoia.  Don't want to silently skip tests due to logic bugs above.
        assertEq(archName, "arm");
        if (options.log) {
            print("---- !! no expected output for target, skipping !!");
        }
        return;
    }

    // Finalise the expected-result string, and stash the original for
    // debug-printing.
    let expectedInitial = expected;
    if (!options.no_prefix) {
        expected = prefix + '\n' + expected;
    }
    if (!options.no_suffix) {
        expected = expected + '\n' + suffix;
    }
    expected = fixlines(expected);

    // Compile the test case and collect disassembly output.
    let ins = wasmEvalText(module_text, {}, options.features);
    if (options.instanceBox)
        options.instanceBox.value = ins;
    let output = wasmDis(ins.exports[export_name], {tier:"ion", asString:true});
    let output_simple = stripencoding(output, encoding);

    // Check for success, print diagnostics
    let output_matches_expected = output_simple.match(new RegExp(expected)) != null;
    if (!output_matches_expected) {
        print("---- adhoc-tier1-test.js: TEST FAILED ----");
    }
    if (options.log && output_matches_expected) {
        print("---- adhoc-tier1-test.js: TEST PASSED ----");
    }
    if (options.log || !output_matches_expected) {
        print("---- module text");
        print(module_text);
        print("---- actual");
        print(output);
        print("---- expected (initial)");
        print(expectedInitial);
        print("---- expected (as used)");
        print(expected);
        print("----");
    }

    // Finally, the whole point of this:
    assertEq(output_matches_expected, true);
}
