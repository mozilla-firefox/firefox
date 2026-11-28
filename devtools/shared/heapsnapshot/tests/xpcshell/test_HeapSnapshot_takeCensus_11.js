/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Test that Debugger.Memory.prototype.takeCensus and
// HeapSnapshot.prototype.takeCensus return the same data for the same heap
// graph.

function doLiveAndOfflineCensus(g, dbg, opts) {
  dbg.memory.allocationSamplingProbability = 1;
  dbg.memory.trackingAllocationSites = true;
  g.unsafeAtAnySpeed();
  dbg.memory.trackingAllocationSites = false;

  return {
    live: dbg.memory.takeCensus(opts),
    offline: saveHeapSnapshotAndTakeCensus(dbg, opts),
  };
}

function getMarkerSize(g, dbg) {
  dbg.memory.allocationSamplingProbability = 1;
  dbg.memory.trackingAllocationSites = true;
  g.eval("var hold = allocationMarker();");
  dbg.memory.trackingAllocationSites = false;
  const live = dbg.memory.takeCensus({
    breakdown: { by: "objectClass", then: { by: "count" } },
  });
  g.hold = null;
  equal(live.AllocationMarker.count, 1);
  return live.AllocationMarker.bytes;
}

function run_test() {
  const g = newGlobal();
  const dbg = new Debugger(g);

  g.eval("this.markers = []");
  // Evaluate this only once so all allocation stacks have the same source.
  g.eval(`                                          // 1
         function unsafeAtAnySpeed() {              // 2
           for (var i = 0; i < 100; i++) {          // 3
             this.markers.push(allocationMarker()); // 4
           }                                        // 5
         }                                          // 6
         `);
  const markerSize = getMarkerSize(g, dbg);

  // First, test that we get the same counts and sizes as we allocate and retain
  // more things.

  let prevCount = 0;
  let prevBytes = 0;

  for (let i = 0; i < 10; i++) {
    const { live, offline } = doLiveAndOfflineCensus(g, dbg, {
      breakdown: { by: "objectClass", then: { by: "count" } },
    });

    equal(live.AllocationMarker.count, offline.AllocationMarker.count);
    equal(live.AllocationMarker.bytes, offline.AllocationMarker.bytes);
    equal(live.AllocationMarker.count, prevCount + 100);
    equal(live.AllocationMarker.bytes, prevBytes + 100 * markerSize);

    prevCount = live.AllocationMarker.count;
    prevBytes = live.AllocationMarker.bytes;
  }

  // Second, test that the reported allocation stacks and counts and sizes at
  // those allocation stacks match up.

  const { live, offline } = doLiveAndOfflineCensus(g, dbg, {
    breakdown: { by: "objectClass", then: { by: "allocationStack" } },
  });

  equal(live.AllocationMarker.size, offline.AllocationMarker.size);
  // One stack with the loop further above, and another stack featuring the call
  // right above.
  equal(live.AllocationMarker.size, 2);

  // Note that because SavedFrame stacks reconstructed from an offline heap
  // snapshot don't have the same principals as SavedFrame stacks captured from
  // a live stack, the live and offline allocation stacks won't be identity
  // equal, but should be structurally the same.

  const liveEntries = [];
  live.AllocationMarker.forEach((v, k) => {
    dumpn("Allocation stack:");
    k.toString()
      .split(/\n/g)
      .forEach(s => dumpn(s));

    equal(k.functionDisplayName, "unsafeAtAnySpeed");
    equal(k.line, 4);

    liveEntries.push([k.toString(), v]);
  });

  const offlineEntries = [];
  offline.AllocationMarker.forEach((v, k) => {
    dumpn("Allocation stack:");
    k.toString()
      .split(/\n/g)
      .forEach(s => dumpn(s));

    equal(k.functionDisplayName, "unsafeAtAnySpeed");
    equal(k.line, 4);

    offlineEntries.push([k.toString(), v]);
  });

  const sortEntries = (a, b) => {
    if (a[0] < b[0]) {
      return -1;
    } else if (a[0] > b[0]) {
      return 1;
    }
    return 0;
  };
  liveEntries.sort(sortEntries);
  offlineEntries.sort(sortEntries);

  equal(liveEntries.length, live.AllocationMarker.size);
  equal(liveEntries.length, offlineEntries.length);

  for (let i = 0; i < liveEntries.length; i++) {
    equal(liveEntries[i][0], offlineEntries[i][0]);
    equal(liveEntries[i][1].count, offlineEntries[i][1].count);
    equal(liveEntries[i][1].bytes, offlineEntries[i][1].bytes);
  }

  do_test_finished();
}
