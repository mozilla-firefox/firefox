# Debugger.Object

A `Debugger.Object` instance represents an object in the debuggee, providing reflection-oriented methods to inspect and modify its referent. The referent’s properties do not appear directly as properties of the `Debugger.Object` instance; the debugger can access them only through methods like `Debugger.Object.prototype.getOwnPropertyDescriptor` and `Debugger.Object.prototype.defineProperty`, ensuring that the debugger will not inadvertently invoke the referent’s getters and setters.

SpiderMonkey creates exactly one `Debugger.Object` instance for each debuggee object it presents to a given {doc}`Debugger <../debugger/index>` instance: if the debugger encounters the same object through two different routes (perhaps two functions are called on the same object), SpiderMonkey presents the same `Debugger.Object` instance to the debugger each time. This means that the debugger can use the `==` operator to recognize when two `Debugger.Object` instances refer to the same debuggee object, and place its own properties on a `Debugger.Object` instance to store metadata about particular debuggee objects.

JavaScript code in different compartments can have different views of the same object. For example, in Firefox, code in privileged compartments sees content DOM element objects without redefinitions or extensions made to that object’s properties by content code. (In Firefox terminology, privileged code sees the element through an “xray wrapper”.) To ensure that debugger code sees each object just as the debuggee would, each `Debugger.Object` instance presents its referent as it would be seen from a particular compartment. This “viewing compartment” is chosen to match the way the debugger came across the referent. As a consequence, a single {doc}`Debugger <../debugger/index>` instance may actually have several `Debugger.Object` instances: one for each compartment from which the referent is viewed.

If more than one {doc}`Debugger <../debugger/index>` instance is debugging the same code, each {doc}`Debugger <../debugger/index>` gets a separate `Debugger.Object` instance for a given object. This allows the code using each {doc}`Debugger <../debugger/index>` instance to place whatever properties it likes on its own `Debugger.Object` instances, without worrying about interfering with other debuggers.

While most `Debugger.Object` instances are created by SpiderMonkey in the process of exposing debuggee’s behavior and state to the debugger, the debugger can use `Debugger.Object.prototype.makeDebuggeeValue` to create `Debugger.Object` instances for given debuggee objects, or use `Debugger.Object.prototype.copy` and `Debugger.Object.prototype.create` to create new objects in debuggee compartments, allocated as if by particular debuggee globals.

`Debugger.Object` instances protect their referents from the garbage collector; as long as the `Debugger.Object` instance is live, the referent remains live. This means that garbage collection has no visible effect on `Debugger.Object` instances.

## Accessor Properties of the Debugger.Object prototype

A `Debugger.Object` instance inherits the following accessor properties from its prototype:

`proto`

: The referent’s prototype (as a new `Debugger.Object` instance), or `null` if it has no prototype. This accessor may throw if the referent is a scripted proxy or some other sort of exotic object (an opaque wrapper, for example).

`class`

: A string naming the ECMAScript `[[Class]]` of the referent.

`callable`

: `true` if the referent is a callable object (such as a function or a function proxy); false otherwise.

`name`

: The name of the referent, if it is a named function. If the referent is an anonymous function, or not a function at all, this is `undefined`.

  This accessor returns whatever name appeared after the `function` keyword in the source code, regardless of whether the function is the result of instantiating a function declaration (which binds the function to its name in the enclosing scope) or evaluating a function expression (which binds the function to its name only within the function’s body).

`displayName`

The referent’s display name, if the referent is a function with a display name. If the referent is not a function, or if it has no display name, this is `undefined`.

If a function has a given name, its display name is the same as its given name. In this case, the `displayName` and `name` properties are equal.

If a function has no name, SpiderMonkey attempts to infer an appropriate name for it given its context. For example:

```javascript
function f() {}          // display name: f (the given name)
var g = function () {};  // display name: g
o.p = function () {};    // display name: o.p
var q = {
  r: function () {}      // display name: q.r
};
```

Note that the display name may not be a proper JavaScript identifier, or even a proper expression: we attempt to find helpful names even when the function is not immediately assigned as the value of some variable or property. Thus, we use `a/b` to refer to the *b* defined within *a*, and `a<` to refer to a function that occurs somewhere within an expression that is assigned to *a*. For example:

