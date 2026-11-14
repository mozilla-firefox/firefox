// Debugger.Script instances with live referents stay alive.

var N = 4;
var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);
var i;
dbg.onDebuggerStatement = function (frame) {
    assertEq(frame.script instanceof Debugger.Script, true);
    frame.script.id = i;
};

g.eval('var arr = [];')
for (i = 0; i < N; i++)  // loop to defeat conservative GC
    g.eval("arr.push(function () { debugger }); arr[arr.length - 1]();");

gc();

var hits;
dbg.onDebuggerStatement = function (frame) {
    hits++;
    assertEq(frame.script.id, i);
};
hits = 0;
for (i = 0; i < N; i++)
    g.arr[i]();
assertEq(hits, N);
