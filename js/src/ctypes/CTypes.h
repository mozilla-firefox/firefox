/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ctypes_CTypes_h
#define ctypes_CTypes_h

#include "mozilla/Sprintf.h"
#include "mozilla/Vector.h"

#include "ffi.h"
#include "prlink.h"

#include "ctypes/typedefs.h"
#include "gc/ZoneAllocator.h"
#include "js/AllocPolicy.h"
#include "js/GCHashTable.h"
#include "js/UniquePtr.h"
#include "js/Vector.h"
#include "vm/JSObject.h"
#include "vm/StringType.h"

namespace JS {
struct CTypesCallbacks;
}  // namespace JS

// libffi's ffi_call_plan is only accelerated for the x86-64 System V ABI;
// elsewhere it just wraps ffi_call, so gate on the targets that benefit. The
// version floor is 3.8.0 rather than 3.7.0, where the plan API landed, because
// the GC accounting needs ffi_call_plan_size.
#if defined(FFI_VERSION_NUMBER) && FFI_VERSION_NUMBER >= 30800 && \
    defined(__x86_64__) && !defined(__ILP32__) && !defined(_WIN64)
#  define CTYPES_HAVE_FAST_CALL_PLAN 1
#endif

namespace js {
namespace ctypes {

/*******************************************************************************
** Utility classes
*******************************************************************************/

// CTypes builds a number of strings. StringBuilder allows repeated appending
// with a single error check at the end. Only the Vector methods required for
// building the string are exposed.

template <class CharT, size_t N>
class StringBuilder {
  Vector<CharT, N, SystemAllocPolicy> v;

  // Have any (OOM) errors been encountered while constructing this string?
  bool errored{false};

#ifdef DEBUG
  // Have we finished building this string?
  bool finished{false};

  // Did we check for errors?
  mutable bool checked{false};
#endif

 public:
  explicit operator bool() const {
#ifdef DEBUG
    checked = true;
#endif
    return !errored;
  }

  // Handle the result of modifying the string, by remembering the persistent
  // errored status.
  bool handle(bool result) {
    MOZ_ASSERT(!finished);
    if (!result) {
      errored = true;
    }
    return result;
  }

  bool resize(size_t n) { return handle(v.resize(n)); }

  CharT& operator[](size_t index) { return v[index]; }
  const CharT& operator[](size_t index) const { return v[index]; }
  size_t length() const { return v.length(); }

  template <typename U>
  [[nodiscard]] bool append(U&& u) {
    return handle(v.append(u));
  }

  template <typename U>
  [[nodiscard]] bool append(const U* begin, const U* end) {
    return handle(v.append(begin, end));
  }

  template <typename U>
  [[nodiscard]] bool append(const U* begin, size_t len) {
    return handle(v.append(begin, len));
  }

  CharT* begin() {
    MOZ_ASSERT(!finished);
    return v.begin();
  }

