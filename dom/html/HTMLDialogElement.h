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
  using Element::Command;

  enum class ClosedBy : uint8_t {
    Auto,
    None,
    Any,
    CloseRequest,
  };

  explicit HTMLDialogElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
      : nsGenericHTMLElement(std::move(aNodeInfo)),
        mPreviouslyFocusedElement(nullptr),
        mRequestCloseSourceElement(nullptr) {}

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLDialogElement, dialog)

  nsresult Clone(dom::NodeInfo* aNodeInfo, nsINode** aResult) const override;

  ClosedBy GetClosedBy() const;
  void GetClosedBy(nsAString& aValue) const;
  void SetClosedBy(const nsAString& aClosedby, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::closedby, aClosedby, aError);
  }
  bool ParseClosedByAttribute(const nsAString& aValue, nsAttrValue& aResult);

  // nsIContent
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  bool Open() const;
  void SetOpen(bool aOpen, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::open, aOpen, aError);
  }

  void GetReturnValue(nsAString& aReturnValue) { aReturnValue = mReturnValue; }
  void SetReturnValue(const nsAString& aReturnValue) {
    mReturnValue = aReturnValue;
  }

  void GetRequestCloseReturnValue(Maybe<nsAutoString>& aReturnValue) {
    if (mRequestCloseReturnValue.isSome()) {
      aReturnValue.emplace(mRequestCloseReturnValue.ref());
    }
  }
  void ClearRequestCloseReturnValue() { mRequestCloseReturnValue.reset(); }
  void SetRequestCloseReturnValue(const nsAString& aReturnValue) {
    mRequestCloseReturnValue.reset();
    mRequestCloseReturnValue.emplace(aReturnValue);
  }

  nsresult BindToTree(BindContext&, nsINode&) override;
  void UnbindFromTree(UnbindContext&) override;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Close(
      const mozilla::dom::Optional<nsAString>& aReturnValue) {
    Maybe<nsAutoString> retValueCopy;
    if (aReturnValue.WasPassed()) {
      retValueCopy.emplace(aReturnValue.Value());
    }
    return Close(nullptr, retValueCopy);
  }
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Close(
      Element* aSource, const Maybe<nsAutoString>& aReturnValue);
  MOZ_CAN_RUN_SCRIPT void RequestClose(
      const mozilla::dom::Optional<nsAString>& aReturnValue) {
    Maybe<nsAutoString> retValueCopy;
    if (aReturnValue.WasPassed()) {
      retValueCopy.emplace(aReturnValue.Value());
    }
    RequestClose(nullptr, retValueCopy);
  }
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void RequestClose(
      Element* aSource, const Maybe<nsAutoString>& aReturnValue);

  RefPtr<Element> GetRequestCloseSourceElement();

  MOZ_CAN_RUN_SCRIPT void Show(ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT void ShowModal(Element* aSource, ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT void ShowModal(ErrorResult& aError) {
    return ShowModal(nullptr, aError);
  }

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;

  void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

  bool IsInTopLayer() const;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void FocusDialog();

  int32_t TabIndexDefault() override;

  bool IsValidCommandAction(Command aCommand) const override;
  MOZ_CAN_RUN_SCRIPT bool HandleCommandInternal(Element* aSource,
                                                Command aCommand,
                                                ErrorResult& aRv) override;

  Maybe<nsString> mRequestCloseReturnValue;
  nsString mReturnValue;

 protected:
  virtual ~HTMLDialogElement();
  JSObject* WrapNode(JSContext* aCx,
                     JS::Handle<JSObject*> aGivenProto) override;

 private:
  void AddToTopLayerIfNeeded();
  void RemoveFromTopLayerIfNeeded();
  void StorePreviouslyFocusedElement();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void QueueToggleEventTask(Element* aSource);
  void SetDialogCloseWatcherIfNeeded();
  void SetCloseWatcherEnabledState();

  void SetupSteps();
  void CleanupSteps();

  nsWeakPtr mPreviouslyFocusedElement;

  nsWeakPtr mRequestCloseSourceElement;

  RefPtr<AsyncEventDispatcher> mToggleEventDispatcher;

  // This won't need to be cycle collected as CloseWatcher only has strong
  // references to event listeners, which themselves have Weak References back
  // to the Node.
  RefPtr<CloseWatcher> mCloseWatcher;
};

}  // namespace mozilla::dom

#endif
