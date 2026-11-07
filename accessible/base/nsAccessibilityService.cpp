/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAccessibilityService.h"

// NOTE: alphabetically ordered
#include "ARIAGridAccessible.h"
#include "ARIAMap.h"
#include "ApplicationAccessibleWrap.h"
#include "DocAccessible-inl.h"
#include "DocAccessibleChild.h"
#include "FocusManager.h"
#include "HTMLCanvasAccessible.h"
#include "HTMLElementAccessibles.h"
#include "HTMLImageMapAccessible.h"
#include "HTMLLinkAccessible.h"
#include "HTMLListAccessible.h"
#include "HTMLSelectAccessible.h"
#include "HTMLTableAccessible.h"
#include "HyperTextAccessible.h"
#include "OuterDocAccessible.h"
#include "Pivot.h"
#include "RootAccessible.h"
#include "mozilla/FocusModel.h"
#include "mozilla/a11y/Role.h"
#include "nsAccUtils.h"
#include "nsArrayUtils.h"
#include "nsAttrName.h"
#include "nsCRT.h"
#include "nsCoreUtils.h"
#include "nsDOMTokenList.h"
#include "nsEventShell.h"
#include "nsGkAtoms.h"
#include "nsIAccessibleAnnouncementEvent.h"
#include "nsIFrameInlines.h"
#include "nsServiceManagerUtils.h"
#include "nsTextFormatter.h"
#ifdef MOZ_ACCESSIBILITY_ATK
#  include "RootAccessibleWrap.h"
#endif
#include "States.h"
#include "TextLeafAccessible.h"
#include "xpcAccessibleApplication.h"

#ifdef XP_WIN
#  include "mozilla/StaticPtr.h"
#  include "mozilla/a11y/Compatibility.h"
#endif

#ifdef A11Y_LOG
#  include "Logging.h"
#endif

#include "XULAlertAccessible.h"
#include "XULComboboxAccessible.h"
#include "XULElementAccessibles.h"
#include "XULFormControlAccessible.h"
#include "XULListboxAccessible.h"
#include "XULMenuAccessible.h"
#include "XULTabAccessible.h"
#include "XULTreeGridAccessible.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Services.h"
#include "mozilla/a11y/AccTypes.h"
#include "mozilla/dom/ARIANotifyMixinBinding.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/HTMLTableElement.h"
#include "nsExceptionHandler.h"
#include "nsIObserverService.h"
#include "nsImageFrame.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsTreeBodyFrame.h"
#include "nsTreeUtils.h"

using namespace mozilla;
using namespace mozilla::a11y;
using namespace mozilla::dom;

/**
 * Accessibility service force enable/disable preference.
 * Supported values:
 *   Accessibility is force enabled (accessibility should always be enabled): -1
 *   Accessibility is enabled (will be started upon a request, default value): 0
 *   Accessibility is force disabled (never enable accessibility):             1
 */
#define PREF_ACCESSIBILITY_FORCE_DISABLED "accessibility.force_disabled"

////////////////////////////////////////////////////////////////////////////////
// Statics
////////////////////////////////////////////////////////////////////////////////

/**
 * If the element has an ARIA attribute that requires a specific Accessible
 * class, create and return it. Otherwise, return null.
 */
static already_AddRefed<LocalAccessible> MaybeCreateSpecificARIAAccessible(
    const nsRoleMapEntry* aRoleMapEntry, const LocalAccessible* aContext,
    nsIContent* aContent, DocAccessible* aDocument) {
  if (aRoleMapEntry && aRoleMapEntry->accTypes & eTableCell) {
    if (aContent->IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th) &&
        aContext->IsHTMLTableRow()) {
      // Don't use ARIAGridCellAccessible for a valid td/th because
      // HTMLTableCellAccessible can provide additional info; e.g. row/col span
      // from the layout engine.
      return nullptr;
    }
    // A cell must be in a row.
    const Accessible* parent = aContext;
    if (parent->IsGeneric()) {
      parent = parent->GetNonGenericParent();
    }
    if (!parent || parent->Role() != roles::ROW) {
      return nullptr;
    }
    // That row must be in a table, though there may be an intervening rowgroup.
    parent = parent->GetNonGenericParent();
    if (!parent) {
      return nullptr;
    }
    if (!parent->IsTable() && parent->Role() == roles::ROWGROUP) {
      parent = parent->GetNonGenericParent();
      if (!parent) {
        return nullptr;
      }
    }
    if (parent->IsTable() && !parent->IsCustomTable()) {
      return MakeAndAddRef<ARIAGridCellAccessible>(aContent, aDocument);
    }
  }
  return nullptr;
}

// Send a request to all content processes that they build and send back
// information about the given cache domains.
static bool SendCacheDomainRequestToAllContentProcesses(
    uint64_t aCacheDomains) {
  if (!XRE_IsParentProcess()) {
    return false;
  }
  bool sentAll = true;
  nsTArray<ContentParent*> contentParents;
  ContentParent::GetAll(contentParents);
  for (auto* parent : contentParents) {
    sentAll = sentAll && parent->SendSetCacheDomains(aCacheDomains);
  }
  return sentAll;
}

/**
 * Return true if the element must be a generic Accessible, even if it has been
 * marked presentational with role="presentation", etc. MustBeAccessible causes
 * an Accessible to be created as if it weren't marked presentational at all;
 * e.g. <table role="presentation" tabindex="0"> will expose roles::TABLE and
 * support TableAccessible. In contrast, this function causes a generic
 * Accessible to be created; e.g. <table role="presentation" style="position:
 * fixed;"> will expose roles::TEXT_CONTAINER and will not support
 * TableAccessible. This is necessary in certain cases for the
 * RemoteAccessible cache.
 */
static bool MustBeGenericAccessible(nsIContent* aContent) {
  if (aContent->IsInNativeAnonymousSubtree() || aContent->IsSVGElement()) {
    // We should not force create accs for anonymous content.
    // This is an issue for inputs, which have an intermediate
    // container with relevant overflow styling between the input
    // and its internal input content.
    // We should also avoid this for SVG elements (ie. `<foreignobject>`s
    // which have default overflow:hidden styling).
    return false;
  }
  nsIFrame* frame = aContent->GetPrimaryFrame();
  MOZ_ASSERT(frame);
  // If the frame has been transformed, and the content has any children, we
  // should create an Accessible so that we can account for the transform when
  // calculating the Accessible's bounds using the parent process cache.
  // Ditto for content which is position: fixed or sticky or has overflow
  // styling (auto, scroll, hidden, or any multi-axis combination).
  // However, don't do this for XUL widgets, as this breaks XUL a11y code
  // expectations in some cases. XUL widgets are only used in the parent
  // process and can't be cached anyway.
  return !aContent->IsXULElement() &&
         ((aContent->HasChildren() && frame->IsTransformed()) ||
          frame->IsStickyPositioned() ||
          (frame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
           nsLayoutUtils::IsReallyFixedPos(frame)) ||
          frame->StyleDisplay()->IsScrollableOverflow());
}

/**
 * Return true if the element must be accessible.
 */
static bool MustBeAccessible(nsIContent* aContent, DocAccessible* aDocument) {
  if (nsIFrame* frame = aContent->GetPrimaryFrame()) {
    // This document might be invisible when it first loads. Therefore, we must
    // check focusability irrespective of visibility here. Otherwise, we might
    // not create Accessibles for some focusable elements; e.g. a span with only
    // a tabindex. Elements that are invisible within this document are excluded
    // earlier in CreateAccessible.
    if (frame->IsFocusable(IsFocusableFlags::IgnoreVisibility)) {
      return true;
    }
  }

  // Return true if the element has an attribute (ARIA, title, or relation) that
  // requires the creation of an Accessible for the element.
  if (aContent->IsElement()) {
    uint32_t attrCount = aContent->AsElement()->GetAttrCount();
    for (uint32_t attrIdx = 0; attrIdx < attrCount; attrIdx++) {
      const nsAttrName* attr = aContent->AsElement()->GetAttrNameAt(attrIdx);
      if (attr->NamespaceEquals(kNameSpaceID_None)) {
        nsAtom* attrAtom = attr->Atom();
        if (attrAtom == nsGkAtoms::title && aContent->IsHTMLElement()) {
          // If the author provided a title on an element that would not
          // be accessible normally, assume an intent and make it accessible.
          return true;
        }

        nsDependentAtomString attrStr(attrAtom);
        if (!StringBeginsWith(attrStr, u"aria-"_ns)) continue;  // not ARIA

        // A global state or a property and in case of token defined.
        uint8_t attrFlags = aria::AttrCharacteristicsFor(attrAtom);
        if ((attrFlags & ATTR_GLOBAL) &&
            (!(attrFlags & ATTR_VALTOKEN) ||
             nsAccUtils::HasDefinedARIAToken(aContent, attrAtom))) {
          return true;
        }
      }
    }

    // If the given ID is referred by relation attribute then create an
    // Accessible for it.
    if (nsAtom* id = aContent->GetID()) {
      return aDocument->IsDependentID(aContent->AsElement(), id);
    }
  }

  return false;
}

bool nsAccessibilityService::ShouldCreateImgAccessible(
    mozilla::dom::Element* aElement, DocAccessible* aDocument) {
  // The element must have a layout frame for us to proceed. If there is no
  // frame, the image is likely hidden.
  nsIFrame* frame = aElement->GetPrimaryFrame();
  if (!frame) {
    return false;
  }

  // If the element is not an img, not an embedded image via embed or object,
  // and not a pseudo-element with non-empty CSS content alt text, then we
  // should not create an accessible.
  if (!aElement->IsHTMLElement(nsGkAtoms::img) &&
      ((!aElement->IsHTMLElement(nsGkAtoms::embed) &&
        !aElement->IsHTMLElement(nsGkAtoms::object)) ||
       frame->AccessibleType() != AccType::eImageType) &&
      nsCoreUtils::GetCssAltContent(aElement).valueOr(u""_ns).IsEmpty()) {
    return false;
  }

  nsAutoString newAltText;
  const bool hasAlt = aElement->GetAttr(nsGkAtoms::alt, newAltText);
  if (!hasAlt || !newAltText.IsEmpty()) {
    // If there is no alt attribute, we should create an accessible. The
    // author may have missed the attribute, and the AT may want to provide a
    // name. If there is alt text, we should create an accessible.
    return true;
  }

  if (newAltText.IsEmpty() && (nsCoreUtils::HasClickListener(aElement) ||
                               MustBeAccessible(aElement, aDocument))) {
    // If there is empty alt text, but there is a click listener for this img,
    // or if it otherwise must be an accessible (e.g., if it has an aria-label
    // attribute), we should create an accessible.
    return true;
  }

  // Otherwise, no alt text means we should not create an accessible.
  return false;
}

/**
 * Return true if the SVG element should be accessible
 */