  // finish() produces the results of the string building, and is required as
  // the last thing before the string contents are used. The StringBuilder must
  // be checked for errors before calling this, however.
  Vector<CharT, N, SystemAllocPolicy>&& finish() {
    MOZ_ASSERT(!errored);
    MOZ_ASSERT(!finished);
    MOZ_ASSERT(checked);
#ifdef DEBUG
    finished = true;
#endif
    return std::move(v);
  }
};

// Note that these strings do not have any inline storage, because we use move
// constructors to pass the data around and inline storage would necessitate
// copying.
typedef StringBuilder<char16_t, 0> AutoString;
typedef StringBuilder<char, 0> AutoCString;

typedef Vector<char16_t, 0, SystemAllocPolicy> AutoStringChars;
typedef Vector<char, 0, SystemAllocPolicy> AutoCStringChars;

// Convenience functions to append, insert, and compare Strings.
template <class T, size_t N, size_t ArrayLength>
void AppendString(JSContext* cx, StringBuilder<T, N>& v,
                  const char (&array)[ArrayLength]) {
  // Don't include the trailing '\0'.
  size_t alen = ArrayLength - 1;
  size_t vlen = v.length();
  if (!v.resize(vlen + alen)) {
    return;
  }

  for (size_t i = 0; i < alen; ++i) {
    v[i + vlen] = array[i];
  }
}

template <class T, size_t N>
void AppendChars(StringBuilder<T, N>& v, const char c, size_t count) {
  size_t vlen = v.length();
  if (!v.resize(vlen + count)) {
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    v[i + vlen] = c;
  }
}

template <class T, size_t N>
void AppendUInt(StringBuilder<T, N>& v, unsigned n) {
  char array[16];
  size_t alen = SprintfLiteral(array, "%u", n);
  size_t vlen = v.length();
  if (!v.resize(vlen + alen)) {
    return;
  }

  for (size_t i = 0; i < alen; ++i) {
    v[i + vlen] = array[i];
  }
}

template <class T, size_t N, size_t M, class AP>
void AppendString(JSContext* cx, StringBuilder<T, N>& v,
                  mozilla::Vector<T, M, AP>& w) {
  if (!v.append(w.begin(), w.length())) {
    return;
  }
}

template <size_t N>
void AppendString(JSContext* cx, StringBuilder<char16_t, N>& v, JSString* str) {
  MOZ_ASSERT(str);
  JSLinearString* linear = str->ensureLinear(cx);
  if (!linear) {
    return;
  }
  JS::AutoCheckCannotGC nogc;
  if (linear->hasLatin1Chars()) {
    if (!v.append(linear->latin1Chars(nogc), linear->length())) {
      return;
    }
  } else {
    if (!v.append(linear->twoByteChars(nogc), linear->length())) {
      return;
    }
  }
}

template <size_t N>
void AppendString(JSContext* cx, StringBuilder<char, N>& v, JSString* str) {
  MOZ_ASSERT(str);
  size_t vlen = v.length();
  size_t alen = str->length();
  if (!v.resize(vlen + alen)) {
    return;
  }

  JSLinearString* linear = str->ensureLinear(cx);
  if (!linear) {
    return;
  }

  JS::AutoCheckCannotGC nogc;
  if (linear->hasLatin1Chars()) {
    const Latin1Char* chars = linear->latin1Chars(nogc);
    for (size_t i = 0; i < alen; ++i) {
      v[i + vlen] = char(chars[i]);
    }
  } else {
    const char16_t* chars = linear->twoByteChars(nogc);
    for (size_t i = 0; i < alen; ++i) {
      v[i + vlen] = char(chars[i]);
    }
  }
}

template <class T, size_t N, size_t ArrayLength>
void PrependString(JSContext* cx, StringBuilder<T, N>& v,
                   const char (&array)[ArrayLength]) {
  // Don't include the trailing '\0'.
  size_t alen = ArrayLength - 1;
  size_t vlen = v.length();
  if (!v.resize(vlen + alen)) {
    return;
  }

  // Move vector data forward. This is safe since we've already resized.
  memmove(v.begin() + alen, v.begin(), vlen * sizeof(T));

  // Copy data to insert.
  for (size_t i = 0; i < alen; ++i) {
    v[i] = array[i];
  }
}

template <size_t N>
void PrependString(JSContext* cx, StringBuilder<char16_t, N>& v,
                   JSString* str) {
  MOZ_ASSERT(str);
  size_t vlen = v.length();
  size_t alen = str->length();
  if (!v.resize(vlen + alen)) {
    return;
  }

  JSLinearString* linear = str->ensureLinear(cx);
  if (!linear) {
    return;
  }

  // Move vector data forward. This is safe since we've already resized.
  memmove(v.begin() + alen, v.begin(), vlen * sizeof(char16_t));

  // Copy data to insert.
  CopyChars(v.begin(), *linear);
}

[[nodiscard]] bool ReportErrorIfUnpairedSurrogatePresent(JSContext* cx,
                                                         JSLinearString* str);

[[nodiscard]] JSObject* GetThisObject(JSContext* cx, const CallArgs& args,
                                      const char* msg);

/*******************************************************************************
** Function and struct API definitions
*******************************************************************************/

// for JS error reporting
enum ErrorNum {
#define MSG_DEF(name, count, exception, format) name,
#include "ctypes/ctypes.msg"
#undef MSG_DEF
  CTYPESERR_LIMIT
};

/**
 * ABI constants that specify the calling convention to use.
 * ctypes.default_abi corresponds to the cdecl convention, and in almost all
 * cases is the correct choice. ctypes.stdcall_abi is provided for calling
 * stdcall functions on Win32, and implies stdcall symbol name decoration;
 * ctypes.winapi_abi is just stdcall but without decoration.
 */
enum ABICode {
  ABI_DEFAULT,
  ABI_STDCALL,
  ABI_THISCALL,
  ABI_WINAPI,
  INVALID_ABI
};

enum TypeCode {
  TYPE_void_t,
#define DEFINE_TYPE(name, type, ffiType) TYPE_##name,
  CTYPES_FOR_EACH_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
      TYPE_pointer,
  TYPE_function,
  TYPE_array,
  TYPE_struct
};

// Descriptor of one field in a StructType. The name of the field is stored
// as the key to the hash entry.
struct FieldInfo {
  HeapPtr<JSObject*> mType;  // CType of the field
  size_t mIndex;             // index of the field in the struct (first is 0)
  size_t mOffset;            // offset of the field in the struct, in bytes

