/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5TreeOperation.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/Likely.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/LinkStyle.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/Text.h"
#include "nsAttrName.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsEscape.h"
#include "nsGenericHTMLElement.h"
#include "nsHtml5AutoPauseUpdate.h"
#include "nsHtml5DocumentMode.h"
#include "nsHtml5HtmlAttributes.h"
#include "nsHtml5SVGLoadDispatcher.h"
#include "nsHtml5TreeBuilder.h"
#include "nsIFormControl.h"
#include "nsIContentInlines.h"
#include "nsIMutationObserver.h"
#include "nsINode.h"
#include "nsIProtocolHandler.h"
#include "nsIScriptElement.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsTextNode.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin

using namespace mozilla;
using namespace mozilla::dom;

// If you are adding fields to a tree op and this static assert fails,
// please consider reordering the fields to avoid excess padding or,
// if that doesn't help, splitting a rare operation into multiple
// tree ops before allowing the size of all operations to get larger.
static_assert(sizeof(nsHtml5TreeOperation) <= 56);

/**
 * Helper class that opens a notification batch if the current doc
 * is different from the executor doc.
 */
class MOZ_STACK_CLASS nsHtml5OtherDocUpdate {
 public:
  nsHtml5OtherDocUpdate(Document* aCurrentDoc, Document* aExecutorDoc) {
    MOZ_ASSERT(aCurrentDoc, "Node has no doc?");
    MOZ_ASSERT(aExecutorDoc, "Executor has no doc?");
    if (MOZ_LIKELY(aCurrentDoc == aExecutorDoc)) {
      mDocument = nullptr;
    } else {
      mDocument = aCurrentDoc;
      aCurrentDoc->BeginUpdate();
    }
  }

  ~nsHtml5OtherDocUpdate() {
    if (MOZ_UNLIKELY(mDocument)) {
      mDocument->EndUpdate();
    }
  }

 private:
  RefPtr<Document> mDocument;
};

nsHtml5TreeOperation::nsHtml5TreeOperation() : mOperation(uninitialized()) {
  MOZ_COUNT_CTOR(nsHtml5TreeOperation);
}

nsHtml5TreeOperation::~nsHtml5TreeOperation() {
  MOZ_COUNT_DTOR(nsHtml5TreeOperation);

  struct TreeOperationMatcher {
    void operator()(const opAppend& aOperation) {}

    void operator()(const opDetach& aOperation) {}

    void operator()(const opAppendChildrenToNewParent& aOperation) {}

    void operator()(const opFosterParent& aOperation) {}

    void operator()(const opAppendToDocument& aOperation) {}

    void operator()(const opAddAttributes& aOperation) {
      delete aOperation.mAttributes;
    }

    void operator()(const nsHtml5DocumentMode& aMode) {}

    void operator()(const opCreateHTMLElement& aOperation) {
      aOperation.mName->Release();
      delete aOperation.mAttributes;
    }

    void operator()(const opCreateSVGElement& aOperation) {
      aOperation.mName->Release();
      delete aOperation.mAttributes;
    }

    void operator()(const opCreateMathMLElement& aOperation) {
      aOperation.mName->Release();
      delete aOperation.mAttributes;
    }

    void operator()(const opSetFormElement& aOperation) {}

    void operator()(const opAppendText& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opFosterParentText& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opAppendComment& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opAppendCommentToDocument& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opAppendDoctypeToDocument& aOperation) {
      aOperation.mName->Release();
      delete aOperation.mStringPair;
    }

    void operator()(const opGetDocumentFragmentForTemplate& aOperation) {}

    void operator()(const opSetDocumentFragmentForTemplate& aOperation) {}

    void operator()(const opGetShadowRootFromHost& aOperation) {}

    void operator()(const opGetFosterParent& aOperation) {}

    void operator()(const opMarkAsBroken& aOperation) {}

    void operator()(const opRunScriptThatMayDocumentWriteOrBlock& aOperation) {}

    void operator()(
        const opRunScriptThatCannotDocumentWriteOrBlock& aOperation) {}

    void operator()(const opPreventScriptExecution& aOperation) {}

    void operator()(const opDoneAddingChildren& aOperation) {}

    void operator()(const opDoneCreatingElement& aOperation) {}

    void operator()(const opUpdateCharsetSource& aOperation) {}

    void operator()(const opCharsetSwitchTo& aOperation) {}

    void operator()(const opUpdateStyleSheet& aOperation) {}

    void operator()(const opProcessOfflineManifest& aOperation) {
      free(aOperation.mUrl);
    }

    void operator()(const opMarkMalformedIfScript& aOperation) {}

    void operator()(const opStreamEnded& aOperation) {}

    void operator()(const opSetStyleLineNumber& aOperation) {}

    void operator()(const opSetScriptLineAndColumnNumberAndFreeze& aOperation) {
    }

    void operator()(const opSvgLoad& aOperation) {}

    void operator()(const opMaybeComplainAboutCharset& aOperation) {}

    void operator()(const opMaybeComplainAboutDeepTree& aOperation) {}

    void operator()(const opAddClass& aOperation) {}

    void operator()(const opAddViewSourceHref& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opAddViewSourceBase& aOperation) {
      delete[] aOperation.mBuffer;
    }

    void operator()(const opAddErrorType& aOperation) {
      if (aOperation.mName) {
        aOperation.mName->Release();
      }
      if (aOperation.mOther) {
        aOperation.mOther->Release();
      }
    }

    void operator()(const opAddLineNumberId& aOperation) {}

    void operator()(const opShallowCloneInto& aOperation) {}

    void operator()(const opStartLayout& aOperation) {}

    void operator()(const opEnableEncodingMenu& aOperation) {}

    void operator()(const opMicrotaskCheckpoint& aOperation) {}

    void operator()(const uninitialized& aOperation) {
      NS_WARNING("Uninitialized tree op.");
    }
  };

  mOperation.match(TreeOperationMatcher());
}

static void AbortNodeInsertion(nsINode* aNode) {
  if (auto* formControl = nsGenericHTMLFormControlElement::FromNode(aNode)) {
    // Clear form for this element, since it will not actually be
    // inserted.
    formControl->ClearForm(true, true);
  }
}

nsresult nsHtml5TreeOperation::AppendTextToTextNode(
    const char16_t* aBuffer, uint32_t aLength, Text* aTextNode,
    nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aTextNode, "Got null text node.");
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  uint32_t oldLength = aTextNode->TextLength();
  CharacterDataChangeInfo info = {true, oldLength, oldLength, aLength,
                                  MutationEffectOnScript::KeepTrustWorthiness};
  MutationObservers::NotifyCharacterDataWillChange(aTextNode, info);

  nsresult rv = aTextNode->AppendText(aBuffer, aLength, false);
  NS_ENSURE_SUCCESS(rv, rv);

  MutationObservers::NotifyCharacterDataChanged(aTextNode, info);
  return rv;
}

