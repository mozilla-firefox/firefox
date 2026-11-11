/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5StringParser.h"
#include "nsHtml5DependentUTF16Buffer.h"
#include "nsHtml5Tokenizer.h"
#include "nsHtml5TreeBuilder.h"
#include "nsHtml5TreeOpExecutor.h"
#include "nsIContent.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragment.h"

using namespace mozilla;
using mozilla::dom::Document;

NS_IMPL_ISUPPORTS0(nsHtml5StringParser)

nsHtml5StringParser::nsHtml5StringParser()
    : mBuilder(new nsHtml5OplessBuilder()),
      mTreeBuilder(new nsHtml5TreeBuilder(mBuilder)),
      mTokenizer(new nsHtml5Tokenizer(mTreeBuilder.get(), false)) {
  mTokenizer->setInterner(&mAtomTable);
  mTokenizer->setKeepBuffer(true);
  mTreeBuilder->setKeepBuffer(true);
}

nsHtml5StringParser::~nsHtml5StringParser() { ClearCaches(); }

/* https://html.spec.whatwg.org/#html-fragment-parsing-algorithm */
nsresult nsHtml5StringParser::ParseFragment(
    const nsAString& aSourceBuffer, nsIContent* aTargetNode,
    nsAtom* aContextLocalName, int32_t aContextNamespace, bool aQuirks,
    bool aPreventScriptExecution, bool aAllowDeclarativeShadowRoots,
    mozilla::Maybe<RefPtr<mozilla::dom::CustomElementRegistry>>
        aCustomElementRegistry,
    mozilla::dom::Sanitizer* aSanitizer,
    nsContentUtils::SanitizerMode aSanitizerMode) {
  NS_ENSURE_TRUE(aSourceBuffer.Length() <= INT32_MAX, NS_ERROR_OUT_OF_MEMORY);

  mTokenizer->setProcessingInstructionsEnabled(
      mozilla::StaticPrefs::dom_template_for_enabled());

  Document* doc = aTargetNode->OwnerDoc();
  nsIURI* uri = doc->GetDocumentURI();
  NS_ENSURE_TRUE(uri, NS_ERROR_NOT_AVAILABLE);

  // Steps 1-3. Set up the document mode, and step 5, set the tokenizer state,
  // based on the context element.
  mTreeBuilder->setFragmentContext(aContextLocalName, aContextNamespace,
                                   aTargetNode, aQuirks);
  // Step 4 (create a new HTML parser, whose "parser sanitizer configuration" is
  // aSanitizer and whose "remove javascript navigation URLs" is true iff
  // aSanitizerMode is Safe) is this object itself.
  mTreeBuilder->SetSanitizer(
      aSanitizer, aSanitizerMode == nsContentUtils::SanitizerMode::Safe);

  // Step 12 of the spec asks to look up the registry from aTargetNode, but this
  // is often a DocumentFragment which has no registry. So instead it's passed
  // directly as an arg.
  mTreeBuilder->SetCustomElementRegistry(std::move(aCustomElementRegistry));

#ifdef DEBUG
  if (!aPreventScriptExecution) {
    NS_ASSERTION(!aTargetNode->IsInUncomposedDoc(),
                 "If script execution isn't prevented, "
                 "the target node must not be in doc.");
    NS_ASSERTION(
        aTargetNode->NodeType() == nsINode::DOCUMENT_FRAGMENT_NODE,
        "If script execution isn't prevented, must parse to DOM fragment.");
  }
#endif

  mTreeBuilder->SetPreventScriptExecution(aPreventScriptExecution);

  // Steps 10-12. "Place the input into the input stream... Start the HTML
  // parser and let it run until it has consumed all the characters just
  // inserted into the input stream."
  // Step 13. "Return root's children, in tree order." (Nodes are appended
  // directly into aTargetNode by the tree builder.)
  nsresult rv =
      Tokenize(aSourceBuffer, doc, true, aAllowDeclarativeShadowRoots);

  // This parser is a process-wide singleton, so drop the strong reference to
  // the context's custom element registry now that parsing is done; otherwise
  // it (and the document it belongs to) would stay pinned until the next parse.
  mTreeBuilder->SetCustomElementRegistry(mozilla::Nothing());
  return rv;
}

