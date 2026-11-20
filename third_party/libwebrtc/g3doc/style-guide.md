<!-- go/cmark -->

<!--* freshness: {owner: 'danilchap' reviewed: '2026-05-15'} *-->

# WebRTC coding style guide

## General advice

Some older parts of the code violate the style guide in various ways. If making
large changes to such code, consider first cleaning it up in a separate CL.

WebRTC's policy about AI coding matches [Chromium's policy][ai-policy].

## C++

WebRTC follows the [Chromium C++ style guide][chr-style] and the
[Google C++ style guide][goog-style]. In cases where they conflict, the Chromium
style guide trumps the Google style guide, and the rules in this file trump them
both. In addition to style guides it is recommended to follow
[best practices][goog-best-practice] when applicable.

### C++ version

WebRTC is written in C++20, but with some restrictions:

- We only allow the subset of C++20 (language and library) that is not banned by
  Chromium; see the [list of banned C++ features in Chromium][chr-style-cpp].
- We only allow the subset of C++20 that is also valid C++23; otherwise, users
  would not be able to compile WebRTC in C++23 mode.

### Abseil

You may use a subset of the utilities provided by the [Abseil] library when
writing WebRTC C++ code; see the
[instructions on how to use Abseil in WebRTC](abseil-in-webrtc.md).

### <a name="h-cc-pairs"></a>`.h` and `.cc` files come in pairs

`.h` and `.cc` files should come in pairs, with the same name (except for the
file type suffix), in the same directory, in the same build target.

- If a declaration in `path/to/foo.h` has a definition in some `.cc` file, it
  should be in `path/to/foo.cc`.
- If a definition in `path/to/foo.cc` file has a declaration in some `.h` file,
  it should be in `path/to/foo.h`.
- Omit the `.cc` file if it would have been empty, but still list the `.h` file
  in a build target.
- Omit the `.h` file if it would have been empty. (This can happen with unit
  test `.cc` files, and with `.cc` files that define `main`.)

See also the
[examples and exceptions on how to treat `.h` and `.cc` files](style-guide/h-cc-pairs.md).

This makes the source code easier to navigate and organize, and precludes some
questionable build system practices such as having build targets that don't pull
in definitions for everything they declare.

### `TODO` comments

Follow the [Google styleguide for `TODO` comments][goog-style-todo]. TODOs
should have a syntax of the following form `TODO(<metadata,...>): <content>`
When referencing a WebRTC bug, prefer using the URL form (excluding the scheme
part):

```cpp
// TODO(bugs.webrtc.org/42224904): Delete when blocking bugs are resolved.
```

The short form used in commit messages, e.g. `webrtc:12345`, is discouraged.

### Deprecation

Annotate the declarations of deprecated functions and classes with the
[`[[deprecated]]` attribute][deprecated] to cause an error when they're used
inside WebRTC and a compiler warning when they're used by dependant projects.
Like so:

```cpp
[[deprecated("bugs.webrtc.org/12345")]]
std::pony PonyPlz(const std::pony_spec& ps);
```

Prefer [ABSL_DEPRECATE_AND_INLINE] to deprecate an inline function definition or
a type alias. This macro allows to automate inlining the functions's body or
replacing the type where it is used downstream. e.g.,

```cpp
ABSL_DEPRECATE_AND_INLINE() inline int OldFunc(int x) {
  return NewFunc(x, 0);
}
using OldTypeName ABSL_DEPRECATE_AND_INLINE() = NewTypeName;
```

NOTE 1: The annotation goes on the declaration in the `.h` file, not the
definition in the `.cc` file!

NOTE 2: In Chromium and WebRTC [ABSL_DEPRECATE_AND_INLINE] macro is patched not
to add \[[deprecated]\] attribute. That allows to use the macro before all usage
in Chromium and WebRTC are cleaned up to a non-deprecated variant.

NOTE 3: In order to use the \[[deprecated]\] function without getting errors,
for example in a unit test, do something like this:

```cpp
std::pony DEPRECATED_PonyPlz(const std::pony_spec& ps);
[[deprecated("bugs.webrtc.org/12345")]]
inline std::pony PonyPlz(const std::pony_spec& ps) {
  return DEPRECATED_PonyPlz(ps);
}
```

In other words, rename the existing function, and provide an inline wrapper
using the original name that calls it. That way, callers who are willing to call
it using the `DEPRECATED_`-prefixed name don't get the warning.

Alternatively, wrap the code with

```cpp
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  PonyPlz(...);
#pragma clang diagnostic pop
```

### std::span

std::span is allowed and encouraged in WebRTC.

When passing an array of values to a function, use `std::span` whenever
possible—that is, whenever you're not passing ownership of the array, and don't
allow the callee to change the array size.

