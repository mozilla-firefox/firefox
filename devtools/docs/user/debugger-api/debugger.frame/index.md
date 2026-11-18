# Debugger.Frame

A `Debugger.Frame` instance represents a {ref}`visible stack frame <debugger-api-debugger-frame-visible-frames>`. Given a `Debugger.Frame` instance, you can find the script the frame is executing, walk the stack to older frames, find the lexical environment in which the execution is taking place, and so on.

For a given {doc}`Debugger <../debugger/index>` instance, SpiderMonkey creates only one `Debugger.Frame` instance for a given visible frame. Every handler method called while the debuggee is running in a given frame is given the same frame object. Similarly, walking the stack back to a previously accessed frame yields the same frame object as before. Debugger code can add its own properties to a frame object and expect to find them later, use `==` to decide whether two expressions refer to the same frame, and so on.

(If more than one {doc}`Debugger <../debugger/index>` instance is debugging the same code, each {doc}`Debugger <../debugger/index>` gets a separate `Debugger.Frame` instance for a given frame. This allows the code using each {doc}`Debugger <../debugger/index>` instance to place whatever properties it likes on its `Debugger.Frame` instances, without worrying about interfering with other debuggers.)

When the debuggee pops a stack frame (say, because a function call has returned or an exception has been thrown from it), the `Debugger.Frame` instance referring to that frame becomes inactive: its `live` property becomes `false`, and accessing its other properties or calling its methods throws an exception. Note that frames only become inactive at times that are predictable for the debugger: when the debuggee runs, or when the debugger removes frames from the stack itself.

(debugger-api-debugger-frame-visible-frames)=

## Visible frames

When inspecting the call stack, {doc}`Debugger <../debugger/index>` does not reveal all the frames that are actually present on the stack: while it does reveal all frames running debuggee code, it omits frames running the debugger’s own code, and omits most frames running non-debuggee code. We call those stack frames a {doc}`Debugger <../debugger/index>` does reveal *visible frames*.

A frame is a visible frame if any of the following are true:

- it is running debuggee code;
- its immediate caller is a frame running debuggee code; or
- it is a {ref}`"debugger" <debugger-api-debugger-frame-invf>`, representing the continuation of debuggee code invoked by the debugger.

