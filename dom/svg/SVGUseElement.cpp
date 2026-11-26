/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGUseElement.h"

#include "SVGGeometryProperty.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUseFrame.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_svg.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/SVGGraphicsElement.h"
#include "mozilla/dom/SVGLengthBinding.h"
#include "mozilla/dom/SVGSwitchElement.h"
#include "mozilla/dom/SVGSymbolElement.h"
#include "mozilla/dom/SVGUseElementBinding.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIReferrerInfo.h"
#include "nsIURI.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(Use)

namespace mozilla::dom {

JSObject* SVGUseElement::WrapNode(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return SVGUseElement_Binding::Wrap(aCx, this, aGivenProto);
}

////////////////////////////////////////////////////////////////////////
// implementation

SVGElement::LengthInfo SVGUseElement::sLengthInfo[4] = {
    {nsGkAtoms::x, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::X},
    {nsGkAtoms::y, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::Y},
    {nsGkAtoms::width, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::X},
    {nsGkAtoms::height, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::Y},
};

SVGElement::StringInfo SVGUseElement::sStringInfo[2] = {
    {nsGkAtoms::href, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_XLink, true}};

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_CYCLE_COLLECTION_CLASS(SVGUseElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SVGUseElement,
                                                SVGUseElementBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOriginal)
  tmp->UnlinkSource();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SVGUseElement,
                                                  SVGUseElementBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOriginal)
  tmp->mReferencedElementTracker.Traverse(&cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(SVGUseElement, SVGUseElementBase,
                                             nsIMutationObserver)

//----------------------------------------------------------------------
// Implementation

SVGUseElement::SVGUseElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : SVGUseElementBase(std::move(aNodeInfo)), mReferencedElementTracker(this) {
  SetEnabledCallbacks(kCharacterDataChanged | kAttributeChanged |
                      kContentAppended | kContentInserted |
                      kContentWillBeRemoved | kNodeWillBeDestroyed);
}

SVGUseElement::~SVGUseElement() {
  UnlinkSource();
  MOZ_DIAGNOSTIC_ASSERT(!OwnerDoc()->SVGUseElementNeedsShadowTreeUpdate(*this),
                        "Dying without unbinding?");
}

namespace SVGT = SVGGeometryProperty::Tags;

//----------------------------------------------------------------------
// nsINode methods

void SVGUseElement::ProcessAttributeChange(int32_t aNamespaceID,
                                           nsAtom* aAttribute) {
  if (OwnerDoc()->CloningForSVGUse()) {
    return;
  }
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height) {
      const bool hadValidDimensions = HasValidDimensions();
      const bool isUsed = OurWidthAndHeightAreUsed();
      if (isUsed) {
        SyncWidthOrHeight(aAttribute);
      }

      if (auto* frame = GetFrame()) {
        frame->DimensionAttributeChanged(hadValidDimensions, isUsed);
      }
    }
  }

  if ((aNamespaceID == kNameSpaceID_XLink ||
       aNamespaceID == kNameSpaceID_None) &&
      aAttribute == nsGkAtoms::href) {
    // We're changing our nature, clear out the clone information.
    if (auto* frame = GetFrame()) {
      frame->HrefChanged();
    }
    UnlinkSource();
    TriggerReclone();
  }
}

void SVGUseElement::DidAnimateAttribute(int32_t aNameSpaceID,
                                        nsAtom* aAttribute) {
  ProcessAttributeChange(aNameSpaceID, aAttribute);
}

void SVGUseElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aAttribute,
                                 const nsAttrValue* aValue,
                                 const nsAttrValue* aOldValue,
                                 nsIPrincipal* aSubjectPrincipal,
                                 bool aNotify) {
  ProcessAttributeChange(aNamespaceID, aAttribute);
  return SVGUseElementBase::AfterSetAttr(aNamespaceID, aAttribute, aValue,
                                         aOldValue, aSubjectPrincipal, aNotify);
}