nsresult nsHtml5TreeOperation::AppendText(const char16_t* aBuffer,
                                          uint32_t aLength, nsIContent* aParent,
                                          nsHtml5DocumentBuilder* aBuilder) {
  nsresult rv = NS_OK;
  nsIContent* lastChild = aParent->GetLastChild();
  if (lastChild && lastChild->IsText()) {
    nsHtml5OtherDocUpdate update(aParent->OwnerDoc(), aBuilder->GetDocument());
    return AppendTextToTextNode(aBuffer, aLength, lastChild->GetAsText(),
                                aBuilder);
  }

  nsNodeInfoManager* nodeInfoManager = aParent->NodeInfoManager();
  RefPtr<nsTextNode> text = new (nodeInfoManager) nsTextNode(nodeInfoManager);
  NS_ASSERTION(text, "Infallible malloc failed?");
  rv = text->SetText(aBuffer, aLength, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return Append(text, aParent, aBuilder);
}

nsresult nsHtml5TreeOperation::Append(nsIContent* aNode, nsIContent* aParent,
                                      nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  MOZ_ASSERT(!aNode->GetParentNode());
  ErrorResult rv;
  Document* ownerDoc = aParent->OwnerDoc();
  nsHtml5OtherDocUpdate update(ownerDoc, aBuilder->GetDocument());
  aParent->AppendChildTo(aNode, false, rv);
  if (!rv.Failed() && !ownerDoc->DOMNotificationsSuspended()) {
    aNode->SetParserHasNotified();
    MutationObservers::NotifyContentAppended(
        aParent, aNode, {MutationEffectOnScript::KeepTrustWorthiness});
  }
  return rv.StealNSResult();
}

nsresult nsHtml5TreeOperation::Append(nsIContent* aNode, nsIContent* aParent,
                                      FromParser aFromParser,
                                      nsHtml5DocumentBuilder* aBuilder) {
  if (MOZ_UNLIKELY(aNode->GetParentNode())) {
    Detach(aNode, aBuilder);
    if (MOZ_UNLIKELY(aNode->GetParentNode())) {
      // Can this happen? If it can, give up.
      AbortNodeInsertion(aNode);
      return NS_OK;
    }
  }

  if (MOZ_UNLIKELY(aNode->HasChildren()) &&
      aParent->IsInclusiveDescendantOf(aNode)) {
    // "If it is not possible to insert element at the adjusted insertion
    // location, abort these steps."
    // But see https://github.com/whatwg/html/issues/12494
    AbortNodeInsertion(aNode);
    return NS_OK;
  }

  Maybe<AutoSetThrowOnDynamicMarkupInsertionCounter>
      throwOnDynamicMarkupInsertionCounter;
  Maybe<nsHtml5AutoPauseUpdate> autoPause;
  Maybe<AutoCEReaction> autoCEReaction;
  DocGroup* docGroup = aParent->OwnerDoc()->GetDocGroup();
  if (docGroup && aFromParser != FROM_PARSER_FRAGMENT) {
    autoCEReaction.emplace(docGroup->CustomElementReactionsStack(), nullptr);
  }
  nsresult rv = Append(aNode, aParent, aBuilder);
  // Pause the parser only when there are reactions to be invoked to avoid
  // pausing parsing too aggressive.
  if (autoCEReaction.isSome() && docGroup &&
      docGroup->CustomElementReactionsStack()
          ->IsElementQueuePushedForCurrentRecursionDepth()) {
    throwOnDynamicMarkupInsertionCounter.emplace(aBuilder->GetDocument());
    autoPause.emplace(aBuilder);
  }
  return rv;
}

nsresult nsHtml5TreeOperation::AppendToDocument(
    nsIContent* aNode, nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->GetDocument() == aNode->OwnerDoc());
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  if (MOZ_UNLIKELY(aNode->GetParentNode())) {
    Detach(aNode, aBuilder);
    if (MOZ_UNLIKELY(aNode->GetParentNode())) {
      // Can this happen? If it can, give up.
      AbortNodeInsertion(aNode);
      return NS_OK;
    }
  }

  ErrorResult rv;
  Document* doc = aBuilder->GetDocument();
  doc->AppendChildTo(aNode, false, rv);
  if (rv.ErrorCodeIs(NS_ERROR_DOM_HIERARCHY_REQUEST_ERR)) {
    aNode->SetParserHasNotified();
    AbortNodeInsertion(aNode);
    return NS_OK;
  }
  if (rv.Failed()) {
    AbortNodeInsertion(aNode);
    return rv.StealNSResult();
  }

  if (!doc->DOMNotificationsSuspended()) {
    aNode->SetParserHasNotified();
    MutationObservers::NotifyContentInserted(
        doc, aNode, {MutationEffectOnScript::KeepTrustWorthiness});
  }

  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot to block scripts");
  if (aNode->IsElement()) {
    nsContentUtils::AddScriptRunner(
        MakeAndAddRef<nsDocElementCreatedNotificationRunner>(doc));
  }
  return NS_OK;
}

static bool IsElementOrTemplateContent(nsINode* aNode) {
  if (aNode) {
    if (aNode->IsElement()) {
      return true;
    }
    if (aNode->IsDocumentFragment()) {
      // Check if the node is a template content.
      nsIContent* fragHost = aNode->AsDocumentFragment()->GetHost();
      if (fragHost && fragHost->IsTemplateElement()) {
        return true;
      }
    }
  }
  return false;
}

void nsHtml5TreeOperation::Detach(nsIContent* aNode,
                                  nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  nsCOMPtr<nsINode> parent = aNode->GetParentNode();
  if (parent) {
    nsHtml5OtherDocUpdate update(parent->OwnerDoc(), aBuilder->GetDocument());
    parent->RemoveChildNode(aNode, true, nullptr, nullptr,
                            MutationEffectOnScript::KeepTrustWorthiness);
  }
}

nsresult nsHtml5TreeOperation::AppendChildrenToNewParent(
    nsIContent* aNode, nsIContent* aParent, nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  nsHtml5OtherDocUpdate update(aParent->OwnerDoc(), aBuilder->GetDocument());

  bool didAppend = false;
  while (aNode->HasChildren()) {
    nsCOMPtr<nsIContent> child = aNode->GetFirstChild();
    aNode->RemoveChildNode(child, true, nullptr, nullptr,
                           MutationEffectOnScript::KeepTrustWorthiness);

    if (MOZ_UNLIKELY(aParent->IsInclusiveDescendantOf(child))) {
      continue;
    }

    ErrorResult rv;
    aParent->AppendChildTo(child, false, rv);
    if (rv.Failed()) {
      AbortNodeInsertion(aNode);
      return rv.StealNSResult();
    }
    didAppend = true;
  }
  if (didAppend) {
    MutationObservers::NotifyContentAppended(
        aParent, aParent->GetLastChild(),
        {MutationEffectOnScript::KeepTrustWorthiness});
  }
  return NS_OK;
}

nsresult nsHtml5TreeOperation::FosterParent(nsIContent* aNode,
                                            nsIContent* aParent,
                                            nsIContent* aTable,
                                            nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  if (MOZ_UNLIKELY(aNode->GetParentNode())) {
    Detach(aNode, aBuilder);
    if (MOZ_UNLIKELY(aNode->GetParentNode())) {
      // Can this happen? If it can, give up.
      AbortNodeInsertion(aNode);
      return NS_OK;
    }
  }

  nsIContent* foster = aTable->GetParent();

  if (IsElementOrTemplateContent(foster)) {
    if (MOZ_UNLIKELY(aNode->HasChildren()) &&
        aTable->IsInclusiveDescendantOf(aNode)) {
      // "If it is not possible to insert element at the adjusted insertion
      // location, abort these steps."
      // But see https://github.com/whatwg/html/issues/12494
      AbortNodeInsertion(aNode);
      return NS_OK;
    }

    nsHtml5OtherDocUpdate update(foster->OwnerDoc(), aBuilder->GetDocument());

    ErrorResult rv;
    foster->InsertChildBefore(aNode, aTable, false, rv);
    if (rv.Failed()) {
      AbortNodeInsertion(aNode);
      return rv.StealNSResult();
    }

    MutationObservers::NotifyContentInserted(
        foster, aNode, {MutationEffectOnScript::KeepTrustWorthiness});
    return NS_OK;
  }

  if (MOZ_UNLIKELY(aNode->HasChildren()) &&
      aParent->IsInclusiveDescendantOf(aNode)) {
    // "If it is not possible to insert element at the adjusted insertion
    // location, abort these steps."
    // But see https://github.com/whatwg/html/issues/12494
    AbortNodeInsertion(aNode);
    return NS_OK;
  }

  return Append(aNode, aParent, aBuilder);
}

