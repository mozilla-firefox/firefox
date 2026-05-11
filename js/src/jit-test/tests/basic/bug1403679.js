load(libdir + "asserts.js");

const thisGlobal = this;
const otherGlobalSameCompartment = newGlobal({sameCompartmentAs: thisGlobal});
const otherGlobalNewCompartment = newGlobal({newCompartment: true});

const globals = [thisGlobal, otherGlobalSameCompartment, otherGlobalNewCompartment];

function test(fn, variants = [undefined]) {
    for (let variant of variants) {
        for (let global of globals) {
            fn(global, variant);
        }
    }
}

function testBasic(global) {
    let {object: source, transplant} = transplantableObject();

    // Validate that |source| is an object and |transplant| is a function.
    assertEq(typeof source, "object");
    assertEq(typeof transplant, "function");

    // |source| is created in the current global.
    assertEq(objectGlobal(source), this);

    // |source|'s prototype is %ObjectPrototype%.
    assertEq(Object.getPrototypeOf(source), Object.prototype);

    // Properties can be created on |source|.
    assertEq(source.foo, undefined);
    source.foo = 1;
    assertEq(source.foo, 1);

    // Calling |transplant| transplants the object and then returns undefined.
    assertEq(transplant(global), undefined);

    // |source| was moved into the new global. If the new global is in a
    // different compartment, |source| is a now a CCW.
    if (global !== otherGlobalNewCompartment) {
        assertEq(objectGlobal(source), global);
    } else {
        assertEq(objectGlobal(source), null);
        assertEq(isProxy(source), true);
    }

    // The properties are copied over to the swapped object.
    assertEq(source.foo, 1);

    // The prototype was changed to %ObjectPrototype% of |global|.
    assertEq(Object.getPrototypeOf(source), global.Object.prototype);
}
test(testBasic);

// Objects can be transplanted multiple times between globals.
function testTransplantMulti(global1, global2) {
    let {object: source, transplant} = transplantableObject();

    transplant(global1);
    transplant(global2);
}
test(testTransplantMulti, globals);

// Test the case when the source object already has a wrapper in the target global.
function testHasWrapperInTarget(global) {
    let {object: source, transplant} = transplantableObject();

    // Create a wrapper for |source| in the other global.
    global.p = source;
    assertEq(global.eval("p"), source);

    // It's a proxy object either way.
    assertEq(global.eval("isProxy(p)"), true);

    // And now transplant it into that global.
    transplant(global);

    assertEq(global.eval("p"), source);

    // It's a proxy object either way.
    assertEq(global.eval("isProxy(p)"), true);
}
test(testHasWrapperInTarget);

// Test the case when the source object has a wrapper, but in a different compartment.
function testHasWrapperOtherCompartment(global) {
    let thirdGlobal = newGlobal({newCompartment: true});
    let {object: source, transplant} = transplantableObject();

    // Create a wrapper for |source| in the new global.
    thirdGlobal.p = source;
    assertEq(thirdGlobal.eval("p"), source);

    // And now transplant the object.
    transplant(global);

    assertEq(thirdGlobal.eval("p"), source);
}
test(testHasWrapperOtherCompartment);

// Ensure a transplanted object is correctly handled by (weak) collections.
function testCollections(global, AnySet) {
    let {object, transplant} = transplantableObject();

    let set = new AnySet();

    assertEq(set.has(object), false);
    set.add(object);
    assertEq(set.has(object), true);

    transplant(global);

    assertEq(set.has(object), true);
}
test(testCollections, [Set, WeakSet]);

function testArgumentValidation() {
    // Throws an error if too many arguments are present.
    assertThrowsInstanceOf(() => transplantableObject(thisGlobal, {}), Error);

    let {object, transplant} = transplantableObject();

    // Throws an error if called with no arguments.
    assertThrowsInstanceOf(() => transplant(), Error);

    // Throws an error if called with too many arguments.
    assertThrowsInstanceOf(() => transplant(thisGlobal, {}), Error);

    // Throws an error if the first argument isn't an object
    assertThrowsInstanceOf(() => transplant(null), Error);

    // Throws an error if the argument isn't a global object.
    assertThrowsInstanceOf(() => transplant({}), Error);
}
testArgumentValidation();
