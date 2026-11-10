/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHtml5FragmentFastPath_h
#define nsHtml5FragmentFastPath_h

#include <cstdint>

#include "nsStringFwd.h"

class nsIContent;

namespace mozilla {
class ErrorResult;
namespace dom {
class Element;
}  // namespace dom
}  // namespace mozilla

/**
 * A fast path for parsing innerHTML for a small subset of HTML: common tags
 * whose nesting can't trigger any of the tree builder's fix-ups, plain
 * attributes and text without character references.
 *
 * It checks all of the input before it creates any node, so that callers can
 * fall back to the full parser for input outside the subset.
 */
class nsHtml5FragmentFastPath final {
 public:
  /**
   * Parses aSource into aTarget, which must have no children, as the HTML
   * fragment parsing algorithm would with aContext as the context element.
   * Returns false without touching aTarget if aSource or aContext isn't
   * supported. Otherwise returns true, and sets aError to the error, if any,
   * that stopped building the nodes, like the full parser does.
   */
  static bool TryParse(const nsAString& aSource, nsIContent* aTarget,
                       mozilla::dom::Element* aContext,
                       mozilla::ErrorResult& aError);

  /**
   * How many times TryParse() succeeded in this process, for tests.
   */
  static uint32_t SuccessCount();
};

#endif  // nsHtml5FragmentFastPath_h