nsresult nsHtml5TreeOperation::AddAttributes(nsIContent* aNode,
                                             nsHtml5HtmlAttributes* aAttributes,
                                             nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aNode->IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::html));

  Element* node = aNode->AsElement();
  nsHtml5OtherDocUpdate update(node->OwnerDoc(), aBuilder->GetDocument());

  for (nsHtml5AttributeEntry& entry : *aAttributes) {
    nsHtml5String& val = entry.ValueRef();
    nsAtom* localName = entry.NameHTML();
    if (!node->HasAttr(kNameSpaceID_None, localName) &&
        (localName != nsGkAtoms::nonce)) {
      // If value is already an atom, use it directly to avoid string
      // allocation.
      nsAtom* valAtom = val.MaybeAsAtom();
      if (valAtom) {
        node->SetAttr(kNameSpaceID_None, localName, nullptr, valAtom, nullptr,
                      true);
      } else {
        nsString value;  // Not Auto, because using it to hold nsStringBuffer*
        // Safety: OK to call, because val is a reference into the attribute
        // holder, so a call on `val` is a call on an owning instance of
        // `nsHtml5String`.
        val.MoveToString(value);
        node->SetAttr(kNameSpaceID_None, localName, nullptr, value, true);
      }
      // XXX what to do with nsresult?
    }
  }
  return NS_OK;
}

void nsHtml5TreeOperation::SetHTMLElementAttributes(
    Element* aElement, nsHtml5HtmlAttributes* aAttributes) {
  int32_t len = aAttributes->getLength();
  if (!len) {
    return;
  }
  aElement->ReserveAttributeCount((uint32_t)len);
  if (aAttributes->getDuplicateAttributeError()) {
    aElement->SetParserHadDuplicateAttributeError();
  }
  for (nsHtml5AttributeEntry& entry : *aAttributes) {
    nsHtml5String& val = entry.ValueRef();
    nsAtom* localName = entry.NameHTML();
    if (localName == nsGkAtoms::_class) {
      nsAtom* klass = val.MaybeAsAtom();
      if (klass) {
        aElement->SetClassAttrFromParser(klass);
        continue;
      }
    }

    // If value is already an atom, use it directly to avoid string allocation.
    nsAtom* valAtom = val.MaybeAsAtom();
    if (valAtom) {
      aElement->SetAttr(kNameSpaceID_None, localName, nullptr, valAtom, nullptr,
                        false);
    } else {
      nsString value;  // Not Auto, because using it to hold nsStringBuffer*
      // Safety: OK to call, because val is a reference into the attribute
      // holder, so a call on `val` is a call on an owning instance of
      // `nsHtml5String`.
      val.MoveToString(value);
      aElement->SetAttr(kNameSpaceID_None, localName, nullptr, value, false);
    }
  }
#ifdef DEBUG
  aAttributes->MarkAsMovedFrom();
#endif
}

void nsHtml5TreeOperation::SetHTMLElementAttributesFast(
    Element* aElement, nsHtml5HtmlAttributes* aAttributes) {
  int32_t len = aAttributes->getLength();
  if (!len) {
    return;
  }
  aElement->ReserveAttributeCount((uint32_t)len);
  if (aAttributes->getDuplicateAttributeError()) {
    aElement->SetParserHadDuplicateAttributeError();
  }
  // This boolean is state that is shared between the
  // SetNoNameSpaceAttrOnNewlyCreatedElement calls so that
  // if one call schedules pending mapped attribute evaluation,
  // subsequent calls no longer have to check for mapped attribute
  // or schedule evaluation.
  bool isPendingMappedAttributeEvaluation = false;
  for (nsHtml5AttributeEntry& entry : *aAttributes) {
    aElement->SetNoNameSpaceAttrOnNewlyCreatedElement(
        entry.ForgetNameHTML(), entry.ValueRef(),
        isPendingMappedAttributeEvaluation);
  }
#ifdef DEBUG
  aAttributes->MarkAsMovedFrom();
#endif
}

