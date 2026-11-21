gczeal(0);

var src = "";
for (var i = 0; i < 140000; i++) {
  src += "var v" + i + ";";
}
src += "function inner(){ return 42; } return inner;";

var outerFun = new Function(src);
var f = outerFun();

gczeal(4);
(function warmup() {})();
f();
gczeal(0);

f = null;
outerFun = null;
src = null;

gc();
gc();
