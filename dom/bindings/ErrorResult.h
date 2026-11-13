/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A set of structs for tracking exceptions that need to be thrown to JS:
 * ErrorResult and IgnoredErrorResult.
 *
 * Conceptually, these structs represent either success or an exception in the
 * process of being thrown.  This means that a failing ErrorResult _must_ be
 * handled in one of the following ways before coming off the stack:
 *
 * 1) Suppressed via SuppressException().
 * 2) Converted to a pure nsresult return value via StealNSResult().
 * 3) Converted to an actual pending exception on a JSContext via
 *    MaybeSetPendingException.
 * 4) Converted to an exception JS::Value (probably to then reject a Promise
 *    with) via dom::ToJSValue.
 *
 * An IgnoredErrorResult will automatically do the first of those four things.
 */

#ifndef mozilla_ErrorResult_h
#define mozilla_ErrorResult_h

#include <new>
#include <utility>

#include "js/ErrorReport.h"
#include "js/GCAnnotations.h"
#include "js/Value.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Utf8.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nscore.h"

namespace IPC {
class Message;
class MessageReader;
class MessageWriter;
template <typename>
struct ParamTraits;
}  // namespace IPC
class PickleIterator;

namespace mozilla {

namespace dom {

class Promise;

enum ErrNum : uint16_t {
#define MSG_DEF(_name, _argc, _has_context, _exn, _str) _name,
#include "mozilla/dom/Errors.msg"
#undef MSG_DEF
  Err_Limit
};

// Debug-only compile-time table of the number of arguments of each error, for
// use in static_assert.
#if defined(DEBUG) && (defined(__clang__) || defined(__GNUC__))
uint16_t constexpr ErrorFormatNumArgs[] = {
#  define MSG_DEF(_name, _argc, _has_context, _exn, _str) _argc,
#  include "mozilla/dom/Errors.msg"
#  undef MSG_DEF
};
#endif

// Table of whether various error messages want a context arg.
bool constexpr ErrorFormatHasContext[] = {
#define MSG_DEF(_name, _argc, _has_context, _exn, _str) _has_context,
#include "mozilla/dom/Errors.msg"
#undef MSG_DEF
};

// Table of the kinds of exceptions error messages will produce.
JSExnType constexpr ErrorExceptionType[] = {
#define MSG_DEF(_name, _argc, _has_context, _exn, _str) _exn,
#include "mozilla/dom/Errors.msg"
#undef MSG_DEF
};

uint16_t GetErrorArgCount(const ErrNum aErrorNumber);

namespace binding_detail {
void ThrowErrorMessage(JSContext* aCx, const unsigned aErrorNumber, ...);
}  // namespace binding_detail

template <ErrNum errorNumber, typename... Ts>
inline bool ThrowErrorMessage(JSContext* aCx, Ts&&... aArgs) {
#if defined(DEBUG) && (defined(__clang__) || defined(__GNUC__))
  static_assert(ErrorFormatNumArgs[errorNumber] == sizeof...(aArgs),
                "Pass in the right number of arguments");
#endif
  binding_detail::ThrowErrorMessage(aCx, static_cast<unsigned>(errorNumber),
                                    std::forward<Ts>(aArgs)...);
  return false;
}

template <typename CharT>
struct TStringArrayAppender {
  static void Append(nsTArray<nsTString<CharT>>& aArgs, uint16_t aCount) {
    MOZ_RELEASE_ASSERT(aCount == 0,
                       "Must give at least as many string arguments as are "
                       "required by the ErrNum.");
  }

  // Allow passing nsAString/nsACString instances for our args.
  template <typename... Ts>
  static void Append(nsTArray<nsTString<CharT>>& aArgs, uint16_t aCount,
                     const nsTSubstring<CharT>& aFirst, Ts&&... aOtherArgs) {
    if (aCount == 0) {
      MOZ_ASSERT(false,
                 "There should not be more string arguments provided than are "
                 "required by the ErrNum.");
      return;
    }
    aArgs.AppendElement(aFirst);
    Append(aArgs, aCount - 1, std::forward<Ts>(aOtherArgs)...);
  }

