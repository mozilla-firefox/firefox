/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_XULResizerElement_h
#define mozilla_dom_XULResizerElement_h

#include "Units.h"
#include "nsXULElement.h"

namespace mozilla::dom {

nsXULElement* NS_NewXULResizerElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

class XULResizerElement final : public nsXULElement {
 public:
  explicit XULResizerElement(already_AddRefed<dom::NodeInfo> aNodeInfo)
      : nsXULElement(std::move(aNodeInfo)) {}

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEvent(EventChainPostVisitor&) override;

 private:
  virtual ~XULResizerElement() = default;
  JSObject* WrapNode(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;

  struct Direction {
    int8_t mHorizontal;
    int8_t mVertical;
  };
  Direction GetDirection();

  nsIContent* GetContentToResize() const;
  // The current size of the content to resize (if available).
  Maybe<nsSize> GetCurrentSize() const;

  struct SizeInfo {
    nsCString width, height;
  };
  static void SizeInfoDtorFunc(void* aObject, nsAtom* aPropertyName,
                               void* aPropertyValue, void* aData);
  static void ResizeContent(nsIContent* aContent, const Direction& aDirection,
                            const SizeInfo& aSizeInfo,
                            SizeInfo* aOriginalSizeInfo);
  static void MaybePersistOriginalSize(nsIContent* aContent,
                                       const SizeInfo& aSizeInfo);
  static void RestoreOriginalSize(nsIContent* aContent);

  nsSize mMouseDownSize;
  LayoutDeviceIntPoint mMouseDownPoint;
  bool mTrackingMouseMove = false;
};

}  // namespace mozilla::dom

#endif  // XULResizerElement_h
