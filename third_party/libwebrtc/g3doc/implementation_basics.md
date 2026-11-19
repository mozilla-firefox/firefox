<!-- go/cmark -->

<!--* freshness: {owner: 'tommi' reviewed: '2026-06-17'} *-->

# Basic concepts and primitives

## Time

Internally, time is represent using the [webrtc::Timestamp][1] class. This
represents time with a resolution of one microsecond, using a 64-bit integer,
and provides converters to milliseconds or seconds as needed.

All timestamps need to be measured from the system monotonic time.

The epoch is not specified (because we can't always know if the system clock is
correct), but whenever an absolute epoch is needed, the Unix time epoch (Jan 1,
1970 at 0:00 GMT) is used.

Conversion from/to other formats (for example milliseconds, NTP times, timestamp
strings) should happen as close to the interface requiring that format as
possible.

NOTE: There are parts of the codebase that don't use Timestamp, parts of the
codebase that use the NTP epoch, and parts of the codebase that don't use the
monotonic clock. They need to be updated.

## Threads

All execution happens on a TaskQueue instance. How a TaskQueue is implemented
varies by platform, but they all have the [webrtc::TaskQueueBase][3] API.

This API offers primitives for posting tasks, with or without delay.

Some core parts use the [webrtc::Thread][2], which is a subclass of
TaskQueueBase. This may contain a SocketServer for processing I/O, and is used
for policing certain calling pattern between a few core threads (the
NetworkThread cannot do Invoke on the signaling thread, for instance).

## Reserved class suffixes

C++ classes with names ending in the suffixes "Factory", "Builder" and "Manager"
are supposed to behave in certain well known ways.

For a particular class name Foo, the following classes, if they exist, should
behave as follows:

- FooFactory: Has a Create function that creates a Foo object and returns the
  object or an owning reference to it (for instance std::unique_ptr or
  webrtc::scoped_refptr<Foo>). The Create function should NOT alter the factory
  state; ideally, it is marked const. Ownership of the returned object is only
  with the caller.

- FooBuilder: Has a Build function that returns ownership of a Foo object (as
  above). The Builder can only be used once, and resources given to the Builder
  before the Build function is called are either released or owned by the Foo
  object. The Build function may be reference-qualified (declared as
  `Foo Build() &&`), which means it is invoked as `std::move(builder).Build()`,
  and C++ will ensure that it is not used again.

- FooManager: Has a Create function that returns an webrtc::scoped_refptr<Foo>
  (if shared ownership) or a Foo\* (if the Manager retains sole ownership). If
  Create() cannot fail, consider returning a Foo&. The Manager is responsible
  for keeping track of the object; if the Create function returns a Foo\*, the
  Foo object is guaranteed to be destroyed when the FooManager is destroyed.

If a Manager class manages multiple classes of objects, the Create functions
should be appropriately named (the FooAndBarManager would have CreateFoo() and
CreateBar() functions), and the class will have a suitable name for the group of
objects it is managing.

FooFactory is mainly useful for the case where preparation for producing Foo
objects is complex. If Foo can be created with just an argument list, consider
exposing its constructor instead; if Foo creation can fail, consider having a
free function called CreateFoo instead of a factory.

Note that classes with these names exist that do not follow these conventions.
When they are detected, they need to be marked with TODO statements and bugs
filed on them to get them into a conformant state.

## Synchronization primitives

### PostTask and thread-guarded variables

The preferred method for synchronization is to post tasks between threads, and
to let each thread take care of its own variables (lock-free programming). All
variables in classes intended to be used with multiple threads should therefore
be annotated with RTC_GUARDED_BY(thread).

For classes used with only one thread, the recommended pattern is to let them
own a webrtc::SequenceChecker (conventionally named sequence_checker\_) and let
all variables be RTC_GUARDED_BY(sequence_checker\_).

Member variables marked const do not need to be guarded, since they never
change. (But note that they may point to objects that can change!)

When posting tasks with callbacks, it is the duty of the caller to check that
the object one is calling back into still exists when the callback is made. A
helper for this task is the [webrtc::ScopedTaskSafety][5] flag, which can
automatically drop callbacks in this situation, and associated classes.

