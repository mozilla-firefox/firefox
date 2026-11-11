/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5Parser.h"

#include <memory>

#include "ErrorList.h"
#include "encoding_rs_statics.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsCRT.h"
#include "nsContentUtils.h"  // for kLoadAsData
#include "nsHtml5AtomTable.h"
#include "nsHtml5DependentUTF16Buffer.h"
#include "nsHtml5Tokenizer.h"
#include "nsHtml5TreeBuilder.h"
#include "nsNetUtil.h"

using namespace mozilla;

NS_INTERFACE_TABLE_HEAD(nsHtml5Parser)
  NS_INTERFACE_TABLE(nsHtml5Parser, nsIParser, nsISupportsWeakReference,
                     nsIStreamListener)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsHtml5Parser)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsHtml5Parser)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsHtml5Parser)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsHtml5Parser)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsHtml5Parser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mExecutor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(GetStreamParser())
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsHtml5Parser)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mExecutor)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
  tmp->DropStreamParser();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

nsHtml5Parser::nsHtml5Parser()
    : mAboutBlankMode(false),
      mLastWasCR(false),
      mDocWriteSpeculativeLastWasCR(false),
      mBlocked(0),
      mDocWriteSpeculatorActive(false),
      mScriptNestingLevel(0),
      mTerminationStarted(false),
      mDocumentClosed(false),
      mInDocumentWrite(false),
      mInsertionPointPermanentlyUndefined(false),
      mFirstBuffer(new nsHtml5OwningUTF16Buffer((void*)nullptr)),
      mLastBuffer(mFirstBuffer),
      mExecutor(new nsHtml5TreeOpExecutor()),
      mTreeBuilder(new nsHtml5TreeBuilder(mExecutor, nullptr, false)),
      mTokenizer(new nsHtml5Tokenizer(mTreeBuilder.get(), false)),
      mRootContextLineNumber(1),
      mReturnToStreamParserPermitted(false) {
  mTokenizer->setInterner(&mAtomTable);
  mTokenizer->setProcessingInstructionsEnabled(
      mozilla::StaticPrefs::dom_template_for_enabled());
}

nsHtml5Parser::~nsHtml5Parser() {
  mTokenizer->end();
  if (mDocWriteSpeculativeTokenizer) {
    mDocWriteSpeculativeTokenizer->end();
  }
}

NS_IMETHODIMP_(void)
nsHtml5Parser::SetContentSink(nsIContentSink* aSink) {
  NS_ASSERTION(aSink == static_cast<nsIContentSink*>(mExecutor),
               "Attempt to set a foreign sink.");
}

NS_IMETHODIMP_(nsIContentSink*)
nsHtml5Parser::GetContentSink() {
  return static_cast<nsIContentSink*>(mExecutor);
}

NS_IMETHODIMP_(void)
nsHtml5Parser::GetCommand(nsCString& aCommand) {
  aCommand.AssignLiteral("view");
}

NS_IMETHODIMP_(void)
nsHtml5Parser::SetCommand(const char* aCommand) {
  NS_ASSERTION(!strcmp(aCommand, "view") || !strcmp(aCommand, "view-source") ||
                   !strcmp(aCommand, "external-resource") ||
                   !strcmp(aCommand, "import") ||
                   !strcmp(aCommand, kLoadAsData),
               "Unsupported parser command");
}

NS_IMETHODIMP_(void)
nsHtml5Parser::SetCommand(eParserCommands aParserCommand) {
  NS_ASSERTION(aParserCommand == eViewNormal,
               "Parser command was not eViewNormal.");
}

