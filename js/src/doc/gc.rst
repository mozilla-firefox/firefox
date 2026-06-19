SpiderMonkey garbage collector
==============================

The SpiderMonkey garbage collector is responsible for allocating memory
representing JavaScript data structures and deallocating them when they are no
longer in use. It aims to collect as much data as possible in as little time
as possible. As well as JavaScript data it is also used to allocate some
internal SpiderMonkey data structures.

The garbage collector is a hybrid tracing collector, and has the following
features:

 - :ref:`Precise <precise-gc>`
 - :ref:`Incremental <incremental-gc>`
 - :ref:`Generational <generational-gc>`
 - :ref:`Partially concurrent <concurrent-gc>`
 - :ref:`Parallel <parallel-gc>`
 - :ref:`Compacting <compacting-gc>`
 - :ref:`Partitioned heap <partitioned-heap>`

For an overview of garbage collection see:
https://en.wikipedia.org/wiki/Tracing_garbage_collection

Description of features
#######################

.. _precise-gc:

Precise collection
******************

The GC is 'precise' in that it knows the layout of allocations (which is used
to determine reachable children) and also the location of all stack roots. This
means it does not need to resort to conservative techniques that may cause
garbage to be retained unnecessarily.

Knowledge of the stack is achieved with C++ wrapper classes that must be used
for stack roots and handles (pointers) to them. This is enforced by the
SpiderMonkey API (which operates in terms of these types) and checked by a
static analysis that reports places when unrooted GC pointers can be present
when a GC could occur.

For details of stack rooting, see: https://github.com/mozilla-spidermonkey/spidermonkey-embedding-examples/blob/esr78/docs/GC%20Rooting%20Guide.md

We also have a :doc:`static analysis <HazardAnalysis/index>` for detecting
errors in rooting. It can be :doc:`run locally or in CI <HazardAnalysis/running>`.

Rust, WebAssembly GC, and topology instrumentation
**************************************************

Rust ownership and SpiderMonkey garbage collection solve different memory
management problems. Rust code uses ownership, borrowing, RAII, and explicit
unsafe boundaries. SpiderMonkey's GC manages JavaScript and WebAssembly GC
heap values that participate in tracing, barriers, weak edges, nursery
collection, tenuring, and compaction.

Code that bridges Rust or WebAssembly data with SpiderMonkey GC things must
not hide GC pointers from the tracer. SpiderMonkey remains responsible for
rooting, edge classification, barriers, weak edges, and scheduling any cleanup
that is coupled to GC activity.

Experimental topology-based instrumentation should therefore observe compact
graph summaries rather than replace collector behavior. For example, a
test-only observer may score whether JS-to-Wasm reference boundaries, stable
root cuts, or strongly connected components converge across GC cycles. Such
instrumentation must be disabled by default and must not run arbitrary
destructor work during GC.

.. _incremental-gc:

Incremental collection
**********************

'Stop the world' collectors run a whole collection in one go, which can result
in unacceptable pauses for users.  An incremental collector breaks its
execution into a number of small slices, reducing user impact.

As far as possible the SpiderMonkey collector runs incrementally.  Not all
parts of a collection can be performed incrementally however as there are some
operations that need to complete atomically with respect to the rest of the
program.

Currently, most of the collection is performed incrementally.  Root marking,
compacting, and an initial part of sweeping are not.

.. _generational-gc:

Generational collection
***********************

Most real world allocations either die very quickly or live for a long
time. This suggests an approach to collection where allocations are moved
between 'generations' (separate heaps) depending on how long they have
survived. Generations containing young allocations are fast to collect and can
be collected more frequently; older generations are collected less often.

The SpiderMonkey collector implements a single young generation (the nursery)
and a single old generation (the tenured heap). Collecting the nursery is
known as a minor GC as opposed to a major GC that collects the whole heap
(including the nursery).

.. _concurrent-gc:

Concurrent collection
*********************

Many systems have more than one CPU and therefore can benefit from offloading
GC work to another core.  In GC terms 'concurrent' usually refers to GC work
happening while the main program continues to run.

The SpiderMonkey collector currently only uses concurrency in limited phases.

This includes most finalization work (there are some restrictions as not all
finalization code can tolerate this) and some other aspects such as allocating
and decommitting blocks of memory.

Performing marking work concurrently is currently being investigated.

.. _parallel-gc:

Parallel collection
*******************

In GC terms 'parallel' usually means work performed in parallel while the
collector is running, as opposed to the main program itself.  The SpiderMonkey
collector performs work within GC slices in parallel wherever possible.

.. _compacting-gc:

Compacting collection
*********************

The collector allocates data with the same type and size in 'arenas' (often know
as slabs). After many allocations have died this can leave many arenas
containing free space (external fragmentation). Compacting remedies this by
moving allocations between arenas to free up as much memory as possible.

Compacting involves tracing the entire heap to update pointers to moved data
and is not incremental so it only happens rarely, or in response to memory
pressure notifications.

.. _partitioned-heap:

Partitioned heap
****************

The collector has the concept of 'zones' which are separate heaps which can be
collected independently. Objects in different zones can refer to each other
however.

Zones are also used to help incrementalize parts of the collection. For
example, compacting is not fully incremental but can be performed one zone at
a time.

Other documentation
###################

More details about the Garbage Collector (GC) can be found by looking for the
`[SMDOC] Garbage Collector`_ comment in the sources.

.. _[SMDOC] Garbage Collector: https://searchfox.org/mozilla-central/search?q=[SMDOC]+Garbage+Collector
