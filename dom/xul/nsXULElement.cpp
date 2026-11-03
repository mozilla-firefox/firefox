/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULElement.h"

#include <new>
#include <utility>

#include "AttrArray.h"
#include "MainThreadUtils.h"
#include "ReferrerInfo.h"
#include "Units.h"
#include "XULButtonElement.h"
#include "XULFrameElement.h"
#include "XULMenuBarElement.h"
#include "XULMenuElement.h"
#include "XULPopupElement.h"
#include "XULResizerElement.h"
#include "XULTextElement.h"
#include "XULTooltipElement.h"
#include "XULTreeElement.h"
#include "js/CompilationAndEvaluation.h"
#include "js/CompileOptions.h"  // JS::CompileOptions, JS::OwningCompileOptions, , JS::ReadOnlyCompileOptions, JS::ReadOnlyDecodeOptions, JS::DecodeOptions
#include "js/SourceText.h"
#include "js/Transcoding.h"
#include "js/Utility.h"
#include "js/experimental/CompileScript.h"  // JS::NewFrontendContext, JS::DestroyFrontendContext, JS::SetNativeStackQuota, JS::ThreadStackQuotaForSize, JS::CompileGlobalScriptToStencil, JS::CompilationStorage
#include "js/experimental/JSStencil.h"      // JS::Stencil, JS::FrontendContext
#include "jsapi.h"
#include "mozilla/ArrayIterator.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventQueue.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FlushType.h"
#include "mozilla/FocusModel.h"
#include "mozilla/GlobalKeyListener.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ShutdownPhase.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskController.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/BorrowedAttrInfo.h"
#include "mozilla/dom/CSSRuleBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/ReferrerPolicyBinding.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/UnbindContext.h"
#include "mozilla/dom/XULBroadcastManager.h"
#include "mozilla/dom/XULCommandEvent.h"
#include "mozilla/dom/XULElementBinding.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/fallible.h"
#include "nsAtom.h"
#include "nsAttrValueInlines.h"
#include "nsCOMPtr.h"
#include "nsCaseTreatment.h"
#include "nsChangeHint.h"
#include "nsCompatibility.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionNoteChild.h"
#include "nsCycleCollectionTraversalCallback.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIControllers.h"
#include "nsID.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMXULControlElement.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDocShell.h"
#include "nsIFocusManager.h"
#include "nsIFrame.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIRunnable.h"
#include "nsIScriptContext.h"
#include "nsISupportsUtils.h"
#include "nsIURI.h"
#include "nsIURIWithSizeOf.h"
#include "nsIXPConnect.h"
#include "nsMenuPopupFrame.h"
#include "nsNodeInfoManager.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPresContext.h"
#include "nsQueryFrame.h"
#include "nsString.h"
#include "nsStyledElement.h"
#include "nsThreadUtils.h"
#include "nsXULControllers.h"
#include "nsXULPopupListener.h"
#include "nsXULPopupManager.h"
#include "nsXULPrototypeCache.h"
#include "nsXULTooltipListener.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
uint32_t nsXULPrototypeAttribute::gNumElements;
uint32_t nsXULPrototypeAttribute::gNumAttributes;
uint32_t nsXULPrototypeAttribute::gNumCacheTests;
uint32_t nsXULPrototypeAttribute::gNumCacheHits;
uint32_t nsXULPrototypeAttribute::gNumCacheSets;
uint32_t nsXULPrototypeAttribute::gNumCacheFills;
#endif

#define NS_DISPATCH_XUL_COMMAND (1 << 0)

//----------------------------------------------------------------------
// nsXULElement
//

nsXULElement::nsXULElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsStyledElement(std::move(aNodeInfo)) {
  XUL_PROTOTYPE_ATTRIBUTE_METER(gNumElements);
}

nsXULElement::~nsXULElement() = default;

/* static */
nsXULElement* NS_NewBasicXULElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(std::move(aNodeInfo));
  auto* nim = nodeInfo->NodeInfoManager();
  return new (nim) nsXULElement(nodeInfo.forget());
}

/* static */
nsXULElement* nsXULElement::Construct(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo) {
  // NOTE: If you add elements here, you probably also want to change
  // mozilla::dom::binding_detail::HTMLConstructor in BindingUtils.cpp to take
  // them into account, otherwise you'll start getting "Illegal constructor"
  // exceptions in chrome code.
  RefPtr<mozilla::dom::NodeInfo> nodeInfo = aNodeInfo;
  if (nodeInfo->Equals(nsGkAtoms::resizer)) {
    return NS_NewXULResizerElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::label) ||
      nodeInfo->Equals(nsGkAtoms::description)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULTextElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::menupopup) ||
      nodeInfo->Equals(nsGkAtoms::panel)) {
    return NS_NewXULPopupElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::tooltip)) {
    return NS_NewXULTooltipElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::iframe) ||
      nodeInfo->Equals(nsGkAtoms::browser) ||
      nodeInfo->Equals(nsGkAtoms::editor)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULFrameElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::menubar)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULMenuBarElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::menu) ||
      nodeInfo->Equals(nsGkAtoms::menulist)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULMenuElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::tree)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULTreeElement(nodeInfo.forget());
  }

  if (nodeInfo->Equals(nsGkAtoms::checkbox) ||
      nodeInfo->Equals(nsGkAtoms::radio) ||
      nodeInfo->Equals(nsGkAtoms::thumb) ||
      nodeInfo->Equals(nsGkAtoms::button) ||
      nodeInfo->Equals(nsGkAtoms::menuitem) ||
      nodeInfo->Equals(nsGkAtoms::richlistitem) ||
      nodeInfo->Equals(nsGkAtoms::toolbarbutton) ||
      nodeInfo->Equals(nsGkAtoms::toolbarpaletteitem) ||
      nodeInfo->Equals(nsGkAtoms::scrollbarbutton)) {
    auto* nim = nodeInfo->NodeInfoManager();
    return new (nim) XULButtonElement(nodeInfo.forget());
  }

  return NS_NewBasicXULElement(nodeInfo.forget());
}

/* static */
already_AddRefed<Element> nsXULElement::CreateFromPrototype(
    nsXULPrototypeElement* aPrototype, Document* aDocument, bool aIsRoot) {
  mozilla::dom::NodeInfo* ni = aPrototype->mNodeInfo;
  RefPtr<mozilla::dom::NodeInfo> nodeInfo =
      aDocument->NodeInfoManager()->GetNodeInfo(
          ni->NameAtom(), ni->GetPrefixAtom(), ni->NamespaceID(), ELEMENT_NODE);

  nsCOMPtr<Element> baseElement;
  NS_NewXULElement(getter_AddRefs(baseElement), nodeInfo.forget(),
                   dom::FROM_PARSER_NETWORK, aPrototype->mIsAtom);
  if (!baseElement) {
    return nullptr;
  }

  nsXULElement* element = FromNode(baseElement);
  element->MakeHeavyweight(aPrototype);
  return baseElement.forget();
}

nsresult NS_NewXULElement(Element** aResult,
                          already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
                          FromParser aFromParser, nsAtom* aIsAtom,
                          mozilla::dom::CustomElementDefinition* aDefinition) {
  return NS_NewXULElement(aResult, std::move(aNodeInfo), aFromParser, aIsAtom,
                          aDefinition, Nothing());
}

nsresult NS_NewXULElement(
    Element** aResult, already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
    FromParser aFromParser, nsAtom* aIsAtom,
    mozilla::dom::CustomElementDefinition* aDefinition,
    Maybe<RefPtr<CustomElementRegistry>> aCustomElementRegistry) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo = aNodeInfo;

  MOZ_ASSERT(nodeInfo, "need nodeinfo for non-proto Create");

  NS_ASSERTION(
      nodeInfo->NamespaceEquals(kNameSpaceID_XUL),
      "Trying to create XUL elements that don't have the XUL namespace");

  Document* doc = nodeInfo->GetDocument();
  if (doc && !doc->AllowXULXBL()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return nsContentUtils::NewXULOrHTMLElement(aResult, nodeInfo, aFromParser,
                                             aIsAtom, aDefinition,
                                             std::move(aCustomElementRegistry));
}

void NS_TrustedNewXULElement(
    Element** aResult, already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo) {
  RefPtr<mozilla::dom::NodeInfo> ni = aNodeInfo;
  MOZ_ASSERT(ni, "need nodeinfo for non-proto Create");

  // Create an nsXULElement with the specified namespace and tag.
  NS_ADDREF(*aResult = nsXULElement::Construct(ni.forget()));
}

//----------------------------------------------------------------------
// nsISupports interface

NS_IMPL_CYCLE_COLLECTION_INHERITED(nsXULElement, nsStyledElement)

NS_IMPL_ADDREF_INHERITED(nsXULElement, nsStyledElement)
NS_IMPL_RELEASE_INHERITED(nsXULElement, nsStyledElement)

NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(nsXULElement)
  NS_ELEMENT_INTERFACE_TABLE_TO_MAP_SEGUE
NS_INTERFACE_MAP_END_INHERITING(nsStyledElement)

//----------------------------------------------------------------------
// nsINode interface

nsresult nsXULElement::Clone(mozilla::dom::NodeInfo* aNodeInfo,
                             nsINode** aResult) const {
  *aResult = nullptr;
  RefPtr<nsXULElement> element = Construct(do_AddRef(aNodeInfo));
  MOZ_TRY(const_cast<nsXULElement*>(this)->CopyInnerTo(element));
  // Note that we're _not_ copying mControllers.
  element.forget(aResult);
  return NS_OK;
}

//----------------------------------------------------------------------

EventListenerManager* nsXULElement::GetEventListenerManagerForAttr(
    nsAtom* aAttrName) {
  // XXXbz sXBL/XBL2 issue: should we instead use GetComposedDoc()
  // here, override BindToTree for those classes and munge event
  // listeners there?
  Document* doc = OwnerDoc();

  nsPIDOMWindowInner* window;
  Element* root = doc->GetRootElement();
  if ((!root || root == this) && (window = doc->GetInnerWindow())) {
    nsCOMPtr<EventTarget> piTarget = do_QueryInterface(window);

    return piTarget->GetOrCreateListenerManager();
  }

  return nsStyledElement::GetEventListenerManagerForAttr(aAttrName);
}

// returns true if the element is not a list
static bool IsNonList(mozilla::dom::NodeInfo* aNodeInfo) {
  return !aNodeInfo->Equals(nsGkAtoms::tree) &&
         !aNodeInfo->Equals(nsGkAtoms::richlistbox);
}

nsXULElement::XULFocusability nsXULElement::GetXULFocusability(
    IsFocusableFlags aFlags) {
#ifdef XP_MACOSX
  // On Mac, mouse interactions only focus the element if it's a list,
  // or if it's a remote target, since the remote target must handle
  // the focus.
  if ((aFlags & IsFocusableFlags::WithMouse) && IsNonList(mNodeInfo) &&
      !EventStateManager::IsTopLevelRemoteTarget(this)) {
    return XULFocusability::NeverFocusable();
  }
#endif

  XULFocusability result;
  nsCOMPtr<nsIDOMXULControlElement> xulControl = AsXULControl();
  if (xulControl) {
    // A disabled element cannot be focused and is not part of the tab order
    bool disabled;
    xulControl->GetDisabled(&disabled);
    if (disabled && !(aFlags & IsFocusableFlags::IgnoreDisabled)) {
      return XULFocusability::NeverFocusable();
    }
    result.mDefaultFocusable = true;
  }
  if (Maybe<int32_t> attrVal = GetTabIndexAttrValue()) {
    // The tabindex attribute was specified, so the element becomes
    // focusable.
    result.mDefaultFocusable = true;
    result.mForcedFocusable.emplace(true);
    result.mForcedTabIndexIfFocusable.emplace(attrVal.value());
  }
  if (xulControl && FocusModel::AppliesToXUL() &&
      !FocusModel::IsTabFocusable(TabFocusableType::FormElements) &&
      IsNonList(mNodeInfo)) {
    // By default, the tab focus model doesn't apply to xul element on any
    // system but OS X. For compatibility, we only do this for controls,
    // otherwise elements like <browser> cannot take this focus.
    result.mForcedTabIndexIfFocusable = Some(-1);
  }
  return result;
}

// XUL elements are not focusable unless explicitly opted-into it with
// -moz-user-focus: normal, or the tabindex attribute.
Focusable nsXULElement::IsFocusableWithoutStyle(IsFocusableFlags aFlags) {
  const auto focusability = GetXULFocusability(aFlags);
  const bool focusable = focusability.mDefaultFocusable;
  return {focusable,
          focusable ? focusability.mForcedTabIndexIfFocusable.valueOr(-1) : -1};
}

bool nsXULElement::HasMenu() {
  if (auto* button = XULButtonElement::FromNode(this)) {
    return button->IsMenu();
  }
  return false;
}

void nsXULElement::OpenMenu(bool aOpenFlag) {
  // Flush frames first. It's not clear why this is needed, see bug 1704670.
  if (Document* doc = GetComposedDoc()) {
    doc->FlushPendingNotifications(FlushType::Frames);
  }

  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return;
  }

  if (aOpenFlag) {
    // Nothing will happen if this element isn't a menu.
    pm->ShowMenu(this, false);
  } else {
    // Nothing will happen if this element isn't a menu.
    pm->HideMenu(this);
  }
}

Result<bool, nsresult> nsXULElement::PerformAccesskey(bool aKeyCausesActivation,
                                                      bool aIsTrustedEvent) {
  if (IsXULElement(nsGkAtoms::label)) {
    nsAutoString control;
    GetAttr(nsGkAtoms::control, control);
    if (control.IsEmpty()) {
      return Err(NS_ERROR_UNEXPECTED);
    }

    // XXXsmaug Should we use ShadowRoot::GetElementById in case
    //          element is in Shadow DOM?
    RefPtr<Document> document = GetUncomposedDoc();
    if (!document) {
      return Err(NS_ERROR_UNEXPECTED);
    }

    RefPtr<Element> element = document->GetElementById(control);
    if (!element) {
      return Err(NS_ERROR_UNEXPECTED);
    }

    // XXXedgar, This is mainly for HTMLElement which doesn't do visible
    // check in PerformAccesskey. We probably should always do visible
    // check on HTMLElement even if the PerformAccesskey is not redirected from
    // label XULelement per spec.
    nsIFrame* frame = element->GetPrimaryFrame();
    if (!frame || !frame->IsVisibleConsideringAncestors()) {
      return Err(NS_ERROR_UNEXPECTED);
    }

    return element->PerformAccesskey(aKeyCausesActivation, aIsTrustedEvent);
  }

  nsIFrame* frame = GetPrimaryFrame();
  if (!frame || !frame->IsVisibleConsideringAncestors()) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  bool focused = false;
  // Define behavior for each type of XUL element.
  if (!IsXULElement(nsGkAtoms::toolbarbutton)) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      RefPtr<Element> elementToFocus = this;
      // for radio buttons, focus the radiogroup instead
      if (IsXULElement(nsGkAtoms::radio)) {
        if (nsCOMPtr<nsIDOMXULSelectControlItemElement> controlItem =
                AsXULSelectControlItem()) {
          bool disabled;
          controlItem->GetDisabled(&disabled);
          if (!disabled) {
            controlItem->GetControl(getter_AddRefs(elementToFocus));
          }
        }
      }

      if (elementToFocus) {
        fm->SetFocus(elementToFocus, nsIFocusManager::FLAG_BYKEY);

        // Return true if the element became focused.
        nsPIDOMWindowOuter* window = OwnerDoc()->GetWindow();
        focused = (window && window->GetFocusedElement() == elementToFocus);
      }
    }
  }

  if (aKeyCausesActivation && !IsXULElement(nsGkAtoms::menulist)) {
    ClickWithInputSource(MouseEvent_Binding::MOZ_SOURCE_KEYBOARD,
                         aIsTrustedEvent);
    return focused;
  }

  // If the accesskey won't cause the activation and the focus isn't changed,
  // either. Return error so EventStateManager would try to find next element
  // to handle the accesskey.
  return focused ? Result<bool, nsresult>{focused} : Err(NS_ERROR_ABORT);
}

//----------------------------------------------------------------------

void nsXULElement::AddListenerForAttributeIfNeeded(nsAtom* aLocalName) {
  // If appropriate, add a popup listener and/or compile the event
  // handler. Called when we change the element's document, create a
  // new element, change an attribute's value, etc.
  // Eventlistenener-attributes are always in the null namespace.
  if (aLocalName == nsGkAtoms::menu || aLocalName == nsGkAtoms::contextmenu ||
      // XXXdwh popup and context are deprecated
      aLocalName == nsGkAtoms::popup || aLocalName == nsGkAtoms::context) {
    AddPopupListener(aLocalName);
  }
  if (nsContentUtils::IsEventAttributeName(aLocalName, EventNameType_XUL)) {
    nsAutoString value;
    GetAttr(aLocalName, value);
    SetEventHandler(aLocalName, value);
  }
}

class XULInContentErrorReporter : public Runnable {
 public:
  explicit XULInContentErrorReporter(Document& aDocument)
      : mozilla::Runnable("XULInContentErrorReporter"), mDocument(aDocument) {}

  NS_IMETHOD Run() override {
    mDocument->WarnOnceAbout(DeprecatedOperations::eImportXULIntoContent,
                             false);
    return NS_OK;
  }

 private:
  OwningNonNull<Document> mDocument;
};