The “immediate caller” rule means that, when debuggee code calls a non-debuggee funct\`on, it looks like a call to a primitive: you see a frame for the non-debuggee function that was accessible to the debuggee, but any further calls that function makes are treated as internal details, and omitted from the stack trace. If the non-debuggee function eventually calls back into debuggee code, then those frames are visible.

(Note that the debuggee is not considered an “immediate caller” of handler methods it triggers. Even though the debuggee and debugger share the same JavaScript stack, frames pushed for SpiderMonkey’s calls to handler methods to report events in the debuggee are never considered visible frames.)

(debugger-api-debugger-frame-invf)=

## Invocation functions and Debugger frames

An *invocation function* is any function in this interface that allows the debugger to invoke code in the debuggee: `Debugger.Object.prototype.call`, `Debugger.Frame.prototype.eval`, and so on.

While invocation functions differ in the code to be run and how to pass values to it, they all follow this general procedure:

1. Let *older* be the youngest visible frame on the stack, or `null` if there is no such frame. (This is never one of the debugger’s own frames; those never appear as `Debugger.Frame` instances.)
2. Push a `"debugger"` frame on the stack, with *older* as its `older` property.
3. Invoke the debuggee code as appropriate for the given invocation function, with the `"debugger"` frame as its continuation. For example, `Debugger.Frame.prototype.eval` pushes an `"eval"` frame for code it runs, whereas `Debugger.Object.prototype.call` pushes a `"call"` frame.
4. When the debuggee code completes, whether by returning, throwing an exception or being terminated, pop the `"debugger"` frame, and return an appropriate completion value from the invocation function to the debugger.

When a debugger calls an invocation function to run debuggee code, that code’s continuation is the debugger, not the next debuggee code frame. Pushing a `"debugger"` frame makes this continuation explicit, and makes it easier to find the extent of the stack created for the invocation.

## Accessor properties of the Debugger.Frame prototype object

A `Debugger.Frame` instance inherits the following accessor properties from its prototype:

`type`

A string describing what sort of frame this is:

- `"call"`: a frame running a function call. (We may not be able to obtain frames for calls to host functions.)
- `"eval"`: a frame running code passed to `eval`.
- `"global"`: a frame running global code (JavaScript that is neither of the above).
- `"module"`: a frame running code at the top level of a module.
- `"wasmcall"`: a frame running a WebAssembly function call.
- `"debugger"`: a frame for a call to user code invoked by the debugger (see the `eval` method below).

`implementation`

A string describing which tier of the JavaScript engine this frame is executing in:

- `"interpreter"`: a frame running in the interpreter.
- `"baseline"`: a frame running in the unoptimizing, baseline JIT.
- `"ion"`: a frame running in the optimizing JIT.
- `"wasm"`: a frame running in WebAssembly baseline JIT.

`this`

The value of `this` for this frame (a debuggee value). For a `wasmcall` frame, this property throws a `TypeError`.

`older`

The next-older visible frame, in which control will resume when this frame completes. If there is no older frame, this is `null`.

`depth`

: The depth of this frame, counting from oldest to youngest; the oldest frame has a depth of zero.

`live`

: True if the frame this `Debugger.Frame` instance refers to is still on the stack; false if it has completed execution or been popped in some other way.

`script`

: The script being executed in this frame (a {doc}`Debugger.Script <../debugger.script/index>` instance), or `null` on frames that do not represent calls to debuggee code. On frames whose `callee` property is not null, this is equal to `callee.script`.

`offset`

: The offset of the bytecode instruction currently being executed in `script`, or `undefined` if the frame’s `script` property is `null`. For a `wasmcall` frame, this property throws a `TypeError`.

`environment`

: The lexical environment within which evaluation is taking place (a {doc}`Debugger.Environment <../debugger.environment/index>` instance), or `null` on frames that do not represent the evaluation of debuggee code, like calls non-debuggee functions, host functions or `"debugger"` frames.

`callee`

: The function whose application created this frame, as a debuggee value, or `null` if this is not a `"call"` frame.

`generator`

: True if this frame is a generator frame, false otherwise.

`constructing`

: True if this frame is for a function called as a constructor, false otherwise.

`arguments`

: The arguments passed to the current frame, or `null` if this is not a `"call"` frame. When non-`null`, this is an object, allocated in the same global as the debugger, with `Array.prototype` on its prototype chain, a non-writable `length` property, and properties whose names are array indices. Each property is a read-only accessor property whose getter returns the current value of the corresponding parameter. When the referent frame is popped, the argument value’s properties’ getters throw an error.

## Handler methods of Debugger.Frame instances

Each `Debugger.Frame` instance inherits accessor properties holding handler functions for SpiderMonkey to call when given events occur in the frame.

Calls to frames’ handler methods are cross-compartment, intra-thread calls: the call takes place in the thread to which the frame belongs, and runs in the compartment to which the handler method belongs.

`Debugger.Frame` instances inherit the following handler method properties:

`onStep`

This property must be either `undefined` or a function. If it is a function, SpiderMonkey calls it when execution in this frame makes a small amount of progress, passing no arguments and providing this `Debugger.Frame` instance as the `this` value. The function should return a resumption value specifying how the debuggee’s execution should proceed.

What constitutes “a small amount of progress” varies depending on the implementation, but it is fine-grained enough to implement useful “step” and “next” behavior.

If multiple {doc}`Debugger <../debugger/index>` instances each have `Debugger.Frame` instances for a given stack frame with `onStep` handlers set, their handlers are run in an unspecified order. If any `onStep` handler forces the frame to return early (by returning a resumption value other than `undefined`), any remaining debuggers’ `onStep` handlers do not run.

This property is ignored on frames that are not executing debuggee code, like `"call"` frames for calls to host functions and `"debugger"` frames.

`onPop`

This property must be either `undefined` or a function. If it is a function, SpiderMonkey calls it just before this frame is popped, passing a completion value indicating how this frame’s execution completed, and providing this `Debugger.Frame` instance as the `this` value. The function should return a resumption value indicating how execution should proceed. On newly created frames, this property’s value is `undefined`.

When this handler is called, this frame’s current execution location, as reflected in its `offset` and `environment` properties, is the operation which caused it to be unwound. In frames returning or throwing an exception, the location is often a return or a throw statement. In frames propagating exceptions, the location is a call.

When an `onPop` call reports the completion of a construction call (that is, a function called via the `new` operator), the completion value passed to the handler describes the value returned by the function body. If this value is not an object, it may be different from the value produced by the `new` expression, which will be the value of the frame’s `this` property. (In ECMAScript terms, the `onPop` handler receives the value returned by the `[[Call]]` method, not the value returned by the `[[Construct]]` method.)

When a debugger handler function forces a frame to complete early, by returning a `{ return:... }`, `{ throw:... }`, or `null` resumption value, SpiderMonkey calls the frame’s `onPop` handler, if any. The completion value passed in this case reflects the resumption value that caused the frame to complete.

When SpiderMonkey calls an `onPop` handler for a frame that is throwing an exception or being terminated, and the handler returns `undefined`, then SpiderMonkey proceeds with the exception or termination. That is, an `undefined` resumption value leaves the frame’s throwing and termination process undisturbed.

If multiple {doc}`Debugger <../debugger/index>` instances each have `Debugger.Frame` instances for a given stack frame with `onPop` handlers set, their handlers are run in an unspecified order. The resumption value each handler returns establishes the completion value reported to the next handler.

This handler is not called on `"debugger"` frames. It is also not called when unwinding a frame due to an over-recursion or out-of-memory exception.

## Function properties of the Debugger.Frame prototype object

The functions described below may only be called with a `this` value referring to a `Debugger.Frame` instance; they may not be used as methods of other kinds of objects.

(debugger-api-debugger-frame-eval)=

`eval(code, [options])`

Evaluate *code* in the execution context of this frame, and return a completion value describing how it completed. *Code* is a string. If this frame’s `environment` property is `null` or `type` property is `wasmcall`, throw a `TypeError`. All extant handler methods, breakpoints, and so on remain active during the call. This function follows the {ref}`invocation function convention <debugger-api-debugger-frame-invf>`.

*Code* is interpreted as strict mode code when it contains a Use Strict Directive, or the code executing in this frame is strict mode code.

If *code* is not strict mode code, then variable declarations in *code* affect the environment of this frame. (In the terms used by the ECMAScript specification, the `VariableEnvironment` of the execution context for the eval code is the `VariableEnvironment` of the execution context that this frame represents.) If implementation restrictions prevent SpiderMonkey from extending this frame’s environment as requested, this call throws an Error exception.

If given, *options* should be an object whose properties specify details of how the evaluation should occur. The `eval` method recognizes the following properties:

`url`

: The filename or URL to which we should attribute *code*. If this property is omitted, the URL defaults to `"debugger eval code"`.

`lineNumber`

: The line number at which the evaluated code should be claimed to begin within *url*.

`evalWithBindings(*code*,*bindings*, [*options*])`

Like `eval`, but evaluate *code* in the environment of this frame, extended with bindings from the object *bindings*. For each own enumerable property of *bindings* named *name* whose value is *value*, include a variable in the environment in which *code* is evaluated named *name*, whose value is *value*. Each *value* must be a debuggee value. (This is not like a `with` statement: *code* may access, assign to, and delete the introduced bindings without having any effect on the *bindings* object.)

This method allows debugger code to introduce temporary bindings that are visible to the given debuggee code and which refer to debugger-held debuggee values, and do so without mutating any existing debuggee environment.

Note that, like `eval`, declarations in the *code* passed to `evalWithBindings` affect the environment of this frame, even as that environment is extended by bindings visible within *code*. (In the terms used by the ECMAScript specification, the `VariableEnvironment` of the execution context for the eval code is the `VariableEnvironment` of the execution context that this frame represents, and the *bindings* appear in a new declarative environment, which is the eval code’s `LexicalEnvironment`.) If implementation restrictions prevent SpiderMonkey from extending this frame’s environment as requested, this call throws an `Error` exception.

The *options* argument is as for {ref}`Debugger.Frame.prototype.eval <debugger-api-debugger-frame-eval>`, described above. Also like `eval`, if this frame’s `environment` property is `null` or `type` property is `wasmcall`, throw a `TypeError`.

### Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.Frame.md

Watermark:

: sha256:b1894f88b76b7afd661f3044a05690d76d1498c54c596ca729c6ee0d150d2da1

Changeset:

: [3ef5a14350ec](https://github.com/mozilla-firefox/firefox/commit/3ef5a14350ecfc851fc94e470a507e28f854668b)
