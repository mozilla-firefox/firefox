/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a set of ranges on a number-line */

#ifndef nsIntervalSet_h_
#define nsIntervalSet_h_

#include "nsCoord.h"

/*
 * A list-based class (hopefully tree-based when I get around to it)
 * for representing a set of ranges on a number-line.
 */
class nsIntervalSet {
 public:
  typedef nscoord coord_type;

  nsIntervalSet();
  ~nsIntervalSet();

  /*
   * Include the interval [aBegin, aEnd] in the set.
   *
   * Removal of intervals added is not supported because that would
   * require keeping track of the individual intervals that were
   * added (nsIntervalMap should do that).  It would be simple to
   * implement ExcludeInterval if anyone wants it, though.
   */
  void IncludeInterval(coord_type aBegin, coord_type aEnd);

  /*
   * Are _some_ points in [aBegin, aEnd] contained within the set
   * of intervals?
   */
  bool Intersects(coord_type aBegin, coord_type aEnd) const;

  /*
   * Are _all_ points in [aBegin, aEnd] contained within the set
   * of intervals?
   */
  bool Contains(coord_type aBegin, coord_type aEnd) const;

  bool IsEmpty() const { return !mList; }

 private:
  class Interval {
   public:
    Interval(coord_type aBegin, coord_type aEnd)
        : mBegin(aBegin), mEnd(aEnd), mPrev(nullptr), mNext(nullptr) {}

    coord_type mBegin;
    coord_type mEnd;
    Interval* mPrev;
    Interval* mNext;
  };

  void* AllocateInterval();
  void FreeInterval(Interval* aInterval);

  Interval* mList;
};

#endif  // !defined(nsIntervalSet_h_)
