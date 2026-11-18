# Wiring up new WebExtensions WebIDL files into the Firefox build

## Add a new entry in `dom/bindings/Bindings.conf`

New WebIDL bindings should be added as new entries in `dom/bindings/Bindings.conf`. The new entry should be
added in alphabetic order and nearby the other WebExtensions API bindings already listed in this config file
(look for the `ExtensionBrowser` webidl definition and the other existing WebIDL bindings related to the
WebExtensions APIs):

```text
# WebExtension API
...
'ExtensionRuntime': {
  'headerFile': 'mozilla/extensions/ExtensionRuntime.h',
  'nativeType': 'mozilla::extensions::ExtensionRuntime',
},
```

:::{warning}
`mach build` will fail if the entries in `dom/bindings/Bindings.conf` are not in alphabetic order,
or if the `headerFile` referenced does not exist yet.
:::

## Add a new entry in `dom/webidl/moz.build`

The new `.webidl` file has to be also listed in "dom/webidl/moz.build", it should be added in

- the existing group of `WEBIDL_FILES` entries meant specifically for the WebExtensions API bindings
- or in the group of `PREPROCESSED_WEBIDL_FILES` entries meant specifically for the WebExtensions
  API bindings, **if the generated \`.webidl\` includes preprocessing macros** (e.g. when part of an API
  is not available in all builds, e.g. subset of APIs that are only available in Desktop builds).

```text
# WebExtensions API.
WEBIDL_FILES += [
  ...
  "ExtensionRuntime.webidl",
  ...
]

PREPROCESSED_WEBIDL_FILES += [
  ...
]
```

:::{warning}
The group of PREPROCESSED_WERBIDL_FILES meant to list WebExtensions APIs `.webidl` files
may not exist yet (one will be added right after the existing `WEBIDL_FILES` when the first
preprocessed `.webidl` will be added).
:::

## Add new entries in `toolkit/components/extensions/webidl-api/moz.build`

The new C++ files for the WebExtensions API binding needs to be added to `toolkit/components/extensions/webidl-api/moz.build`
to make them part of the build, The new `.cpp` file has to be added into the `UNIFIED_SOURCES` group
where the other WebIDL bindings are being listed. Similarly, the new `.h` counterpart has to be added to
`EXPORTS.mozilla.extensions` (which ensures that the header file will be placed into the path set earlier
in `dom/bindings/Bindings.conf`):

```text
# WebExtensions API namespaces.
UNIFIED_SOURCES += [
  ...
  "ExtensionRuntime.cpp",
  ...
]

EXPORTS.mozilla.extensions += [
  ...
  "ExtensionRuntime.h",
  ...
]
```

## Wiring up the new API into `dom/webidl/ExtensionBrowser.webidl`

To make the new WebIDL bindings part of the `browser` global, a new attribute has to be added to
`dom/webidl/ExtensionBrowser.webidl`:

```cpp
// `browser.runtime` API namespace.
[Replaceable, SameObject, BinaryName="GetExtensionRuntime",
 Func="mozilla::extensions::ExtensionRuntime::IsAllowed"]
readonly attribute ExtensionRuntime runtime;
```

:::{note}
`chrome` is defined as an alias of the `browser` global, and so by adding the new attribute
into ``` ExtensionBrowser` the same attribute will also be available in the ``chrome ``` global.
Unlike the "Privileged JS"-based WebExtensions API, the `chrome` and `browser` APIs are
exactly the same and a the async methods return a Promise if no callback has been passed
(similarly to Safari versions where the WebExtensions APIs are supported).
:::

The additional attribute added into `ExtensionBrowser.webidl` will require some addition to the `ExtensionBrowser`
C++ class as defined in `toolkit/components/extensions/webidl-api/ExtensionBrowser.h`:

- the definition of a new corresponding **public method** (by convention named `GetExtensionMyNamespace`)
- a `RefPtr` as a new **private data member named** (by convention named `mExtensionMyNamespace`)

```cpp
...
namespace extensions {

...
class ExtensionRuntime;
...

class ExtensionBrowser final : ... {
  ...
  RefPtr<ExtensionRuntime> mExtensionRuntime;
  ...

  public:
    ...
    ExtensionRuntime* GetExtensionRuntime();
}
...
```

And then in its `toolkit/components/extensions/webidl-api/ExtensionBrowser.cpp` counterpart:

- the implementation of the new public method
- the addition of the new private member data `RefPtr` in the `NS_IMPL_CYCLE_COLLECTION_UNLINK`
  and `NS_IMPL_CYCLE_COLLECTION_TRAVERSE` macros

```cpp
...
#include "mozilla/extensions/ExtensionRuntime.h"
...
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ExtensionBrowser)
  ...
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mExtensionRuntime)
  ...
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ExtensionBrowser)
  ...
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mExtensionRuntime)
  ...
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
...

ExtensionRuntime* ExtensionBrowser::GetExtensionRuntime() {
  if (!mExtensionRuntime) {
    mExtensionRuntime = new ExtensionRuntime(mGlobal, this);
  }

  return mExtensionRuntime
}
```

:::{warning}
Forgetting to add the new `RefPtr` into the cycle collection traverse and unlink macros
will not result in a build error, but it will result into a leak.

Make sure to don't forget to double-check these macros, especially if some tests are failing
because of detected shutdown leaks.
:::