```javascript
function h() {
  var i = function() {};    // display name: h/i
  f(function () {});        // display name: h/<
}
var s = f(function () {});  // display name: s<``</pre>
```

`parameterNames`

: If the referent is a debuggee function, the names of the its parameters, as an array of strings. If the referent is not a debuggee function, or not a function at all, this is `undefined`.

  If the referent is a host function for which parameter names are not available, return an array with one element per parameter, each of which is `undefined`.

  If the referent is a function proxy, return an empty array.

  If the referent uses destructuring parameters, then the array’s elements reflect the structure of the parameters. For example, if the referent is a function declared in this way:

  ```javascript
  function f(a, [b, c], {d, e:f}) { ... }
  ```

  then this `Debugger.Object` instance’s `parameterNames` property would have the value:

  ```javascript
  ["a", ["b", "c"], {d:"d", e:"f"}]
  ```

`script`

: If the referent is a function that is debuggee code, this is that function’s script, as a {doc}`Debugger.Script <../debugger.script/index>` instance. If the referent is a function proxy or not debuggee code, this is `undefined`.

`environment`

: If the referent is a function that is debuggee code, a {doc}`Debugger.Environment <../debugger.environment/index>` instance representing the lexical environment enclosing the function when it was created. If the referent is a function proxy or not debuggee code, this is `undefined`.

`errorMessageName`

: If the referent is an error created with an engine internal message template this is a string which is the name of the template; `undefined` otherwise.

`errorLineNumber`

: If the referent is an Error object, this is the line number at which the referent was created; `undefined` otherwise.

`errorColumnNumber`

: If the referent is an Error object, this is the column number at which the referent was created; `undefined` otherwise.

`isBoundFunction`

: If the referent is a debuggee function, returns `true` if the referent is a bound function; `false` otherwise. If the referent is not a debuggee function, or not a function at all, returns `undefined` instead.

`isArrowFunction`

: If the referent is a debuggee function, returns `true` if the referent is an arrow function; `false` otherwise. If the referent is not a debuggee function, or not a function at all, returns `undefined` instead.

`isGeneratorFunction`

: If the referent is a debuggee function, returns `true` if the referent was created with a `function*` expression or statement, or false if it is some other sort of function. If the referent is not a debuggee function, or not a function at all, this is `undefined`. (This is always equal to `obj.script.isGeneratorFunction`, assuming `obj.script` is a `Debugger.Script`.)

`isAsyncFunction`

: If the referent is a debuggee function, returns `true` if the referent is an async function, defined with an `async function` expression or statement, or false if it is some other sort of function. If the referent is not a debuggee function, or not a function at all, this is `undefined`. (This is always equal to `obj.script.isAsyncFunction`, assuming `obj.script` is a `Debugger.Script`.)

`isPromise`

: `true` if the referent is a Promise; `false` otherwise.

`boundTargetFunction`

: If the referent is a bound debuggee function, this is its target function— the function that was bound to a particular `this` object. If the referent is either not a bound function, not a debuggee function, or not a function at all, this is `undefined`.

`boundThis`

: If the referent is a bound debuggee function, this is the `this` value it was bound to. If the referent is either not a bound function, not a debuggee function, or not a function at all, this is `undefined`.

`boundArguments`

: If the referent is a bound debuggee function, this is an array (in the Debugger object’s compartment) that contains the debuggee values of the `arguments` object it was bound to. If the referent is either not a bound function, not a debuggee function, or not a function at all, this is `undefined`.

`isProxy`

: If the referent is a (scripted) proxy, either revoked or not, return `true`. If the referent is not a (scripted) proxy, return `false`.

`proxyTarget`

: If the referent is a non-revoked (scripted) proxy, return a `Debugger.Object` instance referring to the ECMAScript `[[ProxyTarget]]` of the referent. If the referent is a revoked (scripted) proxy, return `null`. If the referent is not a (scripted) proxy, return `undefined`.

`proxyHandler`

: If the referent is a non-revoked (scripted) proxy, return a `Debugger.Object` instance referring to the ECMAScript `[[ProxyHandler]]` of the referent. If the referent is a revoked (scripted) proxy, return `null`. If the referent is not a (scripted) proxy, return `undefined`.

`promiseState`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), return a string indicating whether the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) is pending, or has been fulfilled or rejected. This string takes one of the following values:

