/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_INTERVALS_H_
#define DOM_MEDIA_INTERVALS_H_

#include <algorithm>
#include <type_traits>

#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsTArray.h"

// Specialization for nsTArray CopyChooser.
namespace mozilla::media {
template <class T>
class IntervalSet;
class TimeUnit;
}  // namespace mozilla::media

template <class E>
struct nsTArray_RelocationStrategy<mozilla::media::IntervalSet<E>> {
  typedef nsTArray_RelocateUsingMoveConstructor<mozilla::media::IntervalSet<E>>
      Type;
};

namespace mozilla::media {

/* Interval defines an interval between two points. Unlike a traditional
   interval [A,B] where A <= x <= B, the upper boundary B is exclusive: A <= x <
   B (e.g [A,B[ or [A,B) depending on where you're living) It provides basic
   interval arithmetic and fuzzy edges. The type T must provides a default
   constructor and +, -, <, <= and == operators.
 */
template <typename T>
class Interval {
 public:
  typedef Interval<T> SelfType;

  Interval() : mStart(T()), mEnd(T()), mFuzz(T()) {}

  template <typename StartArg, typename EndArg>
  Interval(StartArg&& aStart, EndArg&& aEnd)
      : mStart(aStart), mEnd(aEnd), mFuzz() {
    MOZ_DIAGNOSTIC_ASSERT(mStart <= mEnd, "Invalid Interval");
  }

  template <typename StartArg, typename EndArg, typename FuzzArg>
  Interval(StartArg&& aStart, EndArg&& aEnd, FuzzArg&& aFuzz)
      : mStart(aStart), mEnd(aEnd), mFuzz(aFuzz) {
    MOZ_DIAGNOSTIC_ASSERT(mStart <= mEnd, "Invalid Interval");
  }

  Interval(const SelfType& aOther)
      : mStart(aOther.mStart), mEnd(aOther.mEnd), mFuzz(aOther.mFuzz) {}

  Interval(SelfType&& aOther)
      : mStart(std::move(aOther.mStart)),
        mEnd(std::move(aOther.mEnd)),
        mFuzz(std::move(aOther.mFuzz)) {}

  SelfType& operator=(const SelfType& aOther) = default;

  SelfType& operator=(SelfType&& aOther) {
    MOZ_ASSERT(&aOther != this, "self-moves are prohibited");
    this->~Interval();
    new (this) Interval(std::move(aOther));
    return *this;
  }

  // Basic interval arithmetic operator definition.
  SelfType operator+(const SelfType& aOther) const {
    return SelfType(mStart + aOther.mStart, mEnd + aOther.mEnd,
                    mFuzz + aOther.mFuzz);
  }

  SelfType operator+(const T& aVal) const {
    return SelfType(mStart + aVal, mEnd + aVal, mFuzz);
  }

  SelfType operator-(const SelfType& aOther) const {
    return SelfType(mStart - aOther.mEnd, mEnd - aOther.mStart,
                    mFuzz + aOther.mFuzz);
  }

  SelfType operator-(const T& aVal) const {
    return SelfType(mStart - aVal, mEnd - aVal, mFuzz);
  }

  SelfType& operator+=(const SelfType& aOther) {
    mStart += aOther.mStart;
    mEnd += aOther.mEnd;
    mFuzz += aOther.mFuzz;
    return *this;
  }

  SelfType& operator+=(const T& aVal) {
    mStart += aVal;
    mEnd += aVal;
    return *this;
  }

  SelfType& operator-=(const SelfType& aOther) {
    mStart -= aOther.mStart;
    mEnd -= aOther.mEnd;
    mFuzz += aOther.mFuzz;
    return *this;
  }

  SelfType& operator-=(const T& aVal) {
    mStart -= aVal;
    mEnd -= aVal;
    return *this;
  }

  bool operator==(const SelfType& aOther) const {
    return mStart == aOther.mStart && mEnd == aOther.mEnd;
  }

  bool operator!=(const SelfType& aOther) const { return !(*this == aOther); }

  bool Contains(const T& aX) const {
    return mStart - mFuzz <= aX && aX < mEnd + mFuzz;
  }

  bool ContainsStrict(const T& aX) const { return mStart <= aX && aX < mEnd; }

  bool ContainsWithStrictEnd(const T& aX) const {
    return mStart - mFuzz <= aX && aX < mEnd;
  }

  bool Contains(const SelfType& aOther) const {
    return (mStart - mFuzz <= aOther.mStart + aOther.mFuzz) &&
           (aOther.mEnd - aOther.mFuzz <= mEnd + mFuzz);
  }

