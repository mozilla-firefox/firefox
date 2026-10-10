/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLFormElement_h
#define mozilla_dom_HTMLFormElement_h

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/AnchorAreaFormRelValues.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/RadioGroupManager.h"
#include "nsCOMPtr.h"
#include "nsIFormControl.h"
#include "nsGenericHTMLElement.h"
#include "nsIRadioGroupContainer.h"
#include "nsIWeakReferenceUtils.h"
#include "nsThreadUtils.h"
#include "nsInterfaceHashtable.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"
#include "js/friend/DOMProxy.h"  // JS::ExpandoAndGeneration

class nsIMutableArray;
class nsIURI;

namespace mozilla {
class EventChainPostVisitor;
class EventChainPreVisitor;
namespace dom {
class DialogFormSubmission;
class HTMLFormControlsCollection;
class HTMLFormSubmission;
class HTMLImageElement;
class FormData;

class HTMLFormElement final : public nsGenericHTMLElement,
                              public nsIRadioGroupContainer,
                              public AnchorAreaFormRelValues,
                              RadioGroupManager {
  friend class HTMLFormControlsCollection;

 public:
  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLFormElement, form)

  explicit HTMLFormElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

  enum { FORM_CONTROL_LIST_HASHTABLE_LENGTH = 8 };

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  int32_t IndexOfContent(nsIContent* aContent);
  nsGenericHTMLFormElement* GetDefaultSubmitElement() const;

  // nsIRadioGroupContainer
  void SetCurrentRadioButton(const nsAString& aName,
                             HTMLInputElement* aRadio) override;
  HTMLInputElement* GetCurrentRadioButton(const nsAString& aName) override;
  NS_IMETHOD GetNextRadioButton(const nsAString& aName, const bool aPrevious,
                                HTMLInputElement* aFocusedRadio,
                                HTMLInputElement** aRadioOut) override;
  NS_IMETHOD WalkRadioGroup(const nsAString& aName,
                            nsIRadioVisitor* aVisitor) override;
  void AddToRadioGroup(const nsAString& aName,
                       HTMLInputElement* aRadio) override;
  void RemoveFromRadioGroup(const nsAString& aName,
                            HTMLInputElement* aRadio) override;
  uint32_t GetRequiredRadioCount(const nsAString& aName) const override;
  void RadioRequiredWillChange(const nsAString& aName,
                               bool aRequiredAdded) override;
  bool GetValueMissingState(const nsAString& aName) const override;
  void SetValueMissingState(const nsAString& aName, bool aValue) override;

  ElementState IntrinsicState() const override;

  // EventTarget
  void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

  /** Whether we already dispatched a DOMFormHasPassword event or not */
  bool mHasPendingPasswordEvent = false;
  /** Whether we already dispatched a DOMFormHasPossibleUsername event or not */
  bool mHasPendingPossibleUsernameEvent = false;

  // nsIContent
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;
  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  void WillHandleEvent(EventChainPostVisitor& aVisitor) override;
  nsresult PostHandleEvent(EventChainPostVisitor& aVisitor) override;

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(bool aNullParent = true) override;
  void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;

  /**
   * Forget all information about the current submission (and the fact that we
   * are currently submitting at all).
   */
  void ForgetCurrentSubmission();

  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLFormElement,
                                           nsGenericHTMLElement)

  /**
   * Remove an element from this form's list of elements
   *
   * @param aElement the element to remove
   * @param aUpdateValidity If true, updates the form validity.
   * @return NS_OK if the element was successfully removed.
   */
  nsresult RemoveElement(nsGenericHTMLFormElement* aElement,
                         bool aUpdateValidity);

  /**
   * Remove an element from the lookup table maintained by the form.
   * We can't fold this method into RemoveElement() because when
   * RemoveElement() is called it doesn't know if the element is
   * removed because the id attribute has changed, or because the
   * name attribute has changed.
   *
   * @param aElement the element to remove
   * @param aName the name or id of the element to remove
   * @return NS_OK if the element was successfully removed.
   */
  nsresult RemoveElementFromTable(nsGenericHTMLFormElement* aElement,
                                  const nsAString& aName);

