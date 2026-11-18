// Megamorphic property gets with keys built at runtime, which JIT code can't
// atomize inline, so lookups reach the C++ fallback without a cache entry.
//
// Each phase reads a single key through its own access site. Megamorphic cache
// slots depend on the key's hash, so mixing keys at one site lets them evict
// each other depending on the hash function, and a site that has seen a getter
// switches to a different path for every key. Neither can then silently stop a
// phase from exercising the path under test.

var ITERATIONS = 2000;
var RECEIVERS = 20;

function makeGetter() {
  return new Function("o", "prefix", "suffix", "return o[prefix + suffix];");
}

function Base() {}
var mid = Object.create(Base.prototype);

function makeReceivers(kind) {
  var objs = [];
  for (var i = 0; i < RECEIVERS; i++) {
    var o = kind === "deep" ? Object.create(mid) : new Base();
    o[kind + "Shape" + i] = i;
    o.tag = i;
    o.ownValue = "own" + i;
    objs.push(o);
  }
  return objs;
}

function runPhase(objs, check, mutate) {
  for (var iter = 0; iter < ITERATIONS; iter++) {
    for (var i = 0; i < objs.length; i++) {
      check(objs[i], i, iter);
    }
    if (mutate) {
      mutate(iter, objs);
    }
  }
}

// Own data property on the receiver.
(function () {
  var get = makeGetter();
  runPhase(makeReceivers("own"), function (o, i) {
    assertEq(get(o, "own", "Value"), "own" + i);
  });
})();

// Data property one hop up the proto chain. Replacing its value doesn't
// invalidate the cache, so a hit must read the current slot.
(function () {
  var get = makeGetter();
  Base.prototype.protoMethod = () => "first";
  runPhase(
    makeReceivers("hop1"),
    function (o, i, iter) {
      assertEq(get(o, "proto", "Method")(), iter > 1000 ? "second" : "first");
    },
    function (iter) {
      if (iter == 1000) {
        Base.prototype.protoMethod = () => "second";
      }
    }
  );
})();

// Data property two hops up the proto chain.
(function () {
  var get = makeGetter();
  Base.prototype.deepMethod = () => "deep";
  runPhase(makeReceivers("deep"), function (o) {
    assertEq(get(o, "deep", "Method")(), "deep");
  });
})();

// Missing everywhere on the proto chain.
(function () {
  var get = makeGetter();
  runPhase(makeReceivers("missing"), function (o) {
    assertEq(get(o, "absent", "Key"), undefined);
  });
})();

// Getter on the prototype. This path never caches getters, so this phase only
// checks that they're still called with the right receiver.
(function () {
  var get = makeGetter();
  Object.defineProperty(Base.prototype, "protoGetter", {
    get() {
      return "getter" + this.tag;
    },
  });
  runPhase(makeReceivers("getter"), function (o, i) {
    assertEq(get(o, "proto", "Getter"), "getter" + i);
  });
})();

// hasOwnProperty with a key built at runtime leaves a missing-own entry for the
// same receiver and key, which the get after it must not treat as the answer.
(function () {
  var get = makeGetter();
  Base.prototype.sharedKey = () => "shared";
  runPhase(makeReceivers("hasOwn"), function (o) {
    assertEq(o.hasOwnProperty("shared" + "Key".slice(0)), false);
    assertEq(get(o, "shared", "Key")(), "shared");
  });
})();

// Deleting from a prototype invalidates the cache; adding the key to a nearer
// prototype later moves the answer one hop closer.
(function () {
  var get = makeGetter();
  Base.prototype.movingKey = () => "base";
  runPhase(
    makeReceivers("deep"),
    function (o, i, iter) {
      var f = get(o, "moving", "Key");
      if (iter < 700) {
        assertEq(f(), "base");
      } else if (iter < 1400) {
        assertEq(f, undefined);
      } else {
        assertEq(f(), "mid");
      }
    },
    function (iter) {
      if (iter == 699) {
        delete Base.prototype.movingKey;
      } else if (iter == 1399) {
        mid.movingKey = () => "mid";
      }
    }
  );
})();

// Receivers gaining an own property that shadows the prototype's.
(function () {
  var get = makeGetter();
  Base.prototype.shadowKey = () => "proto";
  var own = () => "own";
  runPhase(
    makeReceivers("shadow"),
    function (o, i, iter) {
      var shadowed = iter >= 1000 && i % 4 == 0;
      assertEq(get(o, "shadow", "Key")(), shadowed ? "own" : "proto");
    },
    function (iter, objs) {
      if (iter == 999) {
        for (var i = 0; i < objs.length; i += 4) {
          objs[i].shadowKey = own;
        }
      }
    }
  );
})();