  bool ContainsStrict(const SelfType& aOther) const {
    return mStart <= aOther.mStart && aOther.mEnd <= mEnd;
  }

  bool ContainsWithStrictEnd(const SelfType& aOther) const {
    return (mStart - mFuzz <= aOther.mStart + aOther.mFuzz) &&
           aOther.mEnd <= mEnd;
  }

  bool Intersects(const SelfType& aOther) const {
    return (mStart - mFuzz < aOther.mEnd + aOther.mFuzz) &&
           (aOther.mStart - aOther.mFuzz < mEnd + mFuzz);
  }

  bool IntersectsStrict(const SelfType& aOther) const {
    return mStart < aOther.mEnd && aOther.mStart < mEnd;
  }

  // Same as Intersects, but including the boundaries.
  bool Touches(const SelfType& aOther) const {
    return (mStart - mFuzz <= aOther.mEnd + aOther.mFuzz) &&
           (aOther.mStart - aOther.mFuzz <= mEnd + mFuzz);
  }

  // Returns true if aOther is strictly to the right of this and contiguous.
  // This operation isn't commutative.
  bool Contiguous(const SelfType& aOther) const {
    return mEnd <= aOther.mStart &&
           aOther.mStart - mEnd <= mFuzz + aOther.mFuzz;
  }

  bool RightOf(const SelfType& aOther) const {
    return aOther.mEnd - aOther.mFuzz <= mStart + mFuzz;
  }

  bool LeftOf(const SelfType& aOther) const {
    return mEnd - mFuzz <= aOther.mStart + aOther.mFuzz;
  }

  SelfType Span(const SelfType& aOther) const {
    if (IsEmpty()) {
      return aOther;
    }
    SelfType result(*this);
    if (aOther.mStart < mStart) {
      result.mStart = aOther.mStart;
    }
    if (mEnd < aOther.mEnd) {
      result.mEnd = aOther.mEnd;
    }
    if (mFuzz < aOther.mFuzz) {
      result.mFuzz = aOther.mFuzz;
    }
    return result;
  }

  SelfType Intersection(const SelfType& aOther) const {
    const T& s = std::max(mStart, aOther.mStart);
    const T& e = std::min(mEnd, aOther.mEnd);
    const T& f = std::max(mFuzz, aOther.mFuzz);
    if (s < e) {
      return SelfType(s, e, f);
    }
    // Return an empty interval.
    return SelfType();
  }

  T Length() const { return mEnd - mStart; }

  bool IsEmpty() const { return mStart == mEnd; }

  void SetFuzz(const T& aFuzz) { mFuzz = aFuzz; }

  // Returns true if the two intervals intersect with this being on the right
  // of aOther
  bool TouchesOnRight(const SelfType& aOther) const {
    return aOther.mStart <= mStart &&
           (mStart - mFuzz <= aOther.mEnd + aOther.mFuzz) &&
           (aOther.mStart - aOther.mFuzz <= mEnd + mFuzz);
  }

  // Returns true if the two intervals intersect with this being on the right
  // of aOther, ignoring fuzz.
  bool TouchesOnRightStrict(const SelfType& aOther) const {
    return aOther.mStart <= mStart && mStart <= aOther.mEnd;
  }

  nsCString ToString() const {
    if constexpr (std::is_same_v<T, TimeUnit>) {
      return nsPrintfCString("[%s, %s](%s)", mStart.ToString().get(),
                             mEnd.ToString().get(), mFuzz.ToString().get());
    } else if constexpr (std::is_same_v<T, double>) {
      return nsPrintfCString("[%lf, %lf](%lf)", mStart, mEnd, mFuzz);
    }
  }

  T mStart;
  T mEnd;
  T mFuzz;
};

// An IntervalSet in a collection of Intervals. The IntervalSet is always
// normalized.
template <typename T>
class IntervalSet {
 public:
  typedef IntervalSet<T> SelfType;
  typedef Interval<T> ElemType;
  typedef AutoTArray<ElemType, 4> ContainerType;
  typedef typename ContainerType::index_type IndexType;

  IntervalSet() = default;
  virtual ~IntervalSet() = default;

  IntervalSet(const SelfType& aOther) : mIntervals(aOther.mIntervals.Clone()) {}

  IntervalSet(SelfType&& aOther) {
    mIntervals.AppendElements(std::move(aOther.mIntervals));
  }

  explicit IntervalSet(const ElemType& aOther) {
    if (!aOther.IsEmpty()) {
      mIntervals.AppendElement(aOther);
    }
  }

