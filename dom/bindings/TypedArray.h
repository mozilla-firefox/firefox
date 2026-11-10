/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TypedArray_h
#define mozilla_dom_TypedArray_h

#include <type_traits>
#include <utility>

#include "js/ArrayBuffer.h"
#include "js/ArrayBufferMaybeShared.h"
#include "js/Context.h"
#include "js/GCAPI.h"       // JS::AutoCheckCannotGC
#include "js/RootingAPI.h"  // JS::Rooted
#include "js/ScalarType.h"  // JS::Scalar::Type
#include "js/SharedArrayBuffer.h"
#include "js/experimental/TypedData.h"  // js::Unwrap(Ui|I)nt(8|16|32)Array, js::Get(Ui|I)nt(8|16|32)ArrayLengthAndData, js::UnwrapUint8ClampedArray, js::GetUint8ClampedArrayLengthAndData, js::UnwrapFloat(32|64)Array, js::GetFloat(32|64)ArrayLengthAndData, JS_GetArrayBufferViewType
#include "js/friend/ErrorMessages.h"
#include "mozilla/Attributes.h"
#include "mozilla/Buffer.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Vector.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SpiderMonkeyInterface.h"
#include "nsIGlobalObject.h"
#include "nsWrapperCache.h"
#include "nsWrapperCacheInlines.h"

