/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLCOMMANDQUEUE_H_
#define WEBGLCOMMANDQUEUE_H_

#include <type_traits>

#include "QueueParamTraits.h"
#include "WebGLTypes.h"
#include "mozilla/gfx/Logging.h"

namespace mozilla {

namespace webgl {

class RangeConsumerView final : public webgl::ConsumerView<RangeConsumerView> {
  RangedPtr<const uint8_t> mSrcItr;
  const RangedPtr<const uint8_t> mSrcEnd;

 public:
  auto Remaining() const { return *MaybeAs<size_t>(mSrcEnd - mSrcItr); }

  explicit RangeConsumerView(const Range<const uint8_t> range)
      : ConsumerView(this), mSrcItr(range.begin()), mSrcEnd(range.end()) {
    (void)Remaining();  // assert size non-negative
  }

  void AlignTo(const size_t alignment) {
    const auto padToAlign = AlignmentOffset(alignment, mSrcItr.get());
    if (padToAlign > Remaining()) [[unlikely]] {
      mSrcItr = mSrcEnd;
      return;
    }
    mSrcItr += padToAlign;
  }

  template <typename T>
  Maybe<Range<const T>> ReadRange(const size_t elemCount) {
    constexpr auto alignment = alignof(T);
    AlignTo(alignment);

    constexpr auto elemSize = sizeof(T);
    const auto byteSizeChecked = CheckedInt<size_t>(elemCount) * elemSize;
    MOZ_RELEASE_ASSERT(byteSizeChecked.isValid());
    const auto& byteSize = byteSizeChecked.value();

    const auto remaining = Remaining();
    if (byteSize > remaining) [[unlikely]] {
      return {};
    }

    const auto begin = reinterpret_cast<const T*>(mSrcItr.get());
    mSrcItr += byteSize;
    return Some(Range<const T>{begin, elemCount});
  }
};

// -

namespace details {

class SizeOnlyProducerView final
    : public webgl::ProducerView<SizeOnlyProducerView> {
  struct Info {
    size_t requiredByteCount = 0;
    size_t alignmentOverhead = 0;
  };
  Info mInfo;

 public:
  SizeOnlyProducerView() : ProducerView(this) {}

  template <typename T>
  bool WriteFromRange(const Range<const T>& src) {
    constexpr auto alignment = alignof(T);
    const size_t byteSize = ByteSize(src);
    // printf_stderr("SizeOnlyProducerView: @%zu +%zu\n", alignment, byteSize);

    const auto padToAlign = AlignmentOffset(alignment, mInfo.requiredByteCount);
    mInfo.alignmentOverhead += padToAlign;

    mInfo.requiredByteCount += padToAlign;
    mInfo.requiredByteCount += byteSize;
    return true;
  }

  const auto& Info() const { return mInfo; }
};

// -

class RangeProducerView final : public webgl::ProducerView<RangeProducerView> {
  const RangedPtr<uint8_t> mDestBegin;
  const RangedPtr<uint8_t> mDestEnd;
  RangedPtr<uint8_t> mDestItr;

 public:
  auto Remaining() const { return *MaybeAs<size_t>(mDestEnd - mDestItr); }

  explicit RangeProducerView(const Range<uint8_t> range)
      : ProducerView(this),
        mDestBegin(range.begin()),
        mDestEnd(range.end()),
        mDestItr(mDestBegin) {
    (void)Remaining();  // assert size non-negative
  }