  explicit IntervalSet(ElemType&& aOther) {
    if (!aOther.IsEmpty()) {
      mIntervals.AppendElement(std::move(aOther));
    }
  }

  bool operator==(const SelfType& aOther) const {
    return mIntervals == aOther.mIntervals;
  }

  bool operator!=(const SelfType& aOther) const {
    return mIntervals != aOther.mIntervals;
  }

  SelfType& operator=(const SelfType& aOther) {
    mIntervals = aOther.mIntervals.Clone();
    return *this;
  }

  SelfType& operator=(SelfType&& aOther) {
    MOZ_ASSERT(&aOther != this, "self-moves are prohibited");
    this->~IntervalSet();
    new (this) IntervalSet(std::move(aOther));
    return *this;
  }

  SelfType& operator=(const ElemType& aInterval) {
    mIntervals.Clear();
    if (!aInterval.IsEmpty()) {
      mIntervals.AppendElement(aInterval);
    }
    return *this;
  }

  SelfType& operator=(ElemType&& aInterval) {
    mIntervals.Clear();
    if (!aInterval.IsEmpty()) {
      mIntervals.AppendElement(std::move(aInterval));
    }
    return *this;
  }

  SelfType& Add(const SelfType& aIntervals) {
    if (aIntervals.mIntervals.Length() == 1) {
      Add(aIntervals.mIntervals[0]);
    } else {
      mIntervals.AppendElements(aIntervals.mIntervals);
      Normalize();
    }
    return *this;
  }

  SelfType& Add(const ElemType& aInterval) {
    if (aInterval.IsEmpty()) {
      return *this;
    }
    if (mIntervals.IsEmpty()) {
      mIntervals.AppendElement(aInterval);
      return *this;
    }
    ElemType& last = mIntervals.LastElement();
    if (aInterval.TouchesOnRight(last)) {
      last = last.Span(aInterval);
      return *this;
    }
    // Most of our actual usage is adding an interval that will be outside the
    // range. We can speed up normalization here.
    if (last.mStart <= aInterval.mStart) {
      MOZ_ASSERT(aInterval.RightOf(last));
      mIntervals.AppendElement(aInterval);
      return *this;
    }

    ContainerType normalized;
    ElemType current(aInterval);
    IndexType i = 0;
    for (; i < mIntervals.Length(); i++) {
      ElemType& interval = mIntervals[i];
      if (current.Touches(interval)) {
        current = current.Span(interval);
      } else if (current.LeftOf(interval)) {
        break;
      } else {
        normalized.AppendElement(std::move(interval));
      }
    }
    normalized.AppendElement(std::move(current));
    for (; i < mIntervals.Length(); i++) {
      normalized.AppendElement(std::move(mIntervals[i]));
    }
    mIntervals.Clear();
    mIntervals.AppendElements(std::move(normalized));

    return *this;
  }

  SelfType& operator+=(const SelfType& aIntervals) {
    Add(aIntervals);
    return *this;
  }

  SelfType& operator+=(const ElemType& aInterval) {
    Add(aInterval);
    return *this;
  }

  SelfType operator+(const SelfType& aIntervals) const {
    SelfType intervals(*this);
    intervals.Add(aIntervals);
    return intervals;
  }

  SelfType operator+(const ElemType& aInterval) const {
    SelfType intervals(*this);
    intervals.Add(aInterval);
    return intervals;
  }

  friend SelfType operator+(const ElemType& aInterval,
                            const SelfType& aIntervals) {
    SelfType intervals;
    intervals.Add(aInterval);
    intervals.Add(aIntervals);
    return intervals;
  }

  // Excludes an interval from an IntervalSet.
  SelfType& operator-=(const ElemType& aInterval) {
    if (aInterval.IsEmpty() || mIntervals.IsEmpty()) {
      return *this;
    }
    if (mIntervals.Length() == 1 &&
        mIntervals[0].TouchesOnRightStrict(aInterval)) {
      // Fast path when we're removing from the front of a set with a
      // single interval. This is common for the buffered time ranges
      // we see on Twitch.
      if (aInterval.mEnd >= mIntervals[0].mEnd) {
        mIntervals.RemoveElementAt(0);
      } else {
        mIntervals[0].mStart = aInterval.mEnd;
        mIntervals[0].mFuzz = std::max(mIntervals[0].mFuzz, aInterval.mFuzz);
      }
      return *this;
    }

    // General case performed by inverting aInterval within the bounds of
    // mIntervals and then doing the intersection.
    T firstEnd = std::max(mIntervals[0].mStart, aInterval.mStart);
    T secondStart = std::min(mIntervals.LastElement().mEnd, aInterval.mEnd);
    ElemType startInterval(mIntervals[0].mStart, firstEnd);
    ElemType endInterval(secondStart, mIntervals.LastElement().mEnd);
    SelfType intervals(std::move(startInterval));
    intervals += std::move(endInterval);
    return Intersection(intervals);
  }

