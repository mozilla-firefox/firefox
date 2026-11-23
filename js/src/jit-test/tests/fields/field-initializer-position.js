// Test class field initializers have reasonable lineno/column values

gczeal(0);

// Use the Debugger API to introspect the line / column.
let d = new Debugger();
let g = newGlobal({newCompartment: true})
let gw = d.addDebuggee(g);

let source = `
    let A = "A";
    let B = "B";

    class C {
        // START----v
                    [A];
                    [B] = 2;
                    'field_str';
                    'field_str_with_init' = 1;
             static x;
             static y = 3;
             static [A + "static"];
             static [B + "static"] = 4;
        // END
    }
`;

let NumInitializers = 8;

// Compute (1-based) line number of 'START' and 'END' markers.
let START = source.split('\n').findIndex(x => x.includes("START")) + 1;
let END   = source.split('\n').findIndex(x => x.includes("END")) + 1;
assertEq(END - START - 1, NumInitializers);

// Use debugger to locate internal field-initializer scripts.
g.evaluate(source);
let scripts = d.findScripts()
               .filter(script => (script.startLine >= START &&
                                  script.startLine <= END));
scripts.sort((x, y) => (x.sourceStart - y.sourceStart))

for (var i = 0; i < NumInitializers; ++i) {
    let script = scripts[i];
    let lineText = source.split('\n')[START + i];

    // Check the initializer lambda has expected line/column
    assertEq(script.startLine, START + 1 + i);
    assertEq(script.startColumn, 21);

    // Check that source length matches expectations.
    assertEq(script.startColumn + script.sourceLength + ';'.length, lineText.length + 1);
}