static bool NeedTooltipSupport(const nsXULElement& aXULElement) {
  if (aXULElement.NodeInfo()->Equals(nsGkAtoms::treechildren)) {
    // treechildren always get tooltip support, since cropped tree cells show
    // their full text in a tooltip.
    return true;
  }

  return aXULElement.GetBoolAttr(nsGkAtoms::tooltip) ||
         aXULElement.GetBoolAttr(nsGkAtoms::tooltiptext);
}

// Makes the next key event rebuild the handlers of aKeyset, if it is a keyset,
// after one of its key elements was added, removed or changed.
static void InvalidateKeysetHandlers(nsINode* aKeyset) {
  Element* keyset = Element::FromNodeOrNull(aKeyset);
  if (keyset && keyset->IsXULElement(nsGkAtoms::keyset)) {
    XULKeySetGlobalKeyListener::InvalidateHandlers(keyset);
  }
}

nsresult nsXULElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsStyledElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!IsInComposedDoc()) {
    return rv;
  }

  Document& doc = aContext.OwnerDoc();
  if (!IsInNativeAnonymousSubtree() && !doc.AllowXULXBL() &&
      !doc.HasWarnedAbout(DeprecatedOperations::eImportXULIntoContent)) {
    nsContentUtils::AddScriptRunner(
        MakeAndAddRef<XULInContentErrorReporter>(doc));
  }

#ifdef DEBUG
  if (!doc.AllowXULXBL() && !doc.IsLoadedAsData()) {
    // To save CPU cycles and memory, we don't load xul.css for other elements
    // except scrollbars.
    //
    // This assertion makes sure no other XUL element is used in a non-XUL
    // document.
    nsAtom* tag = NodeInfo()->NameAtom();
    MOZ_ASSERT(tag == nsGkAtoms::scrollbar ||
                   tag == nsGkAtoms::scrollbarbutton ||
                   tag == nsGkAtoms::scrollcorner || tag == nsGkAtoms::slider ||
                   tag == nsGkAtoms::thumb || tag == nsGkAtoms::resizer,
               "Unexpected XUL element in non-XUL doc");
  }
#endif

  if (NodeInfo()->Equals(nsGkAtoms::keyset, kNameSpaceID_XUL)) {
    // Create our XUL key listener and hook it up.
    XULKeySetGlobalKeyListener::AttachKeyHandler(this);
  } else if (IsXULElement(nsGkAtoms::key)) {
    InvalidateKeysetHandlers(GetParentNode());
  }

  RegUnRegAccessKey(true);

  if (NeedTooltipSupport(*this)) {
    AddTooltipSupport();
  }

  if (XULBroadcastManager::MayNeedListener(*this)) {
    if (!doc.HasXULBroadcastManager()) {
      doc.InitializeXULBroadcastManager();
    }
    XULBroadcastManager* broadcastManager = doc.GetXULBroadcastManager();
    broadcastManager->AddListener(this);
  }
  return rv;
}

void nsXULElement::UnbindFromTree(UnbindContext& aContext) {
  if (NodeInfo()->Equals(nsGkAtoms::keyset, kNameSpaceID_XUL)) {
    XULKeySetGlobalKeyListener::DetachKeyHandler(this);
  } else if (IsXULElement(nsGkAtoms::key) && aContext.IsUnbindRoot(this) &&
             aContext.WasInComposedDoc()) {
    InvalidateKeysetHandlers(aContext.GetOriginalSubtreeParent());
  }

  RegUnRegAccessKey(false);

  if (NeedTooltipSupport(*this)) {
    RemoveTooltipSupport();
  }

  Document* doc = GetComposedDoc();
  if (doc && doc->HasXULBroadcastManager() &&
      XULBroadcastManager::MayNeedListener(*this)) {
    RefPtr<XULBroadcastManager> broadcastManager =
        doc->GetXULBroadcastManager();
    broadcastManager->RemoveListener(this);
  }

  // mControllers can own objects that are implemented
  // in JavaScript (such as some implementations of
  // nsIControllers.  These objects prevent their global
  // object's script object from being garbage collected,
  // which means JS continues to hold an owning reference
  // to the nsGlobalWindow, which owns the document,
  // which owns this content.  That's a cycle, so we break
  // it here.  (It might be better to break this by releasing
  // mDocument in nsGlobalWindow::SetDocShell, but I'm not
  // sure whether that would fix all possible cycles through
  // mControllers.)
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  if (slots) {
    slots->mControllers = nullptr;
  }

  nsStyledElement::UnbindFromTree(aContext);
}

void nsXULElement::DoneAddingChildren(bool aHaveNotified) {
  if (IsXULElement(nsGkAtoms::linkset)) {
    Document* doc = GetComposedDoc();
    if (doc) {
      doc->OnL10nResourceContainerParsed();
    }
  }
}

void nsXULElement::RegUnRegAccessKey(bool aDoReg) {
  // Don't try to register for unsupported elements
  if (!SupportsAccessKey()) {
    return;
  }

  nsStyledElement::RegUnRegAccessKey(aDoReg);
}

bool nsXULElement::SupportsAccessKey() const {
  if (NodeInfo()->Equals(nsGkAtoms::label) && HasAttr(nsGkAtoms::control)) {
    return true;
  }

  // XXX(ntim): check if description[value] or description[accesskey] are
  // actually used, remove `value` from {Before/After}SetAttr if not the case
  if (NodeInfo()->Equals(nsGkAtoms::description) && HasAttr(nsGkAtoms::value) &&
      HasAttr(nsGkAtoms::control)) {
    return true;
  }

  return IsAnyOfXULElements(nsGkAtoms::button, nsGkAtoms::toolbarbutton,
                            nsGkAtoms::checkbox, nsGkAtoms::tab,
                            nsGkAtoms::radio);
}

void nsXULElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                 const nsAttrValue* aValue, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::accesskey || aName == nsGkAtoms::control ||
        aName == nsGkAtoms::value) {
      RegUnRegAccessKey(false);
    } else if ((aName == nsGkAtoms::command || aName == nsGkAtoms::observes) &&
               IsInUncomposedDoc()) {
      //         XXX sXBL/XBL2 issue! Owner or current document?
      // XXX Why does this not also remove broadcast listeners if the
      // "element" attribute was changed on an <observer>?
      nsAutoString oldValue;
      GetAttr(nsGkAtoms::observes, oldValue);
      if (oldValue.IsEmpty()) {
        GetAttr(nsGkAtoms::command, oldValue);
      }
      Document* doc = GetUncomposedDoc();
      if (!oldValue.IsEmpty() && doc->HasXULBroadcastManager()) {
        RefPtr<XULBroadcastManager> broadcastManager =
            doc->GetXULBroadcastManager();
        broadcastManager->RemoveListener(this);
      }
#ifdef DEBUG
    } else if (aName == nsGkAtoms::usercontextid) {
      const nsAttrValue* oldValue = GetParsedAttr(aName);
      if (oldValue && (!aValue || !aValue->Equals(*oldValue))) {
        MOZ_ASSERT(false,
                   "Changing usercontextid doesn't really work properly.");
      }
#endif
    }
  }

  return nsStyledElement::BeforeSetAttr(aNamespaceID, aName, aValue, aNotify);
}

void nsXULElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                const nsAttrValue* aValue,
                                const nsAttrValue* aOldValue,
                                nsIPrincipal* aSubjectPrincipal, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aValue) {
      AddListenerForAttributeIfNeeded(aName);
    }

    if (aName == nsGkAtoms::accesskey || aName == nsGkAtoms::control ||
        aName == nsGkAtoms::value) {
      RegUnRegAccessKey(true);
    } else if (aName == nsGkAtoms::tooltip || aName == nsGkAtoms::tooltiptext) {
      if (!!aValue != !!aOldValue && IsInComposedDoc() &&
          !NodeInfo()->Equals(nsGkAtoms::treechildren)) {
        if (aValue) {
          AddTooltipSupport();
        } else {
          RemoveTooltipSupport();
        }
      }
    }
    Document* doc = GetComposedDoc();
    if (doc && doc->HasXULBroadcastManager()) {
      RefPtr<XULBroadcastManager> broadcastManager =
          doc->GetXULBroadcastManager();
      broadcastManager->AttributeChanged(this, aNamespaceID, aName);
    }
    if (doc && XULBroadcastManager::MayNeedListener(*this)) {
      if (!doc->HasXULBroadcastManager()) {
        doc->InitializeXULBroadcastManager();
      }
      XULBroadcastManager* broadcastManager = doc->GetXULBroadcastManager();
      broadcastManager->AddListener(this);
    }

    if (IsXULElement(nsGkAtoms::key) &&
        (aName == nsGkAtoms::key || aName == nsGkAtoms::keycode ||
         aName == nsGkAtoms::charcode || aName == nsGkAtoms::modifiers ||
         aName == nsGkAtoms::reserved || aName == nsGkAtoms::event) &&
        IsInComposedDoc()) {
      InvalidateKeysetHandlers(GetParentNode());
    }

    // XXX need to check if they're changing an event handler: if
    // so, then we need to unhook the old one.  Or something.
  }

  return nsStyledElement::AfterSetAttr(aNamespaceID, aName, aValue, aOldValue,
                                       aSubjectPrincipal, aNotify);
}

