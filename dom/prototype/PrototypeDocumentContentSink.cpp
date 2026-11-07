/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/PrototypeDocumentContentSink.h"

#include "js/CompilationAndEvaluation.h"
#include "js/Utility.h"  // JS::FreePolicy
#include "js/experimental/JSStencil.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/Try.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/CDATASection.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ProcessingInstruction.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/XMLStylesheetProcessingInstruction.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsHtml5SVGLoadDispatcher.h"
#include "nsIChannel.h"
#include "nsIContent.h"
#include "nsIContentPolicy.h"
#include "nsIParser.h"
#include "nsIScriptContext.h"
#include "nsIScriptElement.h"
#include "nsIScriptError.h"
#include "nsIScriptGlobalObject.h"
#include "nsIURI.h"
#include "nsMimeTypes.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsNodeInfoManager.h"
#include "nsReadableUtils.h"
#include "nsRect.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"
#include "nsXULElement.h"
#include "nsXULPrototypeCache.h"
#include "prtime.h"

using namespace mozilla;
using namespace mozilla::dom;

LazyLogModule PrototypeDocumentContentSink::gLog("PrototypeDocument");

nsresult NS_NewPrototypeDocumentContentSink(nsIContentSink** aResult,
                                            Document* aDoc, nsIURI* aURI,
                                            nsISupports* aContainer,
                                            nsIChannel* aChannel) {
  MOZ_ASSERT(nullptr != aResult, "null ptr");
  if (nullptr == aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  RefPtr<PrototypeDocumentContentSink> it = new PrototypeDocumentContentSink();

  nsresult rv = it->Init(aDoc, aURI, aContainer, aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  it.forget(aResult);
  return NS_OK;
}

namespace mozilla::dom {

PrototypeDocumentContentSink::PrototypeDocumentContentSink()
    : mNextSrcLoadWaiter(nullptr),
      mCurrentScriptProto(nullptr),
      mOffThreadCompiling(false),
      mStillWalking(false),
      mPendingSheets(0) {}

PrototypeDocumentContentSink::~PrototypeDocumentContentSink() {
  NS_ASSERTION(
      mNextSrcLoadWaiter == nullptr,
      "unreferenced document still waiting for script source to load?");
}

nsresult PrototypeDocumentContentSink::Init(Document* aDoc, nsIURI* aURI,
                                            nsISupports* aContainer,
                                            nsIChannel* aChannel) {
  MOZ_ASSERT(aDoc, "null ptr");
  MOZ_ASSERT(aURI, "null ptr");

  mDocument = aDoc;

  mDocument->SetDelayFrameLoaderInitialization(true);
  mDocument->SetMayStartLayout(false);

  // Get the URI.  this should match the uri used for the OnNewURI call in
  // nsDocShell::CreateDocumentViewer.
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(mDocumentURI));
  NS_ENSURE_SUCCESS(rv, rv);

  mScriptLoader = mDocument->GetScriptLoader();

  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION(PrototypeDocumentContentSink, mParser, mDocumentURI,
                         mDocument, mScriptLoader, mContextStack,
                         mCurrentPrototype)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PrototypeDocumentContentSink)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIContentSink)
  NS_INTERFACE_MAP_ENTRY(nsIContentSink)
  NS_INTERFACE_MAP_ENTRY(nsIStreamLoaderObserver)
  NS_INTERFACE_MAP_ENTRY(nsICSSLoaderObserver)
  NS_INTERFACE_MAP_ENTRY(nsIOffThreadScriptReceiver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(PrototypeDocumentContentSink)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PrototypeDocumentContentSink)

//----------------------------------------------------------------------
//
// nsIContentSink interface
//

void PrototypeDocumentContentSink::SetDocumentCharset(
    NotNull<const Encoding*> aEncoding) {
  if (mDocument) {
    mDocument->SetDocumentCharacterSet(aEncoding);
  }
}

nsISupports* PrototypeDocumentContentSink::GetTarget() {
  return ToSupports(mDocument);
}

bool PrototypeDocumentContentSink::IsScriptExecuting() {
  if (!mScriptLoader) {
    MOZ_ASSERT(false, "Can't load prototype docs as data");
    return false;
  }
  return !!mScriptLoader->GetCurrentScript();
}

NS_IMETHODIMP
PrototypeDocumentContentSink::SetParser(nsParserBase* aParser) {
  MOZ_ASSERT(aParser, "Should have a parser here!");
  mParser = aParser;
  return NS_OK;
}

nsIParser* PrototypeDocumentContentSink::GetParser() {
  return static_cast<nsIParser*>(mParser.get());
}

void PrototypeDocumentContentSink::ContinueInterruptedParsingIfEnabled() {
  if (mParser && mParser->IsParserEnabled()) {
    GetParser()->ContinueInterruptedParsing();
  }
}

void PrototypeDocumentContentSink::ContinueInterruptedParsingAsync() {
  nsCOMPtr<nsIRunnable> ev = NewRunnableMethod(
      "PrototypeDocumentContentSink::ContinueInterruptedParsingIfEnabled", this,
      &PrototypeDocumentContentSink::ContinueInterruptedParsingIfEnabled);
  mDocument->Dispatch(ev.forget());
}

//----------------------------------------------------------------------
//
// PrototypeDocumentContentSink::ContextStack
//

PrototypeDocumentContentSink::ContextStack::ContextStack()
    : mTop(nullptr), mDepth(0) {}

PrototypeDocumentContentSink::ContextStack::~ContextStack() { Clear(); }

void PrototypeDocumentContentSink::ContextStack::Traverse(
    nsCycleCollectionTraversalCallback& aCallback, const char* aName,
    uint32_t aFlags) {
  aFlags |= CycleCollectionEdgeNameArrayFlag;
  Entry* current = mTop;
  while (current) {
    CycleCollectionNoteChild(aCallback, current->mElement, aName, aFlags);
    current = current->mNext;
  }
}

void PrototypeDocumentContentSink::ContextStack::Clear() {
  while (mTop) {
    Entry* doomed = mTop;
    mTop = mTop->mNext;
    NS_IF_RELEASE(doomed->mElement);
    delete doomed;
  }
  mDepth = 0;
}

nsresult PrototypeDocumentContentSink::ContextStack::Push(
    nsXULPrototypeElement* aPrototype, nsIContent* aElement) {
  Entry* entry = new Entry;
  entry->mPrototype = aPrototype;
  entry->mElement = aElement;
  NS_IF_ADDREF(entry->mElement);
  entry->mIndex = 0;

  entry->mNext = mTop;
  mTop = entry;

  ++mDepth;
  return NS_OK;
}

nsresult PrototypeDocumentContentSink::ContextStack::Pop() {
  if (mDepth == 0) return NS_ERROR_UNEXPECTED;

  Entry* doomed = mTop;
  mTop = mTop->mNext;
  --mDepth;

  NS_IF_RELEASE(doomed->mElement);
  delete doomed;
  return NS_OK;
}

nsresult PrototypeDocumentContentSink::ContextStack::Peek(
    nsXULPrototypeElement** aPrototype, nsIContent** aElement,
    int32_t* aIndex) {
  if (mDepth == 0) return NS_ERROR_UNEXPECTED;

  *aPrototype = mTop->mPrototype;
  *aElement = mTop->mElement;
  NS_IF_ADDREF(*aElement);
  *aIndex = mTop->mIndex;

  return NS_OK;
}

nsresult PrototypeDocumentContentSink::ContextStack::SetTopIndex(
    int32_t aIndex) {
  if (mDepth == 0) return NS_ERROR_UNEXPECTED;

  mTop->mIndex = aIndex;
  return NS_OK;
}

//----------------------------------------------------------------------
//
// Content model walking routines
//

nsresult PrototypeDocumentContentSink::OnPrototypeLoadDone(
    nsXULPrototypeDocument* aPrototype) {
  mCurrentPrototype = aPrototype;
  mDocument->SetPrototypeDocument(aPrototype);

  nsresult rv = PrepareToWalk();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ResumeWalk();

  return rv;
}

nsresult PrototypeDocumentContentSink::PrepareToWalk() {
  MOZ_ASSERT(mCurrentPrototype);
  nsresult rv;

  mStillWalking = true;

  // Notify document that the load is beginning
  mDocument->BeginLoad();
  MOZ_ASSERT(!mDocument->HasChildren());

  // Get the prototype's root element and initialize the context
  // stack for the prototype walk.
  nsXULPrototypeElement* proto = mCurrentPrototype->GetRootElement();

  if (!proto) {
    if (MOZ_LOG_TEST(gLog, LogLevel::Error)) {
      nsCOMPtr<nsIURI> url = mCurrentPrototype->GetURI();

      nsAutoCString urlspec;
      rv = url->GetSpec(urlspec);
      if (NS_FAILED(rv)) return rv;

      MOZ_LOG(gLog, LogLevel::Error,
              ("prototype: error parsing '%s'", urlspec.get()));
    }

    return NS_OK;
  }

  const nsTArray<RefPtr<nsXULPrototypePI> >& processingInstructions =
      mCurrentPrototype->GetProcessingInstructions();

  uint32_t total = processingInstructions.Length();
  for (uint32_t i = 0; i < total; ++i) {
    rv = CreateAndInsertPI(processingInstructions[i], mDocument,
                           /* aInProlog */ true);
    if (NS_FAILED(rv)) return rv;
  }

  // Do one-time initialization.
  RefPtr<Element> root;

  // Add the root element
  rv = CreateElementFromPrototype(proto, getter_AddRefs(root), nullptr);
  if (NS_FAILED(rv)) return rv;

  ErrorResult error;
  mDocument->AppendChildTo(root, false, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  // TODO(emilio): Should this really notify? We don't notify of appends anyhow,
  // and we just appended the root so no styles can possibly depend on it.
  mDocument->UpdateDocumentStates(DocumentState::RTL_LOCALE, true);

  nsContentUtils::AddScriptRunner(
      MakeAndAddRef<nsDocElementCreatedNotificationRunner>(mDocument));

  // There'd better not be anything on the context stack at this
  // point! This is the basis case for our "induction" in
  // ResumeWalk(), below, which'll assume that there's always a
  // content element on the context stack if we're in the document.
  NS_ASSERTION(mContextStack.Depth() == 0,
               "something's on the context stack already");
  if (mContextStack.Depth() != 0) return NS_ERROR_UNEXPECTED;

  rv = mContextStack.Push(proto, root);
  if (NS_FAILED(rv)) return rv;

  return NS_OK;
}

nsresult PrototypeDocumentContentSink::CreateAndInsertPI(
    const nsXULPrototypePI* aProtoPI, nsINode* aParent, bool aInProlog) {
  MOZ_ASSERT(aProtoPI, "null ptr");
  MOZ_ASSERT(aParent, "null ptr");

  RefPtr<ProcessingInstruction> node = NS_NewXMLProcessingInstruction(
      aParent->NodeInfoManager(), aProtoPI->mTarget, aProtoPI->mData);

  nsresult rv;
  if (aProtoPI->mTarget.EqualsLiteral("xml-stylesheet")) {
    MOZ_ASSERT(LinkStyle::FromNode(*node),
               "XML Stylesheet node does not implement LinkStyle!");
    auto* pi = static_cast<XMLStylesheetProcessingInstruction*>(node.get());
    rv = InsertXMLStylesheetPI(aProtoPI, aParent, pi);
  } else {
    // Handles the special <?csp ?> PI, which will be handled before
    // creating any element with potential inline style or scripts.
    if (aInProlog && aProtoPI->mTarget.EqualsLiteral("csp")) {
      CSP_ApplyMetaCSPToDoc(*aParent->OwnerDoc(), aProtoPI->mData);
    }

    // No special processing, just add the PI to the document.
    ErrorResult error;
    aParent->AppendChildTo(node->AsContent(), false, error);
    rv = error.StealNSResult();
  }

  return rv;
}

nsresult PrototypeDocumentContentSink::InsertXMLStylesheetPI(
    const nsXULPrototypePI* aProtoPI, nsINode* aParent,
    XMLStylesheetProcessingInstruction* aPINode) {
  // We want to be notified when the style sheet finishes loading, so
  // disable style sheet loading for now.
  aPINode->DisableUpdates();
  aPINode->OverrideBaseURI(mCurrentPrototype->GetURI());

  ErrorResult rv;
  aParent->AppendChildTo(aPINode, false, rv);
  if (rv.Failed()) {
    return rv.StealNSResult();
  }

  // load the stylesheet if necessary, passing ourselves as
  // nsICSSObserver
  auto result = aPINode->EnableUpdatesAndUpdateStyleSheet(this);
  if (result.isErr()) {
    // Ignore errors from UpdateStyleSheet; we don't want failure to
    // do that to break the XUL document load.  But do propagate out
    // NS_ERROR_OUT_OF_MEMORY.
    if (result.unwrapErr() == NS_ERROR_OUT_OF_MEMORY) {
      return result.unwrapErr();
    }
    return NS_OK;
  }

  auto update = result.unwrap();
  if (update.ShouldBlock()) {
    ++mPendingSheets;
  }

  return NS_OK;
}

void PrototypeDocumentContentSink::CloseElement(Element* aElement) {
  if (nsIContent::RequiresDoneAddingChildren(
          aElement->NodeInfo()->NamespaceID(),
          aElement->NodeInfo()->NameAtom())) {
    nsAutoScriptBlocker scriptBlocker;
    aElement->DoneAddingChildren(false);
  }

  if (auto* linkStyle = LinkStyle::FromNode(*aElement)) {
    auto result = linkStyle->EnableUpdatesAndUpdateStyleSheet(this);
    if (result.isOk() && result.unwrap().ShouldBlock()) {
      ++mPendingSheets;
    }
    return;
  }

  // See bug 370111 and bug 1495946. We don't cache module scripts in the
  // prototype cache, so we need to do this for the script to be properly
  // processed.
  if (aElement->IsHTMLElement(nsGkAtoms::script) ||
      aElement->IsSVGElement(nsGkAtoms::script)) {
    nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(aElement);
    MOZ_ASSERT(sele, "Node didn't QI to script.");
    if (sele->GetScriptIsModule()) {
      // https://html.spec.whatwg.org/#parsing-main-incdata
      // An end tag whose tag name is "script"
      //  - If the active speculative HTML parser is null and the JavaScript
      // execution context stack is empty, then perform a microtask checkpoint.
      {
        nsAutoMicroTask mt;
      }
      sele->AttemptToExecute(nullptr /* aParser */);
    }
  }
}

nsresult PrototypeDocumentContentSink::ResumeWalk() {
  nsresult rv = ResumeWalkInternal();
  if (NS_FAILED(rv)) {
    nsContentUtils::ReportToConsoleNonLocalized(
        u"Failed to load document from prototype document."_ns,
        nsIScriptError::errorFlag, "Prototype Document"_ns, mDocument,
        SourceLocation{mDocumentURI.get()});
  }
  return rv;
}

nsresult PrototypeDocumentContentSink::ResumeWalkInternal() {
  MOZ_ASSERT(mStillWalking);
  // Walk the prototype and build the delegate content model. The
  // walk is performed in a top-down, left-to-right fashion. That
  // is, a parent is built before any of its children; a node is
  // only built after all of its siblings to the left are fully
  // constructed.
  //
  // It is interruptable so that transcluded documents (e.g.,
  // <html:script src="..." />) can be properly re-loaded if the
  // cached copy of the document becomes stale.
  nsresult rv;
  nsCOMPtr<nsIURI> docURI =
      mCurrentPrototype ? mCurrentPrototype->GetURI() : nullptr;

  while (true) {
    // Begin (or resume) walking the current prototype.

    while (mContextStack.Depth() > 0) {
      // Look at the top of the stack to determine what we're
      // currently working on.
      // This will always be a node already constructed and
      // inserted to the actual document.
      nsXULPrototypeElement* proto;
      nsCOMPtr<nsIContent> element;
      nsCOMPtr<nsIContent> nodeToPushTo;
      int32_t indx;  // all children of proto before indx (not
                     // inclusive) have already been constructed
      rv = mContextStack.Peek(&proto, getter_AddRefs(element), &indx);
      if (NS_FAILED(rv)) return rv;

      if (indx >= (int32_t)proto->mChildren.Length()) {
        if (element) {
          // We've processed all of the prototype's children.
          CloseElement(element->AsElement());
        }
        // Now pop the context stack back up to the parent
        // element and continue the prototype walk.
        mContextStack.Pop();
        continue;
      }

      nodeToPushTo = element;
      // For template elements append the content to the template's document
      // fragment.
      if (auto* templateElement = HTMLTemplateElement::FromNode(element)) {
        nodeToPushTo = templateElement->Content();
      }

      // Grab the next child, and advance the current context stack
      // to the next sibling to our right.
      nsXULPrototypeNode* childproto = proto->mChildren[indx];
      mContextStack.SetTopIndex(++indx);

      switch (childproto->mType) {
        case nsXULPrototypeNode::eType_Element: {
          // An 'element', which may contain more content.
          auto* protoele = static_cast<nsXULPrototypeElement*>(childproto);

          RefPtr<Element> child;
          MOZ_TRY(CreateElementFromPrototype(protoele, getter_AddRefs(child),
                                             nodeToPushTo));

          if (auto* linkStyle = LinkStyle::FromNode(*child)) {
            linkStyle->DisableUpdates();
          }

          // ...and append it to the content model.
          ErrorResult error;
          nodeToPushTo->AppendChildTo(child, false, error);
          if (error.Failed()) {
            return error.StealNSResult();
          }

          if (nsIContent::RequiresDoneCreatingElement(
                  protoele->mNodeInfo->NamespaceID(),
                  protoele->mNodeInfo->NameAtom())) {
            child->DoneCreatingElement();
          }

          // If it has children, push the element onto the context
          // stack and begin to process them.
          if (protoele->mChildren.Length() > 0) {
            rv = mContextStack.Push(protoele, child);
            if (NS_FAILED(rv)) return rv;
          } else {
            // If there are no children, close the element immediately.
            CloseElement(child);
          }
        } break;

        case nsXULPrototypeNode::eType_Script: {
          // A script reference. Execute the script immediately;
          // this may have side effects in the content model.
          auto* scriptproto = static_cast<nsXULPrototypeScript*>(childproto);
          if (scriptproto->mSrcURI) {
            // A transcluded script reference; this may
            // "block" our prototype walk if the script isn't
            // cached, or the cached copy of the script is
            // stale and must be reloaded.
            bool blocked;
            rv = LoadScript(scriptproto, &blocked);
            // If the script cannot be loaded, just keep going!

            if (NS_SUCCEEDED(rv) && blocked) return NS_OK;
          } else if (scriptproto->HasStencil()) {
            // An inline script
            rv = ExecuteScript(scriptproto);
            if (NS_FAILED(rv)) return rv;
          }
        } break;

        case nsXULPrototypeNode::eType_Text: {
          nsNodeInfoManager* nim = nodeToPushTo->NodeInfo()->NodeInfoManager();
          // A simple text node.
          RefPtr<nsTextNode> text = new (nim) nsTextNode(nim);

          auto* textproto = static_cast<nsXULPrototypeText*>(childproto);
          text->SetText(textproto->mValue, false);

          ErrorResult error;
          nodeToPushTo->AppendChildTo(text, false, error);
          if (error.Failed()) {
            return error.StealNSResult();
          }
        } break;

        case nsXULPrototypeNode::eType_PI: {
          auto* piProto = static_cast<nsXULPrototypePI*>(childproto);

          // <?xml-stylesheet?> and <?csp?> don't have an effect
          // outside the prolog, issue a warning.

          if (piProto->mTarget.EqualsLiteral("xml-stylesheet") ||
              piProto->mTarget.EqualsLiteral("csp")) {
            AutoTArray<nsString, 1> params = {piProto->mTarget};

            nsContentUtils::ReportToConsole(
                nsIScriptError::warningFlag, "XUL Document"_ns, nullptr,
                PropertiesFile::XUL_PROPERTIES, "PINotInProlog2", params,
                SourceLocation(docURI.get()));
          }

          if (nsIContent* parent = element.get()) {
            // an inline script could have removed the root element
            rv = CreateAndInsertPI(piProto, parent, /* aInProlog */ false);
            NS_ENSURE_SUCCESS(rv, rv);
          }
        } break;

        default:
          MOZ_ASSERT_UNREACHABLE("Unexpected nsXULPrototypeNode::Type");
      }
    }

    // Once we get here, the context stack will have been
    // depleted. That means that the entire prototype has been
    // walked and content has been constructed.
    break;
  }

  mStillWalking = false;
  return MaybeDoneWalking();
}

void PrototypeDocumentContentSink::InitialTranslationCompleted() {
  MaybeDoneWalking();
}

nsresult PrototypeDocumentContentSink::MaybeDoneWalking() {
  if (mPendingSheets > 0 || mStillWalking) {
    return NS_OK;
  }

  if (mDocument->HasPendingInitialTranslation()) {
    mDocument->OnParsingCompleted();
    return NS_OK;
  }

  return DoneWalking();
}

nsresult PrototypeDocumentContentSink::DoneWalking() {
  MOZ_ASSERT(mPendingSheets == 0, "there are sheets to be loaded");
  MOZ_ASSERT(!mStillWalking, "walk not done");
  MOZ_ASSERT(!mDocument->HasPendingInitialTranslation(), "translation pending");

  if (const RefPtr<Document> doc = mDocument) {
    MOZ_ASSERT(doc->GetReadyStateEnum() == Document::READYSTATE_LOADING,
               "Bad readyState");
    doc->SetReadyStateInternal(Document::READYSTATE_INTERACTIVE);
    doc->NotifyPossibleTitleChange(false);

    nsContentUtils::DispatchEventOnlyToChrome(doc, doc,
                                              u"MozBeforeInitialXULLayout"_ns,
                                              CanBubble::eYes, Cancelable::eNo);
  }

  if (const RefPtr<ScriptLoader> scriptLoader = mScriptLoader) {
    scriptLoader->ParsingComplete(false);
    scriptLoader->DeferCheckpointReached();
  }

  StartLayout();

  if (mDocumentURI->SchemeIs("chrome") &&
      nsXULPrototypeCache::GetInstance()->IsEnabled()) {
    bool isCachedOnDisk;
    nsXULPrototypeCache::GetInstance()->HasPrototype(mDocumentURI,
                                                     &isCachedOnDisk);
    if (!isCachedOnDisk) {
      if (!mDocument->GetDocumentElement() ||
          (mDocument->GetDocumentElement()->NodeInfo()->Equals(
               nsGkAtoms::parsererror) &&
           mDocument->GetDocumentElement()->NodeInfo()->NamespaceEquals(
               nsDependentAtomString(nsGkAtoms::nsuri_parsererror)))) {
        nsXULPrototypeCache::GetInstance()->RemovePrototype(mDocumentURI);
      } else {
        nsXULPrototypeCache::GetInstance()->WritePrototype(mCurrentPrototype);
      }
    }
  }

  mDocument->SetDelayFrameLoaderInitialization(false);
  RefPtr<Document> doc = mDocument;
  doc->MaybeInitializeFinalizeFrameLoaders();

  // If the document we are loading has a reference or it is a
  // frameset document, disable the scroll bars on the views.

  doc->SetScrollToRef(mDocument->GetDocumentURI());

  doc->EndLoad(/* aFireDOMContentLoadedSync = */ true);

  return NS_OK;
}

void PrototypeDocumentContentSink::StartLayout() {
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING(
      "PrototypeDocumentContentSink::StartLayout", LAYOUT,
      mDocumentURI->GetSpecOrDefault());
  mDocument->SetMayStartLayout(true);
  RefPtr<PresShell> presShell = mDocument->GetPresShell();
  if (presShell && !presShell->DidInitialize()) {
    nsresult rv = presShell->Initialize();
    if (NS_FAILED(rv)) {
      return;
    }
  }
}

NS_IMETHODIMP
PrototypeDocumentContentSink::StyleSheetLoaded(StyleSheet* aSheet,
                                               bool aWasDeferred,
                                               nsresult aStatus) {
  if (!aWasDeferred) {
    // Don't care about when alternate sheets finish loading
    MOZ_ASSERT(mPendingSheets > 0, "Unexpected StyleSheetLoaded notification");

    --mPendingSheets;

    return MaybeDoneWalking();
  }

  return NS_OK;
}

nsresult PrototypeDocumentContentSink::LoadScript(
    nsXULPrototypeScript* aScriptProto, bool* aBlock) {
  // Load a transcluded script
  nsresult rv;

  bool isChromeDoc = mDocumentURI->SchemeIs("chrome");

  if (isChromeDoc && aScriptProto->HasStencil()) {
    rv = ExecuteScript(aScriptProto);

    // Ignore return value from execution, and don't block
    *aBlock = false;
    return NS_OK;
  }

  // Try the XUL script cache, in case two XUL documents source the same
  // .js file (e.g., strres.js from navigator.xul and utilityOverlay.xul).
  // XXXbe the cache relies on aScriptProto's GC root!
  bool useXULCache = nsXULPrototypeCache::GetInstance()->IsEnabled();

  if (isChromeDoc && useXULCache) {
    RefPtr<JS::Stencil> newStencil =
        nsXULPrototypeCache::GetInstance()->GetStencil(aScriptProto->mSrcURI);
    if (newStencil) {
      // The script language for a proto must remain constant - we
      // can't just change it for this unexpected language.
      aScriptProto->Set(newStencil);
    }

    if (aScriptProto->HasStencil()) {
      rv = ExecuteScript(aScriptProto);

      // Ignore return value from execution, and don't block
      *aBlock = false;
      return NS_OK;
    }
  }

  // Release stencil from FastLoad since we decided against using them
  aScriptProto->Set(nullptr);

  // Set the current script prototype so that OnStreamComplete can report
  // the right file if there are errors in the script.
  NS_ASSERTION(!mCurrentScriptProto,
               "still loading a script when starting another load?");
  mCurrentScriptProto = aScriptProto;

  if (isChromeDoc && aScriptProto->mSrcLoading) {
    // Another document load has started, which is still in progress.
    // Remember to ResumeWalk this document when the load completes.
    mNextSrcLoadWaiter = aScriptProto->mSrcLoadWaiters;
    aScriptProto->mSrcLoadWaiters = this;
    NS_ADDREF_THIS();
  } else {
    nsCOMPtr<nsILoadGroup> group =
        mDocument
            ->GetDocumentLoadGroup();  // found in
                                       // mozilla::dom::Document::SetScriptGlobalObject

    // Note: the loader will keep itself alive while it's loading.
    nsCOMPtr<nsIStreamLoader> loader;
    rv = NS_NewStreamLoader(
        getter_AddRefs(loader), aScriptProto->mSrcURI,
        this,       // aObserver
        mDocument,  // aRequestingContext
        nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
        nsIContentPolicy::TYPE_INTERNAL_SCRIPT, group);

    if (NS_FAILED(rv)) {
      mCurrentScriptProto = nullptr;
      return rv;
    }

    aScriptProto->mSrcLoading = true;
  }

  // Block until OnStreamComplete resumes us.
  *aBlock = true;
  return NS_OK;
}

NS_IMETHODIMP
PrototypeDocumentContentSink::OnStreamComplete(nsIStreamLoader* aLoader,
                                               nsISupports* context,
                                               nsresult aStatus,
                                               uint32_t stringLen,
                                               const uint8_t* string) {
  nsCOMPtr<nsIRequest> request;
  aLoader->GetRequest(getter_AddRefs(request));
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);

#ifdef DEBUG
  // print a load error on bad status
  if (NS_FAILED(aStatus)) {
    if (channel) {
      nsCOMPtr<nsIURI> uri;
      channel->GetURI(getter_AddRefs(uri));
      if (uri) {
        printf("Failed to load %s\n", uri->GetSpecOrDefault().get());
      }
    }
  }
#endif

  // This is the completion routine that will be called when a
  // transcluded script completes. Compile and execute the script
  // if the load was successful, then continue building content
  // from the prototype.
  nsresult rv = aStatus;

  NS_ASSERTION(mCurrentScriptProto && mCurrentScriptProto->mSrcLoading,
               "script source not loading on unichar stream complete?");
  if (!mCurrentScriptProto) {
    // XXX Wallpaper for bug 270042
    return NS_OK;
  }

  if (NS_SUCCEEDED(aStatus)) {
    // If the including document is a FastLoad document, and we're
    // compiling an out-of-line script (one with src=...), then we must
    // be writing a new FastLoad file.  If we were reading this script
    // from the FastLoad file, XULContentSinkImpl::OpenScript (over in
    // nsXULContentSink.cpp) would have already deserialized a non-null
    // script->mStencil, causing control flow at the top of LoadScript
    // not to reach here.
    nsCOMPtr<nsIURI> uri = mCurrentScriptProto->mSrcURI;

    // XXX should also check nsIHttpChannel::requestSucceeded

    MOZ_ASSERT(!mOffThreadCompiling,
               "PrototypeDocument can't load multiple scripts at once");

    UniquePtr<Utf8Unit[], JS::FreePolicy> units;
    size_t unitsLength = 0;

    rv = ScriptLoader::ConvertToUTF8(channel, string, stringLen, u""_ns,
                                     mDocument, units, unitsLength);
    if (NS_SUCCEEDED(rv)) {
      rv = mCurrentScriptProto->CompileMaybeOffThread(
          std::move(units), unitsLength, uri, 1, mDocument, this);
      if (NS_SUCCEEDED(rv) && !mCurrentScriptProto->HasStencil()) {
        mOffThreadCompiling = true;
        mDocument->BlockOnload();
        return NS_OK;
      }
    }
  }

  return OnScriptCompileComplete(mCurrentScriptProto->GetStencil(), rv);
}

NS_IMETHODIMP
PrototypeDocumentContentSink::OnScriptCompileComplete(JS::Stencil* aStencil,
                                                      nsresult aStatus) {
  // The mCurrentScriptProto may have been cleared out by another
  // PrototypeDocumentContentSink.
  if (!mCurrentScriptProto) {
    return NS_OK;
  }

  // When compiling off thread the script will not have been attached to the
  // script proto yet.
  if (aStencil && !mCurrentScriptProto->HasStencil()) {
    mCurrentScriptProto->Set(aStencil);
  }

  // Allow load events to be fired once off thread compilation finishes.
  if (mOffThreadCompiling) {
    mOffThreadCompiling = false;
    mDocument->UnblockOnload(false);
  }

  // Clear mCurrentScriptProto now, but save it first for use below in
  // the execute code, and in the while loop that resumes walks of other
  // documents that raced to load this script.
  nsXULPrototypeScript* scriptProto = mCurrentScriptProto;
  mCurrentScriptProto = nullptr;

  // Clear the prototype's loading flag before executing the script or
  // resuming document walks, in case any of those control flows starts a
  // new script load.
  scriptProto->mSrcLoading = false;

  nsresult rv = aStatus;
  if (NS_SUCCEEDED(rv)) {
    rv = ExecuteScript(scriptProto);

    // If the XUL cache is enabled, save the script object there in
    // case different XUL documents source the same script.
    //
    // But don't save the script in the cache unless the master XUL
    // document URL is a chrome: URL.  It is valid for a URL such as
    // about:config to translate into a master document URL, whose
    // prototype document nodes -- including prototype scripts that
    // hold GC roots protecting their mJSObject pointers -- are not
    // cached in the XUL prototype cache.  See StartDocumentLoad,
    // the fillXULCache logic.
    //
    // A document such as about:config is free to load a script via
    // a URL such as chrome://global/content/config.js, and we must
    // not cache that script object without a prototype cache entry
    // containing a companion nsXULPrototypeScript node that owns a
    // GC root protecting the script object.  Otherwise, the script
    // cache entry will dangle once the uncached prototype document
    // is released when its owning document is unloaded.
    //
    // (See http://bugzilla.mozilla.org/show_bug.cgi?id=98207 for
    // the true crime story.)
    bool useXULCache = nsXULPrototypeCache::GetInstance()->IsEnabled();

    if (useXULCache && mDocumentURI->SchemeIs("chrome") &&
        scriptProto->HasStencil()) {
      nsXULPrototypeCache::GetInstance()->PutStencil(scriptProto->mSrcURI,
                                                     scriptProto->GetStencil());
    }
    // ignore any evaluation errors
  }

  rv = ResumeWalk();

  // Load a pointer to the prototype-script's list of documents who
  // raced to load the same script
  PrototypeDocumentContentSink** docp = &scriptProto->mSrcLoadWaiters;

  // Resume walking other documents that waited for this one's load, first
  // executing the script we just compiled, in each doc's script context
  PrototypeDocumentContentSink* doc;
  while ((doc = *docp) != nullptr) {
    NS_ASSERTION(doc->mCurrentScriptProto == scriptProto,
                 "waiting for wrong script to load?");
    doc->mCurrentScriptProto = nullptr;

    // Unlink doc from scriptProto's list before executing and resuming
    *docp = doc->mNextSrcLoadWaiter;
    doc->mNextSrcLoadWaiter = nullptr;

    if (aStatus == NS_BINDING_ABORTED && !scriptProto->HasStencil()) {
      // If the previous doc load was aborted, we want to try loading
      // again for the next doc. Otherwise, one abort would lead to all
      // subsequent waiting docs to abort as well.
      bool block = false;
      doc->LoadScript(scriptProto, &block);
      NS_RELEASE(doc);
      return rv;
    }

    // Execute only if we loaded and compiled successfully, then resume
    if (NS_SUCCEEDED(aStatus) && scriptProto->HasStencil()) {
      doc->ExecuteScript(scriptProto);
    }
    doc->ResumeWalk();
    NS_RELEASE(doc);
  }

  return rv;
}

nsresult PrototypeDocumentContentSink::ExecuteScript(
    nsXULPrototypeScript* aScript) {
  MOZ_ASSERT(aScript != nullptr, "null ptr");
  NS_ENSURE_TRUE(aScript, NS_ERROR_NULL_POINTER);

  nsIScriptGlobalObject* scriptGlobalObject;
  bool aHasHadScriptHandlingObject;
  scriptGlobalObject =
      mDocument->GetScriptHandlingObject(aHasHadScriptHandlingObject);

  NS_ENSURE_TRUE(scriptGlobalObject, NS_ERROR_NOT_INITIALIZED);

  nsresult rv;
  rv = scriptGlobalObject->EnsureScriptEnvironment();
  NS_ENSURE_SUCCESS(rv, rv);

  // Execute the precompiled script with the given version
  nsAutoMicroTask mt;

  // We're about to run script via JS_ExecuteScript, so we need an
  // AutoEntryScript. This is Gecko specific and not in any spec.
  AutoEntryScript aes(scriptGlobalObject, "precompiled XUL <script> element");
  JSContext* cx = aes.cx();

  JS::Rooted<JSScript*> scriptObject(cx);
  rv = aScript->InstantiateScript(cx, &scriptObject);
  NS_ENSURE_SUCCESS(rv, rv);

  JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));
  NS_ENSURE_TRUE(xpc::Scriptability::Get(global).Allowed(), NS_OK);

  if (!aScript->mOutOfLine) {
    // Check if CSP allows loading of inline scripts.
    if (nsCOMPtr<nsIContentSecurityPolicy> csp =
            PolicyContainer::GetCSP(mDocument->GetPolicyContainer())) {
      nsAutoJSString content;
      JS::Rooted<JSString*> decompiled(cx,
                                       JS_DecompileScript(cx, scriptObject));
      if (NS_WARN_IF(!decompiled || !content.init(cx, decompiled))) {
        JS_ClearPendingException(cx);
      }

      bool allowInlineScript = false;
      rv = csp->GetAllowsInline(
          nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE,
          /* aHasUnsafeHash */ false, /* aNonce */ u""_ns,
          /* aParserCreated */ true,
          /* aTriggeringElement */ nullptr,
          /* nsICSPEventListener */ nullptr,
          /* aContentOfPseudoScript */ content, aScript->mLineNo,
          /* aColumnNumber */ 0, &allowInlineScript);
      if (NS_FAILED(rv) || !allowInlineScript) {
        return NS_OK;
      }
    }
  }

  // On failure, ~AutoScriptEntry will handle exceptions, so
  // there is no need to manually check the return value.
  JS::Rooted<JS::Value> rval(cx);
  (void)JS_ExecuteScript(cx, scriptObject, &rval);

  return NS_OK;
}

