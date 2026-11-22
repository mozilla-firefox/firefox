# Web IDL bindings

<div class="note">
<div class="admonition-title">Note</div>

Need to document the setup for indexed and named
setters/creators/deleters.

</div>

The [Web IDL](https://webidl.spec.whatwg.org/) bindings are generated
at build time based on two things: the actual Web IDL file and a
configuration file that lists some metadata about how the Web IDL should
be reflected into Gecko-internal code.

All Web IDL files should be placed in
[`dom/webidl`](https://searchfox.org/firefox-main/source/dom/webidl/)
and added to the list in the
[moz.build](https://searchfox.org/firefox-main/source/dom/webidl/moz.build)
file in that directory.

Note that if you're adding new interfaces, then the test at
`dom/tests/mochitest/general/test_interfaces.html` will most likely
fail. This is a signal that you need to get a review from a [DOM
peer](https://wiki.mozilla.org/Modules/All#Document_Object_Model).
Resist the urge to just add your interfaces to the
[moz.build](https://searchfox.org/firefox-main/source/dom/webidl/moz.build) list
without the review; it will just annoy the DOM peers and they'll make
you get the review anyway.

The configuration file, `dom/bindings/Bindings.conf`, is basically a
Python dict that maps interface names to information about the
interface, called a _descriptor_. There are all sorts of possible
options here that handle various edge cases, but most descriptors can be
very simple.

All the generated code is placed in the `mozilla::dom` namespace. For
each interface, a namespace whose name is the name of the interface with
`Binding` appended is created, and all the things pertaining to that
interface's binding go in that namespace.

There are various helper objects and utility methods in
[`dom/bindings`](https://searchfox.org/firefox-main/source/dom/bindings)
that are also all in the `mozilla::dom` namespace and whose headers
are all exported into `mozilla/dom` (placed in
`$OBJDIR/dist/include` by the build process).

## Adding Web IDL bindings to a class

To add a Web IDL binding for interface `MyInterface` to a class
`mozilla::dom::MyInterface` that's supposed to implement that
interface, you need to do the following:

1. If your interface doesn't inherit from any other interfaces, inherit
   from `nsWrapperCache` and hook up the class to the cycle collector
   so it will trace the wrapper cache properly. Note that you may not
   need to do this if your objects can only be created, never gotten
   from other objects. If you also inherit from `nsISupports`, make
   sure the `nsISupports` comes before the `nsWrapperCache` in your
   list of parent classes. If your interface _does_ inherit from another
   interface, just inherit from the C++ type that the other interface
   corresponds to.

   If you do need to hook up cycle collection, it will look like this in
   the common case of also inheriting from nsISupports:

   ``` cpp
   // Add strong pointers your class holds here. If you do, change to using
   // NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE.
   NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(MyClass)
   NS_IMPL_CYCLE_COLLECTING_ADDREF(MyClass)
   NS_IMPL_CYCLE_COLLECTING_RELEASE(MyClass)
   NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MyClass)
     NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
     NS_INTERFACE_MAP_ENTRY(nsISupports)
   NS_INTERFACE_MAP_END
   ```

1. If your class doesn't inherit from a class that implements
   `GetParentObject`, then add a function of that name that, for a
   given instance of your class, returns the same object every time
   (unless you write explicit code that handles your parent object
   changing by reparenting JS wrappers, as nodes do). The idea is that
   walking the `GetParentObject` chain will eventually get you to a
   Window, so that every Web IDL object is associated with a particular
   Window.
   For example, `nsINode::GetParentObject` returns the node's owner
   document. The return type of `GetParentObject` doesn't matter other
   than it must either singly-inherit from `nsISupports` or have a
   corresponding
   [`ToSupports`](https://searchfox.org/firefox-main/search?q=ToSupports&path=&case=true&regexp=false)
   method that can produce an `nsISupports` from it. (This allows the
   return value to be implicitly converted to a
   [`ParentObject`](https://searchfox.org/firefox-main/search?q=ParentObject&path=&case=true&regexp=false)
   instance by the compiler via one of that class's non-explicit
   constructors.)
   If many instances of `MyInterface` are expected to be created
   quickly, the return value of `GetParentObject` should itself inherit
   from `nsWrapperCache` for optimal performance. Returning null from
   `GetParentObject` is allowed in situations in which it's OK to
   associate the resulting object with a random global object for
   security purposes; this is not usually ok for things that are exposed
   to web content. Again, if you do not need wrapper caching you don't
   need to do this. The actual type returned from `GetParentObject`
   must be defined in a header included from your implementation header,
   so that this type's definition is visible to the binding code.

1. Add the Web IDL for `MyInterface` in `dom/webidl` and to the list
   in `dom/webidl/moz.build`.

1. Add an entry to `dom/bindings/Bindings.conf` that sets some basic
   information about the implementation of the interface. If the C++
   type is not `mozilla::dom::MyInterface`, you need to set the
   `'nativeType'` to the right type. If the type is not in the header
   file one gets by replacing '::' with '/' and appending '`.h`', then
   add a corresponding `'headerFile'` annotation (or
   [`HeaderFile`](#headerfile-path-to-headerfile-h) annotation to the .webidl file). If
   you don't have to set any annotations, then you don't need to add an
   entry either and the code generator will simply assume the defaults
   here. Note that using a `'headerFile'` annotation is generally not
   recommended. If you do use it, you will need to make sure your
   header includes all the headers needed for your [`Func`](#func-funcname)
   annotations.

1. Add external interface entries to `Bindings.conf` for whatever
   non-Web IDL interfaces your new interface has as arguments or return
   values.

1. Implement a `WrapObject` override on `mozilla::dom::MyInterface`
   that just calls through to
   `mozilla::dom::MyInterface_Binding::Wrap`. Note that if your C++
   type is implementing multiple distinct Web IDL interfaces, you need
   to choose which `mozilla::dom::MyInterface_Binding::Wrap` to call
   here. See `AudioContext::WrapObject`, for example.

1. Expose whatever methods the interface needs on
   `mozilla::dom::MyInterface`. These can be inline, virtual, have any
   calling convention, and so forth, as long as they have the right
   argument types and return types. You can see an example of what the
   function declarations should look like by running
   `mach webidl-example MyInterface`. This will produce two files in
   `dom/bindings` in your objdir: `MyInterface-example.h` and
   `MyInterface-example.cpp`, which show a basic implementation of the
   interface using a class that inherits from `nsISupports` and has a
   wrapper cache.

See this [sample patch that migrates window.performance.\* to Web IDL
bindings](https://github.com/mozilla-firefox/firefox/commit/a3b31d18c5c80e600f97f1ad03e092102304eb27).

<div class="note"><div class="admonition-title">Note</div>

If your object can only be reflected into JS by creating it, not by
retrieving it from somewhere, you can skip steps 1 and 2 above and
instead add `'wrapperCache': False` to your descriptor. You will
need to flag the functions that return your object as
[`[NewObject]`](https://webidl.spec.whatwg.org/#NewObject) in
the Web IDL. If your object is not refcounted then the return value of
functions that return it should return a UniquePtr.

</div>

(c-reflections-of-webidl-constructs)=

## C++ reflections of Web IDL constructs

### C++ reflections of Web IDL operations (methods)

A Web IDL operation is turned into a method call on the underlying C++
object. The return type and argument types are determined [as described
below](#c-reflections-of-webidl-constructs). In addition to those, all [methods that are
allowed to throw](#throws-getterthrows-setterthrows) will get an `ErrorResult&` argument
appended to their argument list. Non-static methods that use certain
Web IDL types like `any` or `object` will get a `JSContext*`
argument prepended to the argument list. Static methods will be passed a
[`const GlobalObject&`](#globalobject) for the relevant global and
can get a `JSContext*` by calling `Context()` on it.

The name of the C++ method is simply the name of the Web IDL operation
with the first letter converted to uppercase.

Web IDL overloads are turned into C++ overloads: they simply call C++
methods with the same name and different signatures.

For example, this Web IDL:

``` webidl
interface MyInterface
{
  undefined doSomething(long number);
  double doSomething(MyInterface? otherInstance);

  [Throws]
  MyInterface doSomethingElse(optional long maybeNumber);
  [Throws]
  undefined doSomethingElse(MyInterface otherInstance);

  undefined doTheOther(any something);

  undefined doYetAnotherThing(optional boolean actuallyDoIt = false);

  static undefined staticOperation(any arg);
};
```

will require these method declarations:

``` cpp
class MyClass
{
  void DoSomething(int32_t a number);
  double DoSomething(MyClass* aOtherInstance);

  already_AddRefed<MyInterface> DoSomethingElse(Optional<int32_t> aMaybeNumber,
                                                ErrorResult& rv);
  void DoSomethingElse(MyClass& aOtherInstance, ErrorResult& rv);

  void DoTheOther(JSContext* cx, JS::Handle<JS::Value> aSomething);

  void DoYetAnotherThing(bool aActuallyDoIt);

  static void StaticOperation(const GlobalObject& aGlobal, JS::Handle<JS::Value> aSomething);
}
```

### C++ reflections of Web IDL attributes

A Web IDL attribute is turned into a pair of method calls for the getter
and setter on the underlying C++ object. A readonly attribute only has a
getter and no setter.

The getter's name is the name of the attribute with the first letter
converted to uppercase. This has `Get` prepended to it if any of these
conditions hold:

1. The type of the attribute is nullable.
1. The getter can throw.
1. The return value of the attribute is returned via an out parameter in
   the C++.

The method signature for the getter looks just like an operation with no
arguments and the attribute's type as the return type.

The setter's name is `Set` followed by the name of the attribute with
the first letter converted to uppercase. The method signature looks just
like an operation with an undefined return value and a single argument
whose type is the attribute's type.

### C++ reflections of Web IDL constructors

A Web IDL constructor is turned into a static class method named
`Constructor`. The arguments of this method will be the arguments of
the Web IDL constructor, with a
[`const GlobalObject&`](#globalobject) for the relevant global
prepended. For the non-worker case, the global is typically the inner
window for the DOM Window the constructor function is attached to. If a
`JSContext*` is also needed due to some of the argument types, it will
come after the global. The return value of the constructor for
`MyInterface` is exactly the same as that of a method returning an
instance of `MyInterface`. Constructors are always allowed to throw.

For example, this IDL:

``` webidl
interface MyInterface {
  constructor();
  constructor(unsigned long someNumber);
};
```

will require the following declarations in `MyClass`:

``` cpp
class MyClass {
  // Various nsISupports stuff or whatnot
  static
  already_AddRefed<MyClass> Constructor(const GlobalObject& aGlobal,
                                        ErrorResult& rv);
  static
  already_AddRefed<MyClass> Constructor(const GlobalObject& aGlobal,
                                        uint32_t aSomeNumber,
                                        ErrorResult& rv);
};
```

### C++ reflections of Web IDL types

The exact C++ representation for Web IDL types can depend on the precise
way that they're being used (e.g., return values, arguments, and
sequence or dictionary members might all have different
representations).

Unless stated otherwise, a type only has one representation. Also,
unless stated otherwise, nullable types are represented by wrapping
[`Nullable<>`](#nullable-t) around the base type.

In all cases, optional arguments which do not have a default value are
represented by wrapping [`const Optional<>&`](#optional-t) around the
representation of the argument type. If the argument type is a C++
reference, it will also become a [`NonNull<>`](#nonnull-t) around the
actual type of the object in the process. Optional arguments which do
have a default value are just represented by the argument type itself,
set to the default value if the argument was not in fact passed in.

Variadic Web IDL arguments are treated as a
[`const Sequence<>&`](#sequence-t) around the actual argument type.

Here's a table, see the specific sections below for more details and
explanations.

| Web IDL Type                | Argument Type                     | Return Type                                                                                                      | Dictionary/Member Type                 |
| -------------------------- | --------------------------------- | ---------------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| any                        | `JS::Handle<JS::Value>`           | `JS::MutableHandle<JS::Value>`                                                                                   | `JS::Value`                            |
| boolean                    | `bool`                            | `bool`                                                                                                           | `bool`                                 |
| byte                       | `int8_t`                          | `int8_t`                                                                                                         | `int8_t`                               |
| ByteString                 | `const nsACString&`               | `nsCString&` _(outparam)_<br>`nsACString&` _(outparam)_                                                          | `nsCString`                            |
| Date                       |                                   |                                                                                                                  | `mozilla::dom::Date`                   |
| DOMString                  | `const nsAString&`                | [`mozilla::dom::DOMString&`](#domstring-c) _(outparam)_<br>`nsAString&` _(outparam)_<br>`nsString&` _(outparam)_ | `nsString`                             |
| UTF8String                 | `const nsACString&` _(outparam)_  | `nsACString&`                                                                                                    | `nsCString`                            |
| double                     | `double`                          | `double`                                                                                                         | `double`                               |
| float                      | `float`                           | `float`                                                                                                          | `float`                                |
| interface:<br>non-nullable | `Foo&`                            | `already_addRefed<Foo>`                                                                                          | [`OwningNonNull<Foo>`](#owningnonnull-t) |
| interface:<br>nullable     | `Foo*`                            | `already_addRefed<Foo>`<br>`Foo*`                                                                                | `RefPtr<Foo>`                          |
| long                       | `int32_t`                         | `int32_t`                                                                                                        | `int32_t`                              |
| long long                  | `int64_t`                         | `int64_t`                                                                                                        | `int64_t`                              |
| object                     | `JS::Handle<JSObject*>`           | `JS::MutableHandle<JSObject*>`                                                                                   | `JSObject*`                            |
| octet                      | `uint8_t`                         | `uint8_t`                                                                                                        | `uint8_t`                              |
| sequence                   | [`const Sequence<T>&`](#sequence-t) | `nsTArray<T>&` _(outparam)_                                                                                      |                                        |
| short                      | `int16_t`                         | `int16_t`                                                                                                        | `int16_t`                              |
| unrestricted double        | `double`                          | `double`                                                                                                         | `double`                               |
| unrestricted float         | `float`                           | `float`                                                                                                          | `float`                                |
| unsigned long              | `uint32_t`                        | `uint32_t`                                                                                                       | `uint32_t`                             |
| unsigned long long         | `uint64_t`                        | `uint64_t`                                                                                                       | `uint64_t`                             |
| unsigned short             | `uint16_t`                        | `uint16_t`                                                                                                       | `uint16_t`                             |
| USVString                  | `const nsAString&`                | [`mozilla::dom::DOMString&`](#domstring-c) _(outparam)_<br>`nsAString&` _(outparam)_<br>`nsString&` _(outparam)_ | `nsString`                             |

#### `any`

`any` is represented in three different ways, depending on use:

- `any` arguments become `JS::Handle<JS::Value>`. They will be in
  the compartment of the passed-in JSContext.
- `any` return values become a `JS::MutableHandle<JS::Value>` out
  param appended to the argument list. This comes after all IDL
  arguments, but before the `ErrorResult&`, if any, for the method.
  The return value is allowed to be in any compartment; bindings will
  wrap it into the context compartment as needed.
- `any` dictionary members and sequence elements become
  `JS::Value`. The dictionary members and sequence elements are
  guaranteed to be marked by whomever puts the sequence or dictionary
  on the stack, using `SequenceRooter` and `DictionaryRooter`.

Methods using `any` always get a `JSContext*` argument.

For example, this Web IDL:

``` webidl
interface Test {
  attribute any myAttr;
  any myMethod(any arg1, sequence<any> arg2, optional any arg3);
};
```

will correspond to these C++ function declarations:

``` cpp
void MyAttr(JSContext* cx, JS::MutableHandle<JS::Value> retval);
void SetMyAttr(JSContext* cx, JS::Handle<JS::Value> value);
void MyMethod(JSContext* cx, JS::Handle<JS::Value> arg1,
              const Sequence<JS::Value>& arg2,
              const Optional<JS::Handle<JS::Value>>& arg3,
              JS::MutableHandle<JS::Value> retval);
```

#### `boolean`

The `boolean` Web IDL type is represented as a C++ `bool`.

For example, this Web IDL:

``` webidl
interface Test {
  attribute boolean myAttr;
  boolean myMethod(optional boolean arg);
};
```

will correspond to these C++ function declarations:

``` cpp
bool MyAttr();
void SetMyAttr(bool value);
bool MyMethod(const Optional<bool>& arg);
```

#### Integer types

Integer Web IDL types are mapped to the corresponding C99 stdint types.

For example, this Web IDL:

``` webidl
interface Test {
  attribute short myAttr;
  long long myMethod(unsigned long? arg);
};
```

will correspond to these C++ function declarations:

``` cpp
int16_t MyAttr();
void SetMyAttr(int16_t value);
int64_t MyMethod(const Nullable<uint32_t>& arg);
```

#### Floating point types

Floating point Web IDL types are mapped to the C++ type of the same
name. So `float` and `unrestricted float` become a C++ `float`,
while `double` and `unrestricted double` become a C++ `double`.

For example, this Web IDL:

``` webidl
interface Test {
  float myAttr;
  double myMethod(unrestricted double? arg);
};
```

will correspond to these C++ function declarations:

``` cpp
float MyAttr();
void SetMyAttr(float value);
double MyMethod(const Nullable<double>& arg);
```

(domstring-c)=

#### `DOMString`

Strings are reflected in three different ways, depending on use:

- String arguments become `const nsAString&`.
- String return values become a
  [`mozilla::dom::DOMString&`](#domstring-c) out param
  appended to the argument list. This comes after all IDL arguments,
  but before the `ErrorResult&`, if any, for the method. Note that
  this allows callees to declare their methods as taking an
  `nsAString&` or `nsString&` if desired.
- Strings in sequences, dictionaries, owning unions, and variadic
  arguments become `nsString`.

Nullable strings are represented by the same types as non-nullable ones,
but the string will return true for `DOMStringIsNull()`. Returning
null as a string value can be done using `SetDOMStringToNull` on the
out param if it's an `nsAString` or calling `SetNull()` on a
`DOMString`.

For example, this Web IDL:

``` webidl
interface Test {
  DOMString myAttr;
  [Throws]
  DOMString myMethod(sequence<DOMString> arg1, DOMString? arg2, optional DOMString arg3);
};
```

will correspond to these C++ function declarations:

``` cpp
void GetMyAttr(nsString& retval);
void SetMyAttr(const nsAString& value);
void MyMethod(const Sequence<nsString>& arg1, const nsAString& arg2,
              const Optional<nsAString>& arg3, nsString& retval, ErrorResult& rv);
```

#### `USVString`

`USVString` is reflected just like `DOMString`.

#### `UTF8String`

`UTF8String` is a string with guaranteed-valid UTF-8 contents. It is
not a standard in the Web IDL spec, but its observables are the same as
those of `USVString`.

It is a good fit for when the specification allows a `USVString`, but
you want to process the string as UTF-8 rather than UTF-16.

It is reflected in three different ways, depending on use:

- Arguments become `const nsACString&`.
- Return values become an `nsACString&` out param appended to the
  argument list. This comes after all IDL arguments, but before the
  `ErrorResult&`, if any, for the method.
- In sequences, dictionaries owning unions, and variadic arguments it
  becomes `nsCString`.

Nullable `UTF8String`s are represented by the same types as
non-nullable ones, but the string will return true for `IsVoid()`.
Returning null as a string value can be done using `SetIsVoid()` on
the out param.

#### `ByteString`

`ByteString` is reflected in three different ways, depending on use:

- `ByteString` arguments become `const nsACString&`.
- `ByteString` return values become an `nsCString&` out param
  appended to the argument list. This comes after all IDL arguments,
  but before the `ErrorResult&`, if any, for the method.
- `ByteString` in sequences, dictionaries, owning unions, and
  variadic arguments becomes `nsCString`.

Nullable `ByteString` are represented by the same types as
non-nullable ones, but the string will return true for `IsVoid()`.
Returning null as a string value can be done using `SetIsVoid()` on
the out param.

#### `object`

`object` is represented in three different ways, depending on use:

- `object` arguments become `JS::Handle<JSObject*>`. They will be
  in the compartment of the passed-in JSContext.
- `object` return values become a `JS::MutableHandle<JSObject*>`
  out param appended to the argument list. This comes after all IDL
  arguments, but before the `ErrorResult&`, if any, for the method.
  The return value is allowed to be in any compartment; bindings will
  wrap it into the context compartment as needed.
- `object` dictionary members and sequence elements become
  `JSObject*`. The dictionary members and sequence elements are
  guaranteed to be marked by whoever puts the sequence or dictionary on
  the stack, using `SequenceRooter` and `DictionaryRooter`.

Methods using `object` always get a `JSContext*` argument.

For example, this Web IDL:

``` webidl
interface Test {
  object myAttr;
  object myMethod(object arg1, object? arg2, sequence<object> arg3, optional object arg4,
                  optional object? arg5);
};
```

will correspond to these C++ function declarations:

``` cpp
void GetMyAttr(JSContext* cx, JS::MutableHandle<JSObject*> retval);
void SetMyAttr(JSContext* cx, JS::Handle<JSObject*> value);
void MyMethod(JSContext* cx, JS::Handle<JSObject*> arg1, JS::Handle<JSObject*> arg2,
              const Sequence<JSObject*>& arg3,
              const Optional<JS::Handle<JSObject*>>& arg4,
              const Optional<JS::Handle<JSObject*>>& arg5,
              JS::MutableHandle<JSObject*> retval);
```

#### Interface types

There are four kinds of interface types in the Web IDL bindings. Callback
interfaces are used to represent script objects that browser code can
call into. External interfaces are used to represent objects that have
not been converted to the Web IDL bindings yet. Web IDL interfaces are
used to represent Web IDL binding objects. "SpiderMonkey" interfaces are
used to represent objects that are implemented natively by the
JavaScript engine (e.g., typed arrays).

##### Callback interfaces

Callback interfaces are represented in C++ as objects inheriting from
[`mozilla::dom::CallbackInterface`](#callbackinterface), whose
name, in the `mozilla::dom` namespace, matches the name of the
callback interface in the Web IDL. The exact representation depends on
how the type is being used.

- Nullable arguments become `Foo*`.
- Non-nullable arguments become `Foo&`.
- Return values become `already_AddRefed<Foo>` or `Foo*` as
  desired. The pointer form is preferred because it results in faster
  code, but it should only be used if the return value was not addrefed
  (and so it can only be used if the return value is kept alive by the
  callee until at least the binding method has returned).
- Web IDL callback interfaces in sequences, dictionaries, owning unions,
  and variadic arguments are represented by `RefPtr<Foo>` if nullable
  and [`OwningNonNull<Foo>`](#owningnonnull-t) otherwise.

If the interface is a single-operation interface, the object exposes two
methods that both invoke the same underlying JS callable. The first of
these methods allows the caller to pass in a `this` object, while the
second defaults to `undefined` as the `this` value. In either case,
the `this` value is only used if the callback interface is implemented
by a JS callable. If it's implemented by an object with a property whose
name matches the operation, the object itself is always used as
`this`.

If the interface is not a single-operation interface, it just exposes a
single method for every IDL method/getter/setter.

The signatures of the methods correspond to the signatures for throwing
IDL methods/getters/setters with an additional trailing
`mozilla::dom::CallbackObject::ExceptionHandling aExceptionHandling`
argument, defaulting to `eReportExceptions`.
If `aReportExceptions` is set to `eReportExceptions`, the methods
will report JS exceptions before returning. If `aReportExceptions` is
set to `eRethrowExceptions`, JS exceptions will be stashed in the
`ErrorResult` and will be reported when the stack unwinds to wherever
the `ErrorResult` was set up.

For example, this Web IDL:

``` webidl
callback interface MyCallback {
  attribute long someNumber;
  short someMethod(DOMString someString);
};
callback interface MyOtherCallback {
  // single-operation interface
  short doSomething(Node someNode);
};
interface MyInterface {
  attribute MyCallback foo;
  attribute MyCallback? bar;
};
```

will lead to these C++ class declarations in the `mozilla::dom`
namespace:

``` cpp
class MyCallback : public CallbackInterface
{
  int32_t GetSomeNumber(ErrorResult& rv, ExceptionHandling aExceptionHandling = eReportExceptions);
  void SetSomeNumber(int32_t arg, ErrorResult& rv,
                     ExceptionHandling aExceptionHandling = eReportExceptions);
  int16_t SomeMethod(const nsAString& someString, ErrorResult& rv,
                     ExceptionHandling aExceptionHandling = eReportExceptions);
};

class MyOtherCallback : public CallbackInterface
{
public:
  int16_t
  DoSomething(nsINode& someNode, ErrorResult& rv,
              ExceptionHandling aExceptionHandling = eReportExceptions);

  template<typename T>
  int16_t
  DoSomething(const T& thisObj, nsINode& someNode, ErrorResult& rv,
              ExceptionHandling aExceptionHandling = eReportExceptions);
};
```

and these C++ function declarations on the implementation of
`MyInterface`:

``` cpp
already_AddRefed<MyCallback> GetFoo();
void SetFoo(MyCallback&);
already_AddRefed<MyCallback> GetBar();
void SetBar(MyCallback*);
```

A consumer of MyCallback would be able to use it like this:

``` cpp
void
SomeClass::DoSomethingWithCallback(MyCallback& aCallback)
{
  ErrorResult rv;
  int32_t number = aCallback.GetSomeNumber(rv);
  if (rv.Failed()) {
    // The error has already been reported to the JS console; you can handle
    // things however you want here.
    return;
  }

  // For some reason we want to catch and rethrow exceptions from SetSomeNumber, say.
  aCallback.SetSomeNumber(2*number, rv, eRethrowExceptions);
  if (rv.Failed()) {
    // The exception is now stored on rv. This code MUST report
    // it usefully; otherwise it will assert.
  }
}
```

##### External interfaces

External interfaces are represented in C++ as objects that XPConnect
knows how to unwrap to. This can mean XPCOM interfaces (whether declared
in XPIDL or not) or it can mean some type that there's a castable native
unwrapping function for. The C++ type to be used should be the
`nativeType` listed for the external interface in the
[`Bindings.conf`](#bindings-conf-details) file. The exact representation
depends on how the type is being used.

- Arguments become `nsIFoo*`.
- Return values can be `already_AddRefed<nsIFoo>` or `nsIFoo*` as
  desired. The pointer form is preferred because it results in faster
  code, but it should only be used if the return value was not addrefed
  (and so it can only be used if the return value is kept alive by the
  callee until at least the binding method has returned).
- External interfaces in sequences, dictionaries, owning unions, and
  variadic arguments are represented by `RefPtr<nsIFoo>`.

##### Web IDL interfaces

Web IDL interfaces are represented in C++ as C++ classes. The class
involved must either be refcounted or must be explicitly annotated in
`Bindings.conf` as being directly owned by the JS object. If the class
inherits from `nsISupports`, then the canonical `nsISupports` must
be on the primary inheritance chain of the object. If the interface has
a parent interface, the C++ class corresponding to the parent must be on
the primary inheritance chain of the object. This guarantees that a
`void*` can be stored in the JSObject which can then be
`reinterpret_cast` to any of the classes that correspond to interfaces
the object implements. The C++ type to be used should be the
`nativeType` listed for the interface in the
[`Bindings.conf`](#bindings-conf-details) file, or
`mozilla::dom::InterfaceName` if none is listed. The exact
representation depends on how the type is being used.

- Nullable arguments become `Foo*`.
- Non-nullable arguments become `Foo&`.
- Return values become `already_AddRefed<Foo>` or `Foo*` as
  desired. The pointer form is preferred because it results in faster
  code, but it should only be used if the return value was not addrefed
  (and so it can only be used if the return value is kept alive by the
  callee until at least the binding method has returned).
- Web IDL interfaces in sequences, dictionaries, owning unions, and
  variadic arguments are represented by `RefPtr<Foo>` if nullable and
  [`OwningNonNull<Foo>`](#owningnonnull-t) otherwise.

For example, this Web IDL:

``` webidl
interface MyInterface {
  attribute MyInterface myAttr;
  undefined passNullable(MyInterface? arg);
  MyInterface? doSomething(sequence<MyInterface> arg);
  MyInterface doTheOther(sequence<MyInterface?> arg);
  readonly attribute MyInterface? nullableAttr;
  readonly attribute MyInterface someOtherAttr;
  readonly attribute MyInterface someYetOtherAttr;
};
```

Would correspond to these C++ function declarations:

``` cpp
already_AddRefed<MyClass> MyAttr();
void SetMyAttr(MyClass& value);
void PassNullable(MyClass* arg);
already_AddRefed<MyClass> doSomething(const Sequence<OwningNonNull<MyClass>>& arg);
already_AddRefed<MyClass> doTheOther(const Sequence<RefPtr<MyClass>>& arg);
already_Addrefed<MyClass> GetNullableAttr();
MyClass* SomeOtherAttr();
MyClass* SomeYetOtherAttr(); // Don't have to return already_AddRefed!
```

##### "SpiderMonkey" interfaces

Typed array, array buffer, and array buffer view arguments are
represented by the objects in [`TypedArray.h`](#typed-arrays-arraybuffers-array-buffer-views). For
example, this Web IDL:

``` webidl
interface Test {
  undefined passTypedArrayBuffer(ArrayBuffer arg);
  undefined passTypedArray(ArrayBufferView arg);
  undefined passInt16Array(Int16Array? arg);
}
```

will correspond to these C++ function declarations:

``` cpp
void PassTypedArrayBuffer(const ArrayBuffer& arg);
void PassTypedArray(const ArrayBufferView& arg);
void PassInt16Array(const Nullable<Int16Array>& arg);
```

Typed array return values become a `JS::MutableHandle<JSObject*>` out
param appended to the argument list. This comes after all IDL arguments,
but before the `ErrorResult&`, if any, for the method. The return
value is allowed to be in any compartment; bindings will wrap it into
the context compartment as needed.

Typed arrays store a `JSObject*` and hence need to be rooted
properly. On-stack typed arrays can be declared as
`RootedTypedArray<TypedArrayType>` (e.g.
`RootedTypedArray<Int16Array>`). Typed arrays on the heap need to be
traced.

#### Dictionary types

A dictionary argument is represented by a const reference to a struct
whose name is the dictionary name in the `mozilla::dom` namespace.
The struct has one member for each of the dictionary's members with the
same name except the first letter uppercased and prefixed with "m". The
members that are required or have default values have types as described
under the corresponding Web IDL type in this document. The members that
are not required and don't have default values have those types wrapped
in [`Optional<>`](#optional-t).

Dictionary return values are represented by an out parameter whose type
is a non-const reference to the struct described above, with all the
members that have default values preinitialized to those default values.

Note that optional dictionary arguments are always forced to have a
default value of an empty dictionary by the IDL parser and code
generator, so dictionary arguments are never wrapped in `Optional<>`.

If necessary, dictionaries can be directly initialized from a
`JS::Value` in C++ code by invoking their `Init()` method. Consumers
doing this should declare their dictionary as
`RootedDictionary<DictionaryName>`. When this is done, passing in a
null `JSContext*` is allowed if the passed-in `JS::Value` is
`JS::NullValue()`. Likewise, a dictionary struct can be converted to a
`JS::Value` in C++ by calling `ToJSValue` with the dictionary as the
second argument. If `Init()` or `ToJSValue()` returns false, they
will generally set a pending exception on the JSContext; reporting those
is the responsibility of the caller.

For example, this Web IDL:

``` text
dictionary Dict {
  long foo = 5;
  DOMString bar;
};

interface Test {
  undefined initSomething(optional Dict arg = {});
};
```

will correspond to this C++ function declaration:

``` cpp
void InitSomething(const Dict& arg);
```

and the `Dict` struct will look like this:

``` cpp
struct Dict {
  bool Init(JSContext* aCx, JS::Handle<JS::Value> aVal, const char* aSourceDescription = "value");

  Optional<nsString> mBar;
  int32_t mFoo;
}
```

Note that the dictionary members are sorted in the struct in
alphabetical order.

##### API for working with dictionaries

There are a few useful methods found on dictionaries and dictionary
members that you can use to quickly determine useful things.

- **member.WasPassed()** - as the name suggests, was a particular
  member passed?
  (e.g., `if (arg.foo.WasPassed() { /* do nice things!*/ }`)
- **dictionary.IsAnyMemberPresent()** - great for checking if you need
  to do anything.
  (e.g., `if (!arg.IsAnyMemberPresent()) return; // nothing to do`)
- **member.Value()** - getting the actual data/value of a member that
  was passed.
  (e.g., `mBar.Assign(args.mBar.value())`)

Example implementation using all of the above:

``` cpp
void
MyInterface::InitSomething(const Dict& aArg){
  if (!aArg.IsAnyMemberPresent()) {
    return; // nothing to do!
  }
  if (aArg.mBar.WasPassed() && !mBar.Equals(aArg.mBar.value())) {
    mBar.Assign(aArg.mBar.Value());
  }
}
```

#### Enumeration types

Web IDL enumeration types are represented as C++ enum classes. The values
of the C++ enum are named by taking the strings in the Web IDL
enumeration, replacing all non-alphanumerics with underscores, and
uppercasing the first letter, with a special case for the empty string,
which becomes the value `_empty`.

For a Web IDL enum named `MyEnum`, the C++ enum is named `MyEnum` and
placed in the `mozilla::dom` namespace, while the values are placed in
the `mozilla::dom::MyEnum` namespace.

The type of the enum class is automatically selected to be the smallest
unsigned integer type that can hold all the values. In practice, this
is always uint8_t, because Web IDL enums tend to not have more than 255
values.

For example, this Web IDL:

``` webidl
enum MyEnum {
  "something",
  "something-else",
  "",
  "another"
};
```

would lead to this C++ enum declaration:

``` cpp
enum class MyEnum : uint8_t {
  Something,
  Something_else,
  _empty,
  Another
};
```

`mozilla::dom::GetEnumString` is a templated helper function declared in
[`BindingUtils.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingUtils.h)
and exported to `mozilla/dom/BindingUtils.h` that can be used to convert an enum
value to its corresponding string value. It returns a `const nsCString&`
containing the string value.

`mozilla::dom::StringToEnum` is a templated helper function in
[`BindingUtils.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingUtils.h)
and exported to `mozilla/dom/BindingUtils.h` that can be used to convert a
string to the corresponding enum value. It needs to be supplied with the enum
class as a template argument, and returns a `mozilla::Maybe<Enum>`. If the string
value passed to it as an argument is not one of the string values for the enum
then it returns `mozilla::Nothing()`, else it returns the right enum value in
the `mozilla::Maybe`.

`mozilla::dom::WebIDLEnumSerializer` is a templated alias in
[`BindingIPCUtils.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingIPCUtils.h)
exported to `mozilla/dom/BindingIPCUtils.h` to implement an IPC serializer with
the right validation for WebIDL enums. It uses a
`mozilla::MaxContinuousEnumValue` that is generated for every WebIDL enum to
implement the validation.

`mozilla::dom::MakeWebIDLEnumeratedRange` is a templated helper function in
[`BindingUtils.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingUtils.h)
and exported to `mozilla/dom/BindingUtils.h` that can be used to create a
`mozilla::EnumeratedRange` for a WebIDL enum.

#### Callback function types

Callback functions are represented as an object, inheriting from
[`mozilla::dom::CallbackFunction`](#callbackfunction), whose name,
in the `mozilla::dom` namespace, matches the name of the callback
function in the Web IDL. If the type is nullable, a pointer is passed in;
otherwise a reference is passed in.

The object exposes two `Call` methods, which both invoke the
underlying JS callable. The first `Call` method has the same signature
as a throwing method declared just like the callback function, with an
additional trailing `mozilla::dom::CallbackObject::ExceptionHandling aExceptionHandling`
argument, defaulting to `eReportExceptions`,
and calling it will invoke the callable with `undefined` as the
`this` value. The second `Call` method allows passing in an explicit
`this` value as the first argument. This second call method is a
template on the type of the first argument, so the `this` value can be
passed in in whatever form is most convenient, as long as it's either a
type that can be wrapped by XPConnect or a Web IDL interface type.

If `aReportExceptions` is set to `eReportExceptions`, the `Call`
methods will report JS exceptions before returning. If
`aReportExceptions` is set to `eRethrowExceptions`, JS exceptions
will be stashed in the `ErrorResult` and will be reported when the
stack unwinds to wherever the `ErrorResult` was set up.

For example, this Web IDL:

``` webidl
callback MyCallback = long (MyInterface arg1, boolean arg2);
interface MyInterface {
  attribute MyCallback foo;
  attribute MyCallback? bar;
};
```

will lead to this C++ class declaration, in the `mozilla::dom`
namespace:

``` cpp
class MyCallback : public CallbackFunction
{
public:
  int32_t
  Call(MyInterface& arg1, bool arg2, ErrorResult& rv,
       ExceptionHandling aExceptionHandling = eReportExceptions);

  template<typename T>
  int32_t
  Call(const T& thisObj, MyInterface& arg1, bool arg2, ErrorResult& rv,
       ExceptionHandling aExceptionHandling = eReportExceptions);
};
```

and these C++ function declarations in the `MyInterface` class:

``` cpp
already_AddRefed<MyCallback> GetFoo();
void SetFoo(MyCallback&);
already_AddRefed<MyCallback> GetBar();
void SetBar(MyCallback*);
```

A consumer of MyCallback would be able to use it like this:

``` cpp
void
SomeClass::DoSomethingWithCallback(MyCallback& aCallback, MyInterface& aInterfaceInstance)
{
  ErrorResult rv;
  int32_t number = aCallback.Call(aInterfaceInstance, false, rv);
  if (rv.Failed()) {
    // The error has already been reported to the JS console; you can handle
    // things however you want here.
    return;
  }

  // Now for some reason we want to catch and rethrow exceptions from the callback,
  // and use "this" as the this value for the call to JS.
  number = aCallback.Call(*this, true, rv, eRethrowExceptions);
  if (rv.Failed()) {
    // The exception is now stored on rv.  This code MUST report
    // it usefully; otherwise it will assert.
  }
}
```

#### Sequences

Sequence arguments are represented by
[`const Sequence<T>&`](#sequence-t), where `T` depends on the type
of elements in the Web IDL sequence.

Sequence return values are represented by an `nsTArray<T>` out param
appended to the argument list, where `T` is the return type for the
elements of the Web IDL sequence. This comes after all IDL arguments, but
before the `ErrorResult&`, if any, for the method.

#### Arrays

IDL array objects are not supported yet. The spec on these is likely to
change drastically anyway.

#### Union types

Union types are reflected as a struct in the `mozilla::dom` namespace.
There are two kinds of union structs: one kind does not keep its members
alive (is "non-owning"), and the other does (is "owning"). Const
references to non-owning unions are used for plain arguments. Owning
unions are used in dictionaries, sequences, and for variadic arguments.
Union return values become a non-const owning union out param. The name
of the struct is the concatenation of the names of the types in the
union, with "Or" inserted between them, and for an owning struct
"Owning" prepended. So for example, this IDL:

``` text
undefined passUnion((object or long) arg);
(object or long) receiveUnion();
undefined passSequenceOfUnions(sequence<(object or long)> arg);
undefined passOtherUnion((HTMLDivElement or ArrayBuffer or EventInit) arg);
```

would correspond to these C++ function declarations:

``` cpp
void PassUnion(const ObjectOrLong& aArg);
void ReceiveUnion(OwningObjectObjectOrLong& aArg);
void PassSequenceOfUnions(const Sequence<OwningObjectOrLong>& aArg);
void PassOtherUnion(const HTMLDivElementOrArrayBufferOrEventInit& aArg);
```

Union structs expose accessors to test whether they're of a given type
and to get hold of the data of that type. They also expose setters that
set the union as being of a particular type and return a reference to
the union's internal storage where that type could be stored. The one
exception is the `object` type, which uses a somewhat different form
of setter where the `JSObject*` is passed in directly. For example,
`ObjectOrLong` would have the following methods:

``` cpp
bool IsObject() const;
JSObject* GetAsObject() const;
void SetToObject(JSContext*, JSObject*);
bool IsLong() const;
int32_t GetAsLong() const;
int32_t& SetAsLong()
```

Owning unions used on the stack should be declared as a
`RootedUnion<UnionType>`, for example,
`RootedUnion<OwningObjectOrLong>`.

#### `Date`

Web IDL `Date` types are represented by a `mozilla::dom::Date`
struct.

### C++ reflections of Web IDL declarations

Web IDL declarations (maplike/setlike/iterable) are turned into a set of
properties and functions on the interface they are declared on. Each has
a different set of helper functions it comes with. In addition, for
iterable, there are requirements for C++ function implementation by the
interface developer.

#### Maplike

Example Interface:

``` webidl
interface StringToLongMap {
  maplike<DOMString, long>;
};
```

The bindings for this interface will generate the storage structure for
the map, as well as helper functions for accessing that structure from
C++. The generated C++ API will look as follows:

``` cpp
namespace StringToLongMapBinding {
namespace MaplikeHelpers {
void Clear(mozilla::dom::StringToLongMap* self, ErrorResult& aRv);
bool Delete(mozilla::dom::StringToLongMap* self, const nsAString& aKey, ErrorResult& aRv);
bool Has(mozilla::dom::StringToLongMap* self, const nsAString& aKey, ErrorResult& aRv);
void Set(mozilla::dom::StringToLongMap* self, const nsAString& aKey, int32_t aValue, ErrorResult& aRv);
} // namespace MaplikeHelpers
} // namespace StringToLongMapBindings
```

#### Setlike

Example Interface:

``` webidl
interface StringSet {
  setlike<DOMString>;
};
```

The bindings for this interface will generate the storage structure for
the set, as well as helper functions for accessing that structure from
c++. The generated C++ API will look as follows:

``` cpp
namespace StringSetBinding {
namespace SetlikeHelpers {
void Clear(mozilla::dom::StringSet* self, ErrorResult& aRv);
bool Delete(mozilla::dom::StringSet* self, const nsAString& aKey, ErrorResult& aRv);
bool Has(mozilla::dom::StringSet* self, const nsAString& aKey, ErrorResult& aRv);
void Add(mozilla::dom::StringSet* self, const nsAString& aKey, ErrorResult& aRv);
} // namespace SetlikeHelpers
}
```

#### Iterable

Unlike maplike and setlike, iterable does not have any C++ helpers, as
the structure backing the iterable data for the interface is left up to
the developer. With that in mind, the generated iterable bindings expect
the wrapper object to provide certain methods for the interface to
access.

Iterable interfaces have different requirements, based on if they are
single or pair value iterators.

Example Interface for a single value iterator:

``` webidl
interface LongIterable {
  iterable<long>;
  getter long(unsigned long index);
  readonly attribute unsigned long length;
};
```

For single value iterator interfaces, we treat the interface as an
[indexed getter](#indexed-getters), as required by the spec. See the
[indexed getter implementation section](#indexed-getters) for more
information on building this kind of structure.

Example Interface for a pair value iterator:

``` webidl
interface StringAndLongIterable {
  iterable<DOMString, long>;
};
```

The bindings for this pair value iterator interface require the
following methods be implemented in the C++ object:

``` cpp
class StringAndLongIterable {
public:
  // Returns the number of items in the iterable storage
  size_t GetIterableLength();
  // Returns key of pair at aIndex in iterable storage
  nsAString& GetKeyAtIndex(uint32_t aIndex);
  // Returns value of pair at aIndex in iterable storage
  uint32_t& GetValueAtIndex(uint32_t aIndex);
}
```

### Stringifiers

Named stringifiers operations in Web IDL will just invoke the
corresponding C++ method.

Anonymous stringifiers in Web IDL will invoke the C++ method called
`Stringify`. So, for example, given this IDL:

``` webidl
interface FirstInterface {
  stringifier;
};

interface SecondInterface {
  stringifier DOMString getStringRepresentation();
};
```

the corresponding C++ would be:

``` cpp
class FirstInterface {
public:
  void Stringify(nsAString& aResult);
};

class SecondInterface {
public:
  void GetStringRepresentation(nsAString& aResult);
};
```

### Legacy Callers

Only anonymous legacy callers are supported, and will invoke the C++
method called `LegacyCall`. This will be passed the JS "this" value as
the first argument, then the arguments to the actual operation. A
`JSContext` will be passed if any of the operation arguments need it.
So for example, given this IDL:

``` webidl
interface InterfaceWithCall {
  legacycaller long (float arg);
};
```

the corresponding C++ would be:

``` cpp
class InterfaceWithCall {
public:
  int32_t LegacyCall(JS::Handle<JS::Value> aThisVal, float aArgument);
};
```

### Named getters

If the interface has a named getter, the binding will expect several
methods on the C++ implementation:

- A `NamedGetter` method. This takes a property name and returns
  whatever type the named getter is declared to return. It also has a
  boolean out param for whether a property with that name should exist
  at all.
- A `NameIsEnumerable` method. This takes a property name and
  returns a boolean that indicates whether the property is enumerable.
- A `GetSupportedNames` method. This takes an unsigned integer which
  corresponds to the flags passed to the `iterate` proxy trap and
  returns a list of property names. For implementations of this method,
  the important flags is `JSITER_HIDDEN`. If that flag is set, the
  call needs to return all supported property names. If it's not set,
  the call needs to return only the enumerable ones.

The `NameIsEnumerable` and `GetSupportedNames` methods need to agree
on which names are and are not enumerable. The `NamedGetter` and
`GetSupportedNames` methods need to agree on which names are
supported.

So for example, given this IDL:

``` webidl
interface InterfaceWithNamedGetter {
  getter long(DOMString arg);
};
```

the corresponding C++ would be:

``` cpp
class InterfaceWithNamedGetter
{
public:
  int32_t NamedGetter(const nsAString& aName, bool& aFound);
  bool NameIsEnumerable(const nsAString& aName);
  undefined GetSupportedNames(unsigned aFlags, nsTArray<nsString>& aNames);
};
```

### Indexed getters

If the interface has a indexed getter, the binding will expect the
following methods on the C++ implementation:

- A `IndexedGetter` method. This takes an integer index value and
  returns whatever type the indexed getter is declared to return. It
  also has a boolean out param for whether a property with that index
  should exist at all. The implementation must set this out param
  correctly. The return value is guaranteed to be ignored if the out
  param is set to false.

So for example, given this IDL:

``` webidl
interface InterfaceWithIndexedGetter {
  getter long(unsigned long index);
  readonly attribute unsigned long length;
};
```

the corresponding C++ would be:

``` cpp
class InterfaceWithIndexedGetter
{
public:
  uint32_t Length() const;
  int32_t IndexedGetter(uint32_t aIndex, bool& aFound) const;
};
```

## Throwing exceptions from Web IDL methods, getters, and setters

Web IDL methods, getters, and setters that are [explicitly marked as
allowed to throw](#throws-getterthrows-setterthrows) have an `ErrorResult&` argument as their
last argument. To throw an exception, simply call `Throw()` on the
`ErrorResult&` and return from your C++ back into the binding code.

In cases when the specification calls for throwing a `TypeError`, you
should use `ErrorResult::ThrowTypeError()` instead of calling
`Throw()`.

## Custom extended attributes

Our Web IDL parser and code generator recognize several extended
attributes that are not present in the Web IDL spec.

### `[AllowLarge]`

Can be specified on buffer source types (`ArrayBuffer`, `ArrayBufferView`,
typed arrays) to skip the default 2 GB size check. This allows
incremental migration of consumers to support buffers larger than 2 GB.
Can be combined with `[AllowShared]`. On the C++ side, use
`ProcessData<true>(...)` or `ProcessFixedData<true>(...)` to access the
data.

### `[Alias=propName]`

This extended attribute can be specified on a method and indicates that
another property with the specified name will also appear on the
interface prototype object and will have the same Function object value
as the property for the method. For example:

``` webidl
interface MyInterface {
  [Alias=performSomething] undefined doSomething();
};
```

`MyInterface.prototype.performSomething` will have the same Function
object value as `MyInterface.prototype.doSomething`.

Multiple `[Alias]` extended attribute can be used on the one method.
`[Alias]` cannot be used on a static method, nor on methods on a
global interface (such as `Window`).

Aside from regular property names, the name of an alias can be
[Symbol.iterator](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Symbol/iterator).
This is specified by writing `[Alias="@@iterator"]`.

### `[BindingAlias=propName]`

This extended attribute can be specified on an attribute and indicates
that another property with the specified name will also appear on the
interface prototype object and will call the same underlying C++
implementation for the getter and setter. This is more efficient than
using the same `BinaryName` for both attributes, because it shares the
binding glue code between them. The properties still have separate
getter/setter functions in JavaScript, so from the point of view of web
consumers it's as if you actually had two separate attribute
declarations on your interface. For example:

``` webidl
interface MyInterface {
  [BindingAlias=otherAttr] readonly attribute boolean attr;
};
```

`MyInterface.prototype.otherAttr` and `MyInterface.prototype.attr`
will both exist, have separate getter/setter functions, but call the
same binding glue code and implementation function on the objects
implementing `MyInterface`.

Multiple `[BindingAlias]` extended attributes can be used on a single
attribute.

### `[BindingTemplate=(name, type, value)]`

This extended attribute can be specified on an attribute, and causes the getter
and setter for this attribute to forward to a common generated implementation,
shared with all other attributes that have a `[BindingTemplate]` with the same
value for the `name` argument. The `TemplatedAttributes` dictionary in
Bindings.conf needs to contain a definition for the template with the name
`name`. The argument `type::value` will be passed when calling the common
generated implementation.

This is aimed at very specialized use cases where an interface has a
large number of attributes that all have the same type, and for which we have a
native implementation that's common to all these attributes, and typically uses
some id based on the attribute's name in the implementation. All the attributes
that use the same template need to mostly have the same extended attributes,
except form a small number that are allowed to differ (`[BindingTemplate]`,
`[BindingAlias]`, `[Pure]`, [`Pref`] and [`Func`], and the annotations for
whether the getter and setter throws exceptions).

### `[ChromeOnly]`

This extended attribute can be specified on any method, attribute, or
constant on an interface or on an interface as a whole. It can also be
specified on dictionary members.

Interface members flagged as `[ChromeOnly]` are only exposed in chrome
Windows (and in particular, are not exposed to webpages). From the point
of view of web content, it's as if the interface member were not there
at all. These members _are_ exposed to chrome script working with a
content object via Xrays.

If specified on an interface as a whole, this functions like
[`[Func]`](#func-funcname) except that the binding code will automatically
check whether the caller script has the system principal (is chrome or a
worker started from a chrome page) instead of calling into the C++
implementation to determine whether to expose the interface object on
the global. This means that accessing a content global via Xrays will
show `[ChromeOnly]` interface objects on it.

If specified on a dictionary member, then the dictionary member will
only appear to exist in system-privileged code.

This extended attribute can be specified together with `[Func]`, and
`[Pref]`. If more than one of these is specified, all conditions will
need to test true for the interface or interface member to be exposed.

### `[Pref=prefname]`

This extended attribute can be specified on any method, attribute, or
constant on an interface or on an interface as a whole. It can also be
specified on dictionary members. It takes a value, which must be the
name of a boolean preference exposed from `StaticPrefs`. The
`StaticPrefs` function that will be called is calculated from the
value of the extended attribute, with dots replaced by underscores
(`StaticPrefs::my_pref_name()` in the example below).

If specified on an interface member, the interface member involved is
only exposed if the preference is set to `true`. An example of how
this can be used:

``` webidl
interface MyInterface {
  attribute long alwaysHere;
  [Pref="my.pref.name"] attribute long onlyHereIfEnabled;
};
```

If specified on an interface as a whole, this functions like
[`[Func]`](#func-funcname) except that the binding will check the value of
the preference directly without calling into the C++ implementation of
the interface at all. This is useful when the enable check is simple and
it's desirable to keep the prefname with the Web IDL declaration.

If specified on a dictionary member, the web-observable behavior when
the pref is set to false will be as if the dictionary did not have a
member of that name defined. That means that on the JS side no
observable get of the property will happen. On the C++ side, the
behavior would be as if the passed-in object did not have a property
with the relevant name: the dictionary member would either be
`!Passed()` or have the default value if there is a default value.

> An example of how this can be used:

``` webidl
[Pref="my.pref.name"]
interface MyConditionalInterface {
};
```

This extended attribute can be specified together with `[ChromeOnly]`,
and `[Func]`. If more than one of these is specified, all conditions
will need to test true for the interface or interface member to be
exposed.

(func-funcname)=

### `[Func="funcname"]`

This extended attribute can be specified on any method, attribute, or
constant on an interface or on an interface as a whole. It can also be
specified on dictionary members. It takes a value, which must be the
name of a static function.

If specified on an interface member, the interface member involved is
only exposed if the specified function returns `true`. An example of
how this can be used:

``` webidl
interface MyInterface {
  attribute long alwaysHere;
  [Func="MyClass::StuffEnabled"] attribute long onlyHereIfEnabled;
};
```

The function is invoked with two arguments: the `JSContext` that the
operation is happening on and the `JSObject` for the global of the
object that the property will be defined on if the function returns
true. In particular, in the Xray case the `JSContext` is in the caller
compartment (typically chrome) but the `JSObject` is in the target
compartment (typically content). This allows the method implementation
to select which compartment it cares about in its checks.

The above IDL would also require the following C++:

``` cpp
class MyClass {
  static bool StuffEnabled(JSContext* cx, JSObject* obj);
};
```

If specified on an interface as a whole, then lookups for the interface
object for this interface on a DOM Window will only find it if the
specified function returns true. For objects that can only be created
via a constructor, this allows disabling the functionality altogether
and making it look like the feature is not implemented at all.

If specified on a dictionary member, the web-observable behavior when
the function returns false will be as if the dictionary did not have a
member of that name defined. That means that on the JS side no
observable get of the property will happen. On the C++ side, the
behavior would be as if the passed-in object did not have a property
with the relevant name: the dictionary member would either be
`!Passed()` or have the default value if there is a default value.

An example of how `[Func]` can be used:

``` webidl
[Func="MyClass::MyConditionalInterfaceEnabled"]
interface MyConditionalInterface {
};
```

In this case, the C++ function is passed a `JS::Handle<JSObject*>`. So
the C++ in this case would look like this:

``` cpp
class MyClass {
  static bool MyConditionalInterfaceEnabled(JSContext* cx, JS::Handle<JSObject*> obj);
};
```

Just like in the interface member case, the `JSContext` is in the
caller compartment but the `JSObject` is the actual object the
property would be defined on. In the Xray case that means obj is in the
target compartment (typically content) and `cx` is typically chrome.

This extended attribute can be specified together with `[ChromeOnly]`,
and `[Pref]`. If more than one of these is specified, all conditions
will need to test true for the interface or interface member to be
exposed.

Binding code will include the headers necessary for a `[Func]`, unless
the interface is using a non-default header file. If a non-default
header file is used, that header file needs to do any header inclusions
necessary for `[Func]` annotations.

### `[Throws]`, `[GetterThrows]`, `[SetterThrows]`

Used to flag methods or attributes as allowing the C++ callee to throw.
This causes the binding generator, and in many cases the JIT, to
generate extra code to handle possible exceptions. Possibly-throwing
methods and attributes get an `ErrorResult&` argument.

`[Throws]` applies to both methods and attributes; for attributes it
means both the getter and the setter can throw. `[GetterThrows]`
applies only to attributes. `[SetterThrows]` applies only to
non-readonly attributes.

For interfaces flagged with `[JSImplementation]`, all methods and
properties are assumed to be able to throw and do not need to be flagged
as throwing.

### `[DependsOn]`

Used for a method or attribute to indicate what the return value depends
on. Possible values are:

- `Everything`

  This value can't actually be specified explicitly; this is the
  default value you get when `[DependsOn]` is not specified. This
  means we don't know anything about the return value's dependencies
  and hence can't rearrange other code that might change values around
  the method or attribute.

- `DOMState`

  The return value depends on the state of the "DOM", by which we mean
  all objects specified via Web IDL. The return value is guaranteed to
  not depend on the state of the JS heap or other JS engine data
  structures, and is guaranteed to not change unless some function with
  [`[Affects=Everything]`](#affects) is executed.

- `DeviceState`

  The return value depends on the state of the device we're running on
  (e.g., the system clock). The return value is guaranteed to not be
  affected by any code running inside Gecko itself, but we might get a
  new value every time the method or getter is called even if no Gecko
  code ran between the calls.

- `Nothing`

  The return value is a constant that never changes. This value cannot
  be used on non-readonly attributes, since having a non-readonly
  attribute whose value never changes doesn't make sense.

Values other than `Everything`, when used in combination with
[`[Affects=Nothing]`](#affects), can used by the JIT to
perform loop-hoisting and common subexpression elimination on the return
values of IDL attributes and methods.

### `[Affects]`

Used for a method or attribute getter to indicate what sorts of state
can be affected when the function is called. Attribute setters are, for
now, assumed to affect everything. Possible values are:

- `Everything`

  This value can't actually be specified explicitly; this is the
  default value you get when `[Affects]` is not specified. This means
  that calling the method or getter might change any mutable state in
  the DOM or JS heap.

- `Nothing`

  Calling the method or getter will have no side-effects on either the
  DOM or the JS heap.

Methods and attribute getters with `[Affects=Nothing]` are allowed to
throw exceptions, as long as they do so deterministically. In the case
of methods, whether an exception is thrown is allowed to depend on the
arguments, as long as calling the method with the same arguments will
always either throw or not throw.

The `Nothing` value, when used with `[DependsOn]` values other than
`Everything`, can used by the JIT to perform loop-hoisting and common
subexpression elimination on the return values of IDL attributes and
methods, as well as code motion past DOM methods that might depend on
system state but have no side effects.

### `[Pure]`

This is an alias for `[Affects=Nothing, DependsOn=DOMState]`.
Attributes/methods flagged in this way promise that they will keep
returning the same value as long as nothing that has
`[Affects=Everything]` executes.

### `[Constant]`

This is an alias for `[Affects=Nothing, DependsOn=Nothing]`. Used to
flag readonly attributes or methods that could have been annotated with
`[Pure]` and also always return the same value. This should only be
used when it's absolutely guaranteed that the return value of the
attribute getter will always be the same from the JS engine's point of
view.

The spec's `[SameObject]` extended attribute is an alias for
`[Constant]`, but can only be applied to things returning objects,
whereas `[Constant]` can be used for any type of return value.

### `[NeedResolve]`

Used to flag interfaces which have a custom resolve hook. This
annotation will cause the `DoResolve` method to be called on the
underlying C++ class when a property lookup happens on the object. The
signature of this method is:
`bool DoResolve(JSContext*, JS::Handle<JSObject*>, JS::Handle<jsid>, JS::MutableHandle<JS::Value>)`.
Here the passed-in object is the object the property lookup is happening
on (which may be an Xray for the actual DOM object) and the jsid is the
property name. The value that the property should have is returned in
the `MutableHandle<Value>`, with `UndefinedValue()` indicating that
the property does not exist.

If this extended attribute is used, then the underlying C++ class must
also implement a method called `GetOwnPropertyNames` with the
signature
`void GetOwnPropertyNames(JSContext* aCx, nsTArray<nsString>& aNames, ErrorResult& aRv)`.
This method will be called by the JS engine's enumerate hook and must
provide a superset of all the property names that `DoResolve` might
resolve. Providing names that `DoResolve` won't actually resolve is
OK.

(headerfile-path-to-headerfile-h)=

### `[HeaderFile="path/to/headerfile.h"]`

Indicates where the implementation can be found. Similar to the
headerFile annotation in Bindings.conf. Just like headerFile in
Bindings.conf, should be avoided.

### `[JSImplementation="@mozilla.org/some-contractid;1"]`

Used on an interface to provide the contractid of the [JavaScript
component implementing the
interface](#implementing-webidl-using-javascript).

### `[StoreInSlot]`

Used to flag attributes that can be gotten very quickly from the JS
object by the JIT. Such attributes will have their getter called
immediately when the JS wrapper for the DOM object is created, and the
returned value will be stored directly on the JS object. Later gets of
the attribute will not call the C++ getter and instead use the cached
value. If the value returned by the attribute needs to change, the C++
code should call the `ClearCachedFooValue` method in the namespace of
the relevant binding, where `foo` is the name of the attribute. This
will immediately call the C++ getter and cache the value it returns, so
it needs a `JSContext` to work on. This extended attribute can only be
used on attributes whose getters are [`[Pure]`](#pure) or
[`[Constant]`](#constant) and which are not
[`[Throws]`](#throws-getterthrows-setterthrows) or [`[GetterThrows]`](#throws-getterthrows-setterthrows).

So for example, given this IDL:

``` webidl
interface MyInterface {
  [Pure, StoreInSlot] attribute long myAttribute;
};
```

the C++ implementation of MyInterface would clear the cached value by
calling
`mozilla::dom::MyInterface_Binding::ClearCachedMyAttributeValue(cx, this)`.
This function will return false on error and the caller is responsible
for handling any JSAPI exception that is set by the failure.

If the attribute is not readonly, setting it will automatically clear
the cached value and reget it again before the setter returns.

### `[Cached]`

Used to flag attributes that, when their getter is called, will cache
the returned value on the JS object. This can be used to implement
attributes whose value is a sequence or dictionary (which would
otherwise end up returning a new object each time and hence not be
allowed in Web IDL).

Unlike [`[StoreInSlot]`](#storeinslot) this does _not_ cause the
getter to be eagerly called at JS wrapper creation time; the caching is
lazy. `[Cached]` attributes must be [`[Pure]`](#pure) or
[`[Constant]`](#constant), because otherwise not calling the C++
getter would be observable, but are allowed to have throwing getters.
Their cached value can be cleared by calling the `ClearCachedFooValue`
method in the namespace of the relevant binding, where `foo` is the
name of the attribute. Unlike `[StoreInSlot]` attributes, doing so
will not immediately invoke the getter, so it does not need a
`JSContext`.

So for example, given this IDL:

``` webidl
interface MyInterface {
  [Pure, Cached] attribute long myAttribute;
};
```

the C++ implementation of MyInterface would clear the cached value by
calling
`mozilla::dom::MyInterface_Binding::ClearCachedMyAttributeValue(this)`.
JS-implemented Web IDL can clear the cached value by calling
`this.__DOM_IMPL__._clearCachedMyAttributeValue()`.

If the attribute is not readonly, setting it will automatically clear
the cached value.

### `[Frozen]`

Used to flag attributes that, when their getter is called, will call
[`Object.freeze`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Object/freeze)
on the return value before returning it. This extended attribute is only
allowed on attributes that return sequences, dictionaries and
`MozMap`, and corresponds to returning a frozen `Array` (for the
sequence case) or `Object` (for the other two cases).

### `[BinaryName]`

`[BinaryName]` can be specified on method or attribute to change the
C++ function name that will be used for the method or attribute. It
takes a single string argument, which is the name you wish the method or
attribute had instead of the one it actually has.

For example, given this IDL:

``` webidl
interface InterfaceWithRenamedThings {
  [BinaryName="renamedMethod"]
  undefined someMethod();
  [BinaryName="renamedAttribute"]
  attribute long someAttribute;
};
```

the corresponding C++ would be:

``` cpp
class InterfaceWithRenamedThings
{
public:
  void RenamedMethod();
  int32_t RenamedAttribute();
  void SetRenamedAttribute(int32_t);
};
```

### `[Deprecated="tag"]`

When deprecating an interface or method, the `[Deprecated]` annotation
causes the Web IDL compiler to insert code that generates deprecation
warnings. This annotation can be added to interface methods or
interfaces. Adding this to an interface causes a warning to be issued
the first time the object is constructed, or any static method on the
object is invoked.

The complete list of valid deprecation tags is maintained in
[nsDeprecatedOperationList.inc](https://searchfox.org/firefox-main/source/dom/base/nsDeprecatedOperationList.inc).
Each new tag requires that a localized string be defined, containing the
deprecation message to display.

### `[CrossOriginReadable]`

Used to flag an attribute that, when read, will not have the same-origin
constraint tested: it can be read from a context with a different
origin.

### `[CrossOriginWrite]`

Used to flag an attribute that, when written, will not have the
same-origin constraint tested: it can be written from a context with a
different origin.

### `[CrossOriginCallable]`

Used to flag a method that, when called, will not have the same-origin
constraint tested: it can be called from a context with a different
origin.

### `[SecureContext]`

We implement the [standard extended
attribute](https://webidl.spec.whatwg.org/#SecureContext) with a few
details specific to Gecko:

- System principals are considered secure.
- An extension poking at non-secured DOM objects will see APIs marked
  with `[SecureContext]`.
- XPConnect sandboxes don't see `[SecureContext]` APIs, except if
  they're created with `isSecureContext: true`.

### `[NeedsSubjectPrincipal]`, `[GetterNeedsSubjectPrincipal]`, `[SetterNeedsSubjectPrincipal]`

Used to flag a method or an attribute that needs to know the subject
principal. This principal will be passed as argument. The argument
will be a `nsIPrincipal&` because a subject principal is always
available.

`[NeedsSubjectPrincipal]` applies to both methods and attributes; for
attributes it means both the getter and the setter need a subject
principal. `[GetterNeedsSubjectPrincipal]` applies only to attributes.
`[SetterNeedsSubjectPrincipal]` applies only to non-readonly
attributes.

These attributes may also be constrained to non-system principals using
`[{Getter,Setter,}NeedsSubjectPrincipal=NonSystem]`. This changes the argument
type to `nsIPrincipal*`, and passes `nullptr` when called with a system
principal.

### `[NeedsCallerType]`

Used to flag a method or an attribute that needs to know the caller
type, in the `mozilla::dom::CallerType` sense. This can be safely
used for APIs exposed in workers; there it will indicate whether the
worker involved is a `ChromeWorker` or not. At the moment the only
possible caller types are `System` (representing system-principal
callers) and `NonSystem`.

### `[GenerateInit]`

When set on a dictionary it will add two `Init` methods to the generated C++
class with the following signatures:

``` cpp
bool Init(BindingCallContext& cx, JS::Handle<JS::Value> val, const char* sourceDescription="Value", bool passedToJSImpl=false);
bool Init(JSContext* cx_, JS::Handle<JS::Value> val, const char* sourceDescription="Value", bool passedToJSImpl=false);
```

These methods will initialize the dictionary from `val` by following WebIDL's
[JavaScript type mapping](https://webidl.spec.whatwg.org/#js-dictionary).

### `[GenerateInitFromJSON]`

When set on a dictionary it will add `Init` methods to the generated C++
class with the following signatures:

``` cpp
bool Init(const nsAString& aJSON);
bool Init(const nsACString& aJSON);
```

This extended attribute will only have an effect if all of the types of the
dictionary's members are representable in JSON (they are a string type, a
primitive type that's not an unrestricted float/double, a void type, or a
sequence, union, dictionary or record containing these types).

The method is expected to be called with a JSON string as input. The JSON string
will be parsed into a JavaScript value, and then the dictionary is initialized
with this value by following WebIDL's
[JavaScript type mapping](https://webidl.spec.whatwg.org/#js-dictionary).

Note: As a side-effect of how this is implemented it will also add the two
`Init` methods that would be added by a [`[GenerateInit]`](#generateinit)
extended attribute.

### `[GenerateToJSON]`

When set on a dictionary it will add `ToJSON` methods to the generated C++
class with the following signatures:

``` cpp
bool ToJSON(nsAString& aJSON);
bool ToJSON(nsACString& aJSON);
```

The method will generate a JSON representation of the dictionary members' values
in `aJSON` by converting the dictionary to a JavaScript object by following
WebIDL's [JavaScript type mapping](https://webidl.spec.whatwg.org/#js-dictionary)
and then converting that object to a JSON string.

The same restrictions on types applies as on
[`[GenerateInitFromJSON]`](#generateinitfromjson).

Note: As a side-effect of how this is implemented it will also add the
`ToObjectInternal` method that would be added by a
[`[GenerateConversionToJS]`](#generateconversiontojs) extended attribute.

### `[GenerateConversionToJS]`

When set on a dictionary it will add a `ToObjectInternal` method to the
generated C++ class with the following signature:

``` cpp
bool ToObjectInternal(JSContext* cx, JS::MutableHandle<JS::Value> rval);
```

The method will create a JavaScript object by following WebIDL's
[JavaScript type mapping](https://webidl.spec.whatwg.org/#js-dictionary).

### `[GenerateEqualityOperator]`

When set on a dictionary it will add an equality operator to the generated C++
class.

This is only allowed on dictionaries who only have members (own or inherited)
with string, primitive or enum types.

### `[Unsorted]`

When set on a dictionary the dictionary's members will not be sorted in
lexicographic order (which is specified by WebIDL).

This should only ever be used on internal APIs that are not exposed to the Web!

### `[ReflectedHTMLAttributeReturningFrozenArray]`

Used to flag a HTML reflected IDL attribute as having a `FrozenArray<T>?` type,
where `T` is either `Element` or an interface that inherits from `Element`. This
should only be used to implement the algorithms for that kind of reflected IDL
attributes.

When this attribute's getter is called, it will cache the JS reflection of the
returned value on the JS object. The C++ getter will be passed an additional
`bool*` argument before the result argument. If this argument is not `null` then
the implementation is supposed to set the `bool` that this argument is pointing
to to whether the
[attr-associated elements](https://html.spec.whatwg.org/#attr-associated-elements)
and the
[cached attr-associated elements](https://html.spec.whatwg.org/#cached-attr-associated-elements)
are equal. If it is not `null`, and the getter sets the pointee to `true` then
the cached JS value will be returned, and the result value from the C++ getter
will be ignored (so there is no need to set the result's value). If it is
`null`, or the getter sets the pointee to `false`, then the cached value will be
set to the JS reflection of the result value from the C++ getter, and that JS
value will then be returned.

Note that this will not cause the JIT to directly get the cached value from the
slot (as `[StoreInSlot]` or `[Cached]` would). The setter will also not clear
the cached value from the slot.

For example, this IDL:

``` webidl
interface Element {
  [Frozen, ReflectedHTMLAttributeReturningFrozenArray]
  attribute sequence<Element>? reflectedHTMLAttribute;
};
```

will require the following declarations in `Element`:

``` cpp
class Element {
  // …
  void GetReflectedHTMLAttribute(
    bool* aUseCachedValue, Nullable<nsTArray<RefPtr<Element>>>& aResult);
  void SetReflectedHTMLAttribute(
    const Nullable<Sequence<OwningNonNull<Element>>>& aValue);
};
```

## Helper objects

The C++ side of the bindings uses a number of helper objects.

(nullable-t)=

### `Nullable<T>`

`Nullable<>` is a struct declared in
[`Nullable.h`](https://searchfox.org/firefox-main/source/dom/bindings/Nullable.h)
and exported to `mozilla/dom/Nullable.h` that is used to represent
nullable values of types that don't have a natural way to represent
null.

`Nullable<T>` has an `IsNull()` getter that returns whether null is
represented and a `Value()` getter that returns a `const T&` and can
be used to get the value when it's not null.

`Nullable<T>` has a `SetNull()` setter that sets it as representing
null and two setters that can be used to set it to a value:
`void SetValue(T)` (for setting it to a given value) and
`T& SetValue()` for directly modifying the underlying `T&`.

(optional-t)=

### `Optional<T>`

`Optional<>` is a struct declared in
[`BindingDeclarations.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingDeclarations.h)
and exported to `mozilla/dom/BindingDeclarations.h` that is used to
represent optional arguments and dictionary members, but only those that
have no default value.

`Optional<T>` has a `WasPassed()` getter that returns true if a
value is available. In that case, the `Value()` getter can be used to
get a `const T&` for the value.

(nonnull-t)=

### `NonNull<T>`

`NonNull<T>` is a struct declared in
[`BindingUtils.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingUtils.h)
and exported to `mozilla/dom/BindingUtils.h` that is used to represent
non-null C++ objects. It has a conversion operator that produces `T&`.

(owningnonnull-t)=

### `OwningNonNull<T>`

`OwningNonNull<T>` is a struct declared in
[`OwningNonNull.h`](https://searchfox.org/firefox-main/source/xpcom/base/OwningNonNull.h)
and exported to `mozilla/OwningNonNull.h` that is used to represent
non-null C++ objects and holds a strong reference to them. It has a
conversion operator that produces `T&`.

### Typed arrays, arraybuffers, array buffer views

`TypedArray.h` is exported to `mozilla/dom/TypedArray.h` and exposes
structs that correspond to the various typed array types, as well as
`ArrayBuffer` and `ArrayBufferView`, all in the `mozilla::dom`
namespace. Each struct has a `Data()` method that returns a pointer to
the relevant type (`uint8_t` for `ArrayBuffer` and
`ArrayBufferView`) and a `Length()` method that returns the length
in units of `*Data()`. So for example, `Int32Array` has a `Data()`
returning `int32_t*` and a `Length()` that returns the number of
32-bit ints in the array.

By default, the generated bindings reject typed arrays, `ArrayBuffer`,
and `ArrayBufferView` objects larger than 2 GB. To allow larger buffers,
use the [`[AllowLarge]`](#allowlarge) extended attribute.

(sequence-t)=

### `Sequence<T>`

`Sequence<>` is a type declared in
[`BindingDeclarations.h`](https://searchfox.org/firefox-main/source/dom/bindings/BindingDeclarations.h)
and exported to `mozilla/dom/BindingDeclarations.h` that is used to
represent sequence arguments. It's some kind of typed array, but which
exact kind is opaque to consumers. This allows the binding code to
change the exact definition (e.g., to use auto arrays of different sizes
and so forth) without having to update all the callees.

### `CallbackFunction`

`CallbackFunction` is a type declared in
[CallbackFunction.h](https://searchfox.org/firefox-main/source/dom/bindings/CallbackFunction.h)
and exported to `mozilla/dom/CallbackFunction.h` that is used as a
common base class for all the generated callback function
representations. This class inherits from `nsISupports`, and consumers
must make sure to cycle-collect it, since it keeps JS objects alive.

### `CallbackInterface`

`CallbackInterface` is a type declared in
[CallbackInterface.h](https://searchfox.org/firefox-main/source/dom/bindings/CallbackInterface.h)
and exported to `mozilla/dom/CallbackInterface.h` that is used as a
common base class for all the generated callback interface
representations. This class inherits from `nsISupports`, and consumers
must make sure to cycle-collect it, since it keeps JS objects alive.

### `DOMString`

`DOMString` is a class declared in
[BindingDeclarations.h](https://searchfox.org/firefox-main/source/dom/bindings/BindingDeclarations.h)
and exported to `mozilla/dom/BindingDeclarations.h` that is used for
Web IDL `DOMString` return values. It has a conversion operator to
`nsString&` so that it can be passed to methods that take that type or
`nsAString&`, but callees that care about performance, have an
`StringBuffer` available, and promise to hold on to the
`StringBuffer` at least until the binding code comes off the stack
can also take a `DOMString` directly for their string return value and
call its `SetStringBuffer` method with the `StringBuffer` and its
length. This allows the binding code to avoid extra reference-counting
of the string buffer in many cases, and allows it to take a faster
codepath even if it does end up having to addref the `StringBuffer`.

### `GlobalObject`

`GlobalObject` is a class declared in
[BindingDeclarations.h](https://searchfox.org/firefox-main/source/dom/bindings/BindingDeclarations.h)
and exported to `mozilla/dom/BindingDeclarations.h` that is used to
represent the global object for static attributes and operations
(including constructors). It has a `Get()` method that returns the
`JSObject*` for the global and a `GetAsSupports()` method that
returns an `nsISupports*` for the global on the main thread, if such
is available. It also has a `Context()` method that returns the
`JSContext*` the call is happening on. A caveat: the compartment of
the `JSContext` may not match the compartment of the global!

### `Date`

`Date` is a class declared in
[BindingDeclarations.h](https://searchfox.org/firefox-main/source/dom/bindings/BindingDeclarations.h)
and exported to `mozilla/dom/BindingDeclarations.h` that is used to
represent Web IDL Dates. It has a `TimeStamp()` method returning a
double which represents a number of milliseconds since the epoch, as
well as `SetTimeStamp()` methods that can be used to initialize it
with a double timestamp or a JS `Date` object. It also has a
`ToDateObject()` method that can be used to create a new JS `Date`.

### `ErrorResult`

`ErrorResult` is a class declared in
[ErrorResult.h](https://searchfox.org/firefox-main/source/dom/bindings/ErrorResult.h)
and exported to `mozilla/ErrorResult.h` that is used to represent
exceptions in Web IDL bindings. This has the following methods:

- `Throw`: allows throwing an `nsresult`. The `nsresult` must be
  a failure code.
- `ThrowTypeError`: allows throwing a `TypeError` with the given
  error message. The list of allowed `TypeError`s and corresponding
  messages is in
  [`dom/bindings/Errors.msg`](https://searchfox.org/firefox-main/source/dom/bindings/Errors.msg).
- `ThrowJSException`: allows throwing a preexisting JS exception
  value. However, the `MightThrowJSException()` method must be called
  before any such exceptions are thrown (even if no exception is
  thrown).
- `Failed`: checks whether an exception has been thrown on this
  `ErrorResult`.
- `ErrorCode`: returns a failure `nsresult` representing (perhaps
  incompletely) the state of this `ErrorResult`.
- `operator=`: takes an `nsresult` and acts like `Throw` if the
  result is an error code, and like a no-op otherwise (unless an
  exception has already been thrown, in which case it asserts). This
  should only be used for legacy code that has nsresult everywhere; we
  would like to get rid of this operator at some point.

## Events

Simple `Event` interfaces can be automatically generated by adding the
interface file to GENERATED_EVENTS_WEBIDL_FILES in the
appropriate dom/webidl/moz.build file. You can also take a simple
generated C++ file pair and use it to build a more complex event (i.e.,
one that has methods).

### Event handler attributes

A lot of interfaces define event handler attributes, like:

``` webidl
attribute EventHandler onthingchange;
```

If you need to implement an event handler attribute for an interface, in
the definition (header file), you use the handy
"IMPL_EVENT_HANDLER" macro:

``` cpp
IMPL_EVENT_HANDLER(onthingchange);
```

The "onthingchange" needs to be added to the StaticAtoms.py file:

``` py
Atom("onthingchange", "onthingchange")
```

The actual implementation (.cpp) for firing the event would then look
something like:

``` cpp
nsresult
MyInterface::DispatchThingChangeEvent()
{
  NS_NAMED_LITERAL_STRING(type, "thingchange");
  EventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  RefPtr<Event> event = Event::Constructor(this, type, init);
  event->SetTrusted(true);
  ErrorResult rv;
  DispatchEvent(*event, rv);
  return rv.StealNSResult();  // Assuming the caller cares about the return code.
}
```

(bindings-conf-details)=

## `Bindings.conf` details

Write me. In particular, need to describe at least use of `concrete`,
`prefable`, and `addExternalInterface`.

### How to get a JSContext passed to a given method

In some rare cases you may need a `JSContext*` argument to be passed
to a C++ method that wouldn't otherwise get such an argument. To see how
to achieve this, search for `implicitJSContext` in
[dom/bindings/Bindings.conf](#bindings-conf-details).

(implementing-webidl-using-javascript)=

## Implementing Web IDL using Javascript

<div class="warning"><div class="admonition-title">Warning</div>

Implementing Web IDL using Javascript is deprecated. New interfaces
should always be implemented in C++!

</div>

It is possible to implement Web IDL interfaces in JavaScript within Gecko
-- however, **this is limited to interfaces that are not exposed in Web
Workers**. When the binding occurs, two objects are created:

- _Content-exposed object:_ what gets exposed to the web page.
- _Implementation object:_ running as a chrome-privileged script. This
  allows the implementation object to have various APIs that the
  content-exposed object does not.

Because there are two types of objects, you have to be careful about
which object you are creating.

### Creating JS-implemented Web IDL objects

To create a JS-implemented Web IDL object, one must create both the
chrome-side implementation object and the content-side page-exposed
object. There are three ways to do this.

#### Using the Web IDL constructor

If the interface has a constructor, a content-side object can be created
by getting that constructor from the relevant content window and
invoking it. For example:

``` js
var contentObject = new contentWin.RTCPeerConnection();
```

The returned object will be an Xray wrapper for the content-side object.
Creating the object this way will automatically create the chrome-side
object using its contractID.

This method is limited to the constructor signatures exposed to
webpages. Any additional configuration of the object needs to be done
methods on the interface.

Creating many objects this way can be slow due to the createInstance
overhead involved.

#### Using a `_create` method

A content-side object can be created for a given chrome-side object by
invoking the static `_create` method on the interface. This method
takes two arguments: the content window in which to create the object
and the chrome-side object to use. For example:

``` js
var contentObject = RTCPeerConnection._create(contentWin, new
MyPeerConnectionImpl());
```

However, if you are in a JS component, you may only be able to get to
the correct interface object via some window object. In this case, the
code would look more like:

``` js
var contentObject = contentWin.RTCPeerConnection._create(contentWin,
new MyPeerConnectionImpl());
```

Creating the object this way will not invoke its `__init` method or
`init` method.

#### By returning a chrome-side object from a JS-implemented Web IDL method

If a JS-implemented Web IDL method is declared as returning a
JS-implemented interface, then a non-Web IDL object returned from that
method will be treated as the chrome-side part of a JS-implemented
WebIdL object and the content-side part will be automatically created.

Creating the object this way will not invoke its `__init` method or
`init` method.

### Implementing a Web IDL object in JavaScript

To implement a Web IDL interface in JavaScript, first add a Web IDL file,
in the same way as you would for a C++-implemented interface. To support
implementation in JS, you must add an extended attribute
`JSImplementation="CONTRACT_ID_STRING"` on your interface, where
CONTRACT_ID_STRING is the XPCOM component contract ID of the JS
implementation -- note ";1" is just a Mozilla convention for versioning
APIs. Here's an example:

``` webidl
[JSImplementation="@mozilla.org/my-number;1"]
interface MyNumber {
  constructor(optional long firstNumber);
  attribute long value;
  readonly attribute long otherValue;
  undefined doNothing();
};
```

Next, create an XPCOM component that implements this interface. Use
the same contract ID as you specified in the Web IDL file. The class
ID doesn't matter, except that it should be a newly generated one. For
`QueryInterface`, you only need to implement `nsISupports`, not
anything corresponding to the Web IDL interface. The name you use for
the XPCOM component should be distinct from the name of the interface,
to avoid confusing error messages.

Web IDL attributes are implemented as properties on the JS object or its
prototype chain, whereas Web IDL methods are implemented as methods on
the object or prototype. Note that any other instances of the interface
that you are passed in as arguments are the full web-facing version of
the object, and not the JS implementation, so you currently cannot
access any private data.

The Web IDL constructor invocation will first create your object. If the
XPCOM component implements `nsIDOMGlobalPropertyInitializer`, then
the object's `init` method will be invoked with a single argument:
the content window the constructor came from. This allows the JS
implementation to know which content window it's associated with.
The `init` method should not return anything. After this, the
content-side object will be created. Then,if there are any constructor
arguments, the object's `__init` method will be invoked, with the
constructor arguments as its arguments.

### Static Members

Static attributes and methods are not supported on JS-implemented Web IDL
(see [bug
863952](https://bugzilla.mozilla.org/show_bug.cgi?id=863952)).
However, with the changes in [bug
1172785](https://bugzilla.mozilla.org/show_bug.cgi?id=1172785) you
can route static methods to a C++ implementation on another object using
a `StaticClassOverride` annotation. This annotation includes the
full, namespace-qualified name of the class that contains an
implementation of the named method. The include for that class must be
found in a directory based on its name.

``` webidl
[JSImplementation="@mozilla.org/dom/foo;1"]
interface Foo {
  [StaticClassOverride="mozilla::dom::OtherClass"]
  static Promise<undefined> doSomething();
};
```

Rather than calling into a method on the JS implementation; calling
`Foo.doSomething()` will result in calling
`mozilla::dom::OtherClass::DoSomething()`.

### Checking for Permissions or Preferences

With JS-implemented Web IDL, the `init` method should only return
undefined. If any other value, such as `null`, is returned, the
bindings code will assert or crash. In other words, it acts like it has
an "undefined" return type. Preference or permission checking should be
implemented by adding an extended attribute to the Web IDL interface.
This has the advantage that if the check fails, the constructor or
object will not show up at all.

For preference checking, add an extended attribute
`Pref="myPref.enabled"` where `myPref.enabled` is the preference
that should be checked. `SettingsLock` is an example of this.

For permissions or other kinds of checking, add an extended attribute
`Func="MyPermissionChecker"` where `MyPermissionChecker` is a
function implemented in C++ that returns true if the interface should be
enabled. This function can do whatever checking is needed. One example
of this is `PushManager`.

### Example

Here's an example JS implementation of the above interface. The
`invisibleValue` field will not be accessible to web content, but is
usable by the doNothing() method.

``` js
function MyNumberInner() {
  this.value = 111;
  this.invisibleValue = 12345;
}

MyNumberInner.prototype = {
  classDescription: "Get my number XPCOM Component",
  contractID: "@mozilla.org/my-number;1",
  QueryInterface: ChromeUtils.generateQI([]),
  doNothing: function() {},
  get otherValue() { return this.invisibleValue - 4; },
  __init: function(firstNumber) {
    if (arguments.length > 0) {
      this.value = firstNumber;
    }
  }
}
```

Finally, add a component and a contract and whatever other manifest
stuff you need to implement an XPCOM component.

### Guarantees provided by bindings

When implementing a Web IDL interface in JavaScript, certain guarantees
will be provided by the binding implementation. For example, string or
numeric arguments will actually be primitive strings or numbers.
Dictionaries will contain only the properties that they are declared to
have, and they will have the right types. Interface arguments will
actually be objects implementing that interface.

What the bindings will NOT guarantee is much of anything about
`object` and `any` arguments. They will get cross-compartment
wrappers that make touching them from chrome code not be an immediate
security bug, but otherwise they can have quite surprising behavior if
the page is trying to be malicious. Try to avoid using these types if
possible.

### Accessing the content object from the implementation

If the JS implementation of the Web IDL interface needs to access the
content object, it is available as a property called `__DOM_IMPL__` on
the chrome implementation object. This property only appears after the
content-side object has been created. So it is available in `__init`
but not in `init`.

### Determining the principal of the caller that invoked the Web IDL API

This can be done by calling
`Component.utils.getWebIDLCallerPrincipal()`.

### Throwing exceptions from JS-implemented APIs

There are two reasons a JS implemented API might throw. The first reason
is that some unforeseen condition occurred and the second is that a
specification requires an exception to be thrown.

When throwing for an unforeseen condition, the exception will be
reported to the console, and a sanitized NS_ERROR_UNEXPECTED exception
will be thrown to the calling content script, with the file/line of the
content code that invoked your API. This will avoid exposing chrome URIs
and other implementation details to the content code.

When throwing because a specification requires an exception, you need to
create the exception from the window your Web IDL object is associated
with (the one that was passed to your `init` method). The binding code
will then rethrow that exception to the web page. An example of how
this could work:

``` js
if (!isValid(passedInObject)) {
  throw new this.contentWindow.TypeError("Object is invalid");
}
```

or

``` js
if (!isValid(passedInObject)) {
  throw new this.contentWindow.DOMException("Object is invalid", "InvalidStateError");
}
```

depending on which exact exception the specification calls for throwing
in this situation.

In some cases you may need to perform operations whose exception message
you just want to propagate to the content caller. This can be done like
so:

``` js
try {
  someOperationThatCanThrow();
} catch (e) {
  throw new this.contentWindow.Error(e.message);
}
```

### Inheriting from interfaces implemented in C++

It's possible to have an interface implemented in JavaScript inherit
from an interface implemented in C++. To do so, simply have one
interface inherit from the other and the bindings code will
auto-generate a C++ object inheriting from the implementation of the
parent interface. The class implementing the parent interface will need
a constructor that takes an `nsPIDOMWindow*` (though it doesn't have
to do anything with that argument).

If the class implementing the parent interface is abstract and you want
to use a specific concrete class as the implementation to inherit from,
you will need to add a `defaultImpl` annotation to the descriptor for
the parent interface in `Bindings.conf`. The value of the annotation
is the C++ class to use as the parent for JS-implemented descendants; if
`defaultImpl` is not specified, the `nativeType` will be used.

For example, consider this interface that we wish to implement in
JavaScript:

``` webidl
[JSImplementation="some-contract"]
interface MyEventTarget : EventTarget {
  attribute EventHandler onmyevent;
  undefined dispatchTheEvent(); // Sends a "myevent" event to this EventTarget
}
```

The implementation would look something like this, ignoring most of the
XPCOM boilerplate:

``` js
function MyEventTargetImpl() {
}
MyEventTargetImpl.prototype = {
  // QI to nsIDOMGlobalPropertyInitializer so we get init() called on us.
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGlobalPropertyInitializer"]),

  init: function(contentWindow) {
    this.contentWindow = contentWindow;
  },

  get onmyevent() {
    return this.__DOM_IMPL__.getEventHandler("onmyevent");
  },

  set onmyevent(handler) {
    this.__DOM_IMPL__.setEventHandler("onmyevent", handler);
  },

  dispatchTheEvent: function() {
    var event = new this.contentWindow.Event("myevent");
    this.__DOM_IMPL__.dispatchEvent(event);
  },
};
```

The implementation would automatically support the API exposed on
`EventTarget` (so, for example, `addEventListener`). Calling the
`dispatchTheEvent` method would cause dispatch of an event that
content script can see via listeners it has added.

Note that in this case the chrome implementation is relying on some
`[ChromeOnly]` methods on EventTarget that were added specifically to
make it possible to easily implement event handlers. Other cases can do
similar things as needed.
