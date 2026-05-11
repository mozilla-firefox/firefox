/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_EditContext_h
#define mozilla_dom_EditContext_h

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/EditContextBinding.h"

namespace mozilla::dom {

class EditContext final : public DOMEventTargetHelper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(EditContext, DOMEventTargetHelper)

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<EditContext> Constructor(const GlobalObject& aGlobal,
                                                   const EditContextInit& aInit,
                                                   ErrorResult& aRv);

  void UpdateText(uint32_t aRangeStart, uint32_t aRangeEnd,
                  const nsAString& aText) {
    auto newLength = CheckedUint32(mText.Length()) + aText.Length();
    if (NS_WARN_IF(!newLength.isValid())) {
      // don't allow text length to overflow uint32_t
      return;
    }
    size_t start = std::min(aRangeStart, aRangeEnd);
    start = std::min(start, mText.Length());
    size_t end = std::max(aRangeStart, aRangeEnd);
    end = std::min(end, mText.Length());

    mText.Replace(start, end - start, aText);
  }
  void UpdateSelection(uint32_t aStart, uint32_t aEnd) {
    mSelectionStart = aStart;
    mSelectionEnd = aEnd;
  }
  void UpdateControlBounds(DOMRect& aControlBounds);
  void UpdateSelectionBounds(DOMRect& aSelectionBounds);
  void UpdateCharacterBounds(
      uint32_t aRangeStart,
      const Sequence<OwningNonNull<DOMRect>>& aCharacterBounds);
  void AttachedElements(nsTArray<RefPtr<nsGenericHTMLElement>>& aRetVal) {
    if (mAssociatedElement) {
      aRetVal.AppendElement(mAssociatedElement);
    }
  }

  void GetText(nsAString& aText) const { aText = mText; }
  uint32_t SelectionStart() const { return mSelectionStart; }
  uint32_t SelectionEnd() const { return mSelectionEnd; }
  uint32_t CharacterBoundsRangeStart() const {
    return mCodepointRectsStartIndex;
  }
  void CharacterBounds(nsTArray<RefPtr<DOMRect>>& aRetVal) const;

  nsGenericHTMLElement* GetAssociatedElement() const {
    return mAssociatedElement;
  }
  void SetAssociatedElement(nsGenericHTMLElement* aElement) {
    mAssociatedElement = aElement;
  }

  // https://w3c.github.io/edit-context/#dfn-deactivate-an-editcontext
  MOZ_CAN_RUN_SCRIPT void Deactivate();

  IMPL_EVENT_HANDLER(characterboundsupdate);
  IMPL_EVENT_HANDLER(compositionstart);
  IMPL_EVENT_HANDLER(compositionend);
  IMPL_EVENT_HANDLER(textformatupdate);
  IMPL_EVENT_HANDLER(textupdate);

  static EditContext* GetForElement(const Element& aElement);
  static void SetForElement(const Element& aElement, EditContext* aEditContext);
  /*
   * Returns whether there is any EditContext attached to any element
   * in this process.
   */
  static bool IsAnyAttached();

 private:
  explicit EditContext(nsIGlobalObject* aGlobalObject,
                       const EditContextInit& aInit);
  ~EditContext() = default;

  using Rect = gfx::RectTyped<CSSPixel, double>;

  RefPtr<DOMRect> ToDOMRect(const Rect& copy) const;
  Rect ToRect(const DOMRect& rect) const;

  RefPtr<nsGenericHTMLElement> mAssociatedElement;
  nsTArray<Rect> mCodepointRects;
  Rect mControlBounds;
  Rect mSelectionBounds;
  nsString mText;
  uint32_t mSelectionStart = 0;
  uint32_t mSelectionEnd = 0;
  uint32_t mCodepointRectsStartIndex = 0;
  bool mIsComposing = false;
};

}  // namespace mozilla::dom

#endif
