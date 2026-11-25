// |jit-test| --fast-warmup; --no-threads

// Test String.prototype.match with the fast path for string receivers and
// unmodified RegExp objects, and the cases which must use the slow path.

function testFastPath() {
  var re = /b(c)?/;
  for (var i = 0; i < 200; i++) {
    var m = "abc".match(re);
    assertEq(m.length, 2);
    assertEq(m[0], "bc");
    assertEq(m[1], "c");
    assertEq(m.index, 1);
    assertEq(m.input, "abc");
    assertEq("xyz".match(re), null);
  }
}
testFastPath();

function testGlobal() {
  var re = /a/g;
  for (var i = 0; i < 200; i++) {
    var m = "aXaXa".match(re);
    assertEq(m.length, 3);
    assertEq(m.index, undefined);
    assertEq(re.lastIndex, 0);
  }
}
testGlobal();

function testSticky() {
  var re = /a/y;
  for (var i = 0; i < 200; i++) {
    re.lastIndex = 1;
    assertEq("aa".match(re)[0], "a");
    assertEq(re.lastIndex, 2);
    assertEq("ab".match(re), null);
    assertEq(re.lastIndex, 0);
  }
}
testSticky();

function testLastIndexCoercion() {
  for (var i = 0; i < 200; i++) {
    var re = /a/y;
    var calls = 0;
    re.lastIndex = {
      valueOf() {
        calls++;
        re.exec = function () {
          return ["patched"];
        };
        return 1;
      },
    };
    assertEq("aa".match(re).index, 1);
    assertEq(calls, 1);
    assertEq(re.lastIndex, 2);
    assertEq("aa".match(re)[0], "patched");
  }
}
testLastIndexCoercion();

function testReceivers() {
  var re = /1/;
  for (var i = 0; i < 200; i++) {
    assertEq(String.prototype.match.call(123, re)[0], "1");
    assertEq(String.prototype.match.call(new String("a1"), re).index, 1);
    var calls = 0;
    var obj = {
      toString() {
        calls++;
        return "x1";
      },
    };
    assertEq(String.prototype.match.call(obj, re).index, 1);
    assertEq(calls, 1);
  }
  for (var thisv of [null, undefined]) {
    var ex = null;
    try {
      String.prototype.match.call(thisv, re);
    } catch (e) {
      ex = e;
    }
    assertEq(ex instanceof TypeError, true);
  }
}
testReceivers();

function testReceiverToStringModifiesRegExp() {
  var re = /a/;
  var obj = {
    toString() {
      re.exec = function () {
        return ["exec"];
      };
      return "a";
    },
  };
  assertEq(String.prototype.match.call(obj, re)[0], "exec");
}
testReceiverToStringModifiesRegExp();

function testNonRegExp() {
  for (var i = 0; i < 200; i++) {
    assertEq("a.c".match(".")[0], "a");
    assertEq("abc".match(undefined)[0], "");
    var custom = {
      [Symbol.match](s) {
        return "custom:" + s;
      },
    };
    assertEq("abc".match(custom), "custom:abc");
  }
}
testNonRegExp();

function testModifiedRegExp() {
  var re = /a/;
  re.extra = 1;
  re[Symbol.match] = function (s) {
    return "own:" + s;
  };
  for (var i = 0; i < 200; i++) {
    assertEq("a".match(re), "own:a");
  }

  class MyRegExp extends RegExp {
    exec(s) {
      return ["sub"];
    }
  }
  var sub = new MyRegExp("a");
  for (var i = 0; i < 200; i++) {
    assertEq("a".match(sub)[0], "sub");
  }
}
testModifiedRegExp();

function testCrossRealm() {
  var g = newGlobal();
  var re = g.evaluate("/b/");
  for (var i = 0; i < 200; i++) {
    assertEq("abc".match(re).index, 1);
  }
}
testCrossRealm();

function testGlobalUnicode() {
  var re = /\u{1F0A1}|./gu;
  for (var i = 0; i < 200; i++) {
    var m = "\u{1F0A1}a\u{1F0A1}".match(re);
    assertEq(m.length, 3);
    assertEq(m[0], "\u{1F0A1}");
    assertEq(m[1], "a");
    assertEq(re.lastIndex, 0);
  }
  var re2 = /(?:)/g;
  for (var i = 0; i < 200; i++) {
    assertEq("\u{1F0A1}".match(re2).length, 3);
  }
  var re3 = /(?:)/gu;
  var re4 = /(?:)/gv;
  for (var i = 0; i < 200; i++) {
    assertEq("\u{1F0A1}".match(re3).length, 2);
    assertEq("\u{1F0A1}".match(re4).length, 2);
  }
  assertEq("abc".match(/x/g), null);
}
testGlobalUnicode();

function testMatchModifiedLater() {
  var re = /a/;
  function f(s) {
    return s.match(re);
  }
  for (var i = 0; i < 200; i++) {
    assertEq(f("a")[0], "a");
  }
  RegExp.prototype[Symbol.match] = function (s) {
    assertEq(this, re);
    return "patched:" + s;
  };
  assertEq(f("a"), "patched:a");
}
newGlobal().evaluate("(" + testMatchModifiedLater + ")()");

function testExecModifiedLater() {
  var re = /a/;
  function f(s) {
    return s.match(re);
  }
  for (var i = 0; i < 200; i++) {
    assertEq(f("a")[0], "a");
  }
  RegExp.prototype.exec = function () {
    return ["patched"];
  };
  assertEq(f("a")[0], "patched");
}
testExecModifiedLater();