nsIContent* nsHtml5TreeOperation::CreateHTMLElement(
    nsAtom* aName, nsHtml5HtmlAttributes* aAttributes, FromParser aFromParser,
    nsNodeInfoManager* aNodeInfoManager, nsHtml5DocumentBuilder* aBuilder,
    HTMLContentCreatorFunction aCreator, nsINode* aIntendedParent,
    Maybe<RefPtr<CustomElementRegistry>> aContextRegistry) {
  // https://html.spec.whatwg.org/#create-an-element-for-the-token
  // 1. If the active speculative HTML parser is not null, then return the
  // result of creating a speculative mock element given namespace, token's tag
  // name, and token's attributes.
  // 2. Otherwise, optionally create a speculative mock element given namespace,
  // token's tag name, and token's attributes.
  // (Speculative mocks handled elsewhere).
  RefPtr<NodeInfo> nodeInfo = aNodeInfoManager->GetNodeInfo(
      aName, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);
  NS_ASSERTION(nodeInfo, "Got null nodeinfo.");

  // 3. Let document be intendedParent's node document.
  Document* document = nodeInfo->GetDocument();

  // 4. Let localName be token's tag name.
  // (this is `aName`).

  // 5. Let is be the value of the "is" attribute in token, if such an attribute
  // exists; otherwise null.
  RefPtr<nsAtom> isAtom;
  if (aAttributes) {
    nsHtml5String is = aAttributes->getValue(nsHtml5AttributeName::ATTR_IS);
    if (is) {
      nsAutoString isValue;
      is.ToString(isValue);
      isAtom = NS_Atomize(isValue);
    }
  }

  // 6. Let registry be the result of looking up a custom element registry given
  // intendedParent.
  //
  // (intendedParent may specify its own registry (the common case during
  // fragment parsing). It might specify a scoped or "null" registry
  // (Some(nullptr)). Both of these are valid and must be propagated to the
  // node. In some cases, intendedParent will have an unspecified "global"
  // registry (Nothing()); in these cases we assume registry from context and
  // fall back to aContextRegistry).
  Maybe<RefPtr<CustomElementRegistry>> customElementRegistry =
      nsContentUtils::GetCustomElementRegistry(aIntendedParent);
  if (customElementRegistry.isNothing() &&
      document == aBuilder->GetDocument()) {
    customElementRegistry = std::move(aContextRegistry);
  }

  // https://github.com/whatwg/html/pull/12000
  // https://html.spec.whatwg.org/#create-an-element-for-the-token
  // Step 6: "If token has a customelementregistry attribute, then set registry
  // to null." This opts the element (and its descendants, which inherit via
  // intendedParent) out of all registries, overriding any inherited or context
  // registry.
  if (aAttributes &&
      StaticPrefs::dom_scoped_custom_element_registries_enabled() &&
      aAttributes->contains(nsHtml5AttributeName::ATTR_CUSTOMELEMENTREGISTRY)) {
    customElementRegistry = Some(RefPtr<CustomElementRegistry>(nullptr));
  }

  // 7. Let definition be the result of looking up a custom element definition
  // given registry, namespace, localName, and is.
  const bool isCustomElement = aCreator == NS_NewCustomElement || isAtom;
  CustomElementDefinition* customElementDefinition = nullptr;

  /// customElementDefinition is only used if willExecuteScript is true,
  /// which will only be the case if parser is not in fragment parsing.
  /// Therefore we can skip looking up the definition here when in fragment
  /// parsing, as it's wasted work.
  if (isCustomElement && aFromParser != FROM_PARSER_FRAGMENT) {
    RefPtr<nsAtom> tagAtom = nodeInfo->NameAtom();
    RefPtr<nsAtom> typeAtom =
        aCreator == NS_NewCustomElement ? tagAtom : isAtom;

    MOZ_ASSERT(nodeInfo->NameAtom()->Equals(nodeInfo->LocalName()));
    // https://html.spec.whatwg.org/#create-an-element-for-the-token
    // Step 7: "Let definition be the result of looking up a custom element
    // definition given registry, namespace, localName, and is."
    // https://html.spec.whatwg.org/#look-up-a-custom-element-definition
    // Step 1: "If registry is null, then return null."
    if (!(customElementRegistry.isSome() && !customElementRegistry.value())) {
      customElementDefinition = nsContentUtils::LookupCustomElementDefinition(
          document, nodeInfo->NameAtom(), nodeInfo->NamespaceID(), typeAtom);
    }
  }

  // 8. Let willExecuteScript be true if definition is non-null and the parser
  // was not created as part of the HTML fragment parsing algorithm; otherwise
  // false.
  const bool willExecuteScript =
      customElementDefinition && aFromParser != FROM_PARSER_FRAGMENT;

  auto DoCreateElement = [&](HTMLContentCreatorFunction aCreator) -> Element* {
    // 10. Let element be the result of creating an element given document,
    // localName, namespace, null, is, willExecuteScript, and registry.
    nsCOMPtr<Element> newElement;
    if (aCreator) {
      newElement = aCreator(nodeInfo.forget(), aFromParser);
    } else {
      NS_NewHTMLElement(getter_AddRefs(newElement), nodeInfo.forget(),
                        aFromParser, isAtom, customElementDefinition,
                        customElementRegistry);
    }

    MOZ_ASSERT(newElement, "Element creation created null pointer.");
    Element* element = newElement.get();
    aBuilder->HoldElement(newElement.forget());

    // When a custom element registry is provided (e.g. from fragment parsing
    // with a scoped registry), assign it to the element. NS_NewHTMLElement
    // handles this internally via NewXULOrHTMLElement, but elements created
    // directly via aCreator (built-in elements like <span>, <div>) bypass
    // that path and need the registry set explicitly.
    if (aCreator && customElementRegistry.isSome()) {
      if (RefPtr<CustomElementRegistry> registry =
              customElementRegistry.value()) {
        element->SetCustomElementRegistry(registry);
      } else {
        element->SetNullCustomElementRegistry();
      }
    }

    if (auto* linkStyle = LinkStyle::FromNode(*element)) {
      linkStyle->DisableUpdates();
    }

    if (!aAttributes) {
      return element;
    }

    // 11. Append each attribute in the given token to element.
    //
    // aCreator is nullptr if this is a custom element. We can use
    // the fast path when we have a non-custom (HTML) element.
    if (aCreator) {
      SetHTMLElementAttributesFast(element, aAttributes);
    } else {
      SetHTMLElementAttributes(element, aAttributes);
    }
    return element;
  };

  // 9. If willExecuteScript is true:
  // 12. If willExecuteScript is true:
  if (willExecuteScript) {
    // 9.1. Increment document's throw-on-dynamic-markup-insertion counter.
    AutoSetThrowOnDynamicMarkupInsertionCounter
        throwOnDynamicMarkupInsertionCounter(aBuilder->GetDocument());
    nsHtml5AutoPauseUpdate autoPauseContentUpdate(aBuilder);
    // 9.2. If the JavaScript execution context stack is empty, then perform a
    // microtask checkpoint.
    {
      nsAutoMicroTask mt;
    }
    // 9.3. Push a new element queue onto document's relevant agent's custom
    // element reactions stack.
    // 12.1. Let queue be the result of popping from document's relevant agent's
    // custom element reactions stack. (This will be the same element queue as
    // was pushed above.)
    // 12.2. Invoke custom element reactions in queue.
    // 12.3. Decrement document's throw-on-dynamic-markup-insertion counter.
    AutoCEReaction autoCEReaction(
        document->GetDocGroup()->CustomElementReactionsStack(), nullptr);
    return DoCreateElement(nullptr);
  }
  return DoCreateElement(isCustomElement ? nullptr : aCreator);
}

nsIContent* nsHtml5TreeOperation::CreateSVGElement(
    nsAtom* aName, nsHtml5HtmlAttributes* aAttributes, FromParser aFromParser,
    nsNodeInfoManager* aNodeInfoManager, nsHtml5DocumentBuilder* aBuilder,
    SVGContentCreatorFunction aCreator) {
  nsCOMPtr<nsIContent> newElement;
  if (MOZ_LIKELY(aNodeInfoManager->SVGEnabled())) {
    RefPtr<NodeInfo> nodeInfo = aNodeInfoManager->GetNodeInfo(
        aName, nullptr, kNameSpaceID_SVG, nsINode::ELEMENT_NODE);
    MOZ_ASSERT(nodeInfo, "Got null nodeinfo.");

    DebugOnly<nsresult> rv =
        aCreator(getter_AddRefs(newElement), nodeInfo.forget(), aFromParser);
    MOZ_ASSERT(NS_SUCCEEDED(rv) && newElement);
  } else {
    RefPtr<NodeInfo> nodeInfo = aNodeInfoManager->GetNodeInfo(
        aName, nullptr, kNameSpaceID_disabled_SVG, nsINode::ELEMENT_NODE);
    MOZ_ASSERT(nodeInfo, "Got null nodeinfo.");

    // The mismatch between NS_NewXMLElement and SVGContentCreatorFunction
    // argument types is annoying.
    nsCOMPtr<Element> xmlElement;
    DebugOnly<nsresult> rv =
        NS_NewXMLElement(getter_AddRefs(xmlElement), nodeInfo.forget());
    MOZ_ASSERT(NS_SUCCEEDED(rv) && xmlElement);
    newElement = xmlElement;
  }

  Element* newContent = newElement->AsElement();
  aBuilder->HoldElement(newElement.forget());

  if (MOZ_UNLIKELY(aName == nsGkAtoms::style)) {
    if (auto* linkStyle = LinkStyle::FromNode(*newContent)) {
      linkStyle->DisableUpdates();
    }
  }

  if (!aAttributes) {
    return newContent;
  }
  int32_t len = aAttributes->getLength();
  if (!len) {
    return newContent;
  }

  newContent->ReserveAttributeCount((uint32_t)len);

  if (aAttributes->getDuplicateAttributeError()) {
    newContent->SetParserHadDuplicateAttributeError();
  }

  for (nsHtml5AttributeEntry& entry : *aAttributes) {
    nsHtml5String& val = entry.ValueRef();
    auto triple = entry.NameSVG();
    if (triple.mLocal == nsGkAtoms::_class) {
      nsAtom* klass = val.MaybeAsAtom();
      if (klass) {
        newContent->SetClassAttrFromParser(klass);
        continue;
      }
    }

    // If value is already an atom, use it directly to avoid string allocation.
    nsAtom* valAtom = val.MaybeAsAtom();
    if (valAtom) {
      newContent->SetAttr(triple.mNamespace, triple.mLocal, triple.mPrefix,
                          valAtom, nullptr, false);
    } else {
      nsString value;  // Not Auto, because using it to hold nsStringBuffer*
      // Safety: OK to call, because val is a reference into the attribute
      // holder, so a call on `val` is a call on an owning instance of
      // `nsHtml5String`.
      val.MoveToString(value);
      newContent->SetAttr(triple.mNamespace, triple.mLocal, triple.mPrefix,
                          value, false);
    }
  }
#ifdef DEBUG
  aAttributes->MarkAsMovedFrom();
#endif
  return newContent;
}