  // Also allow passing literal instances for our args.
  template <int N, typename... Ts>
  static void Append(nsTArray<nsTString<CharT>>& aArgs, uint16_t aCount,
                     const CharT (&aFirst)[N], Ts&&... aOtherArgs) {
    if (aCount == 0) {
      MOZ_ASSERT(false,
                 "There should not be more string arguments provided than are "
                 "required by the ErrNum.");
      return;
    }
    aArgs.AppendElement(nsTLiteralString<CharT>(aFirst));
    Append(aArgs, aCount - 1, std::forward<Ts>(aOtherArgs)...);
  }
};

using StringArrayAppender = TStringArrayAppender<char16_t>;
using CStringArrayAppender = TStringArrayAppender<char>;

}  // namespace dom

class ErrorResult;
class OOMReporter;
class CopyableErrorResult;

namespace binding_danger {

/**
 * Templated implementation class for various ErrorResult-like things.  The
 * instantiations differ only in terms of their cleanup policies (used in the
 * destructor), which they can specify via the template argument.  Note that
 * this means it's safe to reinterpret_cast between the instantiations unless
 * you plan to invoke the destructor through such a cast pointer.
 *
 * A cleanup policy consists of two booleans: whether to assert that we've been
 * reported or suppressed, and whether to then go ahead and suppress the
 * exception.
 */
template <typename CleanupPolicy>
class TErrorResult {
 public:
  TErrorResult()
      : mResult(NS_OK)
#ifdef DEBUG
        ,
        mMightHaveUnreportedJSException(false),
        mUnionState(HasNothing)
#endif
  {
  }

  ~TErrorResult() {
    AssertInOwningThread();

    if (CleanupPolicy::assertHandled) {
      // Consumers should have called one of MaybeSetPendingException
      // (possibly via ToJSValue), StealNSResult, and SuppressException
      AssertReportedOrSuppressed();
    }

    if (CleanupPolicy::suppress) {
      SuppressException();
    }

    // And now assert that we're in a good final state.
    AssertReportedOrSuppressed();
  }

  TErrorResult(TErrorResult&& aRHS)
      // Initialize mResult and whatever else we need to default-initialize, so
      // the ClearUnionData call in our operator= will do the right thing
      // (nothing).
      : TErrorResult() {
    *this = std::move(aRHS);
  }
  TErrorResult& operator=(TErrorResult&& aRHS);

  explicit TErrorResult(nsresult aRv) : TErrorResult() { AssignErrorCode(aRv); }

  // Not to be implemented, to make sure people always pass this by
  // reference, not by value.
  TErrorResult(const TErrorResult&) = delete;
  void operator=(const TErrorResult&) = delete;

  operator ErrorResult&();
  operator const ErrorResult&() const;
  operator OOMReporter&();

  // This method is deprecated.  Consumers should Throw*Error with the
  // appropriate DOMException name if they are throwing a DOMException.  If they
  // have a random nsresult which may or may not correspond to a DOMException
  // type, they should consider using an appropriate DOMException with an
  // informative message and calling the relevant Throw*Error.
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG Throw(nsresult rv) {
    MOZ_ASSERT(NS_FAILED(rv), "Please don't try throwing success");
    ClearUnionData();
    AssignErrorCode(rv);
  }

  // Duplicate our current state on the given TErrorResult object.  Any
  // existing errors or messages on the target will be suppressed before
  // cloning.  Our own error state remains unchanged.
  void CloneTo(TErrorResult& aRv) const;

  // Use SuppressException when you want to suppress any exception that might be
  // on the TErrorResult.  After this call, the TErrorResult will be back a "no
  // exception thrown" state.
  void SuppressException() {
    WouldReportJSException();
    MaybeClearUnionData();
    // We don't use AssignErrorCode, because we want to override existing error
    // states, which AssignErrorCode is not allowed to do.
    mResult = NS_OK;
  }

