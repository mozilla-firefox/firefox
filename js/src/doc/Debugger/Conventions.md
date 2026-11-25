# General Conventions

This page describes general conventions used in the [`Debugger`][debugger] API,
and defines some terminology used throughout the specification.


## Properties

Properties of objects that comprise the `Debugger` interface, and those
that the interface creates, follow some general conventions:

- Instances and prototypes are extensible; you can add your own properties
  and methods to them.

- Properties are configurable. This applies to both "own" and prototype
  properties, and to both methods and data properties. (Leaving these
  properties open to redefinition will hopefully make it easier for
  JavaScript debugger code to cope with bugs, bug fixes, and changes in the
  interface over time.)

- Method properties are writable.

- We prefer inherited accessor properties to own data properties. Both are
  read using the same syntax, but inherited accessors seem like a more
  accurate reflection of what's going on. Unless otherwise noted, these
  properties have getters but no setters, as they cannot meaningfully be
  assigned to.


## Debuggee Values

The `Debugger` interface follows some conventions to help debuggers safely
inspect and modify the debuggee's objects and values. Primitive values are
passed freely between debugger and debuggee; copying or wrapping is handled
transparently. Objects received from the debuggee (including host objects
like DOM elements) are fronted in the debugger by `Debugger.Object`
instances, which provide reflection-oriented methods for inspecting their
referents; see `Debugger.Object`, below.

Of the debugger's objects, only `Debugger.Object` instances may be passed
to the debuggee: when this occurs, the debuggee receives the
`Debugger.Object`'s referent, not the `Debugger.Object` instance itself.

In the descriptions below, the term "debuggee value" means either a
primitive value or a `Debugger.Object` instance; it is a value that might
be received from the debuggee, or that could be passed to the debuggee.


## Debuggee Code

Each `Debugger` instance maintains a set of global objects that, taken
together, comprise the debuggee. Code evaluated in the scope of a debuggee
global object, directly or indirectly, is considered *debuggee code*.
Similarly:

- a *debuggee frame* is a frame running debuggee code;

- a *debuggee function* is a function that closes over a debuggee
  global object (and thus the function's code is debuggee code);

- a *debuggee environment* is an environment whose outermost
  enclosing environment is a debuggee global object; and

- a *debuggee script* is a script containing debuggee code.


## Completion Values

The `Debugger` API often needs to convey the result of running some JS code. For example, suppose you get a `frame.onPop` callback telling you that a method in the debuggee just finished. Did it return successfully? Did it throw? What did it return? The debugger passes the `onPop` handler a *completion value* that tells what happened.

A completion value is one of these:

* `{ return: value }`

  The code completed normally, returning <i>value</i>. <i>Value</i> is a
  debuggee value.

* `{ throw: value, stack: stack }`

  The code threw <i>value</i> as an exception. <i>Value</i> is a debuggee
  value.  <i>stack</i> is a `SavedFrame` representing the location from which
  the value was thrown, and may be missing.

* `null`

  The code was terminated, as if by the "slow script" ribbon.

Generators and async functions add a wrinkle: they can suspend themselves (with `yield` or `await`), which removes their frame from the stack. Later, the generator or async frame might be returned to the stack and continue running where it left off. Does it count as "completion" when a generator suspends itself?

The `Debugger` API says yes. `yield` and `await` do trigger the `frame.onPop` handler, passing a completion value that explains why the frame is being suspended. The completion value gets an extra `.yield` or `.await` property, to distinguish this kind of completion from a normal `return`.

```js
{ return: value, yield: true }
```

where *value* is a debuggee value for the iterator result object, like `{ value: 1, done: false }`, for the yield.

When a generator function is called, it first evaluates any default argument
expressions and destructures its arguments. Then its frame is suspended, and the
new generator object is returned to the caller. This initial suspension is reported
to any `onPop` handlers as a completion value of the form:

```js
{ return: generatorObject, yield: true, initial: true }
```

where *generatorObject* is a debuggee value for the generator object being
returned to the caller.

When an async function awaits a promise, its suspension is reported to any
`onPop` handlers as a completion value of the form:

```js
{ return: promise, await: true }
```

where *promise* is a debuggee value for the promise being returned to the
caller.

The first time a call to an async function awaits, returns, or throws, a promise
of its result is returned to the caller. Subsequent resumptions of the async
call, if any, are initiated directly from the job queue's event loop, with no
calling frame on the stack. Thus, if needed, an `onPop` handler can distinguish
an async call's initial suspension, which returns the promise, from any
subsequent suspensions by checking the `Debugger.Frame`'s `older` property: if
that is `null`, the call was resumed directly from the event loop.

Async generators are a combination of async functions and generators that can
use both `yield` and `await` expressions. Suspensions of async generator frames
are reported using any combination of the completion values above.
A `yield` expression in an async generator awaits its operand without resuming
the frame, so it's reported as a single suspension of the form
`{ return: value, yield: true }`, where *value* is the operand before it's
awaited. If the await is rejected, nothing is yielded to the consumer: the frame
is resumed with a throw completion at the `yield` expression instead.


## Resumption Values

As the debuggee runs, the `Debugger` interface calls various
debugger-provided handler functions to report the debuggee's behavior.
Some of these calls can return a value indicating how the debuggee's
execution should continue; these are called *resumption values*. A
resumption value has one of the following forms:

* `undefined`

  The debuggee should continue execution normally.

* `{ return: value }`

  Force the top frame of the debuggee to return <i>value</i> immediately,
  as if by executing a `return` statement. <i>Value</i> must be a debuggee
  value. (Most handler functions support this, except those whose
  descriptions say otherwise.) See the list of special cases below.

* `{ throw: value }`

  Throw <i>value</i> as an exception from the current bytecode
  instruction. <i>Value</i> must be a debuggee value. Note that unlike
  completion values, resumption values do not specify a stack.  When
  initiating an exceptional return from a handler, the current debuggee stack
  will be used. If a handler wants to avoid modifying the stack of an
  already-thrown exception, it should return `undefined`.

* `null`

  Terminate the debuggee, as if it had been cancelled by the "slow script"
  dialog box.

In some places, the JS language treats `return` statements specially or
doesn't allow them at all. So there are a few special cases.

* An arrow function without curly braces can't contain a return
  statement, but `{ return: value }` works anyway,
  returning the specified value.

  Likewise, if the top frame of the debuggee is not in a function at
  all—that is, it's running toplevel code in a `script` tag, or `eval`
  code—then <i>value</i> is returned even though `return` statements
  aren't legal in that kind of code. (In the case of a `script` tag,
  the browser discards the return value.)

