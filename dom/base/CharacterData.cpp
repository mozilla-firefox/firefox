/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for DOM Core's Comment, DocumentType, Text,
 * CDATASection and ProcessingInstruction nodes.
 */

#include "mozilla/dom/CharacterData.h"

#include "mozAutoDocUpdate.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Sprintf.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/UnbindContext.h"
#include "nsBidiUtils.h"
#include "nsIContentInlines.h"
#include "nsReadableUtils.h"
#include "nsTextNode.h"
#include "nsWindowSizes.h"

#if defined(ACCESSIBILITY) && defined(DEBUG)
#  include "nsAccessibilityService.h"
#endif

namespace mozilla::dom {

CharacterData::CharacterData(already_AddRefed<dom::NodeInfo> aNodeInfo)
    : nsIContent(std::move(aNodeInfo)) {
  MOZ_ASSERT(mNodeInfo->NodeType() == TEXT_NODE ||
                 mNodeInfo->NodeType() == CDATA_SECTION_NODE ||
                 mNodeInfo->NodeType() == COMMENT_NODE ||
                 mNodeInfo->NodeType() == PROCESSING_INSTRUCTION_NODE ||
                 mNodeInfo->NodeType() == DOCUMENT_TYPE_NODE,
             "Bad NodeType in aNodeInfo");
}

CharacterData::~CharacterData() {
  MOZ_ASSERT(!IsInUncomposedDoc(),
             "Please remove this from the document properly");
  if (GetParent()) {
    NS_RELEASE(mParent);
  }
}

Element* CharacterData::GetNameSpaceElement() {
  return Element::FromNodeOrNull(GetParentNode());
}

// Note, _INHERITED macro isn't used here since nsINode implementations are
// rather special.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(CharacterData)

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(CharacterData)
  return Element::CanSkip(tmp, aRemovingAllowed);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(CharacterData)
  return Element::CanSkipInCC(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(CharacterData)
  return Element::CanSkipThis(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// We purposefully don't TRAVERSE_BEGIN_INHERITED here.  All the bits
// we should traverse should be added here or in nsINode::Traverse.
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(CharacterData)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    char name[40];
    SprintfLiteral(name, "CharacterData (len=%d)", tmp->mBuffer.GetLength());
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name);
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(CharacterData, tmp->mRefCnt.get())
  }

  if (!nsIContent::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

// We purposefully don't UNLINK_BEGIN_INHERITED here.
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CharacterData)
  nsIContent::Unlink(tmp);

