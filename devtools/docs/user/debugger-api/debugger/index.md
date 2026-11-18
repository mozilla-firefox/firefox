# Debugger

## The Debugger Object

When called as a constructor, the `Debugger` object creates a new `Debugger` instance.

`new Debugger([global, …])`

: Create a debugger object, and apply its {ref}`addDebuggee <debugger-api-debugger-add-debuggee>` method to each of the given *global* objects to add them as the initial debuggees.

### Accessor Properties of the Debugger Prototype Object

A `Debugger` instance inherits the following accessor properties from its prototype:

`enabled`

A boolean value indicating whether this `Debugger` instance’s handlers, breakpoints, and the like are currently enabled. It is an accessor property with a getter and setter: assigning to it enables or disables this `Debugger` instance; reading it produces true if the instance is enabled, or false otherwise. This property is initially `true` in a freshly created `Debugger` instance.

This property gives debugger code a single point of control for disentangling itself from the debuggee, regardless of what sort of events or handlers or “points” we add to the interface.

`allowWasmBinarySource`

A boolean value indicating whether WebAssembly sources will be available in binary form. The WebAssembly text generation will be disabled.

`collectCoverageInfo`

A boolean value indicating whether code coverage should be enabled inside each debuggee of this `Debugger` instance. Changing this flag value will recompile all JIT code to add or remove code coverage instrumentation. Changing this flag when any frame of the debuggee is currently active on the stack will produce an exception.

Setting this to `true` enables code coverage instrumentation, which can be accessed via the {doc}`Debugger.Script <../debugger.script/index>` `getOffsetsCoverage` function. In some cases, the code coverage might expose information which pre-date the modification of this flag. Code coverage reports are monotone, thus one can take a snapshot when the Debugger is enabled, and output the difference.

Setting this to `false` prevents this `Debugger` instance from requiring any code coverage instrumentation, but it does not guarantee that the instrumentation is not present.

`uncaughtExceptionHook`

Either `null` or a function that SpiderMonkey calls when a call to a debug event handler, breakpoint handler, or similar function throws some exception, which we refer to as *debugger-exception* here. Exceptions thrown in the debugger are not propagated to debuggee code; instead, SpiderMonkey calls this function, passing *debugger-exception* as its sole argument and the `Debugger` instance as the `this` value. This function should return a resumption value, which determines how the debuggee should continue.

If the uncaught exception hook itself throws an exception, *uncaught-hook-exception*, SpiderMonkey throws a new error object, *confess-to-debuggee-exception*, to the debuggee whose message blames the debugger, and includes textual descriptions of *uncaught-hook-exception* and the original *debugger-exception*.

If `uncaughtExceptionHook`’s value is `null`, SpiderMonkey throws an exception to the debuggee whose message blames the debugger, and includes a textual description of *debugger-exception*.

Assigning anything other than a callable value or `null` to this property throws a `TypeError` exception.

(This is not an ideal way to handle debugger bugs, but the hope here is that some sort of backstop, even if imperfect, will make life easier for debugger developers. For example, an uncaught exception hook may have access to browser-level features like the `alert` function, which this API’s implementation does not, making it possible to present debugger errors to the developer in a way suited to the context.)

## Debugger Handler Functions

Each `Debugger` instance inherits accessor properties with which you can store handler functions for SpiderMonkey to call when given events occur in debuggee code.

When one of the events described below occurs in debuggee code, the engine pauses the debuggee and calls the corresponding debugging handler on each `Debugger` instance that is observing the debuggee. The handler functions receive the `Debugger` instance as their `this` value. Most handler functions can return a resumption value indicating how the debuggee’s execution should proceed.

On a new `Debugger` instance, each of these properties is initially `undefined`. Any value assigned to a debugging handler must be either a function or `undefined`; otherwise a `TypeError` is thrown.

Handler functions run in the same thread in which the event occurred. They run in the compartment to which they belong, not in a debuggee compartment.

`onNewScript(script, global)`

New code, represented by the {doc}`Debugger.Script <../debugger.script/index>` instance *script*, has been loaded in the scope of the debuggees.

This method’s return value is ignored.

`onPromiseSettled(promise)`

A Promise object, referenced by the {doc}`Debugger.Object <../debugger.object/index>` instance *promise* that was allocated within a debuggee scope, has settled (either fulfilled or rejected). The Promise’s state, fulfillment or rejection value, and the allocation and resolution stacks can be obtained using the Promise-related accessor properties of the {doc}`Debugger.Object <../debugger.object/index>` instance *promise*.

