/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef QUEUEPARAMTRAITS_H_
#define QUEUEPARAMTRAITS_H_ 1

#include "WebGLTypes.h"
#include "ipc/EnumSerializer.h"
#include "mozilla/Assertions.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Logging.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsExceptionHandler.h"
#include "nsString.h"

namespace mozilla::webgl {

template <typename T>
struct RemoveCVR {
  using Type =
      typename std::remove_reference<typename std::remove_cv<T>::type>::type;
};

/**
 * QueueParamTraits provide the user with a way to implement PCQ argument
 * (de)serialization.  It uses a PcqView, which permits the system to
 * abandon all changes to the underlying PCQ if any operation fails.
 *
 * The transactional nature of PCQ operations make the ideal behavior a bit
 * complex.  Since the PCQ has a fixed amount of memory available to it,
 * TryInsert operations operations are expected to sometimes fail and be
 * re-issued later.  We want these failures to be inexpensive.  The same
 * goes for TryRemove, which fails when there isn't enough data in
 * the queue yet for them to complete.
 *
 * Their expected interface is:
 *
 * template<> struct QueueParamTraits<typename RemoveCVR<Arg>::Type> {
 *   // Write data from aArg into the PCQ.
 *   static QueueStatus Write(ProducerView& aProducerView, const Arg& aArg)
 *   {...};
 *
 *   // Read data from the PCQ into aArg, or just skip the data if aArg is null.
 *   static QueueStatus Read(ConsumerView& aConsumerView, Arg* aArg) {...}
 * };
 */
template <typename Arg>
struct QueueParamTraits;  // Todo: s/QueueParamTraits/SizedParamTraits/

template <typename T>
inline Range<T> AsRange(T* const begin, T* const end) {
  const auto size = MaybeAs<size_t>(end - begin);
  MOZ_RELEASE_ASSERT(size);
  return {begin, *size};
}

// -
// BytesAlwaysValidT

template <class T>
struct BytesAlwaysValidT {
  using non_cv = typename std::remove_cv<T>::type;
  static constexpr bool value =
      std::is_arithmetic<T>::value && !std::is_same<non_cv, bool>::value;
};
static_assert(BytesAlwaysValidT<float>::value);
static_assert(!BytesAlwaysValidT<bool>::value);
static_assert(!BytesAlwaysValidT<const bool>::value);
static_assert(!BytesAlwaysValidT<int*>::value);
static_assert(BytesAlwaysValidT<intptr_t>::value);

template <class T, size_t N>
struct BytesAlwaysValidT<std::array<T, N>> {
  static constexpr bool value = BytesAlwaysValidT<T>::value;
};
static_assert(BytesAlwaysValidT<std::array<int, 4>>::value);
static_assert(!BytesAlwaysValidT<std::array<bool, 4>>::value);

template <class T, size_t N>
struct BytesAlwaysValidT<T[N]> {
  static constexpr bool value = BytesAlwaysValidT<T>::value;
};
static_assert(BytesAlwaysValidT<int[4]>::value);
static_assert(!BytesAlwaysValidT<bool[4]>::value);

// -

template <>
struct BytesAlwaysValidT<webgl::UniformDataVal> {
  static constexpr bool value = true;
};
template <>
struct BytesAlwaysValidT<const webgl::UniformDataVal> {
  static constexpr bool value = true;
};

// -

/**
 * Used to give QueueParamTraits a way to write to the Producer without
 * actually altering it, in case the transaction fails.
 * THis object maintains the error state of the transaction and
 * discards commands issued after an error is encountered.
 */
template <typename _Producer>
class ProducerView {
 public:
  using Producer = _Producer;

  explicit ProducerView(Producer* aProducer) : mProducer(aProducer) {}

  template <typename T>
  bool WriteFromRange(const Range<const T>& src) {
    static_assert(BytesAlwaysValidT<T>::value);
    if (mOk) [[likely]] {
      mOk &= mProducer->WriteFromRange(src);
    }
    return mOk;
  }

