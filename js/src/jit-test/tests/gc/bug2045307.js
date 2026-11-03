// |jit-test| --no-threads; --fast-warmup

gczeal(0);

// A dynamically-created atom, referenced by no script's atom list.
let holder = {["poc_" + "atom_998877"]: 1};
let s = Object.keys(holder)[0];
let idx = getAtomRefIndex(s);

// Zone A marks the atom in its atom bitmap, then drops its reference.
let a = newGlobal({newCompartment: true});
a.s = s;
a.eval("s = null;");

// This zone keeps the atom alive only as a weakmap value.
let wm = new WeakMap();
let key = {};
wm.set(key, s);
s = holder = null;

// Compile WeakMap.prototype.get with Ion so the read goes through the
// inlined barrier fast path.
function readJit(m, k) {
  sink = m.get(k);
}
let warmMap = new WeakMap();
let warmKey = {};
warmMap.set(warmKey, {});
for (let i = 0; i < 100; i++) {
  readJit(warmMap, warmKey);
}

// Incrementally collect zone A and the atoms zone, but not this zone.
schedulezone(a);
schedulezone("atoms");
startgc(1);
while (gcstate() == "Prepare" || gcstate() == "MarkRoots") {
  gcslice(1);
}
assertEq(gcstate(), "Mark");
assertEq(gcstate(a), "MarkBlackOnly");

// Read the atom via the Ion path while the atoms zone is being marked. This
// needs to apply the read barrier.
readJit(wm, key);

// Finish: A refines its atom bitmap from the chunk mark bits. Without the
// barrier the atom is white and A drops it.
finishgc();
assertEq(getAtomRefColor(a, idx), "black");
