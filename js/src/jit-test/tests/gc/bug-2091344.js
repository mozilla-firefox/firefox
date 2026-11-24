// |jit-test| skip-if: !('oomAfterAllocations' in this) || getJitCompilerOptions()['blinterp.warmup.trigger'] === 0 || getJitCompilerOptions()['baseline.warmup.trigger'] === 0 || getJitCompilerOptions()['ion.warmup.trigger'] === 0

gczeal(0);

function test(oomCount) {
  const gA = newGlobal({newCompartment: true});
  const gB = newGlobal({newCompartment: true});
  gB.eval(`
    var targets = [];
    for (let i = 0; i < 64; i++) targets.push({a: i});
  `);
  gA.eval(`
    function take(o) { grayRoot().push(o); }
    function filler(n) {
      let keep = [];
      for (let i = 0; i < n; i++) { let o = {i}; keep.push(o); new WeakRef(o); }
      return keep;
    }
    function expose() { return grayRoot().length; }
    function use(i) { return grayRoot()[i].a; }
  `);
  gA.gB = gB;
  gA.eval(`for (let i = 0; i < 64; i++) take(gB.targets[i]); gB = null;`);
  gB.eval("targets = null;");
  finishgc();
  schedulezone(gA);
  schedulezone(gB);
  gc("zone");
  let keepA = gA.filler(1000);
  gA.expose();
  finishgc();
  minorgc();

  gcparam("nurseryEnabled", 0);
  schedulezone(gA);
  schedulezone(gB);
  let hitOOM = false;
  startgc(1);
  while (gcstate() !== "NotActive") {
    gcslice(1000, {dontStart: true});
    if (oomCount && gcstate() === "Sweep" && gcstate(gA) === "Sweep" &&
        gcstate(gB) === "MarkBlackOnly") {
      oomAfterAllocations(oomCount);
      try {
        gA.expose();
      } catch (e) {}
      hitOOM = resetOOMFailure();
      oomCount = 0;
    }
  }
  gcparam("nurseryEnabled", 1);
  for (let i = 0; i < 64; i++) gA.use(i);
  return hitOOM;
}

for (let i = 1; test(i); i++) {}