  /**
   * Copy bytes from aBuffer to the producer if there is enough room.
   * aBufferSize must not be 0.
   */
  template <typename T>
  inline bool Write(const T* begin, const T* end) {
    MOZ_RELEASE_ASSERT(begin <= end);
    return WriteFromRange(AsRange(begin, end));
  }

  /**
   * Serialize aArg using Arg's QueueParamTraits.
   */
  template <typename Arg>
  bool WriteParam(const Arg& aArg) {
    return mozilla::webgl::QueueParamTraits<
        typename RemoveCVR<Arg>::Type>::Write(*this, aArg);
  }

  bool Ok() const { return mOk; }

 private:
  Producer* const mProducer;
  bool mOk = true;
};

/**
 * Used to give QueueParamTraits a way to read from the Consumer without
 * actually altering it, in case the transaction fails.
 */
template <typename _Consumer>
class ConsumerView {
 public:
  using Consumer = _Consumer;

  explicit ConsumerView(Consumer* aConsumer) : mConsumer(aConsumer) {}

  /**
   * Read bytes from the consumer if there is enough data.  aBuffer may
   * be null (in which case the data is skipped)
   */
  template <typename T>
  inline bool Read(T* const destBegin, T* const destEnd) {
    MOZ_ASSERT(destBegin);
    MOZ_RELEASE_ASSERT(destBegin <= destEnd);

    const auto dest = AsRange(destBegin, destEnd);
    const auto view = ReadRange<T>(dest.length());
    if (view) [[likely]] {
      const auto byteSize = ByteSize(dest);
      if (byteSize) [[likely]] {
        memcpy(dest.begin().get(), view->begin().get(), byteSize);
      }
    }
    return mOk;
  }

  /// Return a view wrapping the shmem.
  template <typename T>
  inline Maybe<Range<const T>> ReadRange(const size_t elemCount) {
    static_assert(BytesAlwaysValidT<T>::value);
    if (!mOk) [[unlikely]] {
      return {};
    }
    const auto view = mConsumer->template ReadRange<T>(elemCount);
    mOk &= bool(view);
    return view;
  }

  /**
   * Deserialize aArg using Arg's QueueParamTraits.
   * If the return value is not Success then aArg is not changed.
   */
  template <typename Arg>
  bool ReadParam(Arg* aArg) {
    MOZ_ASSERT(aArg);
    return mozilla::webgl::QueueParamTraits<std::remove_cv_t<Arg>>::Read(*this,
                                                                         aArg);
  }

  bool Ok() const { return mOk; }

 private:
  Consumer* const mConsumer;
  bool mOk = true;
};

// -

template <typename Arg>
struct QueueParamTraits {
  template <typename ProducerView>
  static bool Write(ProducerView& aProducerView, const Arg& aArg) {
    static_assert(BytesAlwaysValidT<Arg>::value,
                  "No QueueParamTraits specialization was found for this type "
                  "and it does not satisfy BytesAlwaysValid.");
    // Write self as binary
    const auto pArg = &aArg;
    return aProducerView.Write(pArg, pArg + 1);
  }

  template <typename ConsumerView>
  static bool Read(ConsumerView& aConsumerView, Arg* aArg) {
    static_assert(BytesAlwaysValidT<Arg>::value,
                  "No QueueParamTraits specialization was found for this type "
                  "and it does not satisfy BytesAlwaysValid.");
    // Read self as binary
    return aConsumerView.Read(aArg, aArg + 1);
  }
};

// ---------------------------------------------------------------

template <>
struct QueueParamTraits<bool> {
  using ParamType = bool;

  template <typename U>
  static auto Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    uint8_t temp = aArg ? 1 : 0;
    return aProducerView.WriteParam(temp);
  }

  template <typename U>
  static auto Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    uint8_t temp;
    if (aConsumerView.ReadParam(&temp)) {
      MOZ_ASSERT(temp == 1 || temp == 0);
      *aArg = temp ? true : false;
    }
    return aConsumerView.Ok();
  }
};

