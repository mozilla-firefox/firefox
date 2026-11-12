// Test the RegExp species fuse used by RegExpSplit and RegExpMatchAll.

load(libdir + 'eqArrayHelper.js');

function testSplitDefault() {
  var r = /a/;
  assertEqArray("baa".split(r), ["b", "", ""]);
}

function testSplitSubclassSpecies() {
  class SubRegExp extends RegExp {
    static get [Symbol.species]() { return this; }
  }
  var r = new SubRegExp("a", "y");
  r.lastIndex = 1;
  var parts = "aba".split(r);
  assertEqArray(parts, ["", "b", ""]);
}

function testSplitOverriddenSpecies() {
  var calls = 0;
  var orig = Object.getOwnPropertyDescriptor(RegExp, Symbol.species).get;
  Object.defineProperty(RegExp, Symbol.species, {
    get() { calls++; return orig.call(this); },
  });
  try {
    assertEqArray("aXbXc".split(/X/, 2), ["a", "b"]);
  } finally {
    Object.defineProperty(RegExp, Symbol.species, { get: orig });
  }
  assertEq(calls > 0, true);
}

function testSplitOverriddenProtoConstructor() {
  class Ctor {
    constructor(rx, flags) { return new RegExp(rx.source, flags); }
  }
  var desc = Object.getOwnPropertyDescriptor(RegExp.prototype, "constructor");
  RegExp.prototype.constructor = Ctor;
  try {
    assertEqArray("a1b".split(/1/), ["a", "b"]);
  } finally {
    Object.defineProperty(RegExp.prototype, "constructor", desc);
  }
}

function testMatchAllDefault() {
  var r = /t(e)(st(\d?))/g;
  var str = "test1test2";
  var matches = [...str.matchAll(r)];
  assertEq(matches.length, 2);
  assertEq(matches[0][0], "test1");
  assertEq(matches[0][3], "1");
}

function testMatchAllSubclassSpecies() {
  class SubRegExp extends RegExp {
    static get [Symbol.species]() { return this; }
  }
  var r = new SubRegExp("a", "g");
  var matches = [...r[Symbol.matchAll]("aa")];
  assertEq(matches.length, 2);
}

function testSplitOwnConstructor() {
  var r = /a/;
  r.constructor = RegExp;
  assertEqArray("aaa".split(r), ["", "", "", ""]);
}

for (var i = 0; i < 10; i++) {
  testSplitDefault();
  testSplitSubclassSpecies();
  testSplitOverriddenSpecies();
  testSplitOverriddenProtoConstructor();
  testMatchAllDefault();
  testMatchAllSubclassSpecies();
  testSplitOwnConstructor();
}