namespace mozilla::dom {

/*
 * Various typed array classes for argument conversion.  We have a base class
 * that has a way of initializing a TypedArray from an existing typed array, and
 * a subclass of the base class that supports creation of a relevant typed array
 * or array buffer object.
 *
 * Accessing the data of a TypedArray is tricky. The underlying storage is in a
 * JS object, which is subject to the JS GC. The memory for the array can be
 * either inline in the JSObject or stored separately. If the data is stored
 * inline then the exact location in memory of the data buffer can change as a
 * result of a JS GC (which is a moving GC). Running JS code can also mutate the
 * data (including the length). For all these reasons one has to be careful when
 * holding a pointer to the data or keeping a local copy of the length value. On
 * the other hand, most code that takes a TypedArray has to access its data at
 * some point, to process it in some way. The TypedArray class tries to supply a
 * number of helper APIs, so that the most common cases of processing the data
 * can be done safely, without having to check the caller very closely for
 * potential security issues. The main classes of processing TypedArray data
 * are:
 *
 *  1) Appending a copy of the data (or of a subset of the data) to a different
 *     data structure
 *  2) Copying the data (or a subset of the data) into a different data
 *     structure
 *  3) Creating a new data structure with a copy of the data (or of a subset of
 *     the data)
 *  4) Processing the data in some other way
 *
 * The APIs for the first 3 classes all return a boolean and take an optional
 * argument named aCalculator. aCalculator should be a lambda taking a size_t
 * argument which will be passed the total length of the data in the typed
 * array. aCalculator is allowed to return a std::pair<size_t, size_t> or a
 * Maybe<std::pair<size_t, size_t>>. The return value should contain the offset
 * and the length of the subset of the data that should be appended, copied or
 * used for creating a new datastructure. If the calculation can fail then
 * aCalculator should return a Maybe<std::pair<size_t, size_t>>, with Nothing()
 * signaling that the operation should be aborted.
 * The return value of these APIs will be false if appending, copying or
 * creating a structure with the new data failed, or if the optional aCalculator
 * lambda returned Nothing().
 *
 * Here are the different APIs:
 *
 *  1) Appending to a different data structure
 *
 *     There are AppendDataTo helpers for nsCString, nsTArray<T>,
 *     FallibleTArray<T> and Vector<T>. The signatures are:
 *
 *       template <typename... Calculator>
 *       [[nodiscard]] bool AppendDataTo(nsCString& aResult, Calculator&&...
 *                                       aCalculator) const;
 *
 *       template <typename T, typename... Calculator>
 *       [[nodiscard]] bool AppendDataTo(nsTArray<T>& aResult, Calculator&&...
 *                                       aCalculator) const;
 *
 *       template <typename T, typename... Calculator>
 *       [[nodiscard]] bool AppendDataTo(FallibleTArray<T>& aResult,
 *                                       Calculator&&... aCalculator) const;
 *
 *       template <typename T, typename... Calculator>
 *       [[nodiscard]] bool AppendDataTo(Vector<T>& aResult, Calculator&&...
 *                                       aCalculator) const;
 *
 *     The data (or the calculated subset) will be appended to aResult by using
 *     the appropriate fallible API. If the append fails then AppendDataTo will
 *     return false. The aCalculator optional argument is described above.
 *
 *     Examples:
 *
 *       Vector<uint32_t> array;
 *       if (!aUint32Array.AppendDataTo(array)) {
 *         aError.ThrowTypeError("Failed to copy data from typed array");
 *         return;
 *       }
 *
 *       size_t offset, length;
 *       … // Getting offset and length values from somewhere.
 *       FallibleTArray<float> array;
 *       if (!aFloat32Array.AppendDataTo(array, [&](const size_t& aLength) {
 *           size_t dataLength = std::min(aLength - offset, length);
 *           return std::make_pair(offset, dataLength);
 *         }) {
 *         aError.ThrowTypeError("Failed to copy data from typed array");
 *         return;
 *       }
 *
 *       size_t offset, length;
 *       … // Getting offset and length values from somewhere.
 *       FallibleTArray<float> array;
 *       if (!aFloat32Array.AppendDataTo(array, [&](const size_t& aLength) {
 *         if (aLength < offset + length) {
 *           return Maybe<std::pair<size_t, size_t>>();
 *         }
 *         size_t dataLength = std::min(aLength - offset, length);
 *         return Some(std::make_pair(offset, dataLength));
 *       })) {
 *         aError.ThrowTypeError("Failed to copy data from typed array");
 *         return;
 *       }
 *
 *
 *  2) Copying into a different data structure
 *
 *     There is a CopyDataTo helper for a fixed-size buffer. The signature is:
 *
 *       template <typename T, size_t N, typename... Calculator>
 *       [[nodiscard]] bool CopyDataTo(T (&aResult)[N],
 *                                     Calculator&&... aCalculator) const;
 *
 *     The data (or the calculated subset) will be copied to aResult, starting
 *     at aResult[0]. If the length of the data to be copied is bigger than the
 *     size of the fixed-size buffer (N) then nothing will be copied and
 *     CopyDataTo will return false. The aCalculator optional argument is
 *     described above.
 *
 *     Examples:
 *
 *       float data[3];
 *       if (!aFloat32Array.CopyDataTo(data)) {
 *         aError.ThrowTypeError("Typed array doesn't contain the right amount"
 *                               "of data");
 *         return;
 *       }
 *
 *       size_t offset;
 *       … // Getting offset value from somewhere.
 *       uint32_t data[3];
 *       if (!aUint32Array.CopyDataTo(data, [&](const size_t& aLength) {
 *         if (aLength - offset != std::size(data)) {
 *           aError.ThrowTypeError("Typed array doesn't contain the right"
 *                                 " amount of data");
 *           return Maybe<std::pair<size_t, size_t>>();
 *         }
 *         return Some(std::make_pair(offset, std::size(data)));
 *       }) {
 *         return;
 *       }
 *
 *  3) Creating a new data structure with a copy of the data (or a subset of the
 *     data)
 *
 *     There are CreateFromData helper for creating a Vector<element_type>, a
 *     UniquePtr<element_type[]> and a Buffer<element_type>.
 *
 *       template <typename... Calculator>
 *       [[nodiscard]] Maybe<Vector<element_type>>
 *       CreateFromData<Vector<element_type>>(
 *           Calculator&&... aCalculator) const;
 *
 *       template <typename... Calculator>
 *       [[nodiscard]] Maybe<UniquePtr<element_type[]>>
 *       CreateFromData<UniquePtr<element_type[]>>(
 *           Calculator&&... aCalculator) const;
 *
 *       template <typename... Calculator>
 *       [[nodiscard]] Maybe<Buffer<element_type>>
 *       CreateFromData<Buffer<element_type>>(
 *           Calculator&&... aCalculator) const;
 *
 *     A new container will be created, and the data (or the calculated subset)
 *     will be copied to it. The container will be returned inside a Maybe<…>.
 *     If creating the container with the right size fails then Nothing() will
 *     be returned. The aCalculator optional argument is described above.
 *
 *     Examples:
 *
 *       Maybe<Buffer<uint8_t>> buffer =
 *         aUint8Array.CreateFromData<Buffer<uint8_t>>();
 *       if (buffer.isNothing()) {
 *         aError.ThrowTypeError("Failed to create a buffer");
 *         return;
 *       }
 *
 *       size_t offset, length;
 *       … // Getting offset and length values from somewhere.
 *       Maybe<Buffer<uint8_t>> buffer =
 *         aUint8Array.CreateFromData<Buffer<uint8_t>>([&](
 *             const size_t& aLength) {
 *         if (aLength - offset != std::size(data)) {
 *           aError.ThrowTypeError(
 *               "Typed array doesn't contain the right amount" of data");
 *           return Maybe<std::pair<size_t, size_t>>();
 *         }
 *         return Some(std::make_pair(offset, std::size(data)));
 *       });
 *       if (buffer.isNothing()) {
 *         return;
 *       }
 *
 *  4) Processing the data in some other way
 *
 *     This is the API for when none of the APIs above are appropriate for your
 *     usecase. As these are the most dangerous APIs you really should check
 *     first if you can't use one of the safer alternatives above. The reason
 *     these APIs are more dangerous is because they give access to the typed
 *     array's data directly, and the location of that data can be changed by
 *     the JS GC (due to generational and/or compacting collection). There are
 *     two APIs for processing data:
 *
 *       template <typename Processor>
 *       [[nodiscard]] ProcessReturnType<Processor> ProcessData(
 *           Processor&& aProcessor) const;
 *
 *       template <typename Processor>
 *       [[nodiscard]] ProcessReturnType<Processor> ProcessFixedData(
 *           Processor&& aProcessor) const;
 *
 *     ProcessData will call the lambda with as arguments a |const Span<…>&|
 *     wrapping the data pointer and length for the data in the typed array, and
 *     a |JS::AutoCheckCannotGC&&|. The lambda will execute in a context where
 *     GC is not allowed.
 *
 *     ProcessFixedData will call the lambda with as argument |const Span<…>&|.
 *     For small typed arrays where the data is stored inline in the typed
 *     array, and thus could move during GC, then the data will be copied into a
 *     fresh out-of-line allocation before the lambda is called.
 *
 *     The signature of the lambdas for ProcessData and ProcessFixedData differ
 *     in that the ProcessData lambda will additionally be passed a nogc token
 *     to prevent GC from occurring and invalidating the data. If the processing
 *     you need to do in the lambda can't be proven to not GC then you should
 *     probably use ProcessFixedData instead. There are cases where you need to
 *     do something that can cause a GC but you don't actually need to access
 *     the data anymore. A good example would be throwing a JS exception and
 *     return. For those very specific cases you can call nogc.reset() before
 *     doing anything that causes a GC. Be extra careful to not access the data
 *     after you called nogc.reset().
 *
 *     Extra care must be taken to not let the Span<…> or any pointers derived
 *     from it escape the lambda, as the position in memory of the typed array's
 *     data can change again once we leave the lambda (invalidating the
 *     pointers). The lambda passed to ProcessData is not allowed to do anything
 *     that will trigger a GC, and the GC rooting hazard analysis will enforce
 *     that.
 *
 *     Both ProcessData and ProcessFixedData will pin the typed array's length
 *     while calling the lambda, to block any changes to the length of the data.
 *     Note that this means that the lambda itself isn't allowed to change the
 *     length of the typed array's data. Any attempt to change the length will
 *     throw a JS exception.
 *
 *     The return type of ProcessData and ProcessFixedData depends on the return
 *     type of the lambda, as they forward the return value from the lambda to
 *     the caller of ProcessData or ProcessFixedData.
 *
 *     Examples:
 *
 *       aUint32Array.ProcessData([] (const Span<uint32_t>& aData,
 *                                    JS::AutoCheckCannotGC&&) {
 *         for (size_t i = 0; i < aData.Length(); ++i) {
 *           aData[i] = i;
 *         }
 *       });
 *
 *       aUint32Array.ProcessData([&] (const Span<uint32_t>& aData,
 *                                     JS::AutoCheckCannotGC&& nogc) {
 *         for (size_t i = 0; i < aData.Length(); ++i) {
 *           if (!aData[i]) {
 *             nogc.reset();
 *             ThrowJSException("Data shouldn't contain 0");
 *             return;
 *           };
 *           DoSomething(aData[i]);
 *         }
 *       });
 *
 *       uint8_t max = aUint8Array.ProcessData([] (const Span<uint8_t>& aData) {
 *         return std::max_element(aData.cbegin(), aData.cend());
 *       });
 *
 *       aUint8Array.ProcessFixedData([] (const Span<uint8_t>& aData) {
 *         return CallFunctionThatMightGC(aData);
 *       });
 *
 *
 * In addition to the above APIs we provide helpers to call them on the typed
 * array members of WebIDL unions. We have helpers for the 4 different sets of
 * APIs above. The return value of the helpers depends on whether the union can
 * contain a type other than a typed array. If the union can't contain a type
 * other than a typed array then the return type is simply the type returned by
 * the corresponding API above. If the union can contain a type other than a
 * typed array then the return type of the helper is a Maybe<…> wrapping the
 * actual return type, with Nothing() signifying that the union contained a
 * non-typed array value.
 *
 *     template <typename ToType, typename T>
 *     [[nodiscard]] auto AppendTypedArrayDataTo(const T& aUnion,
 *                                               ToType& aResult);
 *
 *     template <typename ToType, typename T>
 *     [[nodiscard]] auto CreateFromTypedArrayData(const T& aUnion);
 *
 *     template <typename T, typename Processor>
 *     [[nodiscard]] auto ProcessTypedArrays(
 *         const T& aUnion, Processor&& aProcessor);
 *
 *     template <typename T, typename Processor>
 *     [[nodiscard]] auto ProcessTypedArraysFixed(const T& aUnion,
 *                                                Processor&& aProcessor);
 *
 */

template <class ArrayT>
struct TypedArray_base : public SpiderMonkeyInterfaceObjectStorage,
                         AllTypedArraysBase {
  using element_type = typename ArrayT::DataType;

  TypedArray_base() = default;
  TypedArray_base(TypedArray_base&& aOther) = default;

  TypedArray_base(const TypedArray_base&) = delete;

 public:
  inline bool Init(JSObject* obj) {
    MOZ_ASSERT(!inited());
    mImplObj = mWrappedObj = ArrayT::unwrap(obj).asObject();
    return inited();
  }

  // About shared memory:
  //
  // Any DOM TypedArray as well as any DOM ArrayBufferView can map the
  // memory of either a JS ArrayBuffer or a JS SharedArrayBuffer.
  //
  // Code that elects to allow views that map shared memory to be used
  // -- ie, code that "opts in to shared memory" -- should generally
  // not access the raw data buffer with standard C++ mechanisms as
  // that creates the possibility of C++ data races, which is
  // undefined behavior.  The JS engine will eventually export (bug
  // 1225033) a suite of methods that avoid undefined behavior.
  //
  // Callers of Obj() that do not opt in to shared memory can produce
  // better diagnostics by checking whether the JSObject in fact maps
  // shared memory and throwing an error if it does.  However, it is
  // safe to use the value of Obj() without such checks.
  //
  // The DOM TypedArray abstraction prevents the underlying buffer object
  // from being accessed directly, but JS_GetArrayBufferViewBuffer(Obj())
  // will obtain the buffer object.  Code that calls that function must
  // not assume the returned buffer is an ArrayBuffer.  That is guarded
  // against by an out parameter on that call that communicates the
  // sharedness of the buffer.
  //
  // Finally, note that the buffer memory of a SharedArrayBuffer is
  // not detachable.

 public:
  /**
   * Helper functions to append a copy of this typed array's data to a
   * container. Returns false if the allocation for copying the data fails.
   *
   * aCalculator is an optional argument to which one can pass a lambda
   * expression that will calculate the offset and length of the data to copy
   * out of the typed array. aCalculator will be called with one argument of
   * type size_t set to the length of the data in the typed array. It is allowed
   * to return a std::pair<size_t, size_t> or a Maybe<std::pair<size_t, size_t>>
   * containing the offset and the length of the subset of the data that we
   * should copy. If the calculation can fail then aCalculator should return a
   * Maybe<std::pair<size_t, size_t>>, if .isNothing() returns true for the
   * return value then AppendDataTo will return false and the data won't be
   * copied.
   */

  template <typename... Calculator>
  [[nodiscard]] bool AppendDataTo(nsCString& aResult,
                                  Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "AppendDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          return aResult.Append(aData, fallible);
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, typename... Calculator>
  [[nodiscard]] bool AppendDataTo(nsTArray<T>& aResult,
                                  Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "AppendDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          return aResult.AppendElements(aData, fallible);
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, typename... Calculator>
  [[nodiscard]] bool AppendDataTo(FallibleTArray<T>& aResult,
                                  Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "AppendDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          return aResult.AppendElements(aData, fallible);
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, typename... Calculator>
  [[nodiscard]] bool AppendDataTo(Vector<T>& aResult,
                                  Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "AppendDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          return aResult.append(aData.Elements(), aData.Length());
        },
        std::forward<Calculator>(aCalculator)...);
  }

  /**
   * Helper functions to copy this typed array's data to a container. This will
   * clear any existing data in the container.
   *
   * See the comments for AppendDataTo for information on the aCalculator
   * argument.
   */

  template <typename T, size_t N, typename... Calculator>
  [[nodiscard]] bool CopyDataTo(T (&aResult)[N],
                                Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "CopyDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          if (aData.Length() != N) {
            return false;
          }
          for (size_t i = 0; i < N; ++i) {
            aResult[i] = aData[i];
          }
          return true;
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, size_t N, typename... Calculator>
  [[nodiscard]] bool CopyDataTo(std::array<T, N>* const aResult,
                                Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "CopyDataTo takes at most one aCalculator");

    return ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          if (aData.Length() != N) {
            return false;
          }
          for (size_t i = 0; i < N; ++i) {
            (*aResult).at(i) = aData[i];
          }
          return true;
        },
        std::forward<Calculator>(aCalculator)...);
  }

  /**
   * Helper functions to copy this typed array's data to a newly created
   * container. Returns Nothing() if creating the container with the right size
   * fails.
   *
   * See the comments for AppendDataTo for information on the aCalculator
   * argument.
   */

  template <typename T, typename... Calculator,
            typename IsVector =
                std::enable_if_t<std::is_same_v<Vector<element_type>, T>>>
  [[nodiscard]] Maybe<Vector<element_type>> CreateFromData(
      Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "CreateFromData takes at most one aCalculator");

    return CreateFromDataHelper<T>(
        [&](const Span<const element_type>& aData,
            Vector<element_type>& aResult) {
          if (!aResult.initCapacity(aData.Length())) {
            return false;
          }
          aResult.infallibleAppend(aData.Elements(), aData.Length());
          return true;
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, typename... Calculator,
            typename IsUniquePtr =
                std::enable_if_t<std::is_same_v<T, UniquePtr<element_type[]>>>>
  [[nodiscard]] Maybe<UniquePtr<element_type[]>> CreateFromData(
      Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "CreateFromData takes at most one aCalculator");

    return CreateFromDataHelper<T>(
        [&](const Span<const element_type>& aData,
            UniquePtr<element_type[]>& aResult) {
          aResult =
              MakeUniqueForOverwriteFallible<element_type[]>(aData.Length());
          if (!aResult.get()) {
            return false;
          }
          memcpy(aResult.get(), aData.Elements(), aData.LengthBytes());
          return true;
        },
        std::forward<Calculator>(aCalculator)...);
  }

  template <typename T, typename... Calculator,
            typename IsBuffer =
                std::enable_if_t<std::is_same_v<T, Buffer<element_type>>>>
  [[nodiscard]] Maybe<Buffer<element_type>> CreateFromData(
      Calculator&&... aCalculator) const {
    static_assert(sizeof...(aCalculator) <= 1,
                  "CreateFromData takes at most one aCalculator");

    return CreateFromDataHelper<T>(
        [&](const Span<const element_type>& aData,
            Buffer<element_type>& aResult) {
          Maybe<Buffer<element_type>> buffer =
              Buffer<element_type>::CopyFrom(aData);
          if (buffer.isNothing()) {
            return false;
          }
          aResult = buffer.extract();
          return true;
        },
        std::forward<Calculator>(aCalculator)...);
  }

 private:
  template <typename Processor, typename R = decltype(std::declval<Processor>()(
                                    std::declval<Span<element_type>>(),
                                    std::declval<JS::AutoCheckCannotGC>()))>
  using ProcessNoGCReturnType = R;

  template <typename Processor>
  [[nodiscard]] static inline ProcessNoGCReturnType<Processor>
  CallProcessorNoGC(const Span<element_type>& aData, Processor&& aProcessor,
                    JS::AutoCheckCannotGC&& nogc) {
    MOZ_ASSERT(
        aData.IsEmpty() || aData.Elements(),
        "We expect a non-null data pointer for typed arrays that aren't empty");

    return aProcessor(aData, std::move(nogc));
  }

  template <typename Processor, typename R = decltype(std::declval<Processor>()(
                                    std::declval<Span<element_type>>()))>
  using ProcessReturnType = R;

  template <typename Processor>
  [[nodiscard]] static inline ProcessReturnType<Processor> CallProcessor(
      const Span<element_type>& aData, Processor&& aProcessor) {
    MOZ_ASSERT(
        aData.IsEmpty() || aData.Elements(),
        "We expect a non-null data pointer for typed arrays that aren't empty");

    return aProcessor(aData);
  }

  struct MOZ_STACK_CLASS LengthPinner {
    explicit LengthPinner(const TypedArray_base* aTypedArray)
        : mTypedArray(aTypedArray),
          mWasPinned(
              !JS::PinArrayBufferOrViewLength(aTypedArray->Obj(), true)) {}
    ~LengthPinner() {
      if (!mWasPinned) {
        JS::PinArrayBufferOrViewLength(mTypedArray->Obj(), false);
      }
    }

   private:
    const TypedArray_base* mTypedArray;
    bool mWasPinned;
  };

  template <typename Processor, typename Calculator>
  [[nodiscard]] bool ProcessDataHelper(
      Processor&& aProcessor, Calculator&& aCalculateOffsetAndLength) const {
    LengthPinner pinner(this);

    JS::AutoCheckCannotGC nogc;  // `data` is GC-sensitive.
    Span<element_type> data = GetCurrentData();
    const auto& offsetAndLength = aCalculateOffsetAndLength(data.Length());
    size_t offset, length;
    if constexpr (std::is_convertible_v<decltype(offsetAndLength),
                                        std::pair<size_t, size_t>>) {
      std::tie(offset, length) = offsetAndLength;
    } else {
      if (offsetAndLength.isNothing()) {
        return false;
      }
      std::tie(offset, length) = offsetAndLength.value();
    }

    return CallProcessorNoGC(data.Subspan(offset, length),
                             std::forward<Processor>(aProcessor),
                             std::move(nogc));
  }

  template <bool AllowLargeTypedArrays = false, typename Processor>
  [[nodiscard]] ProcessNoGCReturnType<Processor> ProcessDataHelper(
      Processor&& aProcessor) const {
    LengthPinner pinner(this);
    // The data from GetCurrentData() is GC sensitive.
    JS::AutoCheckCannotGC nogc;
    return CallProcessorNoGC(GetCurrentData<AllowLargeTypedArrays>(),
                             std::forward<Processor>(aProcessor),
                             std::move(nogc));
  }

 public:
  template <bool AllowLargeTypedArrays = false, typename Processor>
  [[nodiscard]] ProcessNoGCReturnType<Processor> ProcessData(
      Processor&& aProcessor) const {
    return ProcessDataHelper<AllowLargeTypedArrays>(
        std::forward<Processor>(aProcessor));
  }

  template <bool AllowLargeTypedArrays = false, typename Processor>
  [[nodiscard]] ProcessReturnType<Processor> ProcessFixedData(
      Processor&& aProcessor) const {
    mozilla::dom::AutoJSAPI jsapi;
    if (!jsapi.Init(mImplObj)) {
      MOZ_CRASH("Failed to get JSContext");
    }
    JS::AutoBrittleMode abm(jsapi.cx());
    if (!JS::EnsureNonInlineArrayBufferOrView(jsapi.cx(), mImplObj)) {
      // Fails on (small) OOM, or when trying to modify a buffer when its length
      // is already pinned. Brittle mode, set just above, will cause the former
      // to crash before returning false, but that is for diagnostic purposes
      // only and could be removed at any time.
      MOZ_CRASH("failed to make ArrayBuffer data be stored separately");
    }
    LengthPinner pinner(this);

    return CallProcessor(GetCurrentData<AllowLargeTypedArrays>(),
                         std::forward<Processor>(aProcessor));
  }

 private:
  template <bool AllowLargeTypedArrays = false>
  Span<element_type> GetCurrentData() const {
    MOZ_ASSERT(inited());
    MOZ_RELEASE_ASSERT(
        !ArrayT::fromObject(mImplObj).isResizable(),
        "Bindings must have checked ArrayBuffer{View} is non-resizable");
    MOZ_RELEASE_ASSERT(
        !ArrayT::fromObject(mImplObj).isImmutable(),
        "Bindings must have checked ArrayBuffer{View} is mutable");

    // Intentionally return a pointer and length that escape from a nogc region.
    // Private so it can only be used in very limited situations.
    JS::AutoCheckCannotGC nogc;
    bool shared;
    Span<element_type> span =
        ArrayT::fromObject(mImplObj).getData(&shared, nogc);
    if constexpr (!AllowLargeTypedArrays) {
      MOZ_RELEASE_ASSERT(span.Length() <= INT32_MAX,
                         "Bindings must have checked ArrayBuffer{View} length");
    }
    return span;
  }

  template <typename T, typename F, typename... Calculator>
  [[nodiscard]] Maybe<T> CreateFromDataHelper(
      F&& aCreator, Calculator&&... aCalculator) const {
    Maybe<T> result;
    bool ok = ProcessDataHelper(
        [&](const Span<const element_type>& aData, JS::AutoCheckCannotGC&&) {
          result.emplace();
          return aCreator(aData, *result);
        },
        std::forward<Calculator>(aCalculator)...);

    if (!ok) {
      return Nothing();
    }

    return result;
  }
};