  // Use StealNSResult() when you want to safely convert the TErrorResult to
  // an nsresult that you will then return to a caller.  This will
  // SuppressException(), since there will no longer be a way to report it.
  nsresult StealNSResult() {
    nsresult rv = ErrorCode();
    SuppressException();
    // Don't propagate out our internal error codes that have special meaning.
    if (NS_ERROR_GET_MODULE(rv) == NS_ERROR_MODULE_ERRORRESULT) {
      // What to pick here?
      return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    return rv;
  }

  // Use MaybeSetPendingException to convert a TErrorResult to a pending
  // exception on the given JSContext.  This is the normal "throw an exception"
  // codepath.
  //
  // The return value is false if the TErrorResult represents success, true
  // otherwise.  This does mean that in JSAPI method implementations you can't
  // just use this as |return rv.MaybeSetPendingException(cx)| (though you could
  // |return !rv.MaybeSetPendingException(cx)|), but in practice pretty much any
  // consumer would want to do some more work on the success codepath.  So
  // instead the way you use this is:
  //
  //   if (rv.MaybeSetPendingException(cx)) {
  //     bail out here
  //   }
  //   go on to do something useful
  //
  // The success path is inline, since it should be the common case and we don't
  // want to pay the price of a function call in some of the consumers of this
  // method in the common case.
  //
  // Note that a true return value does NOT mean there is now a pending
  // exception on aCx, due to uncatchable exceptions.  It should still be
  // considered equivalent to a JSAPI failure in terms of what callers should do
  // after true is returned.
  //
  // After this call, the TErrorResult will no longer return true from Failed(),
  // since the exception will have moved to the JSContext.
  //
  // If "context" is not null and our exception has a useful message string, the
  // string "%s: ", with the value of "context" replacing %s, will be prepended
  // to the message string.  The passed-in string must be ASCII.
  [[nodiscard]] bool MaybeSetPendingException(
      JSContext* cx, const char* description = nullptr) {
    WouldReportJSException();
    if (!Failed()) {
      return false;
    }

    SetPendingException(cx, description);
    return true;
  }

  // Use StealExceptionFromJSContext to convert a pending exception on a
  // JSContext to a TErrorResult.  This function must be called only when a
  // JSAPI operation failed.  It assumes that lack of pending exception on the
  // JSContext means an uncatchable exception was thrown.
  //
  // Codepaths that might call this method must call MightThrowJSException even
  // if the relevant JSAPI calls do not fail.
  //
  // When this function returns, JS_IsExceptionPending(cx) will definitely be
  // false.
  void StealExceptionFromJSContext(JSContext* cx);

  template <dom::ErrNum errorNumber, typename... Ts>
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowTypeError(Ts&&... messageArgs) {
    static_assert(dom::ErrorExceptionType[errorNumber] == JSEXN_TYPEERR,
                  "Throwing a non-TypeError via ThrowTypeError");
    ThrowErrorWithMessage<errorNumber>(NS_ERROR_INTERNAL_ERRORRESULT_TYPEERROR,
                                       std::forward<Ts>(messageArgs)...);
  }

  // To be used when throwing a TypeError with a completely custom
  // message string that's only used in one spot.
  inline void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowTypeError(const nsACString& aMessage) {
    this->template ThrowTypeError<dom::MSG_ONE_OFF_TYPEERR>(aMessage);
  }

  // To be used when throwing a TypeError with a completely custom
  // message string that's a string literal that's only used in one spot.
  template <int N>
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowTypeError(const char (&aMessage)[N]) {
    ThrowTypeError(nsLiteralCString(aMessage));
  }

  template <dom::ErrNum errorNumber, typename... Ts>
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowRangeError(Ts&&... messageArgs) {
    static_assert(dom::ErrorExceptionType[errorNumber] == JSEXN_RANGEERR,
                  "Throwing a non-RangeError via ThrowRangeError");
    ThrowErrorWithMessage<errorNumber>(NS_ERROR_INTERNAL_ERRORRESULT_RANGEERROR,
                                       std::forward<Ts>(messageArgs)...);
  }

  // To be used when throwing a RangeError with a completely custom
  // message string that's only used in one spot.
  inline void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowRangeError(const nsACString& aMessage) {
    this->template ThrowRangeError<dom::MSG_ONE_OFF_RANGEERR>(aMessage);
  }

  // To be used when throwing a RangeError with a completely custom
  // message string that's a string literal that's only used in one spot.
  template <int N>
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowRangeError(const char (&aMessage)[N]) {
    ThrowRangeError(nsLiteralCString(aMessage));
  }

  bool IsErrorWithMessage() const {
    return ErrorCode() == NS_ERROR_INTERNAL_ERRORRESULT_TYPEERROR ||
           ErrorCode() == NS_ERROR_INTERNAL_ERRORRESULT_RANGEERROR;
  }

  // Facilities for throwing a preexisting JS exception value via this
  // TErrorResult.  The contract is that any code which might end up calling
  // ThrowJSException() or StealExceptionFromJSContext() must call
  // MightThrowJSException() even if no exception is being thrown.  Code that
  // conditionally calls ToJSValue on this TErrorResult only if Failed() must
  // first call WouldReportJSException even if this TErrorResult has not failed.
  //
  // The exn argument to ThrowJSException can be in any compartment.  It does
  // not have to be in the compartment of cx.  If someone later uses it, they
  // will wrap it into whatever compartment they're working in, as needed.
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowJSException(JSContext* cx, JS::Handle<JS::Value> exn);
  bool IsJSException() const {
    return ErrorCode() == NS_ERROR_INTERNAL_ERRORRESULT_JS_EXCEPTION;
  }

  // Facilities for throwing DOMExceptions of whatever type a spec calls for.
  // If an empty message string is passed to one of these Throw*Error functions,
  // the default message string for the relevant type of DOMException will be
  // used.  The passed-in string must be UTF-8.
#define DOMEXCEPTION(name, err)                                \
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG Throw##name( \
      const nsACString& aMessage) {                            \
    ThrowDOMException(err, aMessage);                          \
  }                                                            \
                                                               \
  template <int N>                                             \
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG Throw##name( \
      const char (&aMessage)[N]) {                             \
    ThrowDOMException(err, aMessage);                          \
  }

#include "mozilla/dom/DOMExceptionNames.inc"

#undef DOMEXCEPTION