This handler method should return a resumption value specifying how the debuggee’s execution should proceed. However, note that a `{ return: value }` resumption value is treated like `undefined` (“continue normally”); *value* is ignored.

`onDebuggerStatement(frame)`

Debuggee code has executed a *debugger* statement in *frame*. This method should return a resumption value specifying how the debuggee’s execution should proceed.

`onEnterFrame(frame)`

The stack *frame* is about to begin executing code. (Naturally, *frame* is currently the youngest {doc}`visible frame <../debugger.frame/index>`.) This method should return a resumption value specifying how the debuggee’s execution should proceed.

SpiderMonkey only calls `onEnterFrame` to report {ref}`visible <debugger-api-debugger-frame-visible-frames>`, non-`"debugger"` frames.

`onExceptionUnwind(frame, value)`

The exception *value* has been thrown, and has propagated to *frame*; *frame* is the youngest remaining stack frame, and is a debuggee frame. This method should return a resumption value specifying how the debuggee’s execution should proceed. If it returns `undefined`, the exception continues to propagate as normal: if control in `frame` is in a `try` block, control jumps to the corresponding `catch` or `finally` block; otherwise, *frame* is popped, and the exception propagates to *frame* caller.

When an exception’s propagation causes control to enter a `finally` block, the exception is temporarily set aside. If the `finally` block finishes normally, the exception resumes propagation, and the debugger’s `onExceptionUnwind` handler is called again, in the same frame. (The other possibility is for the `finally` block to exit due to a `return`, `continue`, or `break` statement, or a new exception. In those cases the old exception does not continue to propagate; it is discarded.)

This handler is not called when unwinding a frame due to an over-recursion or out-of-memory exception.

`sourceHandler(ASuffusionOfYellow)`

This method is never called. If it is ever called, a contradiction has been proven, and the debugger is free to assume that everything is true.

`onError(frame, report)`

SpiderMonkey is about to report an error in *frame*. *Report* is an object describing the error, with the following properties:

`message`

: The fully formatted error message.

`file`

: If present, the source file name, URL, etc. (If this property is present, the *line* property will be too, and vice versa.)

`line`

: If present, the source line number at which the error occurred.

`lineText`

: If present, this is the source code of the offending line.

`offset`

: The index of the character within lineText at which the error occurred.

`warning`

: Present and true if this is a warning; absent otherwise.

`strict`

: Present and true if this error or warning is due to the strict option (not to be confused with ES strict mode)

`exception`

: Present and true if an exception will be thrown; absent otherwise.

`arguments`

: An array of strings, representing the arguments substituted into the error message.

This method’s return value is ignored.

`onNewGlobalObject(global)`

A new global object, *global*, has been created.

This handler method should return a resumption value specifying how the debuggee’s execution should proceed. However, note that a `{ return: value }` resumption value is treated like `undefined` (“continue normally”); *value* is ignored. (Allowing the handler to substitute its own value for the new global object doesn’t seem useful.)

This handler method is only available to debuggers running in privileged code (“chrome”, in Firefox). Most functions provided by this `Debugger` API observe activity in only those globals that are reachable by the API’s user, thus imposing capability-based restrictions on a `Debugger`’s reach. However, the `onNewGlobalObject` method allows the API user to monitor all global object creation that occurs anywhere within the JavaScript system (the “JSRuntime”, in SpiderMonkey terms), thereby escaping the capability-based limits. For this reason, `onNewGlobalObject` is only available to privileged code.

## Function Properties of the Debugger Prototype Object

The functions described below may only be called with a `this` value referring to a `Debugger` instance; they may not be used as methods of other kinds of objects.

(debugger-api-debugger-add-debuggee)=

`addDebuggee(global)`

Add the global object designated by *global* to the set of global objects this `Debugger` instance is debugging. If the designated global is already a debuggee, this has no effect. Return this `Debugger` {doc}`Debugger.Object <../debugger.object/index>` instance referring to the designated global.

The value *global* may be any of the following:

