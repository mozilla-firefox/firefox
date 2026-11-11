/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5StreamParser.h"

#include <stdlib.h>
#include <string.h>
#include <utility>
#include "ErrorList.h"
#include "GeckoProfiler.h"
#include "js/GCAPI.h"
#include "mozilla/Buffer.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Encoding.h"
#include "mozilla/EncodingDetector.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_html5.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TextUtils.h"

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/DebuggerUtilsBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/Vector.h"
#include "nsContentSink.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionTraversalCallback.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5Highlighter.h"
#include "nsHtml5Module.h"
#include "nsHtml5OwningUTF16Buffer.h"
#include "nsHtml5Parser.h"
#include "nsHtml5Speculation.h"
#include "nsHtml5StreamParserPtr.h"
#include "nsHtml5Tokenizer.h"
#include "nsHtml5TreeBuilder.h"
#include "nsHtml5TreeOpExecutor.h"
#include "nsIChannel.h"
#include "nsIContentSink.h"
#include "nsID.h"
#include "nsIDocShell.h"
#include "nsIHttpChannel.h"
#include "nsIInputStream.h"
#include "nsINestedURI.h"
#include "nsIObserverService.h"
#include "nsIRequest.h"
#include "nsIRunnable.h"
#include "nsIScriptError.h"
#include "nsIThread.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsJSEnvironment.h"
#include "nsLiteralString.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsTPromiseFlatString.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

extern "C" {
// Defined in intl/encoding_glue/src/lib.rs
const mozilla::Encoding* xmldecl_parse(const uint8_t* buf, size_t buf_len);
};

using namespace mozilla;
using namespace mozilla::dom;

/*
 * Note that nsHtml5StreamParser implements cycle collecting AddRef and
 * Release. Therefore, nsHtml5StreamParser must never be refcounted from
 * the parser thread!
 *
 * To work around this limitation, runnables posted by the main thread to the
 * parser thread hold their reference to the stream parser in an
 * nsHtml5StreamParserPtr. Upon creation, nsHtml5StreamParserPtr addrefs the
 * object it holds
 * just like a regular nsRefPtr. This is OK, since the creation of the
 * runnable and the nsHtml5StreamParserPtr happens on the main thread.
 *
 * When the runnable is done on the parser thread, the destructor of
 * nsHtml5StreamParserPtr runs there. It doesn't call Release on the held object
 * directly. Instead, it posts another runnable back to the main thread where
 * that runnable calls Release on the wrapped object.
 *
 * When posting runnables in the other direction, the runnables have to be
 * created on the main thread when nsHtml5StreamParser is instantiated and
 * held for the lifetime of the nsHtml5StreamParser. This works, because the
 * same runnabled can be dispatched multiple times and currently runnables
 * posted from the parser thread to main thread don't need to wrap any
 * runnable-specific data. (In the other direction, the runnables most notably
 * wrap the byte data of the stream.)
 */
NS_IMPL_CYCLE_COLLECTING_ADDREF(nsHtml5StreamParser)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsHtml5StreamParser)

NS_INTERFACE_TABLE_HEAD(nsHtml5StreamParser)
  NS_INTERFACE_TABLE(nsHtml5StreamParser, nsISupports)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsHtml5StreamParser)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsHtml5StreamParser)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsHtml5StreamParser)
  tmp->DropTimer();
  tmp->Terminate();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRequest)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner)

  // At this point, tasks on the parser thread can still
  // execute. We have a mechanism for making sure that the
  // stream parser stays alive for long enough. However, the
  // way tree ops make their way into the executor happens
  // through non-owning references, so we must not allow
  // `mExecutor` to be deleted too early.
  // `mExecutor` itself is a plain pointer, because
  // `mLoadFlusher`, and `mExecutorFlusher` hold strong
  // refs. Even though cycle collection traversal goes
  // through them to discover a cycle, we deliberately
  // don't set them to `nullptr` here to make sure that
  // the executor stays around for as long as the stream
  // parser stays around. We trust the cycle to be broken
  // at some other edge of the cycle.
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsHtml5StreamParser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRequest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
  // hack: count the strongly owned edge wrapped in the runnable
  if (tmp->mExecutorFlusher) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mExecutorFlusher->mExecutor");
    cb.NoteXPCOMChild(static_cast<nsIContentSink*>(tmp->mExecutor));
  }
  // hack: count the strongly owned edge wrapped in the runnable
  if (tmp->mLoadFlusher) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mLoadFlusher->mExecutor");
    cb.NoteXPCOMChild(static_cast<nsIContentSink*>(tmp->mExecutor));
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

class nsHtml5ExecutorFlusher : public Runnable {
 private:
  RefPtr<nsHtml5TreeOpExecutor> mExecutor;

 public:
  explicit nsHtml5ExecutorFlusher(nsHtml5TreeOpExecutor* aExecutor)
      : Runnable("nsHtml5ExecutorFlusher"), mExecutor(aExecutor) {}
  NS_IMETHOD Run() override {
    if (!mExecutor->isInList()) {
      Document* doc = mExecutor->GetDocument();
      if (XRE_IsContentProcess() &&
          nsContentUtils::
              HighPriorityEventPendingForTopLevelDocumentBeforeContentfulPaint(
                  doc)) {
        // Possible early paint pending, reuse the runnable and try to
        // call RunFlushLoop later.
        nsCOMPtr<nsIRunnable> flusher = this;
        if (NS_SUCCEEDED(doc->Dispatch(flusher.forget()))) {
          PROFILER_MARKER_UNTYPED("HighPrio blocking parser flushing(1)", DOM);
          return NS_OK;
        }
      }
      mExecutor->RunFlushLoop();
    }
    return NS_OK;
  }
};

class nsHtml5LoadFlusher : public Runnable {
 private:
  RefPtr<nsHtml5TreeOpExecutor> mExecutor;

 public:
  explicit nsHtml5LoadFlusher(nsHtml5TreeOpExecutor* aExecutor)
      : Runnable("nsHtml5LoadFlusher"), mExecutor(aExecutor) {}
  NS_IMETHOD Run() override {
    // If we're in sync XHR, do nothing. We'll flush the speculative loads
    // after the flush ends.
    if (!mExecutor->IsFlushing()) {
      mExecutor->FlushSpeculativeLoads();
    }
    return NS_OK;
  }
};

nsHtml5StreamParser::nsHtml5StreamParser(nsHtml5TreeOpExecutor* aExecutor,
                                         nsHtml5Parser* aOwner,
                                         eParserMode aMode)
    : mBomState(eBomState::BOM_SNIFFING_NOT_STARTED),
      mCharsetSource(kCharsetUninitialized),
      mEncodingSwitchSource(kCharsetUninitialized),
      mEncoding(X_USER_DEFINED_ENCODING),  // Obviously bogus value to notice if
                                           // not updated
      mNeedsEncodingSwitchTo(nullptr),
      mSeenEligibleMetaCharset(false),
      mChardetEof(false),
#ifdef DEBUG
      mStartedFeedingDetector(false),
      mStartedFeedingDevTools(false),
#endif
      mReparseForbidden(false),
      mForceAutoDetection(false),
      mChannelHadCharset(false),
      mLookingForMetaCharset(false),
      mStartsWithLtQuestion(false),
      mLookingForXmlDeclarationForXmlViewSource(false),
      mTemplatePushedOrHeadPopped(false),
      mGtBuffer(nullptr),
      mGtPos(0),
      mLastBuffer(nullptr),  // Will be filled when starting
      mExecutor(aExecutor),
      mTreeBuilder(new nsHtml5TreeBuilder(
          (aMode == VIEW_SOURCE_HTML || aMode == VIEW_SOURCE_XML)
              ? nullptr
              : mExecutor->GetStage(),
          mExecutor->GetStage(), aMode == NORMAL)),
      mTokenizer(
          new nsHtml5Tokenizer(mTreeBuilder.get(), aMode == VIEW_SOURCE_XML)),
      mTokenizerMutex("nsHtml5StreamParser mTokenizerMutex"),
      mOwner(aOwner),
      mLastWasCR(false),
      mStreamState(eHtml5StreamState::STREAM_NOT_STARTED),
      mSpeculating(false),
      mAtEOF(false),
      mSpeculationMutex("nsHtml5StreamParser mSpeculationMutex"),
      mSpeculationFailureCount(0),
      mNumBytesBuffered(0),
      mTerminated(false),
      mInterrupted(false),
      mEventTarget(nsHtml5Module::GetStreamParserEventTarget()),
      mExecutorFlusher(new nsHtml5ExecutorFlusher(aExecutor)),
      mLoadFlusher(new nsHtml5LoadFlusher(aExecutor)),
      mInitialEncodingWasFromParentFrame(false),
      mHasHadErrors(false),
      mDetectorHasSeenNonAscii(false),
      mDecodingLocalFileWithoutTokenizing(false),
      mBufferingBytes(false),
      mFlushTimer(NS_NewTimer(mEventTarget)),
      mFlushTimerMutex("nsHtml5StreamParser mFlushTimerMutex"),
      mFlushTimerArmed(false),
      mFlushTimerEverFired(false),
      mMode(aMode),
      mBrowserIdForDevtools(0),
      mBrowsingContextIDForDevtools(0) {
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
#ifdef DEBUG
  mAtomTable.SetPermittedLookupEventTarget(mEventTarget);
#endif
  mTokenizer->setInterner(&mAtomTable);
  mTokenizer->setEncodingDeclarationHandler(this);
  if (aMode == NORMAL) {
    mTokenizer->setProcessingInstructionsEnabled(
        mozilla::StaticPrefs::dom_template_for_enabled());
  }

  if (aMode == VIEW_SOURCE_HTML || aMode == VIEW_SOURCE_XML) {
    nsHtml5Highlighter* highlighter =
        new nsHtml5Highlighter(mExecutor->GetStage());
    mTokenizer->EnableViewSource(highlighter);    // takes ownership
    mTreeBuilder->EnableViewSource(highlighter);  // doesn't own
  }

  // There's a zeroing operator new for everything else
}

nsHtml5StreamParser::~nsHtml5StreamParser() {
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  mTokenizer->end();
#ifdef DEBUG
  {
    mozilla::MutexAutoLock flushTimerLock(mFlushTimerMutex);
    MOZ_ASSERT(!mFlushTimer, "Flush timer was not dropped before dtor!");
  }
  mRequest = nullptr;
  mUnicodeDecoder = nullptr;
  mFirstBuffer = nullptr;
  mExecutor = nullptr;
  mTreeBuilder = nullptr;
  mTokenizer = nullptr;
  mOwner = nullptr;
#endif
}

nsresult nsHtml5StreamParser::GetChannel(nsIChannel** aChannel) {
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  return mRequest ? CallQueryInterface(mRequest, aChannel)
                  : NS_ERROR_NOT_AVAILABLE;
}

std::tuple<NotNull<const Encoding*>, nsCharsetSource>
nsHtml5StreamParser::GuessEncoding(bool aInitial) {
  MOZ_ASSERT(
      mCharsetSource != kCharsetFromFinalUserForcedAutoDetection &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Generic &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8GenericInitialWasASCII &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Content &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8ContentInitialWasASCII &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD &&
      mCharsetSource !=
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLDInitialWasASCII &&
      mCharsetSource != kCharsetFromFinalAutoDetectionFile);
  auto ifHadBeenForced = mDetector->Guess(EmptyCString(), true);
  auto encoding =
      mForceAutoDetection
          ? ifHadBeenForced
          : mDetector->Guess(mTLD, mDecodingLocalFileWithoutTokenizing);
  nsCharsetSource source =
      aInitial
          ? (mForceAutoDetection
                 ? kCharsetFromInitialUserForcedAutoDetection
                 : (mDecodingLocalFileWithoutTokenizing
                        ? kCharsetFromFinalAutoDetectionFile
                        : kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Generic))
          : (mForceAutoDetection
                 ? kCharsetFromFinalUserForcedAutoDetection
                 : (mDecodingLocalFileWithoutTokenizing
                        ? kCharsetFromFinalAutoDetectionFile
                        : kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Generic));
  if (source == kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Generic) {
    if (encoding == ISO_2022_JP_ENCODING) {
      if (EncodingDetector::TldMayAffectGuess(mTLD)) {
        source = kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Content;
      }
    } else if (!mDetectorHasSeenNonAscii) {
      source = kCharsetFromInitialAutoDetectionASCII;  // deliberately Initial
    } else if (ifHadBeenForced == UTF_8_ENCODING) {
      MOZ_ASSERT(mCharsetSource == kCharsetFromInitialAutoDetectionASCII ||
                 mCharsetSource ==
                     kCharsetFromInitialAutoDetectionWouldHaveBeenUTF8 ||
                 mEncoding == ISO_2022_JP_ENCODING);
      source = kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII;
    } else if (encoding != ifHadBeenForced) {
      if (mCharsetSource == kCharsetFromInitialAutoDetectionASCII) {
        source =
            kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLDInitialWasASCII;
      } else {
        source =
            kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD;
      }
    } else if (EncodingDetector::TldMayAffectGuess(mTLD)) {
      if (mCharsetSource == kCharsetFromInitialAutoDetectionASCII) {
        source =
            kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8ContentInitialWasASCII;
      } else {
        source = kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Content;
      }
    } else if (mCharsetSource == kCharsetFromInitialAutoDetectionASCII) {
      source =
          kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8GenericInitialWasASCII;
    }
  } else if (source ==
             kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Generic) {
    if (encoding == ISO_2022_JP_ENCODING) {
      if (EncodingDetector::TldMayAffectGuess(mTLD)) {
        source = kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Content;
      }
    } else if (!mDetectorHasSeenNonAscii) {
      source = kCharsetFromInitialAutoDetectionASCII;
    } else if (ifHadBeenForced == UTF_8_ENCODING) {
      source = kCharsetFromInitialAutoDetectionWouldHaveBeenUTF8;
    } else if (encoding != ifHadBeenForced) {
      source =
          kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD;
    } else if (EncodingDetector::TldMayAffectGuess(mTLD)) {
      source = kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Content;
    }
  }
  return {encoding, source};
}

