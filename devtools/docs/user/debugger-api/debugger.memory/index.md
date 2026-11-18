# Debugger.Memory

The {doc}`Debugger API <../index>` can help tools observe the debuggee’s memory use in various ways:

- It can mark each new object with the JavaScript call stack at which it was allocated.
- It can log all object allocations, yielding a stream of JavaScript call stacks at which allocations have occurred.
- It can compute a *census* of items belonging to the debuggee, categorizing items in various ways, and yielding item counts.

If *dbg* is a {doc}`Debugger <../debugger/index>` instance, then the methods and accessor properties of `dbg.memory` control how *dbg* observes its debuggees’ memory use. The `dbg.memory` object is an instance of `Debugger.Memory`; its inherited accessors and methods are described below.

## Allocation Site Tracking

The JavaScript engine marks each new object with the call stack at which it was allocated, if:

- the object is allocated in the scope of a global object that is a debuggee of some {doc}`Debugger <../debugger/index>` instance *dbg*; and
- {ref}`dbg.memory.trackingAllocationSites <debugger-api-debugger-memory-tracking-allocation-sites>` is set to `true`.
- A [Bernoulli trial](https://en.wikipedia.org/wiki/Bernoulli_trial) succeeds, with probability equal to the maximum of {ref}`d.memory.allocationSamplingProbability <debugger-api-debugger-memory-alloc-sampling-probability>` of all `Debugger` instances `d` that are observing the global that this object is allocated within the scope of.

Given a {doc}`Debugger.Object <../debugger.object/index>` instance *dobj* referring to some object, {ref}`dobj.allocationSite <debugger-api-debugger-object-allocation-site>` returns a saved call stack indicating where *dobj*’s referent was allocated.

## Allocation Logging

If *dbg* is a {doc}`Debugger <../debugger/index>` instance, and {ref}`dbg.memory.trackingAllocationSites <debugger-api-debugger-memory-tracking-allocation-sites>` is set to `true`, then the JavaScript engine logs each object allocated by *dbg*’s debuggee code. You can retrieve the current log by calling *dbg.memory.drainAllocationsLog*. You can control the limit on the log’s size by setting {ref}`dbg.memory.maxAllocationsLogLength <debugger-api-debugger-memory-max-alloc-log>`.

## Censuses

A *census* is a complete traversal of the graph of all reachable memory items belonging to a particular `Debugger`‘s debuggees. It produces a count of those items, broken down by various criteria. If *dbg* is a {doc}`Debugger <../debugger/index>` instance, you can call *dbg.memory.takeCensus* to conduct a census of its debuggees’ possessions.

## Accessor Properties of the `Debugger.Memory.prototype` Object

If *dbg* is a {doc}`Debugger <../debugger/index>` instance, then `dbg.memory` is a `Debugger.Memory` instance, which inherits the following accessor properties from its prototype:

(debugger-api-debugger-memory-tracking-allocation-sites)=

`trackingAllocationSites`

A boolean value indicating whether this `Debugger.Memory` instance is capturing the JavaScript execution stack when each Object is allocated. This accessor property has both a getter and setter: assigning to it enables or disables the allocation site tracking. Reading the accessor produces `true` if the Debugger is capturing stacks for Object allocations, and `false` otherwise. Allocation site tracking is initially disabled in a new Debugger.

Assignment is fallible: if the Debugger cannot track allocation sites, it throws an `Error` instance.

You can retrieve the allocation site for a given object with the {ref}`Debugger.Object.prototype.allocationSite <debugger-api-debugger-object-allocation-site>` accessor property.

(debugger-api-debugger-memory-alloc-sampling-probability)=

`allocationSamplingProbability`

A number between 0 and 1 that indicates the probability with which each new allocation should be entered into the allocations log. 0 is equivalent to “never”, 1 is “always”, and .05 would be “one out of twenty”.

The default is 1, or logging every allocation.

Note that in the presence of multiple `Debugger` instances observing the same allocations within a global’s scope, the maximum `allocationSamplingProbability` of all the `Debugger` is used.

(debugger-api-debugger-memory-max-alloc-log)=

`maxAllocationsLogLength`

The maximum number of allocation sites to accumulate in the allocations log at a time. This accessor can be both fetched and stored to. Its default value is `5000`.

(debugger-api-debugger-memory-alloc-log-overflowed)=

`allocationsLogOverflowed`

Returns `true` if there have been more than \[`maxAllocationsLogLength`\][#max-alloc-log] allocations since the last time \[`drainAllocationsLog`\][#drain-alloc-log] was called and some data has been lost. Returns `false` otherwise.

## Debugger.Memory Handler Functions

Similar to {doc}`Debugger's handler functions <../index>`, `Debugger.Memory` inherits accessor properties that store handler functions for SpiderMonkey to call when given events occur in debuggee code.

Unlike `Debugger`‘s hooks, `Debugger.Memory`’s handlers’ return values are not significant, and are ignored. The handler functions receive the `Debugger.Memory`’s owning `Debugger` instance as their `this` value. The owning `Debugger`’s `uncaughtExceptionHandler` is still fired for errors thrown in `Debugger.Memory` hooks.

On a new `Debugger.Memory` instance, each of these properties is initially `undefined`. Any value assigned to a debugging handler must be either a function or `undefined`; otherwise a `TypeError` is thrown.

Handler functions run in the same thread in which the event occurred. They run in the compartment to which they belong, not in a debuggee compartment.

`onGarbageCollection(statistics)`

A garbage collection cycle spanning one or more debuggees has just been completed.

The *statistics* parameter is an object containing information about the GC cycle. It has the following properties:

`collections`

The `collections` property’s value is an array. Because SpiderMonkey’s collector is incremental, a full collection cycle may consist of multiple discrete collection slices with the JS mutator running interleaved. For each collection slice that occurred, there is an entry in the `collections` array with the following form:

```javascript
{
  "startTimestamp": timestamp,
  "endTimestamp": timestamp,
}
```

Here the *timestamp* values are timestamps of the GC slice’s start and end events.

`reason`

A very short string describing the reason why the collection was triggered. Known values include the following:

- “API”
- “EAGER_ALLOC_TRIGGER”
- “DESTROY_RUNTIME”
- “LAST_DITCH”
- “TOO_MUCH_MALLOC”
- “ALLOC_TRIGGER”
- “DEBUG_GC”
- “COMPARTMENT_REVIVED”
- “RESET”
- “OUT_OF_NURSERY”
- “EVICT_NURSERY”
- “FULL_STORE_BUFFER”
- “SHARED_MEMORY_LIMIT”
- “PERIODIC_FULL_GC”
- “INCREMENTAL_TOO_SLOW”
- “DOM_WINDOW_UTILS”
- “COMPONENT_UTILS”
- “MEM_PRESSURE”
- “CC_WAITING”
- “CC_FORCED”
- “LOAD_END”
- “PAGE_HIDE”
- “NSJSCONTEXT_DESTROY”
- “SET_NEW_DOCUMENT”
- “SET_DOC_SHELL”
- “DOM_UTILS”
- “DOM_IPC”
- “DOM_WORKER”
- “INTER_SLICE_GC”
- “REFRESH_FRAME”
- “FULL_GC_TIMER”
- “SHUTDOWN_CC”
- “USER_INACTIVE”

`nonincrementalReason`

If SpiderMonkey’s collector determined it could not incrementally collect garbage, and had to do a full GC all at once, this is a short string describing the reason it determined the full GC was necessary. Otherwise, `null` is returned. Known values include the following:

- “GC mode”
- “malloc bytes trigger”
- “allocation trigger”
- “requested”

`gcCycleNumber`

The GC cycle’s “number”. Does not correspond to the number of GC cycles that have run, but is guaranteed to be monotonically increasing.

## Function Properties of the `Debugger.Memory.prototype` Object

Memory Use Analysis Exposes Implementation Details

Memory analysis may yield surprising results, because browser implementation details that are transparent to content JavaScript often have visible effects on memory consumption. Web developers need to know their pages’ actual memory consumption on real browsers, so it is correct for the tool to expose these behaviors, as long as it is done in a way that helps developers make decisions about their own code.

This section covers some areas where Firefox’s actual behavior deviates from what one might expect from the specified behavior of the web platform.

### Objects

SpiderMonkey objects usually use less memory than the naïve “table of properties with attributes” model would suggest. For example, it is typical for many objects to have identical sets of properties, with only the properties’ values varying from one object to the next. To take advantage of this regularity, SpiderMonkey objects with identical sets of properties may share their property metadata; only property values are stored directly in the object.

Array objects may also be optimized, if the set of live indices is dense.

### Strings

SpiderMonkey has three representations of strings:

- Normal: the string’s text is counted in its size.
- Substring: the string is a substring of some other string, and points to that string for its storage. This representation may result in a small string retaining a very large string. However, the memory consumed by the string itself is a small constant independent of its size, since it is a reference to the larger string, a start position, and a length.
- Concatenations: When asked to concatenate two strings, SpiderMonkey may elect to delay copying the strings’ data, and represent the result as a pointer to the two original strings. Again, such a string retains other strings, but the memory consumed by the string itself is a small constant independent of its size, since it is a pair of pointers.

SpiderMonkey converts strings from the more complex representations to the simpler ones when it pleases. Such conversions usually increase memory consumption.

SpiderMonkey shares some strings amongst all web pages and browser JS. These shared strings, called *atoms*, are not included in censuses’ string counts.

### Scripts

SpiderMonkey has a complex, hybrid representation of JavaScript code. There are four representations kept in memory:

- *Source code*. SpiderMonkey retains a copy of most JavaScript source code.
- *Compressed source code*. SpiderMonkey compresses JavaScript source code, and de-compresses it on demand. Heuristics determine how long to retain the uncompressed code.
- *Bytecode*. This is SpiderMonkey’s parsed representation of JavaScript. Bytecode can be interpreted directly, or used as input to a just-in-time compiler. Source is parsed into bytecode on demand; functions that are never called are never parsed.
- *Machine code*. SpiderMonkey includes several just-in-time compilers, each of which translates JavaScript source or bytecode to machine code. Heuristics determine which code to compile, and which compiler to use. Machine code may be dropped in response to memory pressure, and regenerated as needed.

Furthermore, SpiderMonkey tracks which types of values have appeared in variables and object properties. This type information can be large.

In a census, all the various forms of JavaScript code are placed in the `"script"` category. Type information is accounted to the `"types"` category.

### Source Metadata

Generated from file:

: js/src/doc/Debugger/Debugger.Memory.md

Watermark:

: sha256:2c1529d6932efec8c624a6f1f366b09cb7fce625a6468657fab81788240bc7ae

Changeset:

: [3ef5a14350ec](https://github.com/mozilla-firefox/firefox/commit/3ef5a14350ecfc851fc94e470a507e28f854668b)
