/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWindowSizes_h
#define nsWindowSizes_h

#include "mozilla/Assertions.h"
#include "mozilla/SizeOfState.h"
#include "nsStyleStructList.h"

class nsTabSizes {
 public:
  enum Kind {
    DOM,    // DOM stuff.
    Style,  // Style stuff.
    Other   // Everything else.
  };

  nsTabSizes() : mDom(0), mStyle(0), mOther(0) {}

  void add(Kind kind, size_t n) {
    switch (kind) {
      case DOM:
        mDom += n;
        break;
      case Style:
        mStyle += n;
        break;
      case Other:
        mOther += n;
        break;
      default:
        MOZ_CRASH("bad nsTabSizes kind");
    }
  }

  size_t mDom;
  size_t mStyle;
  size_t mOther;
};

#define ZERO_SIZE(kind, mSize) mSize(0),
#define ADD_TO_TAB_SIZES(kind, mSize) aSizes->add(nsTabSizes::kind, mSize);
#define ADD_TO_TOTAL_SIZE(kind, mSize) total += mSize;
#define DECL_SIZE(kind, mSize) size_t mSize;

#define NS_STYLE_SIZES_FIELD(name_) mStyle##name_

struct nsStyleSizes {
  nsStyleSizes()
      :
#define INIT_FIELD(name_) NS_STYLE_SIZES_FIELD(name_)(0),
        FOR_EACH_STYLE_STRUCT(INIT_FIELD, INIT_FIELD)
#undef INIT_FIELD

            dummy() {
  }

  void addToTabSizes(nsTabSizes* aSizes) const {
#define ADD_TO_TAB(name_) \
  aSizes->add(nsTabSizes::Style, NS_STYLE_SIZES_FIELD(name_));
    FOR_EACH_STYLE_STRUCT(ADD_TO_TAB, ADD_TO_TAB)
#undef ADD_TO_TAB
  }

  size_t getTotalSize() const {
    size_t total = 0;

#define ADD_TO_TOTAL(name_) total += NS_STYLE_SIZES_FIELD(name_);
    FOR_EACH_STYLE_STRUCT(ADD_TO_TOTAL, ADD_TO_TOTAL)
#undef ADD_TO_TOTAL

    return total;
  }

#define DECLARE_FIELD(name_) size_t NS_STYLE_SIZES_FIELD(name_);
  FOR_EACH_STYLE_STRUCT(DECLARE_FIELD, DECLARE_FIELD)
#undef DECLARE_FIELD

  // Present just to absorb the trailing comma in the constructor.
  int dummy;
};

#define NS_ARENA_SIZES_FIELD(classname) mArena##classname

struct nsArenaSizes {
#define PRES_ARENA_OBJECT(name_) size_t NS_ARENA_SIZES_FIELD(name_) = 0;
#define DISPLAY_LIST_ARENA_OBJECT(name_) PRES_ARENA_OBJECT(name_)
#include "nsDisplayListArenaTypes.inc"
#include "nsPresArenaObjectList.inc"
#undef PRES_ARENA_OBJECT
#undef DISPLAY_LIST_ARENA_OBJECT

  // Memory in the frame and display list arenas that isn't attributed to any
  // particular object type.
  size_t mFrames = 0;
  size_t mDisplayList = 0;
};

struct nsDOMSizes {
#define FOR_EACH_SIZE(MACRO)                 \
  MACRO(DOM, mDOMElementNodesSize)           \
  MACRO(DOM, mDOMTextNodesSize)              \
  MACRO(DOM, mDOMCDATANodesSize)             \
  MACRO(DOM, mDOMCommentNodesSize)           \
  MACRO(DOM, mDOMEventTargetsSize)           \
  MACRO(DOM, mDOMMediaQueryLists)            \
  MACRO(DOM, mDOMPerformanceEventEntries)    \
  MACRO(DOM, mDOMPerformanceUserEntries)     \
  MACRO(DOM, mDOMPerformanceResourceEntries) \
  MACRO(DOM, mDOMResizeObserverControllerSize)

