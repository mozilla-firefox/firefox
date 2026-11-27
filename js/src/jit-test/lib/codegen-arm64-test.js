// Scaffolding for testing arm64 Ion code generation patterns .  See
// codegen-x64-test.js in this directory for more information.

load(libdir + "codegen-test-common.js");

const arm64_arch = {
    name: "arm64",

    // End of prologue
    prefix: `
mov     x29, sp
mov     x20, sp(
bti     j)?(
str     x23, \\[x29, #16\\])?
`,

    // Start of epilogue
    suffix: `
ldp     x29, x30, \\[sp\\], #16
`,

    // Instruction encoding
    encoding: `${HEX}{8}`,
};

// For when nothing else applies: `module_text` is the complete source text of
// the module, `export_name` is the name of the function to be tested,
// `expected` is the non-preprocessed pattern, and options is an options bag,
// described in codegen-x64-test.js.
function codegenTestARM64_adhoc(module_text, export_name, expected, options = {}) {
    codegenTestShared_adhoc(arm64_arch, module_text, export_name, expected, options);
}