  bool IsDOMException() const {
    return ErrorCode() == NS_ERROR_INTERNAL_ERRORRESULT_DOMEXCEPTION;
  }

  // Flag on the TErrorResult that whatever needs throwing has been
  // thrown on the JSContext already and we should not mess with it.
  // If nothing was thrown, this becomes an uncatchable exception.
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  NoteJSContextException(JSContext* aCx);

  // Check whether the TErrorResult says to just throw whatever is on
  // the JSContext already.
  bool IsJSContextException() const {
    return ErrorCode() == NS_ERROR_INTERNAL_ERRORRESULT_EXCEPTION_ON_JSCONTEXT;
  }

  // Support for uncatchable exceptions.
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG ThrowUncatchableException() {
    Throw(NS_ERROR_UNCATCHABLE_EXCEPTION);
  }
  bool IsUncatchableException() const {
    return ErrorCode() == NS_ERROR_UNCATCHABLE_EXCEPTION;
  }

  void MOZ_ALWAYS_INLINE MightThrowJSException() {
#ifdef DEBUG
    mMightHaveUnreportedJSException = true;
#endif
  }
  void MOZ_ALWAYS_INLINE WouldReportJSException() {
#ifdef DEBUG
    mMightHaveUnreportedJSException = false;
#endif
  }

  // In the future, we can add overloads of Throw that take more
  // interesting things, like strings or DOM exception types or
  // something if desired.

  // Backwards-compat to make conversion simpler.  We don't call
  // Throw() here because people can easily pass success codes to
  // this.  This operator is deprecated and ideally shouldn't be used.
  void operator=(nsresult rv) {
    MaybeClearUnionData();
    AssignErrorCode(rv);
  }

  bool Failed() const { return NS_FAILED(mResult); }

  bool ErrorCodeIs(nsresult rv) const { return mResult == rv; }

  // For use in logging ONLY.
  uint32_t ErrorCodeAsInt() const { return static_cast<uint32_t>(ErrorCode()); }

  bool operator==(const ErrorResult& aRight) const;

 protected:
  nsresult ErrorCode() const { return mResult; }

  // Helper methods for throwing DOMExceptions, for now.  We can try to get rid
  // of these once EME code is fixed to not use them and we decouple
  // DOMExceptions from nsresult.
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowDOMException(nsresult rv, const nsACString& message);

  // Same thing, but using a string literal.
  template <int N>
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG
  ThrowDOMException(nsresult rv, const char (&aMessage)[N]) {
    ThrowDOMException(rv, nsLiteralCString(aMessage));
  }

  // Allow Promise to call the above methods when it really needs to.
  // Unfortunately, we can't have the definition of Promise here, so can't mark
  // just it's MaybeRejectWithDOMException method as a friend.  In any case,
  // hopefully it's all temporary until we sort out the EME bits.
  friend class dom::Promise;

  // Implementation of MaybeSetPendingException for the case when we're a
  // failure result.  See documentation of MaybeSetPendingException for the
  // "context" argument.
  void SetPendingException(JSContext* cx, const char* context);

 private:
#ifdef DEBUG
  enum UnionState {
    HasMessage,
    HasDOMExceptionInfo,
    HasJSException,
    HasNothing
  };
#endif  // DEBUG

  friend struct IPC::ParamTraits<TErrorResult>;
  friend struct IPC::ParamTraits<ErrorResult>;
  friend struct IPC::ParamTraits<CopyableErrorResult>;

  void SerializeErrorResult(IPC::MessageWriter* aWriter) const;
  bool DeserializeErrorResult(IPC::MessageReader* aReader);

  // Helper method that creates a new Message for this TErrorResult,
  // and returns the arguments array from that Message.
  nsTArray<nsCString>& CreateErrorMessageHelper(const dom::ErrNum errorNumber,
                                                nsresult errorType);

  // Helper method to replace invalid UTF-8 characters with the replacement
  // character. aValidUpTo is the number of characters that are known to be
  // valid. The string might be truncated if we encounter an OOM error.
  static void EnsureUTF8Validity(nsCString& aValue, size_t aValidUpTo);