void nsHtml5StreamParser::FeedDetector(Span<const uint8_t> aBuffer) {
#ifdef DEBUG
  mStartedFeedingDetector = true;
#endif
  MOZ_ASSERT(!mChardetEof);
  mDetectorHasSeenNonAscii = mDetector->Feed(aBuffer, false);
}

void nsHtml5StreamParser::DetectorEof() {
#ifdef DEBUG
  mStartedFeedingDetector = true;
#endif
  if (mChardetEof) {
    return;
  }
  mChardetEof = true;
  mDetectorHasSeenNonAscii = mDetector->Feed(Span<const uint8_t>(), true);
}

void nsHtml5StreamParser::SetViewSourceTitle(nsIURI* aURL) {
  MOZ_ASSERT(NS_IsMainThread());

  BrowsingContext* browsingContext =
      mExecutor->GetDocument()->GetBrowsingContext();
  if (browsingContext && browsingContext->WatchedByDevTools()) {
    mURIToSendToDevtools = aURL;

    nsID uuid;
    nsresult rv = nsID::GenerateUUIDInPlace(uuid);
    if (!NS_FAILED(rv)) {
      char buffer[NSID_LENGTH];
      uuid.ToProvidedString(buffer);
      mUUIDForDevtools = NS_ConvertASCIItoUTF16(buffer);
    }
    mBrowserIdForDevtools = browsingContext->BrowserId();
    mBrowsingContextIDForDevtools = browsingContext->Id();
  }

  if (aURL) {
    nsCOMPtr<nsIURI> temp;
    if (aURL->SchemeIs("view-source")) {
      nsCOMPtr<nsINestedURI> nested = do_QueryInterface(aURL);
      nested->GetInnerURI(getter_AddRefs(temp));
    } else {
      temp = aURL;
    }
    if (temp->SchemeIs("data")) {
      // Avoid showing potentially huge data: URLs. The three last bytes are
      // UTF-8 for an ellipsis.
      mViewSourceTitle.AssignLiteral("data:\xE2\x80\xA6");
    } else {
      nsresult rv = temp->GetSpec(mViewSourceTitle);
      if (NS_FAILED(rv)) {
        mViewSourceTitle.AssignLiteral("\xE2\x80\xA6");
      }
    }
  }
}

nsresult
nsHtml5StreamParser::SetupDecodingAndWriteSniffingBufferAndCurrentSegment(
    Span<const uint8_t> aPrefix, Span<const uint8_t> aFromSegment) {
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  mUnicodeDecoder = mEncoding->NewDecoderWithBOMRemoval();
  nsresult rv = WriteStreamBytes(aPrefix);
  NS_ENSURE_SUCCESS(rv, rv);
  return WriteStreamBytes(aFromSegment);
}

void nsHtml5StreamParser::SetupDecodingFromBom(
    NotNull<const Encoding*> aEncoding) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mEncoding = aEncoding;
  mDecodingLocalFileWithoutTokenizing = false;
  mLookingForMetaCharset = false;
  mBufferingBytes = false;
  mUnicodeDecoder = mEncoding->NewDecoderWithoutBOMHandling();
  mCharsetSource = kCharsetFromByteOrderMark;
  mForceAutoDetection = false;
  mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);
  mBomState = BOM_SNIFFING_OVER;
  if (mMode == VIEW_SOURCE_HTML) {
    mTokenizer->StartViewSourceBodyContents();
  }
}

void nsHtml5StreamParser::SetupDecodingFromUtf16BogoXml(
    NotNull<const Encoding*> aEncoding) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mEncoding = aEncoding;
  mDecodingLocalFileWithoutTokenizing = false;
  mLookingForMetaCharset = false;
  mBufferingBytes = false;
  mUnicodeDecoder = mEncoding->NewDecoderWithoutBOMHandling();
  mCharsetSource = kCharsetFromXmlDeclarationUtf16;
  mForceAutoDetection = false;
  mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);
  mBomState = BOM_SNIFFING_OVER;
  if (mMode == VIEW_SOURCE_HTML) {
    mTokenizer->StartViewSourceBodyContents();
  }
  auto dst = mLastBuffer->TailAsSpan(READ_BUFFER_SIZE);
  dst[0] = '<';
  dst[1] = '?';
  dst[2] = 'x';
  mLastBuffer->AdvanceEnd(3);
  MOZ_ASSERT(!mStartedFeedingDevTools);
  OnNewContent(dst.To(3));
}

size_t nsHtml5StreamParser::LengthOfLtContainingPrefixInSecondBuffer() {
  MOZ_ASSERT(mBufferedBytes.Length() <= 2);
  if (mBufferedBytes.Length() < 2) {
    return 0;
  }
  Buffer<uint8_t>& second = mBufferedBytes[1];
  const uint8_t* elements = second.Elements();
  const uint8_t* lt = (const uint8_t*)memchr(elements, '>', second.Length());
  if (lt) {
    return (lt - elements) + 1;
  }
  return 0;
}