  void trace(JSTracer* trc) { TraceEdge(trc, &mType, "fieldType"); }
};

struct UnbarrieredFieldInfo {
  JSObject* mType;  // CType of the field
  size_t mIndex;    // index of the field in the struct (first is 0)
  size_t mOffset;   // offset of the field in the struct, in bytes
};
static_assert(sizeof(UnbarrieredFieldInfo) == sizeof(FieldInfo),
              "UnbarrieredFieldInfo should be the same as FieldInfo but with "
              "unbarriered mType");

// Hash policy for FieldInfos.
struct FieldHashPolicy {
  using Key = JSLinearString*;
  using Lookup = Key;

  static HashNumber hash(const Lookup& l) { return js::HashStringChars(l); }

  static bool match(const Key& k, const Lookup& l) {
    return js::EqualStrings(k, l);
  }
};

using FieldInfoHash = GCHashMap<js::HeapPtr<JSLinearString*>, FieldInfo,
                                FieldHashPolicy, CellAllocPolicy>;

// Descriptor of ABI, return type, argument types, and variadicity for a
// FunctionType.
struct FunctionInfo {
  explicit FunctionInfo(JS::Zone* zone) : mArgTypes(zone), mFFITypes(zone) {}

  // Initialized in NewFunctionInfo when !mIsVariadic, but only later, in
  // FunctionType::Call, when mIsVariadic. Not always consistent with
  // mFFITypes, due to lazy initialization when mIsVariadic.
  ffi_cif mCIF;

  // Calling convention of the function. Convert to ffi_abi using GetABI
  // and ObjectValue. Stored as a JSObject* for ease of tracing.
  HeapPtr<JSObject*> mABI;

  // The CType of the value returned by the function.
  HeapPtr<JSObject*> mReturnType;

  // A fixed array of known parameter types, excluding any variadic
  // parameters (if mIsVariadic).
  GCVector<HeapPtr<JSObject*>, 0, CellAllocPolicy> mArgTypes;

  // A variable array of ffi_type*s corresponding to both known parameter
  // types and dynamic (variadic) parameter types. Longer than mArgTypes
  // only if mIsVariadic.
  Vector<ffi_type*, 0, CellAllocPolicy> mFFITypes;

  // Flag indicating whether the function behaves like a C function with
  // ... as the final formal parameter.
  bool mIsVariadic;

#ifdef CTYPES_HAVE_FAST_CALL_PLAN
  // Reusable libffi call plan wrapping mCIF, built once for non-variadic
  // signatures. Holds a pointer into mCIF, so it is freed (in CType::Finalize)
  // before mCIF.
  ffi_call_plan* mCallPlan = nullptr;
#endif
};

// Parameters necessary for invoking a JS function from a C closure.
struct ClosureInfo {
  JSContext* cx;
  HeapPtr<JSObject*> closureObj;  // CClosure object
  HeapPtr<JSObject*> typeObj;     // FunctionType describing the C function
  HeapPtr<JSObject*> thisObj;  // 'this' object to use for the JS function call
  HeapPtr<JSObject*> jsfnObj;  // JS function
  void* errResult;       // Result that will be returned if the closure throws
  ffi_closure* closure;  // The C closure itself

  // Anything conditionally freed in the destructor should be initialized to
  // nullptr here.
  explicit ClosureInfo(JSContext* context)
      : cx(context), errResult(nullptr), closure(nullptr) {}

