/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHtml5StreamParser_h
#define nsHtml5StreamParser_h

#include <tuple>

#include "MainThreadUtils.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Encoding.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "nsCharsetSource.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "nsHtml5AtomTable.h"
#include "nsIRequestObserver.h"
#include "nsISerialEventTarget.h"
#include "nsISupports.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nscore.h"

class nsCycleCollectionTraversalCallback;
class nsHtml5OwningUTF16Buffer;
class nsHtml5Parser;
class nsHtml5Speculation;
class nsHtml5String;
class nsHtml5Tokenizer;
class nsHtml5TreeBuilder;
class nsHtml5TreeOpExecutor;
class nsIChannel;
class nsIInputStream;
class nsIRequest;
class nsIRunnable;
class nsITimer;
class nsIURI;

namespace mozilla {
class EncodingDetector;
template <typename T>
class Buffer;

namespace dom {
class DocGroup;
}
}  // namespace mozilla

enum eParserMode {
  /**
   * Parse a document normally as HTML.
   */
  NORMAL,

  /**
   * View document as HTML source.
   */
  VIEW_SOURCE_HTML,

  /**
   * View document as XML source
   */
  VIEW_SOURCE_XML,

  /**
   * View document as plain text source
   */
  VIEW_SOURCE_PLAIN,

  /**
   * View document as plain text
   */
  PLAIN_TEXT,

  /**
   * Load as data (XHR)
   */
  LOAD_AS_DATA,

  /**
   * Parse (non-initial) about:blank for normal viewing (not View Source or
   * data).
   */
  ABOUT_BLANK,
};

enum eBomState {
  /**
   * BOM sniffing hasn't started.
   */
  BOM_SNIFFING_NOT_STARTED,

  /**
   * BOM sniffing is ongoing, and the first byte of an UTF-16LE BOM has been
   * seen.
   */
  SEEN_UTF_16_LE_FIRST_BYTE,

  /**
   * BOM sniffing is ongoing, and the first byte of an UTF-16BE BOM has been
   * seen.
   */
  SEEN_UTF_16_BE_FIRST_BYTE,

  /**
   * BOM sniffing is ongoing, and the first byte of an UTF-8 BOM has been
   * seen.
   */
  SEEN_UTF_8_FIRST_BYTE,

  /**
   * BOM sniffing is ongoing, and the first and second bytes of an UTF-8 BOM
   * have been seen.
   */
  SEEN_UTF_8_SECOND_BYTE,

  /**
   * Seen \x00 in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_BE_XML_FIRST,

  /**
   * Seen \x00< in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_BE_XML_SECOND,

  /**
   * Seen \x00<\x00 in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_BE_XML_THIRD,

  /**
   * Seen \x00<\x00? in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_BE_XML_FOURTH,

  /**
   * Seen \x00<\x00?\x00 in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_BE_XML_FIFTH,

  /**
   * Seen < in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_LE_XML_FIRST,

  /**
   * Seen <\x00 in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_LE_XML_SECOND,

  /**
   * Seen <\x00? in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_LE_XML_THIRD,

  /**
   * Seen <\x00?\x00 in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_LE_XML_FOURTH,

  /**
   * Seen <\x00?\x00x in UTF-16BE bogo-XML declaration.
   */
  SEEN_UTF_16_LE_XML_FIFTH,

  /**
   * BOM sniffing was started but is now over for whatever reason.
   */
  BOM_SNIFFING_OVER,
};

enum eHtml5StreamState {
  STREAM_NOT_STARTED = 0,
  STREAM_BEING_READ = 1,
  STREAM_ENDED = 2
};

class nsHtml5StreamParser final : public nsISupports {
  template <typename T>
  using NotNull = mozilla::NotNull<T>;
  using Encoding = mozilla::Encoding;

  const uint32_t UNCONDITIONAL_META_SCAN_BOUNDARY = 1024;
  const uint32_t READ_BUFFER_SIZE = 1024;
  const uint32_t LOCAL_FILE_UTF_8_BUFFER_SIZE = 1024 * 1024 * 4;  // 4 MB

  friend class nsHtml5RequestStopper;
  friend class nsHtml5DataAvailable;
  friend class nsHtml5StreamParserContinuation;
  friend class nsHtml5TimerKungFu;
  friend class nsHtml5StreamParserPtr;
  friend class nsHtml5StreamListener;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(nsHtml5StreamParser)

  nsHtml5StreamParser(nsHtml5TreeOpExecutor* aExecutor, nsHtml5Parser* aOwner,
                      eParserMode aMode);

