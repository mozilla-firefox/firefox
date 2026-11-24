/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsRegion_h_
#define nsRegion_h_

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t, uint64_t

#include <ostream>  // for std::ostream
#include <utility>  // for mozilla::Move

#include "fmt/ostream.h"
#include "mozilla/ArrayView.h"      // for ArrayView
#include "mozilla/gfx/MatrixFwd.h"  // for mozilla::gfx::Matrix4x4
#include "nsCoord.h"                // for nscoord
#include "nsMargin.h"               // for nsIntMargin
#include "nsPoint.h"                // for nsIntPoint, nsPoint
#include "nsRect.h"                 // for mozilla::gfx::IntRect, nsRect
#include "nsRectAbsolute.h"
#include "nsRegionFwd.h"  // for nsIntRegion
#include "nsString.h"     // for nsCString
#include "nsTArray.h"

// Uncomment this line to get additional integrity checking.
// #define DEBUG_REGIONS
#ifdef DEBUG_REGIONS
#  include <sstream>
#endif

/* For information on the internal representation look at pixman-region.c
 *
 * This replaces an older homebrew implementation of nsRegion. The
 * representation used here may use more rectangles than nsRegion however, the
 * representation is canonical.  This means that there's no need for an
 * Optimize() method because for a paticular region there is only one
 * representation. This means that nsIntRegion will have more predictable
 * performance characteristics than the old nsRegion and should not become
 * degenerate.
 *
 * The pixman region code originates from X11 which has spread to a variety of
 * projects including Qt, Gtk, Wine. It should perform reasonably well.
 */

enum class VisitSide { TOP, BOTTOM, LEFT, RIGHT };

namespace regiondetails {
struct Band;
}

template <>
struct nsTArray_RelocationStrategy<regiondetails::Band> {
  typedef nsTArray_RelocateUsingMoveConstructor<regiondetails::Band> Type;
};

namespace regiondetails {

template <typename T, typename E>
class UncheckedArray : public T {
 public:
  using T::Elements;
  using T::Length;

  UncheckedArray() = default;
  MOZ_IMPLICIT UncheckedArray(T&& aSrc) : T(std::move(aSrc)) {}

  E& operator[](size_t aIndex) { return Elements()[aIndex]; }
  const E& operator[](size_t aIndex) const { return Elements()[aIndex]; }
  E& LastElement() { return Elements()[Length() - 1]; }
  const E& LastElement() const { return Elements()[Length() - 1]; }

  using iterator = E*;
  using const_iterator = const E*;

  iterator begin() { return iterator(Elements()); }
  const_iterator begin() const { return const_iterator(Elements()); }
  const_iterator cbegin() const { return begin(); }
  iterator end() { return iterator(Elements() + Length()); }
  const_iterator end() const { return const_iterator(Elements() + Length()); }
  const_iterator cend() const { return end(); }
};

struct Strip {
  // Default constructor should never be called, but is required for
  // vector::resize to compile.
  Strip() { MOZ_CRASH(); }
  Strip(int32_t aLeft, int32_t aRight) : left(aLeft), right(aRight) {}

  bool operator!=(const Strip& aOther) const {
    return left != aOther.left || right != aOther.right;
  }

  uint32_t Size() const { return right - left; }

  int32_t left;
  int32_t right;
};

struct Band {
  using Strip = regiondetails::Strip;
#ifndef DEBUG
  using StripArray =
      regiondetails::UncheckedArray<CopyableAutoTArray<Strip, 2>, Strip>;
#else
  using StripArray = CopyableAutoTArray<Strip, 2>;
#endif

  MOZ_IMPLICIT Band(const nsRectAbsolute& aRect)
      : top(aRect.Y()), bottom(aRect.YMost()) {
    mStrips.AppendElement(Strip{aRect.X(), aRect.XMost()});
  }

  Band(const Band& aOther) = default;
  Band(Band&& aOther) = default;

  void InsertStrip(const Strip& aStrip) {
    for (size_t i = 0; i < mStrips.Length(); i++) {
      Strip& strip = mStrips[i];
      if (strip.left > aStrip.right) {
        // Current strip is beyond aStrip, insert aStrip before.
        mStrips.InsertElementAt(i, aStrip);
        return;
      }

      if (strip.right < aStrip.left) {
        // Current strip is before aStrip, try the next.
        continue;
      }

      // Current strip intersects with aStrip, extend to the lext.
      strip.left = std::min(strip.left, aStrip.left);

      if (strip.right >= aStrip.right) {
        // Current strip extends beyond aStrip, done.
        return;
      }

      size_t next = i;
      next++;
      // Consume any subsequent strips intersecting with aStrip.
      while (next < mStrips.Length() && mStrips[next].left <= aStrip.right) {
        strip.right = mStrips[next].right;

        mStrips.RemoveElementAt(next);
      }

      // Extend the strip in case the aStrip goes on beyond it.
      strip.right = std::max(strip.right, aStrip.right);
      return;
    }
    mStrips.AppendElement(aStrip);
  }

  void SubStrip(const Strip& aStrip) {
    for (size_t i = 0; i < mStrips.Length(); i++) {
      Strip& strip = mStrips[i];
      if (strip.left > aStrip.right) {
        // Strip is entirely to the right of aStrip. Done.
        return;
      }

      if (strip.right < aStrip.left) {
        // Strip is entirely to the left of aStrip. Move on.
        continue;
      }

      if (strip.left < aStrip.left) {
        if (strip.right <= aStrip.right) {
          strip.right = aStrip.left;
          // This strip lies to the left of the start of aStrip.
          continue;
        }

        // aStrip is completely contained by this strip.
        Strip newStrip(aStrip.right, strip.right);
        strip.right = aStrip.left;
        if (i < mStrips.Length()) {
          i++;
          mStrips.InsertElementAt(i, newStrip);
        } else {
          mStrips.AppendElement(newStrip);
        }
        return;
      }

      // This strip lies to the right of the start of aStrip.
      if (strip.right <= aStrip.right) {
        // aStrip completely contains this strip.
        mStrips.RemoveElementAt(i);
        // Make sure we evaluate the strip now at i. This loop will increment.
        i--;
        continue;
      }
      strip.left = aStrip.right;
      return;
    }
  }

  bool Intersects(const Strip& aStrip) const {
    for (const Strip& strip : mStrips) {
      if (strip.left >= aStrip.right) {
        return false;
      }

      if (strip.right <= aStrip.left) {
        continue;
      }

      return true;
    }
    return false;
  }

  bool IntersectStripBounds(Strip& aStrip) const {
    bool intersected = false;

    int32_t rightMost;
    for (const Strip& strip : mStrips) {
      if (strip.left > aStrip.right) {
        break;
      }

      if (strip.right <= aStrip.left) {
        continue;
      }

      if (!intersected) {
        // First intersection, this is where the left side begins.
        aStrip.left = std::max(aStrip.left, strip.left);
      }

      intersected = true;
      // Expand to the right for each intersecting strip found.
      rightMost = std::min(strip.right, aStrip.right);
    }

    if (intersected) {
      aStrip.right = rightMost;
    } else {
      aStrip.right = aStrip.left = 0;
    }
    return intersected;
  }

  bool ContainsStrip(const Strip& aStrip) const {
    for (const Strip& strip : mStrips) {
      if (strip.left > aStrip.left) {
        return false;
      }

      if (strip.right >= aStrip.right) {
        return true;
      }
    }
    return false;
  }

  bool EqualStrips(const Band& aBand) const {
    if (mStrips.Length() != aBand.mStrips.Length()) {
      return false;
    }

    for (auto iter1 = mStrips.begin(), iter2 = aBand.mStrips.begin();
         iter1 != mStrips.end(); iter1++, iter2++) {
      if (*iter1 != *iter2) {
        return false;
      }
    }

    return true;
  }

  void IntersectStrip(const Strip& aStrip) {
    size_t i = 0;

    while (i < mStrips.Length()) {
      Strip& strip = mStrips[i];
      if (strip.right <= aStrip.left) {
        mStrips.RemoveElementAt(i);
        continue;
      }

      if (strip.left >= aStrip.right) {
        mStrips.TruncateLength(i);
        return;
      }

      strip.left = std::max(aStrip.left, strip.left);
      strip.right = std::min(aStrip.right, strip.right);
      i++;
    }
  }

  void IntersectStrips(const Band& aOther) {
    auto iter = mStrips.begin();
    auto iterOther = aOther.mStrips.begin();

    StripArray newStrips;

    // This function finds the intersection between two sets of strips.
    while (true) {
      while (true) {
        while (iter != mStrips.end() && iter->right <= iterOther->left) {
          // Increment our current strip until it ends beyond aOther's current
          // strip.
          iter++;
        }

        if (iter == mStrips.end()) {
          // End of our strips. Done.
          break;
        }

        while (iterOther != aOther.mStrips.end() &&
               iterOther->right <= iter->left) {
          // Increment aOther's current strip until it lies beyond our current
          // strip.
          iterOther++;
        }

        if (iterOther == aOther.mStrips.end()) {
          // End of aOther's strips. Done.
          break;
        }

        if (iterOther->left < iter->right) {
          // Intersection!
          break;
        }
      }

      if (iter == mStrips.end() || iterOther == aOther.mStrips.end()) {
        break;
      }

      newStrips.AppendElement(Strip(std::max(iter->left, iterOther->left),
                                    std::min(iterOther->right, iter->right)));

      if (iterOther->right < iter->right) {
        iterOther++;
        if (iterOther == aOther.mStrips.end()) {
          break;
        }
      } else {
        iter++;
      }
    }

    mStrips = std::move(newStrips);
  }

