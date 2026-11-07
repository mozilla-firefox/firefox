/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULPrototypeDocument.h"

#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Text.h"
#include "nsAString.h"
#include "nsCCUncollectableMarker.h"
#include "nsContentUtils.h"
#include "nsDOMCID.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIURI.h"
#include "nsJSPrincipals.h"
#include "nsNodeInfoManager.h"
#include "nsString.h"
#include "nsXULElement.h"
#include "nsXULPrototypeCache.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;
using mozilla::dom::DestroyProtoAndIfaceCache;

//----------------------------------------------------------------------
//
// ctors, dtors, n' stuff
//

nsXULPrototypeDocument::nsXULPrototypeDocument() = default;

nsresult nsXULPrototypeDocument::Init() {
  mNodeInfoManager = new nsNodeInfoManager(nullptr, nullptr);
  return NS_OK;
}

nsXULPrototypeDocument::~nsXULPrototypeDocument() {
  if (mRoot) mRoot->ReleaseSubtree();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULPrototypeDocument)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXULPrototypeDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPrototypeWaiters)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXULPrototypeDocument)
  if (nsCCUncollectableMarker::InGeneration(cb, tmp->mCCGeneration)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRoot)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNodeInfoManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXULPrototypeDocument)
  NS_INTERFACE_MAP_ENTRY(nsISerializable)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsXULPrototypeDocument)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsXULPrototypeDocument)

NS_IMETHODIMP
NS_NewXULPrototypeDocument(nsXULPrototypeDocument** aResult) {
  *aResult = nullptr;
  RefPtr<nsXULPrototypeDocument> doc = new nsXULPrototypeDocument();

  nsresult rv = doc->Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  doc.forget(aResult);
  return rv;
}

//----------------------------------------------------------------------
//
// nsISerializable methods
//

NS_IMETHODIMP
nsXULPrototypeDocument::Read(nsIObjectInputStream* aStream) {
  nsCOMPtr<nsISupports> supports;
  nsresult rv = aStream->ReadObject(true, getter_AddRefs(supports));
  if (NS_FAILED(rv)) {
    return rv;
  }
  mURI = do_QueryInterface(supports);

  // nsIPrincipal mNodeInfoManager->mPrincipal
  nsAutoCString JSON;
  rv = aStream->ReadCString(JSON);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCOMPtr<nsIPrincipal> principal = mozilla::BasePrincipal::FromJSON(JSON);

  // Better safe than sorry....
  mNodeInfoManager->SetDocumentPrincipal(principal);

  mRoot = new nsXULPrototypeElement();

  // mozilla::dom::NodeInfo table
  nsTArray<RefPtr<mozilla::dom::NodeInfo>> nodeInfos;

  uint32_t count, i;
  rv = aStream->Read32(&count);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsAutoString namespaceURI, prefixStr, localName;
  bool prefixIsNull;
  RefPtr<nsAtom> prefix;
  for (i = 0; i < count; ++i) {
    rv = aStream->ReadString(namespaceURI);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = aStream->ReadBoolean(&prefixIsNull);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (prefixIsNull) {
      prefix = nullptr;
    } else {
      rv = aStream->ReadString(prefixStr);
      if (NS_FAILED(rv)) {
        return rv;
      }
      prefix = NS_Atomize(prefixStr);
    }
    rv = aStream->ReadString(localName);
    if (NS_FAILED(rv)) {
      return rv;
    }

    RefPtr<mozilla::dom::NodeInfo> nodeInfo;
    // Using UINT16_MAX here as we don't know which nodeinfos will be
    // used for attributes and which for elements. And that doesn't really
    // matter.
    rv = mNodeInfoManager->GetNodeInfo(localName, prefix, namespaceURI,
                                       UINT16_MAX, getter_AddRefs(nodeInfo));
    if (NS_FAILED(rv)) {
      return rv;
    }
    nodeInfos.AppendElement(nodeInfo);
  }

  // Document contents
  uint32_t type;
  while (NS_SUCCEEDED(rv)) {
    rv = aStream->Read32(&type);
    if (NS_FAILED(rv)) {
      return rv;
      break;
    }

    if ((nsXULPrototypeNode::Type)type == nsXULPrototypeNode::eType_PI) {
      RefPtr<nsXULPrototypePI> pi = new nsXULPrototypePI();

      rv = pi->Deserialize(aStream, this, mURI, &nodeInfos);
      if (NS_FAILED(rv)) {
        return rv;
      }
      rv = AddProcessingInstruction(pi);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } else if ((nsXULPrototypeNode::Type)type ==
               nsXULPrototypeNode::eType_Element) {
      rv = mRoot->Deserialize(aStream, this, mURI, &nodeInfos);
      if (NS_FAILED(rv)) {
        return rv;
      }
      break;
    } else {
      MOZ_ASSERT_UNREACHABLE("Unexpected prototype node type");
      return NS_ERROR_FAILURE;
    }
  }

  return NotifyLoadDone();
}