nsresult nsHtml5StreamParser::SniffStreamBytes(Span<const uint8_t> aFromSegment,
                                               bool aEof) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  MOZ_ASSERT_IF(aEof, aFromSegment.IsEmpty());

  if (mCharsetSource >=
          kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII &&
      mCharsetSource <= kCharsetFromFinalUserForcedAutoDetection) {
    if (mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN) {
      mTreeBuilder->MaybeComplainAboutCharset("EncDetectorReloadPlain", true,
                                              0);
    } else {
      mTreeBuilder->MaybeComplainAboutCharset("EncDetectorReload", true, 0);
    }
  }

  // mEncoding and mCharsetSource potentially have come from channel or higher
  // by now. If we find a BOM, SetupDecodingFromBom() will overwrite them.
  // If we don't find a BOM, the previously set values of mEncoding and
  // mCharsetSource are not modified by the BOM sniffing here.
  static uint8_t utf8[] = {0xEF, 0xBB};
  static uint8_t utf16le[] = {0xFF};
  static uint8_t utf16be[] = {0xFE};
  static uint8_t utf16leXml[] = {'<', 0x00, '?', 0x00, 'x'};
  static uint8_t utf16beXml[] = {0x00, '<', 0x00, '?', 0x00};
  // Buffer for replaying past bytes based on state machine state. If
  // writing this from scratch, probably wouldn't do it this way, but
  // let's keep the changes to a minimum.
  const uint8_t* prefix = utf8;
  size_t prefixLength = 0;
  if (aEof && mBomState == BOM_SNIFFING_NOT_STARTED) {
    // Avoid handling aEof in the BOM_SNIFFING_NOT_STARTED state below.
    mBomState = BOM_SNIFFING_OVER;
  }
  for (size_t i = 0;
       (i < aFromSegment.Length() && mBomState != BOM_SNIFFING_OVER) || aEof;
       i++) {
    switch (mBomState) {
      case BOM_SNIFFING_NOT_STARTED:
        MOZ_ASSERT(i == 0, "Bad BOM sniffing state.");
        MOZ_ASSERT(!aEof, "Should have checked for aEof above!");
        switch (aFromSegment[0]) {
          case 0xEF:
            mBomState = SEEN_UTF_8_FIRST_BYTE;
            break;
          case 0xFF:
            mBomState = SEEN_UTF_16_LE_FIRST_BYTE;
            break;
          case 0xFE:
            mBomState = SEEN_UTF_16_BE_FIRST_BYTE;
            break;
          case 0x00:
            if (mCharsetSource < kCharsetFromXmlDeclarationUtf16 &&
                mCharsetSource != kCharsetFromChannel) {
              mBomState = SEEN_UTF_16_BE_XML_FIRST;
            } else {
              mBomState = BOM_SNIFFING_OVER;
            }
            break;
          case '<':
            if (mCharsetSource < kCharsetFromXmlDeclarationUtf16 &&
                mCharsetSource != kCharsetFromChannel) {
              mBomState = SEEN_UTF_16_LE_XML_FIRST;
            } else {
              mBomState = BOM_SNIFFING_OVER;
            }
            break;
          default:
            mBomState = BOM_SNIFFING_OVER;
            break;
        }
        break;
      case SEEN_UTF_16_LE_FIRST_BYTE:
        if (!aEof && aFromSegment[i] == 0xFE) {
          SetupDecodingFromBom(UTF_16LE_ENCODING);
          return WriteStreamBytes(aFromSegment.From(i + 1));
        }
        prefix = utf16le;
        prefixLength = 1 - i;
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_16_BE_FIRST_BYTE:
        if (!aEof && aFromSegment[i] == 0xFF) {
          SetupDecodingFromBom(UTF_16BE_ENCODING);
          return WriteStreamBytes(aFromSegment.From(i + 1));
        }
        prefix = utf16be;
        prefixLength = 1 - i;
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_8_FIRST_BYTE:
        if (!aEof && aFromSegment[i] == 0xBB) {
          mBomState = SEEN_UTF_8_SECOND_BYTE;
        } else {
          prefixLength = 1 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_8_SECOND_BYTE:
        if (!aEof && aFromSegment[i] == 0xBF) {
          SetupDecodingFromBom(UTF_8_ENCODING);
          return WriteStreamBytes(aFromSegment.From(i + 1));
        }
        prefixLength = 2 - i;
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_16_BE_XML_FIRST:
        if (!aEof && aFromSegment[i] == '<') {
          mBomState = SEEN_UTF_16_BE_XML_SECOND;
        } else {
          prefix = utf16beXml;
          prefixLength = 1 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_BE_XML_SECOND:
        if (!aEof && aFromSegment[i] == 0x00) {
          mBomState = SEEN_UTF_16_BE_XML_THIRD;
        } else {
          prefix = utf16beXml;
          prefixLength = 2 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_BE_XML_THIRD:
        if (!aEof && aFromSegment[i] == '?') {
          mBomState = SEEN_UTF_16_BE_XML_FOURTH;
        } else {
          prefix = utf16beXml;
          prefixLength = 3 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_BE_XML_FOURTH:
        if (!aEof && aFromSegment[i] == 0x00) {
          mBomState = SEEN_UTF_16_BE_XML_FIFTH;
        } else {
          prefix = utf16beXml;
          prefixLength = 4 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_BE_XML_FIFTH:
        if (!aEof && aFromSegment[i] == 'x') {
          SetupDecodingFromUtf16BogoXml(UTF_16BE_ENCODING);
          return WriteStreamBytes(aFromSegment.From(i + 1));
        }
        prefix = utf16beXml;
        prefixLength = 5 - i;
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_16_LE_XML_FIRST:
        if (!aEof && aFromSegment[i] == 0x00) {
          mBomState = SEEN_UTF_16_LE_XML_SECOND;
        } else {
          if (!aEof && aFromSegment[i] == '?' &&
              !(mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN)) {
            mStartsWithLtQuestion = true;
          }
          prefix = utf16leXml;
          prefixLength = 1 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_LE_XML_SECOND:
        if (!aEof && aFromSegment[i] == '?') {
          mBomState = SEEN_UTF_16_LE_XML_THIRD;
        } else {
          prefix = utf16leXml;
          prefixLength = 2 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_LE_XML_THIRD:
        if (!aEof && aFromSegment[i] == 0x00) {
          mBomState = SEEN_UTF_16_LE_XML_FOURTH;
        } else {
          prefix = utf16leXml;
          prefixLength = 3 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_LE_XML_FOURTH:
        if (!aEof && aFromSegment[i] == 'x') {
          mBomState = SEEN_UTF_16_LE_XML_FIFTH;
        } else {
          prefix = utf16leXml;
          prefixLength = 4 - i;
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_16_LE_XML_FIFTH:
        if (!aEof && aFromSegment[i] == 0x00) {
          SetupDecodingFromUtf16BogoXml(UTF_16LE_ENCODING);
          return WriteStreamBytes(aFromSegment.From(i + 1));
        }
        prefix = utf16leXml;
        prefixLength = 5 - i;
        mBomState = BOM_SNIFFING_OVER;
        break;
      default:
        mBomState = BOM_SNIFFING_OVER;
        break;
    }
    if (aEof) {
      break;
    }
  }
  // if we get here, there either was no BOM or the BOM sniffing isn't complete
  // yet

  MOZ_ASSERT(mCharsetSource != kCharsetFromByteOrderMark,
             "Should not come here if BOM was found.");
  MOZ_ASSERT(mCharsetSource != kCharsetFromXmlDeclarationUtf16,
             "Should not come here if UTF-16 bogo-XML declaration was found.");
  MOZ_ASSERT(mCharsetSource != kCharsetFromOtherComponent,
             "kCharsetFromOtherComponent is for XSLT.");

  if (mBomState == BOM_SNIFFING_OVER) {
    if (mMode == VIEW_SOURCE_XML && mStartsWithLtQuestion &&
        mCharsetSource < kCharsetFromChannel) {
      // Sniff for XML declaration only.
      MOZ_ASSERT(!mLookingForXmlDeclarationForXmlViewSource);
      MOZ_ASSERT(!aEof);
      MOZ_ASSERT(!mLookingForMetaCharset);
      MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing);
      // Maybe we've already buffered a '>'.
      MOZ_ASSERT(!mBufferedBytes.IsEmpty(),
                 "How did at least <? not get buffered?");
      Buffer<uint8_t>& first = mBufferedBytes[0];
      const Encoding* encoding =
          xmldecl_parse(first.Elements(), first.Length());
      if (encoding) {
        mEncoding = WrapNotNull(encoding);
        mCharsetSource = kCharsetFromXmlDeclaration;
      } else if (memchr(first.Elements(), '>', first.Length())) {
        // There was a '>', but an encoding still wasn't found.
        ;  // fall through to commit to the UTF-8 default.
      } else if (size_t lengthOfPrefix =
                     LengthOfLtContainingPrefixInSecondBuffer()) {
        // This can only happen if the first buffer was a lone '<', because
        // we come here upon seeing the second byte '?' if the first two bytes
        // were "<?". That is, the only way how we aren't dealing with the first
        // buffer is if the first buffer only contained a single '<' and we are
        // dealing with the second buffer that starts with '?'.
        MOZ_ASSERT(first.Length() == 1);
        MOZ_ASSERT(mBufferedBytes[1][0] == '?');
        // Our scanner for XML declaration-like syntax wants to see a contiguous
        // buffer, so let's linearize the data. (Ideally, the XML declaration
        // scanner would be incremental, but this is the rare path anyway.)
        Vector<uint8_t> contiguous;
        if (!contiguous.append(first.Elements(), first.Length())) {
          MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
          return NS_ERROR_OUT_OF_MEMORY;
        }
        if (!contiguous.append(mBufferedBytes[1].Elements(), lengthOfPrefix)) {
          MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
          return NS_ERROR_OUT_OF_MEMORY;
        }
        encoding = xmldecl_parse(contiguous.begin(), contiguous.length());
        if (encoding) {
          mEncoding = WrapNotNull(encoding);
          mCharsetSource = kCharsetFromXmlDeclaration;
        }
        // else no XML decl, commit to the UTF-8 default.
      } else {
        MOZ_ASSERT(mBufferingBytes);
        mLookingForXmlDeclarationForXmlViewSource = true;
        return NS_OK;
      }
    } else if (mMode != VIEW_SOURCE_XML &&
               (mForceAutoDetection || mCharsetSource < kCharsetFromChannel)) {
      // In order to use the buffering logic for meta with mForceAutoDetection,
      // we set mLookingForMetaCharset but still actually potentially ignore the
      // meta.
      mFirstBufferOfMetaScan = mFirstBuffer;
      MOZ_ASSERT(mLookingForMetaCharset);

      if (mMode == VIEW_SOURCE_HTML) {
        auto r = mTokenizer->FlushViewSource();
        if (r.isErr()) {
          return r.unwrapErr();
        }
      }
      auto r = mTreeBuilder->Flush();
      if (r.isErr()) {
        return r.unwrapErr();
      }
      // Encoding committer flushes the ops on the main thread.

      mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
      nsHtml5Speculation* speculation = new nsHtml5Speculation(
          mFirstBuffer, mFirstBuffer->getStart(), mTokenizer->getLineNumber(),
          mTokenizer->getColumnNumber(), mTreeBuilder->newSnapshot());
      MOZ_ASSERT(!mFlushTimerArmed, "How did we end up arming the timer?");
      if (mMode == VIEW_SOURCE_HTML) {
        mTokenizer->SetViewSourceOpSink(speculation);
        mTokenizer->StartViewSourceBodyContents();
      } else {
        MOZ_ASSERT(mMode != VIEW_SOURCE_XML);
        mTreeBuilder->SetOpSink(speculation);
      }
      mSpeculations.AppendElement(speculation);  // adopts the pointer
      mSpeculating = true;
    } else {
      mLookingForMetaCharset = false;
      mBufferingBytes = false;
      mDecodingLocalFileWithoutTokenizing = false;
      if (mMode == VIEW_SOURCE_HTML) {
        mTokenizer->StartViewSourceBodyContents();
      }
    }
    mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);
    return SetupDecodingAndWriteSniffingBufferAndCurrentSegment(
        Span(prefix, prefixLength), aFromSegment);
  }

  return NS_OK;
}

class AddContentRunnable : public Runnable {
 public:
  AddContentRunnable(const nsAString& aParserID, uint64_t aBrowserId,
                     uint64_t aBrowsingContextID, nsIURI* aURI,
                     Span<const char16_t> aData, bool aComplete)
      : Runnable("AddContent") {
    nsAutoCString spec;
    aURI->GetSpec(spec);
    mData.mUri.Construct(NS_ConvertUTF8toUTF16(spec));
    mData.mParserID.Construct(aParserID);
    mData.mBrowserId.Construct(aBrowserId);
    mData.mBrowsingContextID.Construct(aBrowsingContextID);
    mData.mContents.Construct(aData.Elements(), aData.Length());
    mData.mComplete.Construct(aComplete);
  }

  NS_IMETHOD Run() override {
    nsAutoString json;
    if (!mData.ToJSON(json)) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    if (obsService) {
      obsService->NotifyObservers(nullptr, "devtools-html-content",
                                  PromiseFlatString(json).get());
    }

    return NS_OK;
  }

  HTMLContent mData;
};

inline void nsHtml5StreamParser::OnNewContent(Span<const char16_t> aData) {
#ifdef DEBUG
  mStartedFeedingDevTools = true;
#endif
  if (mURIToSendToDevtools) {
    if (aData.IsEmpty()) {
      // Optimize out the runnable.
      return;
    }
    NS_DispatchToMainThread(new AddContentRunnable(
        mUUIDForDevtools, mBrowserIdForDevtools, mBrowsingContextIDForDevtools,
        mURIToSendToDevtools, aData,
        /* aComplete */ false));
  }
}

inline void nsHtml5StreamParser::OnContentComplete() {
#ifdef DEBUG
  mStartedFeedingDevTools = true;
#endif
  if (mURIToSendToDevtools) {
    NS_DispatchToMainThread(new AddContentRunnable(
        mUUIDForDevtools, mBrowserIdForDevtools, mBrowsingContextIDForDevtools,
        mURIToSendToDevtools, Span<const char16_t>(),
        /* aComplete */ true));
    mURIToSendToDevtools = nullptr;
    mBrowserIdForDevtools = 0;
    mBrowsingContextIDForDevtools = 0;
  }
}

nsresult nsHtml5StreamParser::WriteStreamBytes(
    Span<const uint8_t> aFromSegment) {
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  mTokenizerMutex.AssertCurrentThreadOwns();
  // mLastBuffer should always point to a buffer of the size
  // READ_BUFFER_SIZE.
  if (!mLastBuffer) {
    NS_WARNING("mLastBuffer should not be null!");
    MarkAsBroken(NS_ERROR_NULL_POINTER);
    return NS_ERROR_NULL_POINTER;
  }
  size_t totalRead = 0;
  auto src = aFromSegment;
  for (;;) {
    auto dst = mLastBuffer->TailAsSpan(READ_BUFFER_SIZE);
    auto [result, read, written, hadErrors] =
        mUnicodeDecoder->DecodeToUTF16(src, dst, false);
    if (!(mLookingForMetaCharset || mDecodingLocalFileWithoutTokenizing)) {
      OnNewContent(dst.To(written));
    }
    if (hadErrors && !mHasHadErrors) {
      mHasHadErrors = true;
      if (mEncoding == UTF_8_ENCODING) {
        mTreeBuilder->TryToEnableEncodingMenu();
      }
    }
    src = src.From(read);
    totalRead += read;
    mLastBuffer->AdvanceEnd(written);
    if (result == kOutputFull) {
      RefPtr<nsHtml5OwningUTF16Buffer> newBuf =
          nsHtml5OwningUTF16Buffer::FalliblyCreate(READ_BUFFER_SIZE);
      if (!newBuf) {
        MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return NS_ERROR_OUT_OF_MEMORY;
      }
      mLastBuffer = (mLastBuffer->next = std::move(newBuf));
    } else {
      MOZ_ASSERT(totalRead == aFromSegment.Length(),
                 "The Unicode decoder consumed the wrong number of bytes.");
      (void)totalRead;
      if (!mLookingForMetaCharset && mDecodingLocalFileWithoutTokenizing &&
          mNumBytesBuffered == LOCAL_FILE_UTF_8_BUFFER_SIZE) {
        MOZ_ASSERT(!mStartedFeedingDetector);
        for (auto&& buffer : mBufferedBytes) {
          FeedDetector(buffer);
        }
        // If the file is exactly LOCAL_FILE_UTF_8_BUFFER_SIZE bytes long
        // we end up not considering the EOF. That's not fatal, since we
        // don't consider the EOF if the file is
        // LOCAL_FILE_UTF_8_BUFFER_SIZE + 1 bytes long.
        auto [encoding, source] = GuessEncoding(true);
        mCharsetSource = source;
        if (encoding != mEncoding) {
          mEncoding = encoding;
          nsresult rv = ReDecodeLocalFile();
          if (NS_FAILED(rv)) {
            return rv;
          }
        } else {
          MOZ_ASSERT(mEncoding == UTF_8_ENCODING);
          nsresult rv = CommitLocalFileToEncoding();
          if (NS_FAILED(rv)) {
            return rv;
          }
        }
      }
      return NS_OK;
    }
  }
}

[[nodiscard]] nsresult nsHtml5StreamParser::ReDecodeLocalFile() {
  MOZ_ASSERT(mDecodingLocalFileWithoutTokenizing && !mLookingForMetaCharset);
  MOZ_ASSERT(mFirstBufferOfMetaScan);
  MOZ_ASSERT(mCharsetSource == kCharsetFromFinalAutoDetectionFile ||
             (mForceAutoDetection &&
              mCharsetSource == kCharsetFromInitialUserForcedAutoDetection));

  DiscardMetaSpeculation();

  MOZ_ASSERT(mEncoding != UTF_8_ENCODING);

  mDecodingLocalFileWithoutTokenizing = false;

  mEncoding->NewDecoderWithBOMRemovalInto(*mUnicodeDecoder);
  mHasHadErrors = false;

  // Throw away previous decoded data
  mLastBuffer = mFirstBuffer;
  mLastBuffer->next = nullptr;
  mLastBuffer->setStart(0);
  mLastBuffer->setEnd(0);

  mBufferingBytes = false;
  mForceAutoDetection = false;  // To stop feeding the detector
  mFirstBufferOfMetaScan = nullptr;

  mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, true);

  // Decode again
  for (auto&& buffer : mBufferedBytes) {
    DoDataAvailable(buffer);
  }

  if (mMode == VIEW_SOURCE_HTML) {
    auto r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      return r.unwrapErr();
    }
  }
  auto r = mTreeBuilder->Flush();
  if (r.isErr()) {
    return r.unwrapErr();
  }
  return NS_OK;
}

[[nodiscard]] nsresult nsHtml5StreamParser::CommitLocalFileToEncoding() {
  MOZ_ASSERT(mDecodingLocalFileWithoutTokenizing && !mLookingForMetaCharset);
  MOZ_ASSERT(mFirstBufferOfMetaScan);
  mDecodingLocalFileWithoutTokenizing = false;
  MOZ_ASSERT(mCharsetSource == kCharsetFromFinalAutoDetectionFile ||
             (mForceAutoDetection &&
              mCharsetSource == kCharsetFromInitialUserForcedAutoDetection));
  MOZ_ASSERT(mEncoding == UTF_8_ENCODING);

  MOZ_ASSERT(!mStartedFeedingDevTools);
  if (mURIToSendToDevtools) {
    nsHtml5OwningUTF16Buffer* buffer = mFirstBufferOfMetaScan;
    while (buffer) {
      Span<const char16_t> data(buffer->getBuffer() + buffer->getStart(),
                                buffer->getLength());
      OnNewContent(data);
      buffer = buffer->next;
    }
  }

  mFirstBufferOfMetaScan = nullptr;

  mBufferingBytes = false;
  mForceAutoDetection = false;  // To stop feeding the detector
  mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, true);
  if (mMode == VIEW_SOURCE_HTML) {
    auto r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      return r.unwrapErr();
    }
  }
  auto r = mTreeBuilder->Flush();
  if (r.isErr()) {
    return r.unwrapErr();
  }
  return NS_OK;
}

class MaybeRunCollector : public Runnable {
 public:
  explicit MaybeRunCollector(nsIDocShell* aDocShell)
      : Runnable("MaybeRunCollector"), mDocShell(aDocShell) {}

  NS_IMETHOD Run() override {
    nsJSContext::MaybeRunNextCollectorSlice(mDocShell,
                                            JS::GCReason::HTML_PARSER);
    return NS_OK;
  }

  nsCOMPtr<nsIDocShell> mDocShell;
};

nsresult nsHtml5StreamParser::OnStartRequest(nsIRequest* aRequest) {
  MOZ_RELEASE_ASSERT(STREAM_NOT_STARTED == mStreamState,
                     "Got OnStartRequest when the stream had already started.");
  MOZ_ASSERT(
      !mExecutor->HasStarted(),
      "Got OnStartRequest at the wrong stage in the executor life cycle.");
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");

  // To avoid the cost of instantiating the detector when it's not needed,
  // let's instantiate only if we make it out of this method with the
  // intent to use it.
  auto detectorCreator = MakeScopeExit([&] {
    if ((mForceAutoDetection || mCharsetSource < kCharsetFromParentFrame) ||
        !(mMode == LOAD_AS_DATA || mMode == VIEW_SOURCE_XML)) {
      mDetector = mozilla::EncodingDetector::Create(mMode == PLAIN_TEXT ||
                                                    mMode == VIEW_SOURCE_PLAIN);
    }
  });

  mRequest = aRequest;

  mStreamState = STREAM_BEING_READ;

  // For View Source, the parser should run with scripts "enabled" if a normal
  // load would have scripts enabled.
  bool scriptingEnabled =
      mMode == LOAD_AS_DATA ? false : mExecutor->IsScriptEnabled();
  mOwner->StartTokenizer(scriptingEnabled);

  MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing);
  bool isSrcdoc = false;
  nsCOMPtr<nsIChannel> channel;
  nsresult rv = GetChannel(getter_AddRefs(channel));
  if (NS_SUCCEEDED(rv)) {
    isSrcdoc = NS_IsSrcdocChannel(channel);
    if (!isSrcdoc && mCharsetSource <= kCharsetFromFallback) {
      nsCOMPtr<nsIURI> originalURI;
      rv = channel->GetOriginalURI(getter_AddRefs(originalURI));
      if (NS_SUCCEEDED(rv)) {
        if (originalURI->SchemeIs("resource")) {
          mCharsetSource = kCharsetFromBuiltIn;
          mEncoding = UTF_8_ENCODING;
          mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);
        } else {
          nsCOMPtr<nsIURI> currentURI;
          rv = channel->GetURI(getter_AddRefs(currentURI));
          if (NS_SUCCEEDED(rv)) {
            nsCOMPtr<nsIURI> innermost = NS_GetInnermostURI(currentURI);
            if (innermost->SchemeIs("file")) {
              MOZ_ASSERT(mEncoding == UTF_8_ENCODING);
              if (!(mMode == LOAD_AS_DATA || mMode == VIEW_SOURCE_XML)) {
                mDecodingLocalFileWithoutTokenizing = true;
              }
            } else {
              nsAutoCString host;
              innermost->GetAsciiHost(host);
              if (!host.IsEmpty()) {
                // First let's see if the host is DNS-absolute and ends with a
                // dot and get rid of that one.
                if (host.Last() == '.') {
                  host.SetLength(host.Length() - 1);
                }
                int32_t index = host.RFindChar('.');
                if (index != kNotFound) {
                  // We tolerate an IPv4 component as generic "TLD", so don't
                  // bother checking.
                  ToLowerCase(
                      Substring(host, index + 1, host.Length() - (index + 1)),
                      mTLD);
                }
              }
            }
          }
        }
      }
    }
  }
  mTreeBuilder->setIsSrcdocDocument(isSrcdoc);
  mTreeBuilder->setScriptingEnabled(scriptingEnabled);
  mTreeBuilder->SetPreventScriptExecution(
      !((mMode == NORMAL) && scriptingEnabled));
  mTreeBuilder->setAllowDeclarativeShadowRoots(
      mExecutor->GetDocument()->AllowsDeclarativeShadowRoots());
  mTokenizer->start();
  mExecutor->Start();
  mExecutor->StartReadingFromStage();

  if (mMode == PLAIN_TEXT) {
    mTreeBuilder->StartPlainText();
    mTokenizer->StartPlainText();
    MOZ_ASSERT(
        mTemplatePushedOrHeadPopped);  // Needed to force 1024-byte sniffing
    // Flush the ops to put them where ContinueAfterScriptsOrEncodingCommitment
    // can find them.
    auto r = mTreeBuilder->Flush();
    if (r.isErr()) {
      return mExecutor->MarkAsBroken(r.unwrapErr());
    }
  } else if (mMode == VIEW_SOURCE_PLAIN) {
    nsAutoString viewSourceTitle;
    CopyUTF8toUTF16(mViewSourceTitle, viewSourceTitle);
    mTreeBuilder->EnsureBufferSpace(viewSourceTitle.Length());
    mTreeBuilder->StartPlainTextViewSource(viewSourceTitle);
    mTokenizer->StartPlainText();
    MOZ_ASSERT(
        mTemplatePushedOrHeadPopped);  // Needed to force 1024-byte sniffing
    // Flush the ops to put them where ContinueAfterScriptsOrEncodingCommitment
    // can find them.
    auto r = mTreeBuilder->Flush();
    if (r.isErr()) {
      return mExecutor->MarkAsBroken(r.unwrapErr());
    }
  } else if (mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML) {
    // Generate and flush the View Source document up to and including the
    // pre element start.
    mTokenizer->StartViewSource(NS_ConvertUTF8toUTF16(mViewSourceTitle));
    if (mMode == VIEW_SOURCE_XML) {
      mTokenizer->StartViewSourceBodyContents();
    }
    // Flush the ops to put them where ContinueAfterScriptsOrEncodingCommitment
    // can find them.
    auto r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      return mExecutor->MarkAsBroken(r.unwrapErr());
    }
  }

  /*
   * If you move the following line, be very careful not to cause
   * WillBuildModel to be called before the document has had its
   * script global object set.
   */
  rv = mExecutor->WillBuildModel();
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsHtml5OwningUTF16Buffer> newBuf =
      nsHtml5OwningUTF16Buffer::FalliblyCreate(READ_BUFFER_SIZE);
  if (!newBuf) {
    // marks this stream parser as terminated,
    // which prevents entry to code paths that
    // would use mFirstBuffer or mLastBuffer.
    return mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
  }
  MOZ_ASSERT(!mFirstBuffer, "How come we have the first buffer set?");
  MOZ_ASSERT(!mLastBuffer, "How come we have the last buffer set?");
  mFirstBuffer = mLastBuffer = newBuf;

  rv = NS_OK;

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(mRequest, &rv));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString method;
    (void)httpChannel->GetRequestMethod(method);
    // XXX does Necko have a way to renavigate POST, etc. without hitting
    // the network?
    if (!method.EqualsLiteral("GET")) {
      // This is the old Gecko behavior but the HTML5 spec disagrees.
      // Don't reparse on POST.
      mReparseForbidden = true;
    }
  }

  // Attempt to retarget delivery of data (via OnDataAvailable) to the parser
  // thread, rather than through the main thread.
  nsCOMPtr<nsIThreadRetargetableRequest> threadRetargetableRequest =
      do_QueryInterface(mRequest, &rv);
  if (threadRetargetableRequest) {
    rv = threadRetargetableRequest->RetargetDeliveryTo(mEventTarget);
    if (NS_SUCCEEDED(rv)) {
      // Parser thread should be now ready to get data from necko and parse it
      // and main thread might have a chance to process a collector slice.
      // We need to do this asynchronously so that necko may continue processing
      // the request.
      nsCOMPtr<nsIRunnable> runnable =
          new MaybeRunCollector(mExecutor->GetDocument()->GetDocShell());
      mozilla::SchedulerGroup::Dispatch(runnable.forget());
    }
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to retarget HTML data delivery to the parser thread.");
  }

  if (mCharsetSource == kCharsetFromParentFrame) {
    // Remember this for error reporting.
    mInitialEncodingWasFromParentFrame = true;
    MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing);
  }

  if (mForceAutoDetection || mCharsetSource < kCharsetFromChannel) {
    mBufferingBytes = true;
    if (mMode != VIEW_SOURCE_XML) {
      // We need to set mLookingForMetaCharset to true here in case the first
      // buffer to arrive is larger than 1024. We need the code that splits
      // the buffers at 1024 bytes to work even in that case.
      mLookingForMetaCharset = true;
    }
  }

  if (mCharsetSource < kCharsetFromUtf8OnlyMime) {
    // we aren't ready to commit to an encoding yet
    // leave converter uninstantiated for now
    return NS_OK;
  }

  MOZ_ASSERT(!(mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML));

  MOZ_ASSERT(mEncoding == UTF_8_ENCODING,
             "How come UTF-8-only MIME type didn't set encoding to UTF-8?");

  // We are loading JSON/WebVTT/etc. into a browsing context.
  // There's no need to remove the BOM manually here, because
  // the UTF-8 decoder removes it.
  mReparseForbidden = true;
  mForceAutoDetection = false;

  // Instantiate the converter here to avoid BOM sniffing.
  mDecodingLocalFileWithoutTokenizing = false;
  mUnicodeDecoder = mEncoding->NewDecoderWithBOMRemoval();
  return NS_OK;
}

