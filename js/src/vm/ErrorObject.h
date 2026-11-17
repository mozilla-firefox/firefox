/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_ErrorObject_h_
#define vm_ErrorObject_h_

#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"

#include <stdint.h>

#include "jspubtd.h"
#include "NamespaceImports.h"

#include "js/Class.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/ErrorReport.h"
#include "js/Exception.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/UniquePtr.h"
#include "js/Value.h"
#include "vm/JSObject.h"
#include "vm/NativeObject.h"

extern const JSErrorFormatString js_ErrorFormatString[JSErr_Limit];

namespace js {

class ErrorObject : public NativeObject {
  static JSObject* createProto(JSContext* cx, JSProtoKey key);

  static JSObject* createConstructor(JSContext* cx, JSProtoKey key);

  static bool init(JSContext* cx, Handle<ErrorObject*> obj, JSExnType type,
                   UniquePtr<JSErrorReport> errorReport, HandleString fileName,
                   HandleObject stack, uint32_t sourceId, uint32_t lineNumber,
                   JS::ColumnNumberOneOrigin columnNumber, HandleString message,
                   Handle<mozilla::Maybe<JS::Value>> cause);

  static const ClassSpec classSpecs[JSEXN_ERROR_LIMIT];
  static const JSClass protoClasses[JSEXN_ERROR_LIMIT];

 protected:
  JS_DEFINE_TYPED_SLOT(0, STACK_SLOT, Object, Null);
  JS_DEFINE_TYPED_SLOT(1, ERROR_REPORT_SLOT, Private, Undefined);
  JS_DEFINE_UNTYPED_SLOT(2, FILENAME_SLOT);
  JS_DEFINE_UNTYPED_SLOT(3, LINENUMBER_SLOT);
  JS_DEFINE_UNTYPED_SLOT(4, COLUMNNUMBER_SLOT);
  JS_DEFINE_UNTYPED_SLOT(5, MESSAGE_SLOT);
  JS_DEFINE_UNTYPED_SLOT(6, CAUSE_SLOT);
  JS_DEFINE_TYPED_SLOT(7, SOURCEID_SLOT, Int32);

  static const uint32_t RESERVED_SLOTS = 8;

  // This slot is only used for errors that could be Wasm traps.
  JS_DEFINE_TYPED_SLOT(8, WASM_TRAP_SLOT, Boolean);
  static const uint32_t RESERVED_SLOTS_MAYBE_WASM_TRAP = 9;

 public:
  static const JSClass classes[JSEXN_ERROR_LIMIT];

  static const JSClass* classForType(JSExnType type) {
    MOZ_ASSERT(type < JSEXN_ERROR_LIMIT);
    return &classes[type];
  }

  static bool isErrorClass(const JSClass* clasp) {
    return &classes[0] <= clasp && clasp < &classes[0] + std::size(classes);
  }

  // Create an error of the given type corresponding to the provided location
  // info.  If |message| is non-null, then the error will have a .message
  // property with that value; otherwise the error will have no .message
  // property.
  static ErrorObject* create(JSContext* cx, JSExnType type, HandleObject stack,
                             HandleString fileName, uint32_t sourceId,
                             uint32_t lineNumber,
                             JS::ColumnNumberOneOrigin columnNumber,
                             UniquePtr<JSErrorReport> report,
                             HandleString message,
                             Handle<mozilla::Maybe<JS::Value>> cause,
                             HandleObject proto = nullptr);

  /*
   * Assign the initial error shape to the empty object.  (This shape does
   * *not* include .message, which must be added separately if needed; see
   * ErrorObject::init.)
   */
  static SharedShape* assignInitialShape(JSContext* cx,
                                         Handle<ErrorObject*> obj);

  JSExnType type() const {
    MOZ_ASSERT(isErrorClass(getClass()));
    return static_cast<JSExnType>(getClass() - &classes[0]);
  }

  JSErrorReport* getErrorReport() const {
    const Value& slot = getReservedSlotTyped(ERROR_REPORT_SLOT);
    if (slot.isUndefined()) {
      return nullptr;
    }
    return static_cast<JSErrorReport*>(slot.toPrivate());
  }