static void GetNodeInfos(nsXULPrototypeElement* aPrototype,
                         nsTArray<RefPtr<mozilla::dom::NodeInfo>>& aArray) {
  if (aArray.IndexOf(aPrototype->mNodeInfo) == aArray.NoIndex) {
    aArray.AppendElement(aPrototype->mNodeInfo);
  }

  // Search attributes
  for (nsXULPrototypeAttribute& attr : aPrototype->mAttributes) {
    RefPtr<mozilla::dom::NodeInfo> ni;
    nsAttrName* name = &attr.mName;
    if (name->IsAtom()) {
      ni = aPrototype->mNodeInfo->NodeInfoManager()->GetNodeInfo(
          name->Atom(), nullptr, kNameSpaceID_None, nsINode::ATTRIBUTE_NODE);
    } else {
      ni = name->NodeInfo();
    }

    if (aArray.IndexOf(ni) == aArray.NoIndex) {
      aArray.AppendElement(ni);
    }
  }

  // Search children
  for (nsXULPrototypeNode* child : aPrototype->mChildren) {
    if (child->mType == nsXULPrototypeNode::eType_Element) {
      GetNodeInfos(static_cast<nsXULPrototypeElement*>(child), aArray);
    }
  }
}

NS_IMETHODIMP
nsXULPrototypeDocument::Write(nsIObjectOutputStream* aStream) {
  nsresult rv;

  rv = aStream->WriteCompoundObject(mURI, NS_GET_IID(nsIURI), true);

  // nsIPrincipal mNodeInfoManager->mPrincipal
  nsAutoCString JSON;
  mozilla::BasePrincipal::Cast(mNodeInfoManager->DocumentPrincipal())
      ->ToJSON(JSON);
  nsresult tmp = aStream->WriteStringZ(JSON.get());
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }

#ifdef DEBUG
  // XXX Worrisome if we're caching things without system principal.
  if (!mNodeInfoManager->DocumentPrincipal()->IsSystemPrincipal()) {
    NS_WARNING("Serializing document without system principal");
  }
#endif

  // mozilla::dom::NodeInfo table
  nsTArray<RefPtr<mozilla::dom::NodeInfo>> nodeInfos;
  if (mRoot) {
    GetNodeInfos(mRoot, nodeInfos);
  }

  uint32_t nodeInfoCount = nodeInfos.Length();
  tmp = aStream->Write32(nodeInfoCount);
  if (NS_FAILED(tmp)) {
    rv = tmp;
  }
  uint32_t i;
  for (i = 0; i < nodeInfoCount; ++i) {
    mozilla::dom::NodeInfo* nodeInfo = nodeInfos[i];
    NS_ENSURE_TRUE(nodeInfo, NS_ERROR_FAILURE);

    nsAutoString namespaceURI;
    nodeInfo->GetNamespaceURI(namespaceURI);
    tmp = aStream->WriteWStringZ(namespaceURI.get());
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }

    nsAutoString prefix;
    nodeInfo->GetPrefix(prefix);
    bool nullPrefix = DOMStringIsNull(prefix);
    tmp = aStream->WriteBoolean(nullPrefix);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
    if (!nullPrefix) {
      tmp = aStream->WriteWStringZ(prefix.get());
      if (NS_FAILED(tmp)) {
        rv = tmp;
      }
    }

    nsAutoString localName;
    nodeInfo->GetName(localName);
    tmp = aStream->WriteWStringZ(localName.get());
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
  }

  // Now serialize the document contents
  uint32_t count = mProcessingInstructions.Length();
  for (i = 0; i < count; ++i) {
    nsXULPrototypePI* pi = mProcessingInstructions[i];
    tmp = pi->Serialize(aStream, this, &nodeInfos);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
  }

  if (mRoot) {
    tmp = mRoot->Serialize(aStream, this, &nodeInfos);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
  }

  return rv;
}

//----------------------------------------------------------------------
//

nsresult nsXULPrototypeDocument::InitPrincipal(nsIURI* aURI,
                                               nsIPrincipal* aPrincipal) {
  NS_ENSURE_ARG_POINTER(aURI);

  mURI = aURI;
  mNodeInfoManager->SetDocumentPrincipal(aPrincipal);
  return NS_OK;
}

nsIURI* nsXULPrototypeDocument::GetURI() {
  NS_ASSERTION(mURI, "null URI");
  return mURI;
}

nsXULPrototypeElement* nsXULPrototypeDocument::GetRootElement() {
  return mRoot;
}

void nsXULPrototypeDocument::SetRootElement(nsXULPrototypeElement* aElement) {
  mRoot = aElement;
}

nsresult nsXULPrototypeDocument::AddProcessingInstruction(
    nsXULPrototypePI* aPI) {
  MOZ_ASSERT(aPI, "null ptr");
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  mProcessingInstructions.AppendElement(aPI);
  return NS_OK;
}

const nsTArray<RefPtr<nsXULPrototypePI>>&
nsXULPrototypeDocument::GetProcessingInstructions() const {
  return mProcessingInstructions;
}

nsIPrincipal* nsXULPrototypeDocument::DocumentPrincipal() {
  MOZ_ASSERT(mNodeInfoManager, "missing nodeInfoManager");
  return mNodeInfoManager->DocumentPrincipal();
}