void nsHtml5StreamParser::DoStopRequest() {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  MOZ_RELEASE_ASSERT(STREAM_BEING_READ == mStreamState,
                     "Stream ended without being open.");
  mTokenizerMutex.AssertCurrentThreadOwns();

  auto guard = MakeScopeExit([&] { OnContentComplete(); });

  if (IsTerminated()) {
    return;
  }

  if (MOZ_UNLIKELY(mLookingForXmlDeclarationForXmlViewSource)) {
    mLookingForXmlDeclarationForXmlViewSource = false;
    mBufferingBytes = false;
    mUnicodeDecoder = mEncoding->NewDecoderWithoutBOMHandling();
    mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);

    for (auto&& buffer : mBufferedBytes) {
      nsresult rv = WriteStreamBytes(buffer);
      if (NS_FAILED(rv)) {
        MarkAsBroken(rv);
        return;
      }
    }
  } else if (!mUnicodeDecoder) {
    nsresult rv;
    if (NS_FAILED(rv = SniffStreamBytes(Span<const uint8_t>(), true))) {
      MarkAsBroken(rv);
      return;
    }
  }

  MOZ_ASSERT(mUnicodeDecoder,
             "Should have a decoder after finalizing sniffing.");

  // mLastBuffer should always point to a buffer of the size
  // READ_BUFFER_SIZE.
  if (!mLastBuffer) {
    NS_WARNING("mLastBuffer should not be null!");
    MarkAsBroken(NS_ERROR_NULL_POINTER);
    return;
  }

  Span<uint8_t> src;  // empty span
  for (;;) {
    auto dst = mLastBuffer->TailAsSpan(READ_BUFFER_SIZE);
    uint32_t result;
    size_t read;
    size_t written;
    bool hadErrors;
    // Do not use structured binding lest deal with [-Werror=unused-variable]
    std::tie(result, read, written, hadErrors) =
        mUnicodeDecoder->DecodeToUTF16(src, dst, true);
    if (!(mLookingForMetaCharset || mDecodingLocalFileWithoutTokenizing)) {
      OnNewContent(dst.To(written));
    }
    if (hadErrors) {
      mHasHadErrors = true;
    }
    MOZ_ASSERT(read == 0, "How come an empty span was read form?");
    mLastBuffer->AdvanceEnd(written);
    if (result == kOutputFull) {
      RefPtr<nsHtml5OwningUTF16Buffer> newBuf =
          nsHtml5OwningUTF16Buffer::FalliblyCreate(READ_BUFFER_SIZE);
      if (!newBuf) {
        MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
      mLastBuffer = (mLastBuffer->next = std::move(newBuf));
    } else {
      if (!mLookingForMetaCharset && mDecodingLocalFileWithoutTokenizing) {
        MOZ_ASSERT(mNumBytesBuffered < LOCAL_FILE_UTF_8_BUFFER_SIZE);
        MOZ_ASSERT(!mStartedFeedingDetector);
        for (auto&& buffer : mBufferedBytes) {
          FeedDetector(buffer);
        }
        MOZ_ASSERT(!mChardetEof);
        DetectorEof();
        auto [encoding, source] = GuessEncoding(true);
        mCharsetSource = source;
        if (encoding != mEncoding) {
          mEncoding = encoding;
          nsresult rv = ReDecodeLocalFile();
          if (NS_FAILED(rv)) {
            MarkAsBroken(rv);
            return;
          }
          DoStopRequest();
          return;
        }
        MOZ_ASSERT(mEncoding == UTF_8_ENCODING);
        nsresult rv = CommitLocalFileToEncoding();
        if (NS_FAILED(rv)) {
          MarkAsBroken(rv);
          return;
        }
      }
      break;
    }
  }

  mStreamState = STREAM_ENDED;

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  ParseAvailableData();
}

class nsHtml5RequestStopper : public Runnable {
 private:
  nsHtml5StreamParserPtr mStreamParser;

 public:
  explicit nsHtml5RequestStopper(nsHtml5StreamParser* aStreamParser)
      : Runnable("nsHtml5RequestStopper"), mStreamParser(aStreamParser) {}
  NS_IMETHOD Run() override {
    mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
    mStreamParser->DoStopRequest();
    mStreamParser->PostLoadFlusher();
    return NS_OK;
  }
};

nsresult nsHtml5StreamParser::OnStopRequest(
    nsIRequest* aRequest, nsresult status,
    const mozilla::ReentrantMonitorAutoEnter& aProofOfLock) {
  MOZ_ASSERT_IF(aRequest, mRequest == aRequest);
  if (mOnStopCalled) {
    // OnStopRequest already executed (probably OMT).
    MOZ_ASSERT(NS_IsMainThread(), "Expected to run on main thread");
  } else {
    mOnStopCalled = true;

    if (MOZ_UNLIKELY(NS_IsMainThread())) {
      nsCOMPtr<nsIRunnable> stopper = new nsHtml5RequestStopper(this);
      if (NS_FAILED(
              mEventTarget->Dispatch(stopper, nsIThread::DISPATCH_NORMAL))) {
        NS_WARNING("Dispatching StopRequest event failed.");
      }
    } else {
      if (StaticPrefs::network_send_OnDataFinished_html5parser()) {
        MOZ_ASSERT(IsParserThread(), "Wrong thread!");
        mozilla::MutexAutoLock autoLock(mTokenizerMutex);
        DoStopRequest();
        PostLoadFlusher();
      } else {
        // Let the MainThread event handle this, even though it will just
        // send it back to this thread, so we can accurately judge the impact
        // of this change.   This should eventually be removed
        mOnStopCalled = false;
        // don't record any telemetry for this
        return NS_OK;
      }
    }
  }
  return NS_OK;
}