  SelfType& operator-=(const SelfType& aIntervals) {
    for (const auto& interval : aIntervals.mIntervals) {
      *this -= interval;
    }
    return *this;
  }

  SelfType operator-(const SelfType& aInterval) const {
    SelfType intervals(*this);
    intervals -= aInterval;
    return intervals;
  }

  SelfType operator-(const ElemType& aInterval) const {
    SelfType intervals(*this);
    intervals -= aInterval;
    return intervals;
  }

  // Mutate this IntervalSet to be the union of this and aOther.
  SelfType& Union(const SelfType& aOther) {
    Add(aOther);
    return *this;
  }

  SelfType& Union(const ElemType& aInterval) {
    Add(aInterval);
    return *this;
  }

  // Mutate this TimeRange to be the intersection of this and aOther.
  SelfType& Intersection(const SelfType& aOther) {
    ContainerType intersection;

    // Ensure the intersection has enough capacity to store the upper bound on
    // the intersection size. This ensures that we don't spend time reallocating
    // the storage as we append, at the expense of extra memory.
    intersection.SetCapacity(std::max(aOther.Length(), mIntervals.Length()));

    const ContainerType& other = aOther.mIntervals;
    IndexType i = 0, j = 0;
    for (; i < mIntervals.Length() && j < other.Length();) {
      if (mIntervals[i].IntersectsStrict(other[j])) {
        intersection.AppendElement(mIntervals[i].Intersection(other[j]));
      }
      if (mIntervals[i].mEnd < other[j].mEnd) {
        i++;
      } else {
        j++;
      }
    }
    mIntervals = std::move(intersection);
    return *this;
  }

  SelfType& Intersection(const ElemType& aInterval) {
    SelfType intervals(aInterval);
    return Intersection(intervals);
  }

  const ElemType& operator[](IndexType aIndex) const {
    return mIntervals[aIndex];
  }

  // Returns the start boundary of the first interval. Or a default constructed
  // T if IntervalSet is empty (and aExists if provided will be set to false).
  T GetStart(bool* aExists = nullptr) const {
    bool exists = !mIntervals.IsEmpty();

    if (aExists) {
      *aExists = exists;
    }

    if (exists) {
      return mIntervals[0].mStart;
    } else {
      return T();
    }
  }

  // Returns the end boundary of the last interval. Or a default constructed T
  // if IntervalSet is empty (and aExists if provided will be set to false).
  T GetEnd(bool* aExists = nullptr) const {
    bool exists = !mIntervals.IsEmpty();
    if (aExists) {
      *aExists = exists;
    }

    if (exists) {
      return mIntervals.LastElement().mEnd;
    } else {
      return T();
    }
  }

  IndexType Length() const { return mIntervals.Length(); }

  bool IsEmpty() const { return mIntervals.IsEmpty(); }

  T Start(IndexType aIndex) const { return mIntervals[aIndex].mStart; }

  T Start(IndexType aIndex, bool& aExists) const {
    aExists = aIndex < mIntervals.Length();

    if (aExists) {
      return mIntervals[aIndex].mStart;
    } else {
      return T();
    }
  }

  T End(IndexType aIndex) const { return mIntervals[aIndex].mEnd; }

  T End(IndexType aIndex, bool& aExists) const {
    aExists = aIndex < mIntervals.Length();

    if (aExists) {
      return mIntervals[aIndex].mEnd;
    } else {
      return T();
    }
  }