  template <dom::ErrNum errorNumber, typename... Ts>
  void ThrowErrorWithMessage(nsresult errorType, Ts&&... messageArgs) {
#if defined(DEBUG) && (defined(__clang__) || defined(__GNUC__))
    static_assert(dom::ErrorFormatNumArgs[errorNumber] ==
                      sizeof...(messageArgs) +
                          int(dom::ErrorFormatHasContext[errorNumber]),
                  "Pass in the right number of arguments");
#endif

    ClearUnionData();

    nsTArray<nsCString>& messageArgsArray =
        CreateErrorMessageHelper(errorNumber, errorType);
    uint16_t argCount = dom::GetErrorArgCount(errorNumber);
    if (dom::ErrorFormatHasContext[errorNumber]) {
      // Insert an empty string arg at the beginning and reduce our arg count to
      // still be appended accordingly.
      MOZ_ASSERT(argCount > 0,
                 "Must have at least one arg if we have a context!");
      MOZ_ASSERT(messageArgsArray.Length() == 0,
                 "Why do we already have entries in the array?");
      --argCount;
      messageArgsArray.AppendElement();
    }
    dom::CStringArrayAppender::Append(messageArgsArray, argCount,
                                      std::forward<Ts>(messageArgs)...);
    for (nsCString& arg : messageArgsArray) {
      size_t validUpTo = Utf8ValidUpTo(arg);
      if (validUpTo != arg.Length()) {
        EnsureUTF8Validity(arg, validUpTo);
      }
    }
#ifdef DEBUG
    mUnionState = HasMessage;
#endif  // DEBUG
  }

  MOZ_ALWAYS_INLINE void AssertInOwningThread() const {
#ifdef DEBUG
    if (CleanupPolicy::assertSameThread) {
      NS_ASSERT_OWNINGTHREAD(TErrorResult);
    }
#endif
  }

  void AssignErrorCode(nsresult aRv) {
    MOZ_ASSERT(mUnionState == HasNothing);
    MOZ_ASSERT(aRv != NS_ERROR_INTERNAL_ERRORRESULT_TYPEERROR,
               "Use ThrowTypeError()");
    MOZ_ASSERT(aRv != NS_ERROR_INTERNAL_ERRORRESULT_RANGEERROR,
               "Use ThrowRangeError()");
    MOZ_ASSERT(!IsErrorWithMessage(), "Don't overwrite errors with message");
    MOZ_ASSERT(aRv != NS_ERROR_INTERNAL_ERRORRESULT_JS_EXCEPTION,
               "Use ThrowJSException()");
    MOZ_ASSERT(!IsJSException(), "Don't overwrite JS exceptions");
    MOZ_ASSERT(aRv != NS_ERROR_INTERNAL_ERRORRESULT_DOMEXCEPTION,
               "Use Throw*Error for the appropriate DOMException name");
    MOZ_ASSERT(!IsDOMException(), "Don't overwrite DOM exceptions");
    MOZ_ASSERT(aRv != NS_ERROR_XPC_NOT_ENOUGH_ARGS,
               "May need to bring back ThrowNotEnoughArgsError");
    MOZ_ASSERT(aRv != NS_ERROR_INTERNAL_ERRORRESULT_EXCEPTION_ON_JSCONTEXT,
               "Use NoteJSContextException");
    mResult = aRv;
  }

  void ClearMessage();
  void ClearDOMExceptionInfo();

  // ClearUnionData will try to clear the data in our mExtra union.  After this
  // the union may be in an uninitialized state (e.g. mMessage or
  // mDOMExceptionInfo may point to deleted memory, or mJSException may be a
  // JS::Value containing an invalid gcthing) and the caller must either
  // reinitialize it or change mResult to something that will not involve us
  // touching the union anymore.
  void ClearUnionData();

  // The union only holds data when mResult is one of our internal failure
  // codes, so a success result has nothing to clear.
  MOZ_ALWAYS_INLINE void MaybeClearUnionData() {
    AssertInOwningThread();
    if (Failed()) {
      ClearUnionData();
    } else {
      MOZ_ASSERT(mUnionState == HasNothing);
    }
  }

  // Methods for setting various specific kinds of pending exceptions.  See
  // documentation of MaybeSetPendingException for the "context" argument.
  void SetPendingExceptionWithMessage(JSContext* cx, const char* context);
  void SetPendingJSException(JSContext* cx);
  void SetPendingDOMException(JSContext* cx, const char* context);
  void SetPendingGenericErrorException(JSContext* cx);

  MOZ_ALWAYS_INLINE void AssertReportedOrSuppressed() {
    MOZ_ASSERT(!Failed());
    MOZ_ASSERT(!mMightHaveUnreportedJSException);
    MOZ_ASSERT(mUnionState == HasNothing);
  }