void nsHtml5StreamParser::DoDataAvailableBuffer(
    mozilla::Buffer<uint8_t>&& aBuffer) {
  if (MOZ_UNLIKELY(!mBufferingBytes)) {
    DoDataAvailable(aBuffer);
    return;
  }
  if (MOZ_UNLIKELY(mLookingForXmlDeclarationForXmlViewSource)) {
    const uint8_t* elements = aBuffer.Elements();
    size_t length = aBuffer.Length();
    const uint8_t* lt = (const uint8_t*)memchr(elements, '>', length);
    if (!lt) {
      mBufferedBytes.AppendElement(std::move(aBuffer));
      return;
    }

    // We found an '>'. Now there either is or isn't an XML decl.
    length = (lt - elements) + 1;
    Vector<uint8_t> contiguous;
    for (auto&& buffer : mBufferedBytes) {
      if (!contiguous.append(buffer.Elements(), buffer.Length())) {
        MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
    }
    if (!contiguous.append(elements, length)) {
      MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    const Encoding* encoding =
        xmldecl_parse(contiguous.begin(), contiguous.length());
    if (encoding) {
      mEncoding = WrapNotNull(encoding);
      mCharsetSource = kCharsetFromXmlDeclaration;
    }

    mLookingForXmlDeclarationForXmlViewSource = false;
    mBufferingBytes = false;
    mUnicodeDecoder = mEncoding->NewDecoderWithoutBOMHandling();
    mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, false);

    for (auto&& buffer : mBufferedBytes) {
      DoDataAvailable(buffer);
    }
    DoDataAvailable(aBuffer);
    mBufferedBytes.Clear();
    return;
  }
  CheckedInt<size_t> bufferedPlusLength(aBuffer.Length());
  bufferedPlusLength += mNumBytesBuffered;
  if (!bufferedPlusLength.isValid()) {
    MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  // Ensure that WriteStreamBytes() sees buffers ending
  // exactly at the two special boundaries.
  bool metaBoundaryWithinBuffer =
      mLookingForMetaCharset &&
      mNumBytesBuffered < UNCONDITIONAL_META_SCAN_BOUNDARY &&
      bufferedPlusLength.value() > UNCONDITIONAL_META_SCAN_BOUNDARY;
  bool localFileLimitWithinBuffer =
      mDecodingLocalFileWithoutTokenizing &&
      mNumBytesBuffered < LOCAL_FILE_UTF_8_BUFFER_SIZE &&
      bufferedPlusLength.value() > LOCAL_FILE_UTF_8_BUFFER_SIZE;
  if (!metaBoundaryWithinBuffer && !localFileLimitWithinBuffer) {
    // Truncation OK, because we just checked the range.
    mNumBytesBuffered = bufferedPlusLength.value();
    mBufferedBytes.AppendElement(std::move(aBuffer));
    DoDataAvailable(mBufferedBytes.LastElement());
  } else {
    MOZ_RELEASE_ASSERT(
        !(metaBoundaryWithinBuffer && localFileLimitWithinBuffer),
        "How can Necko give us a buffer this large?");
    size_t boundary = metaBoundaryWithinBuffer
                          ? UNCONDITIONAL_META_SCAN_BOUNDARY
                          : LOCAL_FILE_UTF_8_BUFFER_SIZE;
    // Truncation OK, because the constant is small enough.
    size_t overBoundary = bufferedPlusLength.value() - boundary;
    MOZ_RELEASE_ASSERT(overBoundary < aBuffer.Length());
    size_t untilBoundary = aBuffer.Length() - overBoundary;
    auto span = aBuffer.AsSpan();
    auto head = span.To(untilBoundary);
    auto tail = span.From(untilBoundary);
    MOZ_RELEASE_ASSERT(mNumBytesBuffered + untilBoundary == boundary);
    // The following copies may end up being useless, but optimizing
    // them away would add complexity.
    Maybe<Buffer<uint8_t>> maybeHead = Buffer<uint8_t>::CopyFrom(head);
    if (maybeHead.isNothing()) {
      MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    mNumBytesBuffered = boundary;
    mBufferedBytes.AppendElement(std::move(*maybeHead));
    DoDataAvailable(mBufferedBytes.LastElement());
    // Re-decode may have happened here.

    Maybe<Buffer<uint8_t>> maybeTail = Buffer<uint8_t>::CopyFrom(tail);
    if (maybeTail.isNothing()) {
      MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    mNumBytesBuffered += tail.Length();
    mBufferedBytes.AppendElement(std::move(*maybeTail));
    DoDataAvailable(mBufferedBytes.LastElement());
  }
  // Do this clean-up here to avoid use-after-free when
  // DoDataAvailable is passed a span pointing into an
  // element of mBufferedBytes.
  if (!mBufferingBytes) {
    mBufferedBytes.Clear();
  }
}

void nsHtml5StreamParser::DoDataAvailable(Span<const uint8_t> aBuffer) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  MOZ_RELEASE_ASSERT(STREAM_BEING_READ == mStreamState,
                     "DoDataAvailable called when stream not open.");
  mTokenizerMutex.AssertCurrentThreadOwns();

  if (IsTerminated()) {
    return;
  }

  nsresult rv;
  if (HasDecoder()) {
    if ((mForceAutoDetection || mCharsetSource < kCharsetFromParentFrame) &&
        !mBufferingBytes && !mReparseForbidden &&
        !(mMode == LOAD_AS_DATA || mMode == VIEW_SOURCE_XML)) {
      MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing,
                 "How is mBufferingBytes false if "
                 "mDecodingLocalFileWithoutTokenizing is true?");
      FeedDetector(aBuffer);
    }
    rv = WriteStreamBytes(aBuffer);
  } else {
    rv = SniffStreamBytes(aBuffer, false);
  }
  if (NS_FAILED(rv)) {
    MarkAsBroken(rv);
    return;
  }

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  if (!mLookingForMetaCharset && mDecodingLocalFileWithoutTokenizing) {
    return;
  }

  ParseAvailableData();

  if (mBomState != BOM_SNIFFING_OVER || mFlushTimerArmed || mSpeculating) {
    return;
  }

  {
    mozilla::MutexAutoLock flushTimerLock(mFlushTimerMutex);
    mFlushTimer->InitWithNamedFuncCallback(
        nsHtml5StreamParser::TimerCallback, static_cast<void*>(this),
        mFlushTimerEverFired ? StaticPrefs::html5_flushtimer_initialdelay()
                             : StaticPrefs::html5_flushtimer_subsequentdelay(),
        nsITimer::TYPE_ONE_SHOT, "nsHtml5StreamParser::DoDataAvailable"_ns);
  }
  mFlushTimerArmed = true;
}

class nsHtml5DataAvailable : public Runnable {
 private:
  nsHtml5StreamParserPtr mStreamParser;
  Buffer<uint8_t> mData;

 public:
  nsHtml5DataAvailable(nsHtml5StreamParser* aStreamParser,
                       Buffer<uint8_t>&& aData)
      : Runnable("nsHtml5DataAvailable"),
        mStreamParser(aStreamParser),
        mData(std::move(aData)) {}
  NS_IMETHOD Run() override {
    mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
    mStreamParser->DoDataAvailableBuffer(std::move(mData));
    mStreamParser->PostLoadFlusher();
    return NS_OK;
  }
};

nsresult nsHtml5StreamParser::OnDataAvailable(nsIRequest* aRequest,
                                              nsIInputStream* aInStream,
                                              uint64_t aSourceOffset,
                                              uint32_t aLength) {
  nsresult rv;

  MOZ_ASSERT(mRequest == aRequest, "Got data on wrong stream.");
  uint32_t totalRead;
  // Main thread to parser thread dispatch requires copying to buffer first.
  if (MOZ_UNLIKELY(NS_IsMainThread())) {
    if (NS_FAILED(rv = mExecutor->IsBroken())) {
      return rv;
    }
    Maybe<Buffer<uint8_t>> maybe = Buffer<uint8_t>::Alloc(aLength);
    if (maybe.isNothing()) {
      return mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
    }
    Buffer<uint8_t> data(std::move(*maybe));
    rv = aInStream->Read(reinterpret_cast<char*>(data.Elements()),
                         data.Length(), &totalRead);
    NS_ENSURE_SUCCESS(rv, rv);
    MOZ_ASSERT(totalRead == aLength);

    nsCOMPtr<nsIRunnable> dataAvailable =
        new nsHtml5DataAvailable(this, std::move(data));
    if (NS_FAILED(mEventTarget->Dispatch(dataAvailable,
                                         nsIThread::DISPATCH_NORMAL))) {
      NS_WARNING("Dispatching DataAvailable event failed.");
    }
    return rv;
  }

  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mozilla::MutexAutoLock autoLock(mTokenizerMutex);

  if (NS_FAILED(rv = mTreeBuilder->IsBroken())) {
    return rv;
  }

  // Since we're getting OnDataAvailable directly on the parser thread,
  // there is no nsHtml5DataAvailable that would call PostLoadFlusher.
  // Hence, we need to call PostLoadFlusher() before this method returns.
  // Braces for RAII clarity relative to the mutex despite not being
  // strictly necessary.
  {
    auto speculationFlusher = MakeScopeExit([&] { PostLoadFlusher(); });

    if (mBufferingBytes) {
      Maybe<Buffer<uint8_t>> maybe = Buffer<uint8_t>::Alloc(aLength);
      if (maybe.isNothing()) {
        MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return NS_ERROR_OUT_OF_MEMORY;
      }
      Buffer<uint8_t> data(std::move(*maybe));
      rv = aInStream->Read(reinterpret_cast<char*>(data.Elements()),
                           data.Length(), &totalRead);
      NS_ENSURE_SUCCESS(rv, rv);
      MOZ_ASSERT(totalRead == aLength);
      DoDataAvailableBuffer(std::move(data));
      return rv;
    }
    // Read directly from response buffer.
    rv = aInStream->ReadSegments(CopySegmentsToParser, this, aLength,
                                 &totalRead);
    NS_ENSURE_SUCCESS(rv, rv);
    MOZ_ASSERT(totalRead == aLength);
    return rv;
  }
}

// Called under lock by function ptr
/* static */
nsresult nsHtml5StreamParser::CopySegmentsToParser(
    nsIInputStream* aInStream, void* aClosure, const char* aFromSegment,
    uint32_t aToOffset, uint32_t aCount,
    uint32_t* aWriteCount) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  nsHtml5StreamParser* parser = static_cast<nsHtml5StreamParser*>(aClosure);

  parser->DoDataAvailable(AsBytes(Span(aFromSegment, aCount)));
  // Assume DoDataAvailable consumed all available bytes.
  *aWriteCount = aCount;
  return NS_OK;
}

const Encoding* nsHtml5StreamParser::PreferredForInternalEncodingDecl(
    const nsAString& aEncoding) {
  const Encoding* newEncoding = Encoding::ForLabel(aEncoding);
  if (!newEncoding) {
    // the encoding name is bogus
    mTreeBuilder->MaybeComplainAboutCharset("EncMetaUnsupported", true,
                                            mTokenizer->getLineNumber());
    return nullptr;
  }

  if (newEncoding == UTF_16BE_ENCODING || newEncoding == UTF_16LE_ENCODING) {
    mTreeBuilder->MaybeComplainAboutCharset("EncMetaUtf16", true,
                                            mTokenizer->getLineNumber());
    newEncoding = UTF_8_ENCODING;
  }

  if (newEncoding == X_USER_DEFINED_ENCODING) {
    // WebKit/Blink hack for Indian and Armenian legacy sites
    mTreeBuilder->MaybeComplainAboutCharset("EncMetaUserDefined", true,
                                            mTokenizer->getLineNumber());
    newEncoding = WINDOWS_1252_ENCODING;
  }

  if (newEncoding == REPLACEMENT_ENCODING) {
    // No line number, because the replacement encoding doesn't allow
    // showing the lines.
    mTreeBuilder->MaybeComplainAboutCharset("EncMetaReplacement", true, 0);
  }

  return newEncoding;
}

bool nsHtml5StreamParser::internalEncodingDeclaration(nsHtml5String aEncoding) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  if ((mCharsetSource >= kCharsetFromMetaTag &&
       mCharsetSource != kCharsetFromFinalAutoDetectionFile) ||
      mSeenEligibleMetaCharset) {
    return false;
  }

  nsString newEncoding;  // Not Auto, because using it to hold nsStringBuffer*
  aEncoding.ToString(newEncoding);
  auto encoding = PreferredForInternalEncodingDecl(newEncoding);
  if (!encoding) {
    return false;
  }

  mSeenEligibleMetaCharset = true;

  if (!mLookingForMetaCharset) {
    if (mInitialEncodingWasFromParentFrame) {
      mTreeBuilder->MaybeComplainAboutCharset("EncMetaTooLateFrame", true,
                                              mTokenizer->getLineNumber());
    } else {
      mTreeBuilder->MaybeComplainAboutCharset("EncMetaTooLate", true,
                                              mTokenizer->getLineNumber());
    }
    return false;
  }
  if (mTemplatePushedOrHeadPopped) {
    mTreeBuilder->MaybeComplainAboutCharset("EncMetaAfterHeadInKilobyte", false,
                                            mTokenizer->getLineNumber());
  }

  if (mForceAutoDetection && encoding->IsAsciiCompatible()) {
    return false;
  }

  mNeedsEncodingSwitchTo = encoding;
  mEncodingSwitchSource = kCharsetFromMetaTag;
  return true;
}

bool nsHtml5StreamParser::TemplatePushedOrHeadPopped() {
  MOZ_ASSERT(
      IsParserThread() || mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN,
      "Wrong thread!");
  mTemplatePushedOrHeadPopped = true;
  return mNumBytesBuffered >= UNCONDITIONAL_META_SCAN_BOUNDARY;
}

void nsHtml5StreamParser::RememberGt(int32_t aPos) {
  if (mLookingForMetaCharset) {
    mGtBuffer = mFirstBuffer;
    mGtPos = aPos;
  }
}

void nsHtml5StreamParser::PostLoadFlusher() {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mTokenizerMutex.AssertCurrentThreadOwns();

  mTreeBuilder->FlushLoads();
  // Dispatch this runnable unconditionally, because the loads
  // that need flushing may have been flushed earlier even if the
  // flush right above here did nothing. (Is this still true?)
  nsCOMPtr<nsIRunnable> runnable(mLoadFlusher);
  if (NS_FAILED(
          DispatchToMain(CreateRenderBlockingRunnable(runnable.forget())))) {
    NS_WARNING("failed to dispatch load flush event");
  }

  if ((mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML) &&
      mTokenizer->ShouldFlushViewSource()) {
    auto r = mTreeBuilder->Flush();  // delete useless ops
    MOZ_ASSERT(r.isOk(), "Should have null sink with View Source");
    r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
      return;
    }
    if (r.unwrap()) {
      nsCOMPtr<nsIRunnable> runnable(mExecutorFlusher);
      if (NS_FAILED(DispatchToMain(runnable.forget()))) {
        NS_WARNING("failed to dispatch executor flush event");
      }
    }
  }
}