template <class ArrayT>
struct TypedArray : public TypedArray_base<ArrayT> {
  using Base = TypedArray_base<ArrayT>;
  using element_type = typename Base::element_type;

  TypedArray() = default;

  TypedArray(TypedArray&& aOther) = default;

  TypedArray(const TypedArray&) = delete;

  static inline JSObject* Create(JSContext* cx, nsWrapperCache* creator,
                                 size_t length, ErrorResult& error) {
    return CreateCommon(cx, creator, length, error).asObject();
  }

  static inline JSObject* Create(JSContext* cx, size_t length,
                                 ErrorResult& error) {
    return CreateCommon(cx, length, error).asObject();
  }

  static inline JSObject* Create(JSContext* cx, nsWrapperCache* creator,
                                 Span<const element_type> data,
                                 ErrorResult& error) {
    ArrayT array = CreateCommon(cx, creator, data.Length(), error);
    if (!error.Failed() && !data.IsEmpty()) {
      CopyFrom(cx, data, array);
    }
    return array.asObject();
  }

  static inline JSObject* Create(JSContext* cx, Span<const element_type> data,
                                 ErrorResult& error) {
    ArrayT array = CreateCommon(cx, data.Length(), error);
    if (!error.Failed() && !data.IsEmpty()) {
      CopyFrom(cx, data, array);
    }
    return array.asObject();
  }