  template <typename T>
  bool WriteFromRange(const Range<const T>& src) {
    // uint32_t/float data may masquerade as a Range<uint8_t>.
    constexpr auto alignment = alignof(T);
    const size_t byteSize = ByteSize(src);
    // printf_stderr("RangeProducerView: @%zu +%zu\n", alignment, byteSize);

    const auto padToAlign = AlignmentOffset(alignment, mDestItr.get());
    mDestItr += padToAlign;

    MOZ_ASSERT(byteSize <= Remaining());
    if (byteSize) [[likely]] {
      memcpy(mDestItr.get(), src.begin().get(), byteSize);
    }
    mDestItr += byteSize;
    return true;
  }
};

// -

template <typename ProducerViewT>
inline void Serialize(ProducerViewT&) {}

template <typename ProducerViewT, typename Arg, typename... Args>
inline void Serialize(ProducerViewT& view, const Arg& arg,
                      const Args&... args) {
  MOZ_ALWAYS_TRUE(view.WriteParam(arg));
  Serialize(view, args...);
}

}  // namespace details

// -

template <typename... Args>
auto SerializationInfo(const Args&... args) {
  webgl::details::SizeOnlyProducerView sizeView;
  webgl::details::Serialize(sizeView, args...);
  return sizeView.Info();
}

template <typename... Args>
void Serialize(Range<uint8_t> dest, const Args&... args) {
  webgl::details::RangeProducerView view(dest);
  webgl::details::Serialize(view, args...);
}

// -

inline Maybe<uint16_t> Deserialize(RangeConsumerView&, size_t) { return {}; }

template <typename Arg, typename... Args>
inline Maybe<uint16_t> Deserialize(RangeConsumerView& view,
                                   const uint16_t argId, Arg& arg,
                                   Args&... args) {
  if (!webgl::QueueParamTraits<Arg>::Read(view, &arg)) {
    return Some(argId);
  }
  return Deserialize(view, argId + 1, args...);
}

}  // namespace webgl

// -

template <class R, class... Args>
using fn_t = R(Args...);

// The MethodDispatcher setup uses a CommandSink to read parameters, call the
// given method using the given synchronization protocol, and provide
// compile-time lookup of the ID by class method.
// To use this system, first define a dispatcher subclass of
// EmptyMethodDispatcher.  This class must be parameterized by command ID.
//
// Example:
// template <size_t id=0> class MyDispatcher
//    : public EmptyMethodDispatcher<MyDispatcher> {};
//
// Then, for each command handled, specialize this to subclass MethodDispatcher.
// The subclass must define the Method.  It may optionally define isSync for
// synchronous methods.
//
// Example:
// template <>
// class MyDispatcher<0>
//    : public MethodDispatcher<MyDispatcher, 0,
//        decltype(&MyClass::MyMethod), MyClass::MyMethod> {};
//
// The method may then be called from the source and run on the sink.
//
// Example:
// int result = Run<MyClass::MyMethod>(param1, std::move(param2));

template <template <size_t> typename Derived>
class EmptyMethodDispatcher {
 public:
  template <typename ObjectT>
  static constexpr fn_t<bool, ObjectT&, webgl::RangeConsumerView&>*
  DispatchCommandFuncById(const size_t id) {
    return nullptr;
  }
};

// -

template <typename ReturnT, typename ObjectT, typename... Args>
std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...> ArgsTuple(
    ReturnT (ObjectT::*)(Args... args)) {
  return std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...>{};
}

template <typename ReturnT, typename ObjectT, typename... Args>
std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...> ArgsTuple(
    ReturnT (ObjectT::*)(Args... args) const) {
  return std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...>{};
}

// Derived type must be parameterized by the ID.
template <template <size_t> typename Derived, size_t _Id, typename MethodType,
          MethodType _Method>
class MethodDispatcher {
  static constexpr auto Id = _Id;
  static constexpr auto Method = _Method;
  using DerivedType = Derived<Id>;

 public:
  template <class ObjectT>
  static constexpr fn_t<bool, ObjectT&, webgl::RangeConsumerView&>*
  DispatchCommandFuncById(const size_t targetId) {
    if (targetId != Id)
      return Derived<Id + 1>::template DispatchCommandFuncById<ObjectT>(
          targetId);

    return [](ObjectT& obj, webgl::RangeConsumerView& view) -> bool {
      const auto viewWas = view;
      (void)viewWas;  // For debugging.

      auto argsTuple = ArgsTuple(Method);
      return std::apply(
          [&](auto&... args) {
            const auto badArgId = webgl::Deserialize(view, 1, args...);
            if (badArgId) {
              const auto& name = DerivedType::Name();
              gfxCriticalError() << "webgl::Deserialize failed for " << name
                                 << " arg " << *badArgId;
              return false;
            }
            (obj.*Method)(args...);
            return true;
          },
          argsTuple);
    };
  }
};

struct WebGLMethodInfo {
  enum Flags : uint8_t {};

  size_t id = 0;
  uint8_t flags = 0;

  template <typename MethodT, MethodT Method>
  static WebGLMethodInfo Get();
};

}  // namespace mozilla

#endif  // WEBGLCOMMANDQUEUE_H_