void nsXULElement::AddTooltipSupport() {
  nsXULTooltipListener* listener = nsXULTooltipListener::GetInstance();
  if (!listener) {
    return;
  }

  listener->AddTooltipSupport(this);
}

void nsXULElement::RemoveTooltipSupport() {
  nsXULTooltipListener* listener = nsXULTooltipListener::GetInstance();
  if (!listener) {
    return;
  }

  listener->RemoveTooltipSupport(this);
}

bool nsXULElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                  const nsAString& aValue,
                                  nsIPrincipal* aMaybeScriptedPrincipal,
                                  nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::tabindex) {
    return aResult.ParseIntValue(aValue);
  }
  return nsStyledElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                         aMaybeScriptedPrincipal, aResult);
}

void nsXULElement::DestroyContent() {
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  if (slots) {
    slots->mControllers = nullptr;
  }

  nsStyledElement::DestroyContent();
}

#ifdef MOZ_DOM_LIST
void nsXULElement::List(FILE* out, int32_t aIndent) const {
  nsCString prefix("XUL");
  if (HasSlots()) {
    prefix.Append('*');
  }
  prefix.Append(' ');

  nsStyledElement::List(out, aIndent, prefix);
}
#endif

bool nsXULElement::IsEventStoppedFromAnonymousScrollbar(EventMessage aMessage) {
  return (IsRootOfNativeAnonymousSubtree() &&
          IsAnyOfXULElements(nsGkAtoms::scrollbar, nsGkAtoms::scrollcorner) &&
          (aMessage == ePointerClick || aMessage == eMouseDoubleClick ||
           aMessage == eCommand || aMessage == eContextMenu ||
           aMessage == eDragStart || aMessage == ePointerAuxClick));
}

nsresult nsXULElement::DispatchXULCommand(const EventChainVisitor& aVisitor,
                                          nsAutoString& aCommand) {
  // XXX sXBL/XBL2 issue! Owner or current document?
  nsCOMPtr<Document> doc = GetUncomposedDoc();
  NS_ENSURE_STATE(doc);
  RefPtr<Element> commandElt = doc->GetElementById(aCommand);
  if (commandElt) {
    // Create a new command event to dispatch to the element
    // pointed to by the command attribute. The new event's
    // sourceEvent will be the original command event that we're
    // handling.
    RefPtr<Event> event = aVisitor.mDOMEvent;
    uint16_t inputSource = MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
    int16_t button = 0;
    while (event) {
      NS_ENSURE_STATE(event->GetOriginalTarget() != commandElt);
      RefPtr<XULCommandEvent> commandEvent = event->AsXULCommandEvent();
      if (commandEvent) {
        event = commandEvent->GetSourceEvent();
        inputSource = commandEvent->InputSource();
        button = commandEvent->Button();
      } else {
        event = nullptr;
      }
    }
    WidgetInputEvent* orig = aVisitor.mEvent->AsInputEvent();
    nsContentUtils::DispatchXULCommand(
        commandElt, orig->IsTrusted(), MOZ_KnownLive(aVisitor.mDOMEvent),
        nullptr, orig->IsControl(), orig->IsAlt(), orig->IsShift(),
        orig->IsMeta(), inputSource, button);
  } else {
    NS_WARNING("A XUL element is attached to a command that doesn't exist!");
  }
  return NS_OK;
}

void nsXULElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mForceContentDispatch = true;  // FIXME! Bug 329119
  if (IsEventStoppedFromAnonymousScrollbar(aVisitor.mEvent->mMessage)) {
    // Don't propagate these events from native anonymous scrollbar.
    aVisitor.mCanHandle = true;
    aVisitor.SetParentTarget(nullptr, false);
    return;
  }
  if (aVisitor.mEvent->mMessage == eUnidentifiedEvent &&
      aVisitor.mEvent->mSpecifiedEventType == nsGkAtoms::oncommand &&
      aVisitor.mEvent->mClass == eInputEventClass &&
      aVisitor.mEvent->mOriginalTarget == static_cast<nsIContent*>(this) &&
      !IsXULElement(nsGkAtoms::command)) {
    // Check that we really have an xul command event. That will be handled
    // in a special way.
    // See if we have a command elt.  If so, we execute on the command
    // instead of on our content element.
    if (aVisitor.mDOMEvent && aVisitor.mDOMEvent->AsXULCommandEvent() &&
        HasNonEmptyAttr(nsGkAtoms::command)) {
      // Stop building the event target chain for the original event.
      // We don't want it to propagate to any DOM nodes.
      aVisitor.mCanHandle = false;
      aVisitor.mAutomaticChromeDispatch = false;
      // Dispatch XUL command in PreHandleEvent to prevent it breaks event
      // target chain creation
      aVisitor.mWantsPreHandleEvent = true;
      aVisitor.mItemFlags |= NS_DISPATCH_XUL_COMMAND;
      return;
    }
  }

  nsStyledElement::GetEventTargetParent(aVisitor);
}

nsresult nsXULElement::PreHandleEvent(EventChainVisitor& aVisitor) {
  if (aVisitor.mItemFlags & NS_DISPATCH_XUL_COMMAND) {
    nsAutoString command;
    GetAttr(nsGkAtoms::command, command);
    MOZ_ASSERT(!command.IsEmpty());
    return DispatchXULCommand(aVisitor, command);
  }
  return nsStyledElement::PreHandleEvent(aVisitor);
}

//----------------------------------------------------------------------
// Implementation methods

bool nsXULElement::IsNoNamespaceAttrMapped(const nsAtom* aAttribute) const {
  return false;
}

nsIControllers* nsXULElement::EnsureControllers() {
  auto* slots = ExtendedDOMSlots();
  if (!slots->mControllers) {
    slots->mControllers = new nsXULControllers();
  }
  return slots->mControllers;
}

void nsXULElement::Click(CallerType aCallerType) {
  ClickWithInputSource(MouseEvent_Binding::MOZ_SOURCE_UNKNOWN,
                       aCallerType == CallerType::System);
}

void nsXULElement::ClickWithInputSource(uint16_t aInputSource,
                                        bool aIsTrustedEvent) {
  if (State().HasState(ElementState::DISABLED)) {
    return;
  }

  nsCOMPtr<Document> doc = GetComposedDoc();  // Strong just in case
  if (doc) {
    RefPtr<nsPresContext> context = doc->GetPresContext();
    if (context) {
      // strong ref to PresContext so events don't destroy it

      WidgetMouseEvent eventDown(aIsTrustedEvent, eMouseDown, nullptr,
                                 WidgetMouseEvent::eReal);
      WidgetMouseEvent eventUp(aIsTrustedEvent, eMouseUp, nullptr,
                               WidgetMouseEvent::eReal);
      // This helps to avoid commands being dispatched from
      // XULButtonElement::PostHandleEventForMenu.
      eventUp.mFlags.mMultipleActionsPrevented = true;
      WidgetPointerEvent eventClick(aIsTrustedEvent, ePointerClick, nullptr);
      eventDown.mInputSource = eventUp.mInputSource = eventClick.mInputSource =
          aInputSource;
      switch (aInputSource) {
        case MouseEvent_Binding::MOZ_SOURCE_MOUSE:
          MOZ_ASSERT(eventClick.pointerId == 0 || eventClick.pointerId == 1,
                     "pointerId for the primary mouse pointer must be 0 or 1");
          break;
        case MouseEvent_Binding::MOZ_SOURCE_KEYBOARD:
        case MouseEvent_Binding::MOZ_SOURCE_UNKNOWN:
          // pointerId definition in Pointer Events:
          // > The pointerId value of -1 MUST be reserved and used to indicate
          // > events that were generated by something other than a pointing
          // > device.
          eventDown.pointerId = eventUp.pointerId = eventClick.pointerId = -1;
          break;
      }

      // send mouse down
      nsEventStatus status = nsEventStatus_eIgnore;
      EventDispatcher::Dispatch(this, context, &eventDown, nullptr, &status);

      // send mouse up
      status = nsEventStatus_eIgnore;  // reset status
      EventDispatcher::Dispatch(this, context, &eventUp, nullptr, &status);

      // send mouse click
      status = nsEventStatus_eIgnore;  // reset status
      EventDispatcher::Dispatch(this, context, &eventClick, nullptr, &status);

      // If the click has been prevented, lets skip the command call
      // this is how a physical click works
      if (status == nsEventStatus_eConsumeNoDefault) {
        return;
      }
    }
  }

  // oncommand is fired when an element is clicked...
  DoCommand();
}