  /**
   * Add an element to end of this form's list of elements
   *
   * @param aElement the element to add
   * @param aUpdateValidity If true, the form validity will be updated.
   * @param aNotify If true, send DocumentObserver notifications as needed.
   * @return NS_OK if the element was successfully added
   */
  nsresult AddElement(nsGenericHTMLFormElement* aElement, bool aUpdateValidity,
                      bool aNotify);

  /**
   * Add an element to the lookup table maintained by the form.
   *
   * We can't fold this method into AddElement() because when
   * AddElement() is called, the form control has no
   * attributes.  The name or id attributes of the form control
   * are used as a key into the table.
   */
  nsresult AddElementToTable(nsGenericHTMLFormElement* aChild,
                             const nsAString& aName);

  /**
   * Remove an image element from this form's list of image elements
   *
   * @param aElement the image element to remove
   * @return NS_OK if the element was successfully removed.
   */
  nsresult RemoveImageElement(HTMLImageElement* aElement);

  /**
   * Remove an image element from the lookup table maintained by the form.
   * We can't fold this method into RemoveImageElement() because when
   * RemoveImageElement() is called it doesn't know if the element is
   * removed because the id attribute has changed, or because the
   * name attribute has changed.
   *
   * @param aElement the image element to remove
   * @param aName the name or id of the element to remove
   * @return NS_OK if the element was successfully removed.
   */
  nsresult RemoveImageElementFromTable(HTMLImageElement* aElement,
                                       const nsAString& aName);
  /**
   * Add an image element to the end of this form's list of image elements
   *
   * @param aElement the element to add
   * @return NS_OK if the element was successfully added
   */
  nsresult AddImageElement(HTMLImageElement* aElement);

  /**
   * Add an image element to the lookup table maintained by the form.
   *
   * We can't fold this method into AddImageElement() because when
   * AddImageElement() is called, the image attributes can change.
   * The name or id attributes of the image are used as a key into the table.
   */
  nsresult AddImageElementToTable(HTMLImageElement* aChild,
                                  const nsAString& aName);

  /**
   * Returns true if implicit submission of this form is disabled. For more
   * on implicit submission see:
   *
   * http://www.whatwg.org/specs/web-apps/current-work/multipage/association-of-controls-and-forms.html#implicit-submission
   */
  bool ImplicitSubmissionIsDisabled() const;

  /**
   * Check whether a given nsGenericHTMLFormElement is the last single line
   * input control that is not disabled. aElement is expected to not be null.
   */
  bool IsLastActiveElement(const nsGenericHTMLFormElement* aElement) const;

  /**
   * Check whether a given nsGenericHTMLFormElement is the default submit
   * element.  This is different from just comparing to
   * GetDefaultSubmitElement() in certain situations inside an update
   * when GetDefaultSubmitElement() might not be up to date. aElement
   * is expected to not be null.
   */
  bool IsDefaultSubmitElement(const nsGenericHTMLFormElement* aElement) const;

  /**
   * Flag the form to know that a button or image triggered scripted form
   * submission. In that case the form will defer the submission until the
   * script handler returns and the return value is known.
   */
  void OnSubmitClickBegin(Element* aOriginatingElement);
  void OnSubmitClickEnd();

  /**
   * This method will update the form validity.
   *
   * This method has to be called by form elements whenever their validity state
   * or status regarding constraint validation changes.
   *
   * @note This method isn't used for CheckValidity().
   * @note If an element becomes barred from constraint validation, it has to be
   * considered as valid.
   *
   * @param aElementValidityState the new validity state of the element
   */
  void UpdateValidity(bool aElementValidityState);

  /**
   * This method check the form validity and make invalid form elements send
   * invalid event if needed.
   *
   * @return Whether the form is valid.
   *
   * @note Do not call this method if novalidate/formnovalidate is used.
   * @note This method might disappear with bug 592124, hopefuly.
   * @see
   * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#interactively-validate-the-constraints
   */
  bool CheckValidFormSubmission();

