// Copyright 2025 The ANGLE Project Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is Chromium's base/containers/span.h, modified to support C++17
// as part of the PDFium project, stubbed to be self-contained, and then
// modified to conform to ANGLE:
//  -- fixed missing constexpr as exercised by test.
//  -- added operator==().

#ifndef COMMON_SPAN_H_
#define COMMON_SPAN_H_

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "common/base/anglebase/logging.h"
#include "common/unsafe_buffers.h"

namespace angle
{

constexpr size_t dynamic_extent = static_cast<size_t>(-1);

template <typename T>
using DefaultSpanInternalPtr = T *;

template <typename T,
          size_t Extent        = dynamic_extent,
          typename InternalPtr = DefaultSpanInternalPtr<T>>
class Span;

namespace internal
{

template <typename T>
struct IsSpanImpl : std::false_type
{};

template <typename T>
struct IsSpanImpl<Span<T>> : std::true_type
{};

template <typename T>
using IsSpan = IsSpanImpl<typename std::decay<T>::type>;

template <typename T>
struct IsStdArrayImpl : std::false_type
{};

template <typename T, size_t N>
struct IsStdArrayImpl<std::array<T, N>> : std::true_type
{};

template <typename T>
using IsStdArray = IsStdArrayImpl<typename std::decay<T>::type>;

template <typename From, typename To>
using IsLegalSpanConversion = std::is_convertible<From *, To *>;

template <typename Container, typename T>
using ContainerHasConvertibleData = IsLegalSpanConversion<
    typename std::remove_pointer<decltype(std::declval<Container>().data())>::type,
    T>;
template <typename Container>
using ContainerHasIntegralSize = std::is_integral<decltype(std::declval<Container>().size())>;

template <typename From, typename To>
using EnableIfLegalSpanConversion =
    typename std::enable_if<IsLegalSpanConversion<From, To>::value>::type;

// SFINAE check if Container can be converted to a Span<T>. Note that the
// implementation details of this check differ slightly from the requirements in
// the working group proposal: in particular, the proposal also requires that
// the container conversion constructor participate in overload resolution only
// if two additional conditions are true:
//
//   1. Container implements operator[].
//   2. Container::value_type matches remove_const_t<element_type>.
//
// The requirements are relaxed slightly here: in particular, not requiring (2)
// means that an immutable Span can be easily constructed from a mutable
// container.
template <typename Container, typename T>
using EnableIfSpanCompatibleContainer =
    typename std::enable_if<!internal::IsSpan<Container>::value &&
                            !internal::IsStdArray<Container>::value &&
                            ContainerHasConvertibleData<Container, T>::value &&
                            ContainerHasIntegralSize<Container>::value>::type;

template <typename Container, typename T>
using EnableIfConstSpanCompatibleContainer =
    typename std::enable_if<std::is_const<T>::value && !internal::IsSpan<Container>::value &&
                            !internal::IsStdArray<Container>::value &&
                            ContainerHasConvertibleData<Container, T>::value &&
                            ContainerHasIntegralSize<Container>::value>::type;

}  // namespace internal

// A Span is a value type that represents an array of elements of type T. Since
// it only consists of a pointer to memory with an associated size, it is very
// light-weight. It is cheap to construct, copy, move and use spans, so that
// users are encouraged to use it as a pass-by-value parameter. A Span does not
// own the underlying memory, so care must be taken to ensure that a Span does
// not outlive the backing store.
//
// Differences from the working group proposal
// -------------------------------------------
//
// https://wg21.link/P0122 is the latest working group proposal, Chromium
// currently implements R6.
//
// Differences in constants and types:
// - Span has a capital "S".
// - Allows custom smart pointer types in internal representation, if desired.
// - no element_type type alias
// - no index_type type alias
// - no different_type type alias
// - no extent constant
//
// Differences from [span.cons]:
// - no constructor from a pointer range
//
// Differences from [span.sub]:
// - using size_t instead of ptrdiff_t for indexing
//
// Differences from [span.obs]:
// - using size_t instead of ptrdiff_t to represent size()
//
// Differences from [span.elem]:
// - no operator ()()
// - using size_t instead of ptrdiff_t for indexing
//
// Additions beyond the C++ standard draft
// - as_chars() function.
// - as_writable_chars() function.
// - as_byte_span() function.
// - as_writable_byte_span() function.
// - span_from_ref() function.
// - byte_span_from_ref() function.

// [span], class template span
template <typename T, size_t Extent, typename InternalPtr>
class Span
{
  public:
    using value_type             = typename std::remove_cv<T>::type;
    using pointer                = T *;
    using reference              = T &;
    using iterator               = T *;
    using const_iterator         = const T *;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // [span.cons], span constructors, copy, assignment, and destructor
    constexpr Span() noexcept = default;

