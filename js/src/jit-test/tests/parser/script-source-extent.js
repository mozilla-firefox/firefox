// |jit-test|

// Test that the sourceStart/sourceEnd values of scripts match the current
// expectations. These values are internal details and slightly arbitrary so
// these tests expectations can be updated as needed.
//
// We don't check lineno/column here since they are reasonably robustly
// determined from source start offset.

// We use the Debugger API to introspect locate (possibly hidden) scripts and
// inspect their internal source coordinates. While we want new globals for each
// test case, they can share the same debuggee compartment.
let dbg = new Debugger();
let debuggeeCompartment = newGlobal({newCompartment: true});

// Some static class field initializer lambdas may be thrown away by GC.
gczeal(0);

function getScriptSourceExtent(source) {
    // NOTE: We will _evaluate_ the source below which may introduce dynamic
    //       scripts which are also reported. This is intended so that we may test
    //       those cases too.

    let g = newGlobal({sameCompartmentAs: debuggeeCompartment});
    dbg.addDebuggee(g);

    g.evaluate(source);

    // Use Debugger.findScripts to locate scripts, including hidden ones that
    // are implementation details.
    let scripts = dbg.findScripts();

    // Ignore the top-level script since it may or may not be GC'd and is
    // otherwise uninteresting to us here.
    scripts = scripts.filter(script => (script.sourceStart > 0) || script.isFunction);

    // Sanity-check that line/column are consistent with sourceStart. If we need
    // to test multi-line sources, this will need to be updated.
    for (let script of scripts) {
        assertEq(script.startLine, 1);
        assertEq(script.startColumn, script.sourceStart + 1);
    }

    // Map each found script to a source extent string.
    function getExtentString(script) {
        let start = script.sourceStart;
        let end = script.sourceStart + script.sourceLength;
        let length = script.sourceLength;

        let resultLength = source.length;
        assertEq(start <  resultLength, true);
        assertEq(end   <= resultLength, true);

        // The result string takes one of following forms:
        //  ` ^    `, when start == end.
        //  ` ^--^ `, typical case.
        //  ` ^----`, when end == resultLength.
        let result = " ".repeat(start) + "^";
        if (end > start) {
            result += "^".padStart(length, "-");
        }
        return result.padEnd(resultLength)
                     .substring(0, resultLength);
    }
    let results = scripts.map(getExtentString);

    // Sort results since `findScripts` does not have deterministic ordering.
    results.sort();

    dbg.removeDebuggee(g);

    return results;
}

function testSourceExtent(source, ...expectations) {
    let actual = getScriptSourceExtent(source);

    // Check that strings of each array match. These should have been presorted.
    assertEq(actual.length, expectations.length);
    for (let i = 0; i < expectations.length; ++i) {
        assertEq(actual[i], expectations[i]);
    }
}

////////////////////

// Function statements.
testSourceExtent(`function foo () { }`,
                 `             ^-----`);
testSourceExtent(`function foo (a) { }`,
                 `             ^------`);
testSourceExtent(`function foo (a, b) { }`,
                 `             ^---------`);

// Function expressions.
testSourceExtent(`let foo = function () { }`,
                 `                   ^-----`);
testSourceExtent(`let foo = function (a) { }`,
                 `                   ^------`);
testSourceExtent(`let foo = function (a, b) { }`,
                 `                   ^---------`);

// Named function expressions.
testSourceExtent(`let foo = function bar () { }`,
                 `                       ^-----`);
testSourceExtent(`let foo = function bar (a) { }`,
                 `                       ^------`);
testSourceExtent(`let foo = function bar (a, b) { }`,
                 `                       ^---------`);

// Arrow functions.
testSourceExtent(`let foo = x => { }`,
                 `          ^-------`);
testSourceExtent(`let foo = x => { };`,
                 `          ^-------^`);
testSourceExtent(`let foo = () => { }`,
                 `          ^--------`);
testSourceExtent(`let foo = (a, b) => { }`,
                 `          ^------------`);
testSourceExtent(`let foo = x => x`,
                 `          ^-----`);
