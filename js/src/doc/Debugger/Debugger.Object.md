# Debugger.Object

A `Debugger.Object` instance represents an object in the debuggee,
providing reflection-oriented methods to inspect and modify its referent.
The referent's properties do not appear directly as properties of the
`Debugger.Object` instance; the debugger can access them only through
methods like `Debugger.Object.prototype.getOwnPropertyDescriptor` and
`Debugger.Object.prototype.defineProperty`, ensuring that the debugger will
not inadvertently invoke the referent's getters and setters.

SpiderMonkey creates exactly one `Debugger.Object` instance for each
debuggee object it presents to a given [`Debugger`][debugger-object]
instance: if the debugger encounters the same object through two different
routes (perhaps two functions are called on the same object), SpiderMonkey
presents the same `Debugger.Object` instance to the debugger each time.
This means that the debugger can use the `==` operator to recognize when
two `Debugger.Object` instances refer to the same debuggee object, and
place its own properties on a `Debugger.Object` instance to store metadata
about particular debuggee objects.

JavaScript code in different compartments can have different views of the
same object. For example, in Firefox, code in privileged compartments sees
content DOM element objects without redefinitions or extensions made to
that object's properties by content code. (In Firefox terminology,
privileged code sees the element through an "xray wrapper".) To ensure that
debugger code sees each object just as the debuggee would, each
`Debugger.Object` instance presents its referent as it would be seen from a
particular compartment. This "viewing compartment" is chosen to match the
way the debugger came across the referent. As a consequence, a single
[`Debugger`][debugger-object] instance may actually have several
`Debugger.Object` instances: one for each compartment from which the
referent is viewed.

If more than one [`Debugger`][debugger-object] instance is debugging the
same code, each [`Debugger`][debugger-object] gets a separate
`Debugger.Object` instance for a given object. This allows the code using
each [`Debugger`][debugger-object] instance to place whatever properties it
likes on its own `Debugger.Object` instances, without worrying about
interfering with other debuggers.

While most `Debugger.Object` instances are created by SpiderMonkey in the
process of exposing debuggee's behavior and state to the debugger, the
debugger can use `Debugger.Object.prototype.makeDebuggeeValue` to create
`Debugger.Object` instances for given debuggee objects, or use
`Debugger.Object.prototype.copy` and `Debugger.Object.prototype.create` to
create new objects in debuggee compartments, allocated as if by particular
debuggee globals.

`Debugger.Object` instances protect their referents from the garbage
collector; as long as the `Debugger.Object` instance is live, the referent
remains live. This means that garbage collection has no visible effect on
`Debugger.Object` instances.


## Accessor Properties of the Debugger.Object prototype

A `Debugger.Object` instance inherits the following accessor properties
from its prototype:

### `proto`
The referent's prototype (as a new `Debugger.Object` instance), or `null` if
it has no prototype. This accessor may throw if the referent is a scripted
proxy or some other sort of exotic object (an opaque wrapper, for example).

### `class`
A string naming the ECMAScript `[[Class]]` of the referent.

### `callable`
`true` if the referent is a callable object (such as a function or a
function proxy); false otherwise.

### `name`
The name of the referent, if it is a named function. If the referent is
an anonymous function, or not a function at all, this is `undefined`.

