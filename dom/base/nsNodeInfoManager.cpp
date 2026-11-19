/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A class for handing out nodeinfos and ensuring sharing of them as needed.
 */

#include "nsNodeInfoManager.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/NodeInfoInlines.h"
#include "nsAtom.h"
#include "nsCCUncollectableMarker.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsHashKeys.h"
#include "nsIPrincipal.h"
#include "nsLayoutStatics.h"
#include "nsNameSpaceManager.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsWindowSizes.h"

using namespace mozilla;
using mozilla::dom::NodeInfo;

#include "mozilla/Logging.h"

static LazyLogModule gNodeInfoManagerLeakPRLog("NodeInfoManagerLeak");
static const uint32_t kInitialNodeInfoHashSize = 32;

nsNodeInfoManager::nsNodeInfoManager(mozilla::dom::Document* aDocument,
                                     nsIPrincipal* aPrincipal)
    : mNodeInfoHash(kInitialNodeInfoHashSize), mDocument(aDocument) {
  nsLayoutStatics::AddRef();

  if (aPrincipal) {
    mPrincipal = aPrincipal;
  } else {
    mPrincipal = NullPrincipal::CreateWithoutOriginAttributes();
  }
  mDefaultPrincipal = mPrincipal;

  if (gNodeInfoManagerLeakPRLog) {
    MOZ_LOG(gNodeInfoManagerLeakPRLog, LogLevel::Debug,
            ("NODEINFOMANAGER %p created,  document=%p", this, aDocument));
  }
}

nsNodeInfoManager::~nsNodeInfoManager() {
  // Note: mPrincipal may be null here if we never got inited correctly
  mPrincipal = nullptr;

  mArena = nullptr;

  if (gNodeInfoManagerLeakPRLog)
    MOZ_LOG(gNodeInfoManagerLeakPRLog, LogLevel::Debug,
            ("NODEINFOMANAGER %p destroyed", this));

  nsLayoutStatics::Release();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsNodeInfoManager)