  nsDOMSizes() : FOR_EACH_SIZE(ZERO_SIZE) mDOMOtherSize(0) {}

  void addToTabSizes(nsTabSizes* aSizes) const {
    FOR_EACH_SIZE(ADD_TO_TAB_SIZES)
    aSizes->add(nsTabSizes::DOM, mDOMOtherSize);
  }

  size_t getTotalSize() const {
    size_t total = 0;
    FOR_EACH_SIZE(ADD_TO_TOTAL_SIZE)
    total += mDOMOtherSize;
    return total;
  }

  FOR_EACH_SIZE(DECL_SIZE)

  size_t mDOMOtherSize;
#undef FOR_EACH_SIZE
};

class nsWindowSizes {
#define FOR_EACH_SIZE(MACRO)                                 \
  MACRO(Style, mLayoutStyleSheetsSize)                       \
  MACRO(Style, mLayoutShadowDomStyleSheetsSize)              \
  MACRO(Style, mLayoutShadowDomAuthorStyles)                 \
  MACRO(Other, mLayoutPresShellSize)                         \
  MACRO(Other, mLayoutRetainedDisplayListSize)               \
  MACRO(Style, mLayoutStyleSetsStylistRuleTree)              \
  MACRO(Style, mLayoutStyleSetsStylistElementAndPseudosMaps) \
  MACRO(Style, mLayoutStyleSetsStylistInvalidationMap)       \
  MACRO(Style, mLayoutStyleSetsStylistRevalidationSelectors) \
  MACRO(Style, mLayoutStyleSetsStylistOther)                 \
  MACRO(Style, mLayoutStyleSetsOther)                        \
  MACRO(Style, mLayoutElementDataObjects)                    \
  MACRO(Other, mLayoutTextRunsSize)                          \
  MACRO(Other, mLayoutPresContextSize)                       \
  MACRO(Other, mLayoutFramePropertiesSize)                   \
  MACRO(Style, mLayoutComputedValuesDom)                     \
  MACRO(Style, mLayoutComputedValuesNonDom)                  \
  MACRO(Style, mLayoutComputedValuesVisited)                 \
  MACRO(Style, mLayoutSvgMappedDeclarations)                 \
  MACRO(Other, mPropertyTablesSize)                          \
  MACRO(Other, mBindingsSize)

 public:
  explicit nsWindowSizes(mozilla::SizeOfState& aState)
      : FOR_EACH_SIZE(ZERO_SIZE) mDOMEventTargetsCount(0),
        mDOMEventListenersCount(0),
        mMediaSourceURLsCount(0),
        mState(aState) {}

  void addToTabSizes(nsTabSizes* aSizes) const {
    FOR_EACH_SIZE(ADD_TO_TAB_SIZES)
    mDOMSizes.addToTabSizes(aSizes);
    mStyleSizes.addToTabSizes(aSizes);
  }

  size_t getTotalSize() const {
    size_t total = 0;

    FOR_EACH_SIZE(ADD_TO_TOTAL_SIZE)
    total += mDOMSizes.getTotalSize();
    total += mStyleSizes.getTotalSize();

    return total;
  }

  FOR_EACH_SIZE(DECL_SIZE);

  uint32_t mDOMEventTargetsCount;
  uint32_t mDOMEventListenersCount;

  uint32_t mMediaSourceURLsCount;

  nsDOMSizes mDOMSizes;

  nsStyleSizes mStyleSizes;

  mozilla::SizeOfState& mState;

#undef FOR_EACH_SIZE
};

#undef ZERO_SIZE
#undef ADD_TO_TAB_SIZES
#undef ADD_TO_TOTAL_SIZE
#undef DECL_SIZE

#endif  // nsWindowSizes_h