nsIContent* nsHtml5TreeOperation::CreateMathMLElement(
    nsAtom* aName, nsHtml5HtmlAttributes* aAttributes,
    nsNodeInfoManager* aNodeInfoManager, nsHtml5DocumentBuilder* aBuilder) {
  nsCOMPtr<Element> newElement;
  if (MOZ_LIKELY(aNodeInfoManager->MathMLEnabled())) {
    RefPtr<NodeInfo> nodeInfo = aNodeInfoManager->GetNodeInfo(
        aName, nullptr, kNameSpaceID_MathML, nsINode::ELEMENT_NODE);
    NS_ASSERTION(nodeInfo, "Got null nodeinfo.");

    DebugOnly<nsresult> rv =
        NS_NewMathMLElement(getter_AddRefs(newElement), nodeInfo.forget());
    MOZ_ASSERT(NS_SUCCEEDED(rv) && newElement);
  } else {
    RefPtr<NodeInfo> nodeInfo = aNodeInfoManager->GetNodeInfo(
        aName, nullptr, kNameSpaceID_disabled_MathML, nsINode::ELEMENT_NODE);
    NS_ASSERTION(nodeInfo, "Got null nodeinfo.");

    DebugOnly<nsresult> rv =
        NS_NewXMLElement(getter_AddRefs(newElement), nodeInfo.forget());
    MOZ_ASSERT(NS_SUCCEEDED(rv) && newElement);
  }

  Element* newContent = newElement;
  aBuilder->HoldElement(newElement.forget());

  if (!aAttributes) {
    return newContent;
  }
  int32_t len = aAttributes->getLength();
  if (!len) {
    return newContent;
  }

  newContent->ReserveAttributeCount((uint32_t)len);

  if (aAttributes->getDuplicateAttributeError()) {
    newContent->SetParserHadDuplicateAttributeError();
  }

  for (nsHtml5AttributeEntry& entry : *aAttributes) {
    nsHtml5String& val = entry.ValueRef();
    auto triple = entry.NameMathML();
    if (triple.mLocal == nsGkAtoms::_class) {
      nsAtom* klass = val.MaybeAsAtom();
      if (klass) {
        newContent->SetClassAttrFromParser(klass);
        continue;
      }
    }

    // If value is already an atom, use it directly to avoid string allocation.
    nsAtom* valAtom = val.MaybeAsAtom();
    if (valAtom) {
      newContent->SetAttr(triple.mNamespace, triple.mLocal, triple.mPrefix,
                          valAtom, nullptr, false);
    } else {
      nsString value;  // Not Auto, because using it to hold nsStringBuffer*
      // Safety: OK to call, because val is a reference into the attribute
      // holder, so a call on `val` is a call on an owning instance of
      // `nsHtml5String`.
      val.MoveToString(value);
      newContent->SetAttr(triple.mNamespace, triple.mLocal, triple.mPrefix,
                          value, false);
    }
  }
#ifdef DEBUG
  aAttributes->MarkAsMovedFrom();
#endif
  return newContent;
}

void nsHtml5TreeOperation::SetFormElement(nsIContent* aNode, nsIContent* aForm,
                                          nsIContent* aParent) {
  if (aForm->SubtreeRoot() != aParent->SubtreeRoot()) {
    return;
  }

  RefPtr formElement = HTMLFormElement::FromNodeOrNull(aForm);
  NS_ASSERTION(formElement,
               "The form element doesn't implement HTMLFormElement.");
  nsCOMPtr<nsIFormControl> formControl = nsIFormControl::FromNodeOrNull(aNode);
  if (formControl &&
      formControl->ControlType() !=
          FormControlType::FormAssociatedCustomElement &&
      !formControl->GetFormInternal() &&
      !aNode->AsElement()->HasAttr(nsGkAtoms::form)) {
    formControl->SetForm(formElement);
  } else if (auto* image = HTMLImageElement::FromNodeOrNull(aNode)) {
    if (!image->GetFormInternal()) {
      image->SetForm(formElement);
    }
  }
}

nsresult nsHtml5TreeOperation::FosterParentText(
    nsIContent* aStackParent, char16_t* aBuffer, uint32_t aLength,
    nsIContent* aTable, nsHtml5DocumentBuilder* aBuilder) {
  MOZ_ASSERT(aBuilder);
  MOZ_ASSERT(aBuilder->IsInDocUpdate());
  nsresult rv = NS_OK;
  nsIContent* foster = aTable->GetParent();

  if (IsElementOrTemplateContent(foster)) {
    nsHtml5OtherDocUpdate update(foster->OwnerDoc(), aBuilder->GetDocument());

    nsIContent* previousSibling = aTable->GetPreviousSibling();
    if (previousSibling && previousSibling->IsText()) {
      return AppendTextToTextNode(aBuffer, aLength,
                                  previousSibling->GetAsText(), aBuilder);
    }

    nsNodeInfoManager* nodeInfoManager = aStackParent->NodeInfoManager();
    RefPtr<nsTextNode> text = new (nodeInfoManager) nsTextNode(nodeInfoManager);
    NS_ASSERTION(text, "Infallible malloc failed?");
    rv = text->SetText(aBuffer, aLength, false);
    NS_ENSURE_SUCCESS(rv, rv);

    ErrorResult error;
    foster->InsertChildBefore(text, aTable, false, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }

    MutationObservers::NotifyContentInserted(
        foster, text, {MutationEffectOnScript::KeepTrustWorthiness});
    return rv;
  }

  return AppendText(aBuffer, aLength, aStackParent, aBuilder);
}