- `"pending"`, if the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) is pending.
- `"fulfilled"`, if the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has been fulfilled.
- `"rejected"`, if the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has been rejected.

  If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError`.

`promiseValue`

: Return a debuggee value representing the value the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has been fulfilled with.

  If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), or the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has not been fulfilled, throw a `TypeError`.

`promiseReason`

: Return a debuggee value representing the value the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has been rejected with.

  If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), or the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) has not been rejected, throw a `TypeError`.

`promiseAllocationSite`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), this is the JavaScript execution stack captured at the time of the promise’s allocation. This can return null if the promise was not created from script. If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError` exception.

`promiseResolutionSite`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), this is the JavaScript execution stack captured at the time of the promise’s resolution. This can return null if the promise was not resolved by calling its `resolve` or `reject` resolving functions from script. If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError` exception.

`promiseDependentPromises`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), this is an `Array` of `Debugger.Objects` referring to the promises directly depending on the referent [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise). These are:

1. Return values of `then()` calls on the promise.
2. Return values of `Promise.all()` if the referent [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) was passed in as one of the arguments.
3. Return values of `Promise.race()` if the referent [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) was passed in as one of the arguments.

   Once a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) is settled, it will generally notify its dependent promises and forget about them, so this is most useful on *pending* promises.

   Note that the `Array` only contains the promises that directly depend on the referent [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise). It does not contain promises that depend on promises that depend on the referent [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise).

   If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError` exception.