  JSErrorReport* getOrCreateErrorReport(JSContext* cx);

  inline JSString* fileName(JSContext* cx) const;
  inline uint32_t sourceId() const;

  // Line number (1-origin).
  inline uint32_t lineNumber() const;

  // Column number in UTF-16 code units.
  inline JS::ColumnNumberOneOrigin columnNumber() const;

  // Returns nullptr or a (possibly wrapped) SavedFrame object.
  inline JSObject* stack() const;

  JSString* getMessage() const {
    Value val = getReservedSlot(MESSAGE_SLOT);
    return val.isString() ? val.toString() : nullptr;
  }

  /*
   * Return Nothing if the error was created without an initial cause or if the
   * initial cause data property has been redefined to an accessor property.
   */
  mozilla::Maybe<Value> getCause() const {
    const auto& value = getReservedSlot(CAUSE_SLOT);
    if (value.isMagic(JS_ERROR_WITHOUT_CAUSE) || value.isPrivateGCThing()) {
      return mozilla::Nothing();
    }
    return mozilla::Some(value);
  }

  void setStackSlot(const Value& stack) {
    MOZ_ASSERT(stack.isObjectOrNull());
    setReservedSlotTyped(STACK_SLOT, stack);
  }

  void setCauseSlot(const Value& cause) {
    MOZ_ASSERT(!cause.isMagic());
    MOZ_ASSERT(getCause().isSome());
    setReservedSlot(CAUSE_SLOT, cause);
  }

  // Getter and setter for the Error.prototype.stack accessor.
  static bool getStack(JSContext* cx, unsigned argc, Value* vp);
  static bool getStack_impl(JSContext* cx, const CallArgs& args);
  static bool setStack(JSContext* cx, unsigned argc, Value* vp);
  static bool setStack_impl(JSContext* cx, const CallArgs& args);