void nsHtml5StreamParser::FlushTreeOpsAndDisarmTimer() {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  if (mFlushTimerArmed) {
    // avoid calling Cancel if the flush timer isn't armed to avoid acquiring
    // a mutex
    {
      mozilla::MutexAutoLock flushTimerLock(mFlushTimerMutex);
      mFlushTimer->Cancel();
    }
    mFlushTimerArmed = false;
  }
  if (mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML) {
    auto r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
    }
  }
  auto r = mTreeBuilder->Flush();
  if (r.isErr()) {
    MarkAsBroken(r.unwrapErr());
  }
  nsCOMPtr<nsIRunnable> runnable(mExecutorFlusher);
  if (NS_FAILED(DispatchToMain(runnable.forget()))) {
    NS_WARNING("failed to dispatch executor flush event");
  }
}

void nsHtml5StreamParser::SwitchDecoderIfAsciiSoFar(
    NotNull<const Encoding*> aEncoding) {
  if (mEncoding == aEncoding) {
    MOZ_ASSERT(!mStartedFeedingDevTools);
    // Report all already-decoded buffers to the dev tools if needed.
    if (mURIToSendToDevtools) {
      nsHtml5OwningUTF16Buffer* buffer = mFirstBufferOfMetaScan;
      while (buffer) {
        auto s = Span(buffer->getBuffer(), buffer->getEnd());
        OnNewContent(s);
        buffer = buffer->next;
      }
    }
    return;
  }
  if (!mEncoding->IsAsciiCompatible() || !aEncoding->IsAsciiCompatible()) {
    return;
  }
  size_t numAscii = 0;
  MOZ_ASSERT(mFirstBufferOfMetaScan,
             "Why did we come here without starting meta scan?");
  nsHtml5OwningUTF16Buffer* buffer = mFirstBufferOfMetaScan;
  while (buffer != mFirstBuffer) {
    MOZ_ASSERT(buffer, "mFirstBuffer should have acted as sentinel!");
    MOZ_ASSERT(buffer->getStart() == buffer->getEnd(),
               "Why wasn't an early buffer fully consumed?");
    auto s = Span(buffer->getBuffer(), buffer->getStart());
    if (!IsAscii(s)) {
      return;
    }
    numAscii += s.Length();
    buffer = buffer->next;
  }
  auto s = Span(mFirstBuffer->getBuffer(), mFirstBuffer->getStart());
  if (!IsAscii(s)) {
    return;
  }
  numAscii += s.Length();

  MOZ_ASSERT(!mStartedFeedingDevTools);
  // Report the ASCII prefix to dev tools if needed
  if (mURIToSendToDevtools) {
    buffer = mFirstBufferOfMetaScan;
    while (buffer != mFirstBuffer) {
      MOZ_ASSERT(buffer, "mFirstBuffer should have acted as sentinel!");
      MOZ_ASSERT(buffer->getStart() == buffer->getEnd(),
                 "Why wasn't an early buffer fully consumed?");
      auto s = Span(buffer->getBuffer(), buffer->getStart());
      OnNewContent(s);
      buffer = buffer->next;
    }
    auto s = Span(mFirstBuffer->getBuffer(), mFirstBuffer->getStart());
    OnNewContent(s);
  }

  // Success! Now let's get rid of the already-decoded but not tokenized data:
  mFirstBuffer->setEnd(mFirstBuffer->getStart());
  mLastBuffer = mFirstBuffer;
  mFirstBuffer->next = nullptr;

  // Note: We could have scanned further for ASCII, which could avoid some
  // buffer deallocation and reallocation. However, chances are that if we got
  // until meta without non-ASCII before, there's going to be a title with
  // non-ASCII soon after anyway, so let's avoid the complexity of finding out.

  MOZ_ASSERT(mUnicodeDecoder, "How come we scanned meta without a decoder?");
  mEncoding = aEncoding;
  mEncoding->NewDecoderWithoutBOMHandlingInto(*mUnicodeDecoder);
  mHasHadErrors = false;

  MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing,
             "Must have set mDecodingLocalFileWithoutTokenizing to false to "
             "report data to dev tools below");
  MOZ_ASSERT(!mLookingForMetaCharset,
             "Must have set mLookingForMetaCharset to false to report data to "
             "dev tools below");

  // Now skip over as many bytes and redecode the tail of the
  // buffered bytes.
  size_t skipped = 0;
  for (auto&& buffer : mBufferedBytes) {
    size_t nextSkipped = skipped + buffer.Length();
    if (nextSkipped <= numAscii) {
      skipped = nextSkipped;
      continue;
    }
    if (skipped >= numAscii) {
      WriteStreamBytes(buffer);
      skipped = nextSkipped;
      continue;
    }
    size_t tailLength = nextSkipped - numAscii;
    WriteStreamBytes(Span<uint8_t>(buffer).From(buffer.Length() - tailLength));
    skipped = nextSkipped;
  }
}

size_t nsHtml5StreamParser::CountGts() {
  if (!mGtBuffer) {
    return 0;
  }
  size_t gts = 0;
  nsHtml5OwningUTF16Buffer* buffer = mFirstBufferOfMetaScan;
  for (;;) {
    MOZ_ASSERT(buffer, "How did we walk past mGtBuffer?");
    char16_t* buf = buffer->getBuffer();
    if (buffer == mGtBuffer) {
      for (int32_t i = 0; i <= mGtPos; ++i) {
        if (buf[i] == u'>') {
          ++gts;
        }
      }
      break;
    }
    for (int32_t i = 0; i < buffer->getEnd(); ++i) {
      if (buf[i] == u'>') {
        ++gts;
      }
    }
    buffer = buffer->next;
  }
  return gts;
}

void nsHtml5StreamParser::DiscardMetaSpeculation() {
  mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
  // Rewind the stream
  MOZ_ASSERT(!mAtEOF, "How did we end up setting this?");
  mTokenizer->resetToDataState();
  mTokenizer->setLineNumber(1);
  mLastWasCR = false;

  if (mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN) {
    // resetToDataState() above logically rewinds to the state before
    // the plain text start, so we need to start plain text again to
    // put the tokenizer into the plain text state.
    mTokenizer->StartPlainText();
  }

  mFirstBuffer = mLastBuffer;
  mFirstBuffer->setStart(0);
  mFirstBuffer->setEnd(0);
  mFirstBuffer->next = nullptr;

  mTreeBuilder->flushCharacters();  // empty the pending buffer
  mTreeBuilder->ClearOps();         // now get rid of the failed ops

  if (mMode == VIEW_SOURCE_HTML) {
    mTokenizer->RewindViewSource();
  }

  {
    // We know that this resets the tree builder back to the start state.
    // This must happen _after_ the flushCharacters() call above!
    const auto& speculation = mSpeculations.ElementAt(0);
    mTreeBuilder->loadState(speculation->GetSnapshot());
  }

  // Experimentation suggests that we don't need to do anything special
  // for ignoring the leading LF in View Source here.

  mSpeculations.Clear();  // potentially a huge number of destructors
                          // run here synchronously...

  // Now set up a new speculation for the main thread to find.
  // Note that we stay in the speculating state, because the main thread
  // knows how to come out of that state and this thread does not.

  nsHtml5Speculation* speculation = new nsHtml5Speculation(
      mFirstBuffer, mFirstBuffer->getStart(), mTokenizer->getLineNumber(),
      mTokenizer->getColumnNumber(), mTreeBuilder->newSnapshot());
  MOZ_ASSERT(!mFlushTimerArmed, "How did we end up arming the timer?");
  if (mMode == VIEW_SOURCE_HTML) {
    mTokenizer->SetViewSourceOpSink(speculation);
    mTokenizer->StartViewSourceBodyContents();
  } else {
    MOZ_ASSERT(mMode != VIEW_SOURCE_XML);
    mTreeBuilder->SetOpSink(speculation);
  }
  mSpeculations.AppendElement(speculation);  // adopts the pointer
  MOZ_ASSERT(mSpeculating, "How did we end speculating?");
}

/*
 * The general idea is to match WebKit and Blink exactly for meta
 * scan except:
 *
 * 1. WebKit and Blink look for meta as if scripting was disabled
 *    for `noscript` purposes. This implementation matches the
 *    `noscript` treatment of the observable DOM building (in order
 *    to be able to use the same tree builder run).
 * 2. WebKit and Blink look for meta as if the foreign content
 *    feedback from the tree builder to the tokenizer didn't exist.
 *    This implementation considers the foreign content rules in
 *    order to be able to use the same tree builder run for meta
 *    and the observable DOM building. Note that since <svg> and
 *    <math> imply the end of head, this only matters for meta after
 *    head but starting within the 1024-byte zone.
 *
 * Template is treated specially, because that WebKit/Blink behavior
 * is easy to emulate unlike the above two exceptions. In general,
 * the meta scan token handler in WebKit and Blink behaves as if there
 * was a scripting-disabled tree builder predating the introduction
 * of foreign content and template.
 *
 * Meta is honored if it _starts_ within the first 1024 kilobytes or,
 * if by the 1024-byte boundary head hasn't ended and a template
 * element hasn't started, a meta occurs before the first of the head
 * ending or a template element starting.
 *
 * If a meta isn't honored according to the above definition, and
 * we aren't dealing with plain text, the buffered bytes, which by
 * now have to contain `>` character unless we encountered EOF, are
 * scanned for syntax resembling an XML declaration.
 *
 * If neither a meta nor syntax resembling an XML declaration has
 * been honored and we aren't inheriting the encoding from a
 * same-origin parent or parsing for XHR, chardetng is used.
 * chardetng runs first for the part of the document that was searched
 * for meta and then at EOF. The part searched for meta is defined as
 * follows in order to avoid network buffer boundary-dependent
 * behavior:
 *
 * 1. At least the first 1024 bytes. (This is what happens for plain
 *    text.)
 * 2. If the 1024-byte boundary is within a tag, comment, doctype,
 *    or CDATA section, at least up to the end of that token or CDATA
 *    section. (Exception: If the 1024-byte boundary is in an RCDATA
 *    end tag that hasn't yet been decided to be an end tag, the
 *    token is not considered.)
 * 3. If at the 1024-byte boundary, head hasn't ended and there hasn't
 *    been a template tag, up to the end of the first template tag
 *    or token ending the head, whichever comes first.
 * 4. Except if head is ended by a text token, only to the end of the
 *    most recent tag, comment, or doctype token. (Because text is
 *    coalesced, so it would be harder to correlate the text to the
 *    bytes.)
 *
 * An encoding-related reload is still possible if chardetng's guess
 * at EOF differs from its initial guess.
 */
