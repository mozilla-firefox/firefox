function testDebuggerHooksNX(dbg, g, testHook) {
  function testDebuggerHook(hookName, trigger) {
    var hit = false;
    dbg[hookName] = () => {
      hit = true;
      dbg[hookName] = undefined;
      testHook(hookName);
    };
    trigger();
    assertEq(hit, true);
  }

  // Hooks on the Debugger instance itself.
  testDebuggerHook("onDebuggerStatement",
                   () => { g.eval("debugger;"); });

  testDebuggerHook("onExceptionUnwind",
                   () => {
                     try {
                       g.eval("throw 42;");
                     } catch (e) {
                       assertEq(e, 42);
                     }
                   });

  testDebuggerHook("onNewScript",
                   () => { g.eval("42"); });

  testDebuggerHook("onEnterFrame",
                   () => { g.eval("(() => {})()"); });

  testDebuggerHook("onNewGlobalObject",
                   () => { newGlobal(); });

  // Hooks on frames.
  var onStepHit = false;
  var onPopHit = false;
  dbg.onEnterFrame = (frame) => {
    dbg.onEnterFrame = undefined;

    frame.onStep = () => {
      onStepHit = true;
      frame.onStep = undefined;
      testHook("onStep");
    };

    frame.onPop = () => {
      onPopHit = true;
      testHook("onPop");
    };
  };
  g.eval("42");
  assertEq(onStepHit, true);

  // We can't actually assert that onPop executed because there's one test
  // that tests NX works across a removeDebuggee/addDebuggee toggle. The
  // removeDebuggee purges D.F instances, so the onPop won't be called even
  // when the global is re-added.
  //assertEq(onPopHit, true);

  // Breakpoints
  var breakpointHits = 0;
  dbg.onDebuggerStatement = (frame) => {
    dbg.onDebuggerStatement = undefined;
    var line0 = frame.script.getOffsetMetadata(frame.offset).lineNumber;
    var offs = frame.script.getPossibleBreakpointOffsets({ line: line0 + 1 });
    for (let i = 0; i < offs.length; i++) {
      frame.script.setBreakpoint(offs[i], {
        hit: () => {
          breakpointHits++;
          testHook("breakpoint");
        }
      });
    }
  };
  g.eval(`debugger;
         s = 'a'`);
  assertEq(breakpointHits >= 1, true);
}
