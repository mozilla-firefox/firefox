# Debugger.Script

A `Debugger.Script` instance may refer to a sequence of bytecode in the debuggee or to a block of WebAssembly code. For the former, it is the {doc}`Debugger <../debugger/index>` API’s presentation of a JSAPI `JSScript` object. The two cases are distinguished by their `format` property being `"js"` or `"wasm"`.

## Debugger.Script for JSScripts

For `Debugger.Script` instances referring to a `JSScript`, they are distinguished by their `format` property being `"js"`.

Each of the following is represented by a single `JSScript` object:

- The body of a function—that is, all the code in the function that is not contained within some nested function.
- The code passed to a single call to `eval`, excluding the bodies of any functions that code defines.
- The contents of a `<script>` element.
- A DOM event handler, whether embedded in HTML or attached to the element by other JavaScript code.
- Code appearing in a `javascript:` URL.

The {doc}`Debugger <../debugger/index>` interface constructs `Debugger.Script` objects as scripts of debuggee code are uncovered by the debugger: via the `onNewScript` handler method; via {doc}`Debugger.Frame <../debugger.frame/index>`’s `script` properties; via the `functionScript` method of {doc}`Debugger.Object <../debugger.object/index>` instances; and so on. For a given {doc}`Debugger <../debugger/index>` instance, SpiderMonkey constructs exactly one `Debugger.Script` instance for each underlying script object; debugger code can add its own properties to a script object and expect to find them later, use `==` to decide whether two expressions refer to the same script, and so on.

(If more than one {doc}`Debugger <../debugger/index>` instance is debugging the same code, each {doc}`Debugger <../debugger/index>` gets a separate `Debugger.Script` instance for a given script. This allows the code using each {doc}`Debugger <../debugger/index>` instance to place whatever properties it likes on its `Debugger.Script` instances, without worrying about interfering with other debuggers.)

A `Debugger.Script` instance is a strong reference to a JSScript object; it protects the script it refers to from being garbage collected.

Note that SpiderMonkey may use the same `Debugger.Script` instances for equivalent functions or evaluated code—that is, scripts representing the same source code, at the same position in the same source file, evaluated in the same lexical environment.

## Debugger.Script for WebAssembly

For `Debugger.Script` instances referring to a block of WebAssembly code, they are distinguished by their `format` property being `"wasm"`.

Currently only entire modules evaluated via `new WebAssembly.Module` are represented.

`Debugger.Script` objects for WebAssembly are uncovered via `onNewScript` when a new WebAssembly module is instantiated and via the `findScripts` method on {doc}`Debugger <../debugger/index>` instances. SpiderMonkey constructs exactly one `Debugger.Script` for each underlying WebAssembly module per {doc}`Debugger <../debugger/index>` instance.

A `Debugger.Script` instance is a strong reference to the underlying WebAssembly module; it protects the module it refers to from being garbage collected.

Please note at the time of this writing, support for WebAssembly is very preliminary. Many properties and methods below throw.

## Convention

For descriptions of properties and methods below, if the behavior of the property or method differs between the instance referring to a `JSScript` or to a block of WebAssembly code, the text will be split into two sections, headed by “**if the instance refers to a JSScript**” and “**if the instance refers to WebAssembly code**”, respectively. If the behavior does not differ, no such emphasized headings will appear.

## Accessor Properties of the Debugger.Script Prototype Object

A `Debugger.Script` instance inherits the following accessor properties from its prototype:

`isGeneratorFunction`

: True if this instance refers to a `JSScript` for a function defined with a `function*` expression or statement. False otherwise.

`isAsyncFunction`

: True if this instance refers to a `JSScript` for an async function, defined with an `async function` expression or statement. False otherwise.

`displayName`

: **If the instance refers to a JSScript**, this is the script’s display name, if it has one. If the script has no display name — for example, if it is a top-level `eval` script — this is `undefined`.

  If the script’s function has a given name, its display name is the same as its function’s given name.

  If the script’s function has no name, SpiderMonkey attempts to infer an appropriate name for it given its context. For example:

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

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`url`

**If the instance refers to a JSScript**, the filename or URL from which this script’s code was loaded. For scripts created by `eval` or the `Function` constructor, this may be a synthesized filename, starting with a valid URL and followed by information tracking how the code was introduced into the system; the entire string is not a valid URL. For `Function.prototype`’s script, this is `null`. If this `Debugger.Script`’s `source` property is non-`null`, then this is equal to `source.url`.

**If the instance refers to WebAssembly code**, throw a `TypeError`.

`startLine`

: **If the instance refers to a JSScript**, the number of the line at which this script’s code starts, within the file or document named by `url`.

`lineCount`

: **If the instance refers to a JSScript**, the number of lines this script’s code occupies, within the file or document named by `url`.

`source`