This accessor returns whatever name appeared after the `function`
keyword in the source code, regardless of whether the function is the
result of instantiating a function declaration (which binds the
function to its name in the enclosing scope) or evaluating a function
expression (which binds the function to its name only within the
function's body).

### `displayName`
The referent's display name, if the referent is a function with a
display name. If the referent is not a function, or if it has no display
name, this is `undefined`.

If a function has a given name, its display name is the same as its
given name. In this case, the `displayName` and `name` properties are
equal.

If a function has no name, SpiderMonkey attempts to infer an appropriate
name for it given its context. For example:

```js
function f() {}          // display name: f (the given name)
var g = function () {};  // display name: g
o.p = function () {};    // display name: o.p
var q = {
  r: function () {}      // display name: q.r
};
```

Note that the display name may not be a proper JavaScript identifier,
or even a proper expression: we attempt to find helpful names even when
the function is not immediately assigned as the value of some variable
or property. Thus, we use <code><i>a</i>/<i>b</i></code> to refer to
the <i>b</i> defined within <i>a</i>, and <code><i>a</i>&lt;</code> to
refer to a function that occurs somewhere within an expression that is
assigned to <i>a</i>. For example:

```js
function h() {
  var i = function() {};    // display name: h/i
  f(function () {});        // display name: h/<
}
var s = f(function () {});  // display name: s<
```

### `parameterNames`
If the referent is a debuggee function, the names of its parameters,
as an array of strings. If the referent is not a debuggee function, or
not a function at all, this is `undefined`.

If the referent is a host function for which parameter names are not
available, return an array with one element per parameter, each of which
is `undefined`.

If the referent is a function proxy, return an empty array.

If the function uses destructuring parameters, the corresponding array elements
are `undefined`. For example, if the referent is a function declared in this
way:

```js
function f(a, [b, c], {d, e:f}) { ... }
```

then this `Debugger.Object` instance's `parameterNames` property would
have the value:

```js
["a", undefined, undefined]
```

### `script`
If the referent is a function that is debuggee code, this is that
function's script, as a [`Debugger.Script`][script] instance. If the
referent is a function proxy or not debuggee code, this is `undefined`.

### `environment`
If the referent is a function that is debuggee code, a
[`Debugger.Environment`][environment] instance representing the lexical
environment enclosing the function when it was created. If the referent
is a function proxy or not debuggee code, this is `undefined`.

### `isError`
`true` if the referent is any potentially wrapped Error; `false` otherwise.

### `isMutedError`
`true` if the referent is any potentially wrapped Error associated with a muted
error report; `false` otherwise.

### `errorMessageName`
If the referent is an error created with an engine internal message template
this is a string which is the name of the template; `undefined` otherwise.

### `errorLineNumber`
If the referent is an Error object, this is the 1-origin line number at which
the referent was created; `undefined`  otherwise.

### `errorColumnNumber`
If the referent is an Error object, this is the 1-origin column number in
UTF-16 code units at which the referent was created; `undefined`  otherwise.

### `isBoundFunction`
If the referent is a debuggee function, returns `true` if the referent is a
bound function; `false` otherwise. If the referent is not a debuggee
function, or not a function at all, returns `undefined` instead.

### `isArrowFunction`
If the referent is a debuggee function, returns `true` if the referent is an
arrow function; `false` otherwise. If the referent is not a debuggee
function, or not a function at all, returns `undefined` instead.

### `isGeneratorFunction`
If the referent is a debuggee function, returns `true` if the referent was
created with a `function*` expression or statement, or false if it is some
other sort of function. If the referent is not a debuggee function, or not a
function at all, this is `undefined`. (This is always equal to
`obj.script.isGeneratorFunction`, assuming `obj.script` is a
`Debugger.Script`.)

### `isAsyncFunction`
If the referent is a debuggee function, returns `true` if the referent is an
async function, defined with an `async function` expression or statement, or
false if it is some other sort of function. If the referent is not a
debuggee function, or not a function at all, this is `undefined`. (This is
always equal to `obj.script.isAsyncFunction`, assuming `obj.script` is a
`Debugger.Script`.)

### `isClassConstructor`
If the referent is a debuggee function, returns `true` if the referent is a class,
or false if it is some other sort of function. If the referent is not a debuggee
function, or not a function at all, this is `undefined`. (This is always equal to
`obj.script.isClassConstructor`, assuming `obj.script` is a `Debugger.Script`.)

### `isPromise`
`true` if the referent is a Promise; `false` otherwise.

### `boundTargetFunction`
If the referent is a bound debuggee function, this is its target function—
the function that was bound to a particular `this` object. If the referent
is either not a bound function, not a debuggee function, or not a function
at all, this is `undefined`.

### `boundThis`
If the referent is a bound debuggee function, this is the `this` value it
was bound to. If the referent is either not a bound function, not a debuggee
function, or not a function at all, this is `undefined`.

### `boundArguments`
If the referent is a bound debuggee function, this is an array (in the
Debugger object's compartment) that contains the debuggee values of the
`arguments` object it was bound to. If the referent is either not a bound
function, not a debuggee function, or not a function at all, this is
`undefined`.

### `isProxy`
If the referent is a (scripted) proxy, either revoked or not, return `true`.
If the referent is not a (scripted) proxy, return `false`.

### `proxyTarget`
If the referent is a non-revoked (scripted) proxy, return a `Debugger.Object`
instance referring to the ECMAScript `[[ProxyTarget]]` of the referent.
If the referent is a revoked (scripted) proxy, return `null`.
If the referent is not a (scripted) proxy, return `undefined`.

### `proxyHandler`
If the referent is a non-revoked (scripted) proxy, return a `Debugger.Object`
instance referring to the ECMAScript `[[ProxyHandler]]` of the referent.
If the referent is a revoked (scripted) proxy, return `null`.
If the referent is not a (scripted) proxy, return `undefined`.

### `promiseState`
If the referent is a [`Promise`][promise], return a string indicating
whether the [`Promise`][promise] is pending, or has been fulfilled or
rejected. This string takes one of the following values:

* `"pending"`, if the [`Promise`][promise] is pending.

* `"fulfilled"`, if the [`Promise`][promise] has been fulfilled.

* `"rejected"`, if the [`Promise`][promise] has been rejected.

If the referent is not a [`Promise`][promise], throw a `TypeError`.

### `promiseValue`
Return a debuggee value representing the value the [`Promise`][promise] has
been fulfilled with.

If the referent is not a [`Promise`][promise], or the [`Promise`][promise]
has not been fulfilled, throw a `TypeError`.

### `promiseReason`
Return a debuggee value representing the value the [`Promise`][promise] has
been rejected with.

If the referent is not a [`Promise`][promise], or the [`Promise`][promise]
has not been rejected, throw a `TypeError`.

### `promiseAllocationSite`
If the referent is a [`Promise`][promise], this is the
[JavaScript execution stack][saved-frame] captured at the time of the
promise's allocation. This can return null if the promise was not
created from script. If the referent is not a [`Promise`][promise], throw
a `TypeError` exception.

### `promiseResolutionSite`
If the referent is a [`Promise`][promise], this is the
[JavaScript execution stack][saved-frame] captured at the time of the
promise's resolution. This can return null if the promise was not
resolved by calling its `resolve` or `reject` resolving functions from
script. If the referent is not a [`Promise`][promise], throw a `TypeError`
exception.

### `promiseID`
If the referent is a [`Promise`][promise], this is a process-unique
identifier for the [`Promise`][promise]. With e10s, the same id can
potentially be assigned to multiple [`Promise`][promise] instances, if
those instances were created in different processes. If the referent is
not a [`Promise`][promise], throw a `TypeError` exception.

### `promiseDependentPromises`
If the referent is a [`Promise`][promise], this is an `Array` of
`Debugger.Objects` referring to the promises directly depending on the
referent [`Promise`][promise]. These are:

1) Return values of `then()` calls on the promise.
2) Return values of `Promise.all()` if the referent [`Promise`][promise]
  was passed in as one of the arguments.