// ---------------------------------------------------------------

// We guarantee our robustness via these requirements:
// * Object.MutTiedFields() gives us a tuple,
// * where the combined sizeofs all field types sums to sizeof(Object),
//   * (thus we know we are exhaustively listing all fields)
// * where feeding each field back into ParamTraits succeeds,
// * and ParamTraits is only automated for BytesAlwaysValidT<T> types.
// (BytesAlwaysValidT rejects bool and enum types, and only accepts int/float
// types, or array or std::arrays of such types)
// (Yes, bit-field fields are rejected by MutTiedFields too)

template <class T>
struct QueueParamTraits_TiedFields {
  template <typename ProducerView>
  static bool Write(ProducerView& aProducerView, const T& aArg) {
    const auto fields = TiedFields(aArg);
    static_assert(AreAllBytesTiedFields<T>(),
                  "Are there missing fields or padding between fields?");

    bool ok = true;
    MapTuple(fields, [&](const auto& field) {
      ok &= aProducerView.WriteParam(field);
      return true;
    });
    return ok;
  }

  template <typename ConsumerView>
  static bool Read(ConsumerView& aConsumerView, T* aArg) {
    const auto fields = TiedFields(*aArg);
    static_assert(AreAllBytesTiedFields<T>());

    bool ok = true;
    MapTuple(fields, [&](auto& field) {
      ok &= aConsumerView.ReadParam(&field);
      return true;
    });
    return ok;
  }
};

// ---------------------------------------------------------------

// Adapted from IPC::EnumSerializer, this class safely handles enum values,
// validating that they are in range using the same EnumValidators as IPDL
// (namely ContiguousEnumValidator and ContiguousEnumValidatorInclusive).
template <typename E, typename EnumValidator>
struct EnumSerializer {
  using ParamType = E;
  using DataType = typename std::underlying_type<E>::type;

  template <typename U>
  static auto Write(ProducerView<U>& aProducerView, const ParamType& aValue) {
    MOZ_RELEASE_ASSERT(
        EnumValidator::IsLegalValue(static_cast<DataType>(aValue)));
    return aProducerView.WriteParam(DataType(aValue));
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aResult) {
    DataType value;
    if (!aConsumerView.ReadParam(&value)) {
      CrashReporter::RecordAnnotationCString(
          CrashReporter::Annotation::IPCReadErrorReason, "Bad iter");
      return false;
    }
    if (!EnumValidator::IsLegalValue(static_cast<DataType>(value))) {
      CrashReporter::RecordAnnotationCString(
          CrashReporter::Annotation::IPCReadErrorReason, "Illegal value");
      return false;
    }

    *aResult = ParamType(value);
    return true;
  }
};

using IPC::ContiguousEnumValidator;
using IPC::ContiguousEnumValidatorInclusive;

template <typename E, E MinLegal, E HighBound>
struct ContiguousEnumSerializer
    : EnumSerializer<E, ContiguousEnumValidator<E, MinLegal, HighBound>> {};

template <typename E, E MinLegal, E MaxLegal>
struct ContiguousEnumSerializerInclusive
    : EnumSerializer<E,
                     ContiguousEnumValidatorInclusive<E, MinLegal, MaxLegal>> {
};

// ---------------------------------------------------------------

template <>
struct QueueParamTraits<webgl::TexUnpackBlobDesc> {
  using ParamType = webgl::TexUnpackBlobDesc;

