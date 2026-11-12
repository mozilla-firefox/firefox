/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/InlinableNatives.h"

#ifdef JS_HAS_INTL_API
#  include "builtin/intl/Segmenter.h"
#endif
#include "builtin/AsyncDisposableStackObject.h"
#include "builtin/DisposableStackObject.h"
#include "builtin/MapObject.h"
#include "js/experimental/JitInfo.h"
#include "vm/ArrayBufferObject.h"
#include "vm/AsyncIteration.h"
#include "vm/Iteration.h"
#include "vm/SharedArrayObject.h"

using namespace js;
using namespace js::jit;

#define ADD_NATIVE(native)                   \
  const JSJitInfo js::jit::JitInfo_##native{ \
      {nullptr},                             \
      {uint16_t(InlinableNative::native)},   \
      {0},                                   \
      JSJitInfo::InlinableNative};
INLINABLE_NATIVE_LIST(ADD_NATIVE)
#undef ADD_NATIVE

const JSClass* js::jit::InlinableNativeGuardToClass(InlinableNative native) {
  switch (native) {
#ifdef JS_HAS_INTL_API
    // Intl natives.
    case InlinableNative::IntlGuardToSegments:
      return &intl::SegmentsObject::class_;
    case InlinableNative::IntlGuardToSegmentIterator:
      return &intl::SegmentIteratorObject::class_;
#else
    case InlinableNative::IntlGuardToSegments:
    case InlinableNative::IntlGuardToSegmentIterator:
      MOZ_CRASH("Intl API disabled");
#endif

    // Utility intrinsics.
    case InlinableNative::IntrinsicGuardToArrayIterator:
      return &ArrayIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToMapIterator:
      return &MapIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToSetIterator:
      return &SetIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToStringIterator:
      return &StringIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToRegExpStringIterator:
      return &RegExpStringIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToWrapForValidIterator:
      return &WrapForValidIteratorObject::class_;
    case InlinableNative::IntrinsicGuardToIteratorHelper:
      return &IteratorHelperObject::class_;
#ifdef NIGHTLY_BUILD
    case InlinableNative::IntrinsicGuardToIteratorRange:
      return &IteratorRangeObject::class_;
#endif
    case InlinableNative::IntrinsicGuardToAsyncIteratorHelper:
      return &AsyncIteratorHelperObject::class_;
    case InlinableNative::IntrinsicGuardToAsyncDisposableStack:
      return &AsyncDisposableStackObject::class_;
    case InlinableNative::IntrinsicGuardToDisposableStack:
      return &DisposableStackObject::class_;

    case InlinableNative::IntrinsicGuardToMapObject:
      return &MapObject::class_;
    case InlinableNative::IntrinsicGuardToSetObject:
      return &SetObject::class_;
    case InlinableNative::IntrinsicGuardToArrayBuffer:
      return &FixedLengthArrayBufferObject::class_;
    case InlinableNative::IntrinsicGuardToSharedArrayBuffer:
      return &FixedLengthSharedArrayBufferObject::class_;

    default:
      MOZ_CRASH("Not a GuardTo instruction");
  }
}

#define NATIVE_STRING_CASE(native) \
  case InlinableNative::native:    \
    return #native;

const char* js::jit::InlinableNativeToString(InlinableNative native) {
  switch (native) {
    INLINABLE_NATIVE_LIST(NATIVE_STRING_CASE)

    case InlinableNative::Limit:
      break;
  }

  MOZ_CRASH("Unknown native");
}

#undef NATIVE_STRING_CASE

