/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHtml5FragmentFastPath_h
#define nsHtml5FragmentFastPath_h

#include <cstdint>

#include "mozilla/Attributes.h"
#include "mozilla/TimeStamp.h"
#include "nsStringFwd.h"

class nsIContent;

namespace mozilla {
class ErrorResult;
namespace dom {
class Document;
class Element;
}  // namespace dom
}  // namespace mozilla

/**
 * A fast path for parsing innerHTML and DOMParser input for a small subset of
 * HTML: common tags whose nesting can't trigger any of the tree builder's
 * fix-ups, attributes, comments, and text with the character references whose
 * decoding doesn't depend on the context.
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
   * Parses aSource into aDocument, which must have no children, as the HTML
   * parser would parse a document from it. Supports input that is only body
   * content after optional whitespace, and that starts with a start tag or with
   * text other than a character reference. Returns false without touching
   * aDocument if aSource isn't supported, and otherwise behaves like
   * TryParse().
   */
  static bool TryParseDocument(const nsAString& aSource,
                               mozilla::dom::Document* aDocument,
                               mozilla::ErrorResult& aError);

  /**
   * How many times TryParse() or TryParseDocument() succeeded in this process,
   * for tests.
   */
  static uint32_t SuccessCount();

  /**
   * Measures for telemetry how long innerHTML or DOMParser takes to parse its
   * input, with or without the fast path, for a random sample of the calls
   * (see dom.html_fragment_fast_path.telemetry_sample_shift).
   */
  class MOZ_STACK_CLASS ParseTimer final {
   public:
    enum class Caller : uint8_t { InnerHTML, DOMParser };

    /**
     * Starts measuring the parsing of aLength code units, if this call is
     * sampled.
     */
    ParseTimer(Caller aCaller, uint32_t aLength);

    /**
     * Records that the fast path was tried, and whether it parsed the input.
     */
    void FastPathTried(bool aParsed);

    /**
     * Records the time since the timer was created.
     */
    void Finish();

   private:
    enum class Outcome : uint8_t { Fast, Fallback, Full };

    mozilla::TimeStamp mStart;
    const uint32_t mLength;
    const Caller mCaller;
    const bool mSampled;
    Outcome mOutcome = Outcome::Full;
  };
};

#endif  // nsHtml5FragmentFastPath_h
