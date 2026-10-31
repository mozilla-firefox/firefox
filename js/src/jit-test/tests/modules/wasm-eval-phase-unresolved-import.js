// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// Imports between wasm and JS modules that cannot be resolved throw a
// SyntaxError, reported against the importing module.

async function importError(specifier) {
  try {
    await import(specifier);
  } catch (e) {
    return e;
  }
  throw new Error(`import of ${specifier} should have failed`);
}

let e = await importError("imports-missing-export.wasm");
assertEq(e instanceof SyntaxError, true);
assertEq(/module1\.js' doesn't provide an export named: 'missing'/.test(e.message), true);
assertEq(e.fileName.endsWith("imports-missing-export.wasm"), true);

e = await importError("imports-ambiguous-export.wasm");
assertEq(e instanceof SyntaxError, true);
assertEq(/ambiguous\.js' contains ambiguous star export: 'a'/.test(e.message), true);
assertEq(e.fileName.endsWith("imports-ambiguous-export.wasm"), true);

e = await importError("import-missing-export-from-wasm.js");
assertEq(e instanceof SyntaxError, true);
assertEq(/exports-fun\.wasm' doesn't provide an export named: 'nope'/.test(e.message), true);

// An unknown field of a builtin module is not a builtin import, so the
// builtin module's name is loaded as an ordinary module, which fails.
e = await importError("imports-js-string-unknown-field.wasm");
assertEq(e.message.includes("wasm:js-string"), true);
