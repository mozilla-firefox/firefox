/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ScriptLoadContext_h
#define mozilla_dom_ScriptLoadContext_h

#include "js/AllocPolicy.h"
#include "js/ColumnNumber.h"    // JS::ColumnNumberOneOrigin
#include "js/CompileOptions.h"  // JS::OwningCompileOptions
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/Transcoding.h"  // JS::TranscodeResult
#include "js/TypeDecls.h"
#include "js/WasmModule.h"  // JS::ESMCompileResult, JS::SharedWasmCompileArgs
#include "js/experimental/JSStencil.h"  // JS::FrontendContext, JS::Stencil, JS::InstantiationStorage
#include "js/loader/LoadContextBase.h"
#include "js/loader/ScriptKind.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/CORSMode.h"
#include "mozilla/Mutex.h"
#include "mozilla/PreloaderBase.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TaskController.h"  // mozilla::Task
#include "mozilla/Utf8.h"            // mozilla::Utf8Unit
#include "mozilla/Vector.h"
#include "mozilla/dom/SRIMetadata.h"
#include "mozilla/net/UrlClassifierCommon.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIClassifiedChannel.h"
#include "nsIScriptElement.h"

class nsICacheInfoChannel;
struct JSContext;

namespace mozilla::dom {

class Element;

/*
 * DOM specific ScriptLoadContext.
 *
 * ScriptLoadContexts augment the loading of a ScriptLoadRequest. They
 * describe how a ScriptLoadRequests loading and evaluation needs to be
 * augmented, based on the information provided by the loading context. In
 * the case of the DOM, the ScriptLoadContext is used to identify how a script
 * should be loaded according to information found in the HTML document into
 * which it will be loaded. The following fields describe how the
 * ScriptLoadRequest will be loaded.
 *
 *    * mScriptMode
 *        stores the mode (Async, Sync, Deferred), and preload, which
 *        allows the ScriptLoader to decide if the script should be pushed
 *        offThread, or if the preloaded request should be used.
 *    * mScriptFromHead
 *        Set when the script tag is in the head, and should be treated as
 *        a blocking script
 *    * mIsInline
 *        Set for scripts whose bodies are inline in the html. In this case,
 *        the script does not need to be fetched first.
 *    * mIsXSLT
 *        Set if we are in an XSLT request.
 *    * mIsPreload
 *        Set for scripts that are preloaded in a
 *        <link rel="preload" as="script"> or <link rel="modulepreload">
 *        element.
 *
 * In addition to describing how the ScriptLoadRequest will be loaded by the
 * DOM ScriptLoader, the ScriptLoadContext contains fields that facilitate
 * those custom behaviors, including support for offthread parsing and preload
 * element specific controls.
 *
 */

class ScriptDecodeTask;
class StencilCompileOrDecodeTask;
class WasmCompileTask;

// Base class for tasks which perform off-thread compilation.
class CompileOrDecodeTask : public mozilla::Task {
 protected:
  enum class Type : uint8_t { Compile, Decode, Wasm };

  explicit CompileOrDecodeTask(Type aType);
  virtual ~CompileOrDecodeTask() = default;

  // Performs the compilation or decode. Not called if already cancelled.
  virtual TaskResult RunTask() MOZ_REQUIRES(mMutex) = 0;

  // Called by Cancel to abort a task which may already be running.
  virtual void CancelTask() {}

 public:
  TaskResult Run() final;

  // Cancel the task, discarding its result. One that has already started
  // aborts off-thread where it can, so this only blocks once threads shut down.
  //
  // Called on the main thread by MaybeCancelOffThreadScript, at most once per
  // task, after which the result must not be taken.
  void Cancel() MOZ_EXCLUDES(mMutex);

  // Releases cancelled tasks which have since finished, ahead of shutdown.
  static void ForgetFinishedCancelledTasks();

  bool IsStencilTask() const {
    return mType == Type::Compile || mType == Type::Decode;
  }
  bool IsDecodeTask() const { return mType == Type::Decode; }
  bool IsWasmTask() const { return mType == Type::Wasm; }