nsresult PrototypeDocumentContentSink::CreateElementFromPrototype(
    nsXULPrototypeElement* aPrototype, Element** aResult, nsIContent* aParent) {
  // Create a content model element from a prototype element.
  MOZ_ASSERT(aPrototype, "null ptr");
  if (!aPrototype) return NS_ERROR_NULL_POINTER;

  *aResult = nullptr;
  nsresult rv = NS_OK;

  if (MOZ_LOG_TEST(gLog, LogLevel::Debug)) {
    MOZ_LOG(
        gLog, LogLevel::Debug,
        ("prototype: creating <%s> from prototype",
         NS_ConvertUTF16toUTF8(aPrototype->mNodeInfo->QualifiedName()).get()));
  }

  RefPtr<Element> result;

  Document* doc = aParent ? aParent->OwnerDoc() : mDocument.get();
  if (aPrototype->mNodeInfo->NamespaceEquals(kNameSpaceID_XUL)) {
    const bool isRoot = !aParent;
    // If it's a XUL element, it'll be lightweight until somebody
    // monkeys with it.
    result = nsXULElement::CreateFromPrototype(aPrototype, doc, isRoot);
    if (!result) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  } else {
    // If it's not a XUL element, it's gonna be heavyweight no matter
    // what. So we need to copy everything out of the prototype
    // into the element.  Get a nodeinfo from our nodeinfo manager
    // for this node.
    RefPtr<NodeInfo> newNodeInfo = doc->NodeInfoManager()->GetNodeInfo(
        aPrototype->mNodeInfo->NameAtom(),
        aPrototype->mNodeInfo->GetPrefixAtom(),
        aPrototype->mNodeInfo->NamespaceID(), nsINode::ELEMENT_NODE);
    if (!newNodeInfo) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    const bool isScript =
        newNodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_XHTML) ||
        newNodeInfo->Equals(nsGkAtoms::script, kNameSpaceID_SVG);
    if (aPrototype->mIsAtom &&
        newNodeInfo->NamespaceID() == kNameSpaceID_XHTML) {
      rv = NS_NewHTMLElement(getter_AddRefs(result), newNodeInfo.forget(),
                             FROM_PARSER_NETWORK, aPrototype->mIsAtom);
    } else {
      rv = NS_NewElement(getter_AddRefs(result), newNodeInfo.forget(),
                         FROM_PARSER_NETWORK);
    }
    if (NS_FAILED(rv)) return rv;

    rv = AddAttributes(aPrototype, result);
    if (NS_FAILED(rv)) return rv;

    if (isScript) {
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(result);
      MOZ_ASSERT(sele, "Node didn't QI to script.");
      sele->FreezeExecutionAttrs(doc);
    }
  }

  // FIXME(bug 1627474): Is this right if this is inside an <html:template>?
  if (result->HasAttr(nsGkAtoms::datal10nid)) {
    if (aPrototype->mTranslated) {
      result->SetElementTranslatedFromPrototype();
    } else {
      mDocument->mUntranslatedL10nProtoElements.InsertOrUpdate(
          result, RefPtr{aPrototype});
    }
  }
  result.forget(aResult);
  return NS_OK;
}

nsresult PrototypeDocumentContentSink::AddAttributes(
    nsXULPrototypeElement* aPrototype, Element* aElement) {
  nsresult rv;

  for (size_t i = 0; i < aPrototype->mAttributes.Length(); ++i) {
    nsXULPrototypeAttribute* protoattr = &(aPrototype->mAttributes[i]);
    nsAutoString valueStr;
    protoattr->mValue.ToString(valueStr);

    rv = aElement->SetAttr(
        protoattr->mName.NamespaceID(), protoattr->mName.LocalName(),
        protoattr->mName.GetPrefix(), valueStr, false, IsKnownNewAttr::Yes);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

}  // namespace mozilla::dom