bool nsHtml5StreamParser::ProcessLookingForMetaCharset(bool aEof) {
  MOZ_ASSERT(mBomState == BOM_SNIFFING_OVER);
  MOZ_ASSERT(mMode != VIEW_SOURCE_XML);
  bool rewound = false;
  MOZ_ASSERT(mForceAutoDetection ||
                 mCharsetSource < kCharsetFromInitialAutoDetectionASCII ||
                 mCharsetSource == kCharsetFromParentFrame,
             "Why are we looking for meta charset if we've seen it?");
  // NOTE! We may come here multiple times with
  // mNumBytesBuffered == UNCONDITIONAL_META_SCAN_BOUNDARY
  // if the tokenizer suspends multiple times after decoding has reached
  // mNumBytesBuffered == UNCONDITIONAL_META_SCAN_BOUNDARY. That's why
  // we need to also check whether the we are at the end of the last
  // decoded buffer.
  // Note that DoDataAvailableBuffer() ensures that the code here has
  // the opportunity to run at the exact UNCONDITIONAL_META_SCAN_BOUNDARY
  // even if there isn't a network buffer boundary there.
  bool atKilobyte = false;
  if ((mNumBytesBuffered == UNCONDITIONAL_META_SCAN_BOUNDARY &&
       mFirstBuffer == mLastBuffer && !mFirstBuffer->hasMore())) {
    atKilobyte = true;
    mTokenizer->AtKilobyteBoundary();
  }
  if (!mNeedsEncodingSwitchTo &&
      (aEof || (mTemplatePushedOrHeadPopped &&
                !mTokenizer->IsInTokenStartedAtKilobyteBoundary() &&
                (atKilobyte ||
                 mNumBytesBuffered > UNCONDITIONAL_META_SCAN_BOUNDARY)))) {
    // meta charset was not found
    mLookingForMetaCharset = false;
    if (mStartsWithLtQuestion && mCharsetSource < kCharsetFromXmlDeclaration) {
      // Look for bogo XML declaration.
      // Search the first buffer in the hope that '>' is within it.
      MOZ_ASSERT(!mBufferedBytes.IsEmpty(),
                 "How did at least <? not get buffered?");
      Buffer<uint8_t>& first = mBufferedBytes[0];
      const Encoding* encoding =
          xmldecl_parse(first.Elements(), first.Length());
      if (!encoding) {
        // Our bogo XML declaration scanner wants to see a contiguous buffer, so
        // let's linearize the data. (Ideally, the XML declaration scanner would
        // be incremental, but this is the rare path anyway.)
        Vector<uint8_t> contiguous;
        if (!contiguous.append(first.Elements(), first.Length())) {
          MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
          return false;
        }
        for (size_t i = 1; i < mBufferedBytes.Length(); ++i) {
          Buffer<uint8_t>& buffer = mBufferedBytes[i];
          const uint8_t* elements = buffer.Elements();
          size_t length = buffer.Length();
          const uint8_t* lt = (const uint8_t*)memchr(elements, '>', length);
          bool stop = false;
          if (lt) {
            length = (lt - elements) + 1;
            stop = true;
          }
          if (!contiguous.append(elements, length)) {
            MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
            return false;
          }
          if (stop) {
            // Avoid linearizing all buffered bytes unnecessarily.
            break;
          }
        }
        encoding = xmldecl_parse(contiguous.begin(), contiguous.length());
      }
      if (encoding) {
        if (!(mForceAutoDetection && encoding->IsAsciiCompatible())) {
          mForceAutoDetection = false;
          mNeedsEncodingSwitchTo = encoding;
          mEncodingSwitchSource = kCharsetFromXmlDeclaration;
        }
      }
    }
    // Check again in case we found an encoding in the bogo XML declaration.
    if (!mNeedsEncodingSwitchTo &&
        (mForceAutoDetection ||
         mCharsetSource < kCharsetFromInitialAutoDetectionASCII) &&
        !(mMode == LOAD_AS_DATA || mMode == VIEW_SOURCE_XML) &&
        !(mDecodingLocalFileWithoutTokenizing && !aEof &&
          mNumBytesBuffered <= LOCAL_FILE_UTF_8_BUFFER_SIZE)) {
      MOZ_ASSERT(!mStartedFeedingDetector);
      if (mNumBytesBuffered == UNCONDITIONAL_META_SCAN_BOUNDARY || aEof) {
        // We know that all the buffered bytes have been tokenized, so feed
        // them all to chardetng.
        for (auto&& buffer : mBufferedBytes) {
          FeedDetector(buffer);
        }
        if (aEof) {
          MOZ_ASSERT(!mChardetEof);
          DetectorEof();
        }
        auto [encoding, source] = GuessEncoding(true);
        mNeedsEncodingSwitchTo = encoding;
        mEncodingSwitchSource = source;
      } else if (mNumBytesBuffered > UNCONDITIONAL_META_SCAN_BOUNDARY) {
        size_t gtsLeftToFind = CountGts();
        size_t bytesSeen = 0;
        // We sync the bytes to the UTF-16 code units seen to avoid depending
        // on network buffer boundaries. We do the syncing by counting '>'
        // bytes / code units. However, we always scan at least 1024 bytes.
        // The 1024-byte boundary is guaranteed to be between buffers.
        // The guarantee is implemented in DoDataAvailableBuffer().
        for (auto&& buffer : mBufferedBytes) {
          if (!mNeedsEncodingSwitchTo) {
            if (gtsLeftToFind) {
              auto span = buffer.AsSpan();
              bool feed = true;
              for (size_t i = 0; i < span.Length(); ++i) {
                if (span[i] == uint8_t('>')) {
                  --gtsLeftToFind;
                  if (!gtsLeftToFind) {
                    if (bytesSeen < UNCONDITIONAL_META_SCAN_BOUNDARY) {
                      break;
                    }
                    ++i;  // Skip the gt
                    FeedDetector(span.To(i));
                    auto [encoding, source] = GuessEncoding(true);
                    mNeedsEncodingSwitchTo = encoding;
                    mEncodingSwitchSource = source;
                    FeedDetector(span.From(i));
                    bytesSeen += buffer.Length();
                    // No need to update bytesSeen anymore, but let's do it for
                    // debugging.
                    // We should do `continue outer;` but C++ can't.
                    feed = false;
                    break;
                  }
                }
              }
              if (feed) {
                FeedDetector(buffer);
                bytesSeen += buffer.Length();
              }
              continue;
            }
            if (bytesSeen == UNCONDITIONAL_META_SCAN_BOUNDARY) {
              auto [encoding, source] = GuessEncoding(true);
              mNeedsEncodingSwitchTo = encoding;
              mEncodingSwitchSource = source;
            }
          }
          FeedDetector(buffer);
          bytesSeen += buffer.Length();
        }
      }
      MOZ_ASSERT(mNeedsEncodingSwitchTo,
                 "How come we didn't call GuessEncoding()?");
    }
  }
  if (mNeedsEncodingSwitchTo) {
    mDecodingLocalFileWithoutTokenizing = false;
    mLookingForMetaCharset = false;

    auto needsEncodingSwitchTo = WrapNotNull(mNeedsEncodingSwitchTo);
    mNeedsEncodingSwitchTo = nullptr;

    SwitchDecoderIfAsciiSoFar(needsEncodingSwitchTo);
    // The above line may have changed mEncoding so that mEncoding equals
    // needsEncodingSwitchTo.

    mCharsetSource = mEncodingSwitchSource;

    if (mMode == VIEW_SOURCE_HTML) {
      auto r = mTokenizer->FlushViewSource();
      if (r.isErr()) {
        MarkAsBroken(r.unwrapErr());
        return false;
      }
    }
    auto r = mTreeBuilder->Flush();
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
      return false;
    }

    if (mEncoding != needsEncodingSwitchTo) {
      // Speculation failed
      rewound = true;

      if (mEncoding == ISO_2022_JP_ENCODING ||
          needsEncodingSwitchTo == ISO_2022_JP_ENCODING) {
        // Chances are no Web author will fix anything due to this message, so
        // this is here to help understanding issues when debugging sites made
        // by someone else.
        mTreeBuilder->MaybeComplainAboutCharset("EncSpeculationFail2022", false,
                                                mTokenizer->getLineNumber());
      } else {
        if (mCharsetSource == kCharsetFromMetaTag) {
          mTreeBuilder->MaybeComplainAboutCharset(
              "EncSpeculationFailMeta", false, mTokenizer->getLineNumber());
        } else if (mCharsetSource == kCharsetFromXmlDeclaration) {
          // This intentionally refers to the line number of how far ahead
          // the document was parsed even though the bogo XML decl is always
          // on line 1.
          mTreeBuilder->MaybeComplainAboutCharset(
              "EncSpeculationFailXml", false, mTokenizer->getLineNumber());
        }
      }

      DiscardMetaSpeculation();
      // Redecode the stream.
      mEncoding = needsEncodingSwitchTo;
      mUnicodeDecoder = mEncoding->NewDecoderWithBOMRemoval();
      mHasHadErrors = false;

      MOZ_ASSERT(!mDecodingLocalFileWithoutTokenizing,
                 "Must have set mDecodingLocalFileWithoutTokenizing to false "
                 "to report data to dev tools below");
      MOZ_ASSERT(!mLookingForMetaCharset,
                 "Must have set mLookingForMetaCharset to false to report data "
                 "to dev tools below");
      for (auto&& buffer : mBufferedBytes) {
        nsresult rv = WriteStreamBytes(buffer);
        if (NS_FAILED(rv)) {
          MarkAsBroken(rv);
          return false;
        }
      }
    }
  } else if (!mLookingForMetaCharset && !mDecodingLocalFileWithoutTokenizing) {
    MOZ_ASSERT(!mStartedFeedingDevTools);
    // Report all already-decoded buffers to the dev tools if needed.
    if (mURIToSendToDevtools) {
      nsHtml5OwningUTF16Buffer* buffer = mFirstBufferOfMetaScan;
      while (buffer) {
        auto s = Span(buffer->getBuffer(), buffer->getEnd());
        OnNewContent(s);
        buffer = buffer->next;
      }
    }
  }
  if (!mLookingForMetaCharset) {
    mGtBuffer = nullptr;
    mGtPos = 0;

    if (!mDecodingLocalFileWithoutTokenizing) {
      mFirstBufferOfMetaScan = nullptr;
      mBufferingBytes = false;
      mBufferedBytes.Clear();
      mTreeBuilder->SetDocumentCharset(mEncoding, mCharsetSource, true);
      if (mMode == VIEW_SOURCE_HTML) {
        auto r = mTokenizer->FlushViewSource();
        if (r.isErr()) {
          MarkAsBroken(r.unwrapErr());
          return false;
        }
      }
      auto r = mTreeBuilder->Flush();
      if (r.isErr()) {
        MarkAsBroken(r.unwrapErr());
        return false;
      }
    }
  }
  return rewound;
}

void nsHtml5StreamParser::ParseAvailableData() {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mTokenizerMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(!(mDecodingLocalFileWithoutTokenizing && !mLookingForMetaCharset));

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  if (mSpeculating && !IsSpeculationEnabled()) {
    return;
  }

  bool requestedReload = false;
  for (;;) {
    if (!mFirstBuffer->hasMore()) {
      if (mFirstBuffer == mLastBuffer) {
        switch (mStreamState) {
          case STREAM_BEING_READ:
            // never release the last buffer.
            if (!mSpeculating) {
              // reuse buffer space if not speculating
              mFirstBuffer->setStart(0);
              mFirstBuffer->setEnd(0);
            }
            return;  // no more data for now but expecting more
          case STREAM_ENDED:
            if (mAtEOF) {
              return;
            }
            if (mLookingForMetaCharset) {
              // When called with aEof=true, ProcessLookingForMetaCharset()
              // is guaranteed to set mLookingForMetaCharset to false so
              // that we can't come here twice.
              if (ProcessLookingForMetaCharset(true)) {
                if (IsTerminatedOrInterrupted()) {
                  return;
                }
                continue;
              }
            } else if ((mForceAutoDetection ||
                        mCharsetSource < kCharsetFromParentFrame) &&
                       !(mMode == LOAD_AS_DATA || mMode == VIEW_SOURCE_XML) &&
                       !mReparseForbidden) {
              // An earlier DetectorEof() call is possible in which case
              // the one here is a no-op.
              DetectorEof();
              auto [encoding, source] = GuessEncoding(false);
              if (encoding != mEncoding) {
                // Request a reload from the docshell.
                MOZ_ASSERT(
                    (source >=
                         kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII &&
                     source <=
                         kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLDInitialWasASCII) ||
                    source == kCharsetFromFinalUserForcedAutoDetection);
                mTreeBuilder->NeedsCharsetSwitchTo(encoding, source, 0);
                requestedReload = true;
              } else if (mCharsetSource ==
                             kCharsetFromInitialAutoDetectionASCII &&
                         mDetectorHasSeenNonAscii) {
                mCharsetSource = source;
                mTreeBuilder->UpdateCharsetSource(mCharsetSource);
              }
            }

            mAtEOF = true;
            if (!mForceAutoDetection && !requestedReload) {
              if (mCharsetSource == kCharsetFromParentFrame) {
                mTreeBuilder->MaybeComplainAboutCharset("EncNoDeclarationFrame",
                                                        false, 0);
              } else if (mCharsetSource == kCharsetFromXmlDeclaration) {
                // We know the bogo XML decl is always on the first line.
                mTreeBuilder->MaybeComplainAboutCharset("EncXmlDecl", false, 1);
              } else if (
                  mCharsetSource >=
                      kCharsetFromInitialAutoDetectionWouldHaveBeenUTF8 &&
                  mCharsetSource <=
                      kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD) {
                if (mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN) {
                  mTreeBuilder->MaybeComplainAboutCharset("EncNoDeclPlain",
                                                          true, 0);
                } else {
                  mTreeBuilder->MaybeComplainAboutCharset("EncNoDecl", true, 0);
                }
              }

              if (mHasHadErrors && mEncoding != REPLACEMENT_ENCODING) {
                if (mEncoding == UTF_8_ENCODING) {
                  mTreeBuilder->TryToEnableEncodingMenu();
                }
                if (mCharsetSource == kCharsetFromParentFrame) {
                  if (mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN) {
                    mTreeBuilder->MaybeComplainAboutCharset(
                        "EncErrorFramePlain", true, 0);
                  } else {
                    mTreeBuilder->MaybeComplainAboutCharset("EncErrorFrame",
                                                            true, 0);
                  }
                } else if (
                    mCharsetSource >= kCharsetFromXmlDeclaration &&
                    !(mCharsetSource >=
                          kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII &&
                      mCharsetSource <=
                          kCharsetFromFinalUserForcedAutoDetection)) {
                  mTreeBuilder->MaybeComplainAboutCharset("EncError", true, 0);
                }
              }
            }
            if (NS_SUCCEEDED(mTreeBuilder->IsBroken())) {
              mTokenizer->eof();
              nsresult rv;
              if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
                MarkAsBroken(rv);
              } else {
                mTreeBuilder->StreamEnded();
                if (mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML) {
                  if (!mTokenizer->EndViewSource()) {
                    MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
                  }
                }
              }
            }
            FlushTreeOpsAndDisarmTimer();
            return;  // no more data and not expecting more
          default:
            MOZ_ASSERT_UNREACHABLE("It should be impossible to reach this.");
            return;
        }
      }
      mFirstBuffer = mFirstBuffer->next;
      continue;
    }

    // now we have a non-empty buffer
    mFirstBuffer->adjust(mLastWasCR);
    mLastWasCR = false;
    if (mFirstBuffer->hasMore()) {
      if (!mTokenizer->EnsureBufferSpace(mFirstBuffer->getLength())) {
        MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
      mLastWasCR = mTokenizer->tokenizeBuffer(mFirstBuffer);
      nsresult rv;
      if (NS_FAILED((rv = mTreeBuilder->IsBroken()))) {
        MarkAsBroken(rv);
        return;
      }
      if (mTreeBuilder->HasScriptThatMayDocumentWriteOrBlock()) {
        // `HasScriptThatMayDocumentWriteOrBlock()` cannot return true if the
        // tree builder is preventing script execution.
        MOZ_ASSERT(mMode == NORMAL);
        mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
        nsHtml5Speculation* speculation = new nsHtml5Speculation(
            mFirstBuffer, mFirstBuffer->getStart(), mTokenizer->getLineNumber(),
            mTokenizer->getColumnNumber(), mTreeBuilder->newSnapshot());
        mTreeBuilder->AddSnapshotToScript(speculation->GetSnapshot(),
                                          speculation->GetStartLineNumber());
        if (mLookingForMetaCharset) {
          if (mMode == VIEW_SOURCE_HTML) {
            auto r = mTokenizer->FlushViewSource();
            if (r.isErr()) {
              MarkAsBroken(r.unwrapErr());
              return;
            }
          }
          auto r = mTreeBuilder->Flush();
          if (r.isErr()) {
            MarkAsBroken(r.unwrapErr());
            return;
          }
        } else {
          FlushTreeOpsAndDisarmTimer();
        }
        mTreeBuilder->SetOpSink(speculation);
        mSpeculations.AppendElement(speculation);  // adopts the pointer
        mSpeculating = true;
      }
      if (IsTerminatedOrInterrupted()) {
        return;
      }
    }
    if (mLookingForMetaCharset) {
      (void)ProcessLookingForMetaCharset(false);
    }
  }
}