void nsXULElement::DoCommand() {
  nsCOMPtr<Document> doc = GetComposedDoc();  // strong just in case
  if (doc) {
    RefPtr<nsXULElement> self = this;
    nsContentUtils::DispatchXULCommand(self, true);
  }
}

nsresult nsXULElement::AddPopupListener(nsAtom* aName) {
  // Add a popup listener to the element
  bool isContext =
      (aName == nsGkAtoms::context || aName == nsGkAtoms::contextmenu);
  uint32_t listenerFlag = isContext ? XUL_ELEMENT_HAS_CONTENTMENU_LISTENER
                                    : XUL_ELEMENT_HAS_POPUP_LISTENER;

  if (HasFlag(listenerFlag)) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMEventListener> listener =
      new nsXULPopupListener(this, isContext);

  // Add the popup as a listener on this element.
  EventListenerManager* manager = GetOrCreateListenerManager();
  SetFlags(listenerFlag);

  if (isContext) {
    manager->AddEventListenerByType(listener, u"contextmenu"_ns,
                                    TrustedEventsAtSystemGroupBubble());
  } else {
    manager->AddEventListenerByType(listener, u"mousedown"_ns,
                                    TrustedEventsAtSystemGroupBubble());
  }
  return NS_OK;
}

//----------------------------------------------------------------------

nsresult nsXULElement::MakeHeavyweight(nsXULPrototypeElement* aPrototype) {
  MOZ_ASSERT(aPrototype);
  for (const auto& protoattr : aPrototype->mAttributes) {
    nsAttrValue value(protoattr.mValue);
    MOZ_TRY(SetParsedAttr(
        protoattr.mName.NamespaceID(), protoattr.mName.LocalName(),
        protoattr.mName.GetPrefix(), value,
        /* aNotify = */ false, mozilla::dom::IsKnownNewAttr::No));
  }
  return NS_OK;
}

bool nsXULElement::IsEventAttributeNameInternal(nsAtom* aName) {
  return nsContentUtils::IsEventAttributeName(aName, EventNameType_XUL);
}

JSObject* nsXULElement::WrapNode(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return dom::XULElement_Binding::Wrap(aCx, this, aGivenProto);
}

bool nsXULElement::IsInteractiveHTMLContent() const {
  return IsXULElement(nsGkAtoms::menupopup) ||
         Element::IsInteractiveHTMLContent();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULPrototypeNode)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXULPrototypeNode)
  if (tmp->mType == nsXULPrototypeNode::eType_Element) {
    static_cast<nsXULPrototypeElement*>(tmp)->Unlink();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXULPrototypeNode)
  if (tmp->mType == nsXULPrototypeNode::eType_Element) {
    nsXULPrototypeElement* elem = static_cast<nsXULPrototypeElement*>(tmp);
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mNodeInfo");
    cb.NoteNativeChild(elem->mNodeInfo,
                       NS_CYCLE_COLLECTION_PARTICIPANT(NodeInfo));
    size_t i;
    for (i = 0; i < elem->mAttributes.Length(); ++i) {
      const nsAttrName& name = elem->mAttributes[i].mName;
      if (!name.IsAtom()) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
                                           "mAttributes[i].mName.NodeInfo()");
        cb.NoteNativeChild(name.NodeInfo(),
                           NS_CYCLE_COLLECTION_PARTICIPANT(NodeInfo));
      }
    }
    ImplCycleCollectionTraverse(cb, elem->mChildren, "mChildren");
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsXULPrototypeNode)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

//----------------------------------------------------------------------
//
// nsXULPrototypeAttribute
//

nsXULPrototypeAttribute::~nsXULPrototypeAttribute() {
  MOZ_COUNT_DTOR(nsXULPrototypeAttribute);
}

//----------------------------------------------------------------------
//
// nsXULPrototypeElement
//

nsresult nsXULPrototypeElement::Serialize(
    nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv;

  // Write basic prototype data
  rv = aStream->Write32(mType);

  // Write Node Info
  int32_t index = aNodeInfos->IndexOf(mNodeInfo);
  NS_ASSERTION(index >= 0, "unknown mozilla::dom::NodeInfo index");
  nsresult tmp = aStream->Write32(index);
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

  // Write Attributes
  tmp = aStream->Write32(mAttributes.Length());
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

  nsAutoString attributeValue;
  size_t i;
  for (i = 0; i < mAttributes.Length(); ++i) {
    RefPtr<mozilla::dom::NodeInfo> ni;
    if (mAttributes[i].mName.IsAtom()) {
      ni = mNodeInfo->NodeInfoManager()->GetNodeInfo(
          mAttributes[i].mName.Atom(), nullptr, kNameSpaceID_None,
          nsINode::ATTRIBUTE_NODE);
      NS_ASSERTION(ni, "the nodeinfo should already exist");
    } else {
      ni = mAttributes[i].mName.NodeInfo();
    }

    index = aNodeInfos->IndexOf(ni);
    NS_ASSERTION(index >= 0, "unknown mozilla::dom::NodeInfo index");
    tmp = aStream->Write32(index);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }

    mAttributes[i].mValue.ToString(attributeValue);
    tmp = aStream->WriteWStringZ(attributeValue.get());
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
  }

  // Now write children
  tmp = aStream->Write32(uint32_t(mChildren.Length()));
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }
  for (i = 0; i < mChildren.Length(); i++) {
    nsXULPrototypeNode* child = mChildren[i].get();
    switch (child->mType) {
      case eType_Element:
      case eType_Text:
      case eType_PI:
        tmp = child->Serialize(aStream, aProtoDoc, aNodeInfos);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        break;
      case eType_Script:
        tmp = aStream->Write32(child->mType);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        nsXULPrototypeScript* script =
            static_cast<nsXULPrototypeScript*>(child);

        tmp = aStream->Write8(script->mOutOfLine);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        if (!script->mOutOfLine) {
          tmp = script->Serialize(aStream, aProtoDoc, aNodeInfos);
          if (NS_FAILED(tmp)) {
            rv = tmp;
          }
        } else {
          tmp = aStream->WriteCompoundObject(script->mSrcURI,
                                             NS_GET_IID(nsIURI), true);
          if (NS_FAILED(tmp)) {
            rv = tmp;
          }

          if (script->HasStencil()) {
            // This may return NS_OK without muxing script->mSrcURI's
            // data into the cache file, in the case where that
            // muxed document is already there (written by a prior
            // session, or by an earlier cache episode during this
            // session).
            tmp = script->SerializeOutOfLine(aStream, aProtoDoc);
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
          }
        }
        break;
    }
  }

  return rv;
}

