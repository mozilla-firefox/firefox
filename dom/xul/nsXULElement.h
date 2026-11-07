/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*

  The base XUL element class and associates.

*/

#ifndef nsXULElement_h_
#define nsXULElement_h_

#include <stdint.h>
#include <stdio.h>

#include "ErrorList.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/TracingAPI.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"  // JS::FreePolicy
#include "js/experimental/JSStencil.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/DOMString.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/NodeInfo.h"
#include "nsAtom.h"
#include "nsAttrName.h"
#include "nsAttrValue.h"
#include "nsCOMPtr.h"
#include "nsCaseTreatment.h"
#include "nsChangeHint.h"
#include "nsCycleCollectionParticipant.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsINode.h"
#include "nsISupports.h"
#include "nsLiteralString.h"
#include "nsString.h"
#include "nsStyledElement.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"
#include "nsWindowSizes.h"
#include "nscore.h"

class JSObject;
class nsIControllers;
class nsIObjectInputStream;
class nsIObjectOutputStream;
class nsIOffThreadScriptReceiver;
class nsIPrincipal;
class nsIURI;
class nsXULPrototypeDocument;
class nsXULPrototypeNode;
struct JSContext;

using nsPrototypeArray = nsTArray<RefPtr<nsXULPrototypeNode>>;

namespace JS {
class CompileOptions;
}

namespace mozilla {
class ErrorResult;
class EventChainPreVisitor;
class EventListenerManager;
namespace css {
class StyleRule;
}  // namespace css
namespace dom {
class Document;
class HTMLIFrameElement;
class PrototypeDocumentContentSink;
enum class CallerType : uint32_t;
}  // namespace dom
}  // namespace mozilla

////////////////////////////////////////////////////////////////////////

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
#  define XUL_PROTOTYPE_ATTRIBUTE_METER(counter) \
    (nsXULPrototypeAttribute::counter++)
#else
#  define XUL_PROTOTYPE_ATTRIBUTE_METER(counter) ((void)0)
#endif

/**

  A prototype attribute for an nsXULPrototypeElement.

 */

class nsXULPrototypeAttribute {
 public:
  nsXULPrototypeAttribute()
      : mName(nsGkAtoms::id)  // XXX this is a hack, but names have to have a
                              // value
  {
    XUL_PROTOTYPE_ATTRIBUTE_METER(gNumAttributes);
    MOZ_COUNT_CTOR(nsXULPrototypeAttribute);
  }

  ~nsXULPrototypeAttribute();

  nsAttrName mName;
  nsAttrValue mValue;

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
  static uint32_t gNumElements;
  static uint32_t gNumAttributes;
  static uint32_t gNumCacheTests;
  static uint32_t gNumCacheHits;
  static uint32_t gNumCacheSets;
  static uint32_t gNumCacheFills;
#endif /* !XUL_PROTOTYPE_ATTRIBUTE_METERING */
};

/**

  A prototype content model element that holds the "primordial" values
  that have been parsed from the original XUL document.

 */

class nsXULPrototypeNode {
 public:
  enum Type { eType_Element, eType_Script, eType_Text, eType_PI };

  Type mType;

  virtual nsresult Serialize(
      nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) = 0;
  virtual nsresult Deserialize(
      nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      nsIURI* aDocumentURI,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) = 0;

  /**
   * The prototype document must call ReleaseSubtree when it is going
   * away.  This makes the parents through the tree stop owning their
   * children, whether or not the parent's reference count is zero.
   * Individual elements may still own individual prototypes, but
   * those prototypes no longer remember their children to allow them
   * to be constructed.
   */
  virtual void ReleaseSubtree() {}

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(nsXULPrototypeNode)
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(nsXULPrototypeNode)

 protected:
  explicit nsXULPrototypeNode(Type aType) : mType(aType) {}
  virtual ~nsXULPrototypeNode() = default;
};

class nsXULPrototypeElement : public nsXULPrototypeNode {
 public:
  explicit nsXULPrototypeElement(mozilla::dom::NodeInfo* aNodeInfo = nullptr)
      : nsXULPrototypeNode(eType_Element), mNodeInfo(aNodeInfo) {}