  inline StencilCompileOrDecodeTask* AsStencilCompileOrDecodeTask();
  inline WasmCompileTask* AsWasmCompileTask();
  ScriptDecodeTask* AsScriptDecodeTask();

 protected:
  // Held while the task is running, so that a cancelled task can be waited for.
  mozilla::Mutex mMutex;

  mozilla::Atomic<bool> mIsCancelled{false};

 private:
  // Remembers this task so that it is waited for at shutdown.
  void TrackCancelled() MOZ_EXCLUDES(mMutex);

  // Creates the list on first use, and registers the wait at shutdown.
  static void EnsureCancelledTasksList();

  // Blocks until a running task has finished.
  void WaitForRunningTask() MOZ_EXCLUDES(mMutex);

  // False once the task has run to completion, or been skipped as cancelled.
  bool MayStillRun() const { return mMayStillRun; }

  mozilla::Atomic<bool> mMayStillRun{true};

  const Type mType;
};

// Base class for the off-thread compile or off-thread decode tasks which
// produce a JS::Stencil.
class StencilCompileOrDecodeTask : public CompileOrDecodeTask {
 protected:
  explicit StencilCompileOrDecodeTask(Type aType);
  virtual ~StencilCompileOrDecodeTask();

  nsresult InitFrontendContext();

  void CancelTask() override;

  void DidRunTask(RefPtr<JS::Stencil>&& aStencil) MOZ_REQUIRES(mMutex);

 public:
  // Returns the result of the compilation or decode if it was successful.
  // Returns nullptr otherwise, and sets pending exception on JSContext.
  //
  // aInstantiationStorage receives the storage allocated off main thread
  // on successful case.
  already_AddRefed<JS::Stencil> StealResult(
      JSContext* aCx, JS::InstantiationStorage* aInstantiationStorage);

  // The bytes a decode task was given, to hand back to the LoadedScript.
  // Not on ScriptDecodeTask, which is local to ScriptLoader.cpp.
  JS::TranscodeBuffer TakeSRIAndSerializedStencil();

 protected:
  // The result of decode task, to distinguish throwing case and decode error.
  JS::TranscodeResult mResult = JS::TranscodeResult::Ok;

  // An option used to compile the code, or the equivalent for decode.
  // This holds the filename pointed by errors reported to JS::FrontendContext.
  JS::OwningCompileOptions mOptions;

  // Owning-pointer for the context associated with the script compilation.
  //
  // The context is allocated on main thread in InitFrontendContext method,
  // and is freed on any thread in the destructor.
  JS::FrontendContext* mFrontendContext = nullptr;

 private:
  // The result of the compilation or decode.
  RefPtr<JS::Stencil> mStencil;

  JS::InstantiationStorage mInstantiationStorage;
};

// Off-thread compile task for a wasm module used as an ES module.
class WasmCompileTask final : public CompileOrDecodeTask {
 public:
  using WasmBytesBuffer = mozilla::Vector<uint8_t, 0, js::MallocAllocPolicy>;

  explicit WasmCompileTask(WasmBytesBuffer&& aBytes)
      : CompileOrDecodeTask(Type::Wasm), mBytes(std::move(aBytes)) {}

  nsresult Init(JSContext* aCx, JS::CompileOptions& aOptions);

  TaskResult RunTask() override MOZ_REQUIRES(mMutex);

  // Sets aModuleOut to the module record for the compiled module.
  // Returns false otherwise, and sets pending exception on JSContext.
  bool StealResult(JSContext* aCx, JS::MutableHandle<JSObject*> aModuleOut);

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("WasmCompileTask");
    return true;
  }
#endif

 private:
  JS::SharedWasmCompileArgs mCompileArgs;

  // The result of the compilation, along with any error and warnings, which
  // can only be reported once back on the main thread.
  JS::ESMCompileResult mCompileResult;

  WasmBytesBuffer mBytes;
};

StencilCompileOrDecodeTask*
CompileOrDecodeTask::AsStencilCompileOrDecodeTask() {
  MOZ_ASSERT(IsStencilTask());
  return static_cast<StencilCompileOrDecodeTask*>(this);
}

