/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_COPY_ON_WRITE_BUFFER_H_
#define RTC_BASE_COPY_ON_WRITE_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "api/scoped_refptr.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/system/rtc_export.h"
#include "rtc_base/type_traits.h"

namespace webrtc {

class RTC_EXPORT CopyOnWriteBuffer {
 public:
  using value_type = uint8_t;
  using const_iterator = std::span<const uint8_t>::iterator;

  // An empty buffer.
  CopyOnWriteBuffer();
  // Share the data with an existing buffer.
  CopyOnWriteBuffer(const CopyOnWriteBuffer& buf);
  CopyOnWriteBuffer& operator=(const CopyOnWriteBuffer& buf) = default;

  // Move contents from an existing buffer.
  CopyOnWriteBuffer(CopyOnWriteBuffer&& buf) noexcept;
  CopyOnWriteBuffer& operator=(CopyOnWriteBuffer&& buf) {
    RTC_DCHECK(IsConsistent());
    RTC_DCHECK(buf.IsConsistent());
    buffer_ = std::exchange(buf.buffer_, nullptr);
    offset_ = std::exchange(buf.offset_, 0);
    size_ = std::exchange(buf.size_, 0);
    return *this;
  }

  // Construct a buffer from a string, convenient for unittests.
  explicit CopyOnWriteBuffer(absl::string_view s);

  // Construct a buffer with the specified number of uninitialized bytes.
  explicit CopyOnWriteBuffer(size_t size);
  CopyOnWriteBuffer(size_t size, size_t capacity);

  explicit CopyOnWriteBuffer(std::span<const uint8_t> data);
  CopyOnWriteBuffer(std::span<const uint8_t> data, size_t capacity);

  // Construct a buffer and copy the specified number of bytes into it. The
  // source array may be (const) uint8_t*, int8_t*, or char*.
  template <typename T,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  CopyOnWriteBuffer(const T* data, size_t size, size_t capacity)
      : CopyOnWriteBuffer(
            std::span(reinterpret_cast<const uint8_t*>(data), size),
            capacity) {}

  ~CopyOnWriteBuffer();

  // Get a pointer to the data. Just .data() will give you a (const) uint8_t*,
  // but you may also use .data<int8_t>() and .data<char>().
  template <typename T = uint8_t,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  const T* data() const {
    return cdata<T>();
  }

  // Returns writable span of size `size()`. This will create a copy of the
  // underlying data if it is shared with other buffers.
  std::span<uint8_t> AsMutableSpan();

  // Get writable pointer to the data. This will create a copy of the underlying
  // data if it is shared with other buffers.
  // TODO(bugs.webrtc.org/478086887): Deprecate after migrating all usages in
  // webrtc to a safer `AsMutableSpan`.
  template <typename T = uint8_t,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  T* MutableData() {
    RTC_DCHECK(IsConsistent());
    if (!buffer_) {
      return nullptr;
    }
    UnshareAndEnsureCapacity(capacity());
    return reinterpret_cast<T*>(buffer_->data().subspan(offset_).data());
  }

  // Get const pointer to the data. This will not create a copy of the
  // underlying data if it is shared with other buffers.
  template <typename T = uint8_t,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  const T* cdata() const {
    return reinterpret_cast<const T*>(AsConstSpan().data());
  }

  bool empty() const { return size_ == 0; }

  size_t size() const {
    RTC_DCHECK(IsConsistent());
    return size_;
  }

  size_t capacity() const {
    RTC_DCHECK(IsConsistent());
    return buffer_ ? buffer_->capacity() - offset_ : 0;
  }

  const_iterator begin() const { return AsConstSpan().begin(); }
  const_iterator end() const { return AsConstSpan().end(); }

  bool operator==(const CopyOnWriteBuffer& buf) const;

  uint8_t operator[](size_t index) const {
    RTC_DCHECK_LT(index, size());
    return AsConstSpan()[index];
  }

  // Replaces the content of the buffer with `data`.
  // This function doesn't decrease the capacity.
  void Set(std::span<const uint8_t> data);

  // Appends `data` to the buffer.
  // This function doesn't decrease the capacity.
  void Append(std::span<const uint8_t> data);

  // Replace the contents of the buffer. Accepts the same types as the
  // constructors.
  template <typename T,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  void SetData(const T* data, size_t size) {
    Set(std::span(reinterpret_cast<const uint8_t*>(data), size));
  }

  template <typename T,
            size_t N,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  void SetData(const T (&array)[N]) {
    Set(std::span(reinterpret_cast<const uint8_t*>(std::data(array)), N));
  }

