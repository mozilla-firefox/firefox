# Debugger.Environment

A `Debugger.Environment` instance represents a lexical environment, associating names with variables. Each {doc}`Debugger.Frame <../debugger.frame/index>` instance representing a debuggee frame has an associated environment object describing the variables in scope in that frame; and each {doc}`Debugger.Object <../debugger.object/index>` instance representing a debuggee function has an environment object representing the environment the function has closed over.

ECMAScript environments form a tree, in which each local environment is parented by its enclosing environment (in ECMAScript terms, its ‘outer’ environment). We say an environment *binds* an identifier if that environment itself associates the identifier with a variable, independently of its outer environments. We say an identifier is *in scope* in an environment if the identifier is bound in that environment or any enclosing environment.

SpiderMonkey creates `Debugger.Environment` instances as needed as the debugger inspects stack frames and function objects; calling `Debugger.Environment` as a function or constructor raises a `TypeError` exception.

SpiderMonkey creates exactly one `Debugger.Environment` instance for each environment it presents via a given {doc}`Debugger <../debugger/index>` instance: if the debugger encounters the same environment through two different routes (perhaps two functions have closed over the same environment), SpiderMonkey presents the same `Debugger.Environment` instance to the debugger each time. This means that the debugger can use the `==` operator to recognize when two `Debugger.Environment` instances refer to the same environment in the debuggee, and place its own properties on a `Debugger.Environment` instance to store metadata about particular environments.

(If more than one {doc}`Debugger <../debugger/index>` instance is debugging the same code, each {doc}`Debugger <../debugger/index>` gets a separate `Debugger.Environment` instance for a given environment. This allows the code using each {doc}`Debugger <../debugger/index>` instance to place whatever properties it likes on its own {doc}`Debugger.Object <../debugger.object/index>` instances, without worrying about interfering with other debuggers.)

If a `Debugger.Environment` instance’s referent is not a debuggee environment, then attempting to access its properties (other than `inspectable`) or call any its methods throws an instance of `Error`.

`Debugger.Environment` instances protect their referents from the garbage collector; as long as the `Debugger.Environment` instance is live, the referent remains live. Garbage collection has no visible effect on `Debugger.Environment` instances.

## Accessor Properties of the Debugger.Environment Prototype Object

A `Debugger.Environment` instance inherits the following accessor properties from its prototype:

`inspectable`

True if this environment is a debuggee environment, and can therefore be inspected. False otherwise. All other properties and methods of `Debugger.Environment` instances throw if applied to a non-inspectable environment.

`type`

The type of this environment object, one of the following values:

- “declarative”, indicating that the environment is a declarative environment record. Function calls, calls to `eval`, `let` blocks, `catch` blocks, and the like create declarative environment records.
- “object”, indicating that the environment’s bindings are the properties of an object. The global object and DOM elements appear in the chain of environments via object environments. (Note that `with` statements have their own environment type.)
- “with”, indicating that the environment was introduced by a `with` statement.

`parent`

The environment that encloses this one (the “outer” environment, in ECMAScript terminology), or `null` if this is the outermost environment.

`object`

A {doc}`Debugger.Object <../debugger.object/index>` instance referring to the object whose properties this environment reflects. If this is a declarative environment record, this accessor throws a `TypeError` (since declarative environment records have no such object). Both `"object"` and `"with"` environments have `object` properties that provide the object whose properties they reflect as variable bindings.

`callee`

If this environment represents the variable environment (the top-level environment within the function, which receives `var` definitions) for a call to a function *f*, then this property’s value is a {doc}`Debugger.Object <../debugger.object/index>` instance referring to *f*. Otherwise, this property’s value is `null`.

`optimizedOut`

True if this environment is optimized out. False otherwise. For example, functions whose locals are never aliased may present optimized-out environments. When true, `getVariable` returns an ordinary JavaScript object whose `optimizedOut` property is true on all bindings, and `setVariable` throws a `ReferenceError`.

## Function Properties of the Debugger.Environment Prototype Object

The methods described below may only be called with a `this` value referring to a `Debugger.Environment` instance; they may not be used as methods of other kinds of objects.

`names()`

Return an array of strings giving the names of the identifiers bound by this environment. The result does not include the names of identifiers bound by enclosing environments.

`getVariable(*name*)`

Return the value of the variable bound to *name* in this environment, or `undefined` if this environment does not bind *name*. *Name* must be a string that is a valid ECMAScript identifier name. The result is a debuggee value.

JavaScript engines often omit variables from environments, to save space and reduce execution time. If the given variable should be in scope, but `getVariable` is unable to produce its value, it returns an ordinary JavaScript object (not a {doc}`Debugger.Object <../debugger.object/index>` instance) whose `optimizedOut` property is `true`.

This is not an {ref}`invocation function <debugger-api-debugger-frame-invf>`; if this call would cause debuggee code to run (say, because the environment is a `"with"` environment, and *name* refers to an accessor property of the `with` statement’s operand), this call throws a `Debugger.DebuggeeWouldRun` exception.

`setVariable(*name*,*value*)`

Store *value* as the value of the variable bound to *name* in this environment. *Name* must be a string that is a valid ECMAScript identifier name; *value* must be a debuggee value.

If this environment binds no variable named *name*, throw a `ReferenceError`.

This is not an {ref}`invocation function <debugger-api-debugger-frame-invf>`; if this call would cause debuggee code to run, this call throws a `Debugger.DebuggeeWouldRun` exception.

`find(*name*)`

Return a reference to the innermost environment, starting with this environment, that binds *name*. If *name* is not in scope in this environment, return `null`. *Name* must be a string whose value is a valid ECMAScript identifier name.

### Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.Environment.md

Watermark:

: sha256:3d6f67939e351803d5d7fe201ed38c4aaf766caf032f255e168df1f1c6fe73cb

Changeset:

: [9fd199bbdc62](https://github.com/mozilla-firefox/firefox/commit/9fd199bbdc6283f8d808ba81f5f3ef85e0263bc7)