  bool Intersects(const Band& aOther) const {
    auto iter = mStrips.begin();
    auto iterOther = aOther.mStrips.begin();

    // This function finds the intersection between two sets of strips.
    while (true) {
      while (true) {
        while (iter != mStrips.end() && iter->right <= iterOther->left) {
          // Increment our current strip until it ends beyond aOther's current
          // strip.
          iter++;
        }

        if (iter == mStrips.end()) {
          // End of our strips. Done.
          break;
        }

        while (iterOther != aOther.mStrips.end() &&
               iterOther->right <= iter->left) {
          // Increment aOther's current strip until it lies beyond our current
          // strip.
          iterOther++;
        }

        if (iterOther == aOther.mStrips.end()) {
          // End of aOther's strips. Done.
          break;
        }

        if (iterOther->left < iter->right) {
          // Intersection!
          break;
        }
      }

      if (iter == mStrips.end() || iterOther == aOther.mStrips.end()) {
        break;
      }

      return true;
    }
    return false;
  }

  void SubStrips(const Band& aOther) {
    size_t idx = 0;
    auto iterOther = aOther.mStrips.begin();

    // This function finds the intersection between two sets of strips.
    while (true) {
      while (true) {
        while (idx < mStrips.Length() &&
               mStrips[idx].right <= iterOther->left) {
          // Increment our current strip until it ends beyond aOther's current
          // strip.
          idx++;
        }

        if (idx == mStrips.Length()) {
          // End of our strips. Done.
          break;
        }

        while (iterOther != aOther.mStrips.end() &&
               iterOther->right <= mStrips[idx].left) {
          // Increment aOther's current strip until it lies beyond our current
          // strip.
          iterOther++;
        }

        if (iterOther == aOther.mStrips.end()) {
          // End of aOther's strips. Done.
          break;
        }

        if (iterOther->left < mStrips[idx].right) {
          // Intersection!
          break;
        }
      }

      if (idx == mStrips.Length() || iterOther == aOther.mStrips.end()) {
        break;
      }

      if (mStrips[idx].left < iterOther->left) {
        size_t oldIdx = idx;
        // Our strip starts beyond other's
        if (mStrips[idx].right > iterOther->right) {
          // Our strip ends beyond other's as well.
          Strip newStrip(mStrips[idx]);
          newStrip.left = iterOther->right;
          mStrips.InsertElementAt(idx + 1, newStrip);
          idx++;
        }
        mStrips[oldIdx].right = iterOther->left;
        // Either idx was just incremented, or the current index no longer
        // intersects with iterOther.
        continue;
      } else if (mStrips[idx].right > iterOther->right) {
        mStrips[idx].left = iterOther->right;
        // Current strip no longer intersects, continue.
        iterOther++;
        if (iterOther == aOther.mStrips.end()) {
          break;
        }
        continue;
      }

      // Our current strip is completely contained by the other strip.
      mStrips.RemoveElementAt(idx);
    }
  }

  int32_t top;
  int32_t bottom;
  StripArray mStrips;
};
}  // namespace regiondetails

class nsRegion {
 public:
  using Band = regiondetails::Band;
  using Strip = regiondetails::Strip;
#ifndef DEBUG
  using BandArray = regiondetails::UncheckedArray<nsTArray<Band>, Band>;
  using StripArray = regiondetails::UncheckedArray<AutoTArray<Strip, 2>, Strip>;
#else
  using BandArray = nsTArray<Band>;
  using StripArray = AutoTArray<Strip, 2>;
#endif

  typedef nsRect RectType;
  typedef nsPoint PointType;
  typedef nsMargin MarginType;

  nsRegion() = default;
  MOZ_IMPLICIT nsRegion(const nsRect& aRect) {
    mBounds = nsRectAbsolute::FromRect(aRect);
  }
  MOZ_IMPLICIT nsRegion(const nsRectAbsolute& aRect) { mBounds = aRect; }
  template <typename RectType>
  explicit nsRegion(mozilla::gfx::ArrayView<RectType> aRects) {
    for (uint32_t i = 0; i < aRects.Length(); i++) {
      const RectType& r = aRects[i];
      AddRect(nsRectAbsolute(r.X(), r.Y(), r.XMost(), r.YMost()));
    }
  }

  nsRegion(const nsRegion& aRegion) { Copy(aRegion); }
  nsRegion(nsRegion&& aRegion)
      : mBands(std::move(aRegion.mBands)), mBounds(aRegion.mBounds) {
    aRegion.SetEmpty();
  }
  nsRegion& operator=(nsRegion&& aRegion) {
    mBands = std::move(aRegion.mBands);
    mBounds = aRegion.mBounds;
    aRegion.SetEmpty();
    return *this;
  }
  nsRegion& operator=(const nsRect& aRect) {
    Copy(aRect);
    return *this;
  }
  nsRegion& operator=(const nsRegion& aRegion) {
    Copy(aRegion);
    return *this;
  }
  bool operator==(const nsRegion& aRgn) const { return IsEqual(aRgn); }
  bool operator!=(const nsRegion& aRgn) const { return !(*this == aRgn); }

  friend std::ostream& operator<<(std::ostream& stream, const nsRegion& m);
  void OutputToStream(std::string aObjName, std::ostream& stream) const;

 private:
#ifdef DEBUG_REGIONS
  class OperationStringGenerator {
   public:
    virtual ~OperationStringGenerator() = default;

    virtual void OutputOp() = 0;
  };
#endif
 public:
  void AssertStateInternal() const;
  void AssertState() const {
#ifdef DEBUG_REGIONS
    AssertStateInternal();
#endif
  }

 private:
  void And(BandArray& aOut, const BandArray& aIn1, const BandArray& aIn2) {
    size_t idx = 0;
    size_t idxOther = 0;

    // This algorithm essentially forms a new list of bands, by iterating over
    // both regions' lists of band simultaneously, and building a new band
    // wherever the two regions intersect.
    while (true) {
      while (true) {
        while (idx != aIn1.Length() && aIn1[idx].bottom <= aIn2[idxOther].top) {
          // Increment our current band until it ends beyond aOther's current
          // band.
          idx++;
        }

        if (idx == aIn1.Length()) {
          // This region is out of bands, the other region's future bands are
          // ignored.
          break;
        }

        while (idxOther != aIn2.Length() &&
               aIn2[idxOther].bottom <= aIn1[idx].top) {
          // Increment aOther's current band until it ends beyond our current
          // band.
          idxOther++;
        }

        if (idxOther == aIn2.Length()) {
          // The other region's bands are all processed, all our future bands
          // are ignored.
          break;
        }

        if (aIn2[idxOther].top < aIn1[idx].bottom) {
          // We know the other band's bottom lies beyond our band's top because
          // otherwise we would've incremented above. Intersecting bands found.
          break;
        }
      }

      if (idx == aIn1.Length() || idxOther == aIn2.Length()) {
        // The above loop executed a break because we're done.
        break;
      }

      Band newBand(aIn1[idx]);
      // The new band is the intersection of the two current bands from both
      // regions.
      newBand.top = std::max(aIn1[idx].top, aIn2[idxOther].top);
      newBand.bottom = std::min(aIn1[idx].bottom, aIn2[idxOther].bottom);
      newBand.IntersectStrips(aIn2[idxOther]);

      if (newBand.mStrips.Length()) {
        // The intersecting area of the bands had overlapping strips, if it is
        // identical to the band above it merge, otherwise append.
        if (aOut.Length() && aOut.LastElement().bottom == newBand.top &&
            aOut.LastElement().EqualStrips(newBand)) {
          aOut.LastElement().bottom = newBand.bottom;
        } else {
          aOut.AppendElement(std::move(newBand));
        }
      }

      if (aIn2[idxOther].bottom < aIn1[idx].bottom) {
        idxOther++;
        if (idxOther == aIn2.Length()) {
          // Since we will access idxOther the next iteration, check if we're
          // not done.
          break;
        }
      } else {
        // No need to check here since we do at the beginning of the next
        // iteration.
        idx++;
      }
    }
  }

 public:
  nsRegion& AndWith(const nsRegion& aRegion) {
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorAndWith : public OperationStringGenerator {
     public:
      OperationStringGeneratorAndWith(nsRegion& aRegion,
                                      const nsRegion& aOtherRegion)
          : mRegion(&aRegion),
            mRegionCopy(aRegion),
            mOtherRegion(aOtherRegion) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorAndWith() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r1", stream);
        mOtherRegion.OutputToStream("r2", stream);
        stream << "r1.AndWith(r2);\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsRegion mOtherRegion;
    };

    OperationStringGeneratorAndWith opGenerator(*this, aRegion);
#endif
    if (mBounds.IsEmpty()) {
      // Region is empty, stays empty.
      return *this;
    }

    if (aRegion.IsEmpty()) {
      SetEmpty();
      return *this;
    }

    if (aRegion.mBands.IsEmpty()) {
      // Other region is a rect.
      return AndWith(aRegion.mBounds);
    }

    if (mBands.IsEmpty()) {
      mBands.AppendElement(mBounds);
    }

    BandArray newBands;

    And(newBands, mBands, aRegion.mBands);

    mBands = std::move(newBands);
    if (!mBands.Length()) {
      mBounds = nsRectAbsolute();
    } else {
      mBounds = CalculateBounds();
    }

    EnsureSimplified();
    AssertState();
    return *this;
  }

