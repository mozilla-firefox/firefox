var maxCacheLoadCount = 3;
var cachedLoadCount = 0;
var baseLoadCount = 0;
var done = false;

export class ForceRefreshParent extends JSWindowActorParent {
  constructor() {
    super();
  }

  static resetState() {
    cachedLoadCount = 0;
    baseLoadCount = 0;
    done = false;
  }

  receiveMessage(msg) {
    // if done is called, ignore the msg.
    if (done) {
      return;
    }
    if (msg.data.type === "base-load") {
      baseLoadCount += 1;
      if (cachedLoadCount === maxCacheLoadCount) {
        ForceRefreshParent.SimpleTest.is(
          baseLoadCount,
          2,
          "cached load should occur before second base load"
        );
        done = true;
        ForceRefreshParent.done();
        return;
      }
      if (baseLoadCount !== 1) {
        ForceRefreshParent.SimpleTest.ok(
          false,
          "base load without cached load should only occur once"
        );
        done = true;
        ForceRefreshParent.done();
      }
    } else if (msg.data.type === "base-register") {
      ForceRefreshParent.SimpleTest.ok(
        !cachedLoadCount,
        "cached load should not occur before base register"
      );
      ForceRefreshParent.SimpleTest.is(
        baseLoadCount,
        1,
        "register should occur after first base load"
      );
    } else if (msg.data.type === "base-sw-ready") {
      ForceRefreshParent.SimpleTest.ok(
        !cachedLoadCount,
        "cached load should not occur before base ready"
      );
      ForceRefreshParent.SimpleTest.is(
        baseLoadCount,
        1,
        "ready should occur after first base load"
      );
      ForceRefreshParent.refresh();
    } else if (msg.data.type === "cached-load") {
      ForceRefreshParent.SimpleTest.ok(
        cachedLoadCount < maxCacheLoadCount,
        "cached load should not occur too many times"
      );
      ForceRefreshParent.SimpleTest.is(
        baseLoadCount,
        1,
        "cache load occur after first base load"
      );
      cachedLoadCount += 1;
      if (cachedLoadCount < maxCacheLoadCount) {
        ForceRefreshParent.refresh();
        return;
      }
      ForceRefreshParent.forceRefresh();
    } else if (msg.data.type === "cached-failure") {
      ForceRefreshParent.SimpleTest.ok(false, "failure: " + msg.data.detail);
      done = true;
      ForceRefreshParent.done();
    }
  }
}
