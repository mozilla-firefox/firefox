/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* types defined to pass values through Gecko_* and Servo_* FFI functions */

#ifndef mozilla_ServoTypes_h
#define mozilla_ServoTypes_h

#include "NonCustomCSSPropertyId.h"
#include "X11UndefineNone.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TypedEnumBits.h"
#include "nsCoord.h"

namespace mozilla {
struct StyleLockedFontFaceRule;
enum class StyleOrigin : uint8_t;
struct LangGroupFontPrefs;
}  // namespace mozilla

// used for associating origin with specific @font-face rules
struct nsFontFaceRuleContainer {
  RefPtr<mozilla::StyleLockedFontFaceRule> mRule;
  mozilla::StyleOrigin mOrigin;
};

namespace mozilla {

// Indicates whether the Servo style system should expect the style on an
// element to have already been resolved (i.e. via a parallel traversal), or
// whether it may be lazily computed.
enum class LazyComputeBehavior {
  Allow,
  Assert,
};

// Various flags for the servo traversal.
enum class ServoTraversalFlags : uint32_t {
  Empty = 0,
  // Traverses as normal mode but tries to update all CSS animations.
  ForCSSRuleChanges = 1 << 1,
  // A traversal that isn't followed by a post-traversal (e.g. when styling a
  // new subtree during frame construction), so it doesn't accumulate damage,
  // handle snapshots, or leave restyle state behind.
  Forgetful = 1 << 2,
  // Allows the traversal to run in parallel if there are sufficient cores on
  // the machine.
  ParallelTraversal = 1 << 7,
  // Flush throttled animations. By default, we only update throttled animations
  // when we have other non-throttled work to do. With this flag, we
  // unconditionally tick and process them.
  FlushThrottledAnimations = 1 << 8,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(ServoTraversalFlags)

// Indicates which rules should be included when performing selecting matching
// on an element.  DefaultOnly is used to exclude all rules except for those
// that come from UA style sheets, and is used to implemented
// getDefaultComputedStyle.
enum class StyleRuleInclusion {
  All,
  DefaultOnly,
};

// Represents which tasks are performed in a SequentialTask of UpdateAnimations.
enum class UpdateAnimationsTasks : uint8_t {
  CSSAnimations = 1 << 0,
  CSSTransitions = 1 << 1,
  EffectProperties = 1 << 2,
  CascadeResults = 1 << 3,
  DisplayChangedFromNone = 1 << 4,
  ScrollTimelines = 1 << 5,
  ViewTimelines = 1 << 6,
  TimelineScopes = 1 << 7,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(UpdateAnimationsTasks)

// The kind of style we're generating when requesting Servo to give us an
// inherited style.
enum class InheritTarget {
  // We're requesting a text style.
  Text,
  // We're requesting a first-letter continuation frame style.
  FirstLetterContinuation,
  // We're requesting a style for a placeholder frame.
  PlaceholderFrame,
};

// Represents values for interaction media features.
// https://drafts.csswg.org/mediaqueries-4/#mf-interaction
enum class PointerCapabilities : uint8_t {
  None = 0,
  Coarse = 1 << 0,
  Fine = 1 << 1,
  Hover = 1 << 2,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(PointerCapabilities)

// These measurements are obtained for both the UA cache and the Stylist, but
// not all the fields are used in both cases.
class ServoStyleSetSizes {
 public:
  size_t mRuleTree;               // Stylist-only
  size_t mPrecomputedPseudos;     // UA cache-only
  size_t mElementAndPseudosMaps;  // Used for both
  size_t mInvalidationMap;        // Used for both
  size_t mRevalidationSelectors;  // Used for both
  size_t mOther;                  // Used for both

  ServoStyleSetSizes()
      : mRuleTree(0),
        mPrecomputedPseudos(0),
        mElementAndPseudosMaps(0),
        mInvalidationMap(0),
        mRevalidationSelectors(0),
        mOther(0) {}
};

// A callback that can be sent via FFI which will be invoked _right before_
// being mutated, and at most once.
struct DeclarationBlockMutationClosure {
  // The callback function. The first argument is `data`, the second is the
  // property id that changed.
  void (*function)(void*, NonCustomCSSPropertyId) = nullptr;
  void* data = nullptr;
};

struct MediumFeaturesChangedResult {
  bool mAffectsDocumentRules;
  bool mAffectsNonDocumentRules;
};

}  // namespace mozilla

#endif  // mozilla_ServoTypes_h