nsresult SVGUseElement::Clone(dom::NodeInfo* aNodeInfo,
                              nsINode** aResult) const {
  *aResult = nullptr;
  SVGUseElement* it =
      new (aNodeInfo->NodeInfoManager()) SVGUseElement(do_AddRef(aNodeInfo));

  nsCOMPtr<nsINode> kungFuDeathGrip(it);
  nsresult rv1 = it->Init();
  nsresult rv2 = const_cast<SVGUseElement*>(this)->CopyInnerTo(it);

  if (aNodeInfo->GetDocument()->CloningForSVGUse()) {
    // SVGUseElement specific portion - record who we cloned from
    it->mOriginal = const_cast<SVGUseElement*>(this);
  }

  if (NS_SUCCEEDED(rv1) && NS_SUCCEEDED(rv2)) {
    kungFuDeathGrip.swap(*aResult);
  }

  return NS_FAILED(rv1) ? rv1 : rv2;
}

nsresult SVGUseElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = SVGUseElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  TriggerReclone();
  return NS_OK;
}

void SVGUseElement::UnbindFromTree(UnbindContext& aContext) {
  SVGUseElementBase::UnbindFromTree(aContext);
  OwnerDoc()->UnscheduleSVGUseElementShadowTreeUpdate(*this);
}

already_AddRefed<DOMSVGAnimatedString> SVGUseElement::Href() {
  return mStringAttributes[HREF].IsExplicitlySet() ||
                 !mStringAttributes[XLINK_HREF].IsExplicitlySet()
             ? mStringAttributes[HREF].ToDOMAnimatedString(this)
             : mStringAttributes[XLINK_HREF].ToDOMAnimatedString(this);
}

//----------------------------------------------------------------------