### Synchronization primitives to be used when needed

When it is absolutely necessary to let one thread wait for another thread to do
something, Thread::BlockingCall can be used. This function is DISCOURAGED, since
it leads to performance issues, but is currently still widespread.

When it is absolutely necessary to access one variable from multiple threads,
the webrtc::Mutex can be used. Such variables MUST be marked up with
RTC_GUARDED_BY(mutex), to allow static analysis that lessens the chance of
deadlocks or unintended consequences.

Avoid mixing use of Mutex and TaskQueue. Prefer lock-free programming.

### Synchronization primitives that are being removed

The following non-exhaustive list of synchronization primitives are in the
(slow) process of being removed from the codebase.

- RecursiveCriticalSection. Try to use [webrtc::Mutex][6] instead, and don't
  recurse.

- std::atomic. See [the abseil article about the subject][7] for why.

## Enum-To-String functions

If there is a need to convert an enum to a string representation, such as for
enums exposed at the Javascript API interface, the recommended way is to write a
function named AsString, declared "static constexpr" and returning an
absl::string_view. The declaration should be right after the enum declaration,
in the same scope; the implementation (which must be marked "inline") should be
at the end of the same header file.

If the enum is not defined within a class, the "static" keyword is not needed.

## Namespaces

`cricket` and `rtc` namespaces do not exist; they were removed in 2025. All
WebRTC types are in the `webrtc` namespace.

## Verification of changes

When making changes to the source code, it is not enough to only build the test
binary that covers the modified code.

Before uploading changes to gerrit for review, make sure to complete the
following steps:

- Ensure that your branch is up to date.
- Run `git cl format`. This will format source code and build (.gn) files.
- Check for and fix any IWYU errors.
- Check for and fix any gn errors.
- Ensure that a *full build* of all binaries passes. This will save time and
  resources.
- If at any of the above steps changes had to be made, go back the first step
  and repeat until no changes are required at every step.

## Pointer Nullability Annotations

To ensure high-quality, machine-checkable C++ code in WebRTC, adhere to the
following rules regarding pointer nullability.

- **Core Principle**: Prevent accidental use of invalid null values by
  explicitly declaring the nullability contract of all raw and smart pointers.

- **Classify Every Pointer**: Avoid leaving pointers in the "Unknown"
  (unannotated) state. Actively classify whether null is a valid semantic state
  for every pointer you write or modify.

- **Annotate Nullable Exceptions (`absl_nullable`)**: When null is a valid and
  expected value at the point of use, you must explicitly annotate the pointer.

  - Raw Pointers: Use the format `T* absl_nullable` (e.g.,
    `Widget* absl_nullable widget`).
  - Smart Pointers: Use the format `absl_nullable std::unique_ptr<T>` (e.g.,
    `absl_nullable std::unique_ptr<Widget> widget`).

- **Enforce Strict Requirements (`absl_nonnull`)**: When a pointer must
  fundamentally never be null, enforce this contract explicitly.

  - Raw Pointers: Use the format `T* absl_nonnull` but for non-null raw pointers
    prefer to use a reference instead.
  - Smart Pointers: Use the format `absl_nonnull std::unique_ptr<T>`.

- **Leverage Machine Checking**: Treat these annotations as strict contracts.
  Rely on them to surface potential null-dereference errors or contract
  violations during static analysis and compilation.

[1]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/api/units/timestamp.h;drc=b95d90b78a3491ef8e8aa0640dd521515ec881ca;l=29
[2]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/rtc_base/thread.h;drc=1107751b6f11c35259a1c5c8a0f716e227b7e3b4;l=194
[3]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/api/task_queue/task_queue_base.h;drc=1107751b6f11c35259a1c5c8a0f716e227b7e3b4;l=25
[5]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/rtc_base/task_utils/pending_task_safety_flag.h;drc=86ee89f73e4f4799b3ebcc0b5c65837c9601fe6d;l=117
[6]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/rtc_base/synchronization/mutex.h;drc=0d3c09a8fe5f12dfbc9f1bcd5790fda8830624ec;l=40
[7]: https://abseil.io/docs/cpp/atomic_danger