  /**
   * Contruct the entry list to get their data pumped into the FormData and
   * fire a `formdata` event with the entry list in formData attribute.
   * <https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#constructing-form-data-set>
   *
   * @param aFormData the form data object
   */
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult ConstructEntryList(FormData* aFormData);

  /**
   * Whether the submission of this form has been ever prevented because of
   * being invalid.
   *
   * @return Whether the submission of this form has been prevented because of
   * being invalid.
   */
  bool HasEverTriedInvalidSubmit() const { return mEverTriedInvalidSubmit; }

  /**
   * Implements form[name]. Returns form controls in this form with the correct
   * value of the name attribute.
   */
  already_AddRefed<nsISupports> FindNamedItem(const nsAString& aName,
                                              nsWrapperCache** aCache);

  // WebIDL

  void GetAcceptCharset(DOMString& aValue) {
    GetHTMLAttr(nsGkAtoms::acceptcharset, aValue);
  }

  void SetAcceptCharset(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::acceptcharset, aValue, aRv);
  }

  void GetAction(nsString& aValue);
  void SetAction(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::action, aValue, aRv);
  }

  void GetAutocomplete(nsAString& aValue);
  void SetAutocomplete(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::autocomplete, aValue, aRv);
  }

  void GetEnctype(nsAString& aValue);
  void SetEnctype(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::enctype, aValue, aRv);
  }

  void GetEncoding(nsAString& aValue) { GetEnctype(aValue); }
  void SetEncoding(const nsAString& aValue, ErrorResult& aRv) {
    SetEnctype(aValue, aRv);
  }

  void GetMethod(nsAString& aValue);
  void SetMethod(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::method, aValue, aRv);
  }

  void GetName(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::name, aValue); }

  void SetName(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::name, aValue, aRv);
  }

  bool NoValidate() const { return GetBoolAttr(nsGkAtoms::novalidate); }

  void SetNoValidate(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::novalidate, aValue, aRv);
  }

  void GetTarget(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::target, aValue); }

  void SetTarget(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::target, aValue, aRv);
  }

  void GetRel(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::rel, aValue); }
  void SetRel(const nsAString& aRel, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::rel, aRel, aError);
  }
  nsDOMTokenList* RelList();

  // it's only out-of-line because the class definition is not available in the
  // header
  nsIHTMLCollection* Elements();

  int32_t Length();

  /**
   * Check whether submission can proceed for this form then fire submit event.
   * This basically implements steps 1-6 (more or less) of
   * <https://html.spec.whatwg.org/multipage/forms.html#concept-form-submit>.
   * @param aSubmitter If not null, is the "submitter" from that algorithm.
   *                   Therefore it must be a valid submit control.
   */
  MOZ_CAN_RUN_SCRIPT void MaybeSubmit(Element* aSubmitter);
  MOZ_CAN_RUN_SCRIPT void MaybeReset(Element* aSubmitter);
  void Submit(ErrorResult& aRv);

  /**
   * Requests to submit the form. Unlike submit(), this method includes
   * interactive constraint validation and firing a submit event,
   * either of which can cancel submission.
   *
   * @param aSubmitter The submitter argument can be used to point to a specific
   *                   submit button.
   * @param aRv        An ErrorResult.
   * @see
   * https://html.spec.whatwg.org/multipage/forms.html#dom-form-requestsubmit
   */
  MOZ_CAN_RUN_SCRIPT void RequestSubmit(nsGenericHTMLElement* aSubmitter,
                                        ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void Reset();

  bool CheckValidity() { return CheckFormValidity(nullptr); }

  bool ReportValidity() { return CheckValidFormSubmission(); }

  Element* IndexedGetter(uint32_t aIndex, bool& aFound);

  already_AddRefed<nsISupports> NamedGetter(const nsAString& aName,
                                            bool& aFound);

  void GetSupportedNames(nsTArray<nsString>& aRetval);

  static int32_t CompareFormControlPosition(Element* aElement1,
                                            Element* aElement2,
                                            const nsIContent* aForm);
#ifdef DEBUG
  static void AssertDocumentOrder(
      const nsTArray<nsGenericHTMLFormElement*>& aControls, nsIContent* aForm);
  static void AssertDocumentOrder(
      const nsTArray<RefPtr<nsGenericHTMLFormElement>>& aControls,
      nsIContent* aForm);
#endif

  JS::ExpandoAndGeneration mExpandoAndGeneration;

 protected:
  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  class RemoveElementRunnable;
  friend class RemoveElementRunnable;

  class RemoveElementRunnable : public Runnable {
   public:
    explicit RemoveElementRunnable(HTMLFormElement* aForm)
        : Runnable("dom::HTMLFormElement::RemoveElementRunnable"),
          mForm(aForm) {}

    NS_IMETHOD Run() override {
      mForm->HandleDefaultSubmitRemoval();
      return NS_OK;
    }

   private:
    RefPtr<HTMLFormElement> mForm;
  };

  nsresult DoReset();

  // Async callback to handle removal of our default submit
  void HandleDefaultSubmitRemoval();

  //
  // Submit Helpers
  //
  //
  /**
   * Attempt to submit (submission might be deferred)
   *
   * @param aPresContext the presentation context
   * @param aEvent the DOM event that was passed to us for the submit
   */
  nsresult DoSubmit(Event* aEvent = nullptr);

  /**
   * Prepare the submission object (called by DoSubmit)
   *
   * @param aFormSubmission the submission object
   * @param aEvent the DOM event that was passed to us for the submit
   */
  nsresult BuildSubmission(HTMLFormSubmission** aFormSubmission, Event* aEvent);
  /**
   * Perform the submission (called by DoSubmit and FlushPendingSubmission)
   *
   * @param aFormSubmission the submission object
   */
  nsresult SubmitSubmission(HTMLFormSubmission* aFormSubmission);

  /**
   * Submit a form[method=dialog]
   * @param aFormSubmission the submission object
   */
  nsresult SubmitDialog(DialogFormSubmission* aFormSubmission);

  /**
   * Notify any submit observers of the submit.
   *
   * @param aActionURL the URL being submitted to
   * @param aCancelSubmit out param where submit observers can specify that the
   *        submit should be cancelled.
   */
  nsresult NotifySubmitObservers(nsIURI* aActionURL, bool* aCancelSubmit,
                                 bool aEarlyNotify);

  /**
   * If this form submission is secure -> insecure, ask the user if they want
   * to continue.
   *
   * @param aActionURL the URL being submitted to
   * @param aCancelSubmit out param: will be true if the user wants to cancel
   */
  nsresult DoSecureToInsecureSubmitCheck(nsIURI* aActionURL,
                                         bool* aCancelSubmit);

  /**
   * Find form controls in this form with the correct value in the name
   * attribute.
   */
  already_AddRefed<nsISupports> DoResolveName(const nsAString& aName);

  /**
   * Check the form validity following this algorithm:
   * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#statically-validate-the-constraints
   *
   * @param aInvalidElements [out] parameter containing the list of unhandled
   * invalid controls.
   *
   * @return Whether the form is currently valid.
   */
  bool CheckFormValidity(nsTArray<RefPtr<Element>>* aInvalidElements) const;

  // Clear the mImageNameLookupTable and mImageElements.
  void Clear();

  // Insert a element into the past names map.
  void AddToPastNamesMap(const nsAString& aName, nsISupports* aChild);

  // Remove the given element from the past names map.  The element must be an
  // nsGenericHTMLFormElement or HTMLImageElement.
  void RemoveElementFromPastNamesMap(Element* aElement);

  nsresult AddElementToTableInternal(
      nsInterfaceHashtable<nsStringHashKey, nsISupports>& aTable,
      nsIContent* aChild, const nsAString& aName);

  nsresult RemoveElementFromTableInternal(
      nsInterfaceHashtable<nsStringHashKey, nsISupports>& aTable,
      nsIContent* aChild, const nsAString& aName);

 public:
  /**
   * Flush a possible pending submission. If there was a scripted submission
   * triggered by a button or image, the submission was defered. This method
   * forces the pending submission to be submitted. (happens when the handler
   * returns false or there is an action/target change in the script)
   */
  void FlushPendingSubmission();

  /**
   * Get the full URL to submit to.  Do not submit if the returned URL is null.
   *
   * @param aActionURL the full, unadulterated URL you'll be submitting to [OUT]
   * @param aOriginatingElement the originating element of the form submission
   * [IN]
   */
  nsresult GetActionURL(nsIURI** aActionURL, Element* aOriginatingElement);

  // Returns a number for this form that is unique within its owner document.
  // This is used by nsContentUtils::GenerateStateKey to identify form controls
  // that are inserted into the document by the parser.
  int32_t GetFormNumberForStateKey();

  /**
   * Called when we have been cloned and adopted, and the information of the
   * node has been changed.
   */
  void NodeInfoChanged(Document* aOldDoc) override;

 protected:
  //
  // Data members
  //
  /** The list of controls (form.elements as well as stuff not in elements) */
  RefPtr<HTMLFormControlsCollection> mControls;

  /** The pending submission object */
  UniquePtr<HTMLFormSubmission> mPendingSubmission;

  /** The target browsing context, if any. */
  RefPtr<BrowsingContext> mTargetContext;
  /** The load identifier for the pending request created for a
   * submit, used to be able to block double submits. */
  Maybe<uint64_t> mCurrentLoadId;

  /** The default submit element */
  RefPtr<nsGenericHTMLFormElement> mDefaultSubmitElement;

  /** The first submit element in mElements */
  RefPtr<nsGenericHTMLFormElement> mFirstSubmitInElements;

  /** The first submit element in mNotInElements */
  RefPtr<nsGenericHTMLFormElement> mFirstSubmitNotInElements;

  // This array holds on to all HTMLImageElement(s).
  // This is needed to properly clean up the bi-directional references
  // (both weak and strong) between the form and its HTMLImageElements.

  nsTArray<HTMLImageElement*> mImageElements;  // Holds WEAK references

  // A map from an ID or NAME attribute to the HTMLImageElement(s), this
  // hash holds strong references either to the named HTMLImageElement, or
  // to a list of named HTMLImageElement(s), in the case where this hash
  // holds on to a list of named HTMLImageElement(s) the list has weak
  // references to the HTMLImageElement.

  nsInterfaceHashtable<nsStringHashKey, nsISupports> mImageNameLookupTable;

  // A map from names to elements that were gotten by those names from this
  // form in that past.  See "past names map" in the HTML5 specification.

  nsInterfaceHashtable<nsStringHashKey, nsISupports> mPastNameLookupTable;

  /** Keep track of what the popup state was when the submit was initiated */
  PopupBlocker::PopupControlState mSubmitPopupState;

  RefPtr<nsDOMTokenList> mRelList;

  /**
   * Number of invalid and candidate for constraint validation elements in the
   * form the last time UpdateValidity has been called.
   */
  int32_t mInvalidElementsCount;

  // See GetFormNumberForStateKey.
  int32_t mFormNumber;

  /** Whether we are currently processing a submit event or not */
  bool mGeneratingSubmit;
  /** Whether we are currently processing a reset event or not */
  bool mGeneratingReset;
  /** Whether the submission is to be deferred in case a script triggers it */
  bool mDeferSubmission;
  /** Whether we notified NS_FORMSUBMIT_SUBJECT listeners already */
  bool mNotifiedObservers;
  /** If we notified the listeners early, what was the result? */
  bool mNotifiedObserversResult;
  /**
   * Whether the submission of this form has been ever prevented because of
   * being invalid.
   */
  bool mEverTriedInvalidSubmit;
  /** Whether we are constructing entry list */
  bool mIsConstructingEntryList;
  /** Whether we are firing submission event */
  bool mIsFiringSubmissionEvents;

 private:
  bool IsSubmitting() const;

  NotNull<const Encoding*> GetSubmitEncoding();

  /**
   * Fire an event when the form is removed from the DOM tree. This is now only
   * used by the password manager.
   */
  void MaybeFireFormRemoved();

  ~HTMLFormElement();
};

}  // namespace dom

}  // namespace mozilla

#endif  // mozilla_dom_HTMLFormElement_h