 private:
  template <typename>
  friend class TypedArrayCreator;

  static inline ArrayT CreateCommon(JSContext* cx, nsWrapperCache* creator,
                                    size_t length, ErrorResult& error) {
    JS::Rooted<JSObject*> creatorWrapper(cx);
    Maybe<JSAutoRealm> ar;
    if (creator && (creatorWrapper = creator->GetWrapperPreserveColor())) {
      ar.emplace(cx, creatorWrapper);
    }

    return CreateCommon(cx, length, error);
  }
  static inline ArrayT CreateCommon(JSContext* cx, size_t length,
                                    ErrorResult& error) {
    error.MightThrowJSException();
    ArrayT array = CreateCommon(cx, length);
    if (array) {
      return array;
    }
    error.StealExceptionFromJSContext(cx);
    return ArrayT::fromObject(nullptr);
  }
  // NOTE: this leaves any exceptions on the JSContext, and the caller is
  //       required to deal with them.
  static inline ArrayT CreateCommon(JSContext* cx, size_t length) {
    return ArrayT::create(cx, length);
  }
  static inline void CopyFrom(JSContext* cx,
                              const Span<const element_type>& data,
                              ArrayT& dest) {
    JS::AutoCheckCannotGC nogc;
    bool isShared;
    mozilla::Span<element_type> span = dest.getData(&isShared, nogc);
    MOZ_ASSERT(span.size() == data.size(),
               "Didn't create a large enough typed array object?");
    // Data will not be shared, until a construction protocol exists
    // for constructing shared data.
    MOZ_ASSERT(!isShared);
    memcpy(span.Elements(), data.Elements(), data.LengthBytes());
  }
};