nsresult nsXULPrototypeElement::Deserialize(
    nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    nsIURI* aDocumentURI,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  MOZ_ASSERT(aNodeInfos, "missing nodeinfo array");

  // Read Node Info
  uint32_t number = 0;
  nsresult rv = aStream->Read32(&number);
  if (NS_WARN_IF(NS_FAILED(rv))) return rv;
  mNodeInfo = aNodeInfos->SafeElementAt(number, nullptr);
  if (!mNodeInfo) {
    return NS_ERROR_UNEXPECTED;
  }

  if (mNodeInfo->Equals(nsGkAtoms::parsererror) &&
      mNodeInfo->NamespaceEquals(
          nsDependentAtomString(nsGkAtoms::nsuri_parsererror))) {
    return NS_ERROR_UNEXPECTED;
  }

  // Read Attributes
  rv = aStream->Read32(&number);
  if (NS_WARN_IF(NS_FAILED(rv))) return rv;
  int32_t attributes = int32_t(number);

  if (attributes > 0) {
    mAttributes.AppendElements(attributes);

    nsAutoString attributeValue;
    for (size_t i = 0; i < mAttributes.Length(); ++i) {
      rv = aStream->Read32(&number);
      if (NS_WARN_IF(NS_FAILED(rv))) return rv;
      mozilla::dom::NodeInfo* ni = aNodeInfos->SafeElementAt(number, nullptr);
      if (!ni) {
        return NS_ERROR_UNEXPECTED;
      }

      mAttributes[i].mName.SetTo(ni);

      rv = aStream->ReadString(attributeValue);
      if (NS_WARN_IF(NS_FAILED(rv))) return rv;
      rv = SetAttrAt(i, attributeValue, aDocumentURI);
      if (NS_WARN_IF(NS_FAILED(rv))) return rv;
    }
  }

  rv = aStream->Read32(&number);
  if (NS_WARN_IF(NS_FAILED(rv))) return rv;
  uint32_t numChildren = int32_t(number);

  if (numChildren > 0) {
    if (!mChildren.SetCapacity(numChildren, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < numChildren; i++) {
      rv = aStream->Read32(&number);
      if (NS_WARN_IF(NS_FAILED(rv))) return rv;
      Type childType = (Type)number;

      RefPtr<nsXULPrototypeNode> child;

      switch (childType) {
        case eType_Element:
          child = new nsXULPrototypeElement();
          rv = child->Deserialize(aStream, aProtoDoc, aDocumentURI, aNodeInfos);
          if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          break;
        case eType_Text:
          child = new nsXULPrototypeText();
          rv = child->Deserialize(aStream, aProtoDoc, aDocumentURI, aNodeInfos);
          if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          break;
        case eType_PI:
          child = new nsXULPrototypePI();
          rv = child->Deserialize(aStream, aProtoDoc, aDocumentURI, aNodeInfos);
          if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          break;
        case eType_Script: {
          // language version/options obtained during deserialization.
          RefPtr<nsXULPrototypeScript> script = new nsXULPrototypeScript(0);

          rv = aStream->ReadBoolean(&script->mOutOfLine);
          if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          if (!script->mOutOfLine) {
            rv = script->Deserialize(aStream, aProtoDoc, aDocumentURI,
                                     aNodeInfos);
            if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          } else {
            nsCOMPtr<nsISupports> supports;
            rv = aStream->ReadObject(true, getter_AddRefs(supports));
            if (NS_WARN_IF(NS_FAILED(rv))) return rv;
            script->mSrcURI = do_QueryInterface(supports);

            rv = script->DeserializeOutOfLine(aStream, aProtoDoc);
            if (NS_WARN_IF(NS_FAILED(rv))) return rv;
          }

          child = std::move(script);
          break;
        }
        default:
          MOZ_ASSERT(false, "Unexpected child type!");
          return NS_ERROR_UNEXPECTED;
      }

      MOZ_ASSERT(child, "Don't append null to mChildren");
      MOZ_ASSERT(child->mType == childType);
      mChildren.AppendElement(child);

      // Oh dear. Something failed during the deserialization.
      // We don't know what.  But likely consequences of failed
      // deserializations included calls to |AbortCaching| which
      // shuts down the cache and closes our streams.
      // If that happens, next time through this loop, we die a messy
      // death. So, let's just fail now, and propagate that failure
      // upward so that the ChromeProtocolHandler knows it can't use
      // a cached chrome channel for this.
      if (NS_WARN_IF(NS_FAILED(rv))) return rv;
    }
  }

  return rv;
}

nsresult nsXULPrototypeElement::SetAttrAt(uint32_t aPos,
                                          const nsAString& aValue,
                                          nsIURI* aDocumentURI) {
  MOZ_ASSERT(aPos < mAttributes.Length(), "out-of-bounds");

  // WARNING!!
  // This code is largely duplicated in nsXULElement::SetAttr.
  // Any changes should be made to both functions.

  if (!mNodeInfo->NamespaceEquals(kNameSpaceID_XUL)) {
    if (mNodeInfo->NamespaceEquals(kNameSpaceID_XHTML) &&
        mAttributes[aPos].mName.Equals(nsGkAtoms::is)) {
      // We still care about the is attribute set on HTML elements.
      mAttributes[aPos].mValue.ParseAtom(aValue);
      mIsAtom = mAttributes[aPos].mValue.GetAtomValue();

      return NS_OK;
    }

    mAttributes[aPos].mValue.ParseStringOrAtom(aValue);

    return NS_OK;
  }

  if (mAttributes[aPos].mName.Equals(nsGkAtoms::id) && !aValue.IsEmpty()) {
    // Store id as atom.
    // id="" means that the element has no id. Not that it has
    // emptystring as id.
    mAttributes[aPos].mValue.ParseAtom(aValue);

    return NS_OK;
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::aria_activedescendant)) {
    mAttributes[aPos].mValue.ParseAtom(aValue);

    return NS_OK;
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::aria_controls) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_describedby) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_details) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_errormessage) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_flowto) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_labelledby) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::aria_owns) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::control) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::_for) ||
             mAttributes[aPos].mName.Equals(nsGkAtoms::headers)) {
    mAttributes[aPos].mValue.ParseAtomArray(aValue);

    return NS_OK;
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::is)) {
    // Store is as atom.
    mAttributes[aPos].mValue.ParseAtom(aValue);
    mIsAtom = mAttributes[aPos].mValue.GetAtomValue();

    return NS_OK;
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::_class)) {
    // Compute the element's class list
    mAttributes[aPos].mValue.ParseAtomArray(aValue);

    return NS_OK;
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::style)) {
    // Parse the element's 'style' attribute

    // This is basically duplicating what nsINode::NodePrincipal() does
    nsIPrincipal* principal = mNodeInfo->NodeInfoManager()->DocumentPrincipal();
    // XXX Get correct Base URI (need GetBaseURI on *prototype* element)
    // TODO: If we implement Content Security Policy for chrome documents
    // as has been discussed, the CSP should be checked here to see if
    // inline styles are allowed to be applied.
    // XXX No specific specs talk about xul and referrer policy, pass Unset
    auto referrerInfo =
        MakeRefPtr<ReferrerInfo>(aDocumentURI, ReferrerPolicy::_empty);
    auto data = MakeRefPtr<URLExtraData>(aDocumentURI, referrerInfo, principal);
    RefPtr<DeclarationBlock> declaration = DeclarationBlock::FromCssText(
        aValue, data, eCompatibility_FullStandards, nullptr,
        StyleCssRuleType::Style);
    if (declaration) {
      declaration->SetImmutable();
      mAttributes[aPos].mValue.SetTo(declaration.forget(), &aValue);

      return NS_OK;
    }
    // Don't abort if parsing failed, it could just be malformed css.
  } else if (mAttributes[aPos].mName.Equals(nsGkAtoms::tabindex)) {
    mAttributes[aPos].mValue.ParseIntValue(aValue);

    return NS_OK;
  }

  mAttributes[aPos].mValue.ParseStringOrAtom(aValue);

  return NS_OK;
}

void nsXULPrototypeElement::Unlink() {
  mAttributes.Clear();
  mChildren.Clear();
}

//----------------------------------------------------------------------
//
// nsXULPrototypeScript
//

nsXULPrototypeScript::nsXULPrototypeScript(uint32_t aLineNo)
    : nsXULPrototypeNode(eType_Script),
      mLineNo(aLineNo),
      mSrcLoading(false),
      mOutOfLine(true),
      mSrcLoadWaiters(nullptr),
      mStencil(nullptr) {}

static nsresult WriteStencil(nsIObjectOutputStream* aStream,
                             JS::Stencil* aStencil) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  if (!fc) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  JS::TranscodeBuffer buffer;
  JS::TranscodeResult code;
  code = JS::EncodeStencil(fc, aStencil, buffer);

  JS::DestroyFrontendContext(fc);

  if (code != JS::TranscodeResult::Ok) {
    if (code == JS::TranscodeResult::Throw) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    MOZ_ASSERT(IsTranscodeFailureResult(code));
    return NS_ERROR_FAILURE;
  }

  size_t size = buffer.length();
  if (size > UINT32_MAX) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = aStream->Write32(size);
  if (NS_SUCCEEDED(rv)) {
    // Ideally we could just pass "buffer" here.  See bug 1566574.
    rv = aStream->WriteBytes(Span(buffer.begin(), size));
  }

  return rv;
}

static nsresult ReadStencil(nsIObjectInputStream* aStream, JSContext* aCx,
                            const JS::ReadOnlyDecodeOptions& aOptions,
                            JS::Stencil** aStencilOut) {
  // We don't serialize mutedError-ness of scripts, which is fine as long as
  // we only serialize system and XUL-y things. We can detect this by checking
  // where the caller wants us to deserialize.
  //
  // CompilationScope() could theoretically GC, so get that out of the way
  // before comparing to the cx global.
  JSObject* loaderGlobal = xpc::CompilationScope();
  MOZ_RELEASE_ASSERT(nsContentUtils::IsSystemCaller(aCx) ||
                     JS::CurrentGlobalOrNull(aCx) == loaderGlobal);

  uint32_t size;
  nsresult rv = aStream->Read32(&size);
  if (NS_FAILED(rv)) {
    return rv;
  }

  char* data;
  rv = aStream->ReadBytes(size, &data);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // The decoded stencil shouldn't borrow from the XDR buffer.
  MOZ_ASSERT(!aOptions.borrowBuffer);
  auto cleanupData = MakeScopeExit([&]() { free(data); });

  JS::TranscodeRange range(reinterpret_cast<uint8_t*>(data), size);

  {
    JS::TranscodeResult code;
    RefPtr<JS::Stencil> stencil;
    code = JS::DecodeStencil(aCx, aOptions, range, getter_AddRefs(stencil));
    if (code != JS::TranscodeResult::Ok) {
      if (code == JS::TranscodeResult::Throw) {
        JS_ClearPendingException(aCx);
        return NS_ERROR_OUT_OF_MEMORY;
      }

      MOZ_ASSERT(IsTranscodeFailureResult(code));
      return NS_ERROR_FAILURE;
    }

    stencil.forget(aStencilOut);
  }

  return rv;
}

