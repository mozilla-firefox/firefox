function makeSrc(n) {
  var s = "";
  for (var i = 0; i < n; i++) {
    var decl = [], sum = [];
    for (var j = 0; j < 14; j++) {
      decl.push("a" + j + "=" + j);
      sum.push("a" + j);
    }
    s += "function f" + i + "() { var " + decl.join(",") + "; return function() { return " + sum.join("+") + "; }; }\n";
  }
  return s;
}
var src = makeSrc(60);
for (var k = 0; k < 8; k++) {
  var g = newGlobal({sameZoneAs: this});
  g.evaluate(src, {forceFullParse: true});
  gc();
  gcparam("maxBytes", gcparam("gcBytes"));
  try {
    g.evaluate(src, {eagerBaselineStrategy: "Aggressive", forceFullParse: true});
  } catch (e) {}
  gcparam("maxBytes", 0xffffffff);
}