static bool MustSVGElementBeAccessible(nsIContent* aContent,
                                       DocAccessible* aDocument) {
  // https://w3c.github.io/svg-aam/#include_elements
  for (nsIContent* childElm = aContent->GetFirstChild(); childElm;
       childElm = childElm->GetNextSibling()) {
    if (childElm->IsAnyOfSVGElements(nsGkAtoms::title, nsGkAtoms::desc)) {
      return true;
    }
  }
  return MustBeAccessible(aContent, aDocument);
}

/**
 * Return an accessible for the content if the SVG element requires the creation
 * of an Accessible.
 */
static already_AddRefed<LocalAccessible> MaybeCreateSVGAccessible(
    nsIContent* aContent, DocAccessible* aDocument) {
  if (aContent->IsSVGGeometryElement() ||
      aContent->IsSVGElement(nsGkAtoms::image)) {
    // Shape elements: rect, circle, ellipse, line, path, polygon, and polyline.
    // 'use' and 'text' graphic elements require special support.
    if (MustSVGElementBeAccessible(aContent, aDocument)) {
      // Any accessible that could have TextLeafAccessible children must be a
      // HyperTextAccessible to satisfy the invariant in
      // GetTextAttributesLocalAcc.
      return MakeAndAddRef<EnumRoleHyperTextAccessible<roles::GRAPHIC>>(
          aContent, aDocument);
    }
  } else if (aContent->IsSVGElement(nsGkAtoms::text)) {
    return MakeAndAddRef<HyperTextAccessible>(aContent->AsElement(), aDocument);
  } else if (aContent->IsSVGElement(nsGkAtoms::svg)) {
    // An <svg> element could contain <foreignObject>, which contains HTML but
    // does not normally create its own Accessible. This means that the <svg>
    // Accessible could have TextLeafAccessible children, so it must be a
    // HyperTextAccessible.
    return MakeAndAddRef<EnumRoleHyperTextAccessible<roles::DIAGRAM>>(
        aContent, aDocument);
  } else if (aContent->IsSVGElement(nsGkAtoms::g) &&
             MustSVGElementBeAccessible(aContent, aDocument)) {
    // <g> can also contain <foreignObject>.
    return MakeAndAddRef<EnumRoleHyperTextAccessible<roles::GROUPING>>(
        aContent, aDocument);
  } else if (aContent->IsSVGElement(nsGkAtoms::a)) {
    return MakeAndAddRef<HTMLLinkAccessible>(aContent, aDocument);
  }
  return nullptr;
}

/**
 * Used by XULMap.inc to map both menupopup and popup elements
 */
already_AddRefed<LocalAccessible> CreateMenupopupAccessible(
    Element* aElement, LocalAccessible* aContext) {
#ifdef MOZ_ACCESSIBILITY_ATK
  // ATK considers this node to be redundant when within menubars, and it makes
  // menu navigation with assistive technologies more difficult
  // XXX In the future we will should this for consistency across the
  // nsIAccessible implementations on each platform for a consistent scripting
  // environment, but then strip out redundant accessibles in the AccessibleWrap
  // class for each platform.
  nsIContent* parent = aElement->GetParent();
  if (parent && parent->IsXULElement(nsGkAtoms::menu)) return nullptr;
#endif

  return MakeAndAddRef<XULMenupopupAccessible>(aElement, aContext->Document());
}

static uint64_t GetCacheDomainsForKnownClients(uint64_t aCacheDomains) {
  // Only check clients in the parent process.
  if (!XRE_IsParentProcess()) {
    return aCacheDomains;
  }

  return a11y::GetCacheDomainsForKnownClients(aCacheDomains);
}

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible constructors

static already_AddRefed<LocalAccessible> New_HyperText(
    Element* aElement, LocalAccessible* aContext) {
  return MakeAndAddRef<HyperTextAccessible>(aElement, aContext->Document());
}

template <typename AccClass>
static already_AddRefed<LocalAccessible> New_HTMLDtOrDd(
    Element* aElement, LocalAccessible* aContext) {
  nsIContent* parent = aContext->GetContent();
  if (parent->IsHTMLElement(nsGkAtoms::div)) {
    // It is conforming in HTML to use a div to group dt/dd elements.
    parent = parent->GetParent();
  }

  if (parent && parent->IsHTMLElement(nsGkAtoms::dl)) {
    return MakeAndAddRef<AccClass>(aElement, aContext->Document());
  }

  return nullptr;
}

/**
 * Cached value of the PREF_ACCESSIBILITY_FORCE_DISABLED preference.
 */
static int32_t sPlatformDisabledState = 0;

////////////////////////////////////////////////////////////////////////////////
// Markup maps array.

#define Attr(name, value) {nsGkAtoms::name, nsGkAtoms::value}

#define AttrFromDOM(name, DOMAttrName) \
  {nsGkAtoms::name, nullptr, nsGkAtoms::DOMAttrName}

#define AttrFromDOMIf(name, DOMAttrName, DOMAttrValue) \
  {nsGkAtoms::name, nullptr, nsGkAtoms::DOMAttrName, nsGkAtoms::DOMAttrValue}

#define MARKUPMAP(atom, new_func, r, ...) \
  {nsGkAtoms::atom, new_func, static_cast<a11y::role>(r), {__VA_ARGS__}},

static const MarkupMapInfo sHTMLMarkupMapList[] = {
#include "HTMLMarkupMap.inc"
};

static const MarkupMapInfo sMathMLMarkupMapList[] = {
#include "MathMLMarkupMap.inc"
};

#undef MARKUPMAP

#define XULMAP(atom, ...) {nsGkAtoms::atom, __VA_ARGS__},

#define XULMAP_TYPE(atom, new_type)                                           \
  XULMAP(atom,                                                                \
         [](Element* aElement,                                                \
            LocalAccessible* aContext) -> already_AddRefed<LocalAccessible> { \
           return MakeAndAddRef<new_type>(aElement, aContext->Document());    \
         })

static const XULMarkupMapInfo sXULMarkupMapList[] = {
#include "XULMap.inc"
};

#undef XULMAP_TYPE
#undef XULMAP

#undef Attr
#undef AttrFromDOM
#undef AttrFromDOMIf

////////////////////////////////////////////////////////////////////////////////
// nsAccessibilityService
////////////////////////////////////////////////////////////////////////////////

StaticRefPtr<nsAccessibilityService>
    nsAccessibilityService::gAccessibilityService;
StaticRefPtr<ApplicationAccessible>
    nsAccessibilityService::gApplicationAccessible;
StaticRefPtr<xpcAccessibleApplication>
    nsAccessibilityService::gXPCApplicationAccessible;
uint32_t nsAccessibilityService::gConsumers = 0;
uint64_t nsAccessibilityService::gCacheDomains =
    nsAccessibilityService::kDefaultCacheDomains;

nsAccessibilityService::nsAccessibilityService()
    : mHTMLMarkupMap(std::size(sHTMLMarkupMapList)),
      mMathMLMarkupMap(std::size(sMathMLMarkupMapList)),
      mXULMarkupMap(std::size(sXULMarkupMapList)) {}

nsAccessibilityService::~nsAccessibilityService() {
  NS_ASSERTION(IsShutdown(), "Accessibility wasn't shutdown!");
  gAccessibilityService = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// nsIListenerChangeListener

NS_IMETHODIMP
nsAccessibilityService::ListenersChanged(nsIArray* aEventChanges) {
  uint32_t targetCount;
  nsresult rv = aEventChanges->GetLength(&targetCount);
  NS_ENSURE_SUCCESS(rv, rv);

  for (uint32_t i = 0; i < targetCount; i++) {
    nsCOMPtr<nsIEventListenerChange> change =
        do_QueryElementAt(aEventChanges, i);

    RefPtr<EventTarget> target;
    change->GetTarget(getter_AddRefs(target));
    nsIContent* content(nsIContent::FromEventTargetOrNull(target));
    if (!content || !content->IsElement()) {
      continue;
    }

    uint32_t changeCount;
    change->GetCountOfEventListenerChangesAffectingAccessibility(&changeCount);
    NS_ENSURE_SUCCESS(rv, rv);

    if (changeCount) {
      Document* ownerDoc = content->OwnerDoc();
      DocAccessible* document = GetExistingDocAccessible(ownerDoc);

      if (document) {
        LocalAccessible* acc = document->GetAccessibleOrDocument(content);
        if (!acc && content->IsElement() &&
            content->AsElement()->IsHTMLElement(nsGkAtoms::area)) {
          // For area accessibles, we have to recreate the entire image map,
          // since the image map accessible manages the tree itself. The click
          // listener change may require us to update the role for the
          // accessible associated with the area element.
          LocalAccessible* areaAcc =
              document->GetAccessibleEvenIfNotInMap(content);
          if (areaAcc && areaAcc->LocalParent()) {
            document->RecreateAccessible(areaAcc->LocalParent()->GetContent());
          }
        }
        if (!acc && nsCoreUtils::HasClickListener(content)) {
          // Create an accessible for a inaccessible element having click event
          // handler.
          document->ContentInserted(content, content->GetNextSibling());
        } else if (acc) {
          if ((acc->IsHTMLLink() && !acc->AsHTMLLink()->IsLinked()) ||
              (content->IsElement() &&
               (content->AsElement()->IsHTMLElement(nsGkAtoms::a) ||
                content->AsElement()->IsMathMLElement(nsGkAtoms::a)) &&
               !acc->IsHTMLLink())) {
            // An HTML link without an href attribute should have a generic
            // role, unless it has a click listener. Since we might have gained
            // or lost a click listener here, recreate the accessible so that we
            // can create the correct type of accessible. If it was a link, it
            // may no longer be one. If it wasn't, it may become one.
            document->RecreateAccessible(content);
          }

          // A click listener change might mean losing or gaining an action.
          document->QueueCacheUpdate(acc, CacheDomain::Actions);
        }
      }
    }
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsISupports

NS_IMPL_ISUPPORTS_INHERITED(nsAccessibilityService, DocManager, nsIObserver,
                            nsIListenerChangeListener,
                            nsISelectionListener)  // from SelectionManager

////////////////////////////////////////////////////////////////////////////////
// nsIObserver

NS_IMETHODIMP
nsAccessibilityService::Observe(nsISupports* /*aSubject*/, const char* aTopic,
                                const char16_t* /*aData*/) {
  if (!nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Shutdown();
  }

  return NS_OK;
}

void nsAccessibilityService::NotifyOfAnchorJumpTo(nsIContent* aTargetNode) {
  Document* documentNode = aTargetNode->GetUncomposedDoc();
  if (!documentNode) {
    return;
  }
  DocAccessible* document = GetDocAccessible(documentNode);
  if (!document) {
    return;
  }
  document->SetAnchorJump(aTargetNode);
  // If there is a pending update, the target node might not have been added to
  // the accessibility tree yet, so do not process the anchor jump here. It will
  // be processed in NotificationController::WillRefresh after the tree is up to
  // date. On the other hand, if there is no pending update, process the anchor
  // jump here because the tree is already up to date and there might not be an
  // update in the near future.
  if (!document->Controller()->IsUpdatePending()) {
    document->ProcessAnchorJump();
  }
}

void nsAccessibilityService::FireAccessibleEvent(uint32_t aEvent,
                                                 LocalAccessible* aTarget) {
  nsEventShell::FireEvent(aEvent, aTarget);
}

void nsAccessibilityService::NotifyOfPossibleBoundsChange(
    mozilla::PresShell* aPresShell, nsIContent* aContent) {
  if (!aContent || (!IPCAccessibilityActive() && !aContent->IsText())) {
    return;
  }
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (!document) {
    return;
  }
  LocalAccessible* accessible = document->GetAccessibleOrDocument(aContent);
  if (!accessible) {
    return;
  }
  if (IPCAccessibilityActive()) {
    document->QueueCacheUpdate(accessible, CacheDomain::Bounds);
  }
  MOZ_ASSERT(!aContent->IsText() || accessible->IsTextLeaf(),
             "A DOM Text node should only ever have a TextLeafAccessible");
  if (aContent->IsText() && accessible->IsTextLeaf() &&
      accessible->AsTextLeaf()->Text().EqualsLiteral(" ")) {
    // This space might be becoming invisible, even though it still has a frame.
    // In this case, the frame will have 0 width. Unfortunately, we can't check
    // the frame width here because layout isn't ready yet, so we need to defer
    // this until the refresh driver tick.
    document->UpdateText(aContent);
  }
}

void nsAccessibilityService::NotifyOfComputedStyleChange(
    mozilla::PresShell* aPresShell, nsIContent* aContent) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (!document) {
    return;
  }

  LocalAccessible* accessible = document->GetAccessibleOrDocument(aContent);

  if (!accessible && aContent && aContent->HasChildren() &&
      !aContent->IsInNativeAnonymousSubtree() && !aContent->IsXULElement()) {
    // If the content has children and its frame has a transform, create an
    // Accessible so that we can account for the transform when calculating
    // the Accessible's bounds using the parent process cache. Ditto for
    // position: fixed/sticky and content with overflow styling (hidden, auto,
    // scroll)
    // However, don't do this for XUL widgets, as this breaks XUL a11y code
    // expectations in some cases. XUL widgets are only used in the parent
    // process and can't be cached anyway.
    if (const nsIFrame* frame = aContent->GetPrimaryFrame()) {
      const auto& disp = *frame->StyleDisplay();
      if (disp.HasTransform(frame) ||
          disp.mPosition == StylePositionProperty::Fixed ||
          disp.mPosition == StylePositionProperty::Sticky ||
          disp.IsScrollableOverflow()) {
        document->ContentInserted(aContent, aContent->GetNextSibling());
      }
    }
  } else if (accessible && IPCAccessibilityActive()) {
    accessible->MaybeQueueCacheUpdateForStyleChanges();
  }
}

void nsAccessibilityService::NotifyOfResolutionChange(
    mozilla::PresShell* aPresShell, float aResolution) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (document && document->IPCDoc()) {
    AutoTArray<mozilla::a11y::CacheData, 1> data;
    auto fields = MakeRefPtr<AccAttributes>();
    fields->SetAttribute(CacheKey::Resolution, aResolution);
    data.AppendElement(mozilla::a11y::CacheData(0, fields));
    document->IPCDoc()->SendCache(CacheUpdateType::Update, data);
  }
}

void nsAccessibilityService::NotifyOfDevPixelRatioChange(
    mozilla::PresShell* aPresShell, int32_t aAppUnitsPerDevPixel) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (document && document->IPCDoc()) {
    AutoTArray<mozilla::a11y::CacheData, 1> data;
    auto fields = MakeRefPtr<AccAttributes>();
    fields->SetAttribute(CacheKey::AppUnitsPerDevPixel, aAppUnitsPerDevPixel);
    data.AppendElement(mozilla::a11y::CacheData(0, fields));
    document->IPCDoc()->SendCache(CacheUpdateType::Update, data);
  }
}