  template <typename U>
  static bool Write(ProducerView<U>& view, const ParamType& in) {
    MOZ_RELEASE_ASSERT(!in.image);
    MOZ_RELEASE_ASSERT(!in.sd);
    const bool isDataSurf = bool(in.sourceSurf);
    if (!view.WriteParam(in.imageTarget) || !view.WriteParam(in.size) ||
        !view.WriteParam(in.srcAlphaType) || !view.WriteParam(in.unpacking) ||
        !view.WriteParam(in.cpuData) || !view.WriteParam(in.pboOffset) ||
        !view.WriteParam(in.structuredSrcSize) ||
        !view.WriteParam(in.applyUnpackTransforms) ||
        !view.WriteParam(isDataSurf)) {
      return false;
    }
    if (isDataSurf) {
      const RefPtr<gfx::DataSourceSurface> surf =
          in.sourceSurf->GetDataSurface();
      if (!surf) {
        return false;
      }
      gfx::DataSourceSurface::ScopedMap map(surf, gfx::DataSourceSurface::READ);
      if (!map.IsMapped()) {
        return false;
      }
      const auto& surfSize = surf->GetSize();
      const auto stride = *MaybeAs<size_t>(map.GetStride());
      if (!view.WriteParam(surfSize) || !view.WriteParam(surf->GetFormat()) ||
          !view.WriteParam(stride)) {
        return false;
      }

      const size_t dataSize = stride * surfSize.height;
      const auto& begin = map.GetData();
      const auto range = Range<const uint8_t>{begin, dataSize};
      if (!view.WriteFromRange(range)) {
        return false;
      }
    }
    return true;
  }

  template <typename U>
  static bool Read(ConsumerView<U>& view, ParamType* const out) {
    bool isDataSurf;
    if (!view.ReadParam(&out->imageTarget) || !view.ReadParam(&out->size) ||
        !view.ReadParam(&out->srcAlphaType) ||
        !view.ReadParam(&out->unpacking) || !view.ReadParam(&out->cpuData) ||
        !view.ReadParam(&out->pboOffset) ||
        !view.ReadParam(&out->structuredSrcSize) ||
        !view.ReadParam(&out->applyUnpackTransforms) ||
        !view.ReadParam(&isDataSurf)) {
      return false;
    }
    if (isDataSurf) {
      gfx::IntSize surfSize;
      gfx::SurfaceFormat format;
      size_t stride;
      if (!view.ReadParam(&surfSize) || !view.ReadParam(&format) ||
          !view.ReadParam(&stride)) {
        return false;
      }
      if (!CheckedInt32(stride).isValid() || surfSize.IsEmpty()) {
        return false;
      }
      int32_t bpp = BytesPerPixel(format);
      CheckedInt<size_t> minStride(bpp);
      minStride *= surfSize.width;
      if (!minStride.isValid() || minStride.value() <= 0 ||
          stride < minStride.value()) {
        return false;
      }
      CheckedInt<size_t> dataSize(stride);
      dataSize *= surfSize.height;
      if (!dataSize.isValid()) {
        return false;
      }
      const auto range = view.template ReadRange<uint8_t>(dataSize.value());
      if (!range) return false;

      // DataSourceSurface demands pointer-to-mutable.
      const auto bytes = const_cast<uint8_t*>(range->begin().get());
      out->sourceSurf = gfx::Factory::CreateWrappingDataSourceSurface(
          bytes, stride, surfSize, format);
      MOZ_ASSERT(out->sourceSurf);
    }
    return true;
  }
};

// ---------------------------------------------------------------

template <>
struct QueueParamTraits<nsACString> {
  using ParamType = nsACString;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    if ((!aProducerView.WriteParam(aArg.IsVoid())) || aArg.IsVoid()) {
      return false;
    }

    uint32_t len = aArg.Length();
    if ((!aProducerView.WriteParam(len)) || (len == 0)) {
      return false;
    }

    return aProducerView.Write(aArg.BeginReading(), len);
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    bool isVoid = false;
    if (!aConsumerView.ReadParam(&isVoid)) {
      return false;
    }
    aArg->SetIsVoid(isVoid);
    if (isVoid) {
      return true;
    }

    uint32_t len = 0;
    if (!aConsumerView.ReadParam(&len)) {
      return false;
    }

    if (len == 0) {
      *aArg = "";
      return true;
    }

    char* buf = new char[len + 1];
    if (!buf) {
      return false;
    }
    if (!aConsumerView.Read(buf, len)) {
      return false;
    }
    buf[len] = '\0';
    aArg->Adopt(buf, len);
    return true;
  }
};