  // Used to distinguish errors created from Wasm traps.
  bool mightBeWasmTrap() const {
    return type() == JSEXN_WASMRUNTIMEERROR || type() == JSEXN_INTERNALERR;
  }
  bool fromWasmTrap() const {
    if (!mightBeWasmTrap()) {
      return false;
    } else {
      MOZ_ASSERT(JSCLASS_RESERVED_SLOTS(getClass()) > WASM_TRAP_SLOT.index());
      return getReservedSlotTyped(WASM_TRAP_SLOT).toBoolean();
    }
  }
  void setFromWasmTrap();
};

JSString* ErrorToSource(JSContext* cx, HandleObject obj);

UniquePtr<JSErrorNotes::Note> CopyErrorNote(JSContext* cx,
                                            JSErrorNotes::Note* note);

UniquePtr<JSErrorReport> CopyErrorReport(JSContext* cx, JSErrorReport* report);

// Cut off the stack if it gets too deep (most commonly for infinite recursion
// errors).
static const size_t MAX_REPORTED_STACK_DEPTH = 1u << 7;

mozilla::Maybe<uint32_t> GetStackTraceLimit(JSContext* cx);

bool CaptureStack(JSContext* cx, MutableHandleObject stack, uint32_t limit);

JSString* ComputeStackString(JSContext* cx);

/*
 * Given a JSErrorReport, check to see if there is an exception associated with
 * the error number.  If there is, then create an appropriate Error object,
 * set it as the pending exception.
 *
 * It's possible we fail (due to OOM or some other error) and end up setting
 * JSContext::unwrappedException to a different exception.
 * The original error described by reportp typically won't be reported anywhere
 * in this case.
 *
 * Returns true if the error was converted to an exception. If the error code
 * is unrecognized, we fail due to OOM, or if we decided to do nothing in order
 * to avoid recursion, we return false and this error is just being swept under
 * the rug.
 */
extern bool ErrorToException(JSContext* cx, JSErrorReport* reportp,
                             JSErrorCallback callback, void* userRef);

extern bool ErrorFromException(JSContext* cx, HandleObject obj,
                               JS::BorrowedErrorReport& errorReport);

/*
 * Make a copy of errobj parented to cx's compartment's global.
 *
 * errobj may be in a different compartment than cx, but it must be an Error
 * object (not a wrapper of one) and it must not be one of the standard error
 * prototype objects (errobj->getPrivate() must not be nullptr).
 */
extern JSObject* CopyErrorObject(JSContext* cx,
                                 JS::Handle<ErrorObject*> errobj);

static_assert(
    JSEXN_ERR == 0 &&
        JSProto_Error + int(JSEXN_INTERNALERR) == JSProto_InternalError &&
        JSProto_Error + int(JSEXN_AGGREGATEERR) == JSProto_AggregateError &&
        JSProto_Error + int(JSEXN_EVALERR) == JSProto_EvalError &&
        JSProto_Error + int(JSEXN_RANGEERR) == JSProto_RangeError &&
        JSProto_Error + int(JSEXN_REFERENCEERR) == JSProto_ReferenceError &&
        JSProto_Error + int(JSEXN_SUPPRESSEDERR) == JSProto_SuppressedError &&
        JSProto_Error + int(JSEXN_SYNTAXERR) == JSProto_SyntaxError &&
        JSProto_Error + int(JSEXN_TYPEERR) == JSProto_TypeError &&
        JSProto_Error + int(JSEXN_URIERR) == JSProto_URIError &&
        JSProto_Error + int(JSEXN_DEBUGGEEWOULDRUN) ==
            JSProto_DebuggeeWouldRun &&
        JSProto_Error + int(JSEXN_WASMCOMPILEERROR) == JSProto_CompileError &&
        JSProto_Error + int(JSEXN_WASMLINKERROR) == JSProto_LinkError &&
        JSProto_Error + int(JSEXN_WASMRUNTIMEERROR) == JSProto_RuntimeError &&
#ifdef ENABLE_WASM_JSPI
        JSProto_Error + int(JSEXN_WASMSUSPENDERROR) == JSProto_SuspendError &&
        JSEXN_WASMSUSPENDERROR + 1 == JSEXN_WARN &&
#else
        JSEXN_WASMRUNTIMEERROR + 1 == JSEXN_WARN &&
#endif
        JSEXN_WARN + 1 == JSEXN_NOTE && JSEXN_NOTE + 1 == JSEXN_LIMIT,
    "GetExceptionProtoKey and ExnTypeFromProtoKey require that "
    "each corresponding JSExnType and JSProtoKey value be separated "
    "by the same constant value");

static inline constexpr JSProtoKey GetExceptionProtoKey(JSExnType exn) {
  MOZ_ASSERT(JSEXN_ERR <= exn);
  MOZ_ASSERT(exn < JSEXN_WARN);
  return JSProtoKey(JSProto_Error + int(exn));
}

static inline JSExnType ExnTypeFromProtoKey(JSProtoKey key) {
  JSExnType type = static_cast<JSExnType>(key - JSProto_Error);
  MOZ_ASSERT(type >= JSEXN_ERR);
  MOZ_ASSERT(type < JSEXN_ERROR_LIMIT);
  return type;
}

static inline bool IsErrorProtoKey(JSProtoKey key) {
  int type = key - JSProto_Error;
  return type >= JSEXN_ERR && type < JSEXN_ERROR_LIMIT;
}

class AutoClearPendingException {
  JSContext* cx;

 public:
  explicit AutoClearPendingException(JSContext* cxArg) : cx(cxArg) {}

  ~AutoClearPendingException() { JS_ClearPendingException(cx); }
};

// Convert the given value to a string for use in an error message. This
// function never returns nullptr and never reports an exception.
extern const char* ValueToSourceForError(JSContext* cx, HandleValue val,
                                         JS::UniqueChars& bytes);

bool GetInternalError(JSContext* cx, unsigned errorNumber,
                      MutableHandleValue error);
bool GetTypeError(JSContext* cx, unsigned errorNumber,
                  MutableHandleValue error);
bool GetAggregateError(JSContext* cx, unsigned errorNumber,
                       MutableHandleValue error);

}  // namespace js

template <>
inline bool JSObject::is<js::ErrorObject>() const {
  return js::ErrorObject::isErrorClass(getClass());
}

#endif  // vm_ErrorObject_h_