  nsresult OnStartRequest(nsIRequest* aRequest);

  nsresult OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInStream,
                           uint64_t aSourceOffset, uint32_t aLength);
  /**
   * ReentrantMonitorAutoEnter is used for protecting access to
   * nsHtml5StreamParser::mOnStopCalled and should be obtained from
   * nsHtml5StreamListener::mDelegateMonitor
   */
  nsresult OnStopRequest(
      nsIRequest* aRequest, nsresult status,
      const mozilla::ReentrantMonitorAutoEnter& aProofOfLock);

  // EncodingDeclarationHandler
  // https://hg.mozilla.org/projects/htmlparser/file/tip/src/nu/validator/htmlparser/common/EncodingDeclarationHandler.java
  /**
   * Tree builder uses this to report a late <meta charset>
   */
  bool internalEncodingDeclaration(nsHtml5String aEncoding);

  bool TemplatePushedOrHeadPopped();

  void RememberGt(int32_t aPos);

  // Not from an external interface

  /**
   * Post a runnable to the main thread to perform the speculative load
   * operations without performing the tree operations.
   *
   * This should be called at the end of each data available or stop
   * request runnable running on the parser thread.
   */
  void PostLoadFlusher();

  /**
   * Pass a buffer to chardetng.
   */
  void FeedDetector(mozilla::Span<const uint8_t> aBuffer);

  /**
   * Report EOF to chardetng.
   */
  void DetectorEof();

  /**
   *  Call this method once you've created a parser, and want to instruct it
   *  about what charset to load
   *
   *  @param   aEncoding the charset of a document
   *  @param   aCharsetSource the source of the charset
   */
  inline void SetDocumentCharset(NotNull<const Encoding*> aEncoding,
                                 nsCharsetSource aSource,
                                 bool aForceAutoDetection) {
    MOZ_ASSERT(mStreamState == STREAM_NOT_STARTED,
               "SetDocumentCharset called too late.");
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    MOZ_ASSERT(!(aForceAutoDetection && aSource >= kCharsetFromOtherComponent),
               "Can't force with high-ranking source.");
    mEncoding = aEncoding;
    mCharsetSource = aSource;
    mForceAutoDetection = aForceAutoDetection;
    mChannelHadCharset = (aSource == kCharsetFromChannel);
  }

  nsresult GetChannel(nsIChannel** aChannel);

  /**
   * The owner parser must call this after script execution
   * when no scripts are executing and the document.written
   * buffer has been exhausted.
   *
   * If the first two arguments are nullptr, instead of
   * continuing after scripts, this method commits to an
   * internally-discovered encoding.
   */
  void ContinueAfterScriptsOrEncodingCommitment(
      nsHtml5Tokenizer* aTokenizer, nsHtml5TreeBuilder* aTreeBuilder,
      bool aLastWasCR);

  /**
   * Continues the stream parser if the charset switch failed.
   */
  void ContinueAfterFailedCharsetSwitch();

  void Terminate() { mTerminated = true; }

  void DropTimer();

  /**
   * Sets the URL for View Source title in case this parser ends up being
   * used for View Source. If aURL is a view-source: URL, takes the inner
   * URL. data: URLs are shown with an ellipsis instead of the actual data.
   */
  void SetViewSourceTitle(nsIURI* aURL);

 private:
  virtual ~nsHtml5StreamParser();

#ifdef DEBUG
  bool IsParserThread() { return mEventTarget->IsOnCurrentThread(); }