already_AddRefed<DOMSVGAnimatedLength> SVGUseElement::X() {
  return mLengthAttributes[ATTR_X].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGUseElement::Y() {
  return mLengthAttributes[ATTR_Y].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGUseElement::Width() {
  return mLengthAttributes[ATTR_WIDTH].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGUseElement::Height() {
  return mLengthAttributes[ATTR_HEIGHT].ToDOMAnimatedLength(this);
}

//----------------------------------------------------------------------
// nsIMutationObserver methods

void SVGUseElement::CharacterDataChanged(nsIContent* aContent,
                                         const CharacterDataChangeInfo&) {
  if (nsContentUtils::IsInSameAnonymousTree(mReferencedElementTracker.get(),
                                            aContent)) {
    TriggerReclone();
  }
}

void SVGUseElement::AttributeChanged(Element* aElement, int32_t aNamespaceID,
                                     nsAtom* aAttribute, AttrModType,
                                     const nsAttrValue* aOldValue) {
  if (nsContentUtils::IsInSameAnonymousTree(mReferencedElementTracker.get(),
                                            aElement)) {
    TriggerReclone();
  }
}

void SVGUseElement::ContentAppended(nsIContent* aFirstNewContent,
                                    const ContentAppendInfo&) {
  // FIXME(emilio, bug 1442336): Why does this check the parent but
  // ContentInserted the child?
  if (nsContentUtils::IsInSameAnonymousTree(mReferencedElementTracker.get(),
                                            aFirstNewContent->GetParent())) {
    TriggerReclone();
  }
}

void SVGUseElement::ContentInserted(nsIContent* aChild,
                                    const ContentInsertInfo&) {
  // FIXME(emilio, bug 1442336): Why does this check the child but
  // ContentAppended the parent?
  if (nsContentUtils::IsInSameAnonymousTree(mReferencedElementTracker.get(),
                                            aChild)) {
    TriggerReclone();
  }
}

void SVGUseElement::ContentWillBeRemoved(nsIContent* aChild,
                                         const ContentRemoveInfo&) {
  if (nsContentUtils::IsInSameAnonymousTree(mReferencedElementTracker.get(),
                                            aChild)) {
    TriggerReclone();
  }
}

void SVGUseElement::NodeWillBeDestroyed(nsINode* aNode) {
  nsCOMPtr<nsIMutationObserver> kungFuDeathGrip(this);
  UnlinkSource();
}

// Returns whether this node could ever be displayed.
static bool NodeCouldBeRendered(const nsINode& aNode) {
  if (const auto* symbol = SVGSymbolElement::FromNode(aNode)) {
    return symbol->CouldBeRendered();
  }
  if (auto* svgSwitch =
          SVGSwitchElement::FromNodeOrNull(aNode.GetParentNode())) {
    if (&aNode != svgSwitch->GetActiveChild()) {
      return false;
    }
  } else if (const auto* svgGraphics = SVGGraphicsElement::FromNode(aNode)) {
    if (!svgGraphics->PassesConditionalProcessingTests()) {
      return false;
    }
  }
  return true;
}

// <svg:use> can be used (no pun intended) to trivially cause an explosion of
// clones that could potentially DoS the browser. We have a configurable limit
// to control this.
static bool IsTooMuchRecursion(uint32_t aCount) {
  switch (StaticPrefs::svg_use_element_recursive_clone_limit_enabled()) {
    case 0:
      return false;
    case 1:
      break;
    default:
      if (!XRE_IsParentProcess()) {
        return false;
      }
      break;
  }
  return aCount >= StaticPrefs::svg_use_element_recursive_clone_limit();
}

// Circular loop detection, plus detection of whether this shadow tree is
// rendered at all.
auto SVGUseElement::ScanAncestors(const Element& aTarget) const -> ScanResult {
  uint32_t count = 0;
  return ScanAncestorsInternal(aTarget, count);
}

auto SVGUseElement::ScanAncestorsInternal(const Element& aTarget,
                                          uint32_t& aCount) const
    -> ScanResult {
  if (&aTarget == this) {
    return ScanResult::CyclicReference;
  }
  if (mOriginal) {
    if (IsTooMuchRecursion(++aCount)) {
      return ScanResult::TooDeep;
    }
    auto result = mOriginal->ScanAncestorsInternal(aTarget, aCount);
    switch (result) {
      case ScanResult::TooDeep:
      case ScanResult::CyclicReference:
        return result;
      case ScanResult::Ok:
      case ScanResult::Invisible:
        break;
    }
  }

  auto result = ScanResult::Ok;
  for (nsINode* parent = GetParentOrShadowHostNode(); parent;
       parent = parent->GetParentOrShadowHostNode()) {
    if (parent == &aTarget) {
      return ScanResult::CyclicReference;
    }
    if (auto* use = SVGUseElement::FromNode(*parent)) {
      if (IsTooMuchRecursion(++aCount)) {
        return ScanResult::TooDeep;
      }
      if (mOriginal && use->mOriginal == mOriginal) {
        return ScanResult::CyclicReference;
      }
    }
    // Do we have other similar cases we can optimize out easily?
    if (!NodeCouldBeRendered(*parent)) {
      // NOTE(emilio): We can't just return here. If we're cyclic, we need to
      // know.
      result = ScanResult::Invisible;
    }
  }
  return result;
}

//----------------------------------------------------------------------

enum class ForbiddenUseNodeMode {
  Strict,
  ForeignObjectOnly,
};

enum class RootRemoved : bool { No, Yes };

static bool IsForbiddenUseNode(const nsINode& aNode,
                               ForbiddenUseNodeMode aMode) {
  if (!aNode.IsElement()) {
    return false;
  }
  const auto* svg = SVGElement::FromNode(aNode);
  if (!svg) {
    return true;
  }
  if (svg->IsAnyOfSVGElements(nsGkAtoms::foreignObject)) {
    return true;
  }
  if (aMode == ForbiddenUseNodeMode::ForeignObjectOnly) {
    return false;
  }
  return !svg->IsSVGGraphicsElement();
}

[[nodiscard]]
static RootRemoved CollectForbiddenNodes(Element& aRoot,
                                         ForbiddenUseNodeMode aMode,
                                         nsTArray<RefPtr<nsINode>>& aNodes) {
  auto iter = dom::ShadowIncludingTreeIterator(aRoot);
  while (iter) {
    nsINode* node = *iter;
    if (IsForbiddenUseNode(*node, aMode)) {
      if (node == &aRoot) {
        return RootRemoved::Yes;
      }
      aNodes.AppendElement(node);
      iter.SkipChildren();
      continue;
    }
    ++iter;
  }
  return RootRemoved::No;
}

// SVG1 restricted <use> trees to SVGGraphicsElements.
// https://www.w3.org/TR/SVG11/struct.html#UseElement:
//
//    Any ‘svg’, ‘symbol’, ‘g’, graphics element or other ‘use’ is potentially a
//    template object that can be re-used (i.e., "instanced") in the SVG
//    document via a ‘use’ element. The ‘use’ element references another element
//    and indicates that the graphical contents of that element is
//    included/drawn at that given point in the document.
//
// SVG2 doesn't have that same restriction.
// https://www.w3.org/TR/SVG2/struct.html#UseShadowTree:
//
//    Previous versions of SVG restricted the contents of the shadow tree to SVG
//    graphics elements. This specification allows any valid SVG document
//    subtree to be cloned. Cloning non-graphical content, however, will not
//    usually have any visible effect.
//
// But it's pretty ambiguous as to what the behavior should be for some
// elements, because <script> is inert, but <iframe> is not, see:
// https://github.com/w3c/svgwg/issues/876
//
// So, fairly confusing, all-in-all. So for now by default we allow
// everything-but-foreign-content same document, and are more restrictive
// cross-document.
[[nodiscard]]
static RootRemoved RemoveForbiddenNodes(Element& aRoot, bool aIsCrossDocument) {
  ForbiddenUseNodeMode mode = ForbiddenUseNodeMode::Strict;
  switch (StaticPrefs::svg_use_element_graphics_element_restrictions()) {
    case 0:
      return RootRemoved::No;
    case 1:
      mode = aIsCrossDocument ? ForbiddenUseNodeMode::Strict
                              : ForbiddenUseNodeMode::ForeignObjectOnly;
      break;
    default:
      break;
  }

  AutoTArray<RefPtr<nsINode>, 10> unsafeNodes;
  if (CollectForbiddenNodes(aRoot, mode, unsafeNodes) == RootRemoved::Yes) {
    return RootRemoved::Yes;
  }
  for (auto& unsafeNode : unsafeNodes) {
    unsafeNode->Remove();
  }
  return RootRemoved::No;
}

void SVGUseElement::UpdateShadowTree() {
  MOZ_ASSERT(IsInComposedDoc());

  if (mReferencedElementTracker.get()) {
    mReferencedElementTracker.get()->RemoveMutationObserver(this);
  }

  LookupHref();

  RefPtr<ShadowRoot> shadow = GetShadowRoot();
  if (!shadow) {
    ShadowRootInit init;
    init.mMode = ShadowRootMode::Closed;
    shadow = AttachShadowWithoutNameChecks(init, Nothing());
  }
  MOZ_ASSERT(shadow);

  auto* targetElement =
      SVGGraphicsElement::FromNodeOrNull(mReferencedElementTracker.get());
  RefPtr<Element> newElement;

  auto UpdateShadowTree = mozilla::MakeScopeExit([&]() {
    shadow->RemoveAllChildren(/* aNotify = */ true);
    if (newElement) {
      shadow->AppendChildTo(newElement, /* aNotify = */ true, IgnoreErrors());
    }
  });

  // make sure target is valid type for <use>
  if (!targetElement) {
    return;
  }

  if (ScanAncestors(*targetElement) != ScanResult::Ok) {
    return;
  }

  nsCOMPtr<nsIURI> baseURI = targetElement->GetBaseURI();
  if (!baseURI) {
    return;
  }

  {
    const bool isCrossDocument = targetElement->OwnerDoc() != OwnerDoc();

    nsNodeInfoManager* nodeInfoManager =
        isCrossDocument ? NodeInfoManager() : nullptr;

    nsCOMPtr<nsINode> newNode =
        targetElement->Clone(true, nodeInfoManager, IgnoreErrors());
    if (!newNode) {
      return;
    }

    MOZ_ASSERT(newNode->IsElement());
    newElement = newNode.forget().downcast<Element>();
    if (RemoveForbiddenNodes(*newElement, isCrossDocument) ==
        RootRemoved::Yes) {
      newElement = nullptr;
      return;
    }
  }

  if (newElement->IsAnyOfSVGElements(nsGkAtoms::svg, nsGkAtoms::symbol)) {
    auto* newSVGElement = static_cast<SVGElement*>(newElement.get());
    if (mLengthAttributes[ATTR_WIDTH].IsExplicitlySet())
      newSVGElement->SetLength(nsGkAtoms::width, mLengthAttributes[ATTR_WIDTH]);
    if (mLengthAttributes[ATTR_HEIGHT].IsExplicitlySet())
      newSVGElement->SetLength(nsGkAtoms::height,
                               mLengthAttributes[ATTR_HEIGHT]);
  }

  // Bug 1415044 the specs do not say which referrer information we should use.
  // This may change if there's any spec comes out.
  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*this);
  mContentURLData = new URLExtraData(baseURI.forget(), referrerInfo.forget(),
                                     do_AddRef(NodePrincipal()));

  targetElement->AddMutationObserver(this);
}

Document* SVGUseElement::GetSourceDocument() const {
  nsIContent* targetElement = mReferencedElementTracker.get();
  return targetElement ? targetElement->OwnerDoc() : nullptr;
}

nsIURI* SVGUseElement::GetSourceDocURI() const {
  if (auto* doc = GetSourceDocument()) {
    return doc->GetDocumentURI();
  }
  return nullptr;
}

const Encoding* SVGUseElement::GetSourceDocCharacterSet() const {
  if (auto* doc = GetSourceDocument()) {
    return doc->GetDocumentCharacterSet();
  }
  return nullptr;
}

static nsINode* GetClonedChild(const SVGUseElement& aUseElement) {
  const ShadowRoot* shadow = aUseElement.GetShadowRoot();
  return shadow ? shadow->GetFirstChild() : nullptr;
}

bool SVGUseElement::OurWidthAndHeightAreUsed() const {
  nsINode* clonedChild = GetClonedChild(*this);
  return clonedChild &&
         clonedChild->IsAnyOfSVGElements(nsGkAtoms::svg, nsGkAtoms::symbol);
}

//----------------------------------------------------------------------
// implementation helpers

void SVGUseElement::SyncWidthOrHeight(nsAtom* aName) {
  NS_ASSERTION(aName == nsGkAtoms::width || aName == nsGkAtoms::height,
               "The clue is in the function name");
  NS_ASSERTION(OurWidthAndHeightAreUsed(), "Don't call this");

  if (!OurWidthAndHeightAreUsed()) {
    return;
  }

  auto* target = SVGElement::FromNode(GetClonedChild(*this));
  uint32_t index =
      sLengthInfo[ATTR_WIDTH].mName == aName ? ATTR_WIDTH : ATTR_HEIGHT;

  if (mLengthAttributes[index].IsExplicitlySet()) {
    target->SetLength(aName, mLengthAttributes[index]);
    return;
  }
  if (target->IsSVGElement(nsGkAtoms::svg)) {
    // Our width/height attribute is now no longer explicitly set, so we
    // need to revert the clone's width/height to the width/height of the
    // content that's being cloned.
    TriggerReclone();
    return;
  }
  // Our width/height attribute is now no longer explicitly set, so we
  // need to set the value to 100%
  SVGAnimatedLength length;
  length.Init(SVGLength::Axis::XY, 0xff, 100,
              SVGLength_Binding::SVG_LENGTHTYPE_PERCENTAGE);
  target->SetLength(aName, length);
}

void SVGUseElement::LookupHref() {
  nsAutoString href;
  if (mStringAttributes[HREF].IsExplicitlySet()) {
    mStringAttributes[HREF].GetAnimValue(href, this);
  } else {
    mStringAttributes[XLINK_HREF].GetAnimValue(href, this);
  }

  if (href.IsEmpty()) {
    return;
  }

  const RefPtr<Element> treeToWatch = mOriginal ? mOriginal.get() : this;
  if (nsContentUtils::IsLocalRefURL(href)) {
    mReferencedElementTracker.ResetToLocalFragmentID(*treeToWatch, href);
    return;
  }

  nsCOMPtr<nsIURI> baseURI = treeToWatch->GetBaseURI();
  nsCOMPtr<nsIURI> targetURI;
  nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(targetURI), href,
                                            GetComposedDoc(), baseURI);
  if (!targetURI) {
    return;
  }

  // Don't allow <use href="data:...">. Using "#ref" inside a data: document is
  // handled above.
  if (targetURI->SchemeIs("data")) {
    return;
  }

  const nsCOMPtr<nsIReferrerInfo> referrer =
      OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();
  mReferencedElementTracker.ResetToURIWithFragmentID(*treeToWatch, targetURI,
                                                     referrer);
}

void SVGUseElement::TriggerReclone() {
  if (Document* doc = GetComposedDoc()) {
    doc->ScheduleSVGUseElementShadowTreeUpdate(*this);
  }
}

void SVGUseElement::UnlinkSource() {
  if (mReferencedElementTracker.get()) {
    mReferencedElementTracker.get()->RemoveMutationObserver(this);
  }
  mReferencedElementTracker.Unlink();
}

//----------------------------------------------------------------------
// SVGElement methods

/* virtual */
gfxMatrix SVGUseElement::ChildToUserSpaceTransform() const {
  float x, y;
  if (!SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y>(this, &x, &y)) {
    const_cast<SVGUseElement*>(this)->GetAnimatedLengthValues(&x, &y, nullptr);
  }
  return gfxMatrix::Translation(x, y);
}

/* virtual */
bool SVGUseElement::HasValidDimensions() const {
  if (!OurWidthAndHeightAreUsed()) {
    return true;
  }

  return (!mLengthAttributes[ATTR_WIDTH].IsExplicitlySet() ||
          mLengthAttributes[ATTR_WIDTH].GetAnimValInSpecifiedUnits() > 0) &&
         (!mLengthAttributes[ATTR_HEIGHT].IsExplicitlySet() ||
          mLengthAttributes[ATTR_HEIGHT].GetAnimValInSpecifiedUnits() > 0);
}

SVGElement::LengthAttributesInfo SVGUseElement::GetLengthInfo() {
  return LengthAttributesInfo(mLengthAttributes, sLengthInfo,
                              std::size(sLengthInfo));
}

SVGElement::StringAttributesInfo SVGUseElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

SVGUseFrame* SVGUseElement::GetFrame() const {
  nsIFrame* frame = GetPrimaryFrame();
  // We might be a plain SVGContainerFrame if we didn't pass the conditional
  // processing checks.
  if (!frame || !frame->IsSVGUseFrame()) {
    MOZ_ASSERT_IF(frame, frame->Type() == LayoutFrameType::None);
    return nullptr;
  }
  return static_cast<SVGUseFrame*>(frame);
}

//----------------------------------------------------------------------
// nsIContent methods

bool SVGUseElement::IsNoNamespaceAttrMapped(const nsAtom* name) const {
  return name == nsGkAtoms::x || name == nsGkAtoms::y ||
         SVGUseElementBase::IsNoNamespaceAttrMapped(name);
}

NonCustomCSSPropertyId SVGUseElement::GetCSSPropertyIdForAttrEnum(
    uint8_t aAttrEnum) {
  switch (aAttrEnum) {
    case ATTR_X:
      return eCSSProperty_x;
    case ATTR_Y:
      return eCSSProperty_y;
    default:
      // Currently we don't map width or height to style
      return eCSSProperty_UNKNOWN;
  }
}

}  // namespace mozilla::dom