`promiseLifetime`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), this is the number of milliseconds elapsed since the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) was created. If the referent is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError` exception.

`promiseTimeToResolution`

: If the referent is a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), this is the number of milliseconds elapsed between when the [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise) was created and when it was resolved. If the referent hasn’t been resolved or is not a [Promise](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise), throw a `TypeError` exception.

`global`

: A `Debugger.Object` instance referring to the global object in whose scope the referent was allocated. This does not unwrap cross-compartment wrappers: if the referent is a wrapper, the result refers to the wrapper’s global, not the wrapped object’s global. The result refers to the global directly, not via a wrapper.

(debugger-api-debugger-object-allocation-site)=

`allocationSite`

: If {ref}`object allocation site tracking <debugger-api-debugger-memory-tracking-allocation-sites>` was enabled when this `Debugger.Object`’s referent was allocated, return the JavaScript execution stack captured at the time of the allocation. Otherwise, return `null`.

## Function Properties of the Debugger.Object prototype

The functions described below may only be called with a `this` value referring to a `Debugger.Object` instance; they may not be used as methods of other kinds of objects. The descriptions use “referent” to mean “the referent of this `Debugger.Object` instance”.

Unless otherwise specified, these methods are not {ref}`invocation functions <debugger-api-debugger-frame-invf>`; if a call would cause debuggee code to run (say, because it gets or sets an accessor property whose handler is debuggee code, or because the referent is a proxy whose traps are debuggee code), the call throws a `Debugger.DebuggeeWouldRun` exception.

These methods may throw if the referent is not a native object. Even simple accessors like `isExtensible` may throw if the referent is a proxy or some sort of exotic object like an opaque wrapper.

`getProperty(key, [receiver])`

: Return a completion value with "return" being the value of the referent's property named *key*, or `undefined` if it has no such property. *key* must be a string or symbol; *receiver* must be a debuggee value. If the property is a getter, it will be evaluated with *receiver* as the receiver, defaulting to the `Debugger.Object` if omitted.

`setProperty(key, value, [receiver])`

: Store *value* as the value of the referent’s property named *key*, creating the property if it does not exist. *key* must be a string or symbol; *value* and *receiver* must be debuggee values. If the property is a setter, it will be evaluated with *receiver* as the receiver, defaulting to the `Debugger.Object` if omitted.

`getOwnPropertyDescriptor(name)`

: Return a property descriptor for the property named *name* of the referent. If the referent has no such property, return `undefined`. (This function behaves like the standard `Object.getOwnPropertyDescriptor` function, except that the object being inspected is implicit; the property descriptor returned is allocated as if by code scoped to the debugger’s global object (and is thus in the debugger’s compartment); and its `value`, `get`, and `set` properties, if present, are debuggee values.)

`getOwnPropertyNames()`

: Return an array of strings naming all the referent’s own properties, as if `Object.getOwnPropertyNames(referent)` had been called in the debuggee, and the result copied in the scope of the debugger’s global object.

`getOwnPropertySymbols()`

: Return an array of strings naming all the referent’s own symbols, as if `Object.getOwnPropertySymbols(referent)` had been called in the debuggee, and the result copied in the scope of the debugger’s global object.

`defineProperty(name, attributes)`

: Define a property on the referent named *name*, as described by the property descriptor *descriptor*. Any `value`, `get`, and `set` properties of *attributes* must be debuggee values. (This function behaves like `Object.defineProperty`, except that the target object is implicit, and in a different compartment from the function and descriptor.)

`defineProperties(properties)`

: Add the properties given by *properties* to the referent. (This function behaves like `Object.defineProperties`, except that the target object is implicit, and in a different compartment from the *properties* argument.)

`deleteProperty(name)`

: Remove the referent’s property named *name*. Return true if the property was successfully removed, or if the referent has no such property. Return false if the property is non-configurable.

`seal()`

: Prevent properties from being added to or deleted from the referent. Return this `Debugger.Object` instance. (This function behaves like the standard `Object.seal` function, except that the object to be sealed is implicit and in a different compartment from the caller.)

`freeze()`

: Prevent properties from being added to or deleted from the referent, and mark each property as non-writable. Return this `Debugger.Object` instance. (This function behaves like the standard `Object.freeze` function, except that the object to be sealed is implicit and in a different compartment from the caller.)

`preventExtensions()`

: Prevent properties from being added to the referent. (This function behaves like the standard `Object.preventExtensions` function, except that the object to operate on is implicit and in a different compartment from the caller.)

`isSealed()`

: Return true if the referent is sealed—that is, if it is not extensible, and all its properties have been marked as non-configurable. (This function behaves like the standard `Object.isSealed` function, except that the object inspected is implicit and in a different compartment from the caller.)

`isFrozen()`

: Return true if the referent is frozen—that is, if it is not extensible, and all its properties have been marked as non-configurable and read-only. (This function behaves like the standard `Object.isFrozen` function, except that the object inspected is implicit and in a different compartment from the caller.)

`isExtensible()`

: Return true if the referent is extensible—that is, if it can have new properties defined on it. (This function behaves like the standard `Object.isExtensible` function, except that the object inspected is implicit and in a different compartment from the caller.)

`copy(value)`

: Apply the HTML5 “structured cloning” algorithm to create a copy of *value* in the referent’s global object (and thus in the referent’s compartment), and return a `Debugger.Object` instance referring to the copy.

  Note that this returns primitive values unchanged. This means you can use `Debugger.Object.prototype.copy` as a generic “debugger value to debuggee value” conversion function—within the limitations of the “structured cloning” algorithm.

`create(prototype, [properties])`

: Create a new object in the referent’s global (and thus in the referent’s compartment), and return a `Debugger.Object` referring to it. The new object’s prototype is *prototype*, which must be an `Debugger.Object` instance. The new object’s properties are as given by *properties*, as if *properties* were passed to `Debugger.Object.prototype.defineProperties`, with the new `Debugger.Object` instance as the `this` value.

`makeDebuggeeValue(value)`

: Return the debuggee value that represents *value* in the debuggee. If *value* is a primitive, we return it unchanged; if *value* is an object, we return the `Debugger.Object` instance representing that object, wrapped appropriately for use in this `Debugger.Object`’s referent’s compartment.

  Note that, if *value* is an object, it need not be one allocated in a debuggee global, nor even a debuggee compartment; it can be any object the debugger wishes to use as a debuggee value.

  As described above, each `Debugger.Object` instance presents its referent as viewed from a particular compartment. Given a `Debugger.Object` instance *d* and an object *o*, the call `d.makeDebuggeeValue(o)` returns a `Debugger.Object` instance that presents *o* as it would be seen by code in *d*’s compartment.

`call(this, argument, …)`

: If the referent is callable, call it with the given *this* value and *argument* values, and return a completion value describing how the call completed. *This* should be a debuggee value, or `{ asConstructor: true }` to invoke the referent as a constructor, in which case SpiderMonkey provides an appropriate `this` value itself. Each *argument* must be a debuggee value. All extant handler methods, breakpoints, and so on remain active during the call. If the referent is not callable, throw a `TypeError`. This function follows the invocation function conventions.

`apply(this, arguments)`

: If the referent is callable, call it with the given *this* value and the argument values in *arguments*, and return a completion value describing how the call completed. *This* should be a debuggee value, or `{ asConstructor: true }` to invoke *function* as a constructor, in which case SpiderMonkey provides an appropriate `this` value itself. *Arguments* must either be an array (in the debugger) of debuggee values, or `null` or `undefined`, which are treated as an empty array. All extant handler methods, breakpoints, and so on remain active during the call. If the referent is not callable, throw a `TypeError`. This function follows the {ref}`invocation function conventions <debugger-api-debugger-frame-invf>`.

`executeInGlobal(code, [options])`

: If the referent is a global object, evaluate *code* in that global environment, and return a completion value describing how it completed. *Code* is a string. All extant handler methods, breakpoints, and so on remain active during the call. This function follows the {ref}`invocation function conventions <debugger-api-debugger-frame-invf>`. If the referent is not a global object, throw a `TypeError` exception.

  *Code* is interpreted as strict mode code when it contains a Use Strict Directive.

  This evaluation is semantically equivalent to executing statements at the global level, not an indirect eval. Regardless of *code* being strict mode code, variable declarations in *code* affect the referent global object.

  The *options* argument is as for {ref}`Debugger.Frame.eval <debugger-api-debugger-frame-eval>`.

`executeInGlobalWithBindings(code, bindings, [options])`

Like `executeInGlobal`, but evaluate *code* using the referent as the variable object, but with a lexical environment extended with bindings from the object *bindings*. For each own enumerable property of *bindings* named *name* whose value is value, include a variable in the lexical environment in which *code* is evaluated named *name*, whose value is *value*. Each *value* must be a debuggee value. (This is not like a `with` statement: *code* may access, assign to, and delete the introduced bindings without having any effect on the *bindings* object.)

This method allows debugger code to introduce temporary bindings that are visible to the given debuggee code and which refer to debugger-held debuggee values, and do so without mutating any existing debuggee environment.

Note that, like `executeInGlobal`, any declarations it contains affect the referent global object, even as *code* is evaluated in an environment extended according to *bindings*. (In the terms used by the ECMAScript specification, the `VariableEnvironment` of the execution context for *code* is the referent, and the *bindings* appear in a new declarative environment, which is the eval code’s `LexicalEnvironment`.)

The *options* argument is as for {ref}`Debugger.Frame.eval <debugger-api-debugger-frame-eval>`.

`asEnvironment()`

: If the referent is a global object, return the {doc}`Debugger.Environment <../debugger.environment/index>` instance representing the referent’s global lexical scope. The global lexical scope’s enclosing scope is the global object. If the referent is not a global object, throw a `TypeError`.

`unwrap()`

: If the referent is a wrapper that this `Debugger.Object`’s compartment is permitted to unwrap, return a `Debugger.Object` instance referring to the wrapped object. If we are not permitted to unwrap the referent, return `null`. If the referent is not a wrapper, return this `Debugger.Object` instance unchanged.

`unsafeDereference()`

: Return the referent of this `Debugger.Object` instance.

  If the referent is an inner object (say, an HTML5 `Window` object), return the corresponding outer object (say, the HTML5 `WindowProxy` object). This makes `unsafeDereference` more useful in producing values appropriate for direct use by debuggee code, without using {ref}`invocation functions <debugger-api-debugger-frame-invf>`.

  This method pierces the membrane of `Debugger.Object` instances meant to protect debugger code from debuggee code, and allows debugger code to access debuggee objects through the standard cross-compartment wrappers, rather than via `Debugger.Object`’s reflection-oriented interfaces. This method makes it easier to gradually adapt large code bases to this Debugger API: adapted portions of the code can use `Debugger.Object` instances, but use this method to pass direct object references to code that has not yet been updated.

`forceLexicalInitializationByName(binding)`

: If *binding* is in an uninitialized state initialize it to undefined and return true, otherwise do nothing and return false.

## Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.Object.md

Watermark:

: sha256:7ae16a834e0883a95b4e0d227193293f6b6e4e4dd812c2570372a39c4c04897b

Changeset:

: [b2dd7d222fda+](https://github.com/mozilla-firefox/firefox/commit/b2dd7d222fda395191a9be6ed027aaeee1f6e2b6)
