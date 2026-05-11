/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's Text node.
 */

#include "nsTextNode.h"

#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/TextBinding.h"
#include "nsContentUtils.h"
#include "nsStubMutationObserver.h"
#include "nsThreadUtils.h"
#ifdef MOZ_DOM_LIST
#  include "nsRange.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;

/**
 * class used to implement attr() generated content
 */
class nsAttributeTextNode final : public nsTextNode,
                                  public nsStubMutationObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  nsAttributeTextNode(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
                      int32_t aNameSpaceID, nsAtom* aAttrName,
                      nsAtom* aFallback)
      : nsTextNode(std::move(aNodeInfo)),
        mNameSpaceID(aNameSpaceID),
        mAttrName(aAttrName),
        mFallback(aFallback) {
    NS_ASSERTION(mNameSpaceID != kNameSpaceID_Unknown, "Must know namespace");
    NS_ASSERTION(mAttrName, "Must have attr name");
  }

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;

  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

  already_AddRefed<CharacterData> CloneDataNode(
      mozilla::dom::NodeInfo* aNodeInfo, bool aCloneText) const override {
    RefPtr<nsAttributeTextNode> it =
        new (aNodeInfo->NodeInfoManager()) nsAttributeTextNode(
            do_AddRef(aNodeInfo), mNameSpaceID, mAttrName, mFallback);
    if (aCloneText) {
      it->mBuffer = mBuffer;
    }

    return it.forget();
  }

  // Public method for the event to run
  void UpdateText() { UpdateText(true); }

 private:
  virtual ~nsAttributeTextNode() {
    NS_ASSERTION(!mOriginatingElement, "We were not unbound!");
  }

  // Update our text to our parent's current attr value
  void UpdateText(bool aNotify);

  // This doesn't need to be a strong pointer because it's only non-null
  // while we're bound to the document tree, and it points to an ancestor
  // so the ancestor must be bound to the document tree the whole time
  // and can't be deleted.
  Element* mOriginatingElement = nullptr;
  // What attribute we're showing
  int32_t mNameSpaceID;
  RefPtr<nsAtom> mAttrName;
  RefPtr<nsAtom> mFallback;
};

nsTextNode::~nsTextNode() = default;

// Use the CC variant of this, even though this class does not define
// a new CC participant, to make QIing to the CC interfaces faster.
NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(nsTextNode, CharacterData)