template <JS::Scalar::Type GetViewType(JSObject*)>
struct ArrayBufferView_base : public TypedArray_base<JS::ArrayBufferView> {
 private:
  using Base = TypedArray_base<JS::ArrayBufferView>;

 public:
  ArrayBufferView_base() : Base(), mType(JS::Scalar::MaxTypedArrayViewType) {}

  ArrayBufferView_base(ArrayBufferView_base&& aOther)
      : Base(std::move(aOther)), mType(aOther.mType) {
    aOther.mType = JS::Scalar::MaxTypedArrayViewType;
  }

 private:
  JS::Scalar::Type mType;

 public:
  inline bool Init(JSObject* obj) {
    if (!Base::Init(obj)) {
      return false;
    }

    mType = GetViewType(this->Obj());
    return true;
  }

  inline JS::Scalar::Type Type() const {
    MOZ_ASSERT(this->inited());
    return mType;
  }
};

using Int8Array = TypedArray<JS::Int8Array>;
using Uint8Array = TypedArray<JS::Uint8Array>;
using Uint8ClampedArray = TypedArray<JS::Uint8ClampedArray>;
using Int16Array = TypedArray<JS::Int16Array>;
using Uint16Array = TypedArray<JS::Uint16Array>;
using Int32Array = TypedArray<JS::Int32Array>;
using Uint32Array = TypedArray<JS::Uint32Array>;
using Float32Array = TypedArray<JS::Float32Array>;
using Float64Array = TypedArray<JS::Float64Array>;
using BigInt64Array = TypedArray<JS::BigInt64Array>;
using BigUint64Array = TypedArray<JS::BigUint64Array>;
using ArrayBufferView = ArrayBufferView_base<JS_GetArrayBufferViewType>;
using ArrayBuffer = TypedArray<JS::ArrayBuffer>;