  bool Contains(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.Contains(aInterval)) {
        return true;
      }
    }
    return false;
  }

  bool ContainsStrict(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.ContainsStrict(aInterval)) {
        return true;
      }
    }
    return false;
  }

  bool Contains(const T& aX) const {
    for (const auto& interval : mIntervals) {
      if (interval.Contains(aX)) {
        return true;
      }
    }
    return false;
  }

  bool ContainsStrict(const T& aX) const {
    for (const auto& interval : mIntervals) {
      if (interval.ContainsStrict(aX)) {
        return true;
      }
    }
    return false;
  }

  bool ContainsWithStrictEnd(const T& aX) const {
    for (const auto& interval : mIntervals) {
      if (interval.ContainsWithStrictEnd(aX)) {
        return true;
      }
    }
    return false;
  }

  bool ContainsWithStrictEnd(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.ContainsWithStrictEnd(aInterval)) {
        return true;
      }
    }
    return false;
  }

  bool Intersects(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.Intersects(aInterval)) {
        return true;
      }
    }
    return false;
  }

  bool IntersectsStrict(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.IntersectsStrict(aInterval)) {
        return true;
      }
    }
    return false;
  }

  // Returns if there's any intersection between this and aOther.
  bool IntersectsStrict(const SelfType& aOther) const {
    const ContainerType& other = aOther.mIntervals;
    IndexType i = 0, j = 0;
    for (; i < mIntervals.Length() && j < other.Length();) {
      if (mIntervals[i].IntersectsStrict(other[j])) {
        return true;
      }
      if (mIntervals[i].mEnd < other[j].mEnd) {
        i++;
      } else {
        j++;
      }
    }
    return false;
  }

  bool IntersectsWithStrictEnd(const ElemType& aInterval) const {
    for (const auto& interval : mIntervals) {
      if (interval.IntersectsWithStrictEnd(aInterval)) {
        return true;
      }
    }
    return false;
  }

  // Shift all values by aOffset.
  SelfType& Shift(const T& aOffset) {
    for (auto& interval : mIntervals) {
      interval.mStart = interval.mStart + aOffset;
      interval.mEnd = interval.mEnd + aOffset;
    }
    return *this;
  }

  void SetFuzz(const T& aFuzz) {
    for (auto& interval : mIntervals) {
      interval.SetFuzz(aFuzz);
    }
    MergeOverlappingIntervals();
  }

  static const IndexType NoIndex = IndexType(-1);

  IndexType Find(const T& aValue) const {
    for (IndexType i = 0; i < mIntervals.Length(); i++) {
      if (mIntervals[i].Contains(aValue)) {
        return i;
      }
    }
    return NoIndex;
  }

  // Methods for range-based for loops.
  typename ContainerType::iterator begin() { return mIntervals.begin(); }

  typename ContainerType::const_iterator begin() const {
    return mIntervals.begin();
  }

  typename ContainerType::const_iterator cbegin() const {
    return mIntervals.cbegin();
  }

  typename ContainerType::iterator end() { return mIntervals.end(); }

  typename ContainerType::const_iterator end() const {
    return mIntervals.end();
  }

  typename ContainerType::const_iterator cend() const {
    return mIntervals.cend();
  }

  ElemType& LastInterval() {
    MOZ_ASSERT(!mIntervals.IsEmpty());
    return mIntervals.LastElement();
  }

  const ElemType& LastInterval() const {
    MOZ_ASSERT(!mIntervals.IsEmpty());
    return mIntervals.LastElement();
  }

  void Clear() { mIntervals.Clear(); }

 protected:
  ContainerType mIntervals;

 private:
  void Normalize() {
    if (mIntervals.Length() < 2) {
      return;
    }
    mIntervals.Sort(CompareIntervals());
    MergeOverlappingIntervals();
  }

  void MergeOverlappingIntervals() {
    if (mIntervals.Length() < 2) {
      return;
    }

    // This merges the intervals in place.
    IndexType read = 0;
    IndexType write = 0;
    while (read < mIntervals.Length()) {
      ElemType current(mIntervals[read]);
      read++;
      while (read < mIntervals.Length() && current.Touches(mIntervals[read])) {
        current = current.Span(mIntervals[read]);
        read++;
      }
      mIntervals[write] = current;
      write++;
    }
    mIntervals.SetLength(write);
  }

  struct CompareIntervals {
    bool Equals(const ElemType& aT1, const ElemType& aT2) const {
      return aT1.mStart == aT2.mStart && aT1.mEnd == aT2.mEnd;
    }

    bool LessThan(const ElemType& aT1, const ElemType& aT2) const {
      return aT1.mStart < aT2.mStart;
    }
  };
};

// clang doesn't allow for this to be defined inline of IntervalSet.
template <typename T>
IntervalSet<T> Union(const IntervalSet<T>& aIntervals1,
                     const IntervalSet<T>& aIntervals2) {
  IntervalSet<T> intervals(aIntervals1);
  intervals.Union(aIntervals2);
  return intervals;
}

template <typename T>
IntervalSet<T> Intersection(const IntervalSet<T>& aIntervals1,
                            const IntervalSet<T>& aIntervals2) {
  IntervalSet<T> intersection(aIntervals1);
  intersection.Intersection(aIntervals2);
  return intersection;
}

}  // namespace mozilla::media

#endif  // DOM_MEDIA_INTERVALS_H_
