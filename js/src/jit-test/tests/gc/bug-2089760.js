gczeal(0);
const skipped = newGlobal({newCompartment: true});
const first = newGlobal({newCompartment: true});
skipped.eval(`(() => { const root = grayRoot(); const symbol = Symbol.for('sweep-abort-variant'); root[0] = {symbol}; blackRoot()[0] = {}; addMarkObservers([root, root[0], symbol, blackRoot()[0]]); })();`);
first.eval("var trash = Array.from({length: 20000}, () => new Set().values());");
gc();
assertEq(getMarks().join(','), 'gray,gray,gray,black');
first.eval("trash = null;");
schedulezone(skipped);
schedulezone(first);
startgc(1);
for (let slices = 0; ; slices++) {
  assertEq(slices < 50000, true);
  if (gcstate() === 'Sweep' && gcstate(skipped) === 'MarkBlackOnly') break;
  assertEq(gcstate() !== 'NotActive', true);
  gcslice(100);
}
abortgc();
assertEq(gcstate(), 'NotActive');
assertEq(getMarks().join(','), 'unmarked,unmarked,gray,black');
skipped.eval('blackRoot()[0].symbol = grayRoot()[0].symbol; grayRoot()[0] = null; undefined;');
assertEq(getMarks().join(','), 'unmarked,unmarked,gray,black');
gczeal('CheckGrayMarking', 0);
schedulezone(first);
gc('zone');
assertEq(grayBitsValid(), false);