  ~ClosureInfo() {
    if (closure) {
      ffi_closure_free(closure);
    }
    js_free(errResult);
  }
};

bool IsCTypesGlobal(HandleValue v);
bool IsCTypesGlobal(JSObject* obj);

const JS::CTypesCallbacks* GetCallbacks(JSObject* obj);

/*******************************************************************************
** JSClass reserved slot definitions
*******************************************************************************/

enum CTypesGlobalSlot {
  SLOT_CALLBACKS = 0,  // pointer to JS::CTypesCallbacks struct
  SLOT_ERRNO = 1,      // Value for latest |errno|
  SLOT_LASTERROR =
      2,  // Value for latest |GetLastError|, used only with Windows
  CTYPESGLOBAL_SLOTS
};

enum CABISlot {
  SLOT_ABICODE = 0,  // ABICode of the CABI object
  CABI_SLOTS
};

enum CTypeProtoSlot {
  SLOT_POINTERPROTO = 0,   // ctypes.PointerType.prototype object
  SLOT_ARRAYPROTO = 1,     // ctypes.ArrayType.prototype object
  SLOT_STRUCTPROTO = 2,    // ctypes.StructType.prototype object
  SLOT_FUNCTIONPROTO = 3,  // ctypes.FunctionType.prototype object
  SLOT_CDATAPROTO = 4,     // ctypes.CData.prototype object
  SLOT_POINTERDATAPROTO =
      5,  // common ancestor of all CData objects of PointerType
  SLOT_ARRAYDATAPROTO = 6,  // common ancestor of all CData objects of ArrayType
  SLOT_STRUCTDATAPROTO =
      7,  // common ancestor of all CData objects of StructType
  SLOT_FUNCTIONDATAPROTO =
      8,                // common ancestor of all CData objects of FunctionType
  SLOT_INT64PROTO = 9,  // ctypes.Int64.prototype object
  SLOT_UINT64PROTO = 10,   // ctypes.UInt64.prototype object
  SLOT_CTYPES = 11,        // ctypes object
  SLOT_OURDATAPROTO = 12,  // the data prototype corresponding to this object
  CTYPEPROTO_SLOTS
};

enum CTypeSlot {
  SLOT_PROTO = 0,     // 'prototype' property of the CType object
  SLOT_TYPECODE = 1,  // TypeCode of the CType object
  SLOT_FFITYPE = 2,   // ffi_type representing the type
  SLOT_NAME = 3,      // name of the type
  SLOT_SIZE = 4,      // size of the type, in bytes
  SLOT_ALIGN = 5,     // alignment of the type, in bytes
  SLOT_PTR = 6,       // cached PointerType object for type.ptr
  // Note that some of the slots below can overlap, since they're for
  // mutually exclusive types.
  SLOT_TARGET_T = 7,   // (PointerTypes only) 'targetType' property
  SLOT_ELEMENT_T = 7,  // (ArrayTypes only) 'elementType' property
  SLOT_LENGTH = 8,     // (ArrayTypes only) 'length' property
  SLOT_FIELDS = 7,     // (StructTypes only) 'fields' property
  SLOT_FIELDINFO = 8,  // (StructTypes only) FieldInfoHash table
  SLOT_FNINFO = 7,     // (FunctionTypes only) FunctionInfo struct
  SLOT_ARGS_T = 8,     // (FunctionTypes only) 'argTypes' property (cached)
  CTYPE_SLOTS
};

enum CDataSlot {
  SLOT_CTYPE = 0,     // CType object representing the underlying type
  SLOT_REFERENT = 1,  // JSObject this object must keep alive, if any
  SLOT_DATA = 2,      // pointer to a buffer containing the binary data
  SLOT_OWNS = 3,      // TrueValue() if this CData owns its own buffer
  SLOT_FUNNAME = 4,   // JSString representing the function name
  CDATA_SLOTS
};

enum CClosureSlot {
  SLOT_CLOSUREINFO = 0,  // ClosureInfo struct
  CCLOSURE_SLOTS
};

enum CDataFinalizerSlot {
  // PrivateValue storing CDataFinalizer::Private* pointer or UndefinedValue.
  SLOT_DATAFINALIZER_PRIVATE = 0,
  // The type of the value (a CType JSObject).
  // We hold it to permit ImplicitConvert and ToSource.
  SLOT_DATAFINALIZER_VALTYPE = 1,
  // The type of the function used at finalization (a CType JSObject).
  // We hold it to permit |ToSource|.
  SLOT_DATAFINALIZER_CODETYPE = 2,
  CDATAFINALIZER_SLOTS
};

enum TypeCtorSlot {
  SLOT_FN_CTORPROTO = 0  // ctypes.{Pointer,Array,Struct}Type.prototype
  // JSFunction objects always get exactly two slots.
};

enum Int64Slot {
  SLOT_INT64 = 0,  // pointer to a 64-bit buffer containing the integer
  INT64_SLOTS
};

enum Int64FunctionSlot {
  SLOT_FN_INT64PROTO = 0  // ctypes.{Int64,UInt64}.prototype object
  // JSFunction objects always get exactly two slots.
};

/*******************************************************************************
** Object API definitions
*******************************************************************************/

namespace CType {
JSObject* Create(JSContext* cx, HandleObject typeProto, HandleObject dataProto,
                 TypeCode type, JSString* name, HandleValue size,
                 HandleValue align, ffi_type* ffiType);

JSObject* DefineBuiltin(JSContext* cx, HandleObject ctypesObj,
                        const char* propName, JSObject* typeProto,
                        JSObject* dataProto, const char* name, TypeCode type,
                        HandleValue size, HandleValue align, ffi_type* ffiType);

bool IsCType(JSObject* obj);
bool IsCTypeProto(JSObject* obj);
TypeCode GetTypeCode(JSObject* typeObj);
bool TypesEqual(JSObject* t1, JSObject* t2);
size_t GetSize(JSObject* obj);
[[nodiscard]] bool GetSafeSize(JSObject* obj, size_t* result);
bool IsSizeDefined(JSObject* obj);
size_t GetAlignment(JSObject* obj);
ffi_type* GetFFIType(JSContext* cx, JSObject* obj);
JSString* GetName(JSContext* cx, HandleObject obj);
JSObject* GetProtoFromCtor(JSObject* obj, CTypeProtoSlot slot);
JSObject* GetProtoFromType(JSContext* cx, JSObject* obj, CTypeProtoSlot slot);
const JS::CTypesCallbacks* GetCallbacksFromType(JSObject* obj);
}  // namespace CType

namespace PointerType {
JSObject* CreateInternal(JSContext* cx, HandleObject baseType);

JSObject* GetBaseType(JSObject* obj);
}  // namespace PointerType

using UniquePtrFFIType = UniquePtr<ffi_type>;

namespace ArrayType {
JSObject* CreateInternal(JSContext* cx, HandleObject baseType, size_t length,
                         bool lengthDefined);

JSObject* GetBaseType(JSObject* obj);
size_t GetLength(JSObject* obj);
[[nodiscard]] bool GetSafeLength(JSObject* obj, size_t* result);
UniquePtrFFIType BuildFFIType(JSContext* cx, JSObject* obj);
}  // namespace ArrayType

namespace StructType {
[[nodiscard]] bool DefineInternal(JSContext* cx, JSObject* typeObj,
                                  JSObject* fieldsObj);

const FieldInfoHash* GetFieldInfo(JSObject* obj);
const FieldInfo* LookupField(JSContext* cx, JSObject* obj,
                             JSLinearString* name);
JSObject* BuildFieldsArray(JSContext* cx, JSObject* obj);
UniquePtrFFIType BuildFFIType(JSContext* cx, JSObject* obj);
}  // namespace StructType

namespace FunctionType {
JSObject* CreateInternal(JSContext* cx, HandleValue abi, HandleValue rtype,
                         const HandleValueArray& args);

JSObject* ConstructWithObject(JSContext* cx, JSObject* typeObj,
                              JSObject* refObj, PRFuncPtr fnptr,
                              JSObject* result);

FunctionInfo* GetFunctionInfo(JSObject* obj);
void BuildSymbolName(JSContext* cx, JSString* name, JSObject* typeObj,
                     AutoCString& result);
}  // namespace FunctionType

namespace CClosure {
JSObject* Create(JSContext* cx, HandleObject typeObj, HandleObject fnObj,
                 HandleObject thisObj, HandleValue errVal, PRFuncPtr* fnptr);
}  // namespace CClosure

namespace CData {
JSObject* Create(JSContext* cx, HandleObject typeObj, HandleObject refObj,
                 void* data, bool ownResult);

JSObject* GetCType(JSObject* dataObj);
void* GetData(JSObject* dataObj);
bool IsCData(JSObject* obj);
bool IsCDataMaybeUnwrap(MutableHandleObject obj);
bool IsCData(HandleValue v);
bool IsCDataProto(JSObject* obj);

// Attached by JSAPI as the function 'ctypes.cast'
[[nodiscard]] bool Cast(JSContext* cx, unsigned argc, Value* vp);
// Attached by JSAPI as the function 'ctypes.getRuntime'
[[nodiscard]] bool GetRuntime(JSContext* cx, unsigned argc, Value* vp);
}  // namespace CData

namespace Int64 {
bool IsInt64(JSObject* obj);
}  // namespace Int64

namespace UInt64 {
bool IsUInt64(JSObject* obj);
}  // namespace UInt64

}  // namespace ctypes
}  // namespace js

#endif /* ctypes_CTypes_h */