    ANGLE_UNSAFE_BUFFER_USAGE constexpr Span(T *data, size_t size) noexcept
        : data_(data), size_(size)
    {
        DCHECK(data_ || size_ == 0);
    }

    // TODO(dcheng): Implement construction from a |begin| and |end| pointer.
    template <size_t N>
    constexpr Span(T (&array)[N]) noexcept
        // SAFETY: The type signature guarantees `array` contains `N` elements.
        : ANGLE_UNSAFE_BUFFERS(Span(array, N))
    {
        static_assert(Extent == dynamic_extent || Extent == N);
    }

    template <size_t N>
    constexpr Span(std::array<T, N> &array) noexcept
        // SAFETY: The type signature guarantees `array` contains `N` elements.
        : ANGLE_UNSAFE_BUFFERS(Span(array.data(), N))
    {
        static_assert(Extent == dynamic_extent || Extent == N);
    }

    template <typename U, size_t N, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
    constexpr Span(const std::array<U, N> &array) noexcept
        // SAFETY: The type signature guarantees `array` contains `N` elements.
        : ANGLE_UNSAFE_BUFFERS(Span(array.data(), N))
    {
        static_assert(Extent == dynamic_extent || Extent == N);
    }

    // Conversion from a container that provides |T* data()| and |integral_type
    // size()|. Note that |data()| may not return nullptr for some empty
    // containers, which can lead to container overflow errors when probing
    // raw ptrs.
    template <typename Container,
              typename = internal::EnableIfSpanCompatibleContainer<Container, T>>
    constexpr Span(Container &container)
        // SAFETY: `size()` is the number of elements that can be safely accessed
        // at `data()`.
        : ANGLE_UNSAFE_BUFFERS(Span(container.data(), container.size()))
    {}

    template <typename Container,
              typename = internal::EnableIfConstSpanCompatibleContainer<Container, T>>
    constexpr Span(const Container &container)
        // SAFETY: `size()` is exactly the number of elements in the initializer
        // list, so accessing that many will be safe.
        : ANGLE_UNSAFE_BUFFERS(Span(container.data(), container.size()))
    {}

    constexpr Span(const Span &other) noexcept = default;

    // Conversions from spans of compatible types: this allows a Span<T> to be
    // seamlessly used as a Span<const T>, but not the other way around.
    template <typename U,
              size_t M,
              typename R,
              typename = internal::EnableIfLegalSpanConversion<U, T>>
    constexpr Span(const Span<U, M, R> &other)
        // SAFETY: `size()` is the number of elements that can be safely accessed
        // at `data()`.
        : ANGLE_UNSAFE_BUFFERS(Span(other.data(), other.size()))
    {
        static_assert(Extent == dynamic_extent || Extent == M,
                      "Assigning to fixed span from incompatible span");
    }

    Span &operator=(const Span &other) noexcept = default;
    Span &operator=(Span &&other) noexcept      = default;

    ~Span() noexcept = default;

    template <typename U, size_t M>
    bool operator==(const Span<U, M> &other) const
    {
        return std::equal(begin(), end(), other.begin(), other.end());
    }
    template <typename U, size_t M>
    bool operator!=(const Span<U, M> &other) const
    {
        return !std::equal(begin(), end(), other.begin(), other.end());
    }