: **If the instance refers to a JSScript**, the {doc}`Debugger.Source <../debugger.source/index>` instance representing the source code from which this script was produced. This is `null` if the source code was not retained.

  **If the instance refers to WebAssembly code**, the {doc}`Debugger.Source <../debugger.source/index>` instance representing the serialized text format of the WebAssembly code.

`sourceStart`

: **If the instance refers to a JSScript**, the character within the {doc}`Debugger.Source <../debugger.source/index>` instance given by `source` at which this script’s code starts; zero-based. If this is a function’s script, this is the index of the start of the `function` token in the source code.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`sourceLength`

: **If the instance refers to a JSScript**, the length, in characters, of this script’s code within the {doc}`Debugger.Source <../debugger.source/index>` instance given by `source`.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`global`

**If the instance refers to a JSScript**, a {doc}`Debugger.Object <../debugger.object/index>` instance referring to the global object in whose scope this script runs. The result refers to the global directly, not via a wrapper or a `WindowProxy` (“outer window”, in Firefox).

**If the instance refers to WebAssembly code**, throw a `TypeError`.

`format`

: **If the instance refers to a JSScript**, `"js"`.

  **If the instance refers to WebAssembly code**, `"wasm"`.

## Function Properties of the Debugger.Script Prototype Object

The functions described below may only be called with a `this` value referring to a `Debugger.Script` instance; they may not be used as methods of other kinds of objects.

`getOffsetsCoverage()`:

: **If the instance refers to a JSScript**, return `null` or an array which contains information about the coverage of all opcodes. The elements of the array are objects, each of which describes a single opcode, and contains the following properties:

- lineNumber: the line number of the current opcode.
- columnNumber: the 1-based column number of the current opcode.
- offset: the bytecode instruction offset of the current opcode.
- count: the number of times the current opcode got executed.

  If this script has no coverage, or if it is not instrumented, then this function will return `null`. To ensure that the debuggee is instrumented, the flag `Debugger.collectCoverageInfo` should be set to `true`.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`getChildScripts()`

: **If the instance refers to a JSScript**, return a new array whose elements are Debugger.Script objects for each function in this script. Only direct children are included; nested children can be reached by walking the tree.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`setBreakpoint(offset, handler)`

: **If the instance refers to a JSScript**, set a breakpoint at the bytecode instruction at *offset* in this script, reporting hits to the `hit` method of *handler*. If *offset* is not a valid offset in this script, throw an error.

  When execution reaches the given instruction, SpiderMonkey calls the `hit` method of *handler*, passing a {doc}`Debugger.Frame <../debugger.frame/index>` instance representing the currently executing stack frame. The `hit` method’s return value should be a resumption value, determining how execution should continue.

  Any number of breakpoints may be set at a single location; when control reaches that point, SpiderMonkey calls their handlers in an unspecified order.

  Any number of breakpoints may use the same *handler* object.

  Breakpoint handler method calls are cross-compartment, intra-thread calls: the call takes place in the same thread that hit the breakpoint, and in the compartment containing the handler function (typically the debugger’s compartment).

  The new breakpoint belongs to the {doc}`Debugger <../debugger/index>` instance to which this script belongs. Disabling the {doc}`Debugger <../debugger/index>` instance disables this breakpoint; and removing a global from the {doc}`Debugger <../debugger/index>` instance’s set of debuggees clears all the breakpoints belonging to that {doc}`Debugger <../debugger/index>` instance in that global’s scripts.

`getBreakpoints([offset])`

: **If the instance refers to a JSScript**, return an array containing the handler objects for all the breakpoints set at *offset* in this script. If *offset* is omitted, return the handlers of all breakpoints set anywhere in this script. If *offset* is present, but not a valid offset in this script, throw an error.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

`clearBreakpoint(handler, [offset])`

: **If the instance refers to a JSScript**, remove all breakpoints set in this {doc}`Debugger <../debugger/index>` instance that use *handler* as their handler. If *offset* is given, remove only those breakpoints set at *offset* that use *handler*; if *offset* is not a valid offset in this script, throw an error.

  Note that, if breakpoints using other handler objects are set at the same location(s) as *handler*, they remain in place.

`clearAllBreakpoints([offset])`

: **If the instance refers to a JSScript**, remove all breakpoints set in this script. If *offset* is present, remove all breakpoints set at that offset in this script; if *offset* is not a valid bytecode offset in this script, throw an error.

`isInCatchScope([offset])`

: **If the instance refers to a JSScript**, this is `true` if this offset falls within the scope of a try block, and `false` otherwise.

  **If the instance refers to WebAssembly code**, throw a `TypeError`.

## Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.Script.md

Watermark:

: sha256:8816a4e8617be32c4ce7f3ae54970fe9c8a7d248175d215a8990ccff23e6efa9

Changeset:

: [b2dd7d222fda+](https://github.com/mozilla-firefox/firefox/commit/b2dd7d222fda395191a9be6ed027aaeee1f6e2b6)
