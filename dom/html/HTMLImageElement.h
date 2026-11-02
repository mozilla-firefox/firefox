/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLImageElement_h
#define mozilla_dom_HTMLImageElement_h

#include "Units.h"
#include "mozilla/Attributes.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGenericHTMLElement.h"
#include "nsImageLoadingContent.h"

namespace mozilla {
class EventChainPreVisitor;
namespace dom {

class ResponsiveImageSelector;
class HTMLImageElement final : public nsGenericHTMLElement,
                               public nsImageLoadingContent {
  friend class HTMLSourceElement;
  friend class HTMLPictureElement;

 public:
  explicit HTMLImageElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  static already_AddRefed<HTMLImageElement> Image(
      const GlobalObject& aGlobal, const Optional<uint32_t>& aWidth,
      const Optional<uint32_t>& aHeight, ErrorResult& aError);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLImageElement,
                                           nsGenericHTMLElement)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_ADDSIZEOFEXCLUDINGTHIS

  bool Draggable() const override;

  void MaybeRecomputeAutoSizes(bool aQueueImageTask);

  ResponsiveImageSelector* GetResponsiveImageSelector() const {
    return mResponsiveSelector.get();
  }

  // Element
  bool IsInteractiveHTMLContent() const override;

  // EventTarget
  void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLImageElement, img)

  // override from nsImageLoadingContent
  CORSMode GetCORSMode() override;

  // nsIContent
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;
  nsChangeHint GetAttributeChangeHint(const nsAtom* aAttribute,
                                      AttrModType aModType) const override;
  bool IsNoNamespaceAttrMapped(const nsAtom* aAttribute) const override;
  nsMapRuleToAttributesFunc GetAttributeMappingFunction() const override;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  nsINode* GetScopeChainParent() const override;

  bool IsHTMLFocusable(IsFocusableFlags, bool* aIsFocusable,
                       int32_t* aTabIndex) override;

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;

  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  void NodeInfoChanged(Document* aOldDoc) override;
  nsresult CopyInnerTo(HTMLImageElement* aDest);