// Returns true if |native| can be inlined cross-realm. Especially inlined
// natives that can allocate objects or throw exceptions shouldn't be inlined
// cross-realm without a careful analysis because we might use the wrong realm!
//
// Note that self-hosting intrinsics are never called cross-realm. See the
// MOZ_CRASH below.
//
// If you are adding a new inlinable native, the safe thing is to |return false|
// here.
bool js::jit::CanInlineNativeCrossRealm(InlinableNative native) {
  switch (native) {
    case InlinableNative::MathAbs:
    case InlinableNative::MathFloor:
    case InlinableNative::MathCeil:
    case InlinableNative::MathRound:
    case InlinableNative::MathClz32:
    case InlinableNative::MathSqrt:
    case InlinableNative::MathATan2:
    case InlinableNative::MathHypot:
    case InlinableNative::MathMax:
    case InlinableNative::MathMin:
    case InlinableNative::MathPow:
    case InlinableNative::MathImul:
    case InlinableNative::MathFRound:
    case InlinableNative::MathF16Round:
    case InlinableNative::MathTrunc:
    case InlinableNative::MathSign:
    case InlinableNative::MathSin:
    case InlinableNative::MathTan:
    case InlinableNative::MathCos:
    case InlinableNative::MathExp:
    case InlinableNative::MathLog:
    case InlinableNative::MathASin:
    case InlinableNative::MathATan:
    case InlinableNative::MathACos:
    case InlinableNative::MathLog10:
    case InlinableNative::MathLog2:
    case InlinableNative::MathLog1P:
    case InlinableNative::MathExpM1:
    case InlinableNative::MathCosH:
    case InlinableNative::MathSinH:
    case InlinableNative::MathTanH:
    case InlinableNative::MathACosH:
    case InlinableNative::MathASinH:
    case InlinableNative::MathATanH:
    case InlinableNative::MathCbrt:
    case InlinableNative::Boolean:
      return true;

    case InlinableNative::Array:
      // Cross-realm case handled by inlineArray.
      return true;

    case InlinableNative::MathRandom:
      // RNG state is per-realm.
      return false;

    case InlinableNative::IntlGuardToSegments:
    case InlinableNative::IntlGuardToSegmentIterator:
    case InlinableNative::IsRegExpObject:
    case InlinableNative::IsOptimizableRegExpObject:
    case InlinableNative::IsPossiblyWrappedRegExpObject:
    case InlinableNative::IsRegExpPrototypeOptimizable:
    case InlinableNative::RegExpMatcher:
    case InlinableNative::RegExpSearcher:
    case InlinableNative::RegExpSearcherLastLimit:
    case InlinableNative::RegExpHasCaptureGroups:
    case InlinableNative::GetFirstDollarIndex:
    case InlinableNative::IntrinsicNewArrayIterator:
    case InlinableNative::IntrinsicNewStringIterator:
    case InlinableNative::IntrinsicNewRegExpStringIterator:
    case InlinableNative::IntrinsicStringReplaceString:
    case InlinableNative::IntrinsicStringSplitString:
    case InlinableNative::IntrinsicUnsafeSetReservedSlot:
    case InlinableNative::IntrinsicUnsafeGetReservedSlot:
    case InlinableNative::IntrinsicUnsafeGetObjectFromReservedSlot:
    case InlinableNative::IntrinsicUnsafeGetInt32FromReservedSlot:
    case InlinableNative::IntrinsicUnsafeGetStringFromReservedSlot:
    case InlinableNative::IntrinsicIsCallable:
    case InlinableNative::IntrinsicIsConstructor:
    case InlinableNative::IntrinsicToObject:
    case InlinableNative::IntrinsicIsObject:
    case InlinableNative::IntrinsicIsCrossRealmArrayConstructor:
    case InlinableNative::IntrinsicCanOptimizeArraySpecies:
    case InlinableNative::IntrinsicCanOptimizeRegExpSpecies:
    case InlinableNative::IntrinsicToInteger:
    case InlinableNative::IntrinsicToLength:
    case InlinableNative::IntrinsicIsConstructing:
    case InlinableNative::IntrinsicIsSuspendedGenerator:
    case InlinableNative::IntrinsicSubstringKernel:
    case InlinableNative::IntrinsicGuardToArrayIterator:
    case InlinableNative::IntrinsicGuardToMapIterator:
    case InlinableNative::IntrinsicGuardToSetIterator:
    case InlinableNative::IntrinsicGuardToStringIterator:
    case InlinableNative::IntrinsicGuardToRegExpStringIterator:
    case InlinableNative::IntrinsicGuardToWrapForValidIterator:
    case InlinableNative::IntrinsicGuardToIteratorHelper:
    case InlinableNative::IntrinsicGuardToAsyncIteratorHelper:
#ifdef NIGHTLY_BUILD
    case InlinableNative::IntrinsicGuardToIteratorRange:
#endif
    case InlinableNative::IntrinsicIsPackedArray:
    case InlinableNative::IntrinsicGuardToMapObject:
    case InlinableNative::IntrinsicGetNextMapEntryForIterator:
    case InlinableNative::IntrinsicGuardToSetObject:
    case InlinableNative::IntrinsicGetNextSetEntryForIterator:
    case InlinableNative::IntrinsicGuardToArrayBuffer:
    case InlinableNative::IntrinsicGuardToSharedArrayBuffer:
    case InlinableNative::IntrinsicIsTypedArrayConstructor:
    case InlinableNative::IntrinsicIsTypedArray:
    case InlinableNative::IntrinsicIsPossiblyWrappedTypedArray:
    case InlinableNative::IntrinsicPossiblyWrappedTypedArrayLength:
    case InlinableNative::IntrinsicRegExpBuiltinExec:
    case InlinableNative::IntrinsicRegExpBuiltinExecForTest:
    case InlinableNative::IntrinsicRegExpExec:
    case InlinableNative::IntrinsicRegExpExecForTest:
    case InlinableNative::IntrinsicTypedArrayLength:
    case InlinableNative::IntrinsicArrayIteratorPrototypeOptimizable:
    case InlinableNative::IntrinsicGuardToAsyncDisposableStack:
    case InlinableNative::IntrinsicGuardToDisposableStack:
      MOZ_CRASH("Unexpected cross-realm intrinsic call");

    case InlinableNative::TestBailout:
    case InlinableNative::TestAssertFloat32:
    case InlinableNative::TestAssertRecoveredOnBailout:
      // Testing functions, not worth inlining cross-realm.
      return false;

    case InlinableNative::ArrayIsArray:
    case InlinableNative::ArrayJoin:
    case InlinableNative::ArrayPop:
    case InlinableNative::ArrayShift:
    case InlinableNative::ArrayPush:
    case InlinableNative::ArraySlice:
    case InlinableNative::ArrayBufferByteLength:
    case InlinableNative::AtomicsCompareExchange:
    case InlinableNative::AtomicsExchange:
    case InlinableNative::AtomicsLoad:
    case InlinableNative::AtomicsStore:
    case InlinableNative::AtomicsAdd:
    case InlinableNative::AtomicsSub:
    case InlinableNative::AtomicsAnd:
    case InlinableNative::AtomicsOr:
    case InlinableNative::AtomicsXor:
    case InlinableNative::AtomicsIsLockFree:
    case InlinableNative::AtomicsPause:
    case InlinableNative::BigInt:
    case InlinableNative::BigIntAsIntN:
    case InlinableNative::BigIntAsUintN:
    case InlinableNative::DataViewGetInt8:
    case InlinableNative::DataViewGetUint8:
    case InlinableNative::DataViewGetInt16:
    case InlinableNative::DataViewGetUint16:
    case InlinableNative::DataViewGetInt32:
    case InlinableNative::DataViewGetUint32:
    case InlinableNative::DataViewGetFloat16:
    case InlinableNative::DataViewGetFloat32:
    case InlinableNative::DataViewGetFloat64:
    case InlinableNative::DataViewGetBigInt64:
    case InlinableNative::DataViewGetBigUint64:
    case InlinableNative::DataViewSetInt8:
    case InlinableNative::DataViewSetUint8:
    case InlinableNative::DataViewSetInt16:
    case InlinableNative::DataViewSetUint16:
    case InlinableNative::DataViewSetInt32:
    case InlinableNative::DataViewSetUint32:
    case InlinableNative::DataViewSetFloat16:
    case InlinableNative::DataViewSetFloat32:
    case InlinableNative::DataViewSetFloat64:
    case InlinableNative::DataViewSetBigInt64:
    case InlinableNative::DataViewSetBigUint64:
    case InlinableNative::DataViewByteLength:
    case InlinableNative::DataViewByteOffset:
    case InlinableNative::Date:
    case InlinableNative::DateGetTime:
    case InlinableNative::DateGetFullYear:
    case InlinableNative::DateGetMonth:
    case InlinableNative::DateGetDate:
    case InlinableNative::DateGetDay:
    case InlinableNative::DateGetHours:
    case InlinableNative::DateGetMinutes:
    case InlinableNative::DateGetSeconds:
    case InlinableNative::DateNow:
    case InlinableNative::DateParse:
    case InlinableNative::DurationYears:
    case InlinableNative::DurationMonths:
    case InlinableNative::DurationWeeks:
    case InlinableNative::DurationDays:
    case InlinableNative::DurationHours:
    case InlinableNative::DurationMinutes:
    case InlinableNative::DurationSeconds:
    case InlinableNative::DurationMilliseconds:
    case InlinableNative::DurationMicroseconds:
    case InlinableNative::DurationNanoseconds:
    case InlinableNative::PlainTimeHour:
    case InlinableNative::PlainTimeMinute:
    case InlinableNative::PlainTimeSecond:
    case InlinableNative::PlainTimeMillisecond:
    case InlinableNative::PlainTimeMicrosecond:
    case InlinableNative::PlainTimeNanosecond:
    case InlinableNative::PlainDateTimeHour:
    case InlinableNative::PlainDateTimeMinute:
    case InlinableNative::PlainDateTimeSecond:
    case InlinableNative::PlainDateTimeMillisecond:
    case InlinableNative::PlainDateTimeMicrosecond:
    case InlinableNative::PlainDateTimeNanosecond:
    case InlinableNative::InstantEpochMilliseconds:
    case InlinableNative::ZonedDateTimeEpochMilliseconds:
    case InlinableNative::FunctionBind:
    case InlinableNative::MapConstructor:
    case InlinableNative::MapGet:
    case InlinableNative::MapHas:
    case InlinableNative::MapDelete:
    case InlinableNative::MapSet:
    case InlinableNative::MapSize:
    case InlinableNative::Number:
    case InlinableNative::NumberParseInt:
    case InlinableNative::NumberToString:
    case InlinableNative::ReflectGetPrototypeOf:
    case InlinableNative::RegExpDotAll:
    case InlinableNative::RegExpGlobal:
    case InlinableNative::RegExpHasIndices:
    case InlinableNative::RegExpIgnoreCase:
    case InlinableNative::RegExpMultiline:
    case InlinableNative::RegExpSticky:
    case InlinableNative::RegExpUnicode:
    case InlinableNative::RegExpUnicodeSets:
    case InlinableNative::SetConstructor:
    case InlinableNative::SetHas:
    case InlinableNative::SetDelete:
    case InlinableNative::SetAdd:
    case InlinableNative::SetSize:
    case InlinableNative::SharedArrayBufferByteLength:
    case InlinableNative::String:
    case InlinableNative::StringToString:
    case InlinableNative::StringValueOf:
    case InlinableNative::StringCharCodeAt:
    case InlinableNative::StringCodePointAt:
    case InlinableNative::StringFromCharCode:
    case InlinableNative::StringFromCodePoint:
    case InlinableNative::StringCharAt:
    case InlinableNative::StringAt:
    case InlinableNative::StringIncludes:
    case InlinableNative::StringIndexOf:
    case InlinableNative::StringLastIndexOf:
    case InlinableNative::StringStartsWith:
    case InlinableNative::StringEndsWith:
    case InlinableNative::StringToLowerCase:
    case InlinableNative::StringToUpperCase:
    case InlinableNative::StringToLocaleLowerCase:
    case InlinableNative::StringToLocaleUpperCase:
    case InlinableNative::StringTrim:
    case InlinableNative::StringTrimStart:
    case InlinableNative::StringTrimEnd:
    case InlinableNative::Object:
    case InlinableNative::ObjectCreate:
    case InlinableNative::ObjectIs:
    case InlinableNative::ObjectIsPrototypeOf:
    case InlinableNative::ObjectKeys:
    case InlinableNative::ObjectToString:
    case InlinableNative::TypedArrayConstructor:
    case InlinableNative::TypedArrayFill:
    case InlinableNative::TypedArraySet:
    case InlinableNative::TypedArraySubarray:
    case InlinableNative::TypedArrayLength:
    case InlinableNative::TypedArrayByteLength:
    case InlinableNative::TypedArrayByteOffset:
    case InlinableNative::WeakMapGet:
    case InlinableNative::WeakMapHas:
    case InlinableNative::WeakSetHas:
#ifdef FUZZING_JS_FUZZILLI
    case InlinableNative::FuzzilliHash:
#endif
      // Default to false for most natives.
      return false;

    case InlinableNative::Limit:
      break;
  }
  MOZ_CRASH("Unknown native");
}
