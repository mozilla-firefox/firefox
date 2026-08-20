// |jit-test| skip-if: !hasFunction["gczeal"]

// Check zeal modes trigger GC as expected.

// JIT compiled allocations don't trigger GC zeal.
const jitCompilerOptions = getJitCompilerOptions();
if (jitCompilerOptions["blinterp.warmup.trigger"] === 0 ||
    jitCompilerOptions["baseline.warmup.trigger"] === 0 ||
    jitCompilerOptions["ion.warmup.trigger"] === 0) {
  quit();
}

gczeal(0);
gcparam('minNurseryBytes', 1024 * 1024);
gcparam('maxNurseryBytes', 1024 * 1024);
gc();

function checkGCsWithZeal(mode, freq, allocCount, expectedCounts) {
  let counts = countGCsWithZeal(mode, freq, allocCount);
  print(`checkGCsWithZeal ${mode} ${freq} ${allocCount}: ${JSON.stringify(expectedCounts)}`)
  print(`  got ${JSON.stringify(counts)}`);
  for (const name in expectedCounts) {
    assertEq(counts[name], expectedCounts[name], name);
  }
}

function countGCsWithZeal(mode, freq, allocCount) {
  gc();
  gczeal(mode, freq);
  const counts = countGCs(allocCount);
  gczeal(0);
  return counts;
}

function countGCs(allocCount) {
  const init = {
    minor: gcparam("minorGCNumber"),
    major: gcparam("majorGCNumber"),
    slice: gcparam("sliceNumber")
  }

  for (let i = 0; i < allocCount - 1 ; i++) {
    let o = {x: i};
  }
  finishgc();

  return {
    minor: gcparam("minorGCNumber") - init.minor,
    major: gcparam("majorGCNumber") - init.major,
    slice: gcparam("sliceNumber") - init.slice
  }
}

//  0:  (None) Normal amount of collection (resets all modes)
//
//  This also delazifies the test scripts which can otherwise allocate and
//  confuse the measurements.
checkGCsWithZeal(0, 0, 100,  {major: 0,  minor: 0,  slice: 0});

//  1:  (RootsChange) Collect when roots are added or removed
checkGCsWithZeal(1, 0, 100,  {major: 0,  minor: 0,  slice: 0});

//  2:  (Alloc) Collect when every N allocations (default: 100)
checkGCsWithZeal(2, 10, 100, {major: 10, minor: 10, slice: 10});
checkGCsWithZeal(2, 20, 100, {major: 5,  minor: 5,  slice: 5});

//  4:  (VerifierPre) Verify pre write barriers between instructions
// This may trigger minor GCs because the pre barrier verifer disables and
// reenables generational GC every time it runs.
checkGCsWithZeal(4, 10, 100, {major: 0,             slice: 0});

//  6:  (YieldBeforeRootMarking) Incremental GC in two slice that yields before root marking
checkGCsWithZeal(6, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  7:  (GenerationalGC) Collect the nursery every N nursery allocations
checkGCsWithZeal(7, 10, 100, {major: 0,  minor: 10,  slice: 0});

//  8:  (YieldBeforeMarking) Incremental GC in two slices that yields between
//      the root marking and marking phases
checkGCsWithZeal(8, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  9:  (YieldBeforeSweeping) Incremental GC in two slices that yields between
//      the marking and sweeping phases
checkGCsWithZeal(9, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  10: (IncrementalMultipleSlices) Incremental GC in many slices
//
// This produces non-deterministic results as we poll for background
// finalization to finish. The work budget is ignored for this phase.
let counts = countGCsWithZeal(10, 10, 1000);
print(`check IncrementalMultipleSlices mode: got ${JSON.stringify(counts)}`);
assertEq(counts.major >= 1, true);
assertEq(counts.minor >= 1, true);
assertEq(counts.slice >= 70, true);

//  11: (IncrementalMarkingValidator) Verify incremental marking
checkGCsWithZeal(11, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  12: (ElementsBarrier) Use the individual element post-write barrier
//      regardless of elements size
checkGCsWithZeal(12, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  13: (CheckHashTablesOnMinorGC) Check internal hashtables on minor GC
checkGCsWithZeal(13, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  14: (Compact) Perform a shrinking collection every N allocations
checkGCsWithZeal(14, 10, 100, {major: 10, minor: 10, slice: 10});

//  15: (CheckHeapAfterGC) Walk the heap to check its integrity after every GC
checkGCsWithZeal(15, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  17: (YieldBeforeSweepingAtoms) Incremental GC in two slices that yields
//      before sweeping the atoms table
checkGCsWithZeal(17, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  18: (CheckGrayMarking) Check gray marking invariants after every GC
checkGCsWithZeal(18, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  19: (YieldBeforeSweepingCaches) Incremental GC in two slices that yields
//      before sweeping weak caches
checkGCsWithZeal(19, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  21: (YieldBeforeSweepingObjects) Incremental GC in multiple slices that
//      yields before sweeping foreground finalized objects
checkGCsWithZeal(21, 10, 120, {major: 4,  minor: 4,  slice: 12});

//  22: (YieldBeforeSweepingNonObjects) Incremental GC in multiple slices
//      that yields before sweeping non-object GC things
checkGCsWithZeal(22, 10, 120, {major: 4,  minor: 4,  slice: 12});

//  23: (YieldBeforeSweepingPropMapTrees) Incremental GC in multiple slices
//      that yields before sweeping shape trees
checkGCsWithZeal(23, 10, 120, {major: 4,  minor: 4,  slice: 12});

//  24: (CheckWeakMapMarking) Check weak map marking invariants after every
//      GC
checkGCsWithZeal(24, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  25: (YieldWhileGrayMarking) Incremental GC in two slices that yields
//      during gray marking
checkGCsWithZeal(25, 10, 100, {major: 5,  minor: 5,  slice: 10});

//  26: (CheckHeapBeforeMinorGC) Check for invariant violations before every
//      minor GC
checkGCsWithZeal(26, 0,  100, {major: 0,  minor: 0,  slice: 0});

//  27: (ConcurrentMarkingDelays) Add a short sleep at a few points in the
//      mutator that are risky under concurrent marking
checkGCsWithZeal(27, 0,  100, {major: 0,  minor: 0,  slice: 0});