void nsAccessibilityService::NotifyAnchorPositionedRemoved(
    mozilla::PresShell* aPresShell, nsIFrame* aFrame) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (!document) {
    return;
  }

  const nsIFrame* anchorFrame =
      nsCoreUtils::GetAnchorForPositionedFrame(aPresShell, aFrame);
  if (!anchorFrame) {
    return;
  }

  if (LocalAccessible* anchorAcc =
          document->GetAccessible(anchorFrame->GetContent())) {
    document->QueueCacheUpdate(anchorAcc, CacheDomain::Relations);
  }
}

void nsAccessibilityService::NotifyAnchorRemoved(mozilla::PresShell* aPresShell,
                                                 nsIFrame* aFrame) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (!document) {
    return;
  }

  nsIFrame* positionedFrame =
      nsCoreUtils::GetPositionedFrameForAnchor(aPresShell, aFrame);
  if (!positionedFrame) {
    return;
  }

  if (LocalAccessible* positionedAcc =
          document->GetAccessible(positionedFrame->GetContent())) {
    // If the anchor was removed, its positioned element may now have a 1:1
    // relation with another anchor, and they would get a description a11y
    // relation. So we need to go one level deeper here and refresh the cache of
    // any potential anchors that remain on the positioned element.
    document->RefreshAnchorRelationCacheForTarget(positionedAcc);
  }
}

void nsAccessibilityService::NotifyAnchorPositionedScrollUpdate(
    mozilla::PresShell* aPresShell, nsIFrame* aFrame) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (!document) {
    return;
  }

  if (LocalAccessible* positionedAcc =
          document->GetAccessible(aFrame->GetContent())) {
    // Refresh relations before reflow to notify current anchor.
    document->RefreshAnchorRelationCacheForTarget(positionedAcc);

    // Refresh relations after next tick when reflow updated to the
    // new anchor state.
    document->Controller()->ScheduleNotification<DocAccessible>(
        document, &DocAccessible::RefreshAnchorRelationCacheForTarget,
        positionedAcc);
  }
}

void nsAccessibilityService::NotifyAttrElementWillChange(
    mozilla::dom::Element* aElement, nsAtom* aAttr) {
  if (DocAccessible* docAcc = GetDocAccessible(aElement->GetComposedDoc())) {
    docAcc->AttrElementWillChange(aElement, aAttr);
  }
}

void nsAccessibilityService::NotifyAttrElementChanged(
    mozilla::dom::Element* aElement, nsAtom* aAttr) {
  if (DocAccessible* docAcc = GetDocAccessible(aElement->GetComposedDoc())) {
    docAcc->AttrElementChanged(aElement, aAttr);
  }
}

void nsAccessibilityService::NotifyARIAAttributeDefaultWillChange(
    mozilla::dom::Element* aElement, nsAtom* aAttribute, AttrModType aModType) {
  if (DocAccessible* docAcc = GetDocAccessible(aElement->GetComposedDoc())) {
    docAcc->ARIAAttributeDefaultWillChange(aElement, aAttribute, aModType);
  }
}

void nsAccessibilityService::NotifyARIAAttributeDefaultChanged(
    mozilla::dom::Element* aElement, nsAtom* aAttribute, AttrModType aModType) {
  if (DocAccessible* docAcc = GetDocAccessible(aElement->GetComposedDoc())) {
    docAcc->ARIAAttributeDefaultChanged(aElement, aAttribute, aModType);
  }
}

void nsAccessibilityService::NotifyOfEditContextAttachmentChange(
    mozilla::dom::Element* aElement) {
  dom::Document* doc = aElement->GetComposedDoc();
  if (!doc) {
    return;
  }
  DocAccessible* docAcc = GetExistingDocAccessible(doc);
  if (!docAcc) {
    return;
  }
  if (LocalAccessible* acc = docAcc->GetAccessible(aElement)) {
    // Only the root of an EditContext should get the focusable state, unlike
    // the editable state, which all descendants also get. The DOM READWRITE
    // state has already been updated by this point, so we can just let
    // AccStateChangeEvent recalculate whether acc is focusable now. We can't do
    // this in DocAccessible::ElementStateChanged because there's no way to tell
    // which element was the EditContext root once the EditContext is detached.
    auto event = MakeRefPtr<AccStateChangeEvent>(acc, states::FOCUSABLE);
    docAcc->FireDelayedEvent(event);
  }
}

void nsAccessibilityService::AriaNotify(
    nsINode* aNode, const nsAString& aAnnouncement,
    const mozilla::dom::AriaNotificationOptions& aOptions) {
  Document* doc = aNode->GetComposedDoc();
  if (!doc) {
    return;
  }
  DocAccessible* docAcc = GetDocAccessible(doc);
  if (!docAcc) {
    return;
  }
  LocalAccessible* acc = docAcc->GetAccessible(aNode);
  if (acc) {
    acc->Announce(aAnnouncement,
                  aOptions.mPriority == dom::AriaNotifyPriority::High
                      ? nsIAccessibleAnnouncementEvent::ASSERTIVE
                      : nsIAccessibleAnnouncementEvent::POLITE);
  }
}

LocalAccessible* nsAccessibilityService::GetRootDocumentAccessible(
    PresShell* aPresShell, bool aCanCreate) {
  PresShell* presShell = aPresShell;
  Document* documentNode = aPresShell->GetDocument();
  if (documentNode) {
    nsCOMPtr<nsIDocShellTreeItem> treeItem(documentNode->GetDocShell());
    if (treeItem) {
      nsCOMPtr<nsIDocShellTreeItem> rootTreeItem;
      treeItem->GetInProcessRootTreeItem(getter_AddRefs(rootTreeItem));
      if (treeItem != rootTreeItem) {
        nsCOMPtr<nsIDocShell> docShell(do_QueryInterface(rootTreeItem));
        presShell = docShell->GetPresShell();
      }

      return aCanCreate ? GetDocAccessible(presShell)
                        : presShell->GetDocAccessible();
    }
  }
  return nullptr;
}

void nsAccessibilityService::NotifyOfTabPanelVisibilityChange(
    PresShell* aPresShell, Element* aPanel, bool aNowVisible) {
  MOZ_ASSERT(aPanel->GetParent()->IsXULElement(nsGkAtoms::tabpanels));

  DocAccessible* document = GetDocAccessible(aPresShell);
  if (!document) {
    return;
  }

  if (LocalAccessible* acc = document->GetAccessible(aPanel)) {
    auto event =
        MakeRefPtr<AccStateChangeEvent>(acc, states::OFFSCREEN, aNowVisible);
    document->FireDelayedEvent(event);
  }
}

void nsAccessibilityService::ContentRangeInserted(PresShell* aPresShell,
                                                  nsIContent* aStartChild,
                                                  nsIContent* aEndChild) {
  DocAccessible* document = GetDocAccessible(aPresShell);
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eTree)) {
    logging::MsgBegin("TREE", "content inserted; doc: %p", document);
    logging::Node("container", aStartChild->GetParentNode());
    for (nsIContent* child = aStartChild; child != aEndChild;
         child = child->GetNextSibling()) {
      logging::Node("content", child);
    }
    logging::MsgEnd();
    logging::Stack();
  }