WasmCompileTask* CompileOrDecodeTask::AsWasmCompileTask() {
  MOZ_ASSERT(IsWasmTask());
  return static_cast<WasmCompileTask*>(this);
}

class ScriptLoadContext : public JS::loader::LoadContextBase,
                          public PreloaderBase {
 protected:
  virtual ~ScriptLoadContext();

 public:
  explicit ScriptLoadContext(nsIScriptElement* aScriptElement = nullptr,
                             const nsAString& aSourceText = VoidString());

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ScriptLoadContext,
                                           JS::loader::LoadContextBase)

  static void PrioritizeAsPreload(nsIChannel* aChannel);

  bool IsPreload() const override;

  bool CompileStarted() const;

  net::ClassificationFlags& GetClassificationFlags() {
    return mClassificationFlags;
  }
  void SetClassificationFlags(
      const net::ClassificationFlags& aClassificationFlags) {
    mClassificationFlags = aClassificationFlags;
  }

  void BlockOnload(Document* aDocument);

  void MaybeUnblockOnload();

  // Set for a <link rel=modulepreload> whose module is fetching, fetched or
  // cached, i.e. one that doesn't create a channel to start a network request,
  // and so has to report its own result through
  // NotifyPreloadCoalescingResult(). See ScriptLoader::NotifyPreloadCoalescing.
  void SetIsCoalescedModulePreload() { mIsCoalescedModulePreload = true; }

  // Called by the module loader when this request stopped waiting on an
  // in-progress fetch of the same URL. Only a coalesced module preload has
  // anything to report at that point.
  void NotifyModuleWaitFinished() {
    if (mIsCoalescedModulePreload) {
      NotifyPreloadCoalescingResult();
    }
  }

  // https://html.spec.whatwg.org/multipage/links.html#link-type-modulepreload
  //
  // Fires the load/error event of a coalesced module preload from the top-level
  // module's result. Fires nothing while the module is still fetching; the
  // caller notifies us again once the fetch resolves or is canceled.
  void NotifyPreloadCoalescingResult();

  enum class ScriptMode : uint8_t {
    eBlocking,
    eDeferred,
    eAsync,
    eLinkPreload  // this is a load initiated by <link rel="preload"
                  // as="script"> or <link rel="modulepreload"> tag
  };

  void SetScriptMode(bool aDeferAttr, bool aAsyncAttr, bool aLinkPreload);

  bool IsLinkPreloadScript() const {
    return mScriptMode == ScriptMode::eLinkPreload;
  }

  bool IsBlockingScript() const { return mScriptMode == ScriptMode::eBlocking; }

  bool IsDeferredScript() const { return mScriptMode == ScriptMode::eDeferred; }

  bool IsAsyncScript() const { return mScriptMode == ScriptMode::eAsync; }

  // Accessors for the script element, for each purpose.
  //
  // The script element reference is guaranteed to be available only for:
  //   * inline/external classic script
  //   * inline/external top-level module
  //
  // The reference is valid only for specific purpose explained below.

  // For aLoadingNode parameter of a new channel.
  // TODO: This is basically unnecessary and a document can be used instead.
  //       Remove this.
  inline nsIScriptElement* GetScriptElementForLoadingNode() const {
    MOZ_ASSERT(mScriptElement);
    return mScriptElement;
  }

  // For TRACE_FOR_TEST macros.
  // NOTE: This is called also for imported modules.
  //       The consumer allows nullptr.
  inline nsIScriptElement* GetScriptElementForTrace() const {
    return mScriptElement;
  }

  // For ScriptLoader::mCurrentParserInsertedScript.
  inline nsIScriptElement* GetScriptElementForCurrentParserInsertedScript()
      const {
    MOZ_ASSERT(mScriptElement);
    return mScriptElement;
  }

  // For nsIScriptLoaderObserver.
  inline nsIScriptElement* GetScriptElementForObserver() const {
    MOZ_ASSERT(mScriptElement);
    return mScriptElement;
  }

  // For URL classifier.
  inline nsIScriptElement* GetScriptElementForUrlClassifier() const {
    return mScriptElement;
  }

  // For AutoCurrentScriptUpdater.
  // This is valid only for classic script.
  inline nsIScriptElement* GetScriptElementForCurrentScript() const {
    MOZ_ASSERT(mScriptElement);
    return mScriptElement;
  }

  bool HasScriptElement() const;

  void GetInlineScriptText(nsAString& aText) const;

  void GetHintCharset(nsAString& aCharset) const;

  // TODO: Reimplement with mLineNo/mColumnNo.
  uint32_t GetScriptLineNumber() const;
  JS::ColumnNumberOneOrigin GetScriptColumnNumber() const;

  void BeginEvaluatingTopLevel() const;
  void EndEvaluatingTopLevel() const;

  void UnblockParser() const;
  void ContinueParserAsync() const;

  Document* GetScriptOwnerDocument() const;

  // Make this request a preload (speculative) request.
  void SetIsPreloadRequest() {
    MOZ_ASSERT(!HasScriptElement());
    MOZ_ASSERT(!IsPreload());
    mIsPreload = true;
  }

  // Make a preload request into an actual load request for the given element.
  void SetIsLoadRequest(nsIScriptElement* aElement);

  FromParser GetParserCreated() const {
    if (!mScriptElement) {
      return NOT_FROM_PARSER;
    }
    return mScriptElement->GetParserCreated();
  }

  // Used to output a string for the Gecko Profiler.
  void GetProfilerLabel(nsACString& aOutString) override;

  void MaybeCancelOffThreadScript();

  // Finish the off-main-thread compilation and return the result, or
  // convert the compilation error to runtime error.
  already_AddRefed<JS::Stencil> StealOffThreadResult(
      JSContext* aCx, JS::InstantiationStorage* aInstantiationStorage);

  // Sets aModuleOut to the module record for the compiled wasm module.
  // Returns false otherwise, and sets pending exception on JSContext.
  bool StealOffThreadWasmResult(JSContext* aCx,
                                JS::MutableHandle<JSObject*> aModuleOut);

  ScriptMode mScriptMode;  // Whether this is a blocking, defer or async script.
  bool mScriptFromHead;    // Synchronous head script block loading of other non
                           // js/css content.
  bool mIsInline;          // Is the script inline or loaded?
  bool mInDeferList;       // True if we live in mDeferRequests.
  bool mInAsyncList;       // True if we live in mLoadingAsyncRequests or
                           // mLoadedAsyncRequests.
  bool mIsNonAsyncScriptInserted;  // True if we live in
                                   // mNonAsyncExternalScriptInsertedRequests
  bool mIsXSLT;                    // True if we live in mXSLTRequests.
  bool mInCompilingList;  // True if we are in mOffThreadCompilingRequests.
  bool mWasCompiledOMT;   // True if the script has been compiled off main
                          // thread.
  // Set on preloading scripts or modules.
  bool mIsPreload;

  // Set on a coalesced <link rel=modulepreload> request, i.e. the preloading
  // module is already fetching, or fetched, or cached. Unlike the eLinkPreload
  // script mode, this isn't cleared when a <script> element steals the preload,
  // because the element that coalesced onto it is still waiting for its event.
  bool mIsCoalescedModulePreload;

  // Set when a navigation away from the document cancelled the load.
  bool mWasAbortedByNavigation;

  // For preload requests, we defer reporting errors to the console until the
  // request is used.
  nsresult mUnreportedPreloadError;

  uint32_t mLineNo;
  JS::ColumnNumberOneOrigin mColumnNo;

  // Classification flags of the source of the script.
  net::ClassificationFlags mClassificationFlags;

  // Task that performs off-thread compilation or off-thread decode.
  // This field is used to take the result of the task, or cancel the task.
  //
  // Set to non-null on the task creation, and set to null when taking the
  // result or cancelling the task.
  RefPtr<CompileOrDecodeTask> mCompileOrDecodeTask;

  // Non-null if there is a document that this request is blocking from loading.
  RefPtr<Document> mLoadBlockedDocument;

  // The script element which trigerred this script load.
  // This is valid only for classic script and top-level module script.
  nsCOMPtr<nsIScriptElement> mScriptElement;

  nsString mSourceText;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ScriptLoadContext_h
