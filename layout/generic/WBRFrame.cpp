/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for HTML <wbr> elements */

#include "mozilla/PresShell.h"
#include "nsHTMLParts.h"
#include "nsIFrame.h"

using namespace mozilla;

namespace mozilla {

class WBRFrame final : public nsIFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(WBRFrame)

  friend nsIFrame* ::NS_NewWBRFrame(mozilla::PresShell* aPresShell,
                                    ComputedStyle* aStyle);

  virtual FrameSearchResult PeekOffsetNoAmount(bool aForward,
                                               int32_t* aOffset) override;
  virtual FrameSearchResult PeekOffsetCharacter(
      bool aForward, int32_t* aOffset,
      PeekOffsetCharacterOptions aOptions =
          PeekOffsetCharacterOptions()) override;
  virtual FrameSearchResult PeekOffsetWord(
      bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
      int32_t* aOffset, PeekWordState* aState, bool aTrimSpaces) override;

 protected:
  explicit WBRFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {}
};

}  // namespace mozilla

nsIFrame* NS_NewWBRFrame(mozilla::PresShell* aPresShell,
                         ComputedStyle* aStyle) {
  return new WBRFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(WBRFrame)

nsIFrame::FrameSearchResult WBRFrame::PeekOffsetNoAmount(bool aForward,
                                                         int32_t* aOffset) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // WBR frames can not contain text or non-rendered whitespace
  return CONTINUE;
}

nsIFrame::FrameSearchResult WBRFrame::PeekOffsetCharacter(
    bool aForward, int32_t* aOffset, PeekOffsetCharacterOptions aOptions) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // WBR frames can not contain characters
  return CONTINUE;
}

nsIFrame::FrameSearchResult WBRFrame::PeekOffsetWord(
    bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
    int32_t* aOffset, PeekWordState* aState, bool aTrimSpaces) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // WBR frames can never contain text so we'll never find a word inside them
  return CONTINUE;
}