nsresult nsHtml5TreeOperation::AppendComment(nsIContent* aParent,
                                             char16_t* aBuffer, int32_t aLength,
                                             nsHtml5DocumentBuilder* aBuilder) {
  nsNodeInfoManager* nodeInfoManager = aParent->NodeInfoManager();
  RefPtr<Comment> comment = new (nodeInfoManager) Comment(nodeInfoManager);
  NS_ASSERTION(comment, "Infallible malloc failed?");
  nsresult rv = comment->SetText(aBuffer, aLength, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return Append(comment, aParent, aBuilder);
}

nsresult nsHtml5TreeOperation::AppendCommentToDocument(
    char16_t* aBuffer, int32_t aLength, nsHtml5DocumentBuilder* aBuilder) {
  RefPtr<Comment> comment = new (aBuilder->GetNodeInfoManager())
      Comment(aBuilder->GetNodeInfoManager());
  NS_ASSERTION(comment, "Infallible malloc failed?");
  nsresult rv = comment->SetText(aBuffer, aLength, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return AppendToDocument(comment, aBuilder);
}

nsresult nsHtml5TreeOperation::AppendDoctypeToDocument(
    nsAtom* aName, const nsAString& aPublicId, const nsAString& aSystemId,
    nsHtml5DocumentBuilder* aBuilder) {
  // Adapted from nsXMLContentSink
  // Create a new doctype node
  RefPtr<DocumentType> docType =
      NS_NewDOMDocumentType(aBuilder->GetNodeInfoManager(), aName, aPublicId,
                            aSystemId, VoidString());
  return AppendToDocument(docType, aBuilder);
}

nsIContent* nsHtml5TreeOperation::GetDocumentFragmentForTemplate(
    nsIContent* aNode) {
  auto* tempElem = static_cast<HTMLTemplateElement*>(aNode);
  return tempElem->Content();
}

void nsHtml5TreeOperation::SetDocumentFragmentForTemplate(
    nsIContent* aNode, nsIContent* aDocumentFragment) {
  auto* tempElem = static_cast<HTMLTemplateElement*>(aNode);
  tempElem->SetContent(static_cast<DocumentFragment*>(aDocumentFragment));
}

nsIContent* nsHtml5TreeOperation::GetFosterParent(nsIContent* aTable,
                                                  nsIContent* aStackParent) {
  nsIContent* tableParent = aTable->GetParent();
  return IsElementOrTemplateContent(tableParent) ? tableParent : aStackParent;
}

void nsHtml5TreeOperation::PreventScriptExecution(nsIContent* aNode) {
  nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(aNode);
  if (sele) {
    sele->PreventExecution();
  } else {
    MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
               "Node didn't QI to script, but SVG wasn't disabled.");
  }
}

void nsHtml5TreeOperation::DoneAddingChildren(nsIContent* aNode) {
  aNode->DoneAddingChildren(aNode->HasParserNotified());
}

void nsHtml5TreeOperation::DoneCreatingElement(nsIContent* aNode) {
  aNode->DoneCreatingElement();
}

void nsHtml5TreeOperation::SvgLoad(nsIContent* aNode) {
  nsCOMPtr<nsIRunnable> event = new nsHtml5SVGLoadDispatcher(aNode);
  if (NS_FAILED(aNode->OwnerDoc()->Dispatch(event.forget()))) {
    NS_WARNING("failed to dispatch svg load dispatcher");
  }
}

void nsHtml5TreeOperation::MarkMalformedIfScript(nsIContent* aNode) {
  nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(aNode);
  if (sele) {
    // Make sure to serialize this script correctly, for nice round tripping.
    sele->SetIsMalformed();
  }
}

nsresult nsHtml5TreeOperation::Perform(nsHtml5TreeOpExecutor* aBuilder,
                                       nsIContent** aScriptElement,
                                       bool* aInterrupted, bool* aStreamEnded) {
  struct TreeOperationMatcher {
    TreeOperationMatcher(nsHtml5TreeOpExecutor* aBuilder,
                         nsIContent** aScriptElement, bool* aInterrupted,
                         bool* aStreamEnded)
        : mBuilder(aBuilder),
          mScriptElement(aScriptElement),
          mInterrupted(aInterrupted),
          mStreamEnded(aStreamEnded) {}

    nsHtml5TreeOpExecutor* mBuilder;
    nsIContent** mScriptElement;
    bool* mInterrupted;
    bool* mStreamEnded;

    nsresult operator()(const opAppend& aOperation) {
      return Append(*(aOperation.mChild), *(aOperation.mParent),
                    aOperation.mFromNetwork, mBuilder);
    }

    nsresult operator()(const opDetach& aOperation) {
      Detach(*(aOperation.mElement), mBuilder);
      return NS_OK;
    }

    nsresult operator()(const opAppendChildrenToNewParent& aOperation) {
      nsCOMPtr<nsIContent> node = *(aOperation.mOldParent);
      nsIContent* parent = *(aOperation.mNewParent);
      return AppendChildrenToNewParent(node, parent, mBuilder);
    }

    nsresult operator()(const opFosterParent& aOperation) {
      nsIContent* node = *(aOperation.mChild);
      nsIContent* parent = *(aOperation.mStackParent);
      nsIContent* table = *(aOperation.mTable);
      return FosterParent(node, parent, table, mBuilder);
    }

    nsresult operator()(const opAppendToDocument& aOperation) {
      nsresult rv = AppendToDocument(*(aOperation.mContent), mBuilder);
      mBuilder->PauseDocUpdate(mInterrupted);
      return rv;
    }

    nsresult operator()(const opAddAttributes& aOperation) {
      nsIContent* node = *(aOperation.mElement);
      nsHtml5HtmlAttributes* attributes = aOperation.mAttributes;
      return AddAttributes(node, attributes, mBuilder);
    }

    nsresult operator()(const nsHtml5DocumentMode& aMode) {
      mBuilder->SetDocumentMode(aMode);
      return NS_OK;
    }

    nsresult operator()(const opCreateHTMLElement& aOperation) {
      nsIContent** target = aOperation.mContent;
      HTMLContentCreatorFunction creator = aOperation.mCreator;
      nsAtom* name = aOperation.mName;
      nsHtml5HtmlAttributes* attributes = aOperation.mAttributes;
      nsIContent* intendedParent =
          aOperation.mIntendedParent ? *(aOperation.mIntendedParent) : nullptr;

      // intendedParent == nullptr is a special case where the
      // intended parent is the document.
      nsNodeInfoManager* nodeInfoManager =
          intendedParent ? intendedParent->NodeInfoManager()
                         : mBuilder->GetNodeInfoManager();

      *target = CreateHTMLElement(name, attributes, aOperation.mFromNetwork,
                                  nodeInfoManager, mBuilder, creator,
                                  intendedParent, mozilla::Nothing());
      return NS_OK;
    }

    nsresult operator()(const opCreateSVGElement& aOperation) {
      nsIContent** target = aOperation.mContent;
      SVGContentCreatorFunction creator = aOperation.mCreator;
      nsAtom* name = aOperation.mName;
      nsHtml5HtmlAttributes* attributes = aOperation.mAttributes;
      nsIContent* intendedParent =
          aOperation.mIntendedParent ? *(aOperation.mIntendedParent) : nullptr;

      // intendedParent == nullptr is a special case where the
      // intended parent is the document.
      nsNodeInfoManager* nodeInfoManager =
          intendedParent ? intendedParent->NodeInfoManager()
                         : mBuilder->GetNodeInfoManager();

      *target = CreateSVGElement(name, attributes, aOperation.mFromNetwork,
                                 nodeInfoManager, mBuilder, creator);
      return NS_OK;
    }

    nsresult operator()(const opCreateMathMLElement& aOperation) {
      nsIContent** target = aOperation.mContent;
      nsAtom* name = aOperation.mName;
      nsHtml5HtmlAttributes* attributes = aOperation.mAttributes;
      nsIContent* intendedParent =
          aOperation.mIntendedParent ? *(aOperation.mIntendedParent) : nullptr;

      // intendedParent == nullptr is a special case where the
      // intended parent is the document.
      nsNodeInfoManager* nodeInfoManager =
          intendedParent ? intendedParent->NodeInfoManager()
                         : mBuilder->GetNodeInfoManager();

      *target =
          CreateMathMLElement(name, attributes, nodeInfoManager, mBuilder);
      return NS_OK;
    }

    nsresult operator()(const opSetFormElement& aOperation) {
      SetFormElement(*(aOperation.mContent), *(aOperation.mFormElement),
                     *(aOperation.mIntendedParent));
      return NS_OK;
    }

    nsresult operator()(const opAppendText& aOperation) {
      nsIContent* parent = *aOperation.mParent;
      char16_t* buffer = aOperation.mBuffer;
      uint32_t length = aOperation.mLength;
      return AppendText(buffer, length, parent, mBuilder);
    }

    nsresult operator()(const opFosterParentText& aOperation) {
      nsIContent* stackParent = *aOperation.mStackParent;
      char16_t* buffer = aOperation.mBuffer;
      uint32_t length = aOperation.mLength;
      nsIContent* table = *aOperation.mTable;
      return FosterParentText(stackParent, buffer, length, table, mBuilder);
    }

    nsresult operator()(const opAppendComment& aOperation) {
      nsIContent* parent = *aOperation.mParent;
      char16_t* buffer = aOperation.mBuffer;
      uint32_t length = aOperation.mLength;
      return AppendComment(parent, buffer, length, mBuilder);
    }

    nsresult operator()(const opAppendCommentToDocument& aOperation) {
      char16_t* buffer = aOperation.mBuffer;
      int32_t length = aOperation.mLength;
      return AppendCommentToDocument(buffer, length, mBuilder);
    }

    nsresult operator()(const opAppendDoctypeToDocument& aOperation) {
      nsAtom* name = aOperation.mName;
      nsHtml5TreeOperationStringPair* pair = aOperation.mStringPair;
      nsString publicId;
      nsString systemId;
      pair->Get(publicId, systemId);
      return AppendDoctypeToDocument(name, publicId, systemId, mBuilder);
    }

    nsresult operator()(const opGetDocumentFragmentForTemplate& aOperation) {
      nsIContent* node = *(aOperation.mTemplate);
      *(aOperation.mFragHandle) = GetDocumentFragmentForTemplate(node);
      return NS_OK;
    }

    nsresult operator()(const opSetDocumentFragmentForTemplate& aOperation) {
      SetDocumentFragmentForTemplate(*aOperation.mTemplate,
                                     *aOperation.mFragment);
      return NS_OK;
    }

    nsresult operator()(const opGetShadowRootFromHost& aOperation) {
      nsIContent* root = nsContentUtils::AttachDeclarativeShadowRoot(
          *aOperation.mHost, aOperation.mShadowRootMode,
          aOperation.mShadowRootIsClonable,
          aOperation.mShadowRootIsSerializable,
          aOperation.mShadowRootDelegatesFocus,
          aOperation.mShadowRootCustomElementRegistry,
          aOperation.mShadowRootSlotAssignment,
          aOperation.mShadowRootReferenceTarget);
      if (root) {
        *aOperation.mFragHandle = root;
        return NS_OK;
      }

      // We failed to attach a new shadow root, so instead attach a template
      // element and return its content.
      nsIContent* node = *aOperation.mTemplateNode;
      *aOperation.mFragHandle =
          static_cast<HTMLTemplateElement*>(node)->Content();
      nsContentUtils::LogSimpleConsoleError(
          u"Failed to attach Declarative Shadow DOM."_ns, "DOM"_ns,
          mBuilder->GetDocument()->IsInPrivateBrowsing(),
          mBuilder->GetDocument()->IsInChromeDocShell());

      if (MOZ_UNLIKELY(node->GetParentNode())) {
        Detach(node, mBuilder);
        if (MOZ_UNLIKELY(node->GetParentNode())) {
          // Can this happen? If it can, give up.
          return NS_OK;
        }
      }

      nsIContent* host = *aOperation.mHost;

      if (MOZ_UNLIKELY(node->HasChildren()) &&
          host->IsInclusiveDescendantOf(node)) {
        // "If it is not possible to insert element at the adjusted insertion
        // location, abort these steps."
        // But see https://github.com/whatwg/html/issues/12494
        return NS_OK;
      }

      nsHtml5TreeOperation::Append(node, host, mBuilder);
      return NS_OK;
    }

    nsresult operator()(const opGetFosterParent& aOperation) {
      nsIContent* table = *(aOperation.mTable);
      nsIContent* stackParent = *(aOperation.mStackParent);
      nsIContent* fosterParent = GetFosterParent(table, stackParent);
      if (fosterParent) {
        mBuilder->HoldElement(do_AddRef(fosterParent));
      }
      *aOperation.mParentHandle = fosterParent;
      return NS_OK;
    }

    nsresult operator()(const opMarkAsBroken& aOperation) {
      return aOperation.mResult;
    }

    nsresult operator()(
        const opRunScriptThatMayDocumentWriteOrBlock& aOperation) {
      nsIContent* node = *(aOperation.mElement);
      nsAHtml5TreeBuilderState* snapshot = aOperation.mBuilderState;
      if (snapshot) {
        mBuilder->InitializeDocWriteParserState(snapshot,
                                                aOperation.mLineNumber);
      }
      *mScriptElement = node;
      return NS_OK;
    }

    nsresult operator()(
        const opRunScriptThatCannotDocumentWriteOrBlock& aOperation) {
      mBuilder->RunScript(*(aOperation.mElement), false);
      return NS_OK;
    }

    nsresult operator()(const opPreventScriptExecution& aOperation) {
      PreventScriptExecution(*(aOperation.mElement));
      return NS_OK;
    }

    nsresult operator()(const opDoneAddingChildren& aOperation) {
      nsIContent* node = *(aOperation.mElement);
      node->DoneAddingChildren(node->HasParserNotified());
      return NS_OK;
    }

    nsresult operator()(const opDoneCreatingElement& aOperation) {
      DoneCreatingElement(*(aOperation.mElement));
      return NS_OK;
    }

    nsresult operator()(const opUpdateCharsetSource& aOperation) {
      mBuilder->UpdateCharsetSource(aOperation.mCharsetSource);
      return NS_OK;
    }

    nsresult operator()(const opCharsetSwitchTo& aOperation) {
      auto encoding = WrapNotNull(aOperation.mEncoding);
      mBuilder->NeedsCharsetSwitchTo(encoding, aOperation.mCharsetSource,
                                     (uint32_t)aOperation.mLineNumber);
      return NS_OK;
    }

    nsresult operator()(const opUpdateStyleSheet& aOperation) {
      mBuilder->UpdateStyleSheet(*(aOperation.mElement));
      return NS_OK;
    }

    nsresult operator()(const opProcessOfflineManifest& aOperation) {
      // TODO: remove this
      return NS_OK;
    }

    nsresult operator()(const opMarkMalformedIfScript& aOperation) {
      MarkMalformedIfScript(*(aOperation.mElement));
      return NS_OK;
    }

    nsresult operator()(const opStreamEnded& aOperation) {
      *mStreamEnded = true;
      return NS_OK;
    }

    nsresult operator()(const opSetStyleLineNumber& aOperation) {
      nsIContent* node = *(aOperation.mContent);
      if (auto* linkStyle = LinkStyle::FromNode(*node)) {
        linkStyle->SetLineNumber(aOperation.mLineNumber);
      } else {
        MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
                   "Node didn't QI to style, but SVG wasn't disabled.");
      }
      return NS_OK;
    }

    nsresult operator()(
        const opSetScriptLineAndColumnNumberAndFreeze& aOperation) {
      nsIContent* node = *(aOperation.mContent);
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(node);
      if (sele) {
        sele->SetScriptLineNumber(aOperation.mLineNumber);
        sele->SetScriptColumnNumber(
            JS::ColumnNumberOneOrigin(aOperation.mColumnNumber));
        sele->FreezeExecutionAttrs(node->OwnerDoc());
      } else {
        MOZ_ASSERT(nsNameSpaceManager::GetInstance()->mSVGDisabled,
                   "Node didn't QI to script, but SVG wasn't disabled.");
      }
      return NS_OK;
    }

    nsresult operator()(const opSvgLoad& aOperation) {
      SvgLoad(*(aOperation.mElement));
      return NS_OK;
    }

    nsresult operator()(const opMaybeComplainAboutCharset& aOperation) {
      char* msgId = aOperation.mMsgId;
      bool error = aOperation.mError;
      int32_t lineNumber = aOperation.mLineNumber;
      mBuilder->MaybeComplainAboutCharset(msgId, error, (uint32_t)lineNumber);
      return NS_OK;
    }

    nsresult operator()(const opMaybeComplainAboutDeepTree& aOperation) {
      mBuilder->MaybeComplainAboutDeepTree((uint32_t)aOperation.mLineNumber);
      return NS_OK;
    }

    nsresult operator()(const opAddClass& aOperation) {
      Element* element = (*(aOperation.mElement))->AsElement();
      char16_t* str = aOperation.mClass;
      nsDependentString depStr(str);
      // See viewsource.css for the possible classes
      nsAutoString klass;
      element->GetAttr(nsGkAtoms::_class, klass);
      if (!klass.IsEmpty()) {
        klass.Append(' ');
        klass.Append(depStr);
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass, true);
      } else {
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, depStr, true);
      }
      return NS_OK;
    }

    nsresult operator()(const opAddViewSourceHref& aOperation) {
      Element* element = (*aOperation.mElement)->AsElement();
      char16_t* buffer = aOperation.mBuffer;
      int32_t length = aOperation.mLength;
      nsDependentString relative(buffer, length);

      Document* doc = mBuilder->GetDocument();

      auto encoding = doc->GetDocumentCharacterSet();
      nsCOMPtr<nsIURI> uri;
      nsresult rv = NS_NewURI(getter_AddRefs(uri), relative, encoding,
                              mBuilder->GetViewSourceBaseURI());
      NS_ENSURE_SUCCESS(rv, NS_OK);

      // Reuse the fix for bug 467852
      // URLs that execute script (e.g. "javascript:" URLs) should just be
      // ignored.  There's nothing reasonable we can do with them, and allowing
      // them to execute in the context of the view-source window presents a
      // security risk.  Just return the empty string in this case.
      bool openingExecutesScript = false;
      rv = NS_URIChainHasFlags(uri,
                               nsIProtocolHandler::URI_OPENING_EXECUTES_SCRIPT,
                               &openingExecutesScript);
      if (NS_FAILED(rv) || openingExecutesScript) {
        return NS_OK;
      }

      nsAutoCString viewSourceUrl;

      // URLs that return data (e.g. "http:" URLs) should be prefixed with
      // "view-source:".  URLs that don't return data should just be returned
      // undecorated.
      if (!nsContentUtils::IsExternalProtocol(uri)) {
        viewSourceUrl.AssignLiteral("view-source:");
      }

      nsAutoCString spec;
      rv = uri->GetSpec(spec);
      NS_ENSURE_SUCCESS(rv, rv);

      viewSourceUrl.Append(spec);

      nsAutoString utf16;
      CopyUTF8toUTF16(viewSourceUrl, utf16);

      element->SetAttr(kNameSpaceID_None, nsGkAtoms::href, utf16, true);
      return NS_OK;
    }

    nsresult operator()(const opAddViewSourceBase& aOperation) {
      nsDependentString baseUrl(aOperation.mBuffer, aOperation.mLength);
      mBuilder->AddBase(baseUrl);
      return NS_OK;
    }

    nsresult operator()(const opAddErrorType& aOperation) {
      Element* element = (*(aOperation.mElement))->AsElement();
      char* msgId = aOperation.mMsgId;
      nsAtom* atom = aOperation.mName;
      nsAtom* otherAtom = aOperation.mOther;
      // See viewsource.css for the possible classes in addition to "error".
      nsAutoString klass;
      element->GetAttr(nsGkAtoms::_class, klass);
      if (!klass.IsEmpty()) {
        klass.AppendLiteral(" error");
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass, true);
      } else {
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, u"error"_ns,
                         true);
      }

      nsresult rv;
      nsAutoString message;
      if (otherAtom) {
        rv = nsContentUtils::FormatLocalizedString(
            message, PropertiesFile::HTMLPARSER_PROPERTIES, msgId,
            nsDependentAtomString(atom), nsDependentAtomString(otherAtom));
        NS_ENSURE_SUCCESS(rv, NS_OK);
      } else if (atom) {
        rv = nsContentUtils::FormatLocalizedString(
            message, PropertiesFile::HTMLPARSER_PROPERTIES, msgId,
            nsDependentAtomString(atom));
        NS_ENSURE_SUCCESS(rv, NS_OK);
      } else {
        rv = nsContentUtils::GetLocalizedString(
            PropertiesFile::HTMLPARSER_PROPERTIES, msgId, message);
        NS_ENSURE_SUCCESS(rv, NS_OK);
      }

      nsAutoString title;
      element->GetAttr(nsGkAtoms::title, title);
      if (!title.IsEmpty()) {
        title.Append('\n');
        title.Append(message);
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::title, title, true);
      } else {
        element->SetAttr(kNameSpaceID_None, nsGkAtoms::title, message, true);
      }
      return rv;
    }

    nsresult operator()(const opAddLineNumberId& aOperation) {
      Element* element = (*(aOperation.mElement))->AsElement();
      int32_t lineNumber = aOperation.mLineNumber;
      nsAutoString val(u"line"_ns);
      val.AppendInt(lineNumber);
      element->SetAttr(kNameSpaceID_None, nsGkAtoms::id, val, true);
      return NS_OK;
    }

    nsresult operator()(const opShallowCloneInto& aOperation) {
      nsIContent* src = *aOperation.mSrc;
      ErrorResult rv;
      RefPtr<nsINode> clone = src->CloneNode(false, rv);
      if (NS_WARN_IF(rv.Failed())) {
        return rv.StealNSResult();
      }
      *aOperation.mDst = clone->AsContent();
      mBuilder->HoldElement(clone.forget().downcast<nsIContent>());
      return Append(*aOperation.mDst, *aOperation.mIntendedParent,
                    aOperation.mFromParser, mBuilder);
    }

    nsresult operator()(const opStartLayout& aOperation) {
      mBuilder->StartLayout(
          mInterrupted);  // this causes a notification flush anyway
      return NS_OK;
    }

    nsresult operator()(const opEnableEncodingMenu& aOperation) {
      Document* doc = mBuilder->GetDocument();
      doc->EnableEncodingMenu();
      return NS_OK;
    }

    nsresult operator()(const opMicrotaskCheckpoint& aOperation) {
      nsHtml5AutoPauseUpdate autoPauseContentUpdate(mBuilder);
      nsAutoMicroTask mt;
      return NS_OK;
    }

    nsresult operator()(const uninitialized& aOperation) {
      MOZ_CRASH("uninitialized");
      return NS_OK;
    }
  };

  return mOperation.match(TreeOperationMatcher(aBuilder, aScriptElement,
                                               aInterrupted, aStreamEnded));
}