#endif

  if (document) {
    document->ContentInserted(aStartChild, aEndChild);
  }
}

void nsAccessibilityService::ScheduleAccessibilitySubtreeUpdate(
    PresShell* aPresShell, nsIContent* aContent) {
  DocAccessible* document = GetDocAccessible(aPresShell);
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eTree)) {
    logging::MsgBegin("TREE", "schedule update; doc: %p", document);
    logging::Node("content node", aContent);
    logging::MsgEnd();
  }
#endif

  if (document) {
    document->ScheduleTreeUpdate(aContent);
  }
}

void nsAccessibilityService::ContentRemoved(PresShell* aPresShell,
                                            nsIContent* aChildNode) {
  DocAccessible* document = GetDocAccessible(aPresShell);
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eTree)) {
    logging::MsgBegin("TREE", "content removed; doc: %p", document);
    logging::Node("container node", aChildNode->GetFlattenedTreeParent());
    logging::Node("content node", aChildNode);
    logging::MsgEnd();
  }
#endif

  if (document) {
    document->ContentRemoved(aChildNode);
  }

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eTree)) {
    logging::MsgEnd();
    logging::Stack();
  }
#endif
}

void nsAccessibilityService::TableLayoutGuessMaybeChanged(
    PresShell* aPresShell, nsIContent* aContent) {
  if (DocAccessible* document = GetDocAccessible(aPresShell)) {
    if (LocalAccessible* acc = document->GetAccessible(aContent)) {
      if (LocalAccessible* table = nsAccUtils::TableFor(acc)) {
        document->QueueCacheUpdate(table, CacheDomain::Table);
      }
    }
  }
}

void nsAccessibilityService::ComboboxValueChanged(nsIContent* aSelect) {
  DocAccessible* document =
      GetDocAccessible(aSelect->OwnerDoc()->GetPresShell());
  if (!document) {
    return;
  }
  if (LocalAccessible* accessible = document->GetAccessible(aSelect);
      accessible && accessible->IsCombobox()) {
    document->FireDelayedEvent(nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE,
                               accessible);
  }
}

void nsAccessibilityService::ComboboxOptionMaybeChanged(
    PresShell* aPresShell, nsIContent* aMutatingNode) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (!document) {
    return;
  }

  for (nsIContent* cur = aMutatingNode; cur; cur = cur->GetParent()) {
    if (cur->IsHTMLElement(nsGkAtoms::option)) {
      if (LocalAccessible* accessible = document->GetAccessible(cur)) {
        document->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE,
                                   accessible);
        break;
      }
      if (cur->IsHTMLElement(nsGkAtoms::select)) {
        break;
      }
    }
  }
}

void nsAccessibilityService::UpdateText(PresShell* aPresShell,
                                        nsIContent* aContent) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (document) document->UpdateText(aContent);
}

void nsAccessibilityService::TreeViewChanged(PresShell* aPresShell,
                                             nsIContent* aContent,
                                             nsITreeView* aView) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (document) {
    LocalAccessible* accessible = document->GetAccessible(aContent);
    if (accessible) {
      XULTreeAccessible* treeAcc = accessible->AsXULTree();
      if (treeAcc) treeAcc->TreeViewChanged(aView);
    }
  }
}

void nsAccessibilityService::RangeValueChanged(PresShell* aPresShell,
                                               nsIContent* aContent) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (document) {
    LocalAccessible* accessible = document->GetAccessible(aContent);
    if (accessible) {
      document->FireDelayedEvent(nsIAccessibleEvent::EVENT_VALUE_CHANGE,
                                 accessible);
    }
  }
}

void nsAccessibilityService::ColorValueChanged(PresShell* aPresShell,
                                               nsIContent* aContent) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (document) {
    LocalAccessible* accessible = document->GetAccessible(aContent);
    if (accessible) {
      document->FireDelayedEvent(nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE,
                                 accessible);
    }
  }
}

void nsAccessibilityService::UpdateImageMap(nsImageFrame* aImageFrame) {
  PresShell* presShell = aImageFrame->PresShell();
  DocAccessible* document = GetDocAccessible(presShell);
  if (document) {
    LocalAccessible* accessible =
        document->GetAccessible(aImageFrame->GetContent());
    if (accessible) {
      HTMLImageMapAccessible* imageMap = accessible->AsImageMap();
      if (imageMap) {
        imageMap->UpdateChildAreas();
        return;
      }

      // If image map was initialized after we created an accessible (that'll
      // be an image accessible) then recreate it.
      RecreateAccessible(presShell, aImageFrame->GetContent());
    }
  }
}

void nsAccessibilityService::PresShellActivated(PresShell* aPresShell) {
  DocAccessible* document = aPresShell->GetDocAccessible();
  if (document) {
    RootAccessible* rootDocument = document->RootAccessible();
    NS_ASSERTION(rootDocument, "Entirely broken tree: no root document!");
    if (rootDocument) rootDocument->DocumentActivated(document);
  }
}

void nsAccessibilityService::RecreateAccessible(PresShell* aPresShell,
                                                nsIContent* aContent) {
  DocAccessible* document = GetDocAccessible(aPresShell);
  if (document) document->RecreateAccessible(aContent);
}

void nsAccessibilityService::GetStringRole(uint32_t aRole, nsAString& aString) {
#define ROLE(geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType, \
             nameRule)                                                      \
  case roles::geckoRole:                                                    \
    aString.AssignLiteral(stringRole);                                      \
    return;

  switch (aRole) {
#include "RoleMap.inc"
    default:
      aString.AssignLiteral("unknown");
      return;
  }

#undef ROLE
}

void nsAccessibilityService::GetStringStates(uint32_t aState,
                                             uint32_t aExtraState,
                                             nsISupports** aStringStates) {
  RefPtr<DOMStringList> stringStates =
      GetStringStates(nsAccUtils::To64State(aState, aExtraState));

  // unknown state
  if (!stringStates->Length()) {
    stringStates->Add(u"unknown"_ns);
  }

  stringStates.forget(aStringStates);
}

already_AddRefed<DOMStringList> nsAccessibilityService::GetStringStates(
    uint64_t aStates) const {
  auto stringStates = MakeRefPtr<DOMStringList>();

  if (aStates & states::UNAVAILABLE) {
    stringStates->Add(u"unavailable"_ns);
  }
  if (aStates & states::SELECTED) {
    stringStates->Add(u"selected"_ns);
  }
  if (aStates & states::FOCUSED) {
    stringStates->Add(u"focused"_ns);
  }
  if (aStates & states::PRESSED) {
    stringStates->Add(u"pressed"_ns);
  }
  if (aStates & states::CHECKED) {
    stringStates->Add(u"checked"_ns);
  }
  if (aStates & states::MIXED) {
    stringStates->Add(u"mixed"_ns);
  }
  if (aStates & states::READONLY) {
    stringStates->Add(u"readonly"_ns);
  }
  if (aStates & states::HOTTRACKED) {
    stringStates->Add(u"hottracked"_ns);
  }
  if (aStates & states::DEFAULT) {
    stringStates->Add(u"default"_ns);
  }
  if (aStates & states::EXPANDED) {
    stringStates->Add(u"expanded"_ns);
  }
  if (aStates & states::COLLAPSED) {
    stringStates->Add(u"collapsed"_ns);
  }
  if (aStates & states::BUSY) {
    stringStates->Add(u"busy"_ns);
  }
  if (aStates & states::FLOATING) {
    stringStates->Add(u"floating"_ns);
  }
  if (aStates & states::ANIMATED) {
    stringStates->Add(u"animated"_ns);
  }
  if (aStates & states::INVISIBLE) {
    stringStates->Add(u"invisible"_ns);
  }
  if (aStates & states::OFFSCREEN) {
    stringStates->Add(u"offscreen"_ns);
  }
  if (aStates & states::SIZEABLE) {
    stringStates->Add(u"sizeable"_ns);
  }
  if (aStates & states::MOVEABLE) {
    stringStates->Add(u"moveable"_ns);
  }
  if (aStates & states::SELFVOICING) {
    stringStates->Add(u"selfvoicing"_ns);
  }
  if (aStates & states::FOCUSABLE) {
    stringStates->Add(u"focusable"_ns);
  }
  if (aStates & states::SELECTABLE) {
    stringStates->Add(u"selectable"_ns);
  }
  if (aStates & states::LINKED) {
    stringStates->Add(u"linked"_ns);
  }
  if (aStates & states::TRAVERSED) {
    stringStates->Add(u"traversed"_ns);
  }
  if (aStates & states::MULTISELECTABLE) {
    stringStates->Add(u"multiselectable"_ns);
  }
  if (aStates & states::EXTSELECTABLE) {
    stringStates->Add(u"extselectable"_ns);
  }
  if (aStates & states::PROTECTED) {
    stringStates->Add(u"protected"_ns);
  }
  if (aStates & states::HASPOPUP) {
    stringStates->Add(u"haspopup"_ns);
  }
  if (aStates & states::REQUIRED) {
    stringStates->Add(u"required"_ns);
  }
  if (aStates & states::ALERT) {
    stringStates->Add(u"alert"_ns);
  }
  if (aStates & states::INVALID) {
    stringStates->Add(u"invalid"_ns);
  }
  if (aStates & states::CHECKABLE) {
    stringStates->Add(u"checkable"_ns);
  }
  if (aStates & states::SUPPORTS_AUTOCOMPLETION) {
    stringStates->Add(u"autocompletion"_ns);
  }
  if (aStates & states::DEFUNCT) {
    stringStates->Add(u"defunct"_ns);
  }
  if (aStates & states::SELECTABLE_TEXT) {
    stringStates->Add(u"selectable text"_ns);
  }
  if (aStates & states::EDITABLE) {
    stringStates->Add(u"editable"_ns);
  }
  if (aStates & states::ACTIVE) {
    stringStates->Add(u"active"_ns);
  }
  if (aStates & states::MODAL) {
    stringStates->Add(u"modal"_ns);
  }
  if (aStates & states::MULTI_LINE) {
    stringStates->Add(u"multi line"_ns);
  }
  if (aStates & states::HORIZONTAL) {
    stringStates->Add(u"horizontal"_ns);
  }
  if (aStates & states::OPAQUE1) {
    stringStates->Add(u"opaque"_ns);
  }
  if (aStates & states::SINGLE_LINE) {
    stringStates->Add(u"single line"_ns);
  }
  if (aStates & states::TRANSIENT) {
    stringStates->Add(u"transient"_ns);
  }
  if (aStates & states::VERTICAL) {
    stringStates->Add(u"vertical"_ns);
  }
  if (aStates & states::STALE) {
    stringStates->Add(u"stale"_ns);
  }
  if (aStates & states::ENABLED) {
    stringStates->Add(u"enabled"_ns);
  }
  if (aStates & states::SENSITIVE) {
    stringStates->Add(u"sensitive"_ns);
  }
  if (aStates & states::EXPANDABLE) {
    stringStates->Add(u"expandable"_ns);
  }
  if (aStates & states::PINNED) {
    stringStates->Add(u"pinned"_ns);
  }
  if (aStates & states::CURRENT) {
    stringStates->Add(u"current"_ns);
  }

  return stringStates.forget();
}