JSObject* nsTextNode::WrapNode(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) {
  return Text_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<CharacterData> nsTextNode::CloneDataNode(
    mozilla::dom::NodeInfo* aNodeInfo, bool aCloneText) const {
  RefPtr<nsTextNode> it =
      new (aNodeInfo->NodeInfoManager()) nsTextNode(do_AddRef(aNodeInfo));
  if (aCloneText) {
    it->mBuffer = mBuffer;
  }
  it->SetFlags(GetFlags() & NS_MAYBE_MASKED);
  return it.forget();
}

nsresult nsTextNode::AppendTextForNormalize(const char16_t* aBuffer,
                                            uint32_t aLength, bool aNotify,
                                            nsIContent* aNextSibling) {
  CharacterDataChangeInfo::Details details = {
      CharacterDataChangeInfo::Details::eMerge, aNextSibling};
  return SetTextInternal(mBuffer.GetLength(), 0, aBuffer, aLength, aNotify,
                         MutationEffectOnScript::KeepTrustWorthiness, &details);
}

#ifdef MOZ_DOM_LIST
void nsTextNode::List(FILE* out, int32_t aIndent) const {
  int32_t index;
  for (index = aIndent; --index >= 0;) fputs("  ", out);

  fprintf(out, "Text@%p", static_cast<const void*>(this));
  fprintf(out, " flags=[%08x]", static_cast<unsigned int>(GetFlags()));
  if (IsClosestCommonInclusiveAncestorForRangeInSelection()) {
    const LinkedList<AbstractRange>* ranges =
        GetExistingClosestCommonInclusiveAncestorRanges();
    uint32_t count = ranges ? ranges->length() : 0;
    fprintf(out, " ranges:%d", count);
  }
  fprintf(out, " primaryframe=%p", static_cast<void*>(GetPrimaryFrame()));
  fprintf(out, " refcount=%" PRIuPTR "<", mRefCnt.get());

  nsAutoString tmp;
  ToCString(tmp, 0, mBuffer.GetLength());
  fputs(NS_LossyConvertUTF16toASCII(tmp).get(), out);

  fputs(">\n", out);
}

void nsTextNode::DumpContent(FILE* out, int32_t aIndent, bool aDumpAll) const {
  if (aDumpAll) {
    int32_t index;
    for (index = aIndent; --index >= 0;) fputs("  ", out);

    nsAutoString tmp;
    ToCString(tmp, 0, mBuffer.GetLength());

    if (!tmp.EqualsLiteral("\\n")) {
      fputs(NS_LossyConvertUTF16toASCII(tmp).get(), out);
      if (aIndent) fputs("\n", out);
    }
  }
}
#endif

nsresult NS_NewAttributeContent(nsNodeInfoManager* aNodeInfoManager,
                                int32_t aNameSpaceID, nsAtom* aAttrName,
                                nsAtom* aFallback, nsIContent** aResult) {
  MOZ_ASSERT(aNodeInfoManager, "Missing nodeInfoManager");
  MOZ_ASSERT(aAttrName, "Must have an attr name");
  MOZ_ASSERT(aNameSpaceID != kNameSpaceID_Unknown, "Must know namespace");

  *aResult = nullptr;

  RefPtr<mozilla::dom::NodeInfo> ni = aNodeInfoManager->GetTextNodeInfo();

  RefPtr<nsAttributeTextNode> textNode = new (aNodeInfoManager)
      nsAttributeTextNode(ni.forget(), aNameSpaceID, aAttrName, aFallback);
  textNode.forget(aResult);

  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(nsAttributeTextNode, nsTextNode,
                            nsIMutationObserver)

nsresult nsAttributeTextNode::BindToTree(BindContext& aContext,
                                         nsINode& aParent) {
  MOZ_ASSERT(aParent.IsContent() && aParent.GetParent(),
             "This node can't be a child of the document or of "
             "the document root");

  nsresult rv = nsTextNode::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(!mOriginatingElement, "We were already bound!");
  Element* elem = aParent.GetParent()->AsElement();
  while (PseudoStyle::IsPseudoElement(elem->GetPseudoElementType())) {
    nsINode* node = elem->GetClosestNativeAnonymousSubtreeRootParentOrHost();
    if (!node || !node->IsElement()) {
      return NS_ERROR_UNEXPECTED;
    }
    elem = node->AsElement();
  }
  mOriginatingElement = elem;
  mOriginatingElement->AddMutationObserver(this);

  // Note that there is no need to notify here, since we have no
  // frame yet at this point.
  UpdateText(false);

  return NS_OK;
}

void nsAttributeTextNode::UnbindFromTree(UnbindContext& aContext) {
  // UnbindFromTree can be called anytime so we have to be safe.
  if (mOriginatingElement) {
    // aContext might not be true here, but we want to remove the
    // mutation observer anyway since we only need it while we're
    // in the document.
    mOriginatingElement->RemoveMutationObserver(this);
    mOriginatingElement = nullptr;
  }
  nsTextNode::UnbindFromTree(aContext);
}

void nsAttributeTextNode::AttributeChanged(Element* aElement,
                                           int32_t aNameSpaceID,
                                           nsAtom* aAttribute, AttrModType,
                                           const nsAttrValue* aOldValue) {
  if (aNameSpaceID == mNameSpaceID && aAttribute == mAttrName &&
      aElement == mOriginatingElement) {
    // Since UpdateText notifies, do it when it's safe to run script.  Note
    // that if we get unbound while the event is up that's ok -- we'll just
    // have no originating element when it fires, and will do nothing.
    void (nsAttributeTextNode::*update)() = &nsAttributeTextNode::UpdateText;
    nsContentUtils::AddScriptRunner(NewRunnableMethod(
        "nsAttributeTextNode::AttributeChanged", this, update));
  }
}

void nsAttributeTextNode::NodeWillBeDestroyed(nsINode* aNode) {
  NS_ASSERTION(aNode == static_cast<nsINode*>(mOriginatingElement),
               "Wrong node!");
  mOriginatingElement = nullptr;
}

void nsAttributeTextNode::UpdateText(bool aNotify) {
  if (mOriginatingElement) {
    nsAutoString attrValue;

    if (!mOriginatingElement->GetAttr(mNameSpaceID, mAttrName, attrValue)) {
      // Attr value does not exist, use fallback instead
      mFallback->ToString(attrValue);
    }

    SetText(attrValue, aNotify);
  }
}