  // Special values of mResult:
  // NS_ERROR_INTERNAL_ERRORRESULT_TYPEERROR -- ThrowTypeError() called on us.
  // NS_ERROR_INTERNAL_ERRORRESULT_RANGEERROR -- ThrowRangeError() called on us.
  // NS_ERROR_INTERNAL_ERRORRESULT_JS_EXCEPTION -- ThrowJSException() called
  //                                               on us.
  // NS_ERROR_UNCATCHABLE_EXCEPTION -- ThrowUncatchableException called on us.
  // NS_ERROR_INTERNAL_ERRORRESULT_DOMEXCEPTION -- ThrowDOMException() called
  //                                               on us.
  nsresult mResult;

  struct Message;
  struct DOMExceptionInfo;
  union Extra {
    // mMessage is set by ThrowErrorWithMessage and reported (and deallocated)
    // by SetPendingExceptionWithMessage.
    MOZ_INIT_OUTSIDE_CTOR
    Message* mMessage;  // valid when IsErrorWithMessage()

    // mJSException is set (and rooted) by ThrowJSException and reported (and
    // unrooted) by SetPendingJSException.
    MOZ_INIT_OUTSIDE_CTOR
    JS::Value mJSException;  // valid when IsJSException()

    // mDOMExceptionInfo is set by ThrowDOMException and reported (and
    // deallocated) by SetPendingDOMException.
    MOZ_INIT_OUTSIDE_CTOR
    DOMExceptionInfo* mDOMExceptionInfo;  // valid when IsDOMException()

    // |mJSException| has a non-trivial constructor and therefore MUST be
    // placement-new'd into existence.
    MOZ_PUSH_DISABLE_NONTRIVIAL_UNION_WARNINGS
    Extra() {}  // NOLINT
    MOZ_POP_DISABLE_NONTRIVIAL_UNION_WARNINGS
  } mExtra;

  Message* InitMessage(Message* aMessage) {
    // The |new| here switches the active arm of |mExtra|, from the compiler's
    // point of view.  Mere assignment *won't* necessarily do the right thing!
    new (&mExtra.mMessage) Message*(aMessage);
    return mExtra.mMessage;
  }

  JS::Value& InitJSException() {
    // The |new| here switches the active arm of |mExtra|, from the compiler's
    // point of view.  Mere assignment *won't* necessarily do the right thing!
    new (&mExtra.mJSException) JS::Value();  // sets to undefined
    return mExtra.mJSException;
  }

  DOMExceptionInfo* InitDOMExceptionInfo(DOMExceptionInfo* aDOMExceptionInfo) {
    // The |new| here switches the active arm of |mExtra|, from the compiler's
    // point of view.  Mere assignment *won't* necessarily do the right thing!
    new (&mExtra.mDOMExceptionInfo) DOMExceptionInfo*(aDOMExceptionInfo);
    return mExtra.mDOMExceptionInfo;
  }

#ifdef DEBUG
  // Used to keep track of codepaths that might throw JS exceptions,
  // for assertion purposes.
  bool mMightHaveUnreportedJSException;

  // Used to keep track of what's stored in our union right now.  Note
  // that this may be set to HasNothing even if our mResult suggests
  // we should have something, if we have already cleaned up the
  // something.
  UnionState mUnionState;

  // The thread that created this TErrorResult
  NS_DECL_OWNINGTHREAD;
#endif
} JS_HAZ_ROOTED;

struct JustAssertCleanupPolicy {
  static const bool assertHandled = true;
  static const bool suppress = false;
  static const bool assertSameThread = true;
};

struct AssertAndSuppressCleanupPolicy {
  static const bool assertHandled = true;
  static const bool suppress = true;
  static const bool assertSameThread = true;
};

struct JustSuppressCleanupPolicy {
  static const bool assertHandled = false;
  static const bool suppress = true;
  static const bool assertSameThread = true;
};

struct ThreadSafeJustSuppressCleanupPolicy {
  static const bool assertHandled = false;
  static const bool suppress = true;
  static const bool assertSameThread = false;
};

}  // namespace binding_danger