void nsAccessibilityService::GetStringEventType(uint32_t aEventType,
                                                nsAString& aString) {
  static_assert(
      nsIAccessibleEvent::EVENT_LAST_ENTRY == std::size(kEventTypeNames),
      "nsIAccessibleEvent constants are out of sync to kEventTypeNames");

  if (aEventType >= std::size(kEventTypeNames)) {
    aString.AssignLiteral("unknown");
    return;
  }

  aString.AssignASCII(kEventTypeNames[aEventType]);
}

void nsAccessibilityService::GetStringEventType(uint32_t aEventType,
                                                nsACString& aString) {
  MOZ_ASSERT(nsIAccessibleEvent::EVENT_LAST_ENTRY == std::size(kEventTypeNames),
             "nsIAccessibleEvent constants are out of sync to kEventTypeNames");

  if (aEventType >= std::size(kEventTypeNames)) {
    aString.AssignLiteral("unknown");
    return;
  }

  aString = nsDependentCString(kEventTypeNames[aEventType]);
}

void nsAccessibilityService::GetStringRelationType(uint32_t aRelationType,
                                                   nsAString& aString) {
  NS_ENSURE_TRUE_VOID(aRelationType <=
                      static_cast<uint32_t>(RelationType::LAST));

#define RELATIONTYPE(geckoType, geckoTypeName, atkType, msaaType, ia2Type) \
  case RelationType::geckoType:                                            \
    aString.AssignLiteral(geckoTypeName);                                  \
    return;

  RelationType relationType = static_cast<RelationType>(aRelationType);
  switch (relationType) {
#include "RelationTypeMap.inc"
    default:
      aString.AssignLiteral("unknown");
      return;
  }

#undef RELATIONTYPE
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessibilityService public

LocalAccessible* nsAccessibilityService::CreateAccessible(
    nsINode* aNode, LocalAccessible* aContext, bool* aIsSubtreeHidden) {
  MOZ_ASSERT(aContext, "No context provided");
  MOZ_ASSERT(aNode, "No node to create an accessible for");
  MOZ_ASSERT(gConsumers, "No creation after shutdown");

  if (aIsSubtreeHidden) *aIsSubtreeHidden = false;

  DocAccessible* document = aContext->Document();
  MOZ_ASSERT(!document->GetAccessible(aNode),
             "We already have an accessible for this node.");

  if (aNode->IsDocument()) {
    // If it's document node then ask accessible document loader for
    // document accessible, otherwise return null.
    return GetDocAccessible(aNode->AsDocument());
  }

  // We have a content node.
  if (!aNode->GetComposedDoc()) {
    NS_WARNING("Creating accessible for node with no document");
    return nullptr;
  }

  if (aNode->OwnerDoc() != document->DocumentNode()) {
    NS_ERROR("Creating accessible for wrong document");
    return nullptr;
  }

  if (!aNode->IsContent()) return nullptr;

  if (document->IsRootContent(aNode)) {
    // The root element is represented by the DocAccessible. Don't create an
    // additional Accessible for it.
    return nullptr;
  }

  nsIContent* content = aNode->AsContent();
  if (aria::IsValidARIAHidden(content)) {
    if (aIsSubtreeHidden) {
      *aIsSubtreeHidden = true;
    }
    return nullptr;
  }

  // Check frame and its visibility.
  nsIFrame* frame = content->GetPrimaryFrame();
  if (frame) {
    // If invisible or inert, we don't create an accessible, but we don't mark
    // it with aIsSubtreeHidden = true, since visibility: hidden frame allows
    // visible elements in subtree, and inert elements allow non-inert
    // elements.
    if (!frame->StyleVisibility()->IsVisible() || frame->StyleUI()->IsInert()) {
      return nullptr;
    }
  } else if (nsCoreUtils::CanCreateAccessibleWithoutFrame(content,
                                                          aIsSubtreeHidden)) {
    // display:contents element doesn't have a frame, but retains the
    // semantics. All its children are unaffected.
    const nsRoleMapEntry* roleMapEntry = nullptr;
    RefPtr<LocalAccessible> newAcc;
    const MarkupMapInfo* markupMap = nullptr;
    // The body's role (when it exists) is forwarded to the doc accessible.
    // Avoid making decisions based on the role when we're dealing with the
    // body, and ensure roleMapEntry stays null until we bind the body's
    // acc to the document.
    if (!document->IsBodyElement(content)) {
      roleMapEntry = aria::GetRoleMap(content->AsElement());
      newAcc = MaybeCreateSpecificARIAAccessible(roleMapEntry, aContext,
                                                 content, document);
      if (!newAcc) {
        markupMap = GetMarkupMapInfoFor(content);
        if (markupMap && markupMap->new_func) {
          newAcc = markupMap->new_func(content->AsElement(), aContext);
        }
      }
    }

    // SVG elements are not in a markup map, but we may still need to create an
    // accessible for one, even in the case of display:contents.
    if (!newAcc && content->IsSVGElement()) {
      newAcc = MaybeCreateSVGAccessible(content, document);
    }

    // Check whether this element has an ARIA role or attribute that requires
    // us to create an Accessible.
    const bool hasNonPresentationalARIARole =
        roleMapEntry && !roleMapEntry->Is(nsGkAtoms::presentation) &&
        !roleMapEntry->Is(nsGkAtoms::none);
    if (!newAcc &&
        (hasNonPresentationalARIARole || MustBeAccessible(content, document))) {
      newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
    }

    if (content->IsElement() && content->AsElement()->GetPseudoElementType() ==
                                    PseudoStyleType::Picker) {
      newAcc = MakeRefPtr<HTMLComboboxListAccessible>(content, document);
    }

    // If there's still no Accessible but we do have an entry in the markup
    // map for this non-presentational element, create a generic
    // HyperTextAccessible.
    if (!newAcc && markupMap &&
        (!roleMapEntry || hasNonPresentationalARIARole)) {
      newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
    }

    if (newAcc) {
      document->BindToDocument(newAcc, roleMapEntry);
    }
    return newAcc;
  } else {
    // No frame, and this content can't get an Accessible without one.
    // CanCreateAccessibleWithoutFrame() has already set aIsSubtreeHidden
    // appropriately: true if nothing in this subtree could ever be exposed
    // (e.g. display: none or content-visibility: hidden), or left as-is (false)
    // if a descendant might still be exposed despite this element not being
    // creatable (e.g. an inert ancestor with a descendant which is not inert,
    // such as an open modal dialog).
    return nullptr;
  }

  if (frame->IsHiddenByContentVisibilityOnAnyAncestor(
          nsIFrame::IncludeContentVisibility::Hidden)) {
    if (aIsSubtreeHidden) {
      *aIsSubtreeHidden = true;
    }
    return nullptr;
  }

  if (nsMenuPopupFrame* popupFrame = do_QueryFrame(frame)) {
    // Hidden tooltips and panels don't create accessibles in the whole subtree.
    // Showing them gets handled by RootAccessible::ProcessDOMEvent.
    if (content->IsAnyOfXULElements(nsGkAtoms::tooltip, nsGkAtoms::panel)) {
      nsPopupState popupState = popupFrame->PopupState();
      if (popupState == ePopupHiding || popupState == ePopupInvisible ||
          popupState == ePopupClosed) {
        if (aIsSubtreeHidden) {
          *aIsSubtreeHidden = true;
        }
        return nullptr;
      }
    }
  }

  // Attempt to create an accessible based on what we know.
  RefPtr<LocalAccessible> newAcc;

  // Create accessible for visible text frames.
  if (content->IsText()) {
    nsIFrame::RenderedText text = frame->GetRenderedText(
        0, UINT32_MAX, nsIFrame::TextOffsetType::OffsetsInContentText,
        nsIFrame::TrailingWhitespace::DontTrim);
    Maybe<nsString> cssAlt = nsCoreUtils::GetCssAltContent(content);
    // Ignore not rendered text nodes and whitespace text nodes between table
    // cells.
    if (text.mString.IsEmpty() ||
        (nsCoreUtils::IsTrimmedWhitespaceBeforeHardLineBreak(frame) &&
         // If there is CSS alt text, it's okay if the text itself is just
         // whitespace; e.g. content: " " / "alt"
         !cssAlt) ||
        (aContext->IsTableRow() &&
         nsCoreUtils::IsWhitespaceString(text.mString))) {
      if (aIsSubtreeHidden) *aIsSubtreeHidden = true;

      return nullptr;
    }
    if (cssAlt && cssAlt->IsEmpty()) {
      if (aIsSubtreeHidden) {
        *aIsSubtreeHidden = true;
      }
      return nullptr;
    }
    newAcc = CreateAccessibleByFrameType(frame, content, aContext);
    MOZ_ASSERT(newAcc, "Accessible not created for text node!");
    document->BindToDocument(newAcc, nullptr);
    if (cssAlt) {
      newAcc->AsTextLeaf()->SetText(*cssAlt);
    } else {
      newAcc->AsTextLeaf()->SetText(text.mString);
    }
    return newAcc;
  }

  if (content->IsHTMLElement(nsGkAtoms::map)) {
    // Create hyper text accessible for HTML map if it is used to group links
    // (see http://www.w3.org/TR/WCAG10-HTML-TECHS/#group-bypass). If the HTML
    // map rect is empty then it is used for links grouping. Otherwise it should
    // be used in conjunction with HTML image element and in this case we don't
    // create any accessible for it and don't walk into it. The accessibles for
    // HTML area (HTMLAreaAccessible) the map contains are attached as
    // children of the appropriate accessible for HTML image
    // (ImageAccessible).
    if (nsLayoutUtils::GetAllInFlowRectsUnion(frame, frame->GetParent())
            .IsEmpty()) {
      if (aIsSubtreeHidden) *aIsSubtreeHidden = true;

      return nullptr;
    }

    newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
    document->BindToDocument(newAcc, aria::GetRoleMap(content->AsElement()));
    return newAcc;
  }

  if (document->IsBodyElement(content)) {
    // If the body element exposes any properties that would force acc
    // creation, create an accessible for it.
    if (MustBeGenericAccessible(content) ||
        MustBeAccessible(content, document) ||
        nsCoreUtils::HasClickListener(content)) {
      newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
      // Any role exposed on this acc should be forwarded to the doc
      // accessible, so don't pass a role map entry here.
      document->BindToDocument(newAcc, nullptr);
      return newAcc;
    }
    return nullptr;
  }

  const nsRoleMapEntry* roleMapEntry = aria::GetRoleMap(content->AsElement());

  if (roleMapEntry && (roleMapEntry->Is(nsGkAtoms::presentation) ||
                       roleMapEntry->Is(nsGkAtoms::none))) {
    if (MustBeAccessible(content, document)) {
      // If the element is focusable, a global ARIA attribute is applied to it
      // or it is referenced by an ARIA relationship, then treat
      // role="presentation" on the element as if the role is not there.
      roleMapEntry = nullptr;
    } else if (MustBeGenericAccessible(content)) {
      // Clear roleMapEntry so that we use the generic role specified below.
      // Otherwise, we'd expose roles::NOTHING as specified for presentation in
      // ARIAMap.
      roleMapEntry = nullptr;
      newAcc = MakeRefPtr<EnumRoleHyperTextAccessible<roles::TEXT_CONTAINER>>(
          content, document);
    } else {
      return nullptr;
    }
  }

  // We should always use OuterDocAccessible for OuterDocs, even if there's a
  // specific ARIA class we would otherwise use.
  if (!newAcc && frame->AccessibleType() != eOuterDocType) {
    newAcc = MaybeCreateSpecificARIAAccessible(roleMapEntry, aContext, content,
                                               document);
  }

  if (!newAcc && content->IsHTMLElement()) {  // HTML accessibles
    // Prefer to use markup to decide if and what kind of accessible to
    // create,
    const MarkupMapInfo* markupMap =
        mHTMLMarkupMap.Get(content->NodeInfo()->NameAtom());
    if (markupMap && markupMap->new_func) {
      newAcc = markupMap->new_func(content->AsElement(), aContext);
    }

    if (!newAcc) {  // try by frame accessible type.
      newAcc = CreateAccessibleByFrameType(frame, content, aContext);
    }

    // If table has strong ARIA role then all table descendants shouldn't
    // expose their native roles.
    if (!roleMapEntry && newAcc && aContext->HasStrongARIARole()) {
      if (frame->AccessibleType() == eHTMLTableRowType) {
        const nsRoleMapEntry* contextRoleMap = aContext->ARIARoleMap();
        if (!contextRoleMap->IsOfType(eTable)) {
          roleMapEntry = &aria::gEmptyRoleMap;
        }

      } else if (frame->AccessibleType() == eHTMLTableCellType &&
                 aContext->ARIARoleMap() == &aria::gEmptyRoleMap) {
        roleMapEntry = &aria::gEmptyRoleMap;

      } else if (content->IsAnyOfHTMLElements(nsGkAtoms::dt, nsGkAtoms::li,
                                              nsGkAtoms::dd) ||
                 frame->AccessibleType() == eHTMLLiType) {
        const nsRoleMapEntry* contextRoleMap = aContext->ARIARoleMap();
        if (!contextRoleMap->IsOfType(eList)) {
          roleMapEntry = &aria::gEmptyRoleMap;
        }
      }
    }
  }

  // XUL accessibles.
  // We don't support XUL in remote docs, so if this isn't the top process,
  // don't create XUL accessible for it.
  if (!newAcc && content->IsXULElement() && !IPCAccessibilityActive()) {
    if (content->IsXULElement(nsGkAtoms::panel)) {
      // We filter here instead of in the XUL map because
      // if we filter there and return null, we still end up
      // creating a generic accessible at the end of this function.
      // Doing the filtering here ensures we never create accessibles
      // for panels whose popups aren't visible.
      nsMenuPopupFrame* popupFrame = do_QueryFrame(frame);
      if (!popupFrame) {
        return nullptr;
      }

      nsPopupState popupState = popupFrame->PopupState();
      if (popupState == ePopupHiding || popupState == ePopupInvisible ||
          popupState == ePopupClosed) {
        return nullptr;
      }
    }

    // Prefer to use XUL to decide if and what kind of accessible to create.
    const XULMarkupMapInfo* xulMap =
        mXULMarkupMap.Get(content->NodeInfo()->NameAtom());
    if (xulMap && xulMap->new_func) {
      newAcc = xulMap->new_func(content->AsElement(), aContext);
    }

    // Any XUL/flex box can be used as tabpanel, make sure we create a proper
    // accessible for it.
    if (!newAcc && aContext->IsXULTabpanels() &&
        content->GetParent() == aContext->GetContent()) {
      LayoutFrameType frameType = frame->Type();
      // FIXME(emilio): Why only these frame types?
      if (frameType == LayoutFrameType::FlexContainer ||
          frameType == LayoutFrameType::ScrollContainer) {
        newAcc = MakeRefPtr<XULTabpanelAccessible>(content, document);
      }
    }
  }

  if (!newAcc) {
    if (content->IsSVGElement()) {
      newAcc = MaybeCreateSVGAccessible(content, document);
    } else if (content->IsMathMLElement()) {
      const MarkupMapInfo* markupMap =
          mMathMLMarkupMap.Get(content->NodeInfo()->NameAtom());
      if (markupMap && markupMap->new_func) {
        newAcc = markupMap->new_func(content->AsElement(), aContext);
      }

      // Fall back to text when encountering Content MathML.
      if (!newAcc &&
          !content->IsAnyOfMathMLElements(
              nsGkAtoms::annotation, nsGkAtoms::annotation_xml,
              nsGkAtoms::mpadded, nsGkAtoms::mphantom, nsGkAtoms::maligngroup,
              nsGkAtoms::malignmark, nsGkAtoms::mspace, nsGkAtoms::semantics)) {
        newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
      }
    } else if (content->IsGeneratedContentContainerForMarker()) {
      nsAutoString text;
      if (aContext->IsHTMLListItem()) {
        HTMLListBulletAccessible::ComputeText(content, text);
      }
      // If there is no text (e.g. because there is empty CSS alt content), we
      // don't expose the bullet.
      if (!text.IsEmpty()) {
        newAcc = MakeRefPtr<HTMLListBulletAccessible>(content, document);
        // An HTMLListBulletAccessible is a special kind of TextLeafAccessible.
        // This is so we can cache the text, since it's not safe to retrieve the
        // text from layout except during refresh ticks. This also allows us to
        // use TextUpdater to fire events.
        newAcc->AsTextLeaf()->SetText(text);
      }
      if (aIsSubtreeHidden) {
        *aIsSubtreeHidden = true;
      }
    } else if (Maybe<nsString> cssAlt = nsCoreUtils::GetCssAltContent(content);
               cssAlt && !cssAlt->IsEmpty()) {
      // This is a pseudo-element without children that has CSS alt text which
      // isn't empty. This only happens when there is alt text with an empty
      // content string; e.g. content: "" / "alt" In this case, we need to
      // expose the alt text on the pseudo-element itself, since we don't have a
      // child to use. We create a TextLeafAccessible with the pseudo-element as
      // the backing DOM node.
      newAcc = MakeRefPtr<TextLeafAccessible>(content, document);
      newAcc->AsTextLeaf()->SetText(*cssAlt);
    }
  }

  // If no accessible, see if we need to create a generic accessible because
  // of some property that makes this object interesting
  if (!newAcc && content->GetParent() &&
      (roleMapEntry || MustBeAccessible(content, document) ||
       (content->IsHTMLElement() && nsCoreUtils::HasClickListener(content)))) {
    // This content is focusable or has an interesting dynamic content
    // accessibility property. If it's interesting we need it in the
    // accessibility hierarchy so that events or other accessibles can point to
    // it, or so that it can hold a state, etc.
    // Interesting container which may have selectable text and/or embedded
    // objects. Must be a HyperTextAccessible because children might include
    // TextLeafAccessibles.
    newAcc = MakeRefPtr<HyperTextAccessible>(content, document);
  } else if (!newAcc && MustBeGenericAccessible(content)) {
    newAcc = MakeRefPtr<EnumRoleHyperTextAccessible<roles::TEXT_CONTAINER>>(
        content, document);
  }

  if (newAcc) {
    document->BindToDocument(newAcc, roleMapEntry);
  }
  return newAcc;
}

#if defined(ANDROID)
#  include "mozilla/Maybe.h"
#  include "mozilla/Monitor.h"

constinit static Maybe<Monitor> sAndroidMonitor;

mozilla::Monitor& nsAccessibilityService::GetAndroidMonitor() {
  if (!sAndroidMonitor.isSome()) {
    sAndroidMonitor.emplace("nsAccessibility::sAndroidMonitor");
  }

  return *sAndroidMonitor;
}
#endif

////////////////////////////////////////////////////////////////////////////////
// nsAccessibilityService private

bool nsAccessibilityService::Init(uint64_t aCacheDomains, uint32_t aConsumer) {
  AUTO_PROFILER_MARKER_UNTYPED("nsAccessibilityService::Init", A11Y, {});
  // DO NOT ADD CODE ABOVE HERE: THIS CODE IS MEASURING TIMINGS.
  PerfStats::AutoMetricRecording<
      PerfStats::Metric::A11Y_AccessibilityServiceInit>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  // Initialize accessible document manager.
  if (!DocManager::Init()) return false;

  // Add observers.
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) return false;

  observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);

  // Subscribe to EventListenerService.
  nsCOMPtr<nsIEventListenerService> eventListenerService =
      do_GetService("@mozilla.org/eventlistenerservice;1");
  if (!eventListenerService) return false;

  eventListenerService->AddListenerChangeListener(this);

  for (const auto& info : sHTMLMarkupMapList) {
    mHTMLMarkupMap.InsertOrUpdate(info.tag, &info);
  }
  for (const auto& info : sMathMLMarkupMapList) {
    mMathMLMarkupMap.InsertOrUpdate(info.tag, &info);
  }

  for (const auto& info : sXULMarkupMapList) {
    mXULMarkupMap.InsertOrUpdate(info.tag, &info);
  }

