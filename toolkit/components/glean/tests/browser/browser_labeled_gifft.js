/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function keyedScalarValue(aScalarName) {
  let snapshot = Services.telemetry.getSnapshotForKeyedScalars();
  return "parent" in snapshot ? snapshot.parent[aScalarName] : undefined;
}

add_task(async () => {
  // Ensure we're starting with a clean slate. test-verify can be tricky.
  Services.fog.testResetFOG();
  Services.telemetry.clearScalars();

  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounter.a_label.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnlyIpc.aLabeledCounter.a_label.add(1);
  Glean.testOnlyIpc.aLabeledCounter.another_label.add(2);
  Assert.equal(1, Glean.testOnlyIpc.aLabeledCounter.a_label.testGetValue());
  Assert.equal(
    2,
    Glean.testOnlyIpc.aLabeledCounter.another_label.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounter.__other__.testGetValue()
  );
  Glean.testOnlyIpc.aLabeledCounter["1".repeat(112)].add(3);
  Assert.throws(
    () => Glean.testOnlyIpc.aLabeledCounter.__other__.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );

  let value = keyedScalarValue(
    "telemetry.test.another_mirror_for_labeled_counter"
  );
  Assert.deepEqual(
    {
      a_label: 1,
      another_label: 2,
    },
    value
  );

  // AND NOW, FOR THE TRUE TEST:
  // Will this leak memory all over the place?
});