#endif

  void MarkAsBroken(nsresult aRv);

  /**
   * Marks the stream parser as interrupted. If you ever add calls to this
   * method, be sure to review Uninterrupt usage very, very carefully to
   * avoid having a previous in-flight runnable cancel your Interrupt()
   * call on the other thread too soon.
   */
  void Interrupt() {
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    mInterrupted = true;
  }

  void Uninterrupt() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    MOZ_ASSERT(IsParserThread(), "Wrong thread!");
    mTokenizerMutex.AssertCurrentThreadOwns();
    mInterrupted = false;
  }

  /**
   * Flushes the tree ops from the tree builder and disarms the flush
   * timer.
   */
  void FlushTreeOpsAndDisarmTimer();

  void SwitchDecoderIfAsciiSoFar(NotNull<const Encoding*> aEncoding)
      MOZ_REQUIRES(mTokenizerMutex);
  ;

  size_t CountGts();

  void DiscardMetaSpeculation();

  bool ProcessLookingForMetaCharset(bool aEof) MOZ_REQUIRES(mTokenizerMutex);

  void ParseAvailableData();

  void DoStopRequest();

  void DoDataAvailableBuffer(mozilla::Buffer<uint8_t>&& aBuffer)
      MOZ_REQUIRES(mTokenizerMutex);

  void DoDataAvailable(mozilla::Span<const uint8_t> aBuffer)
      MOZ_REQUIRES(mTokenizerMutex);

  static nsresult CopySegmentsToParser(nsIInputStream* aInStream,
                                       void* aClosure, const char* aFromSegment,
                                       uint32_t aToOffset, uint32_t aCount,
                                       uint32_t* aWriteCount)
      MOZ_REQUIRES(mTokenizerMutex);

  bool IsTerminatedOrInterrupted() { return mTerminated || mInterrupted; }

  bool IsTerminated() { return mTerminated; }

  /**
   * True when there is a Unicode decoder already
   */
  inline bool HasDecoder() { return !!mUnicodeDecoder; }

  /**
   * Returns 0 if 1) there aren't at least 2 buffers in mBufferedBytes
   * or 2) there is no byte '>' in the second buffer.
   * Otherwise, returns the length of the prefix of the second buffer
   * that is long enough to contain the first byte '>' in the second
   * buffer (including the '>' byte).
   */
  size_t LengthOfLtContainingPrefixInSecondBuffer();

  /**
   * Push bytes from network when there is no Unicode decoder yet
   */
  nsresult SniffStreamBytes(mozilla::Span<const uint8_t> aFromSegment,
                            bool aEof) MOZ_REQUIRES(mTokenizerMutex);

  /**
   * Push bytes from network when there is a Unicode decoder already
   */
  nsresult WriteStreamBytes(mozilla::Span<const uint8_t> aFromSegment)
      MOZ_REQUIRES(mTokenizerMutex);

  /**
   * Set up the Unicode decoder and write the sniffing buffer into it
   * followed by the current network buffer.
   *
   * @param aPrefix the part of the stream that has already been seen
   *                prior to aFromSegment. In practice, these are the
   *                bytes that are baked into the state of the BOM
   *                and UTF-16 XML declaration-like sniffing state
   *                machine state.
   * @param aFromSegment The current network buffer
   */
  nsresult SetupDecodingAndWriteSniffingBufferAndCurrentSegment(
      mozilla::Span<const uint8_t> aPrefix,
      mozilla::Span<const uint8_t> aFromSegment) MOZ_REQUIRES(mTokenizerMutex);

  /**
   * Initialize the Unicode decoder, mark the BOM as the source and
   * drop the sniffer.
   *
   * @param aDecoderCharsetName The name for the decoder's charset
   *                            (UTF-16BE, UTF-16LE or UTF-8; the BOM has
   *                            been swallowed)
   */
  void SetupDecodingFromBom(NotNull<const Encoding*> aEncoding);

  void SetupDecodingFromUtf16BogoXml(NotNull<const Encoding*> aEncoding);

  /**
   * When speculatively decoding from file: URL as UTF-8, commit
   * to UTF-8 as the non-speculative encoding and start processing
   * the decoded data.
   */
  [[nodiscard]] nsresult CommitLocalFileToEncoding();

  /**
   * When speculatively decoding from file: URL as UTF-8, redecode
   * the buffered bytes using fallback. The caller is responsible
   * for tokenizing the redecoded data.
   */
  [[nodiscard]] nsresult ReDecodeLocalFile() MOZ_REQUIRES(mTokenizerMutex);

  /**
   * When speculatively decoding from file: URL as UTF-8 and
   * LOCAL_FILE_UTF_8_BUFFER_SIZE bytes have been buffered, guess the
   * encoding and either commit to UTF-8 or redecode.
   */
  [[nodiscard]] nsresult CommitOrReDecodeLocalFile()
      MOZ_REQUIRES(mTokenizerMutex);

  /**
   * Potentially guess the encoding using mozilla::EncodingDetector.
   * Returns the guessed encoding and a telemetry-appropriate source.
   */
  std::tuple<NotNull<const Encoding*>, nsCharsetSource> GuessEncoding(
      bool aInitial);

  /**
   * Become confident or resolve and encoding name to its preferred form.
   * @param aEncoding the value of an internal encoding decl. Acts as an
   *                  out param, too, when the method returns true.
   * @return true if the parser needs to start using the new value of
   *         aEncoding and false if the parser became confident or if
   *         the encoding name did not specify a usable encoding
   */
  const Encoding* PreferredForInternalEncodingDecl(const nsAString& aEncoding);

  /**
   * Callback for mFlushTimer.
   */
  static void TimerCallback(nsITimer* aTimer, void* aClosure);

  /**
   * Parser thread entry point for (maybe) flushing the ops and posting
   * a flush runnable back on the main thread.
   */
  void TimerFlush();

  /**
   * Called when speculation fails.
   */
  void MaybeDisableFutureSpeculation() { mSpeculationFailureCount++; }

  /**
   * Used to check whether we're getting too many speculation failures and
   * should just stop trying.  The 100 is picked pretty randomly to be not too
   * small (so most pages are not affected) but small enough that we don't end
   * up with failed speculations over and over in pathological cases.
   */
  bool IsSpeculationEnabled() { return mSpeculationFailureCount < 100; }

  /**
   * Dispatch an event to a Quantum DOM main thread-ish thread.
   * (Not the parser thread.)
   */
  nsresult DispatchToMain(already_AddRefed<nsIRunnable> aRunnable);

  /**
   * Notify any devtools listeners about content newly received for parsing.
   */
  inline void OnNewContent(mozilla::Span<const char16_t> aData);

  /**
   * Notify any devtools listeners after all parse content has been received.
   */
  inline void OnContentComplete();

  nsCOMPtr<nsIRequest> mRequest;

  /**
   * The document title to use if this turns out to be a View Source parser.
   */
  nsCString mViewSourceTitle;

  /**
   * The Unicode decoder
   */
  mozilla::UniquePtr<mozilla::Decoder> mUnicodeDecoder;

  /**
   * BOM sniffing state
   */
  eBomState mBomState;

  // encoding-related stuff
  /**
   * The source (confidence) of the character encoding in use
   */
  nsCharsetSource mCharsetSource;

  nsCharsetSource mEncodingSwitchSource;

  /**
   * The character encoding in use
   */
  NotNull<const Encoding*> mEncoding;

  const Encoding* mNeedsEncodingSwitchTo;

  bool mSeenEligibleMetaCharset;

  bool mChardetEof;