#ifdef A11Y_LOG
  logging::CheckEnv();
#endif

  gAccessibilityService = this;

  if (XRE_IsParentProcess()) {
    gApplicationAccessible = MakeRefPtr<ApplicationAccessibleWrap>();
  } else {
    gApplicationAccessible = MakeRefPtr<ApplicationAccessible>();
  }

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::Accessibility, "Active");

  if (aConsumer == ePdfOutput) {
    // When running purely for PDF output, we don't force creation of
    // DocAccessibles for existing documents (which would affect unrelated
    // documents) or initialize platform AT APIs (which have no clients in this
    // mode). PDF output also uses doc-specific cache domains.
    gCacheDomains = aCacheDomains;
  } else {
    FullInit(aCacheDomains, aConsumer);
  }

  // We deliberately fire the a11y init notification even for ePdfOutput so that
  // tests know that the accessibility service has started in all cases. We use
  // a different value to differentiate this from full init notifications. Note
  // that gConsumers hasn't been set yet, so we can't use IsOnlyForPdfOutput()
  // here.
  observerService->NotifyObservers(nullptr, "a11y-init-or-shutdown",
                                   aConsumer == ePdfOutput ? u"pdf" : u"1");

  return true;
}

void nsAccessibilityService::FullInit(uint64_t aCacheDomains,
                                      uint32_t aConsumer) {
  gApplicationAccessible->CreateInitialDocs();
  if (XRE_IsParentProcess()) {
    PlatformInit();
  }
  if (XRE_IsParentProcess() &&
      StaticPrefs::accessibility_enable_all_cache_domains_AtStartup()) {
    // We might be in a test environment where we need to have all cache domains
    // enabled (e.g., fuzzing).
    gCacheDomains = CacheDomain::All;
  } else if (aConsumer == eXPCOM) {
    // When instantiated via XPCOM, cache all accessibility information.
    gCacheDomains = CacheDomain::All;
  } else {
    // Set the active accessibility cache domains. We might want to modify the
    // domains that we activate based on information about the instantiator.
    gCacheDomains = ::GetCacheDomainsForKnownClients(aCacheDomains);
  }
}