class nsHtml5StreamParserContinuation : public Runnable {
 private:
  nsHtml5StreamParserPtr mStreamParser;

 public:
  explicit nsHtml5StreamParserContinuation(nsHtml5StreamParser* aStreamParser)
      : Runnable("nsHtml5StreamParserContinuation"),
        mStreamParser(aStreamParser) {}
  NS_IMETHOD Run() override {
    mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
    mStreamParser->Uninterrupt();
    mStreamParser->ParseAvailableData();
    return NS_OK;
  }
};

void nsHtml5StreamParser::ContinueAfterScriptsOrEncodingCommitment(
    nsHtml5Tokenizer* aTokenizer, nsHtml5TreeBuilder* aTreeBuilder,
    bool aLastWasCR) {
  // nullptr for aTokenizer means encoding commitment as opposed to the "after
  // scripts" case.

  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  MOZ_ASSERT(mMode != VIEW_SOURCE_XML,
             "ContinueAfterScriptsOrEncodingCommitment called in XML view "
             "source mode!");
  MOZ_ASSERT(!(aTokenizer && mMode == VIEW_SOURCE_HTML),
             "ContinueAfterScriptsOrEncodingCommitment called with non-null "
             "tokenizer in HTML view "
             "source mode.");
  if (NS_FAILED(mExecutor->IsBroken())) {
    return;
  }
  MOZ_ASSERT(!(aTokenizer && mMode != NORMAL),
             "We should only be executing scripts in the normal mode.");
  if (!aTokenizer && (mMode == PLAIN_TEXT || mMode == VIEW_SOURCE_PLAIN ||
                      mMode == VIEW_SOURCE_HTML)) {
    // Take the ops that were generated from OnStartRequest for the synthetic
    // head section of the document for plain text and HTML View Source.
    // XML View Source never needs this kind of encoding commitment.
    // We need to take the ops here so that they end up in the queue before
    // the ops that we take from a speculation later in this method.
    if (!mExecutor->TakeOpsFromStage()) {
      mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  } else {
#ifdef DEBUG
    mExecutor->AssertStageEmpty();
#endif
  }
  bool speculationFailed = false;
  {
    mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
    if (mSpeculations.IsEmpty()) {
      MOZ_ASSERT_UNREACHABLE(
          "ContinueAfterScriptsOrEncodingCommitment called without "
          "speculations.");
      return;
    }

    const auto& speculation = mSpeculations.ElementAt(0);
    if (aTokenizer &&
        (aLastWasCR || !aTokenizer->isInDataState() ||
         !aTreeBuilder->snapshotMatches(speculation->GetSnapshot()))) {
      speculationFailed = true;
      // We've got a failed speculation :-(
      MaybeDisableFutureSpeculation();
      Interrupt();  // Make the parser thread release the tokenizer mutex sooner
      // Note that the interrupted state continues across possible intervening
      // Necko events until the nsHtml5StreamParserContinuation posted at the
      // end of this method runs. Therefore, this thread is guaranteed to
      // acquire mTokenizerMutex soon even if an intervening Necko event grabbed
      // it between now and the acquisition below.

      // now fall out of the speculationAutoLock into the tokenizerAutoLock
      // block
    } else {
      // We've got a successful speculation!
      if (mSpeculations.Length() > 1) {
        // the first speculation isn't the current speculation, so there's
        // no need to bother the parser thread.
        if (!speculation->FlushToSink(mExecutor)) {
          mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
          return;
        }
        MOZ_ASSERT(!mExecutor->IsScriptExecuting(),
                   "ParseUntilBlocked() was supposed to ensure we don't come "
                   "here when scripts are executing.");
        MOZ_ASSERT(!aTokenizer || mExecutor->IsInFlushLoop(),
                   "How are we here if "
                   "RunFlushLoop() didn't call ParseUntilBlocked() or we're "
                   "not committing to an encoding?");
        mSpeculations.RemoveElementAt(0);
        return;
      }
      // else
      Interrupt();  // Make the parser thread release the tokenizer mutex sooner
      // Note that the interrupted state continues across possible intervening
      // Necko events until the nsHtml5StreamParserContinuation posted at the
      // end of this method runs. Therefore, this thread is guaranteed to
      // acquire mTokenizerMutex soon even if an intervening Necko event grabbed
      // it between now and the acquisition below.

      // now fall through
      // the first speculation is the current speculation. Need to
      // release the the speculation mutex and acquire the tokenizer
      // mutex. (Just acquiring the other mutex here would deadlock)
    }
  }
  {
    mozilla::MutexAutoLock tokenizerAutoLock(mTokenizerMutex);
#ifdef DEBUG
    {
      mAtomTable.SetPermittedLookupEventTarget(
          GetMainThreadSerialEventTarget());
    }
#endif
    // In principle, the speculation mutex should be acquired here,
    // but there's no point, because the parser thread only acquires it
    // when it has also acquired the tokenizer mutex and we are already
    // holding the tokenizer mutex.
    if (speculationFailed) {
      MOZ_ASSERT(mMode == NORMAL);
      // Rewind the stream
      mAtEOF = false;
      const auto& speculation = mSpeculations.ElementAt(0);
      mFirstBuffer = speculation->GetBuffer();
      mFirstBuffer->setStart(speculation->GetStart());
      mTokenizer->setLineNumber(speculation->GetStartLineNumber());
      mTokenizer->setColumnNumberAndResetNextLine(
          speculation->GetStartColumnNumber());

      nsContentUtils::ReportToConsole(
          nsIScriptError::warningFlag, "DOM Events"_ns,
          mExecutor->GetDocument(), PropertiesFile::DOM_PROPERTIES,
          "SpeculationFailed2", nsTArray<nsString>(),
          SourceLocation(mExecutor->GetDocument()->GetDocumentURI(),
                         speculation->GetStartLineNumber(),
                         speculation->GetStartColumnNumber()));

      nsHtml5OwningUTF16Buffer* buffer = mFirstBuffer->next;
      while (buffer) {
        buffer->setStart(0);
        buffer = buffer->next;
      }

      mSpeculations.Clear();  // potentially a huge number of destructors
                              // run here synchronously on the main thread...

      mTreeBuilder->flushCharacters();  // empty the pending buffer
      mTreeBuilder->ClearOps();         // now get rid of the failed ops

      mTreeBuilder->SetOpSink(mExecutor->GetStage());
      mExecutor->StartReadingFromStage();
      mSpeculating = false;

      // Copy state over
      mLastWasCR = aLastWasCR;
      mTokenizer->loadState(aTokenizer);
      mTreeBuilder->loadState(aTreeBuilder);
    } else {
      // We've got a successful speculation and at least a moment ago it was
      // the current speculation
      if (!mSpeculations.ElementAt(0)->FlushToSink(mExecutor)) {
        mExecutor->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
      MOZ_ASSERT(!mExecutor->IsScriptExecuting(),
                 "ParseUntilBlocked() was supposed to ensure we don't come "
                 "here when scripts are executing.");
      MOZ_ASSERT(!aTokenizer || mExecutor->IsInFlushLoop(),
                 "How are we here if "
                 "RunFlushLoop() didn't call ParseUntilBlocked() or we're not "
                 "committing to an encoding?");
      mSpeculations.RemoveElementAt(0);
      if (mSpeculations.IsEmpty()) {
        if (mMode == VIEW_SOURCE_HTML) {
          // If we looked for meta charset in the HTML View Source case.
          mTokenizer->SetViewSourceOpSink(mExecutor->GetStage());
        } else {
          // yes, it was still the only speculation. Now stop speculating
          // However, before telling the executor to read from stage, flush
          // any pending ops straight to the executor, because otherwise
          // they remain unflushed until we get more data from the network.
          mTreeBuilder->SetOpSink(mExecutor);
          auto r = mTreeBuilder->Flush(true);
          if (r.isErr()) {
            mExecutor->MarkAsBroken(r.unwrapErr());
            return;
          }
          mTreeBuilder->SetOpSink(mExecutor->GetStage());
        }
        mExecutor->StartReadingFromStage();
        mSpeculating = false;
      }
    }
    nsCOMPtr<nsIRunnable> event = new nsHtml5StreamParserContinuation(this);
    if (NS_FAILED(mEventTarget->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
      NS_WARNING("Failed to dispatch nsHtml5StreamParserContinuation");
    }
// A stream event might run before this event runs, but that's harmless.
#ifdef DEBUG
    mAtomTable.SetPermittedLookupEventTarget(mEventTarget);
#endif
  }
}

void nsHtml5StreamParser::ContinueAfterFailedCharsetSwitch() {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  nsCOMPtr<nsIRunnable> event = new nsHtml5StreamParserContinuation(this);
  if (NS_FAILED(mEventTarget->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
    NS_WARNING("Failed to dispatch nsHtml5StreamParserContinuation");
  }
}

class nsHtml5TimerKungFu : public Runnable {
 private:
  nsHtml5StreamParserPtr mStreamParser;

 public:
  explicit nsHtml5TimerKungFu(nsHtml5StreamParser* aStreamParser)
      : Runnable("nsHtml5TimerKungFu"), mStreamParser(aStreamParser) {}
  NS_IMETHOD Run() override {
    mozilla::MutexAutoLock flushTimerLock(mStreamParser->mFlushTimerMutex);
    if (mStreamParser->mFlushTimer) {
      mStreamParser->mFlushTimer->Cancel();
      mStreamParser->mFlushTimer = nullptr;
    }
    return NS_OK;
  }
};

void nsHtml5StreamParser::DropTimer() {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  /*
   * Simply nulling out the timer wouldn't work, because if the timer is
   * armed, it needs to be canceled first. Simply canceling it first wouldn't
   * work, because nsTimerImpl::Cancel is not safe for calling from outside
   * the thread where nsTimerImpl::Fire would run. It's not safe to
   * dispatch a runnable to cancel the timer from the destructor of this
   * class, because the timer has a weak (void*) pointer back to this instance
   * of the stream parser and having the timer fire before the runnable
   * cancels it would make the timer access a deleted object.
   *
   * This DropTimer method addresses these issues. This method must be called
   * on the main thread before the destructor of this class is reached.
   * The nsHtml5TimerKungFu object has an nsHtml5StreamParserPtr that addrefs
   * this
   * stream parser object to keep it alive until the runnable is done.
   * The runnable cancels the timer on the parser thread, drops the timer
   * and lets nsHtml5StreamParserPtr send a runnable back to the main thread to
   * release the stream parser.
   */
  mozilla::MutexAutoLock flushTimerLock(mFlushTimerMutex);
  if (mFlushTimer) {
    nsCOMPtr<nsIRunnable> event = new nsHtml5TimerKungFu(this);
    if (NS_FAILED(mEventTarget->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
      NS_WARNING("Failed to dispatch TimerKungFu event");
    }
  }
}

// Using a static, because the method name Notify is taken by the chardet
// callback.
void nsHtml5StreamParser::TimerCallback(nsITimer* aTimer, void* aClosure) {
  (static_cast<nsHtml5StreamParser*>(aClosure))->TimerFlush();
}

void nsHtml5StreamParser::TimerFlush() {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mozilla::MutexAutoLock autoLock(mTokenizerMutex);

  MOZ_ASSERT(!mSpeculating, "Flush timer fired while speculating.");

  // The timer fired if we got here. No need to cancel it. Mark it as
  // not armed, though.
  mFlushTimerArmed = false;

  mFlushTimerEverFired = true;

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  if (mMode == VIEW_SOURCE_HTML || mMode == VIEW_SOURCE_XML) {
    auto r = mTreeBuilder->Flush();  // delete useless ops
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
      return;
    }
    r = mTokenizer->FlushViewSource();
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
      return;
    }
    if (r.unwrap()) {
      nsCOMPtr<nsIRunnable> runnable(mExecutorFlusher);
      if (NS_FAILED(DispatchToMain(runnable.forget()))) {
        NS_WARNING("failed to dispatch executor flush event");
      }
    }
  } else {
    // we aren't speculating and we don't know when new data is
    // going to arrive. Send data to the main thread.
    auto r = mTreeBuilder->Flush(true);
    if (r.isErr()) {
      MarkAsBroken(r.unwrapErr());
      return;
    }
    if (r.unwrap()) {
      nsCOMPtr<nsIRunnable> runnable(mExecutorFlusher);
      if (NS_FAILED(DispatchToMain(runnable.forget()))) {
        NS_WARNING("failed to dispatch executor flush event");
      }
    }
  }
}

void nsHtml5StreamParser::MarkAsBroken(nsresult aRv) {
  MOZ_ASSERT(IsParserThread(), "Wrong thread!");
  mTokenizerMutex.AssertCurrentThreadOwns();

  Terminate();
  mTreeBuilder->MarkAsBroken(aRv);
  auto r = mTreeBuilder->Flush(false);
  if (r.isOk()) {
    MOZ_ASSERT(r.unwrap(), "Should have had the markAsBroken op!");
  } else {
    MOZ_CRASH("OOM prevents propagation of OOM state");
  }
  nsCOMPtr<nsIRunnable> runnable(mExecutorFlusher);
  if (NS_FAILED(DispatchToMain(runnable.forget()))) {
    NS_WARNING("failed to dispatch executor flush event");
  }
}

nsresult nsHtml5StreamParser::DispatchToMain(
    already_AddRefed<nsIRunnable> aRunnable) {
  return SchedulerGroup::Dispatch(std::move(aRunnable));
}
