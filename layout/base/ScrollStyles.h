/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ScrollStyles_h
#define mozilla_ScrollStyles_h

#include <stdint.h>

// Forward declarations
struct nsStyleDisplay;

namespace mozilla {

enum class StyleOverflow : uint8_t;

struct ScrollStyles {
  // Always one of Scroll, Hidden, or Auto.
  StyleOverflow mHorizontal;
  StyleOverflow mVertical;

  ScrollStyles(StyleOverflow aH, StyleOverflow aV);

  // NOTE: This ctor maps `visible` to `auto` and `clip` to `hidden`.
  // It's used for styles that are propagated from the <body> and for
  // scroll frames (which we create also for overflow:clip/visible in
  // some cases, e.g. form controls).
  enum MapOverflowToValidScrollStyleTag { MapOverflowToValidScrollStyle };
  ScrollStyles(const nsStyleDisplay&, MapOverflowToValidScrollStyleTag);

  bool operator==(const ScrollStyles&) const = default;

  bool IsHiddenInBothDirections() const;
};

}  // namespace mozilla

#endif  // mozilla_ScrollStyles_h