void nsAccessibilityService::PromoteFromPdfOutput(uint64_t aCacheDomains,
                                                  uint32_t aConsumer) {
  // Called by GetOrCreateAccService when the service was previously brought
  // up only for ePdfOutput and a real consumer has just been added. Runs the
  // init work that Init skipped for the original ePdfOutput-only consumer.
  // The new consumer is already recorded in gConsumers by this point so the
  // init notification below sees the post-promote state.
  FullInit(aCacheDomains, aConsumer);

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(nullptr, "a11y-init-or-shutdown", u"1");
  }
}

void nsAccessibilityService::Shutdown() {
  // Application is going to be closed, shutdown accessibility and mark
  // accessibility service as shutdown to prevent calls of its methods.
  // Don't null accessibility service static member at this point to be safe
  // if someone will try to operate with it.

  MOZ_ASSERT(gConsumers, "Accessibility was shutdown already");
  UnsetConsumers(eXPCOM | eMainProcess | ePlatformAPI | ePdfOutput);

  // Remove observers.
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }

  // Stop accessible document loader.
  DocManager::Shutdown();

  SelectionManager::Shutdown();

  if (XRE_IsParentProcess()) PlatformShutdown();

  gApplicationAccessible->Shutdown();
  gApplicationAccessible = nullptr;
  gXPCApplicationAccessible = nullptr;

#if defined(ANDROID)
  // Don't allow the service to shut down while an a11y request is being handled
  // in the UI thread, as the request may depend on state from the service.
  MonitorAutoLock mal(GetAndroidMonitor());
#endif
  gAccessibilityService = nullptr;

  if (observerService) {
    static const char16_t kShutdownIndicator[] = {'0', 0};
    observerService->NotifyObservers(nullptr, "a11y-init-or-shutdown",
                                     kShutdownIndicator);
  }
}

already_AddRefed<LocalAccessible>
nsAccessibilityService::CreateAccessibleByFrameType(nsIFrame* aFrame,
                                                    nsIContent* aContent,
                                                    LocalAccessible* aContext) {
  DocAccessible* document = aContext->Document();

  RefPtr<LocalAccessible> newAcc;
  switch (aFrame->AccessibleType()) {
    case eNoType:
      return nullptr;
    case eHTMLBRType:
      newAcc = MakeRefPtr<HTMLBRAccessible>(aContent, document);
      break;
    case eHTMLButtonType:
      newAcc = MakeRefPtr<HTMLButtonAccessible>(aContent, document);
      break;
    case eHTMLCanvasType:
      newAcc = MakeRefPtr<HTMLCanvasAccessible>(aContent, document);
      break;
    case eHTMLCaptionType:
      if (aContext->IsTable() &&
          aContext->GetContent() == aContent->GetParent()) {
        newAcc = MakeRefPtr<HTMLCaptionAccessible>(aContent, document);
      }
      break;
    case eHTMLCheckboxType:
      newAcc = MakeRefPtr<CheckboxAccessible>(aContent, document);
      break;
    case eHTMLComboboxType:
      newAcc = MakeRefPtr<HTMLComboboxAccessible>(aContent, document);
      break;
    case eHTMLFileInputType:
      newAcc = MakeRefPtr<HTMLFileInputAccessible>(aContent, document);
      break;
    case eHTMLGroupboxType:
      newAcc = MakeRefPtr<HTMLGroupboxAccessible>(aContent, document);
      break;
    case eHTMLHRType:
      newAcc = MakeRefPtr<HTMLHRAccessible>(aContent, document);
      break;
    case eHTMLImageMapType:
      newAcc = MakeRefPtr<HTMLImageMapAccessible>(aContent, document);
      break;
    case eHTMLLiType:
      if (aContext->IsList() &&
          aContext->GetContent() == aContent->GetParent()) {
        newAcc = MakeRefPtr<HTMLLIAccessible>(aContent, document);
      } else {
        // Otherwise create a generic text accessible to avoid text jamming.
        newAcc = MakeRefPtr<HyperTextAccessible>(aContent, document);
      }
      break;
    case eHTMLSelectListType:
      newAcc = MakeRefPtr<HTMLSelectListAccessible>(aContent, document);
      break;
    case eHTMLMediaType:
      // The video Accessible can have TextLeafAccessibles as direct children;
      // e.g. if there are captions. Therefore, it must be a
      // HyperTextAccessible.
      newAcc = MakeRefPtr<EnumRoleHyperTextAccessible<roles::GROUPING>>(
          aContent, document);
      break;
    case eHTMLRadioButtonType:
      newAcc = MakeRefPtr<HTMLRadioButtonAccessible>(aContent, document);
      break;
    case eHTMLRangeType:
      newAcc = MakeRefPtr<HTMLRangeAccessible>(aContent, document);
      break;
    case eHTMLSpinnerType:
      newAcc = MakeRefPtr<HTMLSpinnerAccessible>(aContent, document);
      break;
    case eHTMLTableType:
    case eHTMLTableCellType:
      // We handle markup and ARIA tables elsewhere. If we reach here, this is
      // a CSS table part. Just create a generic text container.
      newAcc = MakeRefPtr<HyperTextAccessible>(aContent, document);
      break;
    case eHTMLTableRowType:
      // This is a CSS table row. Don't expose it at all.
      break;
    case eHTMLTextFieldType:
      newAcc = MakeRefPtr<HTMLTextFieldAccessible>(aContent, document);
      break;
    case eHyperTextType: {
      if (aContext->IsTable() || aContext->IsTableRow()) {
        // This is some generic hyperText, for example a block frame element
        // inserted between a table and table row. Treat it as presentational.
        return nullptr;
      }

      if (!aContent->IsAnyOfHTMLElements(nsGkAtoms::dt, nsGkAtoms::dd,
                                         nsGkAtoms::div, nsGkAtoms::thead,
                                         nsGkAtoms::tfoot, nsGkAtoms::tbody)) {
        newAcc = MakeRefPtr<HyperTextAccessible>(aContent, document);
      }
      break;
    }
    case eImageType:
      if (aContent->IsElement() &&
          ShouldCreateImgAccessible(aContent->AsElement(), document)) {
        newAcc = MakeRefPtr<ImageAccessible>(aContent, document);
      }
      break;
    case eOuterDocType:
      newAcc = MakeRefPtr<OuterDocAccessible>(aContent, document);
      break;
    case eTextLeafType:
      newAcc = MakeRefPtr<TextLeafAccessible>(aContent, document);
      break;
    default:
      MOZ_ASSERT(false);
      break;
  }

  return newAcc.forget();
}

void nsAccessibilityService::MarkupAttributes(
    Accessible* aAcc, AccAttributes* aAttributes) const {
  const mozilla::a11y::MarkupMapInfo* markupMap = GetMarkupMapInfoFor(aAcc);
  if (!markupMap) return;

  dom::Element* el = aAcc->IsLocal() ? aAcc->AsLocal()->Elm() : nullptr;
  for (uint32_t i = 0; i < std::size(markupMap->attrs); i++) {
    const MarkupAttrInfo* info = markupMap->attrs + i;
    if (!info->name) break;

    if (info->DOMAttrName) {
      if (!el) {
        // XXX Expose DOM attributes for cached RemoteAccessibles.
        continue;
      }
      if (info->DOMAttrValue) {
        if (el->AttrValueIs(kNameSpaceID_None, info->DOMAttrName,
                            info->DOMAttrValue, eCaseMatters)) {
          aAttributes->SetAttribute(info->name, info->DOMAttrValue);
        }
        continue;
      }

      nsString value;
      el->GetAttr(info->DOMAttrName, value);

      if (!value.IsEmpty()) {
        aAttributes->SetAttribute(info->name, std::move(value));
      }

      continue;
    }

    aAttributes->SetAttribute(info->name, info->value);
  }
}

LocalAccessible* nsAccessibilityService::AddNativeRootAccessible(
    void* aAtkAccessible) {
#ifdef MOZ_ACCESSIBILITY_ATK
  ApplicationAccessible* applicationAcc = ApplicationAcc();
  if (!applicationAcc) return nullptr;

  GtkWindowAccessible* nativeWnd =
      new GtkWindowAccessible(static_cast<AtkObject*>(aAtkAccessible));

  if (applicationAcc->AppendChild(nativeWnd)) return nativeWnd;
#endif

  return nullptr;
}

void nsAccessibilityService::RemoveNativeRootAccessible(
    LocalAccessible* aAccessible) {
#ifdef MOZ_ACCESSIBILITY_ATK
  ApplicationAccessible* applicationAcc = ApplicationAcc();

  if (applicationAcc) applicationAcc->RemoveChild(aAccessible);
#endif
}

