// Test basics of atom reference tracking and check the isAtomReferenced function
// works.

gczeal(0);
let global = newGlobal({newCompartment: true});
global.eval('var x = {}');
gc();

// Unique symbols are not marked using the atom reference bitmap.
let atom = Symbol();
assertEq(isAtomReferenced(this, atom), false);
assertEq(isAtomReferenced(global, atom), false);
global.x[atom] = 0;
assertEq(isAtomReferenced(global, atom), false);

// Symbols created with Symbol.for do use the atom reference bitmap.
let sym = Symbol.for("baz");
assertEq(isAtomReferenced(this, sym), true);
assertEq(isAtomReferenced(global, sym), false);
global.x[sym] = 0;
assertEq(isAtomReferenced(global, sym), true);