testSourceExtent(`let foo = () => 0`,
                 `          ^------`);

// Async / Generator functions.
testSourceExtent(`function * foo () { }`,
                 `               ^-----`);
testSourceExtent(`async function foo () { }`,
                 `                   ^-----`);
testSourceExtent(`async function * foo () { }`,
                 `                     ^-----`);

// Async arrow functions.
testSourceExtent(`let foo = async x => { }`,
                 `                ^-------`);
testSourceExtent(`let foo = async () => { }`,
                 `                ^--------`);

// Basic inner functions.
testSourceExtent(`function foo() { function bar () {} }`,
                 `                              ^----^ `,
                 `            ^------------------------`);

// Default parameter expressions.
// NOTE: Arrow function parser back-tracking may generate multiple scripts for
//       the same source text. Delazification expects these copies to have correct
//       range information for `skipInnerLazyFunction` to work. If syntax parsing is
//       disabled (such as for coverage builds), these extra functions are not
//       generated.
if (!isLcovEnabled()) {
    testSourceExtent(`function foo(a = b => c) {}`,
                     `                 ^-----^   `,
                     `            ^--------------`);
    testSourceExtent(`let foo = (a = (b = c => 1) => 2) => 3;`,
                     `                    ^-----^            `,
                     `               ^----------------^      `,
                     `          ^---------------------------^`);
}

// Object methods, getters, setters.
testSourceExtent(`let obj = { x () {} };`,
                 `              ^----^  `);
testSourceExtent(`let obj = { * x () {} };`,
                 `                ^----^  `);
testSourceExtent(`let obj = { async x () {} };`,
                 `                    ^----^  `);
testSourceExtent(`let obj = { async * x () {} };`,
                 `                      ^----^  `);
testSourceExtent(`let obj = { get x () {} };`,
                 `                  ^----^  `);
testSourceExtent(`let obj = { set x (v) {} };`,
                 `                  ^-----^  `);
testSourceExtent(`let obj = { x: function () {} };`,
                 `                        ^----^  `);
testSourceExtent(`let obj = { x: y => z };`,
                 `               ^-----^  `);

// Classes without user-defined constructors.
testSourceExtent(` class C { } `,
                 ` ^----------^`);
testSourceExtent(` let C = class { } `,
                 `         ^--------^`);
testSourceExtent(` class C { }; class D extends C { } `,
                 `              ^--------------------^`,
                 ` ^----------^                       `);
testSourceExtent(` class C { }; let D = class extends C { } `,
                 `                      ^------------------^`,
                 ` ^----------^                             `);
testSourceExtent(`let C = class extends class { } { }`,
                 `                      ^--------^   `,
                 `        ^--------------------------`);

// Classes with user-defined constructors.
testSourceExtent(` class C { constructor() { } } `,
                 `                      ^-----^  `);
testSourceExtent(` let C = class { constructor() { } } `,
                 `                            ^-----^  `);
testSourceExtent(` class C { }; class D extends C { constructor() { } } `,
                 `                                             ^-----^  `,
                 ` ^----------^                                         `);
testSourceExtent(` class C { }; let D = class extends C { constructor() { } } `,
                 `                                                   ^-----^  `,
                 ` ^----------^                                               `);
testSourceExtent(`let C = class extends class { } { constructor() { } }`,
                 `                                             ^-----^ `,
                 `                      ^--------^                     `);

// Class field initializers lambdas.
// NOTE: These are an implementation detail and may be optimized away in future.
testSourceExtent(`class C { field }`,
                 `^----------------`);
testSourceExtent(`class C { field; }`,
                 `^-----------------`);
testSourceExtent(`class C { "field" }`,
                 `^------------------`);
testSourceExtent(`class C { 0 }`,
                 `          ^^ `,
                 `^------------`);
testSourceExtent(`class C { [1n] }`,
                 `          ^---^ `,
                 `^---------------`);
testSourceExtent(`class C { field = 1 }`,
                 `^--------------------`);
testSourceExtent(`class C { "field" = 1 }`,
                 `^----------------------`);