3) Return values of `Promise.race()` if the referent [`Promise`][promise]
  was passed in as one of the arguments.

Once a [`Promise`][promise] is settled, it will generally notify its
dependent promises and forget about them, so this is most useful on
*pending* promises.

Note that the `Array` only contains the promises that directly depend on
the referent [`Promise`][promise]. It does not contain promises that depend
on promises that depend on the referent [`Promise`][promise].

If the referent is not a [`Promise`][promise], throw a `TypeError`
exception.

### `promiseLifetime`
If the referent is a [`Promise`][promise], this is the number of
milliseconds elapsed since the [`Promise`][promise] was created. If the
referent is not a [`Promise`][promise], throw a `TypeError` exception.

### `promiseTimeToResolution`
If the referent is a [`Promise`][promise], this is the number of
milliseconds elapsed between when the [`Promise`][promise] was created and
when it was resolved. If the referent hasn't been resolved or is not a
[`Promise`][promise], throw a `TypeError` exception.

### `allocationSite`
If [object allocation site tracking][tracking-allocs] was enabled when this
`Debugger.Object`'s referent was allocated, return the
[JavaScript execution stack][saved-frame] captured at the time of the
allocation. Otherwise, return `null`.


## Function Properties of the Debugger.Object prototype

The functions described below may only be called with a `this` value
referring to a `Debugger.Object` instance; they may not be used as methods
of other kinds of objects. The descriptions use "referent" to mean "the
referent of this `Debugger.Object` instance".