  // Append data to the buffer. Accepts the same types as the constructors.
  template <typename T,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  void AppendData(const T* data, size_t size) {
    Append(std::span(reinterpret_cast<const uint8_t*>(data), size));
  }

  template <typename T,
            size_t N,
            typename std::enable_if<
                internal::BufferCompat<uint8_t, T>::value>::type* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  void AppendData(const T (&array)[N]) {
    Append(std::span(reinterpret_cast<const uint8_t*>(std::data(array)), N));
  }

  template <typename VecT,
            typename ElemT = typename std::remove_pointer_t<
                decltype(std::declval<VecT>().data())>,
            typename std::enable_if_t<
                HasDataAndSize<VecT, ElemT>::value &&
                internal::BufferCompat<uint8_t, ElemT>::value>* = nullptr>
  ABSL_DEPRECATE_AND_INLINE()
  void AppendData(const VecT& v) {
    Append(std::span(reinterpret_cast<const uint8_t*>(std::data(v)),
                     std::size(v)));
  }

  // Sets the size of the buffer. If the new size is smaller than the old, the
  // buffer contents will be kept but truncated; if the new size is greater,
  // the existing contents will be kept and the new space will be
  // uninitialized.
  void SetSize(size_t size);

  // Ensure that the buffer size can be increased to at least capacity without
  // further reallocation. (Of course, this operation might need to reallocate
  // the buffer.)
  void EnsureCapacity(size_t capacity);

  // Resets the buffer to zero size without altering capacity. Works even if the
  // buffer has been moved from.
  void Clear();

  // Swaps two buffers.
  friend void swap(CopyOnWriteBuffer& a, CopyOnWriteBuffer& b) {
    a.buffer_.swap(b.buffer_);
    std::swap(a.offset_, b.offset_);
    std::swap(a.size_, b.size_);
  }

  CopyOnWriteBuffer Slice(size_t offset, size_t length) const {
    CopyOnWriteBuffer slice(*this);
    RTC_DCHECK_LE(offset, size_);
    RTC_DCHECK_LE(length + offset, size_);
    slice.offset_ += offset;
    slice.size_ = length;
    return slice;
  }

 private:
  class RTC_EXPORT RawBuffer {
   public:
    explicit RawBuffer(size_t size);

    std::span<uint8_t> data() { return std::span(data_.get(), size_); }
    size_t capacity() const { return size_; }

   private:
    const size_t size_;
    const std::unique_ptr<uint8_t[]> data_;
  };
  using RefCountedBuffer = FinalRefCountedObject<RawBuffer>;

  static scoped_refptr<RefCountedBuffer> CreateBuffer(size_t capacity);

  std::span<const uint8_t> AsConstSpan() const {
    RTC_DCHECK(IsConsistent());
    if (buffer_ == nullptr) {
      return {};
    }
    return buffer_->data().subspan(offset_, size_);
  }

  // Create a copy of the underlying data if it is referenced from other Buffer
  // objects or there is not enough capacity.
  void UnshareAndEnsureCapacity(size_t new_capacity);

  // Pre- and postcondition of all methods.
  bool IsConsistent() const {
    if (buffer_) {
      return buffer_->capacity() > 0 && offset_ + size_ <= buffer_->capacity();
    } else {
      return size_ == 0 && offset_ == 0;
    }
  }

  // buffer_ is either null, or points to an Buffer with capacity > 0.
  scoped_refptr<RefCountedBuffer> buffer_;
  // This buffer may represent a slice of a original data.
  size_t offset_;  // Offset of a current slice in the original data in buffer_.
                   // Should be 0 if the buffer_ is empty.
  size_t size_;    // Size of a current slice in the original data in buffer_.
                   // Should be 0 if the buffer_ is empty.
};

//------------------------------------------------------------------------------
// Implementation details follow
//------------------------------------------------------------------------------

inline CopyOnWriteBuffer::CopyOnWriteBuffer(std::span<const uint8_t> data)
    : CopyOnWriteBuffer(data, data.size()) {}

inline std::span<uint8_t> CopyOnWriteBuffer::AsMutableSpan() {
  RTC_DCHECK(IsConsistent());
  if (empty()) {
    return {};
  }
  UnshareAndEnsureCapacity(capacity());
  return buffer_->data().subspan(offset_, size_);
}

}  //  namespace webrtc


#endif  // RTC_BASE_COPY_ON_WRITE_BUFFER_H_