#ifdef DEBUG

  bool mStartedFeedingDetector;

  bool mStartedFeedingDevTools;

#endif

  /**
   * Whether reparse is forbidden
   */
  bool mReparseForbidden;

  /**
   * Whether the Repair Text Encoding menu item was invoked
   */
  bool mForceAutoDetection;

  /**
   * Whether there was a valid charset parameter on the HTTP layer.
   */
  bool mChannelHadCharset;

  /**
   * We are in the process of looking for <meta charset>
   */
  bool mLookingForMetaCharset;

  /**
   * Whether the byte stream started with ASCII <?
   */
  bool mStartsWithLtQuestion;

  /**
   * If we are viewing XML source and are waiting for a '>' form the network.
   */
  bool mLookingForXmlDeclarationForXmlViewSource;

  /**
   * Whether template has been pushed or head popped within the first 1024
   * bytes.
   */
  bool mTemplatePushedOrHeadPopped;

  // Portable parser objects
  /**
   * The first buffer in the pending UTF-16 buffer queue
   */
  RefPtr<nsHtml5OwningUTF16Buffer> mFirstBuffer;

  /**
   * Non-owning pointer to the most recent buffer that contains the most recent
   * remembered greater-than sign. Used only while mLookingForMetaCharset is
   * true. While mLookingForMetaCharset is true, mFirstBuffer is not changed and
   * keeps the whole linked list of buffers alive. This pointer is non-owning to
   * avoid frequent refcounting.
   */
  nsHtml5OwningUTF16Buffer* mGtBuffer;

  int32_t mGtPos;

  /**
   * The last buffer in the pending UTF-16 buffer queue
   */
  nsHtml5OwningUTF16Buffer*
      mLastBuffer;  // weak ref; always points to
                    // a buffer of the size
                    // NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE

  /**
   * The first buffer of the document if looking for <meta charset> or
   * nullptr afterwards.
   */
  RefPtr<nsHtml5OwningUTF16Buffer> mFirstBufferOfMetaScan;

  /**
   * The tree operation executor
   */
  nsHtml5TreeOpExecutor* mExecutor;

  /**
   * The HTML5 tree builder
   */
  mozilla::UniquePtr<nsHtml5TreeBuilder> mTreeBuilder;

  /**
   * The HTML5 tokenizer
   */
  mozilla::UniquePtr<nsHtml5Tokenizer> mTokenizer;

  /**
   * Makes sure the main thread can't mess the tokenizer state while it's
   * tokenizing. This mutex also protects the current speculation.
   */
  mozilla::Mutex mTokenizerMutex;

  /**
   * The scoped atom table
   */
  nsHtml5AtomTable mAtomTable;

  /**
   * The owner parser.
   */
  RefPtr<nsHtml5Parser> mOwner;

  /**
   * Whether the last character tokenized was a carriage return (for CRLF)
   */
  bool mLastWasCR;

  /**
   * For tracking stream life cycle
   */
  eHtml5StreamState mStreamState;

  /**
   * Whether we are speculating.
   */
  bool mSpeculating;

  /**
   * Whether the tokenizer has reached EOF. (Reset when stream rewinded.)
   */
  bool mAtEOF;

  /**
   * The speculations. The mutex protects the nsTArray itself.
   * To access the queue of current speculation, mTokenizerMutex must be
   * obtained.
   * The current speculation is the last element
   */
  nsTArray<mozilla::UniquePtr<nsHtml5Speculation>> mSpeculations;
  mozilla::Mutex mSpeculationMutex;

  /**
   * Number of times speculation has failed for this parser.
   */
  mozilla::Atomic<uint32_t> mSpeculationFailureCount;

  /**
   * Number of bytes already buffered into mBufferedBytes.
   */
  uint32_t mNumBytesBuffered;

  nsTArray<mozilla::Buffer<uint8_t>> mBufferedBytes;

  /**
   * True to terminate early.
   */
  mozilla::Atomic<bool> mTerminated;

  /**
   * True to release mTokenizerMutex early.
   */
  mozilla::Atomic<bool> mInterrupted;

  /**
   * The thread this stream parser runs on.
   */
  nsCOMPtr<nsISerialEventTarget> mEventTarget;

  nsCOMPtr<nsIRunnable> mExecutorFlusher;

  nsCOMPtr<nsIRunnable> mLoadFlusher;

  /**
   * This runnable is distinct from the regular flushers to
   * signal the intent of encoding commitment without having to
   * protect mPendingEncodingCommitment in the executer with a
   * mutex.
   */
  nsCOMPtr<nsIRunnable> mEncodingCommitter;

  /**
   * The generict detector.
   */
  mozilla::UniquePtr<mozilla::EncodingDetector> mDetector;

  /**
   * The TLD we're loading from or empty if unknown.
   */
  nsCString mTLD;

  /**
   * Whether the initial charset source was kCharsetFromParentFrame
   */
  bool mInitialEncodingWasFromParentFrame;

  bool mHasHadErrors;

  bool mDetectorHasSeenNonAscii;

  /**
   * If true, we are decoding a local file that lacks an encoding
   * declaration and we are not tokenizing yet.
   */
  bool mDecodingLocalFileWithoutTokenizing;

  /**
   * Whether we are keeping the incoming bytes.
   */
  bool mBufferingBytes;

  /**
   * Timer for flushing tree ops once in a while when not speculating.
   */
  nsCOMPtr<nsITimer> mFlushTimer;

  /**
   * Mutex for protecting access to mFlushTimer (but not for the two
   * mFlushTimerFoo booleans below).
   */
  mozilla::Mutex mFlushTimerMutex;

  /**
   * Keeps track whether mFlushTimer has been armed. Unfortunately,
   * nsITimer doesn't enable querying this from the timer itself.
   */
  bool mFlushTimerArmed;

  /**
   * False initially and true after the timer has fired at least once.
   */
  bool mFlushTimerEverFired;

  /**
   * Whether the parser is doing a normal parse, view source or plain text.
   */
  eParserMode mMode;

  /**
   * If the associated docshell is being watched by the devtools, this is
   * set to the URI associated with the parse. All parse data is sent to the
   * devtools, along with this URI. This URI is cleared out after the parse has
   * been marked as completed.
   */
  nsCOMPtr<nsIURI> mURIToSendToDevtools;

  /**
   * If content is being sent to the devtools, an encoded UUID for the parser.
   */
  nsString mUUIDForDevtools;

  /**
   * The browser element's Id for the currently parsed document communicated to
   * devtools.
   */
  uint64_t mBrowserIdForDevtools;

  /**
   * The BrowsingContext ID for the currently parsed document communicated to
   * devtools.
   */
  uint64_t mBrowsingContextIDForDevtools;

  /**
   * prevent multiple calls to OnStopRequest
   * This field can be called from multiple threads and is protected by
   * nsHtml5StreamListener::mDelegateMonitor passed in the OnStopRequest
   */
  bool mOnStopCalled{false};
};

#endif  // nsHtml5StreamParser_h