* If the debuggee is in a function that was called as a constructor (that
  is, via a `new` expression), then <i>value</i> serves as the value
  returned by the function's body, not that produced by the `new`
  expression: if the value is not an object, the `new` expression returns
  the frame's `this` value.

  Similarly, if the function is the constructor for a subclass, then a
  non-object value may result in a `TypeError`.

* Returning from a generator simulates a `return`, not a `yield`;
  there is no way to force a debuggee generator to `yield`.

  The way generators execute is rather odd. When a generator-function
  is first called, it is put onto the stack and runs just a few
  bytecode instructions (or more, if the generator-function has any
  default argument values to compute), then performs the "initial
  suspend".  At that point, a new generator object is created and
  returned to the caller. Thereafter, the caller may cause execution
  of the generator to resume at any time, by calling `genObj.next()`,
  and the generator may pause itself again using `yield`.

  JS generators normally can't return before the "initial
  suspend"—there’s no place to put a `return` statement—but
  `{ return: value }` there works anyway, replacing
  the generator object that the initial suspend would normally create
  and return.

  Returning from a generator that's been resumed via `genobj.next()`
  (or one of the other methods) closes the generator, and the
  `genobj.next()` or other method returns a new object of the form
  `{ done: true, value: value }`.

If a debugger hook function throws an exception, rather than returning a
resumption value, we never propagate such an exception to the debuggee;
instead, we call the associated `Debugger` instance's
`uncaughtExceptionHook` property, as described below.


## Timestamps

Timestamps are expressed in units of milliseconds since an arbitrary,
but fixed, epoch.  The resolution of timestamps is generally greater
than milliseconds, though no specific resolution is guaranteed.


## The `Debugger.DebuggeeWouldRun` Exception

Some debugger operations that appear to simply inspect the debuggee's state
may actually cause debuggee code to run. For example, reading a variable
might run a getter function on the global or on a `with` expression's
operand; and getting an object's property descriptor will run a handler
trap if the object is a proxy. To protect the debugger's integrity, only
methods whose stated purpose is to run debuggee code can do so. These
methods are called [invocation functions][inv fr], and they follow certain
common conventions to report the debuggee's behavior safely. For other
methods, if their normal operation would cause debuggee code to run, they
throw an instance of the `Debugger.DebuggeeWouldRun` exception.

If there are debugger frames on stack from multiple Debugger instances, the
thrown exception is an instance of the topmost locking debugger's global's
`Debugger.DebuggeeWouldRun`.

A `Debugger.DebuggeeWouldRun` exception may have a `cause` property,
providing more detailed information on why the debuggee would have run. The
`cause` property's value is one of the following strings:

* `"proxy"`: Carrying out the operation would have caused a proxy handler to run.           |
* `"getter"`: Carrying out the operation would have caused an object property getter to run. |
* `"setter"`: Carrying out the operation would have caused an object property setter to run. |

If the system can't determine why control attempted to enter the debuggee,
it will leave the exception's `cause` property undefined.


[debugger]: Debugger-API.md
[inv fr]: Debugger.Frame.md#invocation-functions-and-debugger-frames
