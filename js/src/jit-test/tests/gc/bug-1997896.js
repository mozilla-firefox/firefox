function checkMarks(expected) {
  assertEq(getMarks().join(", "), expected.join(", "));
}

gczeal(0);
gczeal('CheckGrayMarking');
gc();

let g1 = newGlobal({newCompartment: true});
let g2 = newGlobal({newCompartment: true});
let makeAndReturnObject = `
  var o = {};
  addMarkObservers([o]);
  grayRoot()[0] = o;
  o;
`;
let o1 = g1.eval(makeAndReturnObject);
let o2 = g2.eval(makeAndReturnObject);
let s = Symbol.for("blah");
addMarkObservers([s]);
let i = getAtomRefIndex(s);
o1.s = s;
o2.s = s;
o1 = undefined;
o2 = undefined;
s = undefined;
g1.o = undefined;
g2.o = undefined;

gc();
checkMarks(['gray', 'gray', 'gray']);
assertEq(getAtomRefColor(g1, i), 'black');
assertEq(getAtomRefColor(g2, i), 'black');

o1 = g1.eval('grayRoot()[0]');
checkMarks(['black', 'gray', 'black']);
assertEq(getAtomRefColor(g1, i), 'black');
assertEq(getAtomRefColor(g2, i), 'black');

o2 = g2.eval('grayRoot()[0]');
checkMarks(['black', 'black', 'black']);
assertEq(getAtomRefColor(g1, i), 'black');
assertEq(getAtomRefColor(g2, i), 'black');

o1 = undefined;
schedulezone(this);
schedulezone(g1);
schedulezone('atoms');
startgc();
assertEq(gcstate(), 'NotActive');

checkMarks(['gray', 'black', 'black']);
// Mark observers weak edge (and any JIT weak edges) hold the atom ref black.
assertEq(getAtomRefColor(g1, i), 'black');
assertEq(getAtomRefColor(g2, i), 'black');