bool nsAccessibilityService::HasAccessible(nsINode* aDOMNode) {
  if (!aDOMNode) return false;

  Document* document = aDOMNode->OwnerDoc();
  if (!document) return false;

  DocAccessible* docAcc = GetExistingDocAccessible(aDOMNode->OwnerDoc());
  if (!docAcc) return false;

  return docAcc->HasAccessible(aDOMNode);
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessibilityService private (DON'T put methods here)

void nsAccessibilityService::SetConsumers(uint32_t aConsumers, bool aNotify) {
  if (gConsumers & aConsumers) {
    return;
  }

  gConsumers |= aConsumers;
  if (aNotify) {
    NotifyOfConsumersChange();
  }
}

void nsAccessibilityService::UnsetConsumers(uint32_t aConsumers) {
  if (!(gConsumers & aConsumers)) {
    return;
  }

  gConsumers &= ~aConsumers;
  NotifyOfConsumersChange();
}

void nsAccessibilityService::GetConsumers(nsAString& aString) {
  const char16_t* kJSONFmt =
      u"{ \"XPCOM\": %s, \"MainProcess\": %s, \"PlatformAPI\": %s }";
  nsString json;
  nsTextFormatter::ssprintf(json, kJSONFmt,
                            gConsumers & eXPCOM ? "true" : "false",
                            gConsumers & eMainProcess ? "true" : "false",
                            gConsumers & ePlatformAPI ? "true" : "false");
  aString.Assign(json);
}

void nsAccessibilityService::SetCacheDomains(uint64_t aCacheDomains) {
  if (XRE_IsParentProcess()) {
    const DebugOnly<bool> requestSent =
        SendCacheDomainRequestToAllContentProcesses(aCacheDomains);
    MOZ_ASSERT(requestSent,
               "Could not send cache domain request to content processes.");
    gCacheDomains = aCacheDomains;
    return;
  }

  // Bail out if we're not a content process.
  if (!XRE_IsContentProcess()) {
    return;
  }

  // Anything not enabled already but enabled now is a newly-enabled domain.
  const uint64_t newDomains = ~gCacheDomains & aCacheDomains;

  // Queue cache updates on all accessibles in all documents within this
  // process.
  if (newDomains != CacheDomain::None) {
    for (const RefPtr<DocAccessible>& doc : mDocAccessibleCache.Values()) {
      MOZ_ASSERT(doc, "DocAccessible in cache is null!");
      if (doc->EffectiveCacheDomains() != gCacheDomains) {
        // This document uses a specific set of cache domains rather than the
        // global set. Thus, a change to the global set shouldn't impact this
        // document.
        MOZ_ASSERT(doc->IsPrintDoc());
        continue;
      }
      doc->QueueCacheUpdate(doc.get(), newDomains, true);
      Pivot pivot(doc.get());
      LocalAccInSameDocRule rule;
      for (Accessible* anchor = doc.get(); anchor;
           anchor = pivot.Next(anchor, rule)) {
        LocalAccessible* acc = anchor->AsLocal();

        // Note: Queueing changes for domains that aren't yet active. The
        // domains will become active at the end of the function.
        doc->QueueCacheUpdate(acc, newDomains, true);
      }
      // Process queued cache updates immediately.
      doc->ProcessQueuedCacheUpdates(newDomains);
    }
  }

  gCacheDomains = aCacheDomains;
}

void nsAccessibilityService::NotifyOfConsumersChange() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (!observerService) {
    return;
  }

  nsAutoString consumers;
  GetConsumers(consumers);
  observerService->NotifyObservers(nullptr, "a11y-consumers-changed",
                                   consumers.get());
}

const mozilla::a11y::MarkupMapInfo* nsAccessibilityService::GetMarkupMapInfoFor(
    Accessible* aAcc) const {
  if (LocalAccessible* localAcc = aAcc->AsLocal()) {
    return localAcc->HasOwnContent()
               ? GetMarkupMapInfoFor(localAcc->GetContent())
               : nullptr;
  }
  // XXX For now, we assume all RemoteAccessibles are HTML elements. This
  // isn't strictly correct, but as far as current callers are concerned,
  // this doesn't matter. If that changes in future, we could expose the
  // element type via AccGenericType.
  return mHTMLMarkupMap.Get(aAcc->TagName());
}

nsAccessibilityService* GetOrCreateAccService(uint32_t aNewConsumer,
                                              uint64_t aCacheDomains) {
  // Do not initialize accessibility if it is force disabled.
  if (PlatformDisabledState() == ePlatformIsDisabled) {
    return nullptr;
  }

  if (!nsAccessibilityService::gAccessibilityService) {
    RefPtr<nsAccessibilityService> service = new nsAccessibilityService();
    if (!service->Init(aCacheDomains, aNewConsumer)) {
      service->Shutdown();
      return nullptr;
    }
  }

  MOZ_ASSERT(nsAccessibilityService::gAccessibilityService,
             "LocalAccessible service is not initialized.");
  // If the service was previously brought up only for tagged PDF output and a
  // real consumer is arriving now, finish the init work that was deferred.
  bool wasOnlyForPdfOutput = nsAccessibilityService::IsOnlyForPdfOutput();
  nsAccessibilityService::gAccessibilityService->SetConsumers(aNewConsumer);
  if (wasOnlyForPdfOutput &&
      aNewConsumer != nsAccessibilityService::ePdfOutput) {
    nsAccessibilityService::gAccessibilityService->PromoteFromPdfOutput(
        aCacheDomains, aNewConsumer);
  }
  return nsAccessibilityService::gAccessibilityService;
}

void MaybeShutdownAccService(uint32_t aFormerConsumer, bool aAsync) {
  nsAccessibilityService* accService =
      nsAccessibilityService::gAccessibilityService;

  if (!accService || nsAccessibilityService::IsShutdown()) {
    return;
  }

  // Check if we're still being used by XPCOM. However, when running purely for
  // PDF output, we ignore event observers so that tests can assert that
  // unexpected events aren't fired without unintentionally preventing the
  // service from shutting down.
  if ((!nsAccessibilityService::IsOnlyForPdfOutput() &&
       nsCoreUtils::AccEventObserversExist()) ||
      xpcAccessibilityService::IsInUse() || accService->HasXPCDocuments()) {
    // In case the XPCOM flag was unset (possibly because of the shutdown
    // timer in the xpcAccessibilityService) ensure it is still present. Note:
    // this should be fixed when all the consumer logic is taken out as a
    // separate class.
    accService->SetConsumers(nsAccessibilityService::eXPCOM, false);

    if (aFormerConsumer != nsAccessibilityService::eXPCOM) {
      // Only unset non-XPCOM consumers.
      accService->UnsetConsumers(aFormerConsumer);
    }
    return;
  }

  if (nsAccessibilityService::gConsumers & ~aFormerConsumer) {
    // There are still other consumers of the accessibility service, so we
    // can't shut down.
    accService->UnsetConsumers(aFormerConsumer);
    return;
  }

  if (!aAsync) {
    accService
        ->Shutdown();  // Will unset all nsAccessibilityService::gConsumers
    return;
  }

  static bool sIsPending = false;
  if (sIsPending) {
    // An async shutdown runnable is pending. Don't dispatch another.
    return;
  }
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "a11y::MaybeShutdownAccService", [aFormerConsumer]() {
        // It's possible (albeit very unlikely) that another accessibility
        // service consumer arrived since this runnable was dispatched. Use
        // MaybeShutdownAccService to be safe.
        MaybeShutdownAccService(aFormerConsumer, false);
        sIsPending = false;
      }));
  sIsPending = true;
}

////////////////////////////////////////////////////////////////////////////////
// Services
////////////////////////////////////////////////////////////////////////////////

namespace mozilla {
namespace a11y {

FocusManager* FocusMgr() {
  return nsAccessibilityService::gAccessibilityService;
}

SelectionManager* SelectionMgr() {
  return nsAccessibilityService::gAccessibilityService;
}

ApplicationAccessible* ApplicationAcc() {
  return nsAccessibilityService::gApplicationAccessible;
}

xpcAccessibleApplication* XPCApplicationAcc() {
  if (!nsAccessibilityService::gXPCApplicationAccessible &&
      nsAccessibilityService::gApplicationAccessible) {
    nsAccessibilityService::gXPCApplicationAccessible =
        MakeRefPtr<xpcAccessibleApplication>(
            nsAccessibilityService::gApplicationAccessible);
  }

  return nsAccessibilityService::gXPCApplicationAccessible;
}

EPlatformDisabledState PlatformDisabledState() {
  static bool platformDisabledStateCached = false;
  if (platformDisabledStateCached) {
    return static_cast<EPlatformDisabledState>(sPlatformDisabledState);
  }

  platformDisabledStateCached = true;
  Preferences::RegisterCallback(PrefChanged, PREF_ACCESSIBILITY_FORCE_DISABLED);
  return ReadPlatformDisabledState();
}

void MaybeStartForceEnabled(bool aAsync) {
  if (!XRE_IsParentProcess()) {
    // Accessibility in content processes is driven by the parent process.
    return;
  }
  // This also ensures the pref is being watched, so a later change to force
  // enabled starts the service even if it wasn't already force enabled here.
  if (PlatformDisabledState() != ePlatformIsForceEnabled) {
    return;
  }
  if (GetAccService()) {
    return;
  }
  if (!aAsync) {
    GetOrCreateAccService(nsAccessibilityService::ePlatformAPI);
    return;
  }
  static bool sIsPending = false;
  if (sIsPending) {
    // An async start runnable is pending. Don't dispatch another.
    return;
  }
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("a11y::MaybeStartForceEnabled", [] {
        // It's possible (albeit unlikely) that the pref changed again since
        // this runnable was dispatched, or that something else already
        // started the service. Use MaybeStartForceEnabled to be safe.
        MaybeStartForceEnabled(false);
        sIsPending = false;
      }));
  sIsPending = true;
}

EPlatformDisabledState ReadPlatformDisabledState() {
  sPlatformDisabledState =
      Preferences::GetInt(PREF_ACCESSIBILITY_FORCE_DISABLED, 0);
  if (sPlatformDisabledState < ePlatformIsForceEnabled) {
    sPlatformDisabledState = ePlatformIsForceEnabled;
  } else if (sPlatformDisabledState > ePlatformIsDisabled) {
    sPlatformDisabledState = ePlatformIsDisabled;
  }

  return static_cast<EPlatformDisabledState>(sPlatformDisabledState);
}

void PrefChanged(const char* /*aPref*/, void* /*aClosure*/) {
  EPlatformDisabledState disabledState = ReadPlatformDisabledState();
  if (disabledState == ePlatformIsDisabled) {
    // Force shut down accessibility.
    nsAccessibilityService* accService =
        nsAccessibilityService::gAccessibilityService;
    if (accService && !nsAccessibilityService::IsShutdown()) {
      accService->Shutdown();
    }
  } else if (disabledState == ePlatformIsForceEnabled) {
    // Start accessibility asynchronously; this callback runs synchronously
    // wherever the pref was set (e.g. from about:config, a test harness or
    // enterprise policy), and starting the service reentrantly from there
    // would pull in a lot of other initialization mid-call.
    MaybeStartForceEnabled(/* aAsync */ true);
  }
}

uint32_t CacheDomainActivationBlocker::sEntryCount = 0;

CacheDomainActivationBlocker::CacheDomainActivationBlocker() {
  AssertIsOnMainThread();
  if (sEntryCount++ != 0) {
    // We're re-entering. This can happen if an earlier event (even in a
    // different document) ends up calling an XUL method, since that can run
    // script which can cause other events to fire. Only the outermost usage
    // should change the flag.
    return;
  }
  if (nsAccessibilityService* service = GetAccService()) {
    MOZ_ASSERT(service->mShouldAllowNewCacheDomains);
    service->mShouldAllowNewCacheDomains = false;
  }
}

CacheDomainActivationBlocker::~CacheDomainActivationBlocker() {
  AssertIsOnMainThread();
  if (--sEntryCount != 0) {
    // Only the outermost usage should change the flag.
    return;
  }
  if (nsAccessibilityService* service = GetAccService()) {
    MOZ_ASSERT(!service->mShouldAllowNewCacheDomains);
    service->mShouldAllowNewCacheDomains = true;
  }
}

}  // namespace a11y
}  // namespace mozilla