testSourceExtent(`class C { 0 = 1 }`,
                 `          ^----^ `,
                 `^----------------`);
testSourceExtent(`class C { [1n] = 1}`,
                 `          ^-------^`,
                 `^------------------`);

// Static class field initializer lambdas.
// NOTE: These are an implementation detail and may be optimized away in future.
testSourceExtent(`class C { static field }`,
                 `                 ^----^ `,
                 `^-----------------------`);
testSourceExtent(`class C { static field; }`,
                 `                 ^----^  `,
                 `^------------------------`);
testSourceExtent(`class C { static field = 1 }`,
                 `                 ^--------^ `,
                 `^---------------------------`);
testSourceExtent(`class C { static [0] = 1 }`,
                 `                 ^------^ `,
                 `^-------------------------`);

// Static class methods, getters, setters.
testSourceExtent(`class C { static mtd() {} }`,
                 `                    ^----^ `,
                 `^--------------------------`);
testSourceExtent(`class C { static * mtd() {} }`,
                 `                      ^----^ `,
                 `^----------------------------`);
testSourceExtent(`class C { static async mtd() {} }`,
                 `                          ^----^ `,
                 `^--------------------------------`);
testSourceExtent(`class C { static async * mtd() {} }`,
                 `                            ^----^ `,
                 `^----------------------------------`);
testSourceExtent(`class C { static get prop() {} }`,
                 `                         ^----^ `,
                 `^-------------------------------`);
testSourceExtent(`class C { static get [0]() {} }`,
                 `                        ^----^ `,
                 `^------------------------------`);
testSourceExtent(`class C { static set prop(v) {} }`,
                 `                         ^-----^ `,
                 `^--------------------------------`);

// Private class field initializer lambdas.
// NOTE: These are an implementation detail and may be optimized away in future.
testSourceExtent(`class C { #field }`,
                 `^-----------------`);
testSourceExtent(`class C { #field = 1 }`,
                 `^---------------------`);
testSourceExtent(`class C { static #field }`,
                 `                 ^-----^ `,
                 `^------------------------`);
testSourceExtent(`class C { static #field = 1 }`,
                 `                 ^---------^ `,
                 `^----------------------------`);

// Private class methods, getters, setters.
// NOTE: These generate both a field initializer lambda and a method script.
testSourceExtent(` class C { #field() { } }`,
                 `                 ^-----^ `,
                 ` ^-----------------------`);
testSourceExtent(` class C { get #field() { } }`,
                 `                     ^-----^ `,
                 `           ^---------------^ `,
                 ` ^---------------------------`);
testSourceExtent(` class C { set #field(v) { } }`,
                 `                     ^------^ `,
                 `           ^----------------^ `,
                 ` ^----------------------------`);
testSourceExtent(` class C { * #field() { } }`,
                 `                   ^-----^ `,
                 ` ^-------------------------`);
testSourceExtent(` class C { async #field() { } }`,
                 `                       ^-----^ `,
                 ` ^-----------------------------`);
testSourceExtent(` class C { async * #field() { } }`,
                 `                         ^-----^ `,
                 ` ^-------------------------------`);

// Private static class methods.
testSourceExtent(` class C { static #mtd() { } }`,
                 `                      ^-----^ `,
                 ` ^----------------------------`);
testSourceExtent(` class C { static * #mtd() { } }`,
                 `                        ^-----^ `,
                 ` ^------------------------------`);
testSourceExtent(` class C { static async #mtd() { } }`,
                 `                            ^-----^ `,
                 ` ^----------------------------------`);
testSourceExtent(` class C { static async * #mtd() { } }`,
                 `                              ^-----^ `,
                 ` ^------------------------------------`);
testSourceExtent(` class C { static get #prop() { } }`,
                 `                           ^-----^ `,
                 ` ^---------------------------------`);
testSourceExtent(` class C { static set #prop(v) { } }`,
                 `                           ^------^ `,
                 ` ^----------------------------------`);

// Static Class Blocks
testSourceExtent(` class C { static { 10; } }`,
                 `           ^-------------^ `,
                 ` ^-------------------------`);