    // Performs a deep copy of the elements referenced by `other` to those
    // referenced by `this`. The spans must be the same size.
    // (Not in `std::`; inspired by Rust's `slice::copy_from_slice()`.)
    template <typename U,
              size_t M,
              typename R,
              typename V = T,
              typename   = std::enable_if_t<
                    !std::is_const<V>::value && std::is_convertible<U *, const T *>::value &&
                    (Extent == dynamic_extent || M == dynamic_extent || M == Extent)>>
    void copy_from(const Span<U, M, R> &other) const
    {
        CHECK(size() == other.size());
        if (std::less_equal<const void *>{}(data(), other.data()))
        {
            std::copy(other.begin(), other.end(), begin());
        }
        else
        {
            std::copy_backward(other.begin(), other.end(), end());
        }
    }

    template <typename Container,
              typename V = T,
              typename   = internal::EnableIfSpanCompatibleContainer<Container, V>,
              typename   = std::enable_if_t<!std::is_const<V>::value>>
    void copy_from(const Container &container) const
    {
        copy_from(Span<const T, Extent>(container));
    }

    // Like `copy_from()`, but allows `other` to be smaller than this span: copies
    // `other` into the first `other.size()` elements and leaves the rest unwritten.
    // (Not in `std::`; matches `base::span::copy_prefix_from()`.)
    template <typename U,
              size_t M,
              typename R,
              typename V = T,
              typename   = std::enable_if_t<
                  !std::is_const<V>::value && std::is_convertible<U *, const T *>::value &&
                  (Extent == dynamic_extent || M == dynamic_extent || M <= Extent)>>
    void copy_prefix_from(const Span<U, M, R> &other) const
    {
        first(other.size()).copy_from(other);
    }

    template <size_t N>
    constexpr std::optional<Span<T, N>> to_fixed_extent() const
    {
        return (size() == N)
                   ? std::optional<Span<T, N>>(ANGLE_UNSAFE_BUFFERS(Span<T, N>(data(), N)))
                   : std::nullopt;
    }

    template <size_t Offset,
              typename = std::enable_if_t<(Extent == dynamic_extent || Offset <= Extent)>>
    constexpr auto split_at() const
    {
        CHECK(Offset <= size());
        return std::make_pair(first<Offset>(), subspan<Offset>());
    }

    constexpr auto split_at(size_t offset) const
    {
        CHECK(offset <= size());
        return std::make_pair(first(offset), subspan(offset));
    }

    // [span.sub], span subviews
    template <size_t Count>
    constexpr Span<T, Count> first() const
    {
        // TODO(tsepez): The following assert isn't yet good enough to replace
        // the runtime check since we are still allowing unchecked conversions
        // to arbitrary non-dynamic_extent spans.
        static_assert(Extent == dynamic_extent || Count <= Extent);
        CHECK(Count <= size_);
        // SAFETY: CHECK() on line above.
        return ANGLE_UNSAFE_BUFFERS(Span<T, Count>(data(), Count));
    }
    constexpr Span<T, dynamic_extent> first(size_t count) const
    {
        CHECK(count <= size_);
        // SAFETY: CHECK() on line above.
        return ANGLE_UNSAFE_BUFFERS(Span(static_cast<T *>(data_), count));
    }

    template <size_t Count>
    constexpr Span<T, Count> last() const
    {
        // TODO(tsepez): The following assert isn't yet good enough to replace
        // the runtime check since we are still allowing unchecked conversions
        // to arbitrary non-dynamic_extent spans.
        static_assert(Extent == dynamic_extent || Count <= Extent);
        CHECK(Count <= size_);
        // SAFETY: CHECK() on line above.
        return ANGLE_UNSAFE_BUFFERS(Span<T, Count>(data() + (size_ - Count), Count));
    }
    constexpr Span<T, dynamic_extent> last(size_t count) const
    {
        CHECK(count <= size_);
        // SAFETY: CHECK() on line above.
        return ANGLE_UNSAFE_BUFFERS(Span(static_cast<T *>(data_) + (size_ - count), count));
    }