  bool IsMap() { return GetBoolAttr(nsGkAtoms::ismap); }
  void SetIsMap(bool aIsMap, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::ismap, aIsMap, aError);
  }
  MOZ_CAN_RUN_SCRIPT uint32_t Width();
  void SetWidth(uint32_t aWidth, ErrorResult& aError) {
    SetUnsignedIntAttr(nsGkAtoms::width, aWidth, 0, aError);
  }
  MOZ_CAN_RUN_SCRIPT uint32_t Height();
  void SetHeight(uint32_t aHeight, ErrorResult& aError) {
    SetUnsignedIntAttr(nsGkAtoms::height, aHeight, 0, aError);
  }

  uint32_t NaturalHeight() { return NaturalSize().height; }
  uint32_t NaturalWidth() { return NaturalSize().width; }

  bool Complete() const;
  uint32_t Hspace() {
    return GetDimensionAttrAsUnsignedInt(nsGkAtoms::hspace, 0);
  }
  void SetHspace(uint32_t aHspace, ErrorResult& aError) {
    SetUnsignedIntAttr(nsGkAtoms::hspace, aHspace, 0, aError);
  }
  uint32_t Vspace() {
    return GetDimensionAttrAsUnsignedInt(nsGkAtoms::vspace, 0);
  }
  void SetVspace(uint32_t aVspace, ErrorResult& aError) {
    SetUnsignedIntAttr(nsGkAtoms::vspace, aVspace, 0, aError);
  }

  void GetAlt(nsAString& aAlt) { GetHTMLAttr(nsGkAtoms::alt, aAlt); }
  void SetAlt(const nsAString& aAlt, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::alt, aAlt, aError);
  }
  void GetSrc(nsAString& aSrc) { GetURIAttr(nsGkAtoms::src, nullptr, aSrc); }
  void SetSrc(const nsAString& aSrc, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::src, aSrc, aError);
  }
  void SetSrc(const nsAString& aSrc, nsIPrincipal* aTriggeringPrincipal,
              ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::src, aSrc, aTriggeringPrincipal, aError);
  }
  void GetSrcset(nsAString& aSrcset) {
    GetHTMLAttr(nsGkAtoms::srcset, aSrcset);
  }
  void SetSrcset(const nsAString& aSrcset, nsIPrincipal* aTriggeringPrincipal,
                 ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::srcset, aSrcset, aTriggeringPrincipal, aError);
  }
  void GetCrossOrigin(nsAString& aResult) {
    // Null for both missing and invalid defaults is ok, since we
    // always parse to an enum value, so we don't need an invalid
    // default, and we _want_ the missing default to be null.
    GetEnumAttr(nsGkAtoms::crossorigin, nullptr, aResult);
  }
  void SetCrossOrigin(const nsAString& aCrossOrigin, ErrorResult& aError) {
    SetOrRemoveNullableStringAttr(nsGkAtoms::crossorigin, aCrossOrigin, aError);
  }
  void GetUseMap(nsAString& aUseMap) {
    GetHTMLAttr(nsGkAtoms::usemap, aUseMap);
  }
  void SetUseMap(const nsAString& aUseMap, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::usemap, aUseMap, aError);
  }
  void GetName(nsAString& aName) { GetHTMLAttr(nsGkAtoms::name, aName); }
  void SetName(const nsAString& aName, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::name, aName, aError);
  }
  void GetAlign(nsAString& aAlign) { GetHTMLAttr(nsGkAtoms::align, aAlign); }
  void SetAlign(const nsAString& aAlign, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::align, aAlign, aError);
  }
  void GetLongDesc(nsAString& aLongDesc) {
    GetURIAttr(nsGkAtoms::longdesc, nullptr, aLongDesc);
  }
  void SetLongDesc(const nsAString& aLongDesc, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::longdesc, aLongDesc, aError);
  }
  void GetSizes(nsAString& aSizes) { GetHTMLAttr(nsGkAtoms::sizes, aSizes); }
  void SetSizes(const nsAString& aSizes, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::sizes, aSizes, aError);
  }
  void GetCurrentSrc(nsAString& aValue);
  void GetBorder(nsAString& aBorder) {
    GetHTMLAttr(nsGkAtoms::border, aBorder);
  }
  void SetBorder(const nsAString& aBorder, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::border, aBorder, aError);
  }
  void SetReferrerPolicy(const nsAString& aReferrer, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::referrerpolicy, aReferrer, aError);
  }
  void GetReferrerPolicy(nsAString& aReferrer) {
    GetEnumAttr(nsGkAtoms::referrerpolicy, "", aReferrer);
  }
  void SetDecoding(const nsAString& aDecoding, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::decoding, aDecoding, aError);
  }
  void GetDecoding(nsAString& aValue);

  void SetLoading(const nsAString& aLoading, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::loading, aLoading, aError);
  }

  bool IsAwaitingLoadOrLazyLoading() const {
    return mLazyLoading ||
           (mImageLoadData && mImageLoadData->mPendingImageLoadTask);
  }

  bool IsLazyLoading() const { return mLazyLoading; }

  already_AddRefed<Promise> Decode(ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT int32_t X();
  MOZ_CAN_RUN_SCRIPT int32_t Y();
  void GetLowsrc(nsAString& aLowsrc) {
    GetURIAttr(nsGkAtoms::lowsrc, nullptr, aLowsrc);
  }
  void SetLowsrc(const nsAString& aLowsrc, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::lowsrc, aLowsrc, aError);
  }

  HTMLFormElement* GetFormInternal() const { return mForm; }
  void SetForm(HTMLFormElement* aForm);
  void ClearForm(bool aRemoveFromForm);

  void DestroyContent() override;

  void MediaFeatureValuesChanged();

  /**
   * Given a hypothetical <img> or <source> tag with the given parameters,
   * return what URI we would attempt to use, if any.  Used by the preloader to
   * resolve sources prior to DOM creation.
   *
   * @param aDocument The document this image would be for, for referencing
   *        viewport width and DPI/zoom
   * @param aIsSourceTag If these parameters are for a <source> tag (as in a
   *        <picture>) rather than an <img> tag. Note that some attrs are unused
   *        when this is true an vice versa
   * @param aSrcAttr [ignored if aIsSourceTag] The src attr for this image.
   * @param aSrcsetAttr The srcset attr for this image/source
   * @param aSizesAttr The sizes attr for this image/source
   * @param aTypeAttr [ignored if !aIsSourceTag] The type attr for this source.
   *                  Should be a void string to differentiate no type attribute
   *                  from an empty one.
   * @param aMediaAttr [ignored if !aIsSourceTag] The media attr for this
   *                   source.  Should be a void string to differentiate no
   *                   media attribute from an empty one.
   * @param aResult A reference to store the resulting URL spec in if we
   *                selected a source.  This value is not guaranteed to parse to
   *                a valid URL, merely the URL that the tag would attempt to
   *                resolve and load (which may be the empty string).  This
   *                parameter is not modified if return value is false.
   * @return True if we were able to select a final source, false if further
   *         sources would be considered.  It follows that this always returns
   *         true if !aIsSourceTag.
   *
   * Note that the return value may be true with an empty string as the result,
   * which implies that the parameters provided describe a tag that would select
   * no source.  This is distinct from a return of false which implies that
   * further <source> or <img> tags would be considered.
   */
  static bool SelectSourceForTagWithAttrs(
      Document* aDocument, bool aIsSourceTag, const nsAString& aSrcAttr,
      const nsAString& aSrcsetAttr, const nsAString& aSizesAttr,
      const nsAString& aTypeAttr, const nsAString& aMediaAttr,
      nsAString& aResult);

  enum class StartLoad : bool { No, Yes };
  void StopLazyLoading(StartLoad);

  // This is used when restyling, for retrieving the extra style from the source
  // element.
  const StyleLockedDeclarationBlock* GetMappedAttributesFromSource() const;

  FetchPriority GetFetchPriorityForImage() const override;

  /**
   * Whether we are lazy loaded with sizes=auto
   * https://html.spec.whatwg.org/#allows-auto-sizes
   */
  bool AllowsAutoSizes() const;

 protected:
  virtual ~HTMLImageElement();

  // Update the responsive source synchronously and queues a task to run
  // LoadSelectedImage pending stable state.
  void UpdateSourceSyncAndQueueImageTask(
      bool aAlwaysLoad, bool aNotify,
      const HTMLSourceElement* aSkippedSource = nullptr);

  // True if we have a srcset attribute or a <picture> parent, regardless of if
  // any valid responsive sources were parsed from either.
  bool HaveSrcsetOrInPicture() const;

  // True if the given URL equals the last URL that was loaded by this element.
  bool SelectedSourceMatchesLast(nsIURI* aSelectedSource);

  // Load the current mResponsiveSelector (responsive mode) or src attr image.
  // Note: This doesn't run the full selection for the responsive selector.
  void LoadSelectedImage(bool aAlwaysLoad, bool aStopLazyLoading) override;

  // True if this string represents a type we would support on <source type>
  static bool SupportedPictureSourceType(const nsAString& aType);

  // Update/create/destroy mResponsiveSelector
  void PictureSourceSrcsetChanged(nsIContent* aSourceNode,
                                  const nsAString& aNewValue, bool aNotify);
  void PictureSourceSizesChanged(nsIContent* aSourceNode,
                                 const nsAString& aNewValue, bool aNotify);
  // As we re-run the source selection on these mutations regardless,
  // we don't actually care which changed or to what
  void PictureSourceMediaOrTypeChanged(nsIContent* aSourceNode, bool aNotify);

  // This is called when we update "width" or "height" attribute of source
  // element.
  void PictureSourceDimensionChanged(HTMLSourceElement* aSourceNode,
                                     bool aNotify);

  void PictureSourceAdded(bool aNotify,
                          HTMLSourceElement* aSourceNode = nullptr);
  // This should be called prior to the unbind, such that nextsibling works
  void PictureSourceRemoved(bool aNotify,
                            HTMLSourceElement* aSourceNode = nullptr);

  // Re-evaluates all source nodes (picture <source>,<img>) and finds
  // the best source set for mResponsiveSelector. If a better source
  // is found, creates a new selector and feeds the source to it. If
  // the current ResponsiveSelector is not changed, runs
  // SelectImage(true) to re-evaluate its candidates.
  //
  // Because keeping the existing selector is the common case (and we
  // often do no-op reselections), this does not re-parse values for
  // the existing mResponsiveSelector, meaning you need to update its
  // parameters as appropriate before calling (or null it out to force
  // recreation)
  //
  // if |aSkippedSource| is non-null, we will skip it when running the
  // algorithm. This is used when we need to update the source when we are
  // removing the source element.
  //
  // Returns true if the source has changed, and false otherwise.
  bool UpdateResponsiveSource(
      const HTMLSourceElement* aSkippedSource = nullptr);

  // Given a <source> node that is a previous sibling *or* ourselves, try to
  // create a ResponsiveSelector.

  // If the node's srcset/sizes make for an invalid selector, returns
  // nullptr. This does not guarantee the resulting selector matches an image,
  // only that it is valid.
  already_AddRefed<ResponsiveImageSelector> TryCreateResponsiveSelector(
      Element* aSourceElement);

  MOZ_CAN_RUN_SCRIPT CSSIntPoint GetXY();
  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;
  void UpdateFormOwner();

  void BeforeSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;
  void OnAttrSetButNotChanged(int32_t aNamespaceID, nsAtom* aName,
                              const nsAttrValueOrString& aValue,
                              bool aNotify) override;

  // Override for nsImageLoadingContent.
  nsIContent* AsContent() override { return this; }

  // Created when we're tracking responsive image state
  RefPtr<ResponsiveImageSelector> mResponsiveSelector;

  // This is a strong reference that this element and the HTMLFormElement
  // cooperate in maintaining.
  RefPtr<HTMLFormElement> mForm;

 private:
  bool SourceElementMatches(Element* aSourceElement);

  // Start or stop observing for resizes when sizes=auto
  void UpdateAutoSizeObserver();

  static void MapAttributesIntoRule(MappedDeclarationsBuilder&);
  /**
   * This function is called by AfterSetAttr and OnAttrSetButNotChanged.
   * It will not be called if the value is being unset.
   *
   * @param aNamespaceID the namespace of the attr being set
   * @param aName the localname of the attribute being set
   * @param aValue the value it's being set to represented as either a string or
   *        a parsed nsAttrValue.
   * @param aOldValue the value previously set. Will be null if no value was
   *        previously set. This value should only be used when
   *        aValueMaybeChanged is true; when aValueMaybeChanged is false,
   *        aOldValue should be considered unreliable.
   * @param aNotify Whether we plan to notify document observers.
   */
  void AfterMaybeChangeAttr(int32_t aNamespaceID, nsAtom* aName,
                            const nsAttrValueOrString& aValue,
                            const nsAttrValue* aOldValue,
                            nsIPrincipal* aMaybeScriptedPrincipal,
                            bool aNotify);

  // Set this image as a lazy load image due to loading="lazy".
  void SetLazyLoading();

  bool IsInPicture() const {
    return mParent && mParent->IsHTMLElement(nsGkAtoms::picture);
  }

  void InvalidateAttributeMapping();

  void SetResponsiveSelector(RefPtr<ResponsiveImageSelector>&& aSource);
  void SetDensity(double aDensity);

  nsCOMPtr<nsIURI> mSrcURI;
  nsCOMPtr<nsIPrincipal> mSrcTriggeringPrincipal;
  nsCOMPtr<nsIPrincipal> mSrcsetTriggeringPrincipal;

  // Last URL that was attempted to load by this element.
  nsCOMPtr<nsIURI> mLastSelectedSource;
  // Last pixel density that was selected.
  double mCurrentDensity = 1.0;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_HTMLImageElement_h */