// A class people should normally use on the stack when they plan to actually
// do something with the exception.
class ErrorResult : public binding_danger::TErrorResult<
                        binding_danger::AssertAndSuppressCleanupPolicy> {
  typedef binding_danger::TErrorResult<
      binding_danger::AssertAndSuppressCleanupPolicy>
      BaseErrorResult;

 public:
  ErrorResult() = default;

  ErrorResult(ErrorResult&& aRHS) = default;
  // Explicitly allow moving out of a CopyableErrorResult into an ErrorResult.
  // This is implemented below so it can see the definition of
  // CopyableErrorResult.
  inline explicit ErrorResult(CopyableErrorResult&& aRHS);

  explicit ErrorResult(nsresult aRv) : BaseErrorResult(aRv) {}

  // This operator is deprecated and ideally shouldn't be used.
  void operator=(nsresult rv) { BaseErrorResult::operator=(rv); }

  ErrorResult& operator=(ErrorResult&& aRHS) = default;

  // Not to be implemented, to make sure people always pass this by
  // reference, not by value.
  ErrorResult(const ErrorResult&) = delete;
  ErrorResult& operator=(const ErrorResult&) = delete;
};

template <typename CleanupPolicy>
binding_danger::TErrorResult<CleanupPolicy>::operator ErrorResult&() {
  return *static_cast<ErrorResult*>(
      reinterpret_cast<TErrorResult<AssertAndSuppressCleanupPolicy>*>(this));
}

template <typename CleanupPolicy>
binding_danger::TErrorResult<CleanupPolicy>::operator const ErrorResult&()
    const {
  return *static_cast<const ErrorResult*>(
      reinterpret_cast<const TErrorResult<AssertAndSuppressCleanupPolicy>*>(
          this));
}

// A class for use when an ErrorResult should just automatically be ignored.
// This doesn't inherit from ErrorResult so we don't make two separate calls to
// SuppressException.
class IgnoredErrorResult : public binding_danger::TErrorResult<
                               binding_danger::JustSuppressCleanupPolicy> {};

// A class for use when an ErrorResult needs to be copied to a lambda, into
// an IPDL structure, etc.  Since this will often involve crossing thread
// boundaries this class will assert if you try to copy a JS exception.  Only
// use this if you are propagating internal errors.  In general its best
// to use ErrorResult by default and only convert to a CopyableErrorResult when
// you need it.
class CopyableErrorResult
    : public binding_danger::TErrorResult<
          binding_danger::ThreadSafeJustSuppressCleanupPolicy> {
  typedef binding_danger::TErrorResult<
      binding_danger::ThreadSafeJustSuppressCleanupPolicy>
      BaseErrorResult;

 public:
  CopyableErrorResult() = default;

  explicit CopyableErrorResult(const ErrorResult& aRight) : BaseErrorResult() {
    auto val = reinterpret_cast<const CopyableErrorResult&>(aRight);
    operator=(val);
  }

  CopyableErrorResult(CopyableErrorResult&& aRHS) = default;

  explicit CopyableErrorResult(ErrorResult&& aRHS) : BaseErrorResult() {
    // We must not copy JS exceptions since it can too easily lead to
    // off-thread use.  Assert this and fall back to a generic error
    // in release builds.
    MOZ_DIAGNOSTIC_ASSERT(
        !aRHS.IsJSException(),
        "Attempt to copy from ErrorResult with a JS exception value.");
    if (aRHS.IsJSException()) {
      aRHS.SuppressException();
      Throw(NS_ERROR_FAILURE);
    } else {
      // We could avoid the cast here if we had a move constructor on
      // TErrorResult templated on the cleanup policy type, but then we'd have
      // to either inline the impl or force all possible instantiations or
      // something.  This is a bit simpler, and not that different from our copy
      // constructor.
      auto val = reinterpret_cast<CopyableErrorResult&&>(aRHS);
      operator=(val);
    }
  }

  explicit CopyableErrorResult(nsresult aRv) : BaseErrorResult(aRv) {}

  // This operator is deprecated and ideally shouldn't be used.
  void operator=(nsresult rv) { BaseErrorResult::operator=(rv); }

  CopyableErrorResult& operator=(CopyableErrorResult&& aRHS) = default;

  CopyableErrorResult(const CopyableErrorResult& aRight) : BaseErrorResult() {
    operator=(aRight);
  }

  CopyableErrorResult& operator=(const CopyableErrorResult& aRight) {
    // We must not copy JS exceptions since it can too easily lead to
    // off-thread use.  Assert this and fall back to a generic error
    // in release builds.
    MOZ_DIAGNOSTIC_ASSERT(
        !IsJSException(),
        "Attempt to copy to ErrorResult with a JS exception value.");
    MOZ_DIAGNOSTIC_ASSERT(
        !aRight.IsJSException(),
        "Attempt to copy from ErrorResult with a JS exception value.");
    if (aRight.IsJSException()) {
      SuppressException();
      Throw(NS_ERROR_FAILURE);
    } else {
      aRight.CloneTo(*this);
    }
    return *this;
  }

  // Disallow implicit converstion to non-const ErrorResult&, because that would
  // allow people to throw exceptions on us while bypassing our checks for JS
  // exceptions.
  operator ErrorResult&() = delete;

  // Allow conversion to ErrorResult&& so we can move out of ourselves into
  // an ErrorResult.
  operator ErrorResult&&() && {
    auto* val = reinterpret_cast<ErrorResult*>(this);
    return std::move(*val);
  }
};