Unless otherwise specified, these methods are not
[invocation functions][inv fr]; if a call would cause debuggee code to run
(say, because it gets or sets an accessor property whose handler is
debuggee code, or because the referent is a proxy whose traps are debuggee
code), the call throws a [`Debugger.DebuggeeWouldRun`][wouldrun] exception.

These methods may throw if the referent is not a native object. Even simple
accessors like `isExtensible` may throw if the referent is a proxy or some sort
of exotic object like an opaque wrapper.

### `getProperty(key, [receiver])`
Return a [completion value][cv] with "return" being the value of the
referent's property named <i>key</i>, or `undefined` if it has no such
property. <i>key</i> must be a string or symbol; <i>receiver</i> must be
a debuggee value. If the property is a getter, it will be evaluated with
<i>receiver</i> as the receiver, defaulting to the `Debugger.Object`
if omitted. All extant handler methods, breakpoints, and so on remain
active during the call.

### `setProperty(key, value<, [receiver])`
Store <i>value</i> as the value of the referent's property named
<i>key</i>, creating the property if it does not exist. <i>key</i>
must be a string or symbol; <i>value</i> and <i>receiver</i> must be
debuggee values. If the property is a setter, it will be evaluated with
<i>receiver</i> as the receiver, defaulting to the `Debugger.Object`
if omitted. Return a [completion value][cv] with "return" being a
success/failure boolean, or else "throw" being the exception thrown
during property assignment. All extant handler methods, breakpoints,
and so on remain active during the call.

### `getOwnPropertyDescriptor(key)`
Return a property descriptor for the property named <i>key</i> of the
referent. If the referent has no such property, return `undefined`.
(This function behaves like the standard
`Object.getOwnPropertyDescriptor` function, except that the object being
inspected is implicit; the property descriptor returned is allocated as
if by code scoped to the debugger's global object (and is thus in the
debugger's compartment); and its `value`, `get`, and `set` properties,
if present, are debuggee values.)

### `getOwnPropertyNames()`
Return an array of strings naming all the referent's own properties, as
if <code>Object.getOwnPropertyNames(<i>referent</i>)</code> had been
called in the debuggee, and the result copied in the scope of the
debugger's global object.

### `getOwnPropertyNamesLength()`
Return the number of the referent's own properties.

### `getOwnPropertySymbols()`
Return an array of strings naming all the referent's own symbols, as
if <code>Object.getOwnPropertySymbols(<i>referent</i>)</code> had been
called in the debuggee, and the result copied in the scope of the
debugger's global object.

### `defineProperty(key, attributes)`
Define a property on the referent named <i>key</i>, as described by
the property descriptor <i>descriptor</i>. Any `value`, `get`, and
`set` properties of <i>attributes</i> must be debuggee values. (This
function behaves like `Object.defineProperty`, except that the target
object is implicit, and in a different compartment from the function
and descriptor.)

### `defineProperties(properties)`
Add the properties given by <i>properties</i> to the referent. (This
function behaves like `Object.defineProperties`, except that the target
object is implicit, and in a different compartment from the
<i>properties</i> argument.)

### `deleteProperty(key)`
Remove the referent's property named <i>key</i>. Return true if the
property was successfully removed, or if the referent has no such
property. Return false if the property is non-configurable.

### `seal()`
Prevent properties from being added to or deleted from the referent.
Return this `Debugger.Object` instance. (This function behaves like the
standard `Object.seal` function, except that the object to be sealed is
implicit and in a different compartment from the caller.)