void nsHtml5Parser::SetDocumentCharset(NotNull<const Encoding*> aEncoding,
                                       int32_t aCharsetSource,
                                       bool aForceAutoDetection) {
  MOZ_ASSERT(!mExecutor->HasStarted(), "Document charset set too late.");
  if (mAboutBlankMode) {
    MOZ_ASSERT(aEncoding == UTF_8_ENCODING);
  } else {
    MOZ_ASSERT(GetStreamParser(), "Setting charset on a script-only parser.");
    GetStreamParser()->SetDocumentCharset(
        aEncoding, (nsCharsetSource)aCharsetSource, aForceAutoDetection);
  }
  mExecutor->SetDocumentCharsetAndSource(aEncoding,
                                         (nsCharsetSource)aCharsetSource);
}

nsresult nsHtml5Parser::GetChannel(nsIChannel** aChannel) {
  if (GetStreamParser()) {
    return GetStreamParser()->GetChannel(aChannel);
  }
  return NS_ERROR_NOT_AVAILABLE;
}

nsIStreamListener* nsHtml5Parser::GetStreamListener() {
  if (mAboutBlankMode) {
    return this;
  }
  return mStreamListener;
}

NS_IMETHODIMP
nsHtml5Parser::ContinueInterruptedParsing() {
  MOZ_ASSERT_UNREACHABLE("Don't call. For interface compat only.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP_(void)
nsHtml5Parser::BlockParser() {
  MOZ_ASSERT(!mAboutBlankMode, "Must not block about:blank");
  mBlocked++;
}

NS_IMETHODIMP_(void)
nsHtml5Parser::UnblockParser() {
  MOZ_ASSERT(!mAboutBlankMode, "Must not unblock about:blank");
  MOZ_DIAGNOSTIC_ASSERT(mBlocked > 0);
  if (MOZ_LIKELY(mBlocked > 0)) {
    mBlocked--;
  }
  if (MOZ_LIKELY(mBlocked == 0) && mExecutor) {
    mExecutor->ContinueInterruptedParsingAsync();
  }
}

NS_IMETHODIMP_(void)
nsHtml5Parser::ContinueInterruptedParsingAsync() {
  if (mExecutor) {
    mExecutor->ContinueInterruptedParsingAsync();
  }
}

NS_IMETHODIMP_(bool)
nsHtml5Parser::IsParserEnabled() { return !mBlocked; }

NS_IMETHODIMP_(bool)
nsHtml5Parser::IsParserClosed() { return mDocumentClosed; }

NS_IMETHODIMP_(bool)
nsHtml5Parser::IsComplete() { return mExecutor->IsComplete(); }

NS_IMETHODIMP
nsHtml5Parser::Parse(nsIURI* aURL) {
  /*
   * Do NOT cause WillBuildModel to be called synchronously from here!
   * The document won't be ready for it until OnStartRequest!
   */
  MOZ_ASSERT(!mExecutor->HasStarted(),
             "Tried to start parse without initializing the parser.");
  if (!mAboutBlankMode) {
    MOZ_ASSERT(GetStreamParser(),
               "Can't call this Parse() variant on script-created parser");

    GetStreamParser()->SetViewSourceTitle(
        aURL);  // In case we're viewing source
    mExecutor->SetStreamParser(GetStreamParser());
  }
  mExecutor->SetParser(this);
  return NS_OK;
}

nsresult nsHtml5Parser::Parse(const nsAString& aSourceBuffer, void* aKey,
                              bool aLastCall) {
  nsresult rv;
  if (NS_FAILED(rv = mExecutor->IsBroken())) {
    return rv;
  }
  if (aSourceBuffer.Length() > INT32_MAX) {
    return mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
  }

  // Maintain a reference to ourselves so we don't go away
  // till we're completely done. The old parser grips itself in this method.
  nsCOMPtr<nsIParser> kungFuDeathGrip(this);

  // Gripping the other objects just in case, since the other old grip
  // required grips to these, too.
  RefPtr<nsHtml5StreamParser> streamKungFuDeathGrip(GetStreamParser());
  (void)streamKungFuDeathGrip;  // Not used within function
  RefPtr<nsHtml5TreeOpExecutor> executor(mExecutor);

  MOZ_RELEASE_ASSERT(executor->HasStarted());

  // Return early if the parser has processed EOF
  if (executor->IsComplete()) {
    return NS_OK;
  }

  if (aLastCall && aSourceBuffer.IsEmpty() && !aKey) {
    // document.close()
    NS_ASSERTION(!GetStreamParser(),
                 "Had stream parser but got document.close().");
    if (mDocumentClosed) {
      // already closed
      return NS_OK;
    }
    mDocumentClosed = true;
    if (!mBlocked && !mInDocumentWrite && !executor->IsFlushing()) {
      return ParseUntilBlocked();
    }
    return NS_OK;
  }

  // If we got this far, we are dealing with a document.write or
  // document.writeln call--not document.close().

  MOZ_RELEASE_ASSERT(
      IsInsertionPointDefined(),
      "Doc.write reached parser with undefined insertion point.");

  MOZ_RELEASE_ASSERT(!(GetStreamParser() && !aKey),
                     "Got a null key in a non-script-created parser");

  // XXX is this optimization bogus?
  if (aSourceBuffer.IsEmpty()) {
    return NS_OK;
  }

  // This guard is here to prevent document.close from tokenizing synchronously
  // while a document.write (that wrote the script that called document.close!)
  // is still on the call stack.
  mozilla::AutoRestore<bool> guard(mInDocumentWrite);
  mInDocumentWrite = true;

  // The script is identified by aKey. If there's nothing in the buffer
  // chain for that key, we'll insert at the head of the queue.
  // When the script leaves something in the queue, a zero-length
  // key-holder "buffer" is inserted in the queue. If the same script
  // leaves something in the chain again, it will be inserted immediately
  // before the old key holder belonging to the same script.
  //
  // We don't do the actual data insertion yet in the hope that the data gets
  // tokenized and there no data or less data to copy to the heap after
  // tokenization. Also, this way, we avoid inserting one empty data buffer
  // per document.write, which matters for performance when the parser isn't
  // blocked and a badly-authored script calls document.write() once per
  // input character. (As seen in a benchmark!)
  //
  // The insertion into the input stream happens conceptually before anything
  // gets tokenized. To make sure multi-level document.write works right,
  // it's necessary to establish the location of our parser key up front
  // in case this is the first write with this key.
  //
  // In a document.open() case, the first write level has a null key, so that
  // case is handled separately, because normal buffers containing data
  // have null keys.

  // These don't need to be owning references, because they always point to
  // the buffer queue and buffers can't be removed from the buffer queue
  // before document.write() returns. The buffer queue clean-up happens the
  // next time ParseUntilBlocked() is called.
  // However, they are made owning just in case the reasoning above is flawed
  // and a flaw would lead to worse problems with plain pointers. If this
  // turns out to be a perf problem, it's worthwhile to consider making
  // prevSearchbuf a plain pointer again.
  RefPtr<nsHtml5OwningUTF16Buffer> prevSearchBuf;
  RefPtr<nsHtml5OwningUTF16Buffer> firstLevelMarker;

  if (aKey) {
    if (mFirstBuffer == mLastBuffer) {
      nsHtml5OwningUTF16Buffer* keyHolder = new nsHtml5OwningUTF16Buffer(aKey);
      keyHolder->next = mLastBuffer;
      mFirstBuffer = keyHolder;
    } else if (mFirstBuffer->key != aKey) {
      prevSearchBuf = mFirstBuffer;
      for (;;) {
        if (prevSearchBuf->next == mLastBuffer) {
          // key was not found
          nsHtml5OwningUTF16Buffer* keyHolder =
              new nsHtml5OwningUTF16Buffer(aKey);
          keyHolder->next = mFirstBuffer;
          mFirstBuffer = keyHolder;
          prevSearchBuf = nullptr;
          break;
        }
        if (prevSearchBuf->next->key == aKey) {
          // found a key holder
          break;
        }
        prevSearchBuf = prevSearchBuf->next;
      }
    }  // else mFirstBuffer is the keyholder

    // prevSearchBuf is the previous buffer before the keyholder or null if
    // there isn't one.
  } else {
    // We have a first-level write in the document.open() case. We insert before
    // mLastBuffer, effectively, by making mLastBuffer be a new sentinel object
    // and redesignating the previous mLastBuffer as our firstLevelMarker.  We
    // need to put a marker there, because otherwise additional document.writes
    // from nested event loops would insert in the wrong place. Sigh.
    mLastBuffer->next = new nsHtml5OwningUTF16Buffer((void*)nullptr);
    firstLevelMarker = mLastBuffer;
    mLastBuffer = mLastBuffer->next;
  }

  nsHtml5DependentUTF16Buffer stackBuffer(aSourceBuffer);

  while (!mBlocked && stackBuffer.hasMore()) {
    stackBuffer.adjust(mLastWasCR);
    mLastWasCR = false;
    if (stackBuffer.hasMore()) {
      int32_t lineNumberSave;
      bool inRootContext = (!GetStreamParser() && !aKey);
      if (inRootContext) {
        mTokenizer->setLineNumber(mRootContextLineNumber);
      } else {
        // we aren't the root context, so save the line number on the
        // *stack* so that we can restore it.
        lineNumberSave = mTokenizer->getLineNumber();
      }

      if (!mTokenizer->EnsureBufferSpace(stackBuffer.getLength())) {
        return executor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      }
      mLastWasCR = mTokenizer->tokenizeBuffer(&stackBuffer);
      if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
        return executor->MarkAsBroken(rv);
      }

      if (inRootContext) {
        mRootContextLineNumber = mTokenizer->getLineNumber();
      } else {
        mTokenizer->setLineNumber(lineNumberSave);
      }

      if (mTreeBuilder->HasScriptThatMayDocumentWriteOrBlock()) {
        auto r = mTreeBuilder->Flush();  // Move ops to the executor
        if (r.isErr()) {
          return executor->MarkAsBroken(r.unwrapErr());
        }
        rv = executor->FlushDocumentWrite();  // run the ops
        NS_ENSURE_SUCCESS(rv, rv);
        // Flushing tree ops can cause all sorts of things.
        // Return early if the parser got terminated.
        if (executor->IsComplete()) {
          return NS_OK;
        }
      }
      // Ignore suspension requests
    }
  }

  RefPtr<nsHtml5OwningUTF16Buffer> heapBuffer;
  if (stackBuffer.hasMore()) {
    // The buffer wasn't tokenized to completion. Create a copy of the tail
    // on the heap.
    heapBuffer = stackBuffer.FalliblyCopyAsOwningBuffer();
    if (!heapBuffer) {
      // Allocation failed. The parser is now broken.
      return executor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
    }
  }

  if (heapBuffer) {
    // We have something to insert before the keyholder holding in the non-null
    // aKey case and we have something to swap into firstLevelMarker in the
    // null aKey case.
    if (aKey) {
      NS_ASSERTION(mFirstBuffer != mLastBuffer, "Where's the keyholder?");
      // the key holder is still somewhere further down the list from
      // prevSearchBuf (which may be null)
      if (mFirstBuffer->key == aKey) {
        NS_ASSERTION(
            !prevSearchBuf,
            "Non-null prevSearchBuf when mFirstBuffer is the key holder?");
        heapBuffer->next = mFirstBuffer;
        mFirstBuffer = heapBuffer;
      } else {
        if (!prevSearchBuf) {
          prevSearchBuf = mFirstBuffer;
        }
        // We created a key holder earlier, so we will find it without walking
        // past the end of the list.
        while (prevSearchBuf->next->key != aKey) {
          prevSearchBuf = prevSearchBuf->next;
        }
        heapBuffer->next = prevSearchBuf->next;
        prevSearchBuf->next = heapBuffer;
      }
    } else {
      NS_ASSERTION(firstLevelMarker, "How come we don't have a marker.");
      firstLevelMarker->Swap(heapBuffer);
    }
  }

  if (!mBlocked) {  // buffer was tokenized to completion
    NS_ASSERTION(!stackBuffer.hasMore(),
                 "Buffer wasn't tokenized to completion?");
    // Scripting semantics require a forced tree builder flush here
    auto r = mTreeBuilder->Flush();  // Move ops to the executor
    if (r.isErr()) {
      return executor->MarkAsBroken(r.unwrapErr());
    }
    rv = executor->FlushDocumentWrite();  // run the ops
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (stackBuffer.hasMore()) {
    // The buffer wasn't tokenized to completion. Tokenize the untokenized
    // content in order to preload stuff. This content will be retokenized
    // later for normal parsing.
    if (!mDocWriteSpeculatorActive) {
      mDocWriteSpeculatorActive = true;
      if (!mDocWriteSpeculativeTreeBuilder) {
        // Lazily initialize if uninitialized
        mDocWriteSpeculativeTreeBuilder = std::make_unique<nsHtml5TreeBuilder>(
            nullptr, executor->GetStage(), true);
        mDocWriteSpeculativeTreeBuilder->setScriptingEnabled(
            mTreeBuilder->isScriptingEnabled());
        mDocWriteSpeculativeTreeBuilder->setAllowDeclarativeShadowRoots(
            mTreeBuilder->isAllowDeclarativeShadowRoots());
        mDocWriteSpeculativeTokenizer = std::make_unique<nsHtml5Tokenizer>(
            mDocWriteSpeculativeTreeBuilder.get(), false);
        mDocWriteSpeculativeTokenizer->setInterner(&mAtomTable);
        mDocWriteSpeculativeTokenizer->start();
      }
      mDocWriteSpeculativeTokenizer->resetToDataState();
      mDocWriteSpeculativeTreeBuilder->loadState(mTreeBuilder.get());
      mDocWriteSpeculativeLastWasCR = false;
    }

    // Note that with multilevel document.write if we didn't just activate the
    // speculator, it's possible that the speculator is now in the wrong state.
    // That's OK for the sake of simplicity. The worst that can happen is
    // that the speculative loads aren't exactly right. The content will be
    // reparsed anyway for non-preload purposes.

    // The buffer position for subsequent non-speculative parsing now lives
    // in heapBuffer, so it's ok to let the buffer position of stackBuffer
    // to be overwritten and not restored below.
    while (stackBuffer.hasMore()) {
      stackBuffer.adjust(mDocWriteSpeculativeLastWasCR);
      if (stackBuffer.hasMore()) {
        if (!mDocWriteSpeculativeTokenizer->EnsureBufferSpace(
                stackBuffer.getLength())) {
          return executor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        }
        mDocWriteSpeculativeLastWasCR =
            mDocWriteSpeculativeTokenizer->tokenizeBuffer(&stackBuffer);
        nsresult rv;
        if (NS_FAILED((rv = mDocWriteSpeculativeTreeBuilder->IsBroken()))) {
          return executor->MarkAsBroken(rv);
        }
      }
    }

    auto r = mDocWriteSpeculativeTreeBuilder->Flush();
    if (r.isErr()) {
      return executor->MarkAsBroken(r.unwrapErr());
    }
    mDocWriteSpeculativeTreeBuilder->DropHandles();
    executor->FlushSpeculativeLoads();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHtml5Parser::Terminate() {
  if (mTerminationStarted) {
    return NS_OK;
  }
  // `mExecutor->IsComplete()` becomes true too late, if JavaScript event
  // handlers cause a nested event loop or calls back into the parser during the
  // termination. It's also hard to make `mExecutor->IsComplete()` to become
  // true sooner. Hence, let's have yet another flag to prevent further parsing.
  mTerminationStarted = true;
  // We should only call DidBuildModel once, so don't do anything if this is
  // the second time that Terminate has been called.
  if (mExecutor->IsComplete()) {
    return NS_OK;
  }
  // XXX - [ until we figure out a way to break parser-sink circularity ]
  // Hack - Hold a reference until we are completely done...
  nsCOMPtr<nsIParser> kungFuDeathGrip(this);
  RefPtr<nsHtml5StreamParser> streamParser(GetStreamParser());
  RefPtr<nsHtml5TreeOpExecutor> executor(mExecutor);
  if (streamParser) {
    streamParser->Terminate();
  }
  return executor->DidBuildModel(true);
}

bool nsHtml5Parser::IsInsertionPointDefined() {
  return !mExecutor->IsFlushing() && !mInsertionPointPermanentlyUndefined &&
         (!GetStreamParser() || mScriptNestingLevel != 0);
}

void nsHtml5Parser::IncrementScriptNestingLevel() { ++mScriptNestingLevel; }

void nsHtml5Parser::DecrementScriptNestingLevel() { --mScriptNestingLevel; }

bool nsHtml5Parser::HasNonzeroScriptNestingLevel() const {
  return mScriptNestingLevel != 0;
}

void nsHtml5Parser::MarkAsNotScriptCreated(const char* aCommand) {
  MOZ_ASSERT(!mStreamListener, "Must not call this twice.");
  eParserMode mode = NORMAL;
  if (!nsCRT::strcmp(aCommand, "view-source")) {
    mode = VIEW_SOURCE_HTML;
  } else if (!nsCRT::strcmp(aCommand, "view-source-xml")) {
    mode = VIEW_SOURCE_XML;
  } else if (!nsCRT::strcmp(aCommand, "view-source-plain")) {
    mode = VIEW_SOURCE_PLAIN;
  } else if (!nsCRT::strcmp(aCommand, "plain-text")) {
    mode = PLAIN_TEXT;
  } else if (!nsCRT::strcmp(aCommand, kLoadAsData)) {
    mode = LOAD_AS_DATA;
  } else if (!nsCRT::strcmp(aCommand, "about-blank")) {
    mode = ABOUT_BLANK;
  }
#ifdef DEBUG
  else {
    NS_ASSERTION(!nsCRT::strcmp(aCommand, "view") ||
                     !nsCRT::strcmp(aCommand, "external-resource") ||
                     !nsCRT::strcmp(aCommand, "import"),
                 "Unsupported parser command!");
  }
#endif
  if (mode == ABOUT_BLANK) {
    mAboutBlankMode = true;
  } else {
    mStreamListener = new nsHtml5StreamListener(
        new nsHtml5StreamParser(mExecutor, this, mode));
  }
}

bool nsHtml5Parser::IsScriptCreated() { return !GetStreamParser(); }

bool nsHtml5Parser::IsAboutBlankMode() { return mAboutBlankMode; }

/* End nsIParser  */

// not from interface
nsresult nsHtml5Parser::ParseUntilBlocked() {
  nsresult rv = mExecutor->IsBroken();
  NS_ENSURE_SUCCESS(rv, rv);
  if (mBlocked || mInsertionPointPermanentlyUndefined || mTerminationStarted ||
      mExecutor->IsComplete()) {
    return NS_OK;
  }
  NS_ASSERTION(mExecutor->HasStarted(), "Bad life cycle.");
  NS_ASSERTION(!mInDocumentWrite,
               "ParseUntilBlocked entered while in doc.write!");

  mDocWriteSpeculatorActive = false;

  for (;;) {
    if (!mFirstBuffer->hasMore()) {
      if (mFirstBuffer == mLastBuffer) {
        if (mExecutor->IsComplete()) {
          // something like cache manisfests stopped the parse in mid-flight
          return NS_OK;
        }
        if (mDocumentClosed) {
          PermanentlyUndefineInsertionPoint();
          nsresult rv;
          MOZ_RELEASE_ASSERT(
              !GetStreamParser(),
              "This should only happen with script-created parser.");
          if (NS_SUCCEEDED((rv = mExecutor->IsBroken()))) {
            mTokenizer->eof();
            if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
              mExecutor->MarkAsBroken(rv);
            } else {
              mTreeBuilder->StreamEnded();
            }
          }
          auto r = mTreeBuilder->Flush();
          if (r.isErr()) {
            return mExecutor->MarkAsBroken(r.unwrapErr());
          }
          mExecutor->FlushDocumentWrite();
          // The below call does memory cleanup, so call it even if the
          // parser has been marked as broken.
          mTokenizer->end();
          return rv;
        }
        // never release the last buffer.
        NS_ASSERTION(!mLastBuffer->getStart() && !mLastBuffer->getEnd(),
                     "Sentinel buffer had its indeces changed.");
        if (GetStreamParser()) {
          if (mReturnToStreamParserPermitted &&
              !mExecutor->IsScriptExecuting()) {
            auto r = mTreeBuilder->Flush();
            if (r.isErr()) {
              return mExecutor->MarkAsBroken(r.unwrapErr());
            }
            mReturnToStreamParserPermitted = false;
            GetStreamParser()->ContinueAfterScriptsOrEncodingCommitment(
                mTokenizer.get(), mTreeBuilder.get(), mLastWasCR);
          }
        } else {
          // Script-created parser
          auto r = mTreeBuilder->Flush();
          if (r.isErr()) {
            return mExecutor->MarkAsBroken(r.unwrapErr());
          }
          // No need to flush the executor, because the executor is already
          // in a flush
          NS_ASSERTION(mExecutor->IsInFlushLoop(),
                       "How did we come here without being in the flush loop?");
        }
        return NS_OK;  // no more data for now but expecting more
      }
      mFirstBuffer = mFirstBuffer->next;
      continue;
    }

    if (mBlocked || mExecutor->IsComplete()) {
      return NS_OK;
    }

    // now we have a non-empty buffer
    mFirstBuffer->adjust(mLastWasCR);
    mLastWasCR = false;
    if (mFirstBuffer->hasMore()) {
      bool inRootContext = (!GetStreamParser() && !mFirstBuffer->key);
      if (inRootContext) {
        mTokenizer->setLineNumber(mRootContextLineNumber);
      }
      if (!mTokenizer->EnsureBufferSpace(mFirstBuffer->getLength())) {
        return mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      }
      mLastWasCR = mTokenizer->tokenizeBuffer(mFirstBuffer);
      nsresult rv;
      if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
        return mExecutor->MarkAsBroken(rv);
      }
      if (inRootContext) {
        mRootContextLineNumber = mTokenizer->getLineNumber();
      }
      if (mTreeBuilder->HasScriptThatMayDocumentWriteOrBlock()) {
        auto r = mTreeBuilder->Flush();
        if (r.isErr()) {
          return mExecutor->MarkAsBroken(r.unwrapErr());
        }
        rv = mExecutor->FlushDocumentWrite();
        NS_ENSURE_SUCCESS(rv, rv);
      }
      if (mBlocked) {
        return NS_OK;
      }
    }
  }
}

nsresult nsHtml5Parser::StartExecutor() {
  MOZ_ASSERT(!GetStreamParser(),
             "Had stream parser but document.write started life cycle.");
  // This is part of the setup document.open() does.
  RefPtr<nsHtml5TreeOpExecutor> executor(mExecutor);
  executor->SetParser(this);
  mTreeBuilder->setScriptingEnabled(executor->IsScriptEnabled());
  mTreeBuilder->setAllowDeclarativeShadowRoots(
      executor->GetDocument()->AllowsDeclarativeShadowRoots());

  mTreeBuilder->setIsSrcdocDocument(false);

  mTokenizer->start();
  executor->Start();

  /*
   * We know we're in document.open(), so our document must already
   * have a script global andthe WillBuildModel call is safe.
   */
  return executor->WillBuildModel();
}

nsresult nsHtml5Parser::Initialize(mozilla::dom::Document* aDoc, nsIURI* aURI,
                                   nsISupports* aContainer,
                                   nsIChannel* aChannel) {
  mTreeBuilder->setAllowDeclarativeShadowRoots(
      aDoc->AllowsDeclarativeShadowRoots());
  return mExecutor->Init(aDoc, aURI, aContainer, aChannel);
}

void nsHtml5Parser::StartTokenizer(bool aScriptingEnabled) {
  bool isSrcdoc = false;
  nsCOMPtr<nsIChannel> channel;
  nsresult rv = GetChannel(getter_AddRefs(channel));
  if (NS_SUCCEEDED(rv)) {
    isSrcdoc = NS_IsSrcdocChannel(channel);
  }
  mTreeBuilder->setIsSrcdocDocument(isSrcdoc);

  mTreeBuilder->SetPreventScriptExecution(!aScriptingEnabled);
  mTreeBuilder->setScriptingEnabled(aScriptingEnabled);
  mTreeBuilder->setAllowDeclarativeShadowRoots(
      mExecutor->GetDocument()->AllowsDeclarativeShadowRoots());
  mTokenizer->start();
}

void nsHtml5Parser::InitializeDocWriteParserState(
    nsAHtml5TreeBuilderState* aState, int32_t aLine) {
  mTokenizer->resetToDataState();
  mTokenizer->setLineNumber(aLine);
  mTreeBuilder->loadState(aState);
  mLastWasCR = false;
  mReturnToStreamParserPermitted = true;
}

void nsHtml5Parser::ContinueAfterFailedCharsetSwitch() {
  MOZ_ASSERT(
      GetStreamParser(),
      "Tried to continue after failed charset switch without a stream parser");
  GetStreamParser()->ContinueAfterFailedCharsetSwitch();
}

NS_IMETHODIMP nsHtml5Parser::OnStartRequest(nsIRequest* aRequest) {
  if (!mAboutBlankMode) {
    MOZ_ASSERT(false,
               "Attempted to use nsHtml5Parser as stream listener in "
               "non-about:blank mode.");
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  MOZ_RELEASE_ASSERT(!GetStreamParser(),
                     "Should not have stream parser in about:blank mode.");
  mTokenizer->start();
  mExecutor->Start();
  nsresult rv = mExecutor->WillBuildModel();
  NS_ENSURE_SUCCESS(rv, rv);
  PermanentlyUndefineInsertionPoint();
  mTokenizer->eof();
  if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
    mExecutor->MarkAsBroken(rv);
  } else {
    mTreeBuilder->StreamEnded();
  }
  auto r = mTreeBuilder->Flush();
  if (r.isErr()) {
    return mExecutor->MarkAsBroken(r.unwrapErr());
  }
  mExecutor->FlushDocumentWrite();
  // The below call does memory cleanup, so call it even if the
  // parser has been marked as broken.
  mTokenizer->end();
  return rv;
}

NS_IMETHODIMP nsHtml5Parser::OnDataAvailable(nsIRequest* aRequest,
                                             nsIInputStream* aInStream,
                                             uint64_t aSourceOffset,
                                             uint32_t aLength) {
  if (!mAboutBlankMode) {
    MOZ_ASSERT(false,
               "Attempted to use nsHtml5Parser as stream listener in "
               "non-about:blank mode.");
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  if (aLength) {
    MOZ_ASSERT(false, "Non-zero-length stream in about:blank mode.");
    return NS_ERROR_ILLEGAL_INPUT;
  }
  return NS_OK;
}

NS_IMETHODIMP nsHtml5Parser::OnStopRequest(nsIRequest* aRequest,
                                           nsresult aStatus) {
  if (!mAboutBlankMode) {
    MOZ_ASSERT(false,
               "Attempted to use nsHtml5Parser as stream listener in "
               "non-about:blank mode.");
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  return NS_OK;
}