void nsXULPrototypeScript::FillCompileOptions(JS::CompileOptions& aOptions,
                                              const char* aFilename,
                                              uint32_t aLineNo) {
  // NOTE: This method shouldn't change any field which also exists in
  //       JS::InstantiateOptions.  If such field is added,
  //       nsXULPrototypeScript::InstantiateScript should also call this method.

  // If the script was inline, tell the JS parser to save source for
  // Function.prototype.toSource(). If it's out of line, we retrieve the
  // source from the files on demand.
  aOptions.setSourceIsLazy(mOutOfLine);

  aOptions.setIntroductionType(mOutOfLine ? "srcScript" : "inlineScript")
      .setFileAndLine(aFilename, mOutOfLine ? 1 : aLineNo);
}

nsresult nsXULPrototypeScript::Serialize(
    nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  NS_ENSURE_TRUE(aProtoDoc, NS_ERROR_UNEXPECTED);

  NS_ASSERTION(!mSrcLoading || mSrcLoadWaiters != nullptr || !mStencil,
               "script source still loading when serializing?!");
  if (!mStencil) return NS_ERROR_FAILURE;

  // Write basic prototype data
  nsresult rv;
  rv = aStream->Write32(mLineNo);
  if (NS_FAILED(rv)) return rv;

  return WriteStencil(aStream, mStencil);
}

nsresult nsXULPrototypeScript::SerializeOutOfLine(
    nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc) {
  if (!mSrcURI->SchemeIs("chrome"))
    // Don't cache scripts that don't come from chrome uris.
    return NS_ERROR_NOT_IMPLEMENTED;

  nsXULPrototypeCache* cache = nsXULPrototypeCache::GetInstance();
  if (!cache) return NS_ERROR_OUT_OF_MEMORY;

  NS_ASSERTION(cache->IsEnabled(),
               "writing to the cache file, but the XUL cache is off?");
  bool exists;
  cache->HasScript(mSrcURI, &exists);

  /* return will be NS_OK from GetAsciiSpec.
   * that makes no sense.
   * nor does returning NS_OK from HasMuxedDocument.
   * XXX return something meaningful.
   */
  if (exists) return NS_OK;

  nsCOMPtr<nsIObjectOutputStream> oos;
  nsresult rv = cache->GetScriptOutputStream(mSrcURI, getter_AddRefs(oos));
  NS_ENSURE_SUCCESS(rv, rv);

  nsresult tmp = Serialize(oos, aProtoDoc, nullptr);
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }
  tmp = cache->FinishScriptOutputStream(mSrcURI);
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

  if (NS_FAILED(rv)) cache->AbortCaching();
  return rv;
}

nsresult nsXULPrototypeScript::Deserialize(
    nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    nsIURI* aDocumentURI,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv;
  NS_ASSERTION(!mSrcLoading || mSrcLoadWaiters != nullptr || !mStencil,
               "prototype script not well-initialized when deserializing?!");

  // Read basic prototype data
  rv = aStream->Read32(&mLineNo);
  if (NS_FAILED(rv)) return rv;

  AutoJSAPI jsapi;
  if (!jsapi.Init(xpc::CompilationScope())) {
    return NS_ERROR_UNEXPECTED;
  }
  JSContext* cx = jsapi.cx();

  JS::DecodeOptions options;
  RefPtr<JS::Stencil> newStencil;
  rv = ReadStencil(aStream, cx, options, getter_AddRefs(newStencil));
  NS_ENSURE_SUCCESS(rv, rv);
  Set(newStencil);
  return NS_OK;
}

nsresult nsXULPrototypeScript::DeserializeOutOfLine(
    nsIObjectInputStream* aInput, nsXULPrototypeDocument* aProtoDoc) {
  // Keep track of failure via rv, so we can
  // AbortCaching if things look bad.
  nsresult rv = NS_OK;
  nsXULPrototypeCache* cache = nsXULPrototypeCache::GetInstance();

  nsCOMPtr<nsIObjectInputStream> objectInput = aInput;
  if (cache) {
    bool useXULCache = true;
    if (mSrcURI) {
      // NB: we must check the XUL script cache early, to avoid
      // multiple deserialization attempts for a given script.
      // Note that PrototypeDocumentContentSink::LoadScript
      // checks the XUL script cache too, in order to handle the
      // serialization case.
      //
      // We need do this only for <script src='strres.js'> and the
      // like, i.e., out-of-line scripts that are included by several
      // different XUL documents stored in the cache file.
      useXULCache = cache->IsEnabled();

      if (useXULCache) {
        RefPtr<JS::Stencil> newStencil = cache->GetStencil(mSrcURI);
        if (newStencil) {
          Set(newStencil);
        }
      }
    }

    if (!mStencil) {
      if (mSrcURI) {
        rv = cache->GetScriptInputStream(mSrcURI, getter_AddRefs(objectInput));
      }
      // If !mSrcURI, we have an inline script. We shouldn't have
      // to do anything else in that case, I think.

      // We do reflect errors into rv, but our caller may want to
      // ignore our return value, because mStencil will be null
      // after any error, and that suffices to cause the script to
      // be reloaded (from the src= URI, if any) and recompiled.
      // We're better off slow-loading than bailing out due to a
      // error.
      if (NS_SUCCEEDED(rv))
        rv = Deserialize(objectInput, aProtoDoc, nullptr, nullptr);

      if (NS_SUCCEEDED(rv)) {
        if (useXULCache && mSrcURI && mSrcURI->SchemeIs("chrome")) {
          cache->PutStencil(mSrcURI, GetStencil());
        }
        cache->FinishScriptInputStream(mSrcURI);
      } else {
        // If mSrcURI is not in the cache,
        // rv will be NS_ERROR_NOT_AVAILABLE and we'll try to
        // update the cache file to hold a serialization of
        // this script, once it has finished loading.
        if (rv != NS_ERROR_NOT_AVAILABLE) cache->AbortCaching();
      }
    }
  }
  return rv;
}

#ifdef DEBUG
static void CheckErrorsAndWarnings(JS::FrontendContext* aFc,
                                   const JS::ReadOnlyCompileOptions& aOptions) {
  if (JS::HadFrontendErrors(aFc)) {
    const JSErrorReport* report = JS::GetFrontendErrorReport(aFc, aOptions);
    if (report) {
      const char* message = "<unknown>";
      const char* filename = "<unknown>";

      if (report->message().c_str()) {
        message = report->message().c_str();
      }
      if (report->filename.c_str()) {
        filename = report->filename.c_str();
      }

      NS_WARNING(
          nsPrintfCString(
              "Had compilation error in ScriptCompileTask: %s at %s:%u:%u",
              message, filename, report->lineno,
              report->column.oneOriginValue())
              .get());
    }

    if (JS::HadFrontendOverRecursed(aFc)) {
      NS_WARNING("Had over recursed in ScriptCompileTask");
    }

    if (JS::HadFrontendOutOfMemory(aFc)) {
      NS_WARNING("Had out of memory in ScriptCompileTask");
    }

    if (JS::HadFrontendAllocationOverflow(aFc)) {
      NS_WARNING("Had allocation overflow in ScriptCompileTask");
    }
  }

  size_t count = JS::GetFrontendWarningCount(aFc);
  for (size_t i = 0; i < count; i++) {
    const JSErrorReport* report = JS::GetFrontendWarningAt(aFc, i, aOptions);

    const char* message = "<unknown>";
    const char* filename = "<unknown>";

    if (report->message().c_str()) {
      message = report->message().c_str();
    }
    if (report->filename.c_str()) {
      filename = report->filename.c_str();
    }

    NS_WARNING(
        nsPrintfCString(
            "Had compilation warning in ScriptCompileTask: %s at %s:%u:%u",
            message, filename, report->lineno, report->column.oneOriginValue())
            .get());
  }
}
#endif

class ScriptCompileTask final : public Task {
 public:
  explicit ScriptCompileTask(UniquePtr<Utf8Unit[], JS::FreePolicy>&& aText,
                             size_t aTextLength)
      : Task(Kind::OffMainThreadOnly, EventQueuePriority::Normal),
        mOptions(JS::OwningCompileOptions::ForFrontendContext()),
        mText(std::move(aText)),
        mTextLength(aTextLength) {}

  ~ScriptCompileTask() {
    if (mFrontendContext) {
      JS::DestroyFrontendContext(mFrontendContext);
    }
  }