### `freeze()`
Prevent properties from being added to or deleted from the referent, and
mark each property as non-writable. Return this `Debugger.Object`
instance. (This function behaves like the standard `Object.freeze`
function, except that the object to be sealed is implicit and in a
different compartment from the caller.)

### `preventExtensions()`
Prevent properties from being added to the referent. (This function
behaves like the standard `Object.preventExtensions` function, except
that the object to operate on is implicit and in a different compartment
from the caller.)

### `isSealed()`
Return true if the referent is sealed—that is, if it is not extensible,
and all its properties have been marked as non-configurable. (This
function behaves like the standard `Object.isSealed` function, except
that the object inspected is implicit and in a different compartment
from the caller.)

### `isFrozen()`
Return true if the referent is frozen—that is, if it is not extensible,
and all its properties have been marked as non-configurable and
read-only. (This function behaves like the standard `Object.isFrozen`
function, except that the object inspected is implicit and in a
different compartment from the caller.)

### `isExtensible()`
Return true if the referent is extensible—that is, if it can have new
properties defined on it. (This function behaves like the standard
`Object.isExtensible` function, except that the object inspected is
implicit and in a different compartment from the caller.)

### `makeDebuggeeValue(value)`
Return the debuggee value that represents <i>value</i> in the debuggee.
If <i>value</i> is a primitive, we return it unchanged; if <i>value</i>
is an object, we return the `Debugger.Object` instance representing
that object, wrapped appropriately for use in this `Debugger.Object`'s
referent's compartment.

Note that, if <i>value</i> is an object, it need not be one allocated
in a debuggee global, nor even a debuggee compartment; it can be any
object the debugger wishes to use as a debuggee value.

As described above, each `Debugger.Object` instance presents its
referent as viewed from a particular compartment. Given a
`Debugger.Object` instance <i>d</i> and an object <i>o</i>, the call
<code><i>d</i>.makeDebuggeeValue(<i>o</i>)</code> returns a
`Debugger.Object` instance that presents <i>o</i> as it would be seen
by code in <i>d</i>'s compartment.

### `isSameNative(value)`
If <i>value</i> is a native function in the debugger's compartment, return
whether the referent is a native function for the same C++ native.

### `isSameNativeWithJitInfo(value)`
If <i>value</i> is a native function in the debugger's compartment, return
whether the referent is a native function for the same C++ native with the
same JSJitInfo pointer value.

This can be used to distinguish functions with shared native function
implementation with different JSJitInfo pointer to define the underlying
functionality.

### `isNativeGetterWithJitInfo()`
Return whether the referent is a native getter function with JSJitInfo.

### `decompile([pretty])`
If the referent is a function that is debuggee code, return the
JavaScript source code for a function definition equivalent to the
referent function in its effect and result, as a string. If
<i>pretty</i> is present and `true`, produce indented code with line
breaks. If the referent is not a function that is debuggee code, return
`undefined`.

### `call(this, argument, ...)`
If the referent is callable, call it with the given <i>this</i> value
and <i>argument</i> values, and return a [completion value][cv]
describing how the call completed. <i>This</i> should be a debuggee
value, or `{ asConstructor: true }` to invoke the referent as a
constructor, in which case SpiderMonkey provides an appropriate `this`
value itself. Each <i>argument</i> must be a debuggee value. All extant
handler methods, breakpoints, and so on remain active
during the call. If the referent is not callable, throw a `TypeError`.
This function follows the [invocation function conventions][inv fr].

Note: If this method is called on an object whose owner
[Debugger object][debugger-object] has an onNativeCall handler, only hooks
on objects associated with that debugger will be called during the evaluation.

### `apply(this, arguments)`
If the referent is callable, call it with the given <i>this</i> value
and the argument values in <i>arguments</i>, and return a
[completion value][cv] describing how the call completed. <i>This</i>
should be a debuggee value, or `{ asConstructor: true }` to invoke
<i>function</i> as a constructor, in which case SpiderMonkey provides
an appropriate `this` value itself. <i>Arguments</i> must either be an
array (in the debugger) of debuggee values, or `null` or `undefined`,
which are treated as an empty array. All extant handler methods,
breakpoints, and so on remain active during the call. If
the referent is not callable, throw a `TypeError`. This function
follows the [invocation function conventions][inv fr].