template <>
struct QueueParamTraits<nsAString> {
  using ParamType = nsAString;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    if ((!aProducerView.WriteParam(aArg.IsVoid())) || (aArg.IsVoid())) {
      return false;
    }
    // DLP: No idea if this includes null terminator
    uint32_t len = aArg.Length();
    if ((!aProducerView.WriteParam(len)) || (len == 0)) {
      return false;
    }
    constexpr const uint32_t sizeofchar = sizeof(typename ParamType::char_type);
    return aProducerView.Write(aArg.BeginReading(), len * sizeofchar);
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    bool isVoid = false;
    if (!aConsumerView.ReadParam(&isVoid)) {
      return false;
    }
    aArg->SetIsVoid(isVoid);
    if (isVoid) {
      return true;
    }

    // DLP: No idea if this includes null terminator
    uint32_t len = 0;
    if (!aConsumerView.ReadParam(&len)) {
      return false;
    }

    if (len == 0) {
      *aArg = nsString();
      return true;
    }

    uint32_t sizeofchar = sizeof(typename ParamType::char_type);
    typename ParamType::char_type* buf = nullptr;
    buf = static_cast<typename ParamType::char_type*>(
        malloc((len + 1) * sizeofchar));
    if (!buf) {
      return false;
    }

    if (!aConsumerView.Read(buf, len * sizeofchar)) {
      return false;
    }

    buf[len] = L'\0';
    aArg->Adopt(buf, len);
    return true;
  }
};

template <>
struct QueueParamTraits<nsCString> : public QueueParamTraits<nsACString> {
  using ParamType = nsCString;
};

template <>
struct QueueParamTraits<nsString> : public QueueParamTraits<nsAString> {
  using ParamType = nsString;
};

// ---------------------------------------------------------------

template <typename NSTArrayType,
          bool = BytesAlwaysValidT<typename NSTArrayType::value_type>::value>
struct NSArrayQueueParamTraits;

// For ElementTypes that are !BytesAlwaysValidT
template <typename _ElementType>
struct NSArrayQueueParamTraits<nsTArray<_ElementType>, false> {
  using ElementType = _ElementType;
  using ParamType = nsTArray<ElementType>;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.Length());
    for (auto& elt : aArg) {
      aProducerView.WriteParam(elt);
    }
    return true;
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    size_t arrayLen;
    if (!aConsumerView.ReadParam(&arrayLen)) {
      return false;
    }

    if (!aArg->AppendElements(arrayLen, fallible)) {
      return false;
    }

    for (auto i : IntegerRange(arrayLen)) {
      ElementType& elt = aArg->ElementAt(i);
      aConsumerView.ReadParam(elt);
    }
    return aConsumerView.Ok();
  }
};

// For ElementTypes that are BytesAlwaysValidT
template <typename _ElementType>
struct NSArrayQueueParamTraits<nsTArray<_ElementType>, true> {
  using ElementType = _ElementType;
  using ParamType = nsTArray<ElementType>;

  // TODO: Are there alignment issues?
  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    size_t arrayLen = aArg.Length();
    aProducerView.WriteParam(arrayLen);
    return aProducerView.Write(&aArg[0], aArg.Length() * sizeof(ElementType));
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    size_t arrayLen;
    if (!aConsumerView.ReadParam(&arrayLen)) {
      return false;
    }

    if (!aArg->AppendElements(arrayLen, fallible)) {
      return false;
    }

    return aConsumerView.Read(aArg->Elements(), arrayLen * sizeof(ElementType));
  }
};

template <typename ElementType>
struct QueueParamTraits<nsTArray<ElementType>>
    : public NSArrayQueueParamTraits<nsTArray<ElementType>> {
  using ParamType = nsTArray<ElementType>;
};

// ---------------------------------------------------------------

template <typename ArrayType,
          bool = BytesAlwaysValidT<typename ArrayType::ElementType>::value>
struct ArrayQueueParamTraits;