  nsRegion& AndWith(const nsRectAbsolute& aRect) {
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorAndWith : public OperationStringGenerator {
     public:
      OperationStringGeneratorAndWith(nsRegion& aRegion,
                                      const nsRectAbsolute& aRect)
          : mRegion(&aRegion), mRegionCopy(aRegion), mRect(aRect) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorAndWith() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r", stream);
        stream << "r.AndWith(nsRect(" << mRect.X() << ", " << mRect.Y() << ", "
               << mRect.Width() << ", " << mRect.Height() << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsRectAbsolute mRect;
    };

    OperationStringGeneratorAndWith opGenerator(*this, aRect);
#endif
    if (aRect.IsEmpty()) {
      SetEmpty();
      return *this;
    }

    if (mBands.IsEmpty()) {
      mBounds = mBounds.Intersect(aRect);
      return *this;
    }

    size_t idx = 0;

    size_t removeStart = 0;

    // This removes all bands that do not intersect with aRect, and intersects
    // the remaining ones with aRect.

    // Start by figuring out how much to remove from the start.
    while (idx != mBands.Length() && mBands[idx].bottom <= aRect.Y()) {
      idx++;
    }

    // We'll remove these later to avoid needless copying in the array.
    removeStart = idx;

    while (idx != mBands.Length()) {
      if (mBands[idx].top >= aRect.YMost()) {
        mBands.TruncateLength(idx);
        break;
      }

      mBands[idx].top = std::max(mBands[idx].top, aRect.Y());
      mBands[idx].bottom = std::min(mBands[idx].bottom, aRect.YMost());

      mBands[idx].IntersectStrip(Strip(aRect.X(), aRect.XMost()));

      if (!mBands[idx].mStrips.Length()) {
        mBands.RemoveElementAt(idx);
      } else {
        if (idx > removeStart) {
          CompressBefore(idx);
        }
        idx++;
      }
    }

    if (removeStart) {
      mBands.RemoveElementsAt(0, removeStart);
    }

    if (mBands.Length()) {
      mBounds = CalculateBounds();
    } else {
      mBounds.SetEmpty();
    }
    EnsureSimplified();
    AssertState();
    return *this;
  }
  nsRegion& AndWith(const nsRect& aRect) {
    return AndWith(nsRectAbsolute::FromRect(aRect));
  }
  nsRegion& And(const nsRegion& aRgn1, const nsRegion& aRgn2) {
    if (&aRgn1 == this) {
      return AndWith(aRgn2);
    }
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorAnd : public OperationStringGenerator {
     public:
      OperationStringGeneratorAnd(nsRegion& aRegion, const nsRegion& aRegion1,
                                  const nsRegion& aRegion2)
          : mRegion(&aRegion), mRegion1(aRegion1), mRegion2(aRegion2) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorAnd() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegion1.OutputToStream("r1", stream);
        mRegion2.OutputToStream("r2", stream);
        stream << "nsRegion r3;\nr3.And(r1, r2);\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegion1;
      nsRegion mRegion2;
    };

    OperationStringGeneratorAnd opGenerator(*this, aRgn1, aRgn2);
#endif
    mBands.Clear();

    if (aRgn1.IsEmpty() || aRgn2.IsEmpty()) {
      mBounds.SetEmpty();
      return *this;
    }

    if (aRgn1.mBands.IsEmpty() && aRgn2.mBands.IsEmpty()) {
      mBounds = aRgn1.mBounds.Intersect(aRgn2.mBounds);
      return *this;
    } else if (aRgn1.mBands.IsEmpty()) {
      return And(aRgn2, aRgn1.mBounds);
    } else if (aRgn2.mBands.IsEmpty()) {
      return And(aRgn1, aRgn2.mBounds);
    }

    And(mBands, aRgn1.mBands, aRgn2.mBands);

    if (!mBands.Length()) {
      mBounds = nsRectAbsolute();
    } else {
      mBounds = CalculateBounds();
    }

    EnsureSimplified();
    AssertState();
    return *this;
  }
  nsRegion& And(const nsRect& aRect, const nsRegion& aRegion) {
    return And(aRegion, aRect);
  }
  nsRegion& And(const nsRegion& aRegion, const nsRectAbsolute& aRect) {
    if (&aRegion == this) {
      return AndWith(aRect);
    }
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorAnd : public OperationStringGenerator {
     public:
      OperationStringGeneratorAnd(nsRegion& aThisRegion,
                                  const nsRegion& aRegion,
                                  const nsRectAbsolute& aRect)
          : mThisRegion(&aThisRegion), mRegion(aRegion), mRect(aRect) {
        aThisRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorAnd() {
        mThisRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegion.OutputToStream("r", stream);
        stream << "nsRegion r2;\nr.And(r2, nsRect(" << mRect.X() << ", "
               << mRect.Y() << ", " << mRect.Width() << ", " << mRect.Height()
               << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mThisRegion;
      nsRegion mRegion;
      nsRectAbsolute mRect;
    };

    OperationStringGeneratorAnd opGenerator(*this, aRegion, aRect);
#endif
    mBands.Clear();

    if (aRect.IsEmpty()) {
      mBounds.SetEmpty();
      return *this;
    }

    if (aRegion.mBands.IsEmpty()) {
      mBounds = aRegion.mBounds.Intersect(aRect);
      return *this;
    }

    size_t idx = 0;
    const BandArray& bands = aRegion.mBands;

    mBands.SetCapacity(bands.Length() + 3);
    while (idx != bands.Length()) {
      // Ignore anything before.
      if (bands[idx].bottom <= aRect.Y()) {
        idx++;
        continue;
      }
      // We're done once we've reached the bottom.
      if (bands[idx].top >= aRect.YMost()) {
        break;
      }

      // Now deal with bands actually intersecting the rectangle.
      Band newBand(bands[idx]);
      newBand.top = std::max(bands[idx].top, aRect.Y());
      newBand.bottom = std::min(bands[idx].bottom, aRect.YMost());

      newBand.IntersectStrip(Strip(aRect.X(), aRect.XMost()));

      if (newBand.mStrips.Length()) {
        if (!mBands.IsEmpty() && newBand.top == mBands.LastElement().bottom &&
            newBand.EqualStrips(mBands.LastElement())) {
          mBands.LastElement().bottom = newBand.bottom;
        } else {
          mBands.AppendElement(std::move(newBand));
        }
      }
      idx++;
    }

    if (mBands.Length()) {
      mBounds = CalculateBounds();
    } else {
      mBounds.SetEmpty();
    }

    EnsureSimplified();
    AssertState();
    return *this;
  }
  nsRegion& And(const nsRegion& aRegion, const nsRect& aRect) {
    return And(aRegion, nsRectAbsolute::FromRect(aRect));
  }
  nsRegion& And(const nsRect& aRect1, const nsRect& aRect2) {
    nsRect tmpRect;

    tmpRect.IntersectRect(aRect1, aRect2);
    return Copy(tmpRect);
  }

  nsRegion& OrWith(const nsRegion& aOther) {
    for (RectIterator idx(aOther); !idx.Done(); idx.Next()) {
      AddRect(idx.GetAbsolute());
    }
    return *this;
  }
  nsRegion& OrWith(const nsRect& aOther) {
    AddRect(nsRectAbsolute::FromRect(aOther));
    return *this;
  }
  nsRegion& Or(const nsRegion& aRgn1, const nsRegion& aRgn2) {
    if (&aRgn1 != this) {
      *this = aRgn1;
    }
    for (RectIterator idx(aRgn2); !idx.Done(); idx.Next()) {
      AddRect(idx.GetAbsolute());
    }
    return *this;
  }
  nsRegion& Or(const nsRegion& aRegion, const nsRect& aRect) {
    if (&aRegion != this) {
      *this = aRegion;
    }
    AddRect(nsRectAbsolute::FromRect(aRect));
    return *this;
  }
  nsRegion& Or(const nsRect& aRect, const nsRegion& aRegion) {
    return Or(aRegion, aRect);
  }
  nsRegion& Or(const nsRect& aRect1, const nsRect& aRect2) {
    Copy(aRect1);
    return Or(*this, aRect2);
  }

  nsRegion& XorWith(const nsRegion& aOther) { return Xor(*this, aOther); }
  nsRegion& XorWith(const nsRect& aOther) { return Xor(*this, aOther); }
  nsRegion& Xor(const nsRegion& aRgn1, const nsRegion& aRgn2) {
    // this could be implemented better if pixman had direct
    // support for xoring regions.
    nsRegion p;
    p.Sub(aRgn1, aRgn2);
    nsRegion q;
    q.Sub(aRgn2, aRgn1);
    return Or(p, q);
  }
  nsRegion& Xor(const nsRegion& aRegion, const nsRect& aRect) {
    return Xor(aRegion, nsRegion(aRect));
  }
  nsRegion& Xor(const nsRect& aRect, const nsRegion& aRegion) {
    return Xor(nsRegion(aRect), aRegion);
  }
  nsRegion& Xor(const nsRect& aRect1, const nsRect& aRect2) {
    return Xor(nsRegion(aRect1), nsRegion(aRect2));
  }

  nsRegion ToAppUnits(nscoord aAppUnitsPerPixel) const;

  nsRegion& SubWith(const nsRegion& aOther) {
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorSubWith : public OperationStringGenerator {
     public:
      OperationStringGeneratorSubWith(nsRegion& aRegion,
                                      const nsRegion& aOtherRegion)
          : mRegion(&aRegion),
            mRegionCopy(aRegion),
            mOtherRegion(aOtherRegion) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorSubWith() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r1", stream);
        mOtherRegion.OutputToStream("r2", stream);
        stream << "r1.SubWith(r2);\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsRegion mOtherRegion;
    };

    OperationStringGeneratorSubWith opGenerator(*this, aOther);
#endif

    if (mBounds.IsEmpty()) {
      return *this;
    }

    if (aOther.mBands.IsEmpty()) {
      return SubWith(aOther.mBounds);
    }

    if (mBands.IsEmpty()) {
      mBands.AppendElement(Band(mBounds));
    }

    size_t idx = 0;
    size_t idxOther = 0;
    while (idx < mBands.Length()) {
      while (true) {
        while (idx != mBands.Length() &&
               mBands[idx].bottom <= aOther.mBands[idxOther].top) {
          // Increment our current band until it ends beyond aOther's current
          // band.
          idx++;
        }

        if (idx == mBands.Length()) {
          // This region is out of bands, the other region's future bands are
          // ignored.
          break;
        }

        while (idxOther != aOther.mBands.Length() &&
               aOther.mBands[idxOther].bottom <= mBands[idx].top) {
          // Increment aOther's current band until it ends beyond our current
          // band.
          idxOther++;
        }

        if (idxOther == aOther.mBands.Length()) {
          // The other region's bands are all processed, all our future bands
          // are ignored.
          break;
        }

        if (aOther.mBands[idxOther].top < mBands[idx].bottom) {
          // We know the other band's bottom lies beyond our band's top because
          // otherwise we would've incremented above. Intersecting bands found.
          break;
        }
      }

      if (idx == mBands.Length() || idxOther == aOther.mBands.Length()) {
        // The above loop executed a break because we're done.
        break;
      }

      const Band& bandOther = aOther.mBands[idxOther];

      if (!mBands[idx].Intersects(bandOther)) {
        if (mBands[idx].bottom < bandOther.bottom) {
          idx++;
        } else {
          idxOther++;
          if (idxOther == aOther.mBands.Length()) {
            break;
          }
        }
        continue;
      }

      // These bands actually intersect.
      if (mBands[idx].top < bandOther.top) {
        mBands.InsertElementAt(idx + 1, Band(mBands[idx]));
        mBands[idx].bottom = bandOther.top;
        idx++;
        mBands[idx].top = bandOther.top;
      }

      // mBands[idx].top >= bandOther.top;
      if (mBands[idx].bottom <= bandOther.bottom) {
        mBands[idx].SubStrips(bandOther);
        if (mBands[idx].mStrips.IsEmpty()) {
          mBands.RemoveElementAt(idx);
        } else {
          CompressBefore(idx);
          idx++;
          // The band before us just changed, it may be identical now.
          CompressBefore(idx);
        }
        continue;
      }

      // mBands[idx].bottom > bandOther.bottom
      Band newBand = mBands[idx];
      newBand.SubStrips(bandOther);

      if (!newBand.mStrips.IsEmpty()) {
        mBands.InsertElementAt(idx, newBand);
        mBands[idx].bottom = bandOther.bottom;
        CompressBefore(idx);
        idx++;
      }

      mBands[idx].top = bandOther.bottom;

      idxOther++;
      if (idxOther == aOther.mBands.Length()) {
        break;
      }
    }

    if (mBands.IsEmpty()) {
      mBounds.SetEmpty();
    } else {
      mBounds = CalculateBounds();
    }

    AssertState();
    EnsureSimplified();
    return *this;
  }
  nsRegion& SubOut(const nsRegion& aOther) { return SubWith(aOther); }
  nsRegion& SubOut(const nsRect& aOther) { return SubWith(aOther); }

 private:
  void AppendOrExtend(const Band& aNewBand) {
    if (aNewBand.mStrips.IsEmpty()) {
      return;
    }
    if (mBands.IsEmpty()) {
      mBands.AppendElement(aNewBand);
      return;
    }

    if (mBands.LastElement().bottom == aNewBand.top &&
        mBands.LastElement().EqualStrips(aNewBand)) {
      mBands.LastElement().bottom = aNewBand.bottom;
    } else {
      mBands.AppendElement(aNewBand);
    }
  }
  void AppendOrExtend(const Band&& aNewBand) {
    if (aNewBand.mStrips.IsEmpty()) {
      return;
    }
    if (mBands.IsEmpty()) {
      mBands.AppendElement(std::move(aNewBand));
      return;
    }

    if (mBands.LastElement().bottom == aNewBand.top &&
        mBands.LastElement().EqualStrips(aNewBand)) {
      mBands.LastElement().bottom = aNewBand.bottom;
    } else {
      mBands.AppendElement(std::move(aNewBand));
    }
  }

 public:
  nsRegion& Sub(const nsRegion& aRgn1, const nsRegion& aRgn2) {
    if (&aRgn1 == this) {
      return SubWith(aRgn2);
    }
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorSub : public OperationStringGenerator {
     public:
      OperationStringGeneratorSub(nsRegion& aRegion, const nsRegion& aRgn1,
                                  const nsRegion& aRgn2)
          : mRegion(&aRegion), mRegion1(aRgn1), mRegion2(aRgn2) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorSub() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegion1.OutputToStream("r1", stream);
        mRegion2.OutputToStream("r2", stream);
        stream << "nsRegion r3;\nr3.Sub(r1, r2);\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegion1;
      nsRegion mRegion2;
    };

    OperationStringGeneratorSub opGenerator(*this, aRgn1, aRgn2);
#endif

    mBands.Clear();

    if (aRgn1.mBounds.IsEmpty()) {
      mBounds.SetEmpty();
      return *this;
    }

    if (aRgn2.mBounds.IsEmpty()) {
      Copy(aRgn1);
      return *this;
    }

    if (aRgn1.mBands.IsEmpty() && aRgn2.mBands.IsEmpty()) {
      return Sub(aRgn1.mBounds, aRgn2.mBounds);
    } else if (aRgn1.mBands.IsEmpty()) {
      return Sub(aRgn1.mBounds, aRgn2);
    } else if (aRgn2.mBands.IsEmpty()) {
      return Sub(aRgn1, aRgn2.mBounds);
    }

    const BandArray& bands1 = aRgn1.mBands;
    const BandArray& bands2 = aRgn2.mBands;

    size_t idx = 0;
    size_t idxOther = 0;

    // We iterate the source region's bands, subtracting the other regions bands
    // from them as we move them into ours.
    while (idx < bands1.Length()) {
      while (idxOther < bands2.Length() &&
             bands2[idxOther].bottom <= bands1[idx].top) {
        // These other bands are irrelevant as they don't intersect with the
        // band we're currently processing.
        idxOther++;
      }
      if (idxOther == bands2.Length()) {
        break;
      }

      const Band& other = bands2[idxOther];

      // bands2[idxOther].bottom >= bands1[idx].top
      Band origBand(bands1[idx]);
      if (other.top >= origBand.bottom) {
        // No intersecting bands, append and continue.
        AppendOrExtend(origBand);
        idx++;
        continue;
      }

      // Push a band for an uncovered region above our band.
      if (origBand.top < other.top) {
        Band newBand(origBand);
        newBand.bottom = other.top;
        AppendOrExtend(std::move(newBand));
      }

      int32_t lastBottom = std::max(other.top, origBand.top);
      while (idxOther < bands2.Length() &&
             bands2[idxOther].top < origBand.bottom) {
        const Band& other = bands2[idxOther];
        Band newBand(origBand);
        newBand.top = std::max(origBand.top, other.top);
        newBand.bottom = std::min(origBand.bottom, other.bottom);

        // If there was a gap, we need to add the original band there.
        if (newBand.top > lastBottom) {
          Band betweenBand(newBand);
          betweenBand.top = lastBottom;
          betweenBand.bottom = newBand.top;
          AppendOrExtend(std::move(betweenBand));
        }

        lastBottom = newBand.bottom;
        newBand.SubStrips(other);
        AppendOrExtend(std::move(newBand));
        idxOther++;
      }
      // Decrement other here so we are back at the last band in region 2
      // that intersected.
      idxOther--;

      if (bands2[idxOther].bottom < origBand.bottom) {
        // The last band in region2 that intersected ended before this one,
        // we can copy the rest.
        Band newBand(origBand);
        newBand.top = bands2[idxOther].bottom;
        AppendOrExtend(std::move(newBand));
        idxOther++;
      }
      idx++;
    }

    // Copy any remaining bands, the first one may have to be extended to fit
    // the last one added before. The rest can be unconditionally appended.
    if (idx < bands1.Length()) {
      AppendOrExtend(bands1[idx]);
      idx++;
    }

    while (idx < bands1.Length()) {
      mBands.AppendElement(bands1[idx]);
      idx++;
    }

    if (mBands.IsEmpty()) {
      mBounds.SetEmpty();
    } else {
      mBounds = CalculateBounds();
    }

    AssertState();
    EnsureSimplified();
    return *this;
  }

 private:
  // Internal helper for executing subtraction.
  void RunSubtraction(const nsRectAbsolute& aRect) {
    Strip rectStrip(aRect.X(), aRect.XMost());

    size_t idx = 0;

    while (idx < mBands.Length()) {
      if (mBands[idx].top >= aRect.YMost()) {
        return;
      }

      if (mBands[idx].bottom <= aRect.Y()) {
        // This band is entirely before aRect, move on.
        idx++;
        continue;
      }

      if (!mBands[idx].Intersects(Strip(aRect.X(), aRect.XMost()))) {
        // This band does not intersect aRect horizontally. Move on.
        idx++;
        continue;
      }

      // This band intersects with aRect.

      if (mBands[idx].top < aRect.Y()) {
        // This band starts above the start of aRect, split the band into two
        // along the intersection, and continue to the next iteration to process
        // the one that now intersects exactly.
        auto above = mBands.InsertElementAt(idx, Band(mBands[idx]));
        above->bottom = aRect.Y();
        idx++;
        mBands[idx].top = aRect.Y();
        // Continue to run the loop for the next band.
        continue;
      }

      if (mBands[idx].bottom <= aRect.YMost()) {
        // This band ends before the end of aRect.
        mBands[idx].SubStrip(rectStrip);
        if (mBands[idx].mStrips.Length()) {
          CompressAdjacentBands(idx);
        } else {
          mBands.RemoveElementAt(idx);
        }
        continue;
      }

      // This band extends beyond aRect.
      Band newBand = mBands[idx];
      newBand.SubStrip(rectStrip);
      newBand.bottom = aRect.YMost();
      mBands[idx].top = aRect.YMost();

      if (newBand.mStrips.Length()) {
        if (idx && mBands[idx - 1].bottom == newBand.top &&
            newBand.EqualStrips(mBands[idx - 1])) {
          mBands[idx - 1].bottom = aRect.YMost();
        } else {
          mBands.InsertElementAt(idx, std::move(newBand));
        }
      }

      return;
    }
  }

 public:
  nsRegion& SubWith(const nsRectAbsolute& aRect) {
    if (!mBounds.Intersects(aRect)) {
      return *this;
    }

    if (aRect.Contains(mBounds)) {
      SetEmpty();
      return *this;
    }

#ifdef DEBUG_REGIONS
    class OperationStringGeneratorSubWith : public OperationStringGenerator {
     public:
      OperationStringGeneratorSubWith(nsRegion& aRegion,
                                      const nsRectAbsolute& aRect)
          : mRegion(&aRegion), mRegionCopy(aRegion), mRect(aRect) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorSubWith() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r", stream);
        stream << "r.SubWith(nsRect(" << mRect.X() << ", " << mRect.Y() << ", "
               << mRect.Width() << ", " << mRect.Height() << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsRectAbsolute mRect;
    };

    OperationStringGeneratorSubWith opGenerator(*this, aRect);
#endif

    if (mBands.IsEmpty()) {
      mBands.AppendElement(Band(mBounds));
    }

    RunSubtraction(aRect);

    if (aRect.X() <= mBounds.X() || aRect.Y() <= mBounds.Y() ||
        aRect.XMost() >= mBounds.XMost() || aRect.YMost() >= mBounds.YMost()) {
      mBounds = CalculateBounds();
    }
    EnsureSimplified();
    AssertState();
    return *this;
  }
  nsRegion& Sub(const nsRegion& aRegion, const nsRectAbsolute& aRect) {
    if (aRect.Contains(aRegion.mBounds)) {
      SetEmpty();
      return *this;
    }
    if (&aRegion == this) {
      return SubWith(aRect);
    }
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorSub : public OperationStringGenerator {
     public:
      OperationStringGeneratorSub(nsRegion& aRegion,
                                  const nsRegion& aRegionOther,
                                  const nsRectAbsolute& aRect)
          : mRegion(&aRegion), mRegionOther(aRegionOther), mRect(aRect) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorSub() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionOther.OutputToStream("r1", stream);
        stream << "nsRegion r2;\nr2.Sub(r1, nsRect(" << mRect.X() << ", "
               << mRect.Y() << ", " << mRect.Width() << ", " << mRect.Height()
               << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionOther;
      nsRectAbsolute mRect;
    };

    OperationStringGeneratorSub opGenerator(*this, aRegion, aRect);
#endif

    mBands.Clear();

    if (aRegion.mBounds.IsEmpty()) {
      mBounds.SetEmpty();
      return *this;
    }

    if (aRect.IsEmpty()) {
      Copy(aRegion);
      return *this;
    }

    if (aRegion.mBands.IsEmpty()) {
      Copy(aRegion.mBounds);
      return SubWith(aRect);
    }

    const BandArray& bands = aRegion.mBands;

    size_t idx = 0;

    Strip strip(aRect.X(), aRect.XMost());

    mBands.SetCapacity(bands.Length() + 3);

    // Process all bands that lie before aRect.
    while (idx < bands.Length() && bands[idx].bottom <= aRect.Y()) {
      mBands.AppendElement(bands[idx]);
      idx++;
    }

    // This band's bottom lies beyond aRect.
    if (idx < bands.Length() && bands[idx].top < aRect.Y()) {
      Band newBand(bands[idx]);
      if (bands[idx].Intersects(strip)) {
        newBand.bottom = aRect.Y();
      } else {
        idx++;
      }
      mBands.AppendElement(std::move(newBand));
    }

    // This tracks whether the band when we -exit- the next loop intersected the
    // rectangle.
    bool didIntersect = false;

    while (idx < bands.Length() && bands[idx].top < aRect.YMost()) {
      // Process all bands intersecting with aRect.
      if (!bands[idx].Intersects(strip)) {
        AppendOrExtend(bands[idx]);
        idx++;
        didIntersect = false;
        continue;
      }

      didIntersect = true;
      Band newBand(bands[idx]);
      newBand.top = std::max(newBand.top, aRect.Y());
      newBand.bottom = std::min(newBand.bottom, aRect.YMost());
      newBand.SubStrip(strip);
      AppendOrExtend(std::move(newBand));
      idx++;
    }

    if (didIntersect) {
      if (aRect.YMost() < bands[idx - 1].bottom) {
        // If this band does not intersect the loop above has already added the
        // whole unmodified band.
        Band newBand(bands[idx - 1]);
        newBand.top = aRect.YMost();
        AppendOrExtend(std::move(newBand));
      }
    }

    // Now process all bands beyond aRect.
    if (idx < bands.Length()) {
      AppendOrExtend(bands[idx]);
      idx++;
    }

    mBands.AppendElements(bands.Elements() + idx, bands.Length() - idx);

    if (mBands.IsEmpty()) {
      mBounds.SetEmpty();
    } else {
      mBounds = CalculateBounds();
    }

    AssertState();
    EnsureSimplified();
    return *this;
  }
  nsRegion& SubWith(const nsRect& aRect) {
    return SubWith(nsRectAbsolute::FromRect(aRect));
  }
  nsRegion& Sub(const nsRect& aRect, const nsRegion& aRegion) {
    Copy(aRect);
    return SubWith(aRegion);
  }
  nsRegion& Sub(const nsRectAbsolute& aRect, const nsRegion& aRegion) {
    Copy(aRect);
    return SubWith(aRegion);
  }
  nsRegion& Sub(const nsRect& aRect1, const nsRect& aRect2) {
    Copy(aRect1);
    return SubWith(aRect2);
  }
  nsRegion& Sub(const nsRegion& aRegion, const nsRect& aRect) {
    return Sub(aRegion, nsRectAbsolute::FromRect(aRect));
  }
  nsRegion& Sub(const nsRectAbsolute& aRect1, const nsRectAbsolute& aRect2) {
    Copy(aRect1);
    return SubWith(aRect2);
  }

  /**
   * Returns true if the given point is inside the region. A region
   * created from a rect (x=0, y=0, w=100, h=100) will NOT contain
   * the point x=100, y=100.
   */
  bool Contains(int aX, int aY) const {
    if (mBands.IsEmpty()) {
      return mBounds.Contains(aX, aY);
    }

    auto iter = mBands.begin();

    while (iter != mBands.end()) {
      if (iter->bottom <= aY) {
        iter++;
        continue;
      }

      if (iter->top > aY) {
        return false;
      }

      if (iter->ContainsStrip(Strip(aX, aX + 1))) {
        return true;
      }
      return false;
    }
    return false;
  }

  bool Contains(const nsPoint& aPoint) const {
    return Contains(aPoint.x, aPoint.y);
  }

  bool Contains(const nsRectAbsolute& aRect) const {
    if (aRect.IsEmpty()) {
      return false;
    }

    if (mBands.IsEmpty()) {
      return mBounds.Contains(aRect);
    }

    auto iter = mBands.begin();

    while (iter != mBands.end()) {
      if (iter->bottom <= aRect.Y()) {
        iter++;
        continue;
      }

      if (iter->top > aRect.Y()) {
        return false;
      }

      // Now inside the rectangle.
      if (!iter->ContainsStrip(Strip(aRect.X(), aRect.XMost()))) {
        return false;
      }

      if (iter->bottom >= aRect.YMost()) {
        return true;
      }

      int32_t lastY = iter->bottom;
      iter++;
      while (iter != mBands.end()) {
        // Bands do not connect.
        if (iter->top != lastY) {
          return false;
        }

        if (!iter->ContainsStrip(Strip(aRect.X(), aRect.XMost()))) {
          return false;
        }

        if (iter->bottom >= aRect.YMost()) {
          return true;
        }

        lastY = iter->bottom;
        iter++;
      }
    }
    return false;
  }
  bool Contains(const nsRect& aRect) const {
    return Contains(nsRectAbsolute::FromRect(aRect));
  }

  bool Contains(const nsRegion& aRgn) const;
  bool Intersects(const nsRectAbsolute& aRect) const;
  bool Intersects(const nsRect& aRect) const {
    return Intersects(nsRectAbsolute::FromRect(aRect));
  }

  void MoveBy(int32_t aXOffset, int32_t aYOffset) {
    MoveBy(nsPoint(aXOffset, aYOffset));
  }
  void MoveBy(nsPoint aPt) {
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorMoveBy : public OperationStringGenerator {
     public:
      OperationStringGeneratorMoveBy(nsRegion& aRegion, const nsPoint& aPoint)
          : mRegion(&aRegion), mRegionCopy(aRegion), mPoint(aPoint) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorMoveBy() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r", stream);
        stream << "r.MoveBy(nsPoint(" << mPoint.x << ", " << mPoint.y
               << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsPoint mPoint;
    };

    OperationStringGeneratorMoveBy opGenerator(*this, aPt);
#endif

    mBounds.MoveBy(aPt);
    for (Band& band : mBands) {
      band.top += aPt.Y();
      band.bottom += aPt.Y();
      for (Strip& strip : band.mStrips) {
        strip.left += aPt.X();
        strip.right += aPt.X();
      }
    }
    AssertState();
  }
  void SetEmpty() {
    mBands.Clear();
    mBounds.SetEmpty();
  }

  nsRegion MovedBy(int32_t aXOffset, int32_t aYOffset) const {
    return MovedBy(nsPoint(aXOffset, aYOffset));
  }
  nsRegion MovedBy(const nsPoint& aPt) const {
    nsRegion copy(*this);
    copy.MoveBy(aPt);
    return copy;
  }

  nsRegion Intersect(const nsRegion& aOther) const {
    nsRegion intersection;
    intersection.And(*this, aOther);
    return intersection;
  }

  void Inflate(const nsMargin& aMargin);

  nsRegion Inflated(const nsMargin& aMargin) const {
    nsRegion copy(*this);
    copy.Inflate(aMargin);
    return copy;
  }

  bool IsEmpty() const { return mBounds.IsEmpty(); }
  bool IsComplex() const { return GetNumRects() > 1; }
  bool IsEqual(const nsRegion& aRegion) const {
    if (!mBounds.IsEqualInterior(aRegion.mBounds)) {
      return false;
    }

    if (mBands.Length() != aRegion.mBands.Length()) {
      return false;
    }

    for (auto iter1 = mBands.begin(), iter2 = aRegion.mBands.begin();
         iter1 != mBands.end(); iter1++, iter2++) {
      if (iter1->top != iter2->top || iter1->bottom != iter2->bottom ||
          !iter1->EqualStrips(*iter2)) {
        return false;
      }
    }

    return true;
  }

  uint32_t GetNumRects() const {
    if (mBands.IsEmpty()) {
      return mBounds.IsEmpty() ? 0 : 1;
    }

    uint32_t rects = 0;

    for (const Band& band : mBands) {
      rects += band.mStrips.Length();
    }

    return rects;
  }
  const nsRect GetBounds() const { return mBounds.ToNSRect(); }
  const nsRectAbsolute GetAbsoluteBounds() const { return mBounds; }
  uint64_t Area() const;

  /**
   * Return this region scaled to a different appunits per pixel (APP) ratio.
   * This applies nsRect::ScaleToOtherAppUnitsRoundOut/In to each rect of the
   * region.
   * @param aFromAPP the APP to scale from
   * @param aToAPP the APP to scale to
   * @note this can turn an empty region into a non-empty region
   */
  [[nodiscard]] nsRegion ScaleToOtherAppUnitsRoundOut(int32_t aFromAPP,
                                                      int32_t aToAPP) const;
  [[nodiscard]] nsRegion ScaleToOtherAppUnitsRoundIn(int32_t aFromAPP,
                                                     int32_t aToAPP) const;
  nsRegion& ScaleRoundOut(float aXScale, float aYScale);
  nsRegion& ScaleInverseRoundOut(float aXScale, float aYScale);
  nsRegion& Transform(const mozilla::gfx::Matrix4x4& aTransform);
  nsIntRegion ScaleToOutsidePixels(float aXScale, float aYScale,
                                   nscoord aAppUnitsPerPixel) const;
  nsIntRegion ScaleToInsidePixels(float aXScale, float aYScale,
                                  nscoord aAppUnitsPerPixel) const;
  nsIntRegion ScaleToNearestPixels(float aXScale, float aYScale,
                                   nscoord aAppUnitsPerPixel) const;
  nsIntRegion ToOutsidePixels(nscoord aAppUnitsPerPixel) const;
  nsIntRegion ToNearestPixels(nscoord aAppUnitsPerPixel) const;

  /**
   * Gets the largest rectangle contained in the region.
   * @param aContainingRect if non-empty, we choose a rectangle that
   * maximizes the area intersecting with aContainingRect (and break ties by
   * then choosing the largest rectangle overall)
   */
  nsRect GetLargestRectangle(const nsRect& aContainingRect = nsRect()) const;

  /**
   * Make sure the region has at most aMaxRects by adding area to it
   * if necessary. The simplified region will be a superset of the
   * original region. The simplified region's bounding box will be
   * the same as for the current region.
   */
  void SimplifyOutward(uint32_t aMaxRects);
  /**
   * Simplify the region by adding at most aThreshold area between spans of
   * rects.  The simplified region will be a superset of the original region.
   * The simplified region's bounding box will be the same as for the current
   * region.
   */
  void SimplifyOutwardByArea(uint32_t aThreshold);
  /**
   * Make sure the region has at most aMaxRects by removing area from
   * it if necessary. The simplified region will be a subset of the
   * original region.
   */
  void SimplifyInward(uint32_t aMaxRects);

  /**
   * VisitEdges is a weird kind of function that we use for padding
   * out surfaces to prevent texture filtering artifacts.
   * It calls the visitFn callback for each of the exterior edges of
   * the regions. The top and bottom edges will be expanded 1 pixel
   * to the left and right if there's an outside corner. The order
   * the edges are visited is not guaranteed.
   *
   * visitFn has a side parameter that can be TOP,BOTTOM,LEFT,RIGHT
   * and specifies which kind of edge is being visited. x1, y1, x2, y2
   * are the coordinates of the line. (x1 == x2) || (y1 == y2)
   */
  typedef void (*visitFn)(void* closure, VisitSide side, int x1, int y1, int x2,
                          int y2);
  void VisitEdges(visitFn, void* closure) const;

  nsCString ToString() const;

 private:
  nsIntRegion ToPixels(nscoord aAppUnitsPerPixel, bool aOutsidePixels) const;

  nsRegion& Copy(const nsRegion& aRegion) {
    mBounds = aRegion.mBounds;
    mBands = aRegion.mBands.Clone();
    return *this;
  }

  nsRegion& Copy(const nsRect& aRect) {
    mBands.Clear();
    mBounds = nsRectAbsolute::FromRect(aRect);
    return *this;
  }

  nsRegion& Copy(const nsRectAbsolute& aRect) {
    mBands.Clear();
    mBounds = aRect;
    return *this;
  }

  void EnsureSimplified() {
    if (mBands.Length() == 1 && mBands.begin()->mStrips.Length() == 1) {
      mBands.Clear();
    }
  }

  void AddRect(const nsRectAbsolute& aRect) {
#ifdef DEBUG_REGIONS
    class OperationStringGeneratorAddRect : public OperationStringGenerator {
     public:
      OperationStringGeneratorAddRect(nsRegion& aRegion,
                                      const nsRectAbsolute& aRect)
          : mRegion(&aRegion), mRegionCopy(aRegion), mRect(aRect) {
        aRegion.mCurrentOpGenerator = this;
      }
      virtual ~OperationStringGeneratorAddRect() {
        mRegion->mCurrentOpGenerator = nullptr;
      }

      virtual void OutputOp() override {
        std::stringstream stream;
        mRegionCopy.OutputToStream("r", stream);
        stream << "r.OrWith(nsRect(" << mRect.X() << ", " << mRect.Y() << ", "
               << mRect.Width() << ", " << mRect.Height() << "));\n";
        gfxCriticalError() << stream.str();
      }

     private:
      nsRegion* mRegion;
      nsRegion mRegionCopy;
      nsRectAbsolute mRect;
    };

    OperationStringGeneratorAddRect opGenerator(*this, aRect);
#endif
    if (aRect.IsEmpty()) {
      return;
    }

    if (mBands.IsEmpty()) {
      if (mBounds.IsEmpty()) {
        mBounds = aRect;
        return;
      } else if (mBounds.Contains(aRect)) {
        return;
      }

      mBands.AppendElement(Band(mBounds));
    }

    mBounds = aRect.UnsafeUnion(mBounds);

    size_t idx = 0;

    Strip strip(aRect.X(), aRect.XMost());
    Band remaining(aRect);

    while (idx != mBands.Length()) {
      if (mBands[idx].bottom <= remaining.top) {
        // This band lies wholly above aRect.
        idx++;
        continue;
      }

      if (remaining.top >= remaining.bottom) {
        AssertState();
        EnsureSimplified();
        return;
      }

      if (mBands[idx].top >= remaining.bottom) {
        // This band lies wholly below aRect.
        break;
      }

      if (mBands[idx].EqualStrips(remaining)) {
        mBands[idx].top = std::min(mBands[idx].top, remaining.top);
        // Nothing to do for this band. Just expand.
        remaining.top = mBands[idx].bottom;
        CompressBefore(idx);
        idx++;
        continue;
      }

      if (mBands[idx].top > remaining.top) {
        auto before = mBands.InsertElementAt(idx, remaining);
        before->bottom = mBands[idx + 1].top;
        remaining.top = before->bottom;
        CompressBefore(idx);
        idx++;
        CompressBefore(idx);
        continue;
      }

      if (mBands[idx].ContainsStrip(strip)) {
        remaining.top = mBands[idx].bottom;
        idx++;
        continue;
      }

      // mBands[idx].top <= remaining.top.

      if (mBands[idx].top < remaining.top) {
        auto before = mBands.InsertElementAt(idx, Band(mBands[idx]));
        before->bottom = remaining.top;
        idx++;
        mBands[idx].top = remaining.top;
        continue;
      }

      // mBands[idx].top == remaining.top
      if (mBands[idx].bottom > remaining.bottom) {
        auto below = mBands.InsertElementAt(idx + 1, Band(mBands[idx]));
        below->top = remaining.bottom;
        mBands[idx].bottom = remaining.bottom;
      }

      mBands[idx].InsertStrip(strip);
      CompressBefore(idx);
      remaining.top = mBands[idx].bottom;
      idx++;
      CompressBefore(idx);
    }

    if (remaining.top < remaining.bottom) {
      // We didn't find any bands that overlapped aRect.
      if (idx) {
        if (mBands[idx - 1].bottom == remaining.top &&
            mBands[idx - 1].EqualStrips(remaining)) {
          mBands[idx - 1].bottom = remaining.bottom;
          CompressBefore(idx);
          AssertState();
          EnsureSimplified();
          return;
        }
      }
      mBands.InsertElementAt(idx, remaining);
      idx++;
      CompressBefore(idx);
    } else {
      CompressBefore(idx);
    }

    AssertState();
    EnsureSimplified();
  }

  // Most callers could probably do this on the fly, if this ever shows up
  // in profiles we could optimize this.
  nsRectAbsolute CalculateBounds() const {
    if (mBands.IsEmpty()) {
      return mBounds;
    }

    int32_t top = mBands.begin()->top;
    int32_t bottom = mBands.LastElement().bottom;

    int32_t leftMost = mBands.begin()->mStrips.begin()->left;
    int32_t rightMost = mBands.begin()->mStrips.LastElement().right;
    for (const Band& band : mBands) {
      leftMost = std::min(leftMost, band.mStrips.begin()->left);
      rightMost = std::max(rightMost, band.mStrips.LastElement().right);
    }

    return nsRectAbsolute(leftMost, top, rightMost, bottom);
  }

  static uint32_t ComputeMergedAreaIncrease(const Band& aTopBand,
                                            const Band& aBottomBand);

  // Returns true if idx is now referring to the 'next' band
  bool CompressAdjacentBands(size_t& aIdx) {
    if ((aIdx + 1) < mBands.Length()) {
      if (mBands[aIdx + 1].top == mBands[aIdx].bottom &&
          mBands[aIdx + 1].EqualStrips(mBands[aIdx])) {
        mBands[aIdx].bottom = mBands[aIdx + 1].bottom;
        mBands.RemoveElementAt(aIdx + 1);
      }
    }
    if (aIdx) {
      if (mBands[aIdx - 1].bottom == mBands[aIdx].top &&
          mBands[aIdx].EqualStrips(mBands[aIdx - 1])) {
        mBands[aIdx - 1].bottom = mBands[aIdx].bottom;
        mBands.RemoveElementAt(aIdx);
        return true;
      }
    }
    return false;
  }

  void CompressBefore(size_t& aIdx) {
    if (aIdx && aIdx < mBands.Length()) {
      if (mBands[aIdx - 1].bottom == mBands[aIdx].top &&
          mBands[aIdx - 1].EqualStrips(mBands[aIdx])) {
        mBands[aIdx].top = mBands[aIdx - 1].top;
        mBands.RemoveElementAt(aIdx - 1);
        aIdx--;
      }
    }
  }

  BandArray mBands;
  // Considering we only ever OR with nsRects, the bounds should fit in an
  // nsRect as well.
  nsRectAbsolute mBounds;
#ifdef DEBUG_REGIONS
  friend class OperationStringGenerator;
  OperationStringGenerator* mCurrentOpGenerator;
#endif

 public:
  class RectIterator {
    const nsRegion& mRegion;
    typename BandArray::const_iterator mCurrentBand;
    typename StripArray::const_iterator mCurrentStrip;

   public:
    explicit RectIterator(const nsRegion& aRegion)
        : mRegion(aRegion),
          mCurrentBand(aRegion.mBands.begin())
#ifndef DEBUG
          ,
          mCurrentStrip(nullptr)
#endif
    {
      mIsDone = mRegion.mBounds.IsEmpty();
      if (mCurrentBand != aRegion.mBands.end()) {
        mCurrentStrip = mCurrentBand->mStrips.begin();
      }
    }

    bool Done() const { return mIsDone; }

    const nsRect Get() const {
      if (mRegion.mBands.IsEmpty()) {
        return mRegion.GetBounds();
      }
      return nsRect(mCurrentStrip->left, mCurrentBand->top,
                    mCurrentStrip->right - mCurrentStrip->left,
                    mCurrentBand->bottom - mCurrentBand->top);
    }

    const nsRectAbsolute GetAbsolute() const {
      if (mRegion.mBands.IsEmpty()) {
        return mRegion.mBounds;
      }
      return nsRectAbsolute(mCurrentStrip->left, mCurrentBand->top,
                            mCurrentStrip->right, mCurrentBand->bottom);
    }

    void Next() {
      if (mRegion.mBands.IsEmpty()) {
        mIsDone = true;
        return;
      }

      mCurrentStrip++;
      if (mCurrentStrip == mCurrentBand->mStrips.end()) {
        mCurrentBand++;
        if (mCurrentBand != mRegion.mBands.end()) {
          mCurrentStrip = mCurrentBand->mStrips.begin();
        } else {
          mIsDone = true;
        }
      }
    }

    bool mIsDone;
  };

  RectIterator RectIter() const { return RectIterator(*this); }
};

namespace mozilla {
namespace gfx {

/**
 * BaseIntRegions use int32_t coordinates.
 */
template <typename Derived, typename Rect, typename Point, typename Margin>
class BaseIntRegion {
  friend class ::nsRegion;

  // Give access to all specializations of IntRegionTyped, not just ones that
  // derive from this specialization of BaseIntRegion.
  template <typename units>
  friend class IntRegionTyped;

 public:
  typedef Rect RectType;
  typedef Point PointType;
  typedef Margin MarginType;

  BaseIntRegion() = default;
  MOZ_IMPLICIT BaseIntRegion(const Rect& aRect) : mImpl(ToRect(aRect)) {}
  explicit BaseIntRegion(mozilla::gfx::ArrayView<Rect> aRects)
      : mImpl(aRects) {}
  BaseIntRegion(const BaseIntRegion& aRegion) = default;
  BaseIntRegion(BaseIntRegion&& aRegion) : mImpl(std::move(aRegion.mImpl)) {}
  Derived& operator=(const Rect& aRect) {
    mImpl = ToRect(aRect);
    return This();
  }
  Derived& operator=(const Derived& aRegion) {
    mImpl = aRegion.mImpl;
    return This();
  }
  Derived& operator=(Derived&& aRegion) {
    mImpl = std::move(aRegion.mImpl);
    return This();
  }

  bool operator==(const Derived& aRgn) const { return IsEqual(aRgn); }
  bool operator!=(const Derived& aRgn) const { return !(*this == aRgn); }

  friend std::ostream& operator<<(std::ostream& stream, const Derived& m) {
    return stream << m.mImpl;
  }

  void AndWith(const Derived& aOther) { And(This(), aOther); }
  void AndWith(const Rect& aOther) { And(This(), aOther); }
  Derived& And(const Derived& aRgn1, const Derived& aRgn2) {
    mImpl.And(aRgn1.mImpl, aRgn2.mImpl);
    return This();
  }
  Derived& And(const Derived& aRegion, const Rect& aRect) {
    mImpl.And(aRegion.mImpl, ToRect(aRect));
    return This();
  }
  Derived& And(const Rect& aRect, const Derived& aRegion) {
    return And(aRegion, aRect);
  }
  Derived& And(const Rect& aRect1, const Rect& aRect2) {
    Rect TmpRect;

    TmpRect.IntersectRect(aRect1, aRect2);
    mImpl = ToRect(TmpRect);
    return This();
  }

  Derived& OrWith(const Derived& aOther) { return Or(This(), aOther); }
  Derived& OrWith(const Rect& aOther) { return Or(This(), aOther); }
  Derived& Or(const Derived& aRgn1, const Derived& aRgn2) {
    mImpl.Or(aRgn1.mImpl, aRgn2.mImpl);
    return This();
  }
  Derived& Or(const Derived& aRegion, const Rect& aRect) {
    mImpl.Or(aRegion.mImpl, ToRect(aRect));
    return This();
  }
  Derived& Or(const Rect& aRect, const Derived& aRegion) {
    return Or(aRegion, aRect);
  }
  Derived& Or(const Rect& aRect1, const Rect& aRect2) {
    mImpl = ToRect(aRect1);
    return Or(This(), aRect2);
  }

  Derived& XorWith(const Derived& aOther) { return Xor(This(), aOther); }
  Derived& XorWith(const Rect& aOther) { return Xor(This(), aOther); }
  Derived& Xor(const Derived& aRgn1, const Derived& aRgn2) {
    mImpl.Xor(aRgn1.mImpl, aRgn2.mImpl);
    return This();
  }
  Derived& Xor(const Derived& aRegion, const Rect& aRect) {
    mImpl.Xor(aRegion.mImpl, ToRect(aRect));
    return This();
  }
  Derived& Xor(const Rect& aRect, const Derived& aRegion) {
    return Xor(aRegion, aRect);
  }
  Derived& Xor(const Rect& aRect1, const Rect& aRect2) {
    mImpl = ToRect(aRect1);
    return Xor(This(), aRect2);
  }

  Derived& SubOut(const Derived& aOther) { return Sub(This(), aOther); }
  Derived& SubOut(const Rect& aOther) { return Sub(This(), aOther); }
  Derived& Sub(const Derived& aRgn1, const Derived& aRgn2) {
    mImpl.Sub(aRgn1.mImpl, aRgn2.mImpl);
    return This();
  }
  Derived& Sub(const Derived& aRegion, const Rect& aRect) {
    mImpl.Sub(aRegion.mImpl, ToRect(aRect));
    return This();
  }
  Derived& Sub(const Rect& aRect, const Derived& aRegion) {
    return Sub(Derived(aRect), aRegion);
  }
  Derived& Sub(const Rect& aRect1, const Rect& aRect2) {
    mImpl = ToRect(aRect1);
    return Sub(This(), aRect2);
  }

  /**
   * Returns true iff the given point is inside the region. A region
   * created from a rect (x=0, y=0, w=100, h=100) will NOT contain
   * the point x=100, y=100.
   */
  bool Contains(int aX, int aY) const { return mImpl.Contains(aX, aY); }
  bool Contains(const Point& aPoint) const {
    return mImpl.Contains(aPoint.x, aPoint.y);
  }
  bool Contains(const Rect& aRect) const {
    return mImpl.Contains(ToRect(aRect));
  }
  bool Contains(const Derived& aRgn) const {
    return mImpl.Contains(aRgn.mImpl);
  }
  bool Intersects(const Rect& aRect) const {
    return mImpl.Intersects(ToRect(aRect));
  }

  void MoveBy(int32_t aXOffset, int32_t aYOffset) {
    MoveBy(Point(aXOffset, aYOffset));
  }
  void MoveBy(Point aPt) { mImpl.MoveBy(aPt.X(), aPt.Y()); }
  Derived MovedBy(int32_t aXOffset, int32_t aYOffset) const {
    return MovedBy(Point(aXOffset, aYOffset));
  }
  Derived MovedBy(const Point& aPt) const {
    Derived copy(This());
    copy.MoveBy(aPt);
    return copy;
  }

  Derived Intersect(const Derived& aOther) const {
    Derived intersection;
    intersection.And(This(), aOther);
    return intersection;
  }

  void Inflate(const Margin& aMargin) {
    mImpl.Inflate(
        nsMargin(aMargin.top, aMargin.right, aMargin.bottom, aMargin.left));
  }
  Derived Inflated(const Margin& aMargin) const {
    Derived copy(This());
    copy.Inflate(aMargin);
    return copy;
  }

  void SetEmpty() { mImpl.SetEmpty(); }

  bool IsEmpty() const { return mImpl.IsEmpty(); }
  bool IsComplex() const { return mImpl.IsComplex(); }
  bool IsEqual(const Derived& aRegion) const {
    return mImpl.IsEqual(aRegion.mImpl);
  }
  uint32_t GetNumRects() const { return mImpl.GetNumRects(); }
  Rect GetBounds() const { return FromRect(mImpl.GetBounds()); }
  uint64_t Area() const { return mImpl.Area(); }
  nsRegion ToAppUnits(nscoord aAppUnitsPerPixel) const {
    nsRegion result;
    for (auto iter = RectIterator(*this); !iter.Done(); iter.Next()) {
      nsRect appRect = ::ToAppUnits(iter.Get(), aAppUnitsPerPixel);
      result.Or(result, appRect);
    }
    return result;
  }
  Rect GetLargestRectangle(const Rect& aContainingRect = Rect()) const {
    return FromRect(mImpl.GetLargestRectangle(ToRect(aContainingRect)));
  }

  Derived& ScaleRoundOut(float aXScale, float aYScale) {
    mImpl.ScaleRoundOut(aXScale, aYScale);
    return This();
  }

  Derived& ScaleInverseRoundOut(float aXScale, float aYScale) {
    mImpl.ScaleInverseRoundOut(aXScale, aYScale);
    return This();
  }

  // Prefer using TransformBy(matrix, region) from UnitTransforms.h,
  // as applying the transform should typically change the unit system.
  // TODO(botond): Move this to IntRegionTyped and disable it for
  //               unit != UnknownUnits.
  Derived& Transform(const mozilla::gfx::Matrix4x4& aTransform) {
    mImpl.Transform(aTransform);
    return This();
  }

  /**
   * Make sure the region has at most aMaxRects by adding area to it
   * if necessary. The simplified region will be a superset of the
   * original region. The simplified region's bounding box will be
   * the same as for the current region.
   */
  void SimplifyOutward(uint32_t aMaxRects) { mImpl.SimplifyOutward(aMaxRects); }
  void SimplifyOutwardByArea(uint32_t aThreshold) {
    mImpl.SimplifyOutwardByArea(aThreshold);
  }
  /**
   * Make sure the region has at most aMaxRects by removing area from
   * it if necessary. The simplified region will be a subset of the
   * original region.
   */
  void SimplifyInward(uint32_t aMaxRects) { mImpl.SimplifyInward(aMaxRects); }

  typedef void (*visitFn)(void* closure, VisitSide side, int x1, int y1, int x2,
                          int y2);
  void VisitEdges(visitFn visit, void* closure) const {
    mImpl.VisitEdges(visit, closure);
  }

  nsCString ToString() const { return mImpl.ToString(); }

  class RectIterator {
    nsRegion::RectIterator mImpl;  // The underlying iterator.
    mutable Rect mTmp;             // The most recently gotten rectangle.

   public:
    explicit RectIterator(const BaseIntRegion& aRegion)
        : mImpl(aRegion.mImpl) {}

    bool Done() const { return mImpl.Done(); }

    const Rect& Get() const {
      mTmp = FromRect(mImpl.Get());
      return mTmp;
    }

    void Next() { mImpl.Next(); }
  };

  RectIterator RectIter() const { return RectIterator(*this); }

 protected:
  // Expose enough to derived classes from them to define conversions
  // between different types of BaseIntRegions.
  explicit BaseIntRegion(const nsRegion& aImpl) : mImpl(aImpl) {}
  const nsRegion& Impl() const { return mImpl; }

 private:
  nsRegion mImpl;

  static nsRect ToRect(const Rect& aRect) {
    return nsRect(aRect.X(), aRect.Y(), aRect.Width(), aRect.Height());
  }
  static Rect FromRect(const nsRect& aRect) {
    return Rect(aRect.X(), aRect.Y(), aRect.Width(), aRect.Height());
  }

  Derived& This() { return *static_cast<Derived*>(this); }
  const Derived& This() const { return *static_cast<const Derived*>(this); }
};

template <class units>
class IntRegionTyped
    : public BaseIntRegion<IntRegionTyped<units>, IntRectTyped<units>,
                           IntPointTyped<units>, IntMarginTyped<units>> {
  typedef BaseIntRegion<IntRegionTyped<units>, IntRectTyped<units>,
                        IntPointTyped<units>, IntMarginTyped<units>>
      Super;

  // Make other specializations of IntRegionTyped friends.
  template <typename OtherUnits>
  friend class IntRegionTyped;

  static_assert(IsPixel<units>::value,
                "'units' must be a coordinate system tag");

 public:
  typedef IntRectTyped<units> RectType;
  typedef IntPointTyped<units> PointType;
  typedef IntMarginTyped<units> MarginType;

  // Forward constructors.
  IntRegionTyped() = default;
  MOZ_IMPLICIT IntRegionTyped(const IntRectTyped<units>& aRect)
      : Super(aRect) {}
  IntRegionTyped(const IntRegionTyped& aRegion) : Super(aRegion) {}
  explicit IntRegionTyped(mozilla::gfx::ArrayView<IntRectTyped<units>> aRects)
      : Super(aRects) {}
  IntRegionTyped(IntRegionTyped&& aRegion) : Super(std::move(aRegion)) {}

  // Assignment operators need to be forwarded as well, otherwise the compiler
  // will declare deleted ones.
  IntRegionTyped& operator=(const IntRegionTyped& aRegion) {
    return Super::operator=(aRegion);
  }
  IntRegionTyped& operator=(IntRegionTyped&& aRegion) {
    return Super::operator=(std::move(aRegion));
  }

  static IntRegionTyped FromUnknownRegion(const IntRegion& aRegion) {
    return IntRegionTyped(aRegion.Impl());
  }
  IntRegion ToUnknownRegion() const {
    // Need |this->| because Impl() is defined in a dependent base class.
    return IntRegion(this->Impl());
  }

 private:
  // This is deliberately private, so calling code uses FromUnknownRegion().
  explicit IntRegionTyped(const nsRegion& aRegion) : Super(aRegion) {}
};

}  // namespace gfx
}  // namespace mozilla

typedef mozilla::gfx::IntRegion nsIntRegion;

template <>
struct fmt::formatter<nsRegion> : fmt::ostream_formatter {};
template <class Units>
struct fmt::formatter<mozilla::gfx::IntRegionTyped<Units>>
    : fmt::ostream_formatter {};

#endif