// A class for converting an nsTArray to a TypedArray
// Note: A TypedArrayCreator must not outlive the nsTArray it was created from.
//       So this is best used to pass from things that understand nsTArray to
//       things that understand TypedArray, as with ToJSValue.
template <typename TypedArrayType>
class MOZ_STACK_CLASS TypedArrayCreator {
  using ValuesType = typename TypedArrayType::element_type;
  using ArrayType = nsTArray<ValuesType>;

 public:
  explicit TypedArrayCreator(const ArrayType& aArray) : mValues(aArray) {}
  explicit TypedArrayCreator(const nsCString& aString) : mValues(aString) {}

  // NOTE: this leaves any exceptions on the JSContext, and the caller is
  //       required to deal with them.
  JSObject* Create(JSContext* aCx) const {
    auto array = TypedArrayType::CreateCommon(aCx, mValues.Length());
    if (array) {
      TypedArrayType::CopyFrom(aCx, mValues, array);
    }
    return array.asObject();
  }

 private:
  Span<const ValuesType> mValues;
};

namespace binding_detail {

template <typename Union, typename UnionMemberType, typename = int>
struct ApplyToTypedArray;

#define APPLY_IMPL(type)                                                       \
  template <typename Union>                                                    \
  struct ApplyToTypedArray<Union, type, decltype((void)&Union::Is##type, 0)> { \
    /* Return type of calling the lambda with a TypedArray 'type'.         */  \
    template <typename F>                                                      \
    using FunReturnType = decltype(std::declval<F>()(std::declval<type>()));   \
                                                                               \
    /* Whether the return type of calling the lambda with a TypedArray     */  \
    /* 'type' is void. */                                                      \
    template <typename F>                                                      \
    static constexpr bool FunReturnsVoid =                                     \
        std::is_same_v<FunReturnType<F>, void>;                                \
                                                                               \
    /* The return type of calling Apply with a union that has 'type' as    */  \
    /* one of its union member types depends on the return type of         */  \
    /* calling the lambda. This return type will be bool if the lambda     */  \
    /* returns void, or it will be a Maybe<…> with the inner type being    */  \
    /* the actual return type of calling the lambda. If the union          */  \
    /* contains a value of the right type, then calling Apply will return  */  \
    /* either 'true', or 'Some(…)' containing the return value of calling  */  \
    /* the lambda. If the union does not contain a value of the right      */  \
    /* type, then calling Apply will return either 'false', or             */  \
    /* 'Nothing()'.                                                        */  \
    template <typename F>                                                      \
    using ApplyReturnType =                                                    \
        std::conditional_t<FunReturnsVoid<F>, bool, Maybe<FunReturnType<F>>>;  \
                                                                               \
   public:                                                                     \
    template <typename F>                                                      \
    static ApplyReturnType<F> Apply(const Union& aUnion, F&& aFun) {           \
      if (!aUnion.Is##type()) {                                                \
        return ApplyReturnType<F>(); /* false or Nothing() */                  \
      }                                                                        \
      if constexpr (FunReturnsVoid<F>) {                                       \
        std::forward<F>(aFun)(aUnion.GetAs##type());                           \
        return true;                                                           \
      } else {                                                                 \
        return Some(std::forward<F>(aFun)(aUnion.GetAs##type()));              \
      }                                                                        \
    }                                                                          \
  };

APPLY_IMPL(Int8Array)
APPLY_IMPL(Uint8Array)
APPLY_IMPL(Uint8ClampedArray)
APPLY_IMPL(Int16Array)
APPLY_IMPL(Uint16Array)
APPLY_IMPL(Int32Array)
APPLY_IMPL(Uint32Array)
APPLY_IMPL(Float32Array)
APPLY_IMPL(Float64Array)
APPLY_IMPL(BigInt64Array)
APPLY_IMPL(BigUint64Array)
APPLY_IMPL(ArrayBufferView)
APPLY_IMPL(ArrayBuffer)

#undef APPLY_IMPL

// The binding code generate creates an alias of this type for every WebIDL
// union that contains a typed array type, with the right value for H (which
// will be true if there are non-typedarray types in the union).
template <typename T, bool H, typename FirstUnionMember,
          typename... UnionMembers>
struct ApplyToTypedArraysHelper {
  static constexpr bool HasNonTypedArrayMembers = H;
  template <typename Fun>
  static auto Apply(const T& aUnion, Fun&& aFun) {
    auto result = ApplyToTypedArray<T, FirstUnionMember>::Apply(
        aUnion, std::forward<Fun>(aFun));
    if constexpr (sizeof...(UnionMembers) == 0) {
      return result;
    } else {
      if (result) {
        return result;
      } else {
        return ApplyToTypedArraysHelper<T, H, UnionMembers...>::template Apply<
            Fun>(aUnion, std::forward<Fun>(aFun));
      }
    }
  }
};

template <typename T, typename Fun>
auto ApplyToTypedArrays(const T& aUnion, Fun&& aFun) {
  using ApplyToTypedArrays = typename T::ApplyToTypedArrays;

  auto result =
      ApplyToTypedArrays::template Apply<Fun>(aUnion, std::forward<Fun>(aFun));
  if constexpr (ApplyToTypedArrays::HasNonTypedArrayMembers) {
    return result;
  } else {
    MOZ_ASSERT(result, "Didn't expect union members other than typed arrays");

    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(result)>, bool>) {
      return;
    } else {
      return result.extract();
    }
  }
}

}  // namespace binding_detail

template <typename T, typename ToType,
          std::enable_if_t<is_dom_union_with_typedarray_members<T>, int> = 0>
[[nodiscard]] auto AppendTypedArrayDataTo(const T& aUnion, ToType& aResult) {
  return binding_detail::ApplyToTypedArrays(
      aUnion, [&](const auto& aTypedArray) {
        return aTypedArray.AppendDataTo(aResult);
      });
}

template <typename ToType, typename T,
          std::enable_if_t<is_dom_union_with_typedarray_members<T>, int> = 0>
[[nodiscard]] auto CreateFromTypedArrayData(const T& aUnion) {
  return binding_detail::ApplyToTypedArrays(
      aUnion, [&](const auto& aTypedArray) {
        return aTypedArray.template CreateFromData<ToType>();
      });
}

template <typename T, typename Processor,
          std::enable_if_t<is_dom_union_with_typedarray_members<T>, int> = 0>
[[nodiscard]] auto ProcessTypedArrays(const T& aUnion, Processor&& aProcessor) {
  return binding_detail::ApplyToTypedArrays(
      aUnion, [&](const auto& aTypedArray) {
        return aTypedArray.ProcessData(std::forward<Processor>(aProcessor));
      });
}

template <typename T, typename Processor,
          std::enable_if_t<is_dom_union_with_typedarray_members<T>, int> = 0>
[[nodiscard]] auto ProcessTypedArraysFixed(const T& aUnion,
                                           Processor&& aProcessor) {
  return binding_detail::ApplyToTypedArrays(
      aUnion, [&](const auto& aTypedArray) {
        return aTypedArray.ProcessFixedData(
            std::forward<Processor>(aProcessor));
      });
}

}  // namespace mozilla::dom

#endif /* mozilla_dom_TypedArray_h */