// For ElementTypes that are !BytesAlwaysValidT
template <typename _ElementType, size_t Length>
struct ArrayQueueParamTraits<Array<_ElementType, Length>, false> {
  using ElementType = _ElementType;
  using ParamType = Array<ElementType, Length>;

  template <typename U>
  static auto Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    for (const auto& elt : aArg) {
      aProducerView.WriteParam(elt);
    }
    return aProducerView.Ok();
  }

  template <typename U>
  static auto Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    for (auto& elt : *aArg) {
      aConsumerView.ReadParam(elt);
    }
    return aConsumerView.Ok();
  }
};

// For ElementTypes that are BytesAlwaysValidT
template <typename _ElementType, size_t Length>
struct ArrayQueueParamTraits<Array<_ElementType, Length>, true> {
  using ElementType = _ElementType;
  using ParamType = Array<ElementType, Length>;

  template <typename U>
  static auto Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    return aProducerView.Write(aArg.begin(), sizeof(ElementType[Length]));
  }

  template <typename U>
  static auto Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    return aConsumerView.Read(aArg->begin(), sizeof(ElementType[Length]));
  }
};

template <typename ElementType, size_t Length>
struct QueueParamTraits<Array<ElementType, Length>>
    : public ArrayQueueParamTraits<Array<ElementType, Length>> {
  using ParamType = Array<ElementType, Length>;
};

// ---------------------------------------------------------------

template <typename ElementType>
struct QueueParamTraits<Maybe<ElementType>> {
  using ParamType = Maybe<ElementType>;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(static_cast<bool>(aArg));
    if (aArg) {
      aProducerView.WriteParam(aArg.ref());
    }
    return aProducerView.Ok();
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    bool isSome;
    if (!aConsumerView.ReadParam(&isSome)) {
      return false;
    }

    if (!isSome) {
      aArg->reset();
      return true;
    }

    aArg->emplace();
    return aConsumerView.ReadParam(aArg->ptr());
  }
};

// ---------------------------------------------------------------

template <typename TypeA, typename TypeB>
struct QueueParamTraits<std::pair<TypeA, TypeB>> {
  using ParamType = std::pair<TypeA, TypeB>;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    aProducerView.WriteParam(aArg.first());
    return aProducerView.WriteParam(aArg.second());
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    aConsumerView.ReadParam(aArg->first());
    return aConsumerView.ReadParam(aArg->second());
  }
};

// -

template <class... T>
struct QueueParamTraits<std::tuple<T...>> {
  using ParamType = std::tuple<T...>;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    bool ok = true;
    mozilla::MapTuple(aArg, [&](const auto& field) {
      ok &= aProducerView.WriteParam(field);
      return true;  // ignored
    });
    return ok;
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    bool ok = true;
    mozilla::MapTuple(*aArg, [&](auto& field) {
      ok &= aConsumerView.ReadParam(&field);
      return true;  // ignored
    });
    return ok;
  }
};

// -

template <class K, class V, class H, class E>
struct QueueParamTraits<std::unordered_map<K, V, H, E>> {
  using ParamType = std::unordered_map<K, V, H, E>;

  template <typename U>
  static bool Write(ProducerView<U>& aProducerView, const ParamType& aArg) {
    bool ok = aProducerView.WriteParam(uint64_t{aArg.size()});
    for (const auto& pair : aArg) {
      ok &= aProducerView.WriteParam(pair);
    }
    return ok;
  }

  template <typename U>
  static bool Read(ConsumerView<U>& aConsumerView, ParamType* aArg) {
    aArg->clear();

    auto size = uint64_t{0};
    if (!aConsumerView.ReadParam(&size)) return false;

    aArg->reserve(size);
    for (const auto i : IntegerRange(size)) {
      (void)i;
      auto pair = std::pair<K, V>{};
      if (!aConsumerView.ReadParam(&pair)) return false;
      aArg->insert(pair);
    }
    return true;
  }
};

}  // namespace mozilla::webgl

#endif  // QUEUEPARAMTRAITS_H_