  if (nsContentSlots* slots = tmp->GetExistingContentSlots()) {
    slots->Unlink(*tmp);
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN(CharacterData)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(CharacterData)
NS_INTERFACE_MAP_END_INHERITING(nsIContent)

void CharacterData::GetNodeValueInternal(nsAString& aNodeValue) {
  GetData(aNodeValue);
}

void CharacterData::SetNodeValueInternal(
    const nsAString& aNodeValue, ErrorResult& aError,
    MutationEffectOnScript aMutationEffectOnScript) {
  aError = SetTextInternal(0, mBuffer.GetLength(), aNodeValue.BeginReading(),
                           aNodeValue.Length(), true, aMutationEffectOnScript);
}

//----------------------------------------------------------------------

// Implementation of CharacterData

void CharacterData::SetTextContentInternal(
    const nsAString& aTextContent, nsIPrincipal* aSubjectPrincipal,
    ErrorResult& aError, MutationEffectOnScript aMutationEffectOnScript) {
  return SetNodeValueInternal(aTextContent, aError, aMutationEffectOnScript);
}

void CharacterData::GetData(nsAString& aData) const {
  if (mBuffer.Is2b()) {
    aData.Truncate();
    mBuffer.AppendTo(aData);
  } else {
    // Must use Substring() since nsDependentCString() requires null
    // terminated strings.
    if (const char* data = mBuffer.Get1b()) {
      CopyASCIItoUTF16(Substring(data, data + mBuffer.GetLength()), aData);
    } else {
      aData.Truncate();
    }
  }
}

void CharacterData::SetDataInternal(
    const nsAString& aData, MutationEffectOnScript aMutationEffectOnScript,
    ErrorResult& aRv) {
  nsresult rv = SetTextInternal(0, mBuffer.GetLength(), aData.BeginReading(),
                                aData.Length(), true, aMutationEffectOnScript);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

void CharacterData::SubstringData(uint32_t aStart, uint32_t aCount,
                                  nsAString& aReturn, ErrorResult& rv) {
  aReturn.Truncate();

  uint32_t textLength = mBuffer.GetLength();
  if (aStart > textLength) {
    rv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  uint32_t amount = aCount;
  if (amount > textLength - aStart) {
    amount = textLength - aStart;
  }

  if (mBuffer.Is2b()) {
    aReturn.Assign(mBuffer.Get2b() + aStart, amount);
  } else {
    // Must use Substring() since nsDependentCString() requires null
    // terminated strings.

    const char* data = mBuffer.Get1b() + aStart;
    CopyASCIItoUTF16(Substring(data, data + amount), aReturn);
  }
}

//----------------------------------------------------------------------

void CharacterData::AppendDataInternal(
    const nsAString& aData, MutationEffectOnScript aMutationEffectOnScript,
    ErrorResult& aRv) {
  InsertDataInternal(mBuffer.GetLength(), aData, aMutationEffectOnScript, aRv);
}

void CharacterData::InsertDataInternal(
    uint32_t aOffset, const nsAString& aData,
    MutationEffectOnScript aMutationEffectOnScript, ErrorResult& aRv) {
  nsresult rv = SetTextInternal(aOffset, 0, aData.BeginReading(),
                                aData.Length(), true, aMutationEffectOnScript);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

void CharacterData::DeleteDataInternal(
    uint32_t aOffset, uint32_t aCount,
    MutationEffectOnScript aMutationEffectOnScript, ErrorResult& aRv) {
  nsresult rv = SetTextInternal(aOffset, aCount, nullptr, 0, true,
                                aMutationEffectOnScript);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

void CharacterData::ReplaceDataInternal(
    uint32_t aOffset, uint32_t aCount, const nsAString& aData,
    MutationEffectOnScript aMutationEffectOnScript, ErrorResult& aRv) {
  nsresult rv = SetTextInternal(aOffset, aCount, aData.BeginReading(),
                                aData.Length(), true, aMutationEffectOnScript);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

nsresult CharacterData::SetTextInternal(
    uint32_t aOffset, uint32_t aCount, const char16_t* aBuffer,
    uint32_t aLength, bool aNotify,
    MutationEffectOnScript aMutationEffectOnScript,
    CharacterDataChangeInfo::Details* aDetails) {
  MOZ_ASSERT(aBuffer || !aLength, "Null buffer passed to SetTextInternal!");

  // sanitize arguments
  uint32_t textLength = mBuffer.GetLength();
  if (aOffset > textLength) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  if (aCount > textLength - aOffset) {
    aCount = textLength - aOffset;
  }

  uint32_t endOffset = aOffset + aCount;

  // Make sure the text fragment can hold the new data.
  if (aLength > aCount && !mBuffer.CanGrowBy(aLength - aCount)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  Document* document = GetComposedDoc();
  // TextNodeChangedDirection also suppresses state-change notifications when
  // aNotify is false, so only notifying changes need a script blocker.
  Maybe<mozAutoDocUpdate> updateBatch;
  if (aNotify) {
    updateBatch.emplace(document, true);
    CharacterDataChangeInfo info = {
        aOffset == textLength,   aOffset, endOffset, aLength,
        aMutationEffectOnScript, aDetails};
    MutationObservers::NotifyCharacterDataWillChange(this, info);
  }

  auto oldDir = Directionality::Unset;
  const bool dirAffectsAncestor =
      IsText() && TextNodeWillChangeDirection(AsText(), &oldDir, aOffset);

  if (aOffset == 0 && endOffset == textLength) {
    // Replacing whole text or old text was empty.
    // If this is marked as "maybe modified frequently", the text should be
    // stored as char16_t since converting char* to char16_t* is expensive.
    bool ok = mBuffer.SetTo(aBuffer, aLength, true,
                            HasFlag(NS_MAYBE_MODIFIED_FREQUENTLY));
    NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);
  } else if (aOffset == textLength) {
    // Appending to existing.
    bool ok = mBuffer.Append(aBuffer, aLength, !mBuffer.IsBidi(),
                             HasFlag(NS_MAYBE_MODIFIED_FREQUENTLY));
    NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);
  } else {
    // Merging old and new

    bool bidi = mBuffer.IsBidi();

    // Allocate new buffer
    const uint32_t newLength = textLength - aCount + aLength;
    // Use nsString and not nsAutoString so that we get a nsStringBuffer which
    // can be just AddRefed in CharacterDataBuffer.
    nsString to;
    to.SetCapacity(newLength);

    // Copy over appropriate data
    if (aOffset) {
      mBuffer.AppendTo(to, 0, aOffset);
    }
    if (aLength) {
      to.Append(aBuffer, aLength);
      if (!bidi) {
        bidi = HasRTLChars(Span(aBuffer, aLength));
      }
    }
    if (endOffset != textLength) {
      mBuffer.AppendTo(to, endOffset, textLength - endOffset);
    }

    // If this is marked as "maybe modified frequently", the text should be
    // stored as char16_t since converting char* to char16_t* is expensive.
    // Use char16_t also when we have bidi characters.
    bool use2b = HasFlag(NS_MAYBE_MODIFIED_FREQUENTLY) || bidi;
    bool ok = mBuffer.SetTo(to, false, use2b);
    mBuffer.SetBidi(bidi);

    NS_ENSURE_TRUE(ok, NS_ERROR_OUT_OF_MEMORY);
  }

  UnsetFlags(NS_CACHED_TEXT_IS_ONLY_WHITESPACE);

  if (document && mBuffer.IsBidi()) {
    // If we found bidi characters in mBuffer.SetTo() above, indicate that the
    // document contains bidi characters.
    document->SetBidiEnabled();
  }

  if (dirAffectsAncestor) {
    // dirAffectsAncestor being true implies that we have a text node, see
    // above.
    MOZ_ASSERT(IsText());
    TextNodeChangedDirection(AsText(), oldDir, aNotify);
  }

  // Notify observers
  if (aNotify) {
    CharacterDataChangeInfo info = {
        aOffset == textLength,   aOffset, endOffset, aLength,
        aMutationEffectOnScript, aDetails};
    MutationObservers::NotifyCharacterDataChanged(this, info);
  }

  return NS_OK;
}

//----------------------------------------------------------------------

// Implementation of nsIContent

#ifdef MOZ_DOM_LIST
void CharacterData::ToCString(nsAString& aBuf, int32_t aOffset,
                              int32_t aLen) const {
  if (mBuffer.Is2b()) {
    const char16_t* cp = mBuffer.Get2b() + aOffset;
    const char16_t* end = cp + aLen;

    while (cp < end) {
      char16_t ch = *cp++;
      if (ch == '&') {
        aBuf.AppendLiteral("&amp;");
      } else if (ch == '<') {
        aBuf.AppendLiteral("&lt;");
      } else if (ch == '>') {
        aBuf.AppendLiteral("&gt;");
      } else if ((ch < ' ') || (ch >= 127)) {
        aBuf.AppendPrintf("\\u%04x", ch);
      } else {
        aBuf.Append(ch);
      }
    }
  } else {
    unsigned char* cp = (unsigned char*)mBuffer.Get1b() + aOffset;
    const unsigned char* end = cp + aLen;

    while (cp < end) {
      char16_t ch = *cp++;
      if (ch == '&') {
        aBuf.AppendLiteral("&amp;");
      } else if (ch == '<') {
        aBuf.AppendLiteral("&lt;");
      } else if (ch == '>') {
        aBuf.AppendLiteral("&gt;");
      } else if ((ch < ' ') || (ch >= 127)) {
        aBuf.AppendPrintf("\\u%04x", ch);
      } else {
        aBuf.Append(ch);
      }
    }
  }
}
#endif

nsresult CharacterData::BindToTree(BindContext& aContext, nsINode& aParent) {
  MOZ_ASSERT(aParent.IsContent() || aParent.IsDocument(),
             "Must have content or document parent!");
  MOZ_ASSERT(aParent.OwnerDoc() == OwnerDoc(),
             "Must have the same owner document");
  MOZ_ASSERT(OwnerDoc() == &aContext.OwnerDoc(), "These should match too");
  MOZ_ASSERT(!IsInUncomposedDoc(), "Already have a document.  Unbind first!");
  MOZ_ASSERT(!IsInComposedDoc(), "Already have a document.  Unbind first!");
  // Note that as we recurse into the kids, they'll have a non-null parent.  So
  // only assert if our parent is _changing_ while we have a parent.
  MOZ_DIAGNOSTIC_ASSERT(!GetParentNode() || &aParent == GetParentNode(),
                        "Already have a different parent.  Unbind first!");

  const bool hadParent = !!GetParentNode();

  if (aParent.IsInNativeAnonymousSubtree()) {
    SetFlags(NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE);
  }
  if (IsRootOfNativeAnonymousSubtree()) {
    aParent.SetMayHaveAnonymousChildren();
  } else if (aParent.HasFlag(NODE_HAS_BEEN_IN_UA_WIDGET)) {
    SetFlags(NODE_HAS_BEEN_IN_UA_WIDGET);
  }

  // Set parent
  mParent = &aParent;
  if (!hadParent && aParent.IsContent()) {
    SetParentIsContent(true);
    NS_ADDREF(mParent);
  }
  MOZ_ASSERT(!!GetParent() == aParent.IsContent());

  SetSubtreeRootPointer(aParent.SubtreeRoot());
  const bool connected = aParent.IsInComposedDoc();
  SetIsConnected(connected);
  if (connected) {
    // Clear the lazy frame construction bits.
    // XXX Why here?
    UnsetFlags(NODE_NEEDS_FRAME | NODE_DESCENDANTS_NEED_FRAMES);
    if (mBuffer.IsBidi()) {
      aContext.OwnerDoc().SetBidiEnabled();
    }
  }
  if (aParent.IsInUncomposedDoc()) {
    SetIsInDocument();
  } else if (aParent.IsInShadowTree()) {
    SetFlags(NODE_IS_IN_SHADOW_TREE);
  }
  MutationObservers::NotifyParentChainChanged(this);

  UpdateEditableState(false);

  // Ensure we only do these once, in the case we move the shadow host around.
  if (aContext.SubtreeRootChanges()) {
    HandleShadowDOMRelatedInsertionSteps(hadParent);
  }

  MOZ_ASSERT(OwnerDoc() == aParent.OwnerDoc(), "Bound to wrong document");
  MOZ_ASSERT(IsInComposedDoc() == aContext.InComposedDoc());
  MOZ_ASSERT(IsInUncomposedDoc() == aContext.InUncomposedDoc());
  MOZ_ASSERT(&aParent == GetParentNode(), "Bound to wrong parent node");
  MOZ_ASSERT(aParent.IsInUncomposedDoc() == IsInUncomposedDoc());
  MOZ_ASSERT(aParent.IsInComposedDoc() == IsInComposedDoc());
  MOZ_ASSERT(aParent.IsInShadowTree() == IsInShadowTree());
  MOZ_ASSERT(aParent.SubtreeRoot() == SubtreeRoot());
  return NS_OK;
}

void CharacterData::UnbindFromTree(UnbindContext& aContext) {
  // Unset frame flags; if we need them again later, they'll get set again.
  UnsetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE | NS_REFRAME_IF_WHITESPACE);

  const bool nullParent = aContext.IsUnbindRoot(this);
  HandleShadowDOMRelatedRemovalSteps(nullParent);

  if (nullParent) {
    if (GetParent()) {
      NS_RELEASE(mParent);
    } else {
      mParent = nullptr;
    }
    SetParentIsContent(false);
  }
  ClearInDocument();
  SetIsConnected(false);

  if (nullParent || !mParent->IsInShadowTree()) {
    UnsetFlags(NODE_IS_IN_SHADOW_TREE);
  }

  SetSubtreeRootPointer(nullParent ? this : mParent->SubtreeRoot());

  MutationObservers::NotifyParentChainChanged(this);

#if defined(ACCESSIBILITY) && defined(DEBUG)
  MOZ_ASSERT(!GetAccService() || !GetAccService()->HasAccessible(this),
             "An accessible for this element still exists!");
#endif
}

//----------------------------------------------------------------------

// Implementation of the nsIContent interface text functions

nsresult CharacterData::SetText(const char16_t* aBuffer, uint32_t aLength,
                                bool aNotify) {
  return SetTextInternal(0, mBuffer.GetLength(), aBuffer, aLength, aNotify,
                         MutationEffectOnScript::KeepTrustWorthiness);
}

nsresult CharacterData::AppendText(const char16_t* aBuffer, uint32_t aLength,
                                   bool aNotify) {
  return SetTextInternal(mBuffer.GetLength(), 0, aBuffer, aLength, aNotify,
                         MutationEffectOnScript::KeepTrustWorthiness);
}

bool CharacterData::TextIsOnlyWhitespace() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!ThreadSafeTextIsOnlyWhitespace()) {
    UnsetFlags(NS_TEXT_IS_ONLY_WHITESPACE);
    SetFlags(NS_CACHED_TEXT_IS_ONLY_WHITESPACE);
    return false;
  }

  SetFlags(NS_CACHED_TEXT_IS_ONLY_WHITESPACE | NS_TEXT_IS_ONLY_WHITESPACE);
  return true;
}

bool CharacterData::ThreadSafeTextIsOnlyWhitespace() const {
  // FIXME: should this method take content language into account?
  if (mBuffer.Is2b()) {
    // The fragment contains non-8bit characters and such characters
    // are never considered whitespace.
    //
    // FIXME(emilio): This is not quite true in presence of the
    // NS_MAYBE_MODIFIED_FREQUENTLY flag... But looks like we only set that on
    // anonymous nodes, so should be fine...
    return false;
  }

  if (HasFlag(NS_CACHED_TEXT_IS_ONLY_WHITESPACE)) {
    return HasFlag(NS_TEXT_IS_ONLY_WHITESPACE);
  }

  return CheckTextIsOnlyWhitespace(0, mBuffer.GetLength());
}

bool CharacterData::TextStartsWithOnlyWhitespace(uint32_t aOffset) const {
  MOZ_ASSERT(aOffset <= mBuffer.GetLength());

  if (HasFlag(NS_CACHED_TEXT_IS_ONLY_WHITESPACE) &&
      HasFlag(NS_TEXT_IS_ONLY_WHITESPACE)) {
    return true;
  }

  return CheckTextIsOnlyWhitespace(0, aOffset);
}

bool CharacterData::TextEndsWithOnlyWhitespace(uint32_t aOffset) const {
  MOZ_ASSERT(aOffset <= mBuffer.GetLength());

  if (HasFlag(NS_CACHED_TEXT_IS_ONLY_WHITESPACE) &&
      HasFlag(NS_TEXT_IS_ONLY_WHITESPACE)) {
    return true;
  }

  return CheckTextIsOnlyWhitespace(aOffset, mBuffer.GetLength());
}

bool CharacterData::CheckTextIsOnlyWhitespace(uint32_t aStartOffset,
                                              uint32_t aEndOffset) const {
  if (mBuffer.Is2b()) {
    const char16_t* cp = mBuffer.Get2b() + aStartOffset;
    const char16_t* end = mBuffer.Get2b() + aEndOffset;

    while (cp < end) {
      char16_t ch = *cp;

      // NOTE(emilio): If you ever change the definition of "whitespace" here,
      // you need to change it too in RestyleManager::CharacterDataChanged.
      if (!dom::IsSpaceCharacter(ch)) {
        return false;
      }

      ++cp;
    }
  } else {
    const char* cp = mBuffer.Get1b() + aStartOffset;
    const char* end = mBuffer.Get1b() + aEndOffset;

    while (cp < end) {
      char ch = *cp;

      // NOTE(emilio): If you ever change the definition of "whitespace" here,
      // you need to change it too in RestyleManager::CharacterDataChanged.
      if (!dom::IsSpaceCharacter(ch)) {
        return false;
      }

      ++cp;
    }
  }
  return true;
}

already_AddRefed<nsAtom> CharacterData::GetCurrentValueAtom() {
  nsAutoString val;
  GetData(val);
  return NS_Atomize(val);
}

void CharacterData::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                           size_t* aNodeSize) const {
  nsIContent::AddSizeOfExcludingThis(aSizes, aNodeSize);
  *aNodeSize += mBuffer.SizeOfExcludingThis(aSizes.mState.mMallocSizeOf);
}

}  // namespace mozilla::dom
