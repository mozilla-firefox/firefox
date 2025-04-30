/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HTMLDialogElement_h
#define HTMLDialogElement_h

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/CloseWatcher.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"

namespace mozilla::dom {

class HTMLDialogElement final : public nsGenericHTMLElement {
 public:
  explicit HTMLDialogElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : nsGenericHTMLElement(std::move(aNodeInfo)),
        mPreviouslyFocusedElement(nullptr) {}

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLDialogElement, dialog)

  nsresult Clone(dom::NodeInfo* aNodeInfo, nsINode** aResult) const override;

  bool Open() const { return GetBoolAttr(nsGkAtoms::open); }
  void SetOpen(bool aOpen, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::open, aOpen, aError);
  }

  void GetReturnValue(nsAString& aReturnValue) { aReturnValue = mReturnValue; }
  void SetReturnValue(const nsAString& aReturnValue) {
    mReturnValue = aReturnValue;
  }

  nsAString& RequestCloseReturnValue() { return mRequestCloseReturnValue; }
  void SetRequestCloseReturnValue(const nsAString& aReturnValue) {
    mRequestCloseReturnValue = aReturnValue;
  }

  void UnbindFromTree(UnbindContext&) override;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Close(
      const mozilla::dom::Optional<nsAString>& aReturnValue);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void RequestClose(
      const mozilla::dom::Optional<nsAString>& aReturnValue);
  MOZ_CAN_RUN_SCRIPT void Show(ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT void ShowModal(ErrorResult& aError);

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;

  void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

  bool IsInTopLayer() const;
  void QueueCancelDialog();
  MOZ_CAN_RUN_SCRIPT void RunCancelDialogSteps();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FocusDialog();

  int32_t TabIndexDefault() override;

  bool IsValidInvokeAction(InvokeAction aAction) const override;
  MOZ_CAN_RUN_SCRIPT bool HandleInvokeInternal(Element* invoker,
                                               InvokeAction aAction,
                                               ErrorResult& aRv) override;

  nsString mRequestCloseReturnValue;
  nsString mReturnValue;

 protected:
  virtual ~HTMLDialogElement();
  JSObject* WrapNode(JSContext* aCx,
                     JS::Handle<JSObject*> aGivenProto) override;

 private:
  void AddToTopLayerIfNeeded();
  void RemoveFromTopLayerIfNeeded();
  void StorePreviouslyFocusedElement();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void QueueToggleEventTask();
  void SetDialogCloseWatcher();

  nsWeakPtr mPreviouslyFocusedElement;

  RefPtr<AsyncEventDispatcher> mToggleEventDispatcher;

  // This won't need to be cycle collected as CloseWatcher only has strong
  // references to event listeners, which themselves have Weak References back
  // to the Node.
  RefPtr<CloseWatcher> mCloseWatcher;
};

}  // namespace mozilla::dom

#endif