Note: If this method is called on an object whose owner
[Debugger object][debugger-object] has an onNativeCall handler, only hooks
on objects associated with that debugger will be called during the evaluation.

### `executeInGlobal(code, [options])`
If the referent is a global object, evaluate <i>code</i> in that global
environment, and return a [completion value][cv] describing how it completed.
<i>Code</i> is a string. All extant handler methods, breakpoints,
and so on remain active during the call (pending note below). This function
follows the [invocation function conventions][inv fr].
If the referent is not a global object, throw a `TypeError` exception.

<i>Code</i> is interpreted as strict mode code when it contains a Use
Strict Directive.

This evaluation is semantically equivalent to executing statements at the
global level, not an indirect eval. Regardless of <i>code</i> being strict
mode code, variable declarations in <i>code</i> affect the referent global
object.

The <i>options</i> argument is as for [`Debugger.Frame.prototype.eval`][fr eval].

Note: If this method is called on an object whose owner
[Debugger object][debugger-object] has an onNativeCall handler, only hooks
on objects associated with that debugger will be called during the evaluation.

### `executeInGlobalWithBindings(code, bindings, [options])`
Like `executeInGlobal`, but evaluate <i>code</i> using the referent as the
variable object, but with a lexical environment extended with bindings
from the object <i>bindings</i>.

An extra environment is created with bindings specified by <i>bindings</i>.
The environment contains bindings corresponding to each own enumerable property
of <i>bindings</i>, where the property name <i>name</i> as binding name,
and the property value <i>value</i> as binding's initial value.

If `options.useInnerBindings` is not specified or is specified as `false`,
it emulates where the bindings environment is placed outside of global,
which means, if the binding conflicts with any global variable declared in
the <i>code</i>, or any existing global variable, the binding is ignored.

If `options.useInnerBindings` is speified as `true`, it directly performs as
the bindings environment is placed inside the global, and the provided bindings
shadow the global variables.

Each <i>value</i> must be a debuggee value.

This is not like a `with` statement: <i>code</i> may access, assign to, and
delete the introduced bindings without having any effect on the passed
<i>bindings</i> object, because the properties are copied to a new object for
each invocation.

This method allows debugger code to introduce temporary bindings that
are visible to the given debuggee code and which refer to debugger-held
debuggee values, and do so without mutating any existing debuggee
environment.