void nsXULPrototypeDocument::SetDocumentPrincipal(nsIPrincipal* aPrincipal) {
  mNodeInfoManager->SetDocumentPrincipal(aPrincipal);
}

void nsXULPrototypeDocument::MarkInCCGeneration(uint32_t aCCGeneration) {
  mCCGeneration = aCCGeneration;
}

nsNodeInfoManager* nsXULPrototypeDocument::GetNodeInfoManager() {
  return mNodeInfoManager;
}

nsresult nsXULPrototypeDocument::AwaitLoadDone(Callback&& aCallback,
                                               bool* aResult) {
  nsresult rv = NS_OK;

  *aResult = mLoaded;

  if (!mLoaded) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier, or change the return type to void.
    mPrototypeWaiters.AppendElement(std::move(aCallback));
  }

  return rv;
}

nsresult nsXULPrototypeDocument::NotifyLoadDone() {
  // Call back to each XUL document that raced to start the same
  // prototype document load, lost the race, but hit the XUL
  // prototype cache because the winner filled the cache with
  // the not-yet-loaded prototype object.

  mLoaded = true;

  for (uint32_t i = mPrototypeWaiters.Length(); i > 0;) {
    --i;
    mPrototypeWaiters[i]();
  }
  mPrototypeWaiters.Clear();

  return NS_OK;
}

void nsXULPrototypeDocument::RebuildPrototypeFromElement(
    nsXULPrototypeElement* aPrototype, Element* aElement, bool aDeep) {
  NodeInfo* oldNodeInfo = aElement->NodeInfo();
  RefPtr<NodeInfo> newNodeInfo = mNodeInfoManager->GetNodeInfo(
      oldNodeInfo->NameAtom(), oldNodeInfo->GetPrefixAtom(),
      oldNodeInfo->NamespaceID(), nsINode::ELEMENT_NODE);
  aPrototype->mNodeInfo = newNodeInfo;

  // First replace the prototype attributes with the new ones from this element.
  aPrototype->mAttributes.Clear();

  uint32_t count = aElement->GetAttrCount();
  nsXULPrototypeAttribute* protoAttr =
      aPrototype->mAttributes.AppendElements(count);
  for (uint32_t index = 0; index < count; index++) {
    BorrowedAttrInfo attr = aElement->GetAttrInfoAt(index);

    if (attr.mName->IsAtom()) {
      protoAttr->mName.SetTo(attr.mName->Atom());
    } else {
      NodeInfo* oldNodeInfo = attr.mName->NodeInfo();
      RefPtr<NodeInfo> newNodeInfo = mNodeInfoManager->GetNodeInfo(
          oldNodeInfo->NameAtom(), oldNodeInfo->GetPrefixAtom(),
          oldNodeInfo->NamespaceID(), nsINode::ATTRIBUTE_NODE);
      protoAttr->mName.SetTo(newNodeInfo);
    }
    protoAttr->mValue.SetTo(*attr.mValue);
    if (protoAttr->mValue.Type() == nsAttrValue::eCSSDeclaration) {
      // Ensure declarations get copied-on-write if needed.
      protoAttr->mValue.GetCSSDeclarationValue()->SetImmutable();
    }
    protoAttr++;
  }

  // Make sure the mIsAtom is correct in case this prototype element has been
  // completely rebuilt.
  CustomElementData* ceData = aElement->GetCustomElementData();
  nsAtom* isAtom = ceData ? ceData->GetIs(aElement) : nullptr;
  aPrototype->mIsAtom = isAtom;

  if (aDeep) {
    // We have to rebuild the prototype children from this element.
    // First release the tree under this element.
    aPrototype->ReleaseSubtree();

    RefPtr<nsXULPrototypeNode>* children =
        aPrototype->mChildren.AppendElements(aElement->GetChildCount());
    for (nsIContent* child = aElement->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      if (child->IsElement()) {
        Element* element = child->AsElement();
        RefPtr<nsXULPrototypeElement> elemProto = new nsXULPrototypeElement;
        RebuildPrototypeFromElement(elemProto, element, true);
        *children = elemProto;
      } else if (child->IsText()) {
        Text* text = child->AsText();
        RefPtr<nsXULPrototypeText> textProto = new nsXULPrototypeText();
        text->AppendTextTo(textProto->mValue);
        *children = textProto;
      } else {
        MOZ_ASSERT(false, "We handle only elements and text nodes here.");
      }

      children++;
    }
  }
}

void nsXULPrototypeDocument::MaybeRebuildPrototypeAfterTranslation(
    Element* aElement, bool aDeep) {
  MOZ_ASSERT(aElement->HasAttr(nsGkAtoms::datal10nid));
  Document* doc = aElement->OwnerDoc();
  if (RefPtr<nsXULPrototypeElement> proto =
          doc->mUntranslatedL10nProtoElements.Get(aElement)) {
    RebuildPrototypeFromElement(proto, aElement, aDeep);
    proto->mTranslated = true;
  }
}