For example,

| instead of                          | use                  |
| ----------------------------------- | -------------------- |
| `const std::vector<T>&`             | `std::span<const T>` |
| `const T* ptr, size_t num_elements` | `std::span<const T>` |
| `T* ptr, size_t num_elements`       | `std::span<T>`       |

See the [cpp reference][span] for more detailed docs.

std::span represents the same concept as base::span in Chromium and absl::Span
in Abseil. WebRTC can't use base::span from Chromium, and prefers std::span over
absl::Span.

<!-- TODO: bugs.webrtc.org/439801349 - Delete this note after 2027-03-04 -->

In the past WebRTC used own ArrayView type to represent a span, however that
type has been migrated to std::span.

### Strings

WebRTC uses std::string, with content assumed to be UTF-8. Note that this has to
be verified whenever accepting external input.

For concatenation of strings, use `webrtc::StrJoin` or `webrtc::StringBuilder`
directly.

For string views, use `absl::string_view`, not `std::string_view`. The former is
heavily used in webrtc, and there are platforms we export to where the two are
different. Eventual conversion will be easiest if usage is consistent. See
[issue 42225436](https://issues.webrtc.org/42225436) for details and current
status.

The following string building tools are NOT recommended:

- The + operator. See [String Concatenation and operator+][totw-3] for why not.
- `absl::StrCat`, `absl::StrAppend`, `absl::StrJoin`. These are optimized for
  speed, not code size, and have significant code size overhead.
- [`std::strcat`][std-strcat]. It is too easy to create buffer overflows.

### Smart pointers

The following smart pointer types are recommended:

- `std::unique_ptr` for all singly-owned objects
- `webrtc::scoped_refptr` for all objects with shared ownership

Use of `std::shared_ptr` is *not permitted*. It is banned in the Chromium style
guide (overriding the Google style guide). See the
[list of banned C++ library features in Chromium][chr-std-shared-ptr] for more
information.

In most cases, one will want to explicitly control lifetimes, and therefore use
`std::unique_ptr`, but in some cases, for instance where references have to
exist both from the API users and internally, with no way to invalidate pointers
held by the API user, `scoped_refptr` can be appropriate.

### `std::bind`

Don't use `std::bind`—there are pitfalls, and lambdas are almost as succinct and
already familiar to modern C++ programmers. See [Avoid std::bind][totw-108] for
more.

### `std::function`

`std::function` is allowed, but remember that it's not the right tool for every
occasion. Prefer to use interfaces when that makes sense.

- Prefer `webrtc::FunctionView` for cases where the callee will not save the
  function object.
- Prefer `absl::AnyInvocable` when you can accomplish the task by moving the
  callable instead of copying it.

### Forward declarations

WebRTC follows the
[Google C++ style guide on forward declarations][goog-forward-declarations]. In
summary: avoid using forward declarations where possible; just `#include` the
headers you need.

### RTTI and dynamic_cast

The Google style guide
[permits the use of dynamic_cast](https://google.github.io/styleguide/cppguide.html#Run-Time_Type_Information__RTTI_).

However, WebRTC does not permit it. WebRTC (and Chrome) is compiled with the
-fno-rtti flag, and the overhead of enabling RTTI it is on the order of 220
Kbytes (for Android Arm64).

Use static_cast and take your own steps to ensure type safety.

### Type casting and conversions

Prefer braced initialization `Type{value}` (e.g. `int{kMaxTemporalStreams}`)
over `static_cast<Type>(value)` when casting constants or values where narrowing
is not intended.

Braced initialization prevents accidental narrowing conversions at compile time
and is more concise. Reserve `static_cast` for cases where explicit value
conversions (such as narrowing or pointer conversions) are intended.

## C

There's a substantial chunk of legacy C code in WebRTC, and a lot of it is old
enough that it violates the parts of the C++ style guide that also applies to C
(naming etc.) for the simple reason that it pre-dates the use of the current C++
style guide for this code base. If making large changes to C code, consider
converting the whole thing to C++ first.

## Java

WebRTC follows the [Google Java style guide][goog-java-style].

## Objective-C and Objective-C++

WebRTC follows the
[Chromium Objective-C and Objective-C++ style guide][chr-objc-style].

## Python

WebRTC follows [Chromium's Python style][chr-py-style].

Chromium's Python style is now using PEP-8 and not all Python code has been
migrated. For this reason running presubmit on old WebRTC python script might
trigger failures. The failures can either be fixed are ignored by adding the
script to the [PYLINT_OLD_STYLE][old-style-lint] list.

## Build files

The WebRTC build files are written in [GN], and we follow the
[GN style guide][gn-style]. Additionally, there are some WebRTC-specific rules
below; in case of conflict, they trump the Chromium style guide.

### <a name="webrtc-gn-templates"></a>WebRTC-specific GN templates

As shown in the table below, for library targets (`source_set` and
`static_library`), you should default on using `rtc_library` (which abstracts
away the complexity of using the correct target type for Chromium component
builds).

The general rule is for library targets is:

1. Use `rtc_library`.
2. If the library is a header only target use `rtc_source_set`.
3. If you really need to generate a static library, use `rtc_static_library`
   (same for shared libraries, in such case use `rtc_shared_library`).

To ensure that all our [GN targets][gn-target] are built with the same
configuration, only use the following [GN templates][gn-templ].

| instead of       | use                                                                                  |
| ---------------- | ------------------------------------------------------------------------------------ |
| `executable`     | `rtc_executable`                                                                     |
| `shared_library` | `rtc_shared_library`                                                                 |
| `source_set`     | `rtc_library` (for header only libraries you may use `rtc_source_set`)               |
| `static_library` | `rtc_static_library` (use `rtc_library` unless you really need `rtc_static_library`) |
| `test`           | `rtc_cc_test`                                                                        |

### Target visibility and the native API

The [WebRTC-specific GN templates](#webrtc-gn-templates) declare build targets
whose default `visibility` allows all other targets in the WebRTC tree (and no
targets outside the tree) to depend on them.

Prefer to restrict the `visibility` if possible:

- If a target is used by only one or a tiny number of other targets, prefer to
  list them explicitly: `visibility = [ ":foo", ":bar" ]`
- If a target is used only by targets in the same `BUILD.gn` file:
  `visibility = [ ":*" ]`.

Setting `visibility = [ "*" ]` means that targets outside the WebRTC tree can
depend on this target; use this only for build targets whose headers are part of
the [native WebRTC API](../native-api.md).

### Conditional compilation with the C preprocessor

Avoid using the C preprocessor to conditionally enable or disable pieces of
code. But if you can't avoid it, introduce a GN variable, and then set a
preprocessor constant to either 0 or 1 in the build targets that need it:

```gn
if (apm_debug_dump) {
  defines = [ "WEBRTC_APM_DEBUG_DUMP=1" ]
} else {
  defines = [ "WEBRTC_APM_DEBUG_DUMP=0" ]
}
```

In the C, C++, or Objective-C files, use `#if` when testing the flag, not
`#ifdef` or `#if defined()`:

```c
#if WEBRTC_APM_DEBUG_DUMP
// One way.
#else
// Or another.
#endif
```

When combined with the `-Wundef` compiler option, this produces compile time
warnings if preprocessor symbols are misspelled, or used without corresponding
build rules to set them.

### Markdown formatting

Markdown formatting is enabled by adding .style.mdformat to the root, which
means that all changes to .md files will be reformatted by "git cl format".

[abseil]: https://abseil.io/about/
[absl_deprecate_and_inline]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/abseil-cpp/absl/base/macros.h?q=ABSL_DEPRECATE_AND_INLINE
[ai-policy]: https://chromium.googlesource.com/chromium/src.git/+/HEAD/agents/ai_policy.md
[chr-objc-style]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/objective-c/objective-c.md
[chr-py-style]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/python/python.md
[chr-std-shared-ptr]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md#shared-pointers-banned
[chr-style]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++.md
[chr-style-cpp]: https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md
[deprecated]: https://en.cppreference.com/w/cpp/language/attributes/deprecated
[gn]: https://gn.googlesource.com/gn/
[gn-style]: https://gn.googlesource.com/gn/+/HEAD/docs/style_guide.md
[gn-target]: https://gn.googlesource.com/gn/+/HEAD/docs/language.md#Targets
[gn-templ]: https://gn.googlesource.com/gn/+/HEAD/docs/language.md#Templates
[goog-best-practice]: https://abseil.io/tips
[goog-forward-declarations]: https://google.github.io/styleguide/cppguide.html#Forward_Declarations
[goog-java-style]: https://google.github.io/styleguide/javaguide.html
[goog-style]: https://google.github.io/styleguide/cppguide.html
[goog-style-todo]: https://google.github.io/styleguide/cppguide.html#TODO_Comments
[old-style-lint]: https://webrtc.googlesource.com/src/+/f70dc714a073397356f6ed866481da73f90f0b96/PRESUBMIT.py#48
[span]: https://en.cppreference.com/w/cpp/container/span.html
[std-strcat]: https://en.cppreference.com/w/cpp/string/byte/strcat.html
[totw-108]: https://abseil.io/tips/108
[totw-3]: https://abseil.io/tips/3
