/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLFormElement_h
#define mozilla_dom_HTMLFormElement_h

#include "js/friend/DOMProxy.h"  // JS::ExpandoAndGeneration
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/RadioGroupContainer.h"
#include "nsGenericHTMLElement.h"
#include "nsIFormControl.h"
#include "nsInterfaceHashtable.h"
#include "nsThreadUtils.h"

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

class HTMLFormElement final : public nsGenericHTMLElement {
  friend class HTMLFormControlsCollection;

 public:
  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLFormElement, form)

  explicit HTMLFormElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  enum { FORM_CONTROL_LIST_HASHTABLE_LENGTH = 8 };

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  int32_t IndexOfContent(nsIContent* aContent);
  nsGenericHTMLFormElement* GetDefaultSubmitElement() const;
  bool IsDefaultSubmitElement(nsGenericHTMLFormElement* aElement) const {
    return aElement == mDefaultSubmitElement;
  }

  // EventTarget
  void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

  /** Whether we already dispatched a DOMFormHasPassword event or not */
  bool mHasPendingPasswordEvent = false;
  /** Whether we already dispatched a DOMPossibleUsernameInputAdded event or not
   */
  bool mHasPendingPossibleUsernameEvent = false;

  // nsIContent
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;
  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  void WillHandleEvent(EventChainPostVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT nsresult
  PostHandleEvent(EventChainPostVisitor& aVisitor) override;

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;
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
   * Flag the form to know that a button or image triggered scripted form
   * submission. In that case the form will defer the submission until the
   * script handler returns and the return value is known.
   */
  void OnSubmitClickBegin();
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
   * @note This method might disappear with bug 592124, hopefuly.
   * @see
   * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#interactively-validate-the-constraints
   */
  MOZ_CAN_RUN_SCRIPT
  bool CheckValidFormSubmission();

  /**
   * Contruct the entry list to get their data pumped into the FormData and
   * fire a `formdata` event with the entry list in formData attribute.
   * <https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#constructing-form-data-set>
   *
   * @param aFormData the form data object
   */
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult ConstructEntryList(FormData*);

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
  HTMLFormControlsCollection* Elements();

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
  MOZ_CAN_RUN_SCRIPT void Submit(ErrorResult& aRv);

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

  MOZ_CAN_RUN_SCRIPT bool CheckValidity() { return CheckFormValidity(nullptr); }

  MOZ_CAN_RUN_SCRIPT
  bool ReportValidity() { return CheckValidFormSubmission(); }

  Element* IndexedGetter(uint32_t aIndex, bool& aFound);

  already_AddRefed<nsISupports> ResolveName(const nsAString&);
  already_AddRefed<nsISupports> NamedGetter(const nsAString& aName,
                                            bool& aFound);

  void GetSupportedNames(nsTArray<nsString>& aRetval);

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

  MOZ_CAN_RUN_SCRIPT nsresult DoReset();

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
  MOZ_CAN_RUN_SCRIPT nsresult DoSubmit(Event* aEvent = nullptr);

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
  MOZ_CAN_RUN_SCRIPT nsresult
  SubmitSubmission(HTMLFormSubmission* aFormSubmission);

  /**
   * Submit a form[method=dialog]
   * @param aFormSubmission the submission object
   */
  nsresult SubmitDialog(DialogFormSubmission* aFormSubmission);

  /**
   * Dispatch a DOMFormBeforeSubmit chrome-only event.
   *
   * @param aCancelSubmit out param where submit observers can specify that the
   *        submit should be cancelled.
   */
  MOZ_CAN_RUN_SCRIPT nsresult
  DispatchBeforeSubmitChromeOnlyEvent(bool* aCancelSubmit);

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
   * Check the form validity following this algorithm:
   * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#statically-validate-the-constraints
   *
   * @param aInvalidElements [out] parameter containing the list of unhandled
   * invalid controls.
   *
   * @return Whether the form is currently valid.
   */
  MOZ_CAN_RUN_SCRIPT bool CheckFormValidity(
      nsTArray<RefPtr<Element>>* aInvalidElements) const;

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
  MOZ_CAN_RUN_SCRIPT void FlushPendingSubmission();

  /**
   * Get the full URL to submit to.  Do not submit if the returned URL is null.
   *
   * @param aActionURL the full, unadulterated URL you'll be submitting to [OUT]
   * @param aOriginatingElement the originating element of the form submission
   * [IN]
   */
  nsresult GetActionURL(nsIURI** aActionURL, Element* aOriginatingElement);

  // Get the target to submit to. This is either the submitter's |formtarget| or
  // the form's |target| (Including <base>).
  void GetSubmissionTarget(nsGenericHTMLElement* aSubmitter,
                           nsAString& aTarget);

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

  // Holds WEAK references
  TreeOrderedArray<HTMLImageElement*> mImageElements;

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

  void SetDefaultSubmitElement(nsGenericHTMLFormElement*);

  NotNull<const Encoding*> GetSubmitEncoding();

  /**
   * Fire an event when the form is removed from the DOM tree. This is now only
   * used by the password manager and formautofill.
   */
  void MaybeFireFormRemoved();

  MOZ_CAN_RUN_SCRIPT
  void ReportInvalidUnfocusableElements(
      const nsTArray<RefPtr<Element>>&& aInvalidElements);

  ~HTMLFormElement();
};

}  // namespace dom

}  // namespace mozilla

#endif  // mozilla_dom_HTMLFormElement_h