 private:
  virtual ~nsXULPrototypeElement() { Unlink(); }

 public:
  void ReleaseSubtree() override {
    for (int32_t i = mChildren.Length() - 1; i >= 0; i--) {
      if (mChildren[i].get()) mChildren[i]->ReleaseSubtree();
    }
    mChildren.Clear();
    nsXULPrototypeNode::ReleaseSubtree();
  }

  nsresult Serialize(
      nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;
  nsresult Deserialize(
      nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      nsIURI* aDocumentURI,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;

  nsresult SetAttrAt(uint32_t aPos, const nsAString& aValue,
                     nsIURI* aDocumentURI);

  void Unlink();

  nsPrototypeArray mChildren;

  RefPtr<mozilla::dom::NodeInfo> mNodeInfo;

  nsTArray<nsXULPrototypeAttribute> mAttributes;  // [OWNER]
  RefPtr<nsAtom> mIsAtom;
  // True if we have the l10n-translated attributes.
  bool mTranslated = false;
};

class nsXULPrototypeScript : public nsXULPrototypeNode {
 public:
  explicit nsXULPrototypeScript(uint32_t aLineNo);

 private:
  virtual ~nsXULPrototypeScript() = default;

  void FillCompileOptions(JS::CompileOptions& aOptions, const char* aFilename,
                          uint32_t aLineNo);

 public:
  nsresult Serialize(
      nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;
  nsresult SerializeOutOfLine(nsIObjectOutputStream* aStream,
                              nsXULPrototypeDocument* aProtoDoc);
  nsresult Deserialize(
      nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      nsIURI* aDocumentURI,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;
  nsresult DeserializeOutOfLine(nsIObjectInputStream* aInput,
                                nsXULPrototypeDocument* aProtoDoc);

  // Compile given JS source text synchronously.
  //
  // This method doesn't take the ownership of aText, but borrows during the
  // compilation.
  //
  // If successfully compiled, `HasStencil()` returns true.
  nsresult Compile(const char16_t* aText, size_t aTextLength, nsIURI* aURI,
                   uint32_t aLineNo, mozilla::dom::Document* aDocument);

  // Compile given JS source text possibly in off-thread.
  //
  // This method takes the ownership of aText.
  //
  // If this doesn't use off-thread compilation and successfully compiled,
  // `HasStencil()` returns true.
  //
  // If this uses off-thread compilation, `HasStencil()` returns false, and
  // once the compilation finishes, aOffThreadReceiver gets notified with the
  // compiled stencil.  The callback is responsible for calling `Set()` with
  // the stencil.
  nsresult CompileMaybeOffThread(
      mozilla::UniquePtr<mozilla::Utf8Unit[], JS::FreePolicy>&& aText,
      size_t aTextLength, nsIURI* aURI, uint32_t aLineNo,
      mozilla::dom::Document* aDocument,
      nsIOffThreadScriptReceiver* aOffThreadReceiver);

  void Set(JS::Stencil* aStencil);

  bool HasStencil() { return mStencil; }

  JS::Stencil* GetStencil() { return mStencil.get(); }

  nsresult InstantiateScript(JSContext* aCx,
                             JS::MutableHandle<JSScript*> aScript);

  void AddSizeOfExcludingThis(nsWindowSizes& aSizes, size_t* aNodeSize) const;

  nsCOMPtr<nsIURI> mSrcURI;
  uint32_t mLineNo;
  bool mSrcLoading;
  bool mOutOfLine;
  mozilla::dom::PrototypeDocumentContentSink*
      mSrcLoadWaiters;  // [OWNER] but not COMPtr
 private:
  RefPtr<JS::Stencil> mStencil;
};

class nsXULPrototypeText : public nsXULPrototypeNode {
 public:
  nsXULPrototypeText() : nsXULPrototypeNode(eType_Text) {}

 private:
  virtual ~nsXULPrototypeText() = default;

 public:
  nsresult Serialize(
      nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;
  nsresult Deserialize(
      nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      nsIURI* aDocumentURI,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;

  nsString mValue;
};

class nsXULPrototypePI : public nsXULPrototypeNode {
 public:
  nsXULPrototypePI() : nsXULPrototypeNode(eType_PI) {}

 private:
  virtual ~nsXULPrototypePI() = default;

 public:
  nsresult Serialize(
      nsIObjectOutputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;
  nsresult Deserialize(
      nsIObjectInputStream* aStream, nsXULPrototypeDocument* aProtoDoc,
      nsIURI* aDocumentURI,
      const nsTArray<RefPtr<mozilla::dom::NodeInfo>>* aNodeInfos) override;

  nsString mTarget;
  nsString mData;
};

////////////////////////////////////////////////////////////////////////

/**

  The XUL element.

 */

#define XUL_ELEMENT_FLAG_BIT(n_) \
  NODE_FLAG_BIT(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// XUL element specific bits
enum {
  XUL_ELEMENT_HAS_CONTENTMENU_LISTENER = XUL_ELEMENT_FLAG_BIT(0),
  XUL_ELEMENT_HAS_POPUP_LISTENER = XUL_ELEMENT_FLAG_BIT(1)
};

ASSERT_NODE_FLAGS_SPACE(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + 2);

#undef XUL_ELEMENT_FLAG_BIT

class nsXULElement : public nsStyledElement {
 protected:
  using Document = mozilla::dom::Document;

  // Use Construct to construct elements instead of this constructor.
  explicit nsXULElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

 public:
  using Element::Blur;
  using Element::Focus;

  static already_AddRefed<mozilla::dom::Element> CreateFromPrototype(
      nsXULPrototypeElement* aPrototype, Document* aDocument, bool aIsRoot);

  // This is the constructor for nsXULElements.
  static nsXULElement* Construct(
      already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  NS_IMPL_FROMNODE(nsXULElement, kNameSpaceID_XUL)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsXULElement, nsStyledElement)

  // nsINode
  void GetEventTargetParent(mozilla::EventChainPreVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult PreHandleEvent(mozilla::EventChainVisitor& aVisitor) override;
  // nsIContent
  void DestroyContent() override;
  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;
  void DoneAddingChildren(bool aHaveNotified) override;

#ifdef MOZ_DOM_LIST
  void List(FILE* out, int32_t aIndent) const override;
  void DumpContent(FILE* out, int32_t aIndent, bool aDumpAll) const override {}
#endif

  MOZ_CAN_RUN_SCRIPT bool HasMenu();
  MOZ_CAN_RUN_SCRIPT void OpenMenu(bool aOpenFlag);

  MOZ_CAN_RUN_SCRIPT
  mozilla::Result<bool, nsresult> PerformAccesskey(
      bool aKeyCausesActivation, bool aIsTrustedEvent) override;
  MOZ_CAN_RUN_SCRIPT void ClickWithInputSource(uint16_t aInputSource,
                                               bool aIsTrustedEvent);
  struct XULFocusability {
    bool mDefaultFocusable = false;
    mozilla::Maybe<bool> mForcedFocusable;
    mozilla::Maybe<int32_t> mForcedTabIndexIfFocusable;

    static XULFocusability NeverFocusable() {
      return {false, mozilla::Some(false), mozilla::Some(-1)};
    }
  };
  XULFocusability GetXULFocusability(mozilla::IsFocusableFlags);
  Focusable IsFocusableWithoutStyle(mozilla::IsFocusableFlags) override;

  bool IsNoNamespaceAttrMapped(const nsAtom* aAttribute) const override;

  nsresult Clone(mozilla::dom::NodeInfo*, nsINode** aResult) const override;

  bool IsEventAttributeNameInternal(nsAtom* aName) override;

  using DOMString = mozilla::dom::DOMString;

  // WebIDL API
  bool Autofocus() const { return GetBoolAttr(nsGkAtoms::autofocus); }
  void SetAutofocus(bool aAutofocus, mozilla::ErrorResult&) {
    SetBoolAttr(nsGkAtoms::autofocus, aAutofocus);
  }
  bool Hidden() const { return GetBoolAttr(nsGkAtoms::hidden); }
  void SetHidden(bool aHidden) { SetBoolAttr(nsGkAtoms::hidden, aHidden); }
  bool Collapsed() const { return GetBoolAttr(nsGkAtoms::collapsed); }
  void SetCollapsed(bool aCollapsed) {
    SetBoolAttr(nsGkAtoms::collapsed, aCollapsed);
  }
  void GetObserves(DOMString& aValue) const {
    GetAttr(nsGkAtoms::observes, aValue);
  }
  void SetObserves(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::observes, aValue, rv);
  }
  void GetMenu(DOMString& aValue) const { GetAttr(nsGkAtoms::menu, aValue); }
  void SetMenu(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::menu, aValue, rv);
  }
  void GetContextMenu(DOMString& aValue) {
    GetAttr(nsGkAtoms::contextmenu, aValue);
  }
  void SetContextMenu(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::contextmenu, aValue, rv);
  }
  void GetTooltip(DOMString& aValue) const {
    GetAttr(nsGkAtoms::tooltip, aValue);
  }
  void SetTooltip(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::tooltip, aValue, rv);
  }
  void GetTooltipText(DOMString& aValue) const {
    GetAttr(nsGkAtoms::tooltiptext, aValue);
  }
  void SetTooltipText(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::tooltiptext, aValue, rv);
  }
  void GetSrc(DOMString& aValue) const { GetAttr(nsGkAtoms::src, aValue); }
  void SetSrc(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetAttr(nsGkAtoms::src, aValue, rv);
  }
  nsIControllers* GetExtantControllers() const {
    const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
    return slots ? slots->mControllers.get() : nullptr;
  }
  nsIControllers* EnsureControllers();
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Click(mozilla::dom::CallerType aCallerType);
  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DoCommand();
  // Style() inherited from nsStyledElement

  nsINode* GetScopeChainParent() const override {
    // For XUL, the parent is the parent element, if any
    Element* parent = GetParentElement();
    return parent ? parent : nsStyledElement::GetScopeChainParent();
  }

  bool IsInteractiveHTMLContent() const override;

 protected:
  ~nsXULElement();

  // This can be removed if EnsureContentsGenerated dies.
  friend class nsNSElementTearoff;

  // Implementation methods
  nsresult EnsureContentsGenerated(void) const;

  nsresult AddPopupListener(nsAtom* aName);

  /**
   * Abandon our prototype linkage, and copy all attributes locally
   */
  nsresult MakeHeavyweight(nsXULPrototypeElement* aPrototype);

  void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;
  void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  mozilla::EventListenerManager* GetEventListenerManagerForAttr(
      nsAtom* aAttrName) override;

  /**
   * Add a listener for the specified attribute, if appropriate.
   */
  void AddListenerForAttributeIfNeeded(nsAtom* aLocalName);

 protected:
  void AddTooltipSupport();
  void RemoveTooltipSupport();

  bool SupportsAccessKey() const;
  void RegUnRegAccessKey(bool aDoReg) override;

  friend nsXULElement* NS_NewBasicXULElement(
      already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  friend nsresult NS_NewXULElement(
      mozilla::dom::Element** aResult, mozilla::dom::NodeInfo* aNodeInfo,
      mozilla::dom::FromParser aFromParser, nsAtom* aIsAtom,
      mozilla::dom::CustomElementDefinition* aDefinition,
      mozilla::Maybe<RefPtr<mozilla::dom::CustomElementRegistry>>
          aCustomElementRegistry);
  friend void NS_TrustedNewXULElement(mozilla::dom::Element** aResult,
                                      mozilla::dom::NodeInfo* aNodeInfo);

  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  bool IsEventStoppedFromAnonymousScrollbar(mozilla::EventMessage aMessage);

  MOZ_CAN_RUN_SCRIPT
  nsresult DispatchXULCommand(const mozilla::EventChainVisitor& aVisitor,
                              nsAutoString& aCommand);
};

#endif  // nsXULElement_h_