    template <size_t Offset, size_t Count = dynamic_extent>
    constexpr Span<T, dynamic_extent> subspan() const
    {
        // TODO(tsepez): The following check isn't yet good enough to replace
        // the runtime check since we are still allowing unchecked conversions
        // to arbitrary non-dynamic_extent spans.
        static_assert(Extent == dynamic_extent || Count == dynamic_extent ||
                      Offset + Count <= Extent);
        return subspan(Offset, Count);
    }
    constexpr Span<T, dynamic_extent> subspan(size_t pos, size_t count = dynamic_extent) const
    {
        CHECK(pos <= size_);
        CHECK(count == dynamic_extent || count <= size_ - pos);
        // SAFETY: CHECK()s on lines above.
        return ANGLE_UNSAFE_BUFFERS(
            Span(static_cast<T *>(data_) + pos, count == dynamic_extent ? size_ - pos : count));
    }

    // [span.obs], span observers
    constexpr size_t size() const noexcept { return size_; }
    constexpr size_t size_bytes() const noexcept { return size() * sizeof(T); }
    constexpr bool empty() const noexcept { return size_ == 0; }

    // [span.elem], span element access
    constexpr T &operator[](size_t index) const noexcept
    {
        CHECK(index < size_);
        return ANGLE_UNSAFE_BUFFERS(static_cast<T *>(data_)[index]);
    }

    constexpr T &front() const noexcept
    {
        CHECK(!empty());
        return *data();
    }

    constexpr T &back() const noexcept
    {
        CHECK(!empty());
        return ANGLE_UNSAFE_BUFFERS(*(data() + size() - 1));
    }

    constexpr T *data() const noexcept { return static_cast<T *>(data_); }

    // [span.iter], span iterator support
    constexpr iterator begin() const noexcept { return static_cast<T *>(data_); }
    constexpr iterator end() const noexcept { return ANGLE_UNSAFE_BUFFERS(begin() + size_); }

    constexpr const_iterator cbegin() const noexcept { return begin(); }
    constexpr const_iterator cend() const noexcept { return end(); }

    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

    constexpr const_reverse_iterator crbegin() const noexcept
    {
        return const_reverse_iterator(cend());
    }
    constexpr const_reverse_iterator crend() const noexcept
    {
        return const_reverse_iterator(cbegin());
    }

  private:
    InternalPtr data_ = nullptr;
    size_t size_      = 0;
};

// Deduction guides.
template <typename T, size_t N>
Span(T (&)[N]) -> Span<T, N>;

template <typename T, size_t N>
Span(const T (&)[N]) -> Span<const T, N>;

template <typename T, size_t N>
Span(std::array<T, N> &) -> Span<T, N>;

template <typename T, size_t N>
Span(const std::array<T, N> &) -> Span<const T, N>;

template <typename Container,
          typename = internal::EnableIfSpanCompatibleContainer<
              Container,
              std::remove_pointer_t<decltype(std::declval<Container>().data())>>>
Span(Container &&) -> Span<std::remove_pointer_t<decltype(std::declval<Container>().data())>>;

// [span.objectrep], views of object representation
template <typename T, size_t N, typename P>
Span<const uint8_t> as_bytes(Span<T, N, P> s) noexcept
{
    // SAFETY: from size_bytes() method.
    return ANGLE_UNSAFE_BUFFERS(
        Span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size_bytes()));
}

template <typename T,
          size_t N,
          typename P,
          typename U = typename std::enable_if<!std::is_const<T>::value>::type>
Span<uint8_t> as_writable_bytes(Span<T, N, P> s) noexcept
{
    // SAFETY: from size_bytes() method.
    return ANGLE_UNSAFE_BUFFERS(
        Span<uint8_t>(reinterpret_cast<uint8_t *>(s.data()), s.size_bytes()));
}

template <typename T, size_t N, typename P>
Span<const char> as_chars(Span<T, N, P> s) noexcept
{
    // SAFETY: from size_bytes() method.
    return ANGLE_UNSAFE_BUFFERS(
        Span<const char>(reinterpret_cast<const char *>(s.data()), s.size_bytes()));
}

