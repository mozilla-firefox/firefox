# IPDL: Inter-Thread and Inter-Process Message Passing

## The Idea

**IPDL**, the "Inter-[thread|process] Protocol Definition Language", is the
Mozilla-specific language that allows code to communicate between system
threads or processes in a standardized, efficient, safe, secure and
platform-agnostic way. IPDL communications take place between *parent* and
*child* objects called *actors*. The architecture is inspired by the [actor
model](https://en.wikipedia.org/wiki/Actor_model).

:::{note}
IPDL actors differ from the actor model in one significant way -- all
IPDL communications are *only* between a parent and its only child.
:::

The actors that constitute a parent/child pair are called **peers**. Peer
actors communicate through an **endpoint**, which is an end of a message pipe.
An actor is explicitly bound to its endpoint, which in turn is bound to a
particular thread soon after it is constructed. An actor never changes its
endpoint and may only send and receive predeclared **messages** from/to that
endpoint, on that thread. Violations result in runtime errors. A thread may
be bound to many otherwise unrelated actors but an endpoint supports
**top-level** actors and any actors they **manage** (see below).

:::{note}
More precisely, endpoints can be bound to any `nsISerialEventTarget`,
which are themselves associated with a specific thread. By default,
IPDL will bind to the current thread's "main" serial event target,
which, if it exists, is retrieved with `GetCurrentSerialEventTarget`.
For the sake of clarity, this document will frequently refer to actors
as bound to threads, although the more precise interpretation of serial
event targets is also always valid.
:::

:::{note}
Internally, we use the "Ports" component of the [Chromium Mojo] library
to *multiplex* multiple endpoints (and, therefore, multiple top-level
actors). This means that the endpoints communicate over the same native
pipe, which conserves limited OS resources. The implications of this are
discussed in [IPDL Best Practices](#ipdl-best-practices).
:::

Parent and child actors may be bound to threads in different processes, in
different threads in the same process, or even in the same thread in the same
process. That last option may seem unreasonable but actors are versatile and
their layout can be established at run-time so this could theoretically arise
as the result of run-time choices. One large example of this versatility is
`PCompositorBridge` actors, which in different cases connect endpoints in the
main process and the GPU process (for UI rendering on Windows), in a content
process and the GPU process (for content rendering on Windows), in the main
process and the content process (for content rendering on Mac, where there is
no GPU process), or between threads on the main process (UI rendering on Mac).
For the most part, this does not require elaborate or redundant coding; it
just needs endpoints to be bound judiciously at runtime. The example in
{ref}`connecting-with-other-processes` shows one way this can be done. It
also shows that, without proper plain-language documentation of *all* of the
ways endpoints are configured, this can quickly lead to unmaintainable code.
Be sure to document your endpoint bindings thoroughly!!!

## The Approach

The actor framework will schedule tasks to run on its associated event target,
in response to messages it receives. Messages are specified in an IPDL
**protocol** file and the response handler tasks are defined per-message by C++
methods. As actors only communicate in pairs, and each is bound to one thread,
sending is always done sequentially, never concurrently (same for receiving).
This means that it can, and does, guarantee that an actor will always receive
messages in the same order they were sent by its related actor -- and that this
order is well defined since the related actor can only send from one thread.

:::{warning}
There are a few (rare) exceptions to the message order guarantee. They
include [synchronous nested](#the-rest) messages and messages with a `[Priority]`
or `[Compress]` annotation.
:::

An IPDL protocol file specifies the messages that may be sent between parent
and child actors, as well as the direction and payload of those messages.
Messages look like function calls but, from the standpoint of their caller,
they may start and end at any time in the future -- they are *asynchronous*,
so they won't block their sending actors or any other components that may be
running in the actor's thread's `MessageLoop`.

:::{note}
Not all IPDL messages are asynchronous. Again, we run into exceptions for
messages that are synchronous or [synchronous nested](#the-rest). Use of synchronous
and nested messages is strongly discouraged but may not always be avoidable.
They will be defined later, along with superior alternatives to both that
should work in nearly all cases.
:::

Protocol files are compiled by the *IPDL compiler* in an early stage of the
build process. The compiler generates C++ code that reflects the protocol.
Specifically, it creates one C++ class that represents the parent actor and one
that represents the child. The generated files are then automatically included
in the C++ build process. The generated classes contain public methods for
sending the protocol messages, which client code will use as the entry-point to
IPC communication. The generated methods are built atop our IPC framework,
defined in {searchfox}`/ipc <ipc>`, that
standardizes the safe and secure use of sockets, pipes, shared memory, etc on
all supported platforms. See [Using The IPDL compiler](#using-the-ipdl-compiler) for more on
integration with the build process.

Client code must be written that subclasses these generated classes, in order
to add handlers for the tasks generated to respond to each message. It must
also add routines (`ParamTraits`) that define serialization and
deserialization for any types used in the payload of a message that aren't
already known to the IPDL system. Primitive types, and a bunch of Mozilla
types, have predefined `ParamTraits` ({searchfox}`here <ipc/glue/IPCMessageUtils.h>`
and {searchfox}`here <ipc/glue/IPCMessageUtilsSpecializations.h>`).

:::{note}
Among other things, client code that uses the generated code must include
`chromium-config.mozbuild` in its `moz.build` file. See [Using The
IPDL compiler](#using-the-ipdl-compiler) for a complete list of required build changes.
:::

### The Steps To Making A New Actor

1. Decide what folder you will work in and create:

   > 1. An IPDL protocol file, named for your actor (e.g. `PMyActor.ipdl` --
   >    actor protocols must begin with a `P`). See [The Protocol Language](#the-protocol-language).
   > 2. Properly-named source files for your actor's parent and child
   >    implementations (e.g. `MyActorParent.h`, `MyActorChild.h` and,
   >    optionally, adjacent .cpp files). See [The C++ Interface](#the-c-interface).
   > 3. IPDL-specific updates to the `moz.build` file. See [Using The IPDL
   >    compiler](#using-the-ipdl-compiler).

2. Write your actor protocol (.ipdl) file:

   > 1. Decide whether you need a top-level actor or a managed actor. See
   >    [Top Level Actors](#top-level-actors).
   > 2. Find/write the IPDL and C++ data types you will use in communication.
   >    Write `ParamTraits` for C++ data types that don't have them. See
   >    [Generating IPDL-Aware C++ Data Types: IPDL Structs and Unions](#generating-ipdl-aware-c-data-types-ipdl-structs-and-unions) for IPDL
   >    structures. See [Referencing Externally Defined Data Types: IPDL
   >    Includes](#referencing-externally-defined-data-types-ipdl-includes) and [ParamTraits](#paramtraits) for C++ data types.
   > 3. Write your actor and its messages. See [Defining Actors](#defining-actors).

3. Write C++ code to create and destroy instances of your actor at runtime.

   > - For managed actors, see [Actor Lifetimes in C++](#actor-lifetimes-in-c).
   > - For top-level actors, see [Creating Top Level Actors From Other Actors](#creating-top-level-actors-from-other-actors).
   >   The first actor in a process is a very special exception -- see [Creating
   >   First Top Level Actors](#creating-first-top-level-actors).

4. Write handlers for your actor's messages. See [Actors and Messages in
   C++](#actors-and-messages-in-c).

5. Start sending messages through your actors! Again, see [Actors and Messages
   in C++](#actors-and-messages-in-c).

## The Protocol Language

This document will follow the integration of two actors into Firefox --
`PMyManager` and `PMyManaged`. `PMyManager` will manage `PMyManaged`.
A good place to start is with the IPDL actor definitions. These are files
that are named for the actor (e.g. `PMyManager.ipdl`) and that declare the
messages that a protocol understands. These actors are for demonstration
purposes and involve quite a bit of functionality. Most actors will use a very
small fraction of these features.

```{literalinclude} _static/PMyManager.ipdl
:language: c++
:name: PMyManager.ipdl
```

```{literalinclude} _static/PMyManaged.ipdl
:language: c++
:name: PMyManaged.ipdl
```

These files reference three additional files. `MyTypes.ipdlh` is an "IPDL
header" that can be included into `.ipdl` files as if it were inline, except
that it also needs to include any external actors and data types it uses:

```{literalinclude} _static/MyTypes.ipdlh
:language: c++
:name: MyTypes.ipdlh
```

`MyActorUtils.h` and `MyDataTypes.h` are normal C++ header files that
contain definitions for types passed by these messages, as well as instructions
for serializing them. They will be covered in [The C++ Interface](#the-c-interface).

### Using The IPDL compiler

To build IPDL files, list them (alphabetically sorted) in a `moz.build` file.
In this example, the `.ipdl` and `.ipdlh` files would be alongside a
`moz.build` containing:

```python
IPDL_SOURCES += [
    "MyTypes.ipdlh",
    "PMyManaged.ipdl",
    "PMyManager.ipdl",
]

UNIFIED_SOURCES += [
    "MyManagedChild.cpp",
    "MyManagedParent.cpp",
    "MyManagerChild.cpp",
    "MyManagerParent.cpp",
]

include("/ipc/chromium/chromium-config.mozbuild")
```

`chromium-config.mozbuild` sets up paths so that generated IPDL header files
are in the proper scope. If it isn't included, the build will fail with
`#include` errors in both your actor code and some internal ipc headers. For
example:

```
c:/mozilla-src/mozilla-unified/obj-64/dist/include\ipc/IPCMessageUtils.h(13,10): fatal error: 'build/build_config.h' file not found
```

`.ipdl` files are compiled to C++ files as one of the earliest post-configure
build steps. Those files are, in turn, referenced throughout the source code
and build process. From `PMyManager.ipdl` the compiler generates two header
files added to the build context and exported globally:
`mozilla/myns/PMyManagerParent.h` and `mozilla/myns/PMyManagerChild.h`, as
discussed in [Namespaces](#namespaces) below. These files contain the base classes for the
actors. It also makes several other files, including C++ source files and
another header, that are automatically included into the build and should not
require attention.

C++ definions of the actors are required for IPDL. They define the actions
that are taken in response to messages -- without this, they would have no
value. There will be much more on this when we discuss [Actors and Messages in
C++](#actors-and-messages-in-c) but note here that C++ header files named for the actor are required by
the IPDL `compiler`. The example would expect
`mozilla/myns/MyManagedChild.h`, `mozilla/myns/MyManagedParent.h`,
`mozilla/myns/MyManagerChild.h` and `mozilla/myns/MyManagerParent.h` and
will not build without them.

### Referencing Externally Defined Data Types: IPDL Includes

Let's begin with `PMyManager.ipdl`. It starts by including types that it
will need from other places:

```cpp
include protocol PMyManaged;
include MyTypes;                          // for MyActorPair

using MyActorEnum from "mozilla/myns/MyActorUtils.h";
using struct mozilla::myns::MyData from "mozilla/MyDataTypes.h";
[MoveOnly] using mozilla::myns::MyOtherData from "mozilla/MyDataTypes.h";
[RefCounted] using class mozilla::myns::MyThirdData from "mozilla/MyDataTypes.h";
```

The first line includes a protocol that PMyManager will manage. That protocol
is defined in its own `.ipdl` file. Cyclic references are expected and pose
no concern.

The second line includes the file `MyTypes.ipdlh`, which defines types like
structs and unions, but in IPDL, which means they have behavior that goes
beyond the similar C++ concepts. Details can be found in [Generating
IPDL-Aware C++ Data Types: IPDL Structs and Unions](#generating-ipdl-aware-c-data-types-ipdl-structs-and-unions).

The final lines include types from C++ headers. Additionally, the [RefCounted]
and [MoveOnly] attributes tell IPDL that the types have special functionality
that is important to operations. These are the data type attributes currently
understood by IPDL:

```{eval-rst}
================ ==============================================================
``[RefCounted]`` Type ``T`` is reference counted (by ``AddRef``/``Release``).
                 As a parameter to a message or as a type in IPDL
                 structs/unions, it is referenced as a ``RefPtr<T>``.
``[MoveOnly]``   The type ``T`` is treated as uncopyable.  When used as a
                 parameter in a message or an IPDL struct/union, it is as an
                 r-value ``T&&``.
``[Tainted]``    Message parameters of type ``T`` (including ``T?``, ``T[]``
                 and ``nullable T``) received by the parent process from
                 another process are wrapped in ``Tainted<>`` and must be
                 validated before use.  See
                 [Declaring IPDL Messages](#declaring-ipdl-messages).
================ ==============================================================
```

Attributes on a type must be the same in every `using` statement that
imports it.

Finally, note that `using`, `using class` and `using struct` are all
valid syntax. The `class` and `struct` keywords are optional.

### Namespaces

From the IPDL file:

```cpp
namespace mozilla {
namespace myns {

    // ... data type and actor definitions ...

}    // namespace myns
}    // namespace mozilla
```

Namespaces work similar to the way they do in C++. They also mimic the
notation, in an attempt to make them comfortable to use. When IPDL actors are
compiled into C++ actors, the namespace scoping is carried over. As previously
noted, when C++ types are included into IPDL files, the same is true. The most
important way in which they differ is that IPDL also uses the namespace to
establish the path to the generated files. So, the example defines the IPDL
data type `mozilla::myns::MyUnion` and the actors
`mozilla::myns::PMyManagerParent` and `mozilla::myns::PMyManagerChild`,
which can be included from `mozilla/myns/PMyManagerParent.h`,
`mozilla/myns/PMyManagerParent.h` and `mozilla/myns/PMyManagerChild.h`,
respectively. The namespace becomes part of the path.

### Generating IPDL-Aware C++ Data Types: IPDL Structs and Unions

`PMyManager.ipdl` and `MyTypes.ipdlh` define:

```cpp
[Comparable] union MyUnion {
    float;
    MyOtherData;
};

struct MyActorPair {
    PMyManaged actor1;
    nullable PMyManaged actor2;
};
```

From these descriptions, IPDL generates C++ classes that approximate the
behavior of C++ structs and unions but that come with pre-defined
`ParamTraits` implementations. These objects can also be used as usual
outside of IPDL, although the lack of control over the generated code means
they are sometimes poorly suited to use as plain data. See [ParamTraits](#paramtraits) for
details.

The `[Comparable]` attribute tells IPDL to generate `operator==` and
`operator!=` for the new type. In order for it to do that, the fields inside
the new type need to define both of those operators.

Finally, the `nullable` keyword indicates that, when serialized, the actor
may be null. It is intended to help users avoid null-object dereference
errors. It only applies to actor types and may also be attached to parameters
in message declarations.

### Defining Actors

The real point of any `.ipdl` file is that each defines exactly one actor
protocol. The definition always matches the `.ipdl` filename. Repeating the
one in `PMyManager.ipdl`:

```cpp
[ChildProc=Content]
sync protocol PMyManager {
    manages PMyManaged;

    async PMyManaged();
    // ... more message declarations ...
};
```

:::{important}
A form of reference counting is `always` used internally by IPDL to make
sure that it and its clients never address an actor the other component
deleted but this becomes fragile, and sometimes fails, when the client code
does not respect the reference count. For example, when IPDL detects that
a connection died due to a crashed remote process, deleting the actor could
leave dangling pointers, so IPDL `cannot` delete it. On the other hand,
there are many cases where IPDL is the only entity to have references to
some actors (this is very common for one side of a managed actor) so IPDL
`must` delete it. If all of those objects were reference counted then
there would be no complexity here. Indeed, new actors using
`[ManualDealloc]` should not be approved without a very compelling
reason. New `[ManualDealloc]` actors may soon be forbidden.
:::

The `sync` keyword tells IPDL that the actor contains messages that block the
sender using `sync` blocking, so the sending thread waits for a response to
the message. There is more on what it and the other blocking modes mean in
[IPDL messages](#declaring-ipdl-messages). For now, just know that this is redundant information whose
value is primarily in making it easy for other developers to know that there
are `sync` messages defined here. This list gives preliminary definitions of
the options for the actor-blocking policy of messages:

```{eval-rst}
======================= =======================================================
``async``               Actor may contain only asynchronous messages.
``sync``                Actor has ``async`` capabilities and adds  ``sync``
                        messages.  ``sync`` messages
                        can only be sent from the child actor to the parent.
======================= =======================================================
```

Beyond these protocol blocking strategies, IPDL supports annotations that
indicate the actor has messages that may be received in an order other than
the one they were sent in. These orderings attempt to handle messages in
"message thread" order (as in e.g. mailing lists). These behaviors can be
difficult to design for. Their use is discouraged but is sometimes warranted.
They will be discussed further in [Nested messages](#nested-messages).

```{eval-rst}
============================== ================================================
``[NestedUpTo=inside_sync]``   Actor has high priority messages that can be
                               handled while waiting for a ``sync`` response.
``[NestedUpTo=inside_cpow]``   Actor has the highest priority messages that
                               can be handled while waiting for a ``sync``
                               response.
============================== ================================================
```

In addition, top-level protocols are annotated with which processes each side
should be bound into using the `[ParentProc=*]` and `[ChildProc=*]`
attributes. The `[ParentProc]` attribute is optional, and defaults to the
`Parent` process. The `[ChildProc]` attribute is required. See [Process
Type Attributes](#process-type-attributes) for possible values.

The `manages` clause tells IPDL that `PMyManager` manages the
`PMyManaged` actor that was previously `include` d. As with any managed
protocol, it must also be the case that `PMyManaged.ipdl` includes
`PMyManager` and declares that `PMyManaged` is `managed` by
`PMyManager`. Recalling the code:

```cpp
// PMyManaged.ipdl
include protocol PMyManager;
// ...

protocol PMyManaged {
  manager PMyManager;
  // ...
};
```

An actor has a `manager` (e.g. `PMyManaged`) or else it is a top-level
actor (e.g. `PMyManager`). An actor protocol may be managed by more than one
actor type. For example, `PMyManaged` could have also been managed by some
`PMyOtherManager` not shown here. In that case, `manager` s are presented
in a list, separated by `or` -- e.g. `manager PMyManager or
PMyOtherManager`. Of course, an **instance** of a managed actor type has only
one manager actor (and is therefore managed by only one of the types of
manager). The manager of an instance of a managee is always the actor that
constructed that managee.

Finally, there is the message declaration `async PMyManaged()`. This message
is a constructor for `MyManaged` actors; unlike C++ classes, it is found in
`MyManager`. Every manager will need to expose constructors to create its
managed types. These constructors are the only way to create an actor that is
managed. They can take parameters and return results, like normal messages.
The implementation of IPDL constructors are discussed in [Actor Lifetimes in
C++](#actor-lifetimes-in-c).

We haven't discussed a way to construct new top level actors. This is a more
advanced topic and is covered separately in [Top Level Actors](#top-level-actors).

### Declaring IPDL Messages

The final part of the actor definition is the declaration of messages:

```cpp
sync protocol PMyManager {
  // ...
  parent:
    async __delete__(nsString aNote);
    sync SomeMsg(MyActorPair? aActors, MyData[] aMyData)
        returns (int32_t x, int32_t y, MyUnion aUnion);
    async PMyManaged();
  both:
    [Tainted] async AnotherMsg(MyActorEnum aEnum, int32_t a number)
        returns (MyOtherData aOtherData);
};
```

The messages are grouped into blocks by `parent:`, `child:` and `both:`.
These labels work the way `public:` and `private:` work in C++ -- messages
after these descriptors are sent/received (only) in the direction specified.

:::{note}
As a mnemonic to remember which direction they indicate, remember to put
the word "to" in front of them. So, for example, `parent:` precedes
`__delete__`, meaning `__delete__` is sent from the child **to** the
parent, and `both:` states that `AnotherMsg` can be sent **to** either
endpoint.
:::

IPDL messages support the following annotations:

```{eval-rst}
======================== ======================================================
``[Compress]``           Indicates repeated messages of this type will
                         consolidate.
``[Tainted]``            Parameters are required to be validated before using
                         them.
``[Priority=Foo]``       Priority of ``MessageTask`` that runs the C++ message
                         handler.  ``Foo`` is one of: ``low``, ``normal``,
                         ``mediumhigh``, ``input``, ``vsync``, or ``control``.
                         See the ``IPC::Message::PriorityValue`` enum.
``[Nested=inside_sync]`` Indicates that the message can sometimes be handled
                         while a sync message waits for a response.
``[Nested=inside_cpow]`` Indicates that the message can sometimes be handled
                         while a sync message waits for a response.
``[LazySend]``           Messages with this annotation will be queued up to be
                         sent together either immediately before a non-LazySend
                         message, or from a direct task.
======================== ======================================================
```

`[Compress]` provides crude protection against spamming with a flood of
messages. When messages of type `M` are compressed, the queue of unprocessed
messages between actors will never contain an `M` beside another one; they
will always be separated by a message of a different type. This is achieved by
throwing out the older of the two messages if sending the new one would break
the rule. This has been used to throttle pointer events between the main and
content processes.

`[Compress=all]` is similar but applies whether or not the messages are
adjacent in the message queue.

`[Tainted]` is a C++ mechanism designed to encourage paying attentiton to
parameter security. The values of tainted parameters cannot be used until you
vouch for their safety. They are discussed in [Actors and Messages in C++](#actors-and-messages-in-c).

Tainting can also be applied to individual types instead of whole messages,
by importing them with `[Tainted] using ...`. Any parameter of such a type,
in any message, is then tainted on each actor side that may run in the parent
process while the other side may run in a different process. This is
determined from the `[ParentProc]` and `[ChildProc]` attributes of the
protocol's toplevel protocols, so for `[ParentProc=GPU, ChildProc=Parent]`
the child side is tainted instead of the parent side.

Individual parameters can opt out of tainting with `[NoTaint=passback]` or
`[NoTaint=allvalid]`, where the value documents why validation is not needed:
`passback` means the value is only passed on to some other code that does its
own validation, and `allvalid` means every possible value is valid. `NoTaint`
is only allowed when the message is `[Tainted]` or the parameter's type is.

```cpp
[Tainted, RefCounted] using class nsIFoo from "nsIFoo.h";

protocol PMyProtocol {
parent:
    // aFoo is received as Tainted<RefPtr<nsIFoo>>, aOther as RefPtr<nsIFoo>.
    async Msg(nullable nsIFoo aFoo, [NoTaint=passback] nullable nsIFoo aOther);
};
```

The `Nested` annotations are deeply related to the message's blocking policy
that follows it and which was briefly discussed in [Defining Actors](#defining-actors). See
[Nested messages](#nested-messages) for details.

`[LazySend]` indicates the message doesn't need to be sent immediately, and
can be sent later, from a direct task. Worker threads which do not support
direct task dispatch will ignore this attribute. Messages with this annotation
will still be delivered in-order with other messages, meaning that if a normal
message is sent, any queued `[LazySend]` messages will be sent first. The
attribute allows the transport layer to combine messages to be sent together,
potentially reducing thread wake-ups for I/O and receiving threads.

The following is a complete list of the available blocking policies. It
resembles the list in [Defining Actors]:

```{eval-rst}
====================== ========================================================
``async``              Actor may contain only asynchronous messages.
``sync``               Actor has ``async`` capabilities and adds  ``sync``
                       messages.  ``sync`` messages can only be sent from the
                       child actor to the parent.
====================== ========================================================
```

The policy defines whether an actor will wait for a response when it sends a
certain type of message. A `sync` actor will wait immediately after sending
a `sync` message, stalling its thread, until a response is received. This is
an easy source of browser stalls. It is rarely required that a message be
synchronous. New `sync` messages are therefore required to get approval from
an IPC peer. The IPDL compiler will require such messages to be listed in the
file `sync-messages.ini`.

The notion that only child actors can send `sync` messages was introduced to
avoid potential deadlocks. It relies on the belief that a cycle (deadlock) of
sync messages is impossible because they all point in one direction. This is
no longer the case because any endpoint can be a child `or` parent and some,
like the main process, sometimes serve as both. This means that sync messages
should be used with extreme care.

:::{note}
The notion of sync messages flowing in one direction is still the main
mechanism IPDL uses to avoid deadlock. New actors should avoid violating
this rule as the consequences are severe (and complex). Actors that break
these rules should not be approved without **extreme** extenuating
circumstances. If you think you need this, check with the IPC team on
Element first (#ipc).
:::

An `async` actor will not wait. An `async` response is essentially
identical to sending another `async` message back. It may be handled
whenever received messages are handled. The value over an `async` response
message comes in the ergonomics -- async responses are usually handled by C++
lambda functions that are more like continuations than methods. This makes
them easier to write and to read. Additionally, they allow a response to
return message failure, while there would be no such response if we were
expecting to send a new async message back, and it failed.

Following synchronization is the name of the message and its parameter list.
The message `__delete__` stands out as strange -- indeed, it terminates the
actor's connection. `It does not delete any actor objects itself!` It severs
the connections of the actor `and any actors it manages` at both endpoints. An
actor will never send or receive any messages after it sends or receives a
`__delete__`. Note that all sends and receives have to happen on a specific
*worker* thread for any actor tree so the send/receive order is well defined.
Anything sent after the actor processes `__delete__` is ignored (send returns
an error, messages yet to be received fail their delivery). In other words,
some future operations may fail but no unexpected behavior is possible.

In our example, the child can break the connection by sending `__delete__` to
the parent. The only thing the parent can do to sever the connection is to
fail, such as by crashing. This sort of unidirectional control is both common
and desirable.

`PMyManaged()` is a managed actor constructor. Note the asymmetry -- an
actor contains its managed actor's constructors but its own destructor.

The list of parameters to a message is fairly straight-forward. Parameters
can be any type that has a C++ `ParamTraits` specialization and is imported
by a directive. That said, there are some surprises in the list of messages:

```{eval-rst}
================= =============================================================
``int32_t``,...   The standard primitive types are included.  See :searchfox:`builtin.py <ipc/ipdl/ipdl/builtin.py>`
                  for a list.  Pointer types are, unsurprisingly, forbidden.
``?``             When following a type T, the parameter is translated into
                  ``Maybe<T>`` in C++.
``[]``            When following a type T, the parameter is translated into
                  ``nsTArray<T>`` in C++.
================= =============================================================
```

Finally, the returns list declares the information sent in response, also as a
tuple of typed parameters. As previously mentioned, even `async` messages
can receive responses. A `sync` message will always wait for a response but
an `async` message will not get one unless it has a `returns` clause.

This concludes our tour of the IPDL example file. The connection to C++ is
discussed in the next chapter; messages in particular are covered in [Actors
and Messages in C++](#actors-and-messages-in-c). For suggestions on best practices when designing your
IPDL actor approach, see [IPDL Best Practices](#ipdl-best-practices).

### IPDL Syntax Quick Reference

The following is a list of the keywords and operators that have been introduced
for use in IPDL files:

```{eval-rst}
============================= =================================================
``include``                   Include a C++ header (quoted file name) or
                              ``.ipdlh`` file (unquoted with no file suffix).
``using (class|struct) from`` Similar to ``include`` but imports only a
                              specific data type.
``include protocol``          Include another actor for use in management
                              statements, IPDL data types or as parameters to
                              messages.
``[RefCounted]``              Indicates that the imported C++ data types are
                              reference counted. Refcounted types require a
                              different ``ParamTraits`` interface than
                              non-reference-counted types.
``[ManualDealloc]``           Indicates that the IPDL interface uses the legacy
                              manual allocation/deallocation interface, rather
                              than modern reference counting.
``[MoveOnly]``                Indicates that an imported C++ data type should
                              not be copied.  IPDL code will move it instead.
``namespace``                 Specifies the namespace for IPDL generated code.
``union``                     An IPDL union definition.
``struct``                    An IPDL struct definition.
``[Comparable]``              Indicates that IPDL should generate
                              ``operator==`` and ``operator!=`` for the given
                              IPDL struct/union.
``nullable``                  Indicates that an actor reference in an IPDL type
                              may be null when sent over IPC.
``protocol``                  An IPDL protocol (actor) definition.
``sync/async``                These are used in two cases: (1) to indicate
                              whether a message blocks as it waits for a result
                              and (2) because an actor that contains ``sync``
                              messages must itself be labeled ``sync``.
``[NestedUpTo=inside_sync]``  Indicates that an actor contains
                              [Nested=inside_sync] messages, in addition to
                              normal messages.
``[NestedUpTo=inside_cpow]``  Indicates that an actor contains
                              [Nested=inside_cpow] messages, in addition to
                              normal messages.
``[Nested=inside_sync]``      Indicates that the message can be handled while
                              waiting for lower-priority, or in-message-thread,
                              sync responses.
``[Nested=inside_cpow]``      Indicates that the message can be handled while
                              waiting for lower-priority, or in-message-thread,
                              sync responses.  Cannot be sent by the parent
                              actor.
``manager``                   Used in a protocol definition to indicate that
                              this actor manages another one.
``manages``                   Used in a protocol definition to indicate that
                              this actor is managed by another one.
``or``                        Used in a ``manager`` clause for actors that have
                              multiple potential managers.
``parent: / child: / both:``  Indicates direction of subsequent actor messages.
                              As a mnemonic to remember which direction they
                              indicate, put the word "to" in front of them.
``returns``                   Defines return values for messages.  All types
                              of message, including ``async``, support
                              returning values.
``__delete__``                A special message that destroys the related
                              actors at both endpoints when sent.
                              ``Recv__delete__`` and ``ActorDestroy`` are
                              called before destroying the actor at the other
                              endpoint, to allow for cleanup.
``int32_t``,...               The standard primitive types are included.
``String``                    Translated into ``nsString`` in C++.
``?``                         When following a type T in an IPDL data structure
                              or message parameter,
                              the parameter is translated into ``Maybe<T>`` in
                              C++.
``[]``                        When following a type T in an IPDL data structure
                              or message parameter,
                              the parameter is translated into ``nsTArray<T>``
                              in C++.
``[Tainted]``                 Used to indicate that a message's handler should
                              receive parameters that it is required to
                              manually validate.  Parameters of type ``T``
                              become ``Tainted<T>`` in C++.  When applied to
                              an imported C++ type, parameters of that type
                              received by the parent process from another
                              process are tainted.
``[NoTaint=...]``             Excludes a message parameter from tainting.
                              The value is ``passback`` or ``allvalid``.
``[Compress]``                Indicates repeated messages of this type will
                              consolidate.  When two messages of this type are
                              sent and end up side-by-side in the message queue
                              then the older message is discarded (not sent).
``[Compress=all]``            Like ``[Compress]`` but discards the older
                              message regardless of whether they are adjacent
                              in the message queue.
``[Priority=Foo]``            Priority of ``MessageTask`` that runs the C++
                              message handler.  ``Foo`` is one of: ``low``,
                              ``normal``, ``mediumhigh``, ``input``, ``vsync``,
                              or ``control``.
``[LazySend]``                Messages with this annotation will be queued up to
                              be sent together immediately before a non-LazySend
                              message, or from a direct task.
``[ChildImpl="RemoteFoo"]``   Indicates that the child side implementation of
                              the actor is a class named ``RemoteFoo``, and the
                              definition is included by one of the
                              ``include "...";`` statements in the file.
                              *New uses of this attribute are discouraged.*
``[ParentImpl="FooImpl"]``    Indicates that the parent side implementation of
                              the actor is a class named ``FooImpl``, and the
                              definition is included by one of the
                              ``include "...";`` statements in the file.
                              *New uses of this attribute are discouraged.*
``[ChildImpl=virtual]``       Indicates that the child side implementation of
                              the actor is not exported by a header, so virtual
                              ``Recv`` methods should be used instead of direct
                              function calls.  *New uses of this attribute are
                              discouraged.*
``[ParentImpl=virtual]``      Indicates that the parent side implementation of
                              the actor is not exported by a header, so virtual
                              ``Recv`` methods should be used instead of direct
                              function calls.  *New uses of this attribute are
                              discouraged.*
``[ChildProc=...]``           Indicates which process the child side of the actor
                              is expected to be bound in.  This will be release
                              asserted when creating the actor.  Required for
                              top-level actors.  See `Process Type Attributes`_
                              for possible values.
``[ParentProc=...]``          Indicates which process the parent side of the
                              actor is expected to be bound in.  This will be
                              release asserted when creating the actor.
                              Defaults to ``Parent`` for top-level actors.  See
                              `Process Type Attributes`_ for possible values.
============================= =================================================
```

(process-type-attributes)=

#### Process Type Attributes

The following are valid values for the `[ChildProc=...]` and
`[ParentProc=...]` attributes on protocols, each corresponding to a specific
process type:

```{eval-rst}
============================= =================================================
``Parent``                    The primary "parent" or "main" process
``Content``                   A content process, such as those used to host web
                              pages, workers, and extensions
``IPDLUnitTest``              Test-only process used in IPDL gtests
``GMPlugin``                  Gecko Media Plugin (GMP) process
``GPU``                       GPU process
``VR``                        VR process
``RDD``                       Remote Data Decoder (RDD) process
``Socket``                    Socket/Networking process
``ForkServer``                Fork Server process
``Utility``                   Utility process
============================= =================================================
```

The attributes also support some wildcard values, which can be used when an
actor can be bound in multiple processes. If you are adding an actor which
needs a new wildcard value, please reach out to the IPC team, and we can add one
for your use-case. They are as follows:

```{eval-rst}
============================= =================================================
``any``                       Any process.  If a more specific value is
                              applicable, it should be preferred where possible.
``anychild``                  Any process other than ``Parent``.  Often used for
                              utility actors which are bound on a per-process
                              basis, such as profiling.
``compositor``                Either the ``GPU`` or ``Parent`` process.  Often
                              used for actors bound to the compositor thread.
``anydom``                    Either the ``Parent`` or a ``Content`` process.
                              Often used for actors used to implement DOM APIs.
============================= =================================================
```

Note that these assertions do not provide security guarantees, and are primarily
intended for use when auditing and as documentation for how actors are being
used.

## The C++ Interface

### ParamTraits

Before discussing how C++ represents actors and messages, we look at how IPDL
connects to the imported C++ data types. In order for any C++ type to be
(de)serialized, it needs an implementation of the `ParamTraits` C++ type
class. `ParamTraits` is how your code tells IPDL what bytes to write to
serialize your objects for sending, and how to convert those bytes back to
objects at the other endpoint. Since `ParamTraits` need to be reachable by
IPDL code, they need to be declared in a C++ header and imported by your
protocol file. Failure to do so will result in a build error.

Most basic types and many essential Mozilla types are always available for use
without inclusion. An incomplete list includes: C++ primitives, strings
(`std` and `mozilla`), vectors (`std` and `mozilla`), `RefPtr<T>`
(for serializable `T`), `UniquePtr<T>`, `nsCOMPtr<T>`, `nsTArray<T>`,
`std::unordered_map<T>`, `nsresult`, etc. See {searchfox}`builtin.py <ipc/ipdl/ipdl/builtin.py>`,
{searchfox}`ipc_message_utils.h <ipc/chromium/src/chrome/common/ipc_message_utils.h>`
and {searchfox}`IPCMessageUtilsSpecializations.h <ipc/glue/IPCMessageUtilsSpecializations.h>`.

`ParamTraits` typically bootstrap with the `ParamTraits` of more basic
types, until they hit bedrock (e.g. one of the basic types above). In the most
extreme cases, a `ParamTraits` author may have to resort to designing a
binary data format for a type. Both options are available.

We haven't seen any of this C++ yet. Let's look at the data types included
from `MyDataTypes.h`:

```cpp
// MyDataTypes.h
namespace mozilla::myns {
    struct MyData {
        nsCString s;
        uint8_t bytes[17];
        MyData();      // IPDL requires the default constructor to be public
    };

    struct MoveonlyData {
        MoveonlyData();
        MoveonlyData& operator=(const MoveonlyData&) = delete;

        MoveonlyData(MoveonlyData&& m);
        MoveonlyData& operator=(MoveonlyData&& m);
    };

    typedef MoveonlyData MyOtherData;

    class MyUnusedData {
    public:
        NS_INLINE_DECL_REFCOUNTING(MyUnusedData)
        int x;
    };
};

namespace IPC {
    // Basic type
    template<>
    struct ParamTraits<mozilla::myns::MyData> {
        typedef mozilla::myns::MyData paramType;
        static void Write(MessageWriter* m, const paramType& in);
        static bool Read(MessageReader* m, paramType* out);
    };

    // [MoveOnly] type
    template<>
    struct ParamTraits<mozilla::myns::MyOtherData> {
        typedef mozilla::myns::MyOtherData paramType;
        static void Write(MessageWriter* m, const paramType& in);
        static bool Read(MessageReader* m, paramType* out);
    };

    // [RefCounted] type
    template<>
    struct ParamTraits<mozilla::myns::MyUnusedData*> {
        typedef mozilla::myns::MyUnusedData paramType;
        static void Write(MessageWriter* m, paramType* in);
        static bool Read(MessageReader* m, RefPtr<paramType>* out);
    };
}
```

MyData is a struct and MyOtherData is a typedef. IPDL is fine with both.
Additionally, MyOtherData is not copyable, matching its IPDL `[MoveOnly]`
annotation.

`ParamTraits` are required to be defined in the `IPC` namespace. They must
contain a `Write` method with the proper signature that is used for
serialization and a `Read` method, again with the correct signature, for
deserialization.

Here we have three examples of declarations: one for an unannotated type, one
for `[MoveOnly]` and a `[RefCounted]` one. Notice the difference in the
`[RefCounted]` type's method signatures. The only difference that may not be
clear from the function types is that, in the non-reference-counted case, a
default-constructed object is supplied to `Read` but, in the
reference-counted case, `Read` is given an empty `RefPtr<MyUnusedData>` and
should only allocate a `MyUnusedData` to return if it so desires.

These are straight-forward implementations of the `ParamTraits` methods for
`MyData`:

```cpp
/* static */ void IPC::ParamTraits<MyData>::Write(MessageWriter* m, const paramType& in) {
    WriteParam(m, in.s);
    m->WriteBytes(in.bytes, sizeof(in.bytes));
}
/* static */ bool IPC::ParamTraits<MyData>::Read(MessageReader* m, paramType* out) {
    return ReadParam(m, &out->s) &&
           m->ReadBytesInto(out->bytes, sizeof(out->bytes));
}
```

`WriteParam` and `ReadParam` call the `ParamTraits` for the data you pass
them, determined using the type of the object as supplied. `WriteBytes` and
`ReadBytesInto` work on raw, contiguous bytes as expected. `MessageWriter`
and `MessageReader` are IPDL internal objects which hold the incoming/outgoing
message as a stream of bytes and the current spot in the stream. It is *very*
rare for client code to need to create or manipulate these objects. Their
advanced use is beyond the scope of this document.

:::{important}
Potential failures in `Read` include everyday C++ failures like
out-of-memory conditions, which can be handled as usual. But `Read` can
also fail due to things like data validation errors. `ParamTraits` read
data that is considered insecure. It is important that they catch
corruption and properly handle it. Returning false from `Read` will
usually result in crashing the process (everywhere except in the main
process). This is the right behavior as the browser would be in an
unexpected state, even if the serialization failure was not malicious
(since it cannot process the message). Other responses, such as failing
with a crashing assertion, are inferior. IPDL fuzzing relies on
`ParamTraits` not crashing due to corruption failures.
Occasionally, validation will require access to state that `ParamTraits`
can't easily reach. (Only) in those cases, validation can be reasonably
done in the message handler. Such cases are a good use of the `Tainted`
annotation. See [Actors and Messages in C++](#actors-and-messages-in-c) for more.
:::

:::{note}
If you need to access the actor object during serialization, use
`IPC::Message{Reader,Writer}::GetActor()`. This should be null-checked, as
it may not be set if an actor is unavailable.
:::

A special case worth mentioning is that of enums. Enums are a common source of
security holes since code is rarely safe with enum values that are not valid.
Since data obtained through IPDL messages should be considered tainted, enums
are of principal concern. `ContiguousEnumSerializer` and
`ContiguousEnumSerializerInclusive` safely implement `ParamTraits` for
enums that are only valid for a contiguous set of values, which is most of
them. The generated `ParamTraits` confirm that the enum is in valid range;
`Read` will return false otherwise. As an example, here is the
`MyActorEnum` included from `MyActorUtils.h`:

```cpp
enum MyActorEnum { e1, e2, e3, e4, e5 };

template<>
struct ParamTraits<MyActorEnum>
  : public ContiguousEnumSerializerInclusive<MyActorEnum, MyActorEnum::e1, MyActorEnum::e5> {};
```

### IPDL Structs and Unions in C++

IPDL structs and unions become C++ classes that provide interfaces that are
fairly self-explanatory. Recalling `MyUnion` and `MyActorPair` from
[IPDL Structs and Unions](#generating-ipdl-aware-c-data-types-ipdl-structs-and-unions) :

```cpp
union MyUnion {
    float;
    MyOtherData;
};

struct MyActorPair {
    PMyManaged actor1;
    nullable PMyManaged actor2;
};
```

These compile to:

```cpp
class MyUnion {
    enum Type { Tfloat, TMyOtherData };
    Type type();
    MyUnion(float f);
    MyUnion(MyOtherData&& aOD);
    MyUnion& operator=(float f);
    MyUnion& operator=(MyOtherData&& aOD);
    operator float&();
    operator MyOtherData&();
};

class MyActorPair {
    MyActorPair(PMyManagedParent* actor1Parent, PMyManagedChild* actor1Child,
                PMyManagedParent* actor2Parent, PMyManagedChild* actor2Child);
    // Exactly one of { actor1Parent(), actor1Child() } must be non-null.
    PMyManagedParent*& actor1Parent();
    PMyManagedChild*& actor1Child();
    // As nullable, zero or one of { actor2Parent(), actor2Child() } will be non-null.
    PMyManagedParent*& actor2Parent();
    PMyManagedChild*& actor2Child();
}
```

The generated `ParamTraits` use the `ParamTraits` for the types referenced
by the IPDL struct or union. Fields respect any annotations for their type
(see [IPDL Includes](#referencing-externally-defined-data-types-ipdl-includes)). For example, a `[RefCounted]` type `T` generates
`RefPtr<T>` fields.

Note that actor members result in members of both the parent and child actor
types, as seen in `MyActorPair`. When actors are used to bridge processes,
only one of those could ever be used at a given endpoint. IPDL makes sure
that, when you send one type (say, `PMyManagedChild`), the adjacent actor of
the other type (`PMyManagedParent`) is received. This is not only true for
message parameters and IPDL structs/unions but also for custom `ParamTraits`
implementations. If you `Write` a `PFooParent*` then you must `Read` a
`PFooChild*`. This is hard to confuse in message handlers since they are
members of a class named for the side they operate on, but this cannot be
enforced by the compiler. If you are writing
`MyManagerParent::RecvSomeMsg(Maybe<MyActorPair>&& aActors, nsTArray<MyData>&& aMyData)`
then the `actor1Child` and `actor2Child` fields cannot be valid since the
child (usually) exists in another process.

### Actors and Messages in C++

As mentioned in [Using The IPDL compiler](#using-the-ipdl-compiler), the IPDL compiler generates two
header files for the protocol `PMyManager`: `PMyManagerParent.h` and
`PMyManagerChild.h`, which declare the actor's base classes. There, we
discussed how the headers are visible to C++ components that include
`chromium-config.mozbuild`. We, in turn, always need to define two files
that declare our actor implementation subclasses (`MyManagerParent.h` and
`MyManagerChild.h`). The IPDL file looked like this:

```{literalinclude} _static/PMyManager.ipdl
:language: c++
```

So `MyManagerParent.h` looks like this:

```cpp
#include "PMyManagerParent.h"

namespace mozilla {
namespace myns {

class MyManagerParent : public PMyManagerParent {
    NS_INLINE_DECL_REFCOUNTING(MyManagerParent, override)
protected:
    IPCResult Recv__delete__(const nsString& aNote);
    IPCResult RecvSomeMsg(const Maybe<MyActorPair>& aActors, const nsTArray<MyData>& aMyData,
                          int32_t* x, int32_t* y, MyUnion* aUnion);
    IPCResult RecvAnotherMsg(const Tainted<MyActorEnum>& aEnum, const Tainted<int32_t>& a number,
                             AnotherMsgResolver&& aResolver);

    already_AddRefed<PMyManagerParent> AllocPMyManagedParent();
    IPCResult RecvPMyManagedConstructor(PMyManagedConstructor* aActor);

    // ... etc ...
};

} // namespace myns
} // namespace mozilla
```

All messages that can be sent to the actor must be handled by `Recv` methods
in the proper actor subclass. They should return `IPC_OK()` on success and
`IPC_FAIL(actor, reason)` if an error occurred (where `actor` is `this`
and `reason` is a human text explanation) that should be considered a failure
to process the message. The handling of such a failure is specific to the
process type.

`Recv` methods are called by IPDL by enqueueing a task to run them on the
`MessageLoop` for the thread on which they are bound. This thread is the
actor's *worker thread*. All actors in a managed actor tree have the same
worker thread -- in other words, actors inherit the worker thread from their
managers. Top level actors establish their worker thread when they are
*bound*. More information on threads can be found in [Top Level Actors](#top-level-actors). For
the most part, client code will never engage with an IPDL actor outside of its
worker thread.

Received parameters become stack variables that are `std::move`-d into the
`Recv` method. They can be received as a const l-value reference,
rvalue-reference, or by value (type-permitting). `[MoveOnly]` types should
not be received as const l-values. Return values for sync messages are
assigned by writing to non-const (pointer) parameters. Return values for async
messages are handled differently -- they are passed to a resolver function. In
our example, `AnotherMsgResolver` would be a `std::function<>` and
`aResolver` would be given the value to return by passing it a reference to a
`MyOtherData` object.

`MyManagerParent` is also capable of `sending` an async message that
returns a value: `AnotherMsg`. This is done with `SendAnotherMsg`, which
is defined automatically by IPDL in the base class `PMyManagerParent`. There
are two signatures for `Send` and they look like this:

```cpp
// Return a Promise that IPDL will resolve with the response or reject.
RefPtr<MozPromise<MyOtherData, ResponseRejectReason, true>>
SendAnotherMsg(const MyActorEnum& aEnum, int32_t a number);

// Provide callbacks to process response / reject.  The callbacks are just
// std::functions.
void SendAnotherMsg(const MyActorEnum& aEnum, int32_t a number,
    ResolveCallback<MyOtherData>&& aResolve, RejectCallback&& aReject);
```

The response is usually handled by lambda functions defined at the site of the
`Send` call, either by attaching them to the returned promise with e.g.
`MozPromise::Then`, or by passing them as callback parameters. See docs on
`MozPromise` for more on its use. The promise itself is either resolved or
rejected by IPDL when a valid reply is received or when the endpoint determines
that the communication failed. `ResponseRejectReason` is an enum IPDL
provides to explain failures.

Additionally, the `AnotherMsg` handler has `Tainted` parameters, as a
result of the [Tainted] annotation in the protocol file. Recall that
`Tainted` is used to force explicit validation of parameters in the message
handler before their values can be used (as opposed to validation in
`ParamTraits`). They therefore have access to any state that the message
handler does. Their APIs, along with a list of macros that are used to
validate them, are detailed {searchfox}`here <mfbt/Tainting.h>`.

Send methods that are not for async messages with return values follow a
simpler form; they return a `bool` indicating success or failure and return
response values in non-const parameters, as the `Recv` methods do. For
example, `PMyManagerChild` defines this to send the sync message `SomeMsg`:

```cpp
// generated in PMyManagerChild
bool SendSomeMsg(const Maybe<MyActorPair>& aActors, const nsTArray<MyData>& aMyData,
                 int32_t& x, int32_t& y, MyUnion& aUnion);
```

Since it is sync, this method will not return to its caller until the response
is received or an error is detected.

All calls to `Send` methods, like all messages handler `Recv` methods, must
only be called on the worker thread for the actor.

Constructors, like the one for `MyManaged`, are clearly an exception to these
rules. They are discussed in the next section.

(actor-lifetimes-in-c)=

### Actor Lifetimes in C++

The constructor message for `MyManaged` becomes *two* methods at the
receiving end. `AllocPMyManagedParent` constructs the managed actor, then
`RecvPMyManagedConstructor` is called to update the new actor. The following
diagram shows the construction of the `MyManaged` actor pair:

```{mermaid}
---
align: center
caption: >
  A ``MyManaged`` actor pair being created by some ``Driver`` object.
  Internal IPC objects in the parent and child processes are combined for
  compactness.  Connected **par** blocks run concurrently.  This shows that
  messages can be safely sent while the parent is still being constructed.
---

%%{init: {'sequence': {'boxMargin': 4, 'actorMargin': 10} }}%%
sequenceDiagram
    participant d as Driver
    participant mgdc as MyManagedChild
    participant mgrc as MyManagerChild
    participant ipc as IPC Child/Parent
    participant mgrp as MyManagerParent
    participant mgdp as MyManagedParent
    d->>mgdc: new
    mgdc->>d: [mgd_child]
    d->>mgrc: SendPMyManagedConstructor<br/>[mgd_child, params]
    mgrc->>ipc: Form actor pair<br/>[mgd_child, params]
    par
    mgdc->>ipc: early PMyManaged messages
    and
    ipc->>mgrp: AllocPMyManagedParent<br/>[params]
    mgrp->>mgdp: new
    mgdp->>mgrp: [mgd_parent]
    ipc->>mgrp: RecvPMyManagedConstructor<br/>[mgd_parent, params]
    mgrp->>mgdp: initialization
    ipc->>mgdp: early PMyManaged messages
    end
    Note over mgdc,mgdp: Bi-directional sending and receiving will now happen concurrently.
```

The next diagram shows the destruction of the `MyManaged` actor pair, as
initiated by a call to `Send__delete__`. `__delete__` is sent from the
child process because that is the only side that can call it, as declared in
the IPDL protocol file.

```{mermaid}
---
align: center
caption: >
  A ``MyManaged`` actor pair being disconnected due to some ``Driver``
  object in the child process sending ``__delete__``.
---

%%{init: {'sequence': {'boxMargin': 4, 'actorMargin': 10} }}%%
sequenceDiagram
    participant d as Driver
    participant mgdc as MyManagedChild
    participant ipc as IPC Child/Parent
    participant mgdp as MyManagedParent
    d->>mgdc: Send__delete__
    mgdc->>ipc: Disconnect<br/>actor pair
    par
    ipc->>mgdc: ActorDestroy
    ipc->>mgdc: Release
    and
    ipc->>mgdp: Recv__delete__
    ipc->>mgdp: ActorDestroy
    ipc->>mgdp: Release
    end
```

Finally, let's take a look at the behavior of an actor whose peer has been lost
(e.g. due to a crashed process).

```{mermaid}
---
align: center
caption: >
  A ``MyManaged`` actor pair being disconnected when its peer is lost due to
  a fatal error.  Note that ``Recv__delete__`` is not called.
---

%%{init: {'sequence': {'boxMargin': 4, 'actorMargin': 10} }}%%
sequenceDiagram
    participant mgdc as MyManagedChild
    participant ipc as IPC Child/Parent
    participant mgdp as MyManagedParent
    Note over mgdc: CRASH!!!
    ipc->>ipc: Notice fatal error.
    ipc->>mgdp: ActorDestroy
    ipc->>mgdp: Release
```

The `Alloc` and `Recv...Constructor` methods are somewhat mirrored by
`Recv__delete__` and `ActorDestroy` but there are a few differences.
First, the `Alloc` method really does create the actor but the
`ActorDestroy` method does not delete it. Additionally, `ActorDestroy`
is run at *both* endpoints, during `Send__delete__` or after
`Recv__delete__`. Finally and most importantly, `Recv__delete__` is only
called if the `__delete__` message is received but it may not be if, for
example, the remote process crashes. `ActorDestroy`, on the other hand, is
guaranteed to run for *every* actor unless the process terminates uncleanly.
For this reason, `ActorDestroy` is the right place for most actor shutdown
code. `Recv__delete__` is rarely useful, although it is occasionally
beneficial to have it receive some final data.

The relevant part of the parent class looks like this:

```cpp
class MyManagerParent : public PMyManagerParent {
    already_AddRefed<PMyManagedParent> AllocPMyManagedParent();
    IPCResult RecvPMyManagedConstructor(PMyManagedParent* aActor);

    IPCResult Recv__delete__(const nsString& aNote);
    void ActorDestroy(ActorDestroyReason why);

    // ... etc ...
};
```

The `Alloc` method is required for managed actors that are constructed by
IPDL receiving a `Send` message. It is not required for the actor at the
endpoint that calls `Send`. The `Recv...Constructor` message is not
required -- it has a base implementation that does nothing.

If the constructor message has parameters, they are sent to both methods.
Parameters are given to the `Alloc` method by const reference but are moved
into the `Recv` method. They differ in that messages can be sent from the
`Recv` method but, in `Alloc`, the newly created actor is not yet
operational.

The `Send` method for a constructor is similarly different from other
`Send` methods. In the child actor, ours looks like this:

```cpp
IPCResult SendPMyManagedConstructor(PMyManagedChild* aActor);
```

The method expects a `PMyManagedChild` that the caller will have constructed,
presumably using `new` (this is why it does not require an `Alloc` method).
Once `Send...Constructor` is called, the actor can be used to send and
receive messages. It does not matter that the remote actor may not have been
created yet due to asynchronicity.

The destruction of actors is as unusual as their construction. Unlike
construction, it is the same for managed and top-level actors. Avoiding
`[ManualDealloc]` actors removes a lot of the complexity but there is still
a process to understand. Actor destruction begins when an `__delete__`
message is sent. In `PMyManager`, this message is declared from child to
parent. The actor calling `Send__delete__` is no longer connected to
anything when the method returns. Future calls to `Send` return an error
and no future messages will be received. This is also the case for an actor
that has run `Recv__delete__`; it is no longer connected to the other
endpoint.

:::{note}
Since `Send__delete__` may release the final reference to itself, it
cannot safely be a class instance method. Instead, unlike other `Send`
methods, it's a `static` class method and takes the actor as a parameter:

```cpp
static IPCResult Send__delete__(PMyManagerChild* aToDelete);
```

Additionally, the `__delete__` message tells IPDL to disconnect both the
given actor *and all of its managed actors*. So it is really deleting the
actor subtree, although `Recv__delete__` is only called for the actor it
was sent to.
:::

During the call to `Send__delete__`, or after the call to `Recv__delete__`,
the actor's `ActorDestroy` method is called. This method gives client code a
chance to do any teardown that must happen in `all` circumstances were it is
possible -- both expected and unexpected. This means that `ActorDestroy`
will also be called when, for example, IPDL detects that the other endpoint has
terminated unexpectedly, so it is releasing its reference to the actor, or
because an ancestral manager (manager or manager's manager...) received a
`__delete__`. The only way for an actor to avoid `ActorDestroy` is for its
process to crash first. `ActorDestroy` is always run after its actor is
disconnected so it is pointless to try to send messages from it.

Why use `ActorDestroy` instead of the actor's destructor? `ActorDestroy`
gives a chance to clean up things that are only used for communication and
therefore don't need to live for however long the actor's (reference-counted)
object does. For example, you might have references to shared memory (Shmems)
that are no longer valid. Or perhaps the actor can now release a cache of data
that was only needed for processing messages. It is cleaner to deal with
communication-related objects in `ActorDestroy`, where they become invalid,
than to leave them in limbo until the destructor is run.

Consider actors to be like normal reference-counted objects, but where IPDL
holds a reference while the connection will or does exist. One common
architecture has IPDL holding the `only` reference to an actor. This is common
with actors created by sending constructor messages but the idea is available to
any actor. That only reference is then released when the `__delete__`
message is sent or received.

The dual of IPDL holding the only reference is to have client code hold the
only reference. A common pattern to achieve this has been to override the
actor's `AddRef` to have it send `__delete__` only when it's count is down
to one reference (which must be IPDL if `actor.CanSend()` is true). A better
approach would be to create a reference-counted delegate for your actor that
can send `__delete__` from its destructor. IPDL does not guarantee that it
will not hold more than one reference to your actor.

(top-level-actors)=

## Top Level Actors

Recall that top level actors are actors that have no manager. They are at the
root of every actor tree. There are two settings in which we use top-level
actors that differ pretty dramatically. The first type are top-level actors
that are created and maintained in a way that resembles managed actors, but
with some important differences we will cover in this section. The second type
of top-level actors are the very first actors in a new process -- these actors
are created through different means and closing them (usually) terminates the
process. The [new process example](https://phabricator.services.mozilla.com/D119038) demonstrates both of
these. It is discussed in detail in {ref}`adding-a-new-type-of-process`.

### Value of Top Level Actors

Top-level actors are harder to create and destroy than normal actors. They
used to be more heavyweight than managed actors but this has recently been
dramatically reduced.

:::{note}
Top-level actors previously required a dedicated *message channel*, which
are limited OS resources. This is no longer the case -- message channels
are now shared by actors that connect the same two processes. This
*message interleaving* can affect message delivery latency but profiling
suggests that the change was basically inconsequential.
:::

So why use a new top level actor?

- The most dramatic property distinguishing top-level actors is the ability to
  *bind* to whatever `EventTarget` they choose. This means that any thread
  that runs a `MessageLoop` can use the event target for that loop as the
  place to send incoming messages. In other words, `Recv` methods would be
  run by that message loop, on that thread. The IPDL apparatus will
  asynchronously dispatch messages to these event targets, meaning that
  multiple threads can be handling incoming messages at the same time. The
  [PBackground](#pbackground) approach was born of a desire to make it easier to exploit
  this, although it has some complications, detailed in that section, that
  limit its value.
- Top level actors suggest modularity. Actor protocols are tough to debug, as
  is just about anything that spans process boundaries. Modularity can give
  other developers a clue as to what they need to know (and what they don't)
  when reading an actor's code. The alternative is proverbial *dumpster
  classes* that are as critical to operations (because they do so much) as they
  are difficult to learn (because they do so much).
- Top level actors are required to connect two processes, regardless of whether
  the actors are the first in the process or not. As said above, the first
  actor is created through special means but other actors are created through
  messages. In Gecko, apart from the launcher and main processes, all new
  processes X are created with their first actor being between X and the main
  process. To create a connection between X and, say, a content process, the
  main process has to send connected `Endpoints` to X and to the content
  process, which in turn use those endpoints to create new top level actors
  that form an actor pair. This is discussed at length in
  {ref}`connecting-with-other-processes`.

Top-level actors are not as frictionless as desired but they are probably
under-utilized relative to their power. In cases where it is supported,
`PBackground` is sometimes a simpler alternative to achieve the same goals.

### Creating Top Level Actors From Other Actors

The most common way to create new top level actors is by creating a pair of
connected Endpoints and sending one to the other actor. This is done exactly
the way it sounds. For example:

```cpp
bool MyPreexistingActorParent::MakeMyActor() {
    Endpoint<PMyActorParent> parentEnd;
    Endpoint<PMyActorChild> childEnd;
    if (NS_WARN_IF(NS_FAILED(PMyActor::CreateEndpoints(&parentEnd, &childEnd)))) {
        // ... handle failure ...
        return false;
    }
    RefPtr<MyActorParent> parent = new MyActorParent;
    if (!parentEnd.Bind(parent)) {
        // ... handle failure ...
        delete parent;
        return false;
    }
    // Do this second so we skip child if parent failed to connect properly.
    if (!SendCreateMyActorChild(std::move(childEnd))) {
        // ... assume an IPDL error will destroy parent.  Handle failure beyond that ...
        return false;
    }
    return true;
}
```

Here `MyPreexistingActorParent` is used to send a child endpoint for the new
top level actor to `MyPreexistingActorChild`, after it hooks up the parent
end. In this example, we bind our new actor to the same thread we are running
on -- which must be the same thread `MyPreexistingActorParent` is bound to
since we are sending `CreateMyActorChild` from it. We could have bound on a
different thread.

At this point, messages can be sent on the parent. Eventually, it will start
receiving them as well.

`MyPreexistingActorChild` still has to receive the create message. The code
for that handler is pretty similar:

```cpp
IPCResult MyPreexistingActorChild::RecvCreateMyActorChild(Endpoint<PMyActorChild>&& childEnd) {
    RefPtr<MyActorChild> child = new MyActorChild;
    if (!childEnd.Bind(child)) {
        // ... handle failure and return ok, assuming a related IPDL error will alert the other side to failure ...
        return IPC_OK();
    }
    return IPC_OK();
}
```

Like the parent, the child is ready to send as soon as `Bind` is complete.
It will start receiving messages soon afterward on the event target for the
thread on which it is bound.

### Creating First Top Level Actors

The first actor in a process is an advanced topic that is covered in
{ref}`the documentation for adding a new process <adding-a-new-type-of-process>`.

## PBackground

Developed as a convenient alternative to top level actors, `PBackground` is
an IPDL protocol whose managees choose their worker threads in the child
process and share a thread dedicated solely to them in the parent process.
When an actor (parent or child) should run without hogging the main thread,
making that actor a managee of `PBackground` (aka a *background actor*) is an
option.

:::{warning}
Background actors can be difficult to use correctly, as spelled out in this
section. It is recommended that other options -- namely, top-level actors
-- be adopted instead.
:::

Background actors can only be used in limited circumstances:

- `PBackground` only supports the following process connections (where
  ordering is parent \<-> child): main \<-> main and main \<-> content.
- Background actor creation is always initiated by the child. Of course, a
  request to create one can be sent to the child by any other means.
- All parent background actors run in the same thread. This thread is
  dedicated to serving as the worker for parent background actors. While it
  has no other functions, it should remain responsive to all connected
  background actors. For this reason, it is a bad idea to conduct long
  operations in parent background actors. For such cases, create a top level
  actor and an independent thread on the parent side instead.
- Background actors are currently *not* reference-counted. IPDL's ownership
  has to be carefully respected and the (de-)allocators for the new actors have
  to be defined. See [The Old Ways](#the-old-ways) for details.

A hypothetical layout of `PBackground` threads, demonstrating some of the
process-type limitations, is shown in the diagram below.

```{mermaid}
---
align: center
caption: >
  Hypothetical ``PBackground`` thread setup.  Arrow direction indicates
  child-to-parent ``PBackground``-managee relationships.  Parents always
  share a thread and may be connected to multiple processes.  Child threads
  can be any thread, including main.
---

flowchart LR
    subgraph content #1
        direction TB
        c1tm[main]
        c1t1[worker #1]
        c1t2[worker #2]
        c1t3[worker #3]
    end
    subgraph content #2
        direction TB
        c2tm[main]
        c2t1[worker #1]
        c2t2[worker #2]
    end
    subgraph socket
        direction TB
        stm[main]
        st1[background parent /\nworker #1]
        st2[worker #2]
    end
    subgraph main
        direction TB
        mtm[main]
        mt1[background parent]
    end

    %% PBackground connections
    c1tm --> mt1
    c1t1 --> mt1
    c1t2 --> mt1

    c1t3 --> mt1
    c1t3 --> st1

    c2t1 --> st1
    c2t1 --> mt1

    c2t2 --> mt1

    c2tm --> st1

    stm --> mt1
    st1 --> mt1
    st2 --> mt1
```

Creating background actors is done a bit differently than normal managees. The
new managed type and constructor are still added to `PBackground.ipdl` as
with normal managees but, instead of `new`-ing the child actor and then
passing it in a `SendFooConstructor` call, background actors issue the send
call to the `BackgroundChild` manager, which returns the new child:

```cpp
// Bind our new PMyBackgroundActorChild to the current thread.
PBackgroundChild* bc = BackgroundChild::GetOrCreateForCurrentThread();
if (!bc) {
    return false;
}
PMyBackgroundActorChild* pmyBac = bac->SendMyBackgroundActor(constructorParameters);
if (!pmyBac) {
    return false;
}
auto myBac = static_cast<MyBackgroundActorChild*>(pmyBac);
```

:::{note}
`PBackgroundParent` still needs a `RecvMyBackgroundActorConstructor`
handler, as usual. This must be done in the `ParentImpl` class.
`ParentImpl` is the non-standard name used for the implementation of
`PBackgroundParent`.
:::

To summarize, `PBackground` attempts to simplify a common desire in Gecko:
to run tasks that communicate between the main and content processes but avoid
having much to do with the main thread of either. Unfortunately, it can be
complicated to use correctly and has missed on some favorable IPDL
improvements, like reference counting. While top level actors are always a
complete option for independent jobs that need a lot of resources,
`PBackground` offers a compromise for some cases.

## IPDL Best Practices

IPC performance is affected by a lot of factors. Many of them are out of our
control, like the influence of the system thread scheduler on latency or
messages whose travel internally requires multiple legs for security reasons.
On the other hand, some things we can and should control for:

- Messages incur inherent performance overhead for a number of reasons: IPDL
  internal thread latency (e.g. the I/O thread), parameter (de-)serialization,
  etc. While not usually dramatic, this cost can add up. What's more, each
  message generates a fair amount of C++ code. For these reasons, it is wise
  to reduce the number of messages being sent as far as is reasonable. This
  can be as simple as consolidating two asynchronous messages that are always
  in succession. Or it can be more complex, like consolidating two
  somewhat-overlapping messages by merging their parameter lists and marking
  parameters that may not be needed as optional. It is easy to go too far down
  this path but careful message optimization can show big gains.
- Even `[moveonly]` parameters are "copied" in the sense that they are
  serialized. The pipes that transmit the data are limited in size and require
  allocation. So understand that the performance of your transmission will be
  inversely proportional to the size of your content. Filter out data you
  won't need. For complex reasons related to Linux pipe write atomicity, it is
  highly desirable to keep message sizes below 4K (including a small amount for
  message metadata).
- On the flip side, very large messages are not permitted by IPDL and will
  result in a runtime error. The limit is currently 256M but message failures
  frequently arise even with slightly smaller messages.
- Parameters to messages are C++ types and therefore can be very complex in the
  sense that they generally represent a tree (or graph) of objects. If this
  tree has a lot of objects in it, and each of them is serialized by
  `ParamTraits`, then we will find that serialization is allocating and
  constructing a lot of objects, which will stress the allocator and cause
  memory fragmentation. Avoid this by using larger objects or by sharing this
  kind of data through careful use of shared memory.
- As it is with everything, concurrency is critical to the performance of IPDL.
  For actors, this mostly manifests in the choice of bound thread. While
  adding a managed actor to an existing actor tree may be a quick
  implementation, this new actor will be bound to the same thread as the old
  one. This contention may be undesirable. Other times it may be necessary
  since message handlers may need to use data that isn't thread safe or may
  need a guarantee that the two actors' messages are received in order. Plan
  up front for your actor hierarchy and its thread model. Recognize when you
  are better off with a new top level actor or `PBackground` managee that
  facilitates processing messages simultaneously.
- Remember that latency will slow your entire thread, including any other
  actors/messages on that thread. If you have messages that will need a long
  time to be processed but can run concurrently then they should use actors
  that run on a separate thread.
- Top-level actors decide a lot of properties for their managees. Probably the
  most important are the process layout of the actor (including which process
  is "Parent" and which is "Child") and the thread. Every top-level actor
  should clearly document this, ideally in their .ipdl file.

## The Old Ways

TODO:

## The FUD

TODO:

## The Rest

### Nested messages

The `Nested` message annotations indicate the nesting type of the message.
They attempt to process messages in the nested order of the "conversation
thread", as found in e.g. a mailing-list client. This is an advanced concept
that should be considered to be discouraged, legacy functionality.
Essentially, `Nested` messages can make other `sync` messages break the
policy of blocking their thread -- nested messages are allowed to be received
while a sync messagee is waiting for a response. The rules for when a nested
message can be handled are somewhat complex but they try to safely allow a
`sync` message `M` to handle and respond to some special (nested) messages
that may be needed for the other endpoint to finish processing `M`. There is
a {searchfox}`comment in MessageChannel <mozilla-central/rev/077501b34cca91763ae04f4633a42fddd919fdbd:ipc/glue/MessageChannel.cpp#54-118>` with info on how the decision to handle nested
messages is made. For sync nested messages, note that this implies a relay
between the endpoints, which could dramatically affect their throughput.

Declaring messages to nest requires an annotation on the actor and one on the
message itself. The nesting annotations were listed in [Defining Actors](#defining-actors) and
[Declaring IPDL Messages](#declaring-ipdl-messages). We repeat them here. The actor annotations
specify the maximum priority level of messages in the actor. It is validated
by the IPDL compiler. The annotations are:

```{eval-rst}
============================== ================================================
``[NestedUpTo=inside_sync]``   Indicates that an actor contains messages of
                               priority [Nested=inside_sync] or lower.
``[NestedUpTo=inside_cpow]``   Indicates that an actor contains messages of
                               priority [Nested=inside_cpow] or lower.
============================== ================================================
```

:::{note}
The order of the nesting priorities is:
(no nesting priority) < `inside_sync` < `inside_cpow`.
:::

The message annotations are:

```{eval-rst}
========================== ====================================================
``[Nested=inside_sync]``   Indicates that the message can be handled while
                           waiting for lower-priority, or in-message-thread,
                           sync responses.
``[Nested=inside_cpow]``   Indicates that the message can be handled while
                           waiting for lower-priority, or in-message-thread,
                           sync responses.  Cannot be sent by the parent actor.
========================== ====================================================
```

:::{note}
`[Nested=inside_sync]` messages must be sync (this is enforced by the
IPDL compiler) but `[Nested=inside_cpow]` may be async.
:::

Nested messages are obviously only interesting when sent to an actor that is
performing a synchronous wait. Therefore, we will assume we are in such a
state. Say `actorX` is waiting for a sync reply from `actorY` for message
`m1` when `actorY` sends `actorX` a message `m2`. We distinguish two
cases here: (1) when `m2` is sent while processing `m1` (so `m2` is sent
by the `RecvM1()` method -- this is what we mean when we say "nested") and
(2) when `m2` is unrelated to `m1`. Case (2) is easy; `m2` is only
dispatched while `m1` waits if
`priority(m2) > priority(m1) > (no priority)` and the message is being
received by the parent, or if `priority(m2) >= priority(m1) > (no priority)`
and the message is being received by the child. Case (1) is less
straightforward.

To analyze case (1), we again distinguish the two possible ways we can end up
in the nested case: (A) `m1` is sent by the parent to the child and `m2`
is sent by the child to the parent, or (B) where the directions are reversed.
The following tables explain what happens in all cases:

## Case (A): Child sends message to a parent that is awaiting a sync response

| sync `m1` type (from parent) | `m2` type (from child) | `m2` handled or rejected |
| --- | --- | --- |
| sync (no priority) | \* | IPDL compiler error: parent cannot send sync (no priority) |
| sync inside_sync | async (no priority) | <del>`m2` delayed until after `m1` completes</del><br/>Currently `m2` is handled during the sync wait (bug?) |
| sync inside_sync | sync (no priority) | <del>`m2` send fails: lower priority than `m1`</del><br/>Currently `m2` is handled during the sync wait (bug?) |
| sync inside_sync | sync inside_sync | `m2` handled during `m1` sync wait: same message thread and same priority |
| sync inside_sync | async inside_cpow | `m2` handled during `m1` sync wait: higher priority |
| sync inside_sync | sync inside_cpow | `m2` handled during `m1` sync wait: higher priority |
| sync inside_cpow | \* | IPDL compiler error: parent cannot use inside_cpow priority |

## Case (B): Parent sends message to a child that is awaiting a sync response

| sync `m1` type (from child) | `m2` type (from parent) | `m2` handled or rejected |
| --- | --- | --- |
| \* | async (no priority) | `m2` delayed until after `m1` completes |
| \* | sync (no priority) | IPDL compiler error: parent cannot send sync (no priority) |
| sync (no priority) | sync inside_sync | `m2` send fails: no-priority sync messages cannot handle incoming messages during wait |
| sync inside_sync | sync inside_sync | `m2` handled during `m1` sync wait: same message thread and same priority |
| sync inside_cpow | sync inside_sync | `m2` send fails: lower priority than `m1` |
| \* | async inside_cpow | IPDL compiler error: parent cannot use inside_cpow priority |
| \* | sync inside_cpow | IPDL compiler error: parent cannot use inside_cpow priority |

We haven't seen rule #2 from the {searchfox}`comment in MessageChannel <mozilla-central/rev/077501b34cca91763ae04f4633a42fddd919fdbd:ipc/glue/MessageChannel.cpp#54-118>` in action but, as
the comment mentions, it is needed to break deadlocks in cases where both the
parent and child are initiating message-threads simultaneously. It
accomplishes this by favoring the parent's sent messages over the child's when
deciding which message-thread to pursue first (and blocks the other until the
first completes). Since this distinction is entirely thread-timing based,
client code needs only to be aware that IPDL internals will not deadlock
because of this type of race, and that this protection is limited to a single
actor tree -- the parent/child messages are only well-ordered when under the
same top-level actor so simultaneous sync messages across trees are still
capable of deadlock.

Clearly, tight control over these types of protocols is required to predict how
they will coordinate within themselves and with the rest of the application
objects. Control flow, and hence state, can be very difficult to predict and
are just as hard to maintain. This is one of the key reasons why we have
stressed that message priorities should be avoided whenever possible.

(message-logging)=

### Message Logging

The environment variable `MOZ_IPC_MESSAGE_LOG` controls the logging of IPC
messages. It logs details about the transmission and reception of messages.
This isn't controlled by `MOZ_LOG` -- it is a separate system. Set this
variable to `1` to log information on all IPDL messages, or specify a
comma-separated list of protocols to log.
If the `Child` or `Parent` suffix is given, then only activity on the given
side is logged; otherwise, both sides are logged. All protocol names must
include the `P` prefix.

For example:

```
MOZ_IPC_MESSAGE_LOG="PMyManagerChild,PMyManaged"
```

This requests logging of child-side activity on `PMyManager`, and both
parent- and child-side activity on `PMyManaged`.

{ref}`debugging-with-ipdl-logging` has an example where IPDL logging is useful
in tracking down a bug.

[chromium mojo]: https://chromium.googlesource.com/chromium/src/+/refs/heads/main/mojo/core/README.md#Port
