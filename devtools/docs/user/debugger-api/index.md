# Debugger-API

## The `Debugger` Interface

Mozilla’s JavaScript engine, SpiderMonkey, provides a debugging interface named `Debugger` which lets JavaScript code observe and manipulate the execution of other JavaScript code. Both Firefox’s built-in developer tools and the Firebug add-on use `Debugger` to implement their JavaScript debuggers. However, `Debugger` is quite general, and can be used to implement other kinds of tools like tracers, coverage analysis, patch-and-continue, and so on.

`Debugger` has three essential qualities:

- It is a *source level* interface: it operates in terms of the JavaScript language, not machine language. It operates on JavaScript objects, stack frames, environments, and code, and presents a consistent interface regardless of whether the debuggee is interpreted, compiled, or optimized. If you have a strong command of the JavaScript language, you should have all the background you need to use `Debugger` successfully, even if you have never looked into the language’s implementation.
- It is for use *by JavaScript code*. JavaScript is both the debuggee language and the tool implementation language, so the qualities that make JavaScript effective on the web can be brought to bear in crafting tools for developers. As is expected of JavaScript APIs, `Debugger` is a *sound* interface: using (or even misusing) `Debugger` should never cause Gecko to crash. Errors throw proper JavaScript exceptions.
- It is an *intra-thread* debugging API. Both the debuggee and the code using `Debugger` to observe it must run in the same thread. Cross-thread, cross-process, and cross-device tools must use `Debugger` to observe the debuggee from within the same thread, and then handle any needed communication themselves. (Firefox’s builtin tools have a {doc}`protocol </devtools/backend/protocol>` defined for this purpose.)

In Gecko, the `Debugger` API is available to chrome code only. By design, it ought not to introduce security holes, so in principle it could be made available to content as well; but it is hard to justify the security risks of the additional attack surface.

The `Debugger` API cannot currently observe self-hosted JavaScript. This is not inherent in the API’s design, but that the self-hosting infrastructure isn’t prepared for the kind of invasions the `Debugger` API can perform.

## Debugger Instances and Shadow Objects

`Debugger` reflects every aspect of the debuggee’s state as a JavaScript value—not just actual JavaScript values like objects and primitives, but also stack frames, environments, scripts, and compilation units, which are not normally accessible as objects in their own right.

Here is a JavaScript program in the process of running a timer callback function:

```{image} shadows.svg
:alt: A running JavaScript program and its Debugger shadows
:class: center
```

A running JavaScript program and its Debugger shadows

This diagram shows the various types of shadow objects that make up the Debugger API (which all follow some general conventions):

- A {doc}`Debugger.Object <debugger.object/index>` represents a debuggee object, offering a reflection-oriented API that protects the debugger from accidentally invoking getters, setters, proxy traps, and so on.
- A {doc}`Debugger.Script <debugger.script/index>` represents a block of JavaScript code—either a function body or a top-level script. Given a `Debugger.Script`, one can set breakpoints, translate between source positions and bytecode offsets (a deviation from the “source level” design principle), and find other static characteristics of the code.
- A {doc}`Debugger.Frame <debugger.frame/index>` represents a running stack frame. You can use these to walk the stack and find each frame’s script and environment. You can also set `onStep` and `onPop` handlers on frames.
- A {doc}`Debugger.Environment <debugger.environment/index>` represents an environment, associating variable names with storage locations. Environments may belong to a running stack frame, captured by a function closure, or reflect some global object’s properties as variables.

The {doc}`Debugger <debugger/index>` instance itself is not really a shadow of anything in the debuggee; rather, it maintains the set of global objects which are to be considered debuggees. A `Debugger` observes only execution taking place in the scope of these global objects. You can set functions to be called when new stack frames are pushed; when new code is loaded; and so on.

Omitted from this picture are {doc}`Debugger.Source <debugger.source/index>` instances, which represent JavaScript compilation units. A `Debugger.Source` can furnish a full copy of its source code, and explain how the code entered the system, whether via a call to `eval`, a `<script>` element, or otherwise. A `Debugger.Script` points to the `Debugger.Source` from which it is derived.

Also omitted is the `Debugger`’s {doc}`Debugger.Memory <debugger.memory/index>` instance, which holds methods and accessors for observing the debuggee’s memory use.

All these types follow some general conventions, which you should look through before drilling down into any particular type’s specification.

All shadow objects are unique per `Debugger` and per referent. For a given `Debugger`, there is exactly one `Debugger.Object` that refers to a particular debuggee object; exactly one `Debugger.Frame` for a particular stack frame; and so on. Thus, a tool can store metadata about a shadow’s referent as a property on the shadow itself, and count on finding that metadata again if it comes across the same referent. And since shadows are per-`Debugger`, tools can do so without worrying about interfering with other tools that use their own `Debugger` instances.

## Examples

Here are some things you can try out yourself that show off some of `Debugger`’s features:

- {doc}`Setting a breakpoint <tutorial-breakpoint/index>` in a page, running a handler function when it is hit that evaluates an expression in the page’s context.
- {doc}`Showing how many objects different call paths allocate. <tutorial-allocation-log-tree/index>`

## Gecko-specific features

While the `Debugger` core API deals only with concepts common to any JavaScript implementation, it also includes some Gecko-specific features:

- {doc}`Global tracking <debugger/index>` supports debugging all the code running in a Gecko instance at once—the ‘chrome debugging’ model.
- {doc}`Object wrapper <debugger.object/index>` functions help manipulate object references that cross privilege boundaries.

### Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger-API.md

Watermark:

: sha256:6ee2381145a0d2e53d2f798f3f682e82dd7ab0caa0ac4dd5e56601c2e49913a7

Changeset:

: [d446aadd185e](https://github.com/mozilla-firefox/firefox/commit/d446aadd185e716de508a777e6378ef31a94694c)

```{toctree}
:hidden:
:maxdepth: 1

debugger/index
debugger.environment/index
debugger.frame/index
debugger.memory/index
debugger.object/index
debugger.script/index
debugger.source/index
tutorial-allocation-log-tree/index
tutorial-breakpoint/index
```