  nsresult Init(JS::CompileOptions& aOptions) {
    mFrontendContext = JS::NewFrontendContext();
    if (!mFrontendContext) {
      return NS_ERROR_FAILURE;
    }

    if (!mOptions.copy(mFrontendContext, aOptions)) {
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

 private:
  void Compile() {
    // NOTE: The stack limit must be set from the same thread that compiles.
    size_t stackSize = TaskController::GetThreadStackSize();
    JS::SetNativeStackQuota(mFrontendContext,
                            JS::ThreadStackQuotaForSize(stackSize));

    JS::SourceText<Utf8Unit> srcBuf;
    if (NS_WARN_IF(!srcBuf.init(mFrontendContext, mText.get(), mTextLength,
                                JS::SourceOwnership::Borrowed))) {
      return;
    }

    mStencil =
        JS::CompileGlobalScriptToStencil(mFrontendContext, mOptions, srcBuf);
#ifdef DEBUG
    // Chrome-privileged code shouldn't have any compilation error.
    CheckErrorsAndWarnings(mFrontendContext, mOptions);
    MOZ_ASSERT(mStencil);
#endif
  }

 public:
  TaskResult Run() override {
    Compile();
    return TaskResult::Complete;
  }

  already_AddRefed<JS::Stencil> StealStencil() { return mStencil.forget(); }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("ScriptCompileTask");
    return true;
  }
#endif

 private:
  // Owning-pointer for the context associated with the script compilation.
  //
  // The context is allocated on main thread in Init method, and is freed on
  // any thread in the destructor.
  JS::FrontendContext* mFrontendContext = nullptr;

  JS::OwningCompileOptions mOptions;

  RefPtr<JS::Stencil> mStencil;

  // The source text for this compilation.
  UniquePtr<Utf8Unit[], JS::FreePolicy> mText;
  size_t mTextLength;
};

class NotifyOffThreadScriptCompletedTask : public Task {
 public:
  NotifyOffThreadScriptCompletedTask(nsIOffThreadScriptReceiver* aReceiver,
                                     ScriptCompileTask* aCompileTask)
      : Task(Kind::MainThreadOnly, EventQueuePriority::Normal),
        mReceiver(aReceiver),
        mCompileTask(aCompileTask) {}

  TaskResult Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    if (PastShutdownPhase(ShutdownPhase::XPCOMShutdownFinal)) {
      return TaskResult::Complete;
    }

    RefPtr<JS::Stencil> stencil = mCompileTask->StealStencil();
    mCompileTask = nullptr;

    (void)mReceiver->OnScriptCompileComplete(
        stencil, stencil ? NS_OK : NS_ERROR_FAILURE);

    return TaskResult::Complete;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("NotifyOffThreadScriptCompletedTask");
    return true;
  }
#endif

 private:
  // NOTE:
  // This field is main-thread only, and this task shouldn't be freed off
  // main thread.
  //
  // This is guaranteed by not having off-thread tasks which depends on this
  // task, or any other pointer from off-thread task to this task, because
  // otherwise the off-thread task's mDependencies can be the last reference,
  // which results in freeing this task off main thread.
  //
  // If such task is added, this field must be moved to separate storage.
  nsCOMPtr<nsIOffThreadScriptReceiver> mReceiver;

  RefPtr<ScriptCompileTask> mCompileTask;
};

nsresult StartOffThreadCompile(JS::CompileOptions& aOptions,
                               UniquePtr<Utf8Unit[], JS::FreePolicy>&& aText,
                               size_t aTextLength,
                               nsIOffThreadScriptReceiver* aOffThreadReceiver) {
  RefPtr<ScriptCompileTask> compileTask =
      new ScriptCompileTask(std::move(aText), aTextLength);

  RefPtr<NotifyOffThreadScriptCompletedTask> notifyTask =
      new NotifyOffThreadScriptCompletedTask(aOffThreadReceiver, compileTask);

  nsresult rv = compileTask->Init(aOptions);
  NS_ENSURE_SUCCESS(rv, rv);

  notifyTask->AddDependency(compileTask.get());

  TaskController::Get()->AddTask(compileTask.forget());
  TaskController::Get()->AddTask(notifyTask.forget());

  return NS_OK;
}

nsresult nsXULPrototypeScript::Compile(const char16_t* aText,
                                       size_t aTextLength, nsIURI* aURI,
                                       uint32_t aLineNo, Document* aDocument) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(xpc::CompilationScope())) {
    return NS_ERROR_UNEXPECTED;
  }
  JSContext* cx = jsapi.cx();

  JS::SourceText<char16_t> srcBuf;
  if (NS_WARN_IF(!srcBuf.init(cx, aText, aTextLength,
                              JS::SourceOwnership::Borrowed))) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString urlspec;
  nsresult rv = aURI->GetSpec(urlspec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  JS::CompileOptions options(cx);
  FillCompileOptions(options, urlspec.get(), aLineNo);

  RefPtr<JS::Stencil> stencil =
      JS::CompileGlobalScriptToStencil(cx, options, srcBuf);
  if (!stencil) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  Set(stencil);
  return NS_OK;
}

nsresult nsXULPrototypeScript::CompileMaybeOffThread(
    mozilla::UniquePtr<mozilla::Utf8Unit[], JS::FreePolicy>&& aText,
    size_t aTextLength, nsIURI* aURI, uint32_t aLineNo, Document* aDocument,
    nsIOffThreadScriptReceiver* aOffThreadReceiver) {
  MOZ_ASSERT(aOffThreadReceiver);

  nsAutoCString urlspec;
  nsresult rv = aURI->GetSpec(urlspec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(xpc::CompilationScope())) {
    return NS_ERROR_UNEXPECTED;
  }
  JSContext* cx = jsapi.cx();

  JS::CompileOptions options(cx);
  FillCompileOptions(options, urlspec.get(), aLineNo);

  // TODO: This uses the same heuristics and the same threshold as the
  //       JS::CanDecodeOffThread API, but the heuristics needs to be updated
  //       to reflect the change regarding the Stencil API, and also the thread
  //       management on the consumer side (bug 1840831).
  static constexpr size_t OffThreadMinimumTextLength = 5 * 1000;

  if (StaticPrefs::javascript_options_parallel_parsing() &&
      aTextLength >= OffThreadMinimumTextLength) {
    rv = StartOffThreadCompile(options, std::move(aText), aTextLength,
                               aOffThreadReceiver);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  } else {
    JS::SourceText<Utf8Unit> srcBuf;
    if (NS_WARN_IF(!srcBuf.init(cx, aText.get(), aTextLength,
                                JS::SourceOwnership::Borrowed))) {
      return NS_ERROR_FAILURE;
    }

    RefPtr<JS::Stencil> stencil =
        JS::CompileGlobalScriptToStencil(cx, options, srcBuf);
    if (!stencil) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    Set(stencil);
  }
  return NS_OK;
}

nsresult nsXULPrototypeScript::InstantiateScript(
    JSContext* aCx, JS::MutableHandle<JSScript*> aScript) {
  MOZ_ASSERT(mStencil);

  JS::CompileOptions options(aCx);
  JS::InstantiateOptions instantiateOptions(options);
  aScript.set(JS::InstantiateGlobalStencil(aCx, instantiateOptions, mStencil));
  if (!aScript) {
    JS_ClearPendingException(aCx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

void nsXULPrototypeScript::Set(JS::Stencil* aStencil) { mStencil = aStencil; }

void nsXULPrototypeScript::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                                  size_t* aNodeSize) const {
  // It is okay to include the size of mSrcURI here even though it might have
  // strong references from elsewhere because the URI was created for this
  // object, in XULContentSinkImpl::OpenScript() or
  // nsXULPrototypeElement::Deserialize(). Only objects that created their own
  // URI will call nsIURIWithSizeOf::SizeOfIncludingThis().
  if (mSrcURI) {
    *aNodeSize += SizeOfIncludingThisIfURIWithSizeOf(
        mSrcURI, aSizes.mState.mMallocSizeOf);
  }
}

//----------------------------------------------------------------------
//
// nsXULPrototypeText
//

nsresult nsXULPrototypeText::Serialize(
    nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv;

  // Write basic prototype data
  rv = aStream->Write32(mType);

  nsresult tmp = aStream->WriteWStringZ(mValue.get());
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

  return rv;
}

nsresult nsXULPrototypeText::Deserialize(
    nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    nsIURI* aDocumentURI,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv = aStream->ReadString(mValue);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

//----------------------------------------------------------------------
//
// nsXULPrototypePI
//

nsresult nsXULPrototypePI::Serialize(
    nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv;

  // Write basic prototype data
  rv = aStream->Write32(mType);

  nsresult tmp = aStream->WriteWStringZ(mTarget.get());
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }
  tmp = aStream->WriteWStringZ(mData.get());
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

  return rv;
}

nsresult nsXULPrototypePI::Deserialize(
    nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
    nsIURI* aDocumentURI,
    const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) {
  nsresult rv;

  rv = aStream->ReadString(mTarget);
  if (NS_FAILED(rv)) return rv;
  rv = aStream->ReadString(mData);
  if (NS_FAILED(rv)) return rv;

  return rv;
}