template <typename T,
          size_t N,
          typename P,
          typename U = typename std::enable_if<!std::is_const<T>::value>::type>
Span<char> as_writable_chars(Span<T, N, P> s) noexcept
{
    // SAFETY: from size_bytes() method.
    return ANGLE_UNSAFE_BUFFERS(Span<char>(reinterpret_cast<char *>(s.data()), s.size_bytes()));
}

// `span_from_ref` converts a reference to T into a span of length 1.  This is a
// non-std helper that is inspired by the `std::slice::from_ref()` function from
// Rust.
template <typename T>
static constexpr Span<T> span_from_ref(T &single_object) noexcept
{
    // SAFETY: single object passed by reference.
    return ANGLE_UNSAFE_BUFFERS(Span<T>(&single_object, 1u));
}

// `byte_span_from_ref` converts a reference to T into a span of uint8_t of
// length sizeof(T).  This is a non-std helper that is a sugar for
// `as_writable_bytes(span_from_ref(x))`.
template <typename T>
static constexpr Span<const uint8_t> byte_span_from_ref(const T &single_object) noexcept
{
    return as_bytes(span_from_ref(single_object));
}
template <typename T>
static constexpr Span<uint8_t> byte_span_from_ref(T &single_object) noexcept
{
    return as_writable_bytes(span_from_ref(single_object));
}

// Convenience function for converting an object which is itself convertible
// to span into a span of bytes (i.e. span of const uint8_t). Typically used
// to convert std::string or string-objects holding chars, or std::vector
// or vector-like objects holding other scalar types, prior to passing them
// into an API that requires byte spans.
template <typename T>
Span<const uint8_t> as_byte_span(const T &arg)
{
    return as_bytes(Span(arg));
}
template <typename T>
Span<const uint8_t> as_byte_span(T &&arg)
{
    return as_bytes(Span(arg));
}

// Convenience function for converting an object which is itself convertible
// to span into a span of mutable bytes (i.e. span of uint8_t). Typically used
// to convert std::string or string-objects holding chars, or std::vector
// or vector-like objects holding other scalar types, prior to passing them
// into an API that requires mutable byte spans.
template <typename T>
constexpr Span<uint8_t> as_writable_byte_span(T &&arg)
{
    return as_writable_bytes(Span(arg));
}

template <typename ElementType, typename ByteType, size_t Extent, typename InternalPtr>
    requires(std::same_as<std::remove_cv_t<ByteType>, uint8_t> &&
             std::is_trivially_copyable_v<ElementType> &&
             (!std::is_const_v<ByteType> || std::is_const_v<ElementType>) &&
             (Extent == dynamic_extent || Extent % sizeof(ElementType) == 0))
[[nodiscard]] constexpr auto reinterpret_span(Span<ByteType, Extent, InternalPtr> span)
{
    CHECK(reinterpret_cast<uintptr_t>(span.data()) % alignof(ElementType) == 0u);
    if constexpr (Extent == dynamic_extent)
    {
        CHECK(span.size_bytes() % sizeof(ElementType) == 0u);
    }
    ElementType *ptr = std::launder(reinterpret_cast<ElementType *>(span.data()));
    if constexpr (Extent == dynamic_extent)
    {
        // SAFETY: We checked for proper alignment, size, strict aliasing rules, and
        // started the lifetime of the array.
        return ANGLE_UNSAFE_BUFFERS(
            Span<ElementType, dynamic_extent>(ptr, span.size_bytes() / sizeof(ElementType)));
    }
    else
    {
        // SAFETY: We checked for proper alignment, size, strict aliasing rules, and
        // started the lifetime of the array.
        constexpr size_t NewCount = Extent / sizeof(ElementType);
        return ANGLE_UNSAFE_BUFFERS(Span<ElementType, NewCount>(ptr, NewCount));
    }
}

}  // namespace angle

#endif  // COMMON_SPAN_H_