Note that, like `executeInGlobal`, any declarations it contains affect the
referent global object, even as <i>code</i> is evaluated in an environment
extended according to <i>bindings</i>. (In the terms used by the ECMAScript
specification, the `VariableEnvironment` of the execution context for
<i>code</i> is the referent, and the <i>bindings</i> appear in a new
declarative environment, which is the eval code's `LexicalEnvironment`.)

The <i>options</i> argument is as for [`Debugger.Frame.prototype.eval`][fr eval].

Note: If this method is called on an object whose owner
[Debugger object][debugger-object] has an onNativeCall handler, only hooks
on objects associated with that debugger will be called during the evaluation.

### `createSource(options)`
If the referent is a global object, return a new JavaScript source in the
global's realm which has its properties filled in according to the `options`
object.  If the referent is not a global object, throw a `TypeError`
exception.  The `options` object can have the following properties:
  * `text`: String contents of the JavaScript in the source.
  * `url`: URL the resulting source should be associated with.
  * `startLine`: Starting line of the source.
  * `startColumn`: Starting column of the source.
  * `sourceMapURL`: Optional URL specifying the source's source map URL.
    If not specified, the source map URL can be filled in if specified by
    the source's text.
  * `isScriptElement`: Optional boolean which will set the source's
    `introductionType` to `"inlineScript"` if specified.  Otherwise, the
    source's `introductionType` will be `undefined`.
  * `forceEnableAsmJS`: Optional boolean to force enable the asm.js feature.
    Unless specified, asm.js is disabled by default in the debuggee global.
    This option can be used when the createSource is used for recompiling the
    top-level script, where the script contains asm.js functions and the asm.js
    was enabled at the first compilation, and the consumer doesn't want the
    asm.js functions being compiled as regular JS functions.

### `asEnvironment()`
If the referent is a global object, return the [`Debugger.Environment`][environment]
instance representing the referent's global lexical scope. The global
lexical scope's enclosing scope is the global object. If the referent is
not a global object, throw a `TypeError`.

### `unwrap()`
If the referent is a wrapper that this `Debugger.Object`'s compartment
is permitted to unwrap, return a `Debugger.Object` instance referring to
the wrapped object. If we are not permitted to unwrap the referent,
return `null`. If the referent is not a wrapper, return this
`Debugger.Object` instance unchanged.

### `unsafeDereference()`
Return the referent of this `Debugger.Object` instance.

If the referent is an inner object (say, an HTML5 `Window` object),
return the corresponding outer object (say, the HTML5 `WindowProxy`
object). This makes `unsafeDereference` more useful in producing values
appropriate for direct use by debuggee code, without using [invocation functions][inv fr].

This method pierces the membrane of `Debugger.Object` instances meant to
protect debugger code from debuggee code, and allows debugger code to
access debuggee objects through the standard cross-compartment wrappers,
rather than via `Debugger.Object`'s reflection-oriented interfaces. This
method makes it easier to gradually adapt large code bases to this
Debugger API: adapted portions of the code can use `Debugger.Object`
instances, but use this method to pass direct object references to code
that has not yet been updated.

### `forceLexicalInitializationByName(binding)`
If <i>binding</i> is in an uninitialized state initialize it to undefined
and return true, otherwise do nothing and return false.

### `getPromiseReactions`

If the referent is a [`Promise`][promise] or a cross-compartment wrapper of one,
this returns an array of objects describing the reaction records added to the
promise. There are several different sorts of reaction records:

-   The array entry for a reaction record added with `then` or `catch` has the
    form `{ resolve: F, reject: F, result: P }`, where each `F` is a `Debugger.Object`
    referring to a function object, and `P` is the promise that will be resolved
    with the result of calling them.

    The `resolve` and `reject` properties may be absent in some cases. A call to
    `then` can omit the rejection handler, and a call to `catch` omits the
    resolution handler. Furthermore, various promise facilities create records
    like this as internal implementation details, creating handlers that are not
    representable as JavaScript functions.

-   When a promise `P1` is resolved to another promise `P2` (such that resolving
    `P2` resolves `P1` in the same way) that adds a reaction record to `P2`. The
    array entry for that reaction record is simply the `Debugger.Object`
    representing `P1`.

    Note that, if `P1` and `P2` are in different compartments, resolving `P1` to
    `P2` creates the same sort of reaction record as a call to `then` or
    `catch`, with `P1` stored only in a private slot of the `resolve` and
    `reject` functions, and not directly available from the reaction record.

-   An `await` expression calls `PromiseResolve` on its operand to obtain a
    promise `P`, and then adds a reaction record to `P` that resumes the
    suspended call appropriately. The array entry for that reaction record is a
    `Debugger.Frame` representing the suspended call.

    If the `await`'s operand `A` is a native promise with the standard
    constructor, then `PromiseResolve` simply returns `A` unchanged, and the
    reaction record for resuming the suspended call is added to `A`'s list. But
    if `A` is some other sort of 'thenable', then `PromiseResolve` creates a new
    promise and enqueues a job to call `A`'s `then` method; this may produce
    more indirect chains from awaitees to awaiters.

-   `JS::AddPromiseReactions` and
    `JS::AddPromiseReactionsIgnoringUnhandledRejection` create a reaction record
    whose promise field is `null`.

[debugger-object]: Debugger.md
[script]: Debugger.Script.md
[environment]: Debugger.Environment.md
[promise]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise
[saved-frame]: ../SavedFrame/index

[tracking-allocs]: Debugger.Memory.md#trackingallocationsites
[inv fr]: Debugger.Frame.md#invocation-functions-and-debugger-frames
[wouldrun]: Conventions.md#the-debugger-debuggeewouldrun-exception
[cv]: Conventions.md#completion-values
[fr eval]: Debugger.Frame.md#eval-code-options