- A global object.
- An HTML5 `WindowProxy` object (an “outer window”, in Firefox terminology), which is treated as if the `Window` object of the browsing context’s active document (the “inner window”) were passed.
- A cross-compartment wrapper of an object; we apply the prior rules to the wrapped object.
- A {doc}`Debugger.Object <../debugger.object/index>` instance belonging to this `Debugger` instance; we apply the prior rules to the referent.
- Any other sort of value is treated as a `TypeError`. (Note that each rule is only applied once in the process of resolving a given *global* argument. Thus, for example, a {doc}`Debugger.Object <../debugger.object/index>` referring to a second {doc}`Debugger.Object <../debugger.object/index>` which refers to a global does not designate that global for the purposes of this function.)

The global designated by *global* must be in a different compartment than this `Debugger` instance itself. If adding the designated global’s compartment would create a cycle of debugger and debuggee compartments, this method throws an error.

This method returns the {doc}`Debugger.Object <../debugger.object/index>` instance whose referent is the designated global object.

The `Debugger` instance does not hold a strong reference to its debuggee globals: if a debuggee global is not otherwise reachable, then it is dropped from the `Debugger` set of debuggees. (Naturally, the {doc}`Debugger.Object <../debugger.object/index>` instance this method returns does hold a strong reference to the added global.)

If this debugger is {ref}`tracking allocation sites <debugger-api-debugger-memory-tracking-allocation-sites>` and cannot track allocation sites for *global*, this method throws an `Error`.

`addAllGlobalsAsDebuggees()`

This method is like {ref}`addDebuggee <debugger-api-debugger-add-debuggee>`, but adds all the global objects from all compartments to this `Debugger` instance’s set of debuggees. Note that it skips this debugger’s compartment.

If this debugger is {ref}`tracking allocation sites <debugger-api-debugger-memory-tracking-allocation-sites>` and cannot track allocation sites for some global, this method throws an `Error`. Otherwise this method returns `undefined`.

This method is only available to debuggers running in privileged code (“chrome”, in Firefox). Most functions provided by this `Debugger` API observe activity in only those globals that are reachable by the API’s user, thus imposing capability-based restrictions on a `Debugger`’s reach. However, the `addAllGlobalsAsDebuggees` method allows the API user to monitor all global object creation that occurs anywhere within the JavaScript system (the “JSRuntime”, in SpiderMonkey terms), thereby escaping the capability-based limits. For this reason, `addAllGlobalsAsDebuggees` is only available to privileged code.

`removeDebuggee(global)`

Remove the global object designated by *global* from this `Debugger` instance’s set of debuggees. Return `undefined`.

This method interprets *global* using the same rules that {ref}`addDebuggee <debugger-api-debugger-add-debuggee>` does.

Removing a global as a debuggee from this `Debugger` clears all breakpoints that belong to that `Debugger` in that global.

`removeAllDebuggees()`

Remove all the global objects from this `Debugger` instance’s set of debuggees. Return `undefined`.

`hasDebuggee(global)`

> Return `true` if the global object designated by *global* is a debuggee of this `Debugger` instance.

This method interprets *global* using the same rules that {ref}`addDebuggee <debugger-api-debugger-add-debuggee>` does.

`getDebuggees()`

Return an array of distinct {doc}`Debugger.Object <../debugger.object/index>` instances whose referents are all the global objects this `Debugger` instance is debugging.

Since `Debugger` instances don’t hold strong references to their debuggee globals, if a debuggee global is otherwise unreachable, it may be dropped at any moment from the array this method returns.

`getNewestFrame()`

Return a {doc}`Debugger.Frame <../debugger.frame/index>` instance referring to the youngest {doc}`visible frame <../debugger.frame/index>` currently on the calling thread’s stack, or `null` if there are no visible frames on the stack.

`findSources([query]) (not yet implemented)`

Return an array of all {doc}`Debugger.Source <../debugger.source/index>` instances matching *query*. Each source appears only once in the array. *Query* is an object whose properties restrict which sources are returned; a source must meet all the criteria given by *query* to be returned. If *query* is omitted, we return all sources of all debuggee scripts.

*Query* may have the following properties:

`url`

: The source’s `url` property must be equal to this value.

`global`

: The source must have been evaluated in the scope of the given global object. If this property’s value is a {doc}`Debugger.Object <../debugger.object/index>` instance belonging to this `Debugger` instance, then its referent is used. If the object is not a global object, then the global in whose scope it was allocated is used.