inline ErrorResult::ErrorResult(CopyableErrorResult&& aRHS)
    : ErrorResult(reinterpret_cast<ErrorResult&&>(aRHS)) {}

namespace dom::binding_detail {

enum class ErrorFor {
  getter,
  setter,
};

template <ErrorFor ErrorType>
struct ErrorDescriptionFor {
  const char* mInterface;
  const char* mMember;
};

class FastErrorResult : public mozilla::binding_danger::TErrorResult<
                            mozilla::binding_danger::JustAssertCleanupPolicy> {
 public:
  using TErrorResult::MaybeSetPendingException;

  template <ErrorFor ErrorType>
  [[nodiscard]] bool MaybeSetPendingException(
      JSContext* aCx, const ErrorDescriptionFor<ErrorType>& aDescription) {
    WouldReportJSException();
    if (!Failed()) {
      return false;
    }

    nsAutoCString description(aDescription.mInterface);
    description.Append('.');
    description.Append(aDescription.mMember);
    if constexpr (ErrorType == ErrorFor::getter) {
      description.AppendLiteral(" getter");
    } else {
      static_assert(ErrorType == ErrorFor::setter);
      description.AppendLiteral(" setter");
    }
    SetPendingException(aCx, description.get());
    return true;
  }
};

}  // namespace dom::binding_detail

// We want an OOMReporter class that has the following properties:
//
// 1) Can be cast to from any ErrorResult-like type.
// 2) Has a fast destructor (because we want to use it from bindings).
// 3) Won't be randomly instantiated by non-binding code (because the fast
//    destructor is not so safe).
// 4) Doesn't look ugly on the callee side (e.g. isn't in the binding_detail or
//    binding_danger namespace).
//
// We do this by creating a class that can't actually be constructed directly
// but can be cast to from ErrorResult-like types, both implicitly and
// explicitly.
class OOMReporter : private dom::binding_detail::FastErrorResult {
 public:
  void MOZ_MUST_RETURN_FROM_CALLER_IF_THIS_IS_ARG ReportOOM() {
    Throw(NS_ERROR_OUT_OF_MEMORY);
  }

  // A method that turns a FastErrorResult into an OOMReporter, which we use in
  // codegen to ensure that callees don't take an ErrorResult when they should
  // only be taking an OOMReporter.  The idea is that we can then just have a
  // FastErrorResult on the stack and call this to produce the thing to pass to
  // callees.
  static OOMReporter& From(FastErrorResult& aRv) { return aRv; }

 private:
  // TErrorResult is a friend so its |operator OOMReporter&()| can work.
  template <typename CleanupPolicy>
  friend class binding_danger::TErrorResult;

  OOMReporter() : dom::binding_detail::FastErrorResult() {}
};

template <typename CleanupPolicy>
binding_danger::TErrorResult<CleanupPolicy>::operator OOMReporter&() {
  return *static_cast<OOMReporter*>(
      reinterpret_cast<TErrorResult<JustAssertCleanupPolicy>*>(this));
}

// A class for use when an ErrorResult should just automatically be
// ignored.  This is designed to be passed as a temporary only, like
// so:
//
//    foo->Bar(IgnoreErrors());
class MOZ_TEMPORARY_CLASS IgnoreErrors {
 public:
  operator ErrorResult&() && { return mInner; }
  operator OOMReporter&() && { return mInner; }

 private:
  // We don't use an ErrorResult member here so we don't make two separate calls
  // to SuppressException (one from us, one from the ErrorResult destructor
  // after asserting).
  binding_danger::TErrorResult<binding_danger::JustSuppressCleanupPolicy>
      mInner;
} JS_HAZ_ROOTED;

/******************************************************************************
 ** Macros for checking results
 ******************************************************************************/

#define RETURN_NSRESULT_ON_FAILURE(res)                                        \
  do {                                                                         \
    (res).WouldReportJSException();                                            \
    if ((res).Failed()) {                                                      \
      NS_WARNING(nsPrintfCString(                                              \
                     "RETURN_NSRESULT_ON_FAILURE(%s) failed with result 0x%X", \
                     #res, (res).ErrorCodeAsInt())                             \
                     .get());                                                  \
      return (res).StealNSResult();                                            \
    }                                                                          \
  } while (0)

}  // namespace mozilla

#endif /* mozilla_ErrorResult_h */
