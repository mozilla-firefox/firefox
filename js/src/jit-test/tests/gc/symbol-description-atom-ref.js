// Reading a unique symbol's description must record a reference to the
// description atom in the reading zone's atom reference bitmap, since unique
// symbols themselves are not recorded there.

gczeal(0);

let other = newGlobal({newCompartment: true});

// Create a unique symbol in the other zone and return the atom mark index of
// its description. The description itself is never returned from |other| so no
// reference to it is recorded in this zone.
function makeSymbol() {
  other.eval(`
    var sym = Symbol("description-" + Math.random());
    var descIndex = getAtomRefIndex(sym.description);
  `);
  return other.eval('descIndex');
}

let index = makeSymbol();
assertEq(getAtomRefColor(other, index), 'black');
assertEq(getAtomRefColor(this, index), 'white');

let sym = other.eval('sym');
assertEq(typeof sym, 'symbol');

// Obtaining the symbol does not reference its description.
assertEq(getAtomRefColor(this, index), 'white');

let desc = sym.description;
assertEq(typeof desc, 'string');
assertEq(getAtomRefColor(this, index), 'black');

// Drop all references to the symbol and check that the description survives a
// full GC, which refines the atom reference bitmaps.
other.eval('sym = undefined;');
sym = undefined;
gc();

assertEq(getAtomRefColor(this, index), 'black');
assertEq(desc.startsWith("description-"), true);

// Same again, but only collect this zone and the atoms zone.
index = makeSymbol();
assertEq(getAtomRefColor(this, index), 'white');
sym = other.eval('sym');
assertEq(getAtomRefColor(this, index), 'white');
desc = sym.description;
assertEq(getAtomRefColor(this, index), 'black');

other.eval('sym = undefined;');
sym = undefined;
schedulezone(this);
schedulezone('atoms');
gc('zone');
assertEq(getAtomRefColor(this, index), 'black');
assertEq(desc.startsWith("description-"), true);