Note that the result may include sources that can no longer ever be used by the debuggee: say, eval code that has finished running, or source for unreachable functions. Whether such sources appear can be affected by the garbage collector’s behavior, so this function’s result is not entirely deterministic.

`findScripts([query])`

Return an array of {doc}`Debugger.Script <../debugger.script/index>` instances for all debuggee scripts matching *query*. Each instance appears only once in the array. *Query* is an object whose properties restrict which scripts are returned; a script must meet all the criteria given by *query* to be returned. If *query* is omitted, we return the {doc}`Debugger.Script <../debugger.script/index>` instances for all debuggee scripts.

*Query* may have the following properties:

`url`

: The script’s `url` property must be equal to this value.

`source`

: The script’s `source` property must be equal to this value.

`line`

: The script must at least partially cover the given source line. If this property is present, the `url` property must be present as well.

`innermost`

: If this property is present and true, the script must be the innermost script covering the given source location; scripts of enclosing code are omitted.

`global`

: The script must be in the scope of the given global object. If this property’s value is a {doc}`Debugger.Object <../debugger.object/index>` instance belonging to this `Debugger` instance, then its referent is used. If the object is not a global object, then the global in whose scope it was allocated is used.

All properties of *query* are optional. Passing an empty object returns all debuggee code scripts.

Note that the result may include {doc}`Debugger.Script <../debugger.script/index>` instances for scripts that can no longer ever be used by the debuggee, say, those for eval code that has finished running, or unreachable functions. Whether such scripts appear can be affected by the garbage collector’s behavior, so this function’s behavior is not entirely deterministic.

`clearBreakpoint(handler)`

Remove all breakpoints set in this `Debugger` instance that use *handler* as their handler. Note that, if breakpoints using other handler objects are set at the same location(s) as *handler*, they remain in place.

`clearAllBreakpoints()`

Remove all breakpoints set using this `Debugger` instance.

`findAllGlobals()`

Return an array of {doc}`Debugger.Object <../debugger.object/index>` instances referring to all the global objects present in this JavaScript instance.

The results of this call can be affected in non-deterministic ways by the details of the JavaScript implementation. The array may include {doc}`Debugger.Object <../debugger.object/index>` instances referring to global objects that are not actually reachable by the debuggee or any other code in the system. (Naturally, once the function has returned, the array’s {doc}`Debugger.Object <../debugger.object/index>` instances strongly reference the globals they refer to.)

This handler method is only available to debuggers running in privileged code (“chrome”, in Firefox). Most functions provided by this `Debugger` API observe activity in only those globals that are reachable by the API’s user, thus imposing capability-based restrictions on a `Debugger`’s reach. However, `findAllGlobals` allows the API user to find all global objects anywhere within the JavaScript system (the “JSRuntime”, in SpiderMonkey terms), thereby escaping the capability-based limits. For this reason, `findAllGlobals` is only available to privileged code.

`makeGlobalObjectReference(global)`

Return the {doc}`Debugger.Object <../debugger.object/index>` whose referent is the global object designated by *global*, without adding the designated global as a debuggee. If *global* does not designate a global object, throw a `TypeError`. Determine which global is designated by *global* using the same rules as \<a href="Debugger#addDebuggee" title="The Debugger object: addDebuggee">\`\`Debugger.prototype.addDebuggee\`\`\</a>.

`adoptDebuggeeValue(value)`

Given a debuggee value `value` owned by an arbitrary `Debugger`, return an equivalent debuggee value owned by this `Debugger`.

If `value` is a primitive value, return it unchanged. If `value` is a `Debugger.Object` owned by an arbitrary `Debugger`, return an equivalent `Debugger.Object` owned by this `Debugger`. Otherwise, if `value` is some other kind of object, and hence not a proper debuggee value, throw a TypeError instead.

## Static methods of the Debugger Object

The functions described below are not called with a `this` value.

`isCompilableUnit(source)`

: Given a string of source code, designated by *source*, return false if the string might become a valid JavaScript statement with the addition of more lines. Otherwise return true. The intent is to support interactive compilation - accumulate lines in a buffer until isCompilableUnit is true, then pass it to the compiler.

### Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.md

Watermark:

: sha256:03b36132885e046a5f213130ba22b1139b473770f7324b842483c09ab7665f7c

Changeset:

: [3ef5a14350ec](https://github.com/mozilla-firefox/firefox/commit/3ef5a14350ecfc851fc94e470a507e28f854668b)