NS_IMPL_CYCLE_COLLECTION_UNLINK_0(nsNodeInfoManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsNodeInfoManager)
  if (tmp->mNonDocumentNodeInfos) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mDocument)
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsNodeInfoManager)
  if (tmp->mDocument) {
    return NS_CYCLE_COLLECTION_PARTICIPANT(mozilla::dom::Document)
        ->CanSkip(tmp->mDocument, aRemovingAllowed);
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsNodeInfoManager)
  if (tmp->mDocument) {
    return NS_CYCLE_COLLECTION_PARTICIPANT(mozilla::dom::Document)
        ->CanSkipInCC(tmp->mDocument);
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsNodeInfoManager)
  if (tmp->mDocument) {
    return NS_CYCLE_COLLECTION_PARTICIPANT(mozilla::dom::Document)
        ->CanSkipThis(tmp->mDocument);
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

void nsNodeInfoManager::DropDocumentReference() {
  // This is probably not needed anymore.
  for (const auto& entry : mNodeInfoHash.Values()) {
    entry->mDocument = nullptr;
  }

  NS_ASSERTION(!mNonDocumentNodeInfos,
               "Shouldn't have non-document nodeinfos!");
  mDocument = nullptr;
}

already_AddRefed<mozilla::dom::NodeInfo> nsNodeInfoManager::GetNodeInfo(
    nsAtom* aName, nsAtom* aPrefix, int32_t aNamespaceID, uint16_t aNodeType,
    nsAtom* aExtraName /* = nullptr */) {
  CheckValidNodeInfo(aNodeType, aName, aNamespaceID, aExtraName);

  NodeInfo::NodeInfoInner tmpKey(aName, aPrefix, aNamespaceID, aNodeType,
                                 aExtraName);

  auto p = mRecentlyUsedNodeInfos.Lookup(tmpKey);
  if (p) {
    RefPtr<NodeInfo> nodeInfo = p.Data();
    return nodeInfo.forget();
  }

  // We don't use WithEntryHandle here as that would end up storing the
  // temporary key instead of using `mInner`.
  RefPtr<NodeInfo> nodeInfo = mNodeInfoHash.Get(&tmpKey);
  if (!nodeInfo) {
    ++mNonDocumentNodeInfos;
    if (mNonDocumentNodeInfos == 1) {
      NS_IF_ADDREF(mDocument);
    }

    nodeInfo =
        new NodeInfo(aName, aPrefix, aNamespaceID, aNodeType, aExtraName, this);
    mNodeInfoHash.InsertOrUpdate(&nodeInfo->mInner, nodeInfo);
  }

  // Have to do the swap thing, because already_AddRefed<nsNodeInfo>
  // doesn't cast to already_AddRefed<mozilla::dom::NodeInfo>
  p.Set(nodeInfo);
  return nodeInfo.forget();
}

nsresult nsNodeInfoManager::GetNodeInfo(const nsAString& aName, nsAtom* aPrefix,
                                        int32_t aNamespaceID,
                                        uint16_t aNodeType,
                                        NodeInfo** aNodeInfo) {
  // TODO(erahm): Combine this with the atom version.
#ifdef DEBUG
  {
    RefPtr<nsAtom> nameAtom = NS_Atomize(aName);
    CheckValidNodeInfo(aNodeType, nameAtom, aNamespaceID, nullptr);
  }
#endif

  NodeInfo::NodeInfoInner tmpKey(aName, aPrefix, aNamespaceID, aNodeType);

  auto p = mRecentlyUsedNodeInfos.Lookup(tmpKey);
  if (p) {
    RefPtr<NodeInfo> nodeInfo = p.Data();
    nodeInfo.forget(aNodeInfo);
    return NS_OK;
  }

  RefPtr<NodeInfo> nodeInfo = mNodeInfoHash.Get(&tmpKey);
  if (!nodeInfo) {
    ++mNonDocumentNodeInfos;
    if (mNonDocumentNodeInfos == 1) {
      NS_IF_ADDREF(mDocument);
    }

    RefPtr<nsAtom> nameAtom = NS_Atomize(aName);
    nodeInfo =
        new NodeInfo(nameAtom, aPrefix, aNamespaceID, aNodeType, nullptr, this);
    mNodeInfoHash.InsertOrUpdate(&nodeInfo->mInner, nodeInfo);
  }

  p.Set(nodeInfo);
  nodeInfo.forget(aNodeInfo);

  return NS_OK;
}

nsresult nsNodeInfoManager::GetNodeInfo(const nsAString& aName, nsAtom* aPrefix,
                                        const nsAString& aNamespaceURI,
                                        uint16_t aNodeType,
                                        NodeInfo** aNodeInfo) {
  int32_t nsid = kNameSpaceID_None;

  if (!aNamespaceURI.IsEmpty()) {
    nsresult rv = nsNameSpaceManager::GetInstance()->RegisterNameSpace(
        aNamespaceURI, nsid);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return GetNodeInfo(aName, aPrefix, nsid, aNodeType, aNodeInfo);
}

already_AddRefed<NodeInfo> nsNodeInfoManager::GetTextNodeInfo() {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;

  if (!mTextNodeInfo) {
    nodeInfo = GetNodeInfo(nsGkAtoms::textTagName, nullptr, kNameSpaceID_None,
                           nsINode::TEXT_NODE, nullptr);
    // Hold a weak ref; the nodeinfo will let us know when it goes away
    mTextNodeInfo = nodeInfo;
  } else {
    nodeInfo = mTextNodeInfo;
  }

  return nodeInfo.forget();
}

already_AddRefed<NodeInfo> nsNodeInfoManager::GetDocumentFragmentNodeInfo() {
  RefPtr<NodeInfo> nodeInfo = mDocumentFragmentNodeInfo;
  if (!nodeInfo) {
    nodeInfo = GetNodeInfo(nsGkAtoms::documentFragmentNodeName, nullptr,
                           kNameSpaceID_None, nsINode::DOCUMENT_FRAGMENT_NODE);
    mDocumentFragmentNodeInfo = nodeInfo;
  }
  return nodeInfo.forget();
}

already_AddRefed<NodeInfo> nsNodeInfoManager::GetCommentNodeInfo() {
  RefPtr<NodeInfo> nodeInfo;

  if (!mCommentNodeInfo) {
    nodeInfo = GetNodeInfo(nsGkAtoms::commentTagName, nullptr,
                           kNameSpaceID_None, nsINode::COMMENT_NODE, nullptr);
    // Hold a weak ref; the nodeinfo will let us know when it goes away
    mCommentNodeInfo = nodeInfo;
  } else {
    nodeInfo = mCommentNodeInfo;
  }

  return nodeInfo.forget();
}

already_AddRefed<NodeInfo> nsNodeInfoManager::GetDocumentNodeInfo() {
  RefPtr<NodeInfo> nodeInfo;

  if (!mDocumentNodeInfo) {
    NS_ASSERTION(mDocument, "Should have mDocument!");
    nodeInfo = GetNodeInfo(nsGkAtoms::documentNodeName, nullptr,
                           kNameSpaceID_None, nsINode::DOCUMENT_NODE, nullptr);
    // Hold a weak ref; the nodeinfo will let us know when it goes away
    mDocumentNodeInfo = nodeInfo;

    --mNonDocumentNodeInfos;
    if (!mNonDocumentNodeInfos) {
      mDocument->Release();  // Don't set mDocument to null!
    }
  } else {
    nodeInfo = mDocumentNodeInfo;
  }

  return nodeInfo.forget();
}

void* nsNodeInfoManager::Allocate(size_t aSize) {
  if (!mHasAllocated) {
    if (!mArena) {
      mozilla::dom::DocGroup* docGroup = GetDocument()->GetDocGroupOrCreate();
      if (docGroup) {
        MOZ_ASSERT(!GetDocument()->HasChildren());
        mArena = docGroup->ArenaAllocator();
      }
    }
#ifdef DEBUG
    else {
      mozilla::dom::DocGroup* docGroup = GetDocument()->GetDocGroup();
      MOZ_ASSERT(docGroup);
      MOZ_ASSERT(mArena == docGroup->ArenaAllocator());
    }
#endif
    mHasAllocated = true;
  }

  if (mArena) {
    return mArena->Allocate(aSize);
  }
  return moz_xmalloc(aSize);
}

void nsNodeInfoManager::SetArenaAllocator(mozilla::dom::DOMArena* aArena) {
  MOZ_DIAGNOSTIC_ASSERT_IF(mArena, mArena == aArena);
  MOZ_DIAGNOSTIC_ASSERT(!mHasAllocated);
  if (!mArena) {
    mArena = aArena;
  }
}

void nsNodeInfoManager::SetDocumentPrincipal(nsIPrincipal* aPrincipal) {
  mPrincipal = nullptr;
  if (!aPrincipal) {
    aPrincipal = mDefaultPrincipal;
  }

  NS_ASSERTION(aPrincipal, "Must have principal by this point!");
  MOZ_DIAGNOSTIC_ASSERT(!nsContentUtils::IsExpandedPrincipal(aPrincipal),
                        "Documents shouldn't have an expanded principal");

  mPrincipal = aPrincipal;
}

void nsNodeInfoManager::RemoveNodeInfo(NodeInfo* aNodeInfo) {
  MOZ_ASSERT(aNodeInfo, "Trying to remove null nodeinfo from manager!");

  if (aNodeInfo == mDocumentNodeInfo) {
    mDocumentNodeInfo = nullptr;
    mDocument = nullptr;
  } else {
    if (--mNonDocumentNodeInfos == 0) {
      if (mDocument) {
        // Note, whoever calls this method should keep NodeInfoManager alive,
        // even if mDocument gets deleted.
        mDocument->Release();
      }
    }
    // Drop weak reference if needed
    if (aNodeInfo == mTextNodeInfo) {
      mTextNodeInfo = nullptr;
    } else if (aNodeInfo == mCommentNodeInfo) {
      mCommentNodeInfo = nullptr;
    } else if (aNodeInfo == mDocumentFragmentNodeInfo) {
      mDocumentFragmentNodeInfo = nullptr;
    }
  }

  mRecentlyUsedNodeInfos.Remove(aNodeInfo->mInner);
  DebugOnly<bool> ret = mNodeInfoHash.Remove(&aNodeInfo->mInner);
  MOZ_ASSERT(ret, "Can't find mozilla::dom::NodeInfo to remove!!!");
}

static bool IsSystemOrAddonOrAboutPrincipal(nsIPrincipal* aPrincipal) {
  return aPrincipal->IsSystemPrincipal() ||
         BasePrincipal::Cast(aPrincipal)->AddonPolicy() ||
         // NOTE: about:blank and about:srcdoc inherit the principal of their
         // parent, so aPrincipal->SchemeIs("about") returns false for them.
         aPrincipal->SchemeIs("about");
}

static bool IsAndroidResource(nsIURI* aURI) {
#ifdef ANDROID
  if (aURI->SchemeIs("resource")) {
    nsAutoCString host, path;
    aURI->GetHost(host);
    aURI->GetFilePath(path);
    if (host.EqualsLiteral("android") &&
        StringBeginsWith(path, "/assets/"_ns)) {
      return true;
    }
  }
#endif
  return false;
}

bool nsNodeInfoManager::InternalSVGEnabled() {
  MOZ_ASSERT(!mSVGEnabled, "Caller should use the cached mSVGEnabled!");

  // If the svg.disabled pref. is true, convert all SVG nodes into
  // disabled SVG nodes by swapping the namespace.
  nsNameSpaceManager* nsmgr = nsNameSpaceManager::GetInstance();
  nsCOMPtr<nsILoadInfo> loadInfo;
  bool SVGEnabled = false;

  if (nsmgr && !nsmgr->mSVGDisabled) {
    SVGEnabled = true;
  } else {
    nsCOMPtr<nsIChannel> channel = mDocument->GetChannel();
    // We don't have a channel for SVGs constructed inside a SVG script
    if (channel) {
      loadInfo = channel->LoadInfo();
    }
  }

  // We allow SVG (regardless of the pref) if this is a system or add-on
  // principal or about: page, or if this load was requested for a system or
  // add-on principal or about: page (e.g. a remote image being served as part
  // of system or add-on UI or about: page)
  bool conclusion =
      (SVGEnabled || IsSystemOrAddonOrAboutPrincipal(mPrincipal) ||
       IsAndroidResource(mDocument->GetDocumentURI()) ||
       (loadInfo &&
        (loadInfo->GetExternalContentPolicyType() ==
             ExtContentPolicy::TYPE_IMAGE ||
         loadInfo->GetExternalContentPolicyType() ==
             ExtContentPolicy::TYPE_OTHER) &&
        (IsSystemOrAddonOrAboutPrincipal(loadInfo->GetLoadingPrincipal()) ||
         IsSystemOrAddonOrAboutPrincipal(loadInfo->TriggeringPrincipal()))));
  mSVGEnabled = Some(conclusion);
  return conclusion;
}

bool nsNodeInfoManager::InternalMathMLEnabled() {
  MOZ_ASSERT(!mMathMLEnabled, "Caller should use the cached mMathMLEnabled!");

  // If the mathml.disabled pref. is true, convert all MathML nodes into
  // disabled MathML nodes by swapping the namespace.
  nsNameSpaceManager* nsmgr = nsNameSpaceManager::GetInstance();
  bool conclusion =
      ((nsmgr && !nsmgr->mMathMLDisabled) || mPrincipal->IsSystemPrincipal());
  mMathMLEnabled = Some(conclusion);
  return conclusion;
}

void nsNodeInfoManager::AddSizeOfIncludingThis(nsWindowSizes& aSizes) const {
  aSizes.mDOMSizes.mDOMOtherSize += aSizes.mState.mMallocSizeOf(this);

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mNodeInfoHash
}