nsresult nsHtml5StringParser::ParseDocument(
    const nsAString& aSourceBuffer, Document* aTargetDoc,
    bool aScriptingEnabledForNoscriptParsing,
    mozilla::dom::Sanitizer* aSanitizer,
    nsContentUtils::SanitizerMode aSanitizerMode) {
  MOZ_ASSERT(!aTargetDoc->GetFirstChild());

  NS_ENSURE_TRUE(aSourceBuffer.Length() <= INT32_MAX, NS_ERROR_OUT_OF_MEMORY);

  mTokenizer->setProcessingInstructionsEnabled(
      mozilla::StaticPrefs::dom_template_for_enabled());

  mTreeBuilder->setFragmentContext(nullptr, kNameSpaceID_None, nullptr, false);
  mTreeBuilder->SetCustomElementRegistry(mozilla::Nothing());

  mTreeBuilder->SetSanitizer(
      aSanitizer, aSanitizerMode == nsContentUtils::SanitizerMode::Safe);
  mTreeBuilder->SetPreventScriptExecution(true);

  return Tokenize(aSourceBuffer, aTargetDoc,
                  aScriptingEnabledForNoscriptParsing,
                  aTargetDoc->AllowsDeclarativeShadowRoots());
}

void nsHtml5StringParser::ClearCaches() {
  mTokenizer->dropBufferIfLongerThan(0);
  mTreeBuilder->dropBufferIfLongerThan(0);
  if (mCacheClearer) {
    mCacheClearer->Disconnect();
    mCacheClearer = nullptr;
  }
}

void nsHtml5StringParser::TryCache() {
  const int32_t kMaxBuffer = 1024 * 1024;
  bool didDrop = mTokenizer->dropBufferIfLongerThan(kMaxBuffer);
  didDrop |= mTreeBuilder->dropBufferIfLongerThan(kMaxBuffer);
  if (didDrop) {
    return;
  }

  if (!mCacheClearer) {
    mCacheClearer = new CacheClearer(this);
    nsCOMPtr<nsIRunnable> runnable = mCacheClearer.get();
    NS_DispatchToMainThreadQueue(runnable.forget(),
                                 mozilla::EventQueuePriority::Idle);
  }
}

nsresult nsHtml5StringParser::Tokenize(const nsAString& aSourceBuffer,
                                       Document* aDocument,
                                       bool aScriptingEnabledForNoscriptParsing,
                                       bool aDeclarativeShadowRootsAllowed) {
  nsIURI* uri = aDocument->GetDocumentURI();

  mBuilder->Init(aDocument, uri, nullptr, nullptr);

  mBuilder->SetParser(this);
  mBuilder->SetNodeInfoManager(aDocument->NodeInfoManager());

  // Mark the parser as *not* broken by passing NS_OK
  nsresult rv = mBuilder->MarkAsBroken(NS_OK);

  mTreeBuilder->setScriptingEnabled(aScriptingEnabledForNoscriptParsing);
  mTreeBuilder->setIsSrcdocDocument(aDocument->IsSrcdocDocument());
  mTreeBuilder->setAllowDeclarativeShadowRoots(aDeclarativeShadowRootsAllowed);
  mBuilder->Start();
  mTokenizer->start();
  if (!aSourceBuffer.IsEmpty()) {
    bool lastWasCR = false;
    nsHtml5DependentUTF16Buffer buffer(aSourceBuffer);
    while (buffer.hasMore()) {
      buffer.adjust(lastWasCR);
      lastWasCR = false;
      if (buffer.hasMore()) {
        if (!mTokenizer->EnsureBufferSpace(buffer.getLength())) {
          rv = mBuilder->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
          break;
        }
        lastWasCR = mTokenizer->tokenizeBuffer(&buffer);
        if (NS_FAILED(rv = mBuilder->IsBroken())) {
          break;
        }
      }
    }
  }
  if (NS_SUCCEEDED(rv)) {
    mTokenizer->eof();
  }

  mTokenizer->end();
  mBuilder->Finish();
  MOZ_ASSERT(!mTreeBuilder->HasSanitizer(),
             "Sanitizer should have cleared at mTokenizer-end()");
  mAtomTable.Clear();
  TryCache();
  return rv;
}
