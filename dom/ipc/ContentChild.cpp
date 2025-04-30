/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidDecoderModule.h"
#endif

#include "BrowserChild.h"
#include "nsNSSComponent.h"
#include "ContentChild.h"
#include "GeckoProfiler.h"
#include "HandlerServiceChild.h"
#include "nsXPLookAndFeel.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Attributes.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/FOGIPC.h"
#include "GMPServiceChild.h"
#include "Geolocation.h"
#include "imgLoader.h"
#include "ScrollingMetrics.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClipboardContentAnalysisChild.h"
#include "mozilla/ClipboardReadRequestChild.h"
#include "mozilla/Components.h"
#include "mozilla/HangDetails.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MemoryTelemetry.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PerfStats.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessHangMonitorIPC.h"
#include "mozilla/RemoteDecoderManagerChild.h"
#include "mozilla/RemoteLazyInputStreamChild.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SharedStyleSheetCache.h"
#include "mozilla/dom/SharedScriptCache.h"
#include "mozilla/SimpleEnumerator.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPrefs_threads.h"
#include "mozilla/StorageAccessAPIHelper.h"
#include "mozilla/TelemetryIPC.h"
#include "mozilla/Unused.h"
#include "mozilla/WebBrowserPersistDocumentChild.h"
#include "mozilla/devtools/HeapSnapshotTempFileHelperChild.h"
#include "mozilla/dom/AutoSuppressEventHandlingAndSuspend.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/BrowserBridgeHost.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/ChildProcessChannelListener.h"
#include "mozilla/dom/ChildProcessMessageManager.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/ContentPlaybackController.h"
#include "mozilla/dom/ContentProcessMessageManager.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/ExternalHelperAppChild.h"
#include "mozilla/dom/GetFilesHelper.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/InProcessChild.h"
#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/JSProcessActorBinding.h"
#include "mozilla/dom/JSProcessActorChild.h"
#include "mozilla/dom/LSObject.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/dom/PSessionStorageObserverChild.h"
#include "mozilla/dom/PostMessageEvent.h"
#include "mozilla/dom/PushNotifier.h"
#include "mozilla/dom/RemoteWorkerDebuggerManagerChild.h"
#include "mozilla/dom/RemoteWorkerService.h"
#include "mozilla/dom/ScreenOrientation.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/dom/URLClassifierChild.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WorkerDebugger.h"
#include "mozilla/dom/WorkerDebuggerManager.h"
#include "mozilla/dom/ipc/SharedMap.h"
#include "mozilla/extensions/ExtensionsChild.h"
#include "mozilla/extensions/StreamFilterParent.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/hal_sandbox/PHalChild.h"
#include "mozilla/intl/L10nRegistry.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/FileDescriptorUtils.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/TestShellChild.h"
#include "mozilla/layers/APZChild.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/ContentProcessController.h"
#include "mozilla/layers/ImageBridgeChild.h"
#ifdef NS_PRINTING
#  include "mozilla/layout/RemotePrintJobChild.h"
#endif
#include "mozilla/loader/ScriptCacheActors.h"
#include "mozilla/media/MediaChild.h"
#include "mozilla/net/CaptivePortalService.h"
#include "mozilla/net/ChildDNSService.h"
#include "mozilla/net/CookieServiceChild.h"
#include "mozilla/net/DocumentChannelChild.h"
#include "mozilla/net/HttpChannelChild.h"
#include "mozilla/widget/RemoteLookAndFeel.h"
#include "mozilla/widget/ScreenManager.h"
#include "mozilla/widget/WidgetMessageUtils.h"
#include "nsBaseDragService.h"
#include "nsDocShellLoadTypes.h"
#include "nsFocusManager.h"
#include "nsHttpHandler.h"
#include "nsIConsoleService.h"
#include "nsIInputStreamChannel.h"
#include "nsILayoutHistoryState.h"
#include "nsILoadGroup.h"
#include "nsIOpenWindowInfo.h"
#include "nsISimpleEnumerator.h"
#include "nsIStringBundle.h"
#include "nsIURIMutator.h"
#include "nsQueryObject.h"
#include "nsRefreshDriver.h"
#include "nsSandboxFlags.h"
#include "mozmemory.h"

#include "ChildProfilerController.h"

#if defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#  if defined(XP_WIN)
#    include "mozilla/sandboxTarget.h"
#    include "mozilla/ProcInfo.h"
#  elif defined(XP_LINUX)
#    include "CubebUtils.h"
#    include "mozilla/Sandbox.h"
#    include "mozilla/SandboxInfo.h"
#    include "mozilla/SandboxProfilerObserver.h"
#  elif defined(XP_MACOSX)
#    include <CoreGraphics/CGError.h>
#    include "mozilla/Sandbox.h"
#  elif defined(__OpenBSD__)
#    include <err.h>
#    include <sys/stat.h>
#    include <unistd.h>

#    include <fstream>

#    include "BinaryPath.h"
#    include "SpecialSystemDirectory.h"
#    include "nsILineInputStream.h"
#    include "mozilla/ipc/UtilityProcessSandboxing.h"
#  endif
#  if defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#    include "mozilla/SandboxTestingChild.h"
#  endif
#endif

#include "SandboxHal.h"
#include "mozInlineSpellChecker.h"
#include "mozilla/GlobalStyleSheetCache.h"
#include "nsAnonymousTemporaryFile.h"
#include "nsCategoryManagerUtils.h"
#include "nsClipboardProxy.h"
#include "nsContentPermissionHelper.h"
#include "nsDebugImpl.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsHashPropertyBag.h"
#include "nsIConsoleListener.h"
#include "nsICycleCollectorListener.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDocumentViewer.h"
#include "nsIDragService.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIMemoryInfoDumper.h"
#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "nsIOService.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsJSEnvironment.h"
#include "nsJSUtils.h"
#include "nsMemoryInfoDumper.h"
#include "nsServiceManagerUtils.h"
#include "nsStyleSheetService.h"
#include "nsThreadManager.h"
#include "nsXULAppAPI.h"
#include "IHistory.h"
#include "ReferrerInfo.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/task.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/PCycleCollectWithLogsChild.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "nsChromeRegistryContent.h"
#include "nsFrameMessageManager.h"
#include "nsNetUtil.h"
#include "nsWindowMemoryReporter.h"

#ifdef MOZ_WEBRTC
#  include "jsapi/WebrtcGlobalChild.h"
#endif

#include "PermissionMessageUtils.h"
#include "mozilla/Permission.h"
#include "mozilla/PermissionManager.h"

#if defined(MOZ_WIDGET_ANDROID)
#  include "APKOpen.h"
#  include <sched.h>
#endif

#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#  include "mozilla/WinDllServices.h"
#endif

#if defined(XP_MACOSX)
#  include "nsMacUtilsImpl.h"
#  include <sys/qos.h>
#endif /* XP_MACOSX */

#ifdef MOZ_X11
#  include "mozilla/X11Util.h"
#endif

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#  ifdef XP_WIN
#    include "mozilla/a11y/AccessibleWrap.h"
#  endif
#  include "mozilla/a11y/DocAccessible.h"
#  include "mozilla/a11y/DocManager.h"
#  include "mozilla/a11y/OuterDocAccessible.h"
#endif

#include "mozilla/dom/File.h"
#include "mozilla/dom/MediaControllerBinding.h"

#ifdef MOZ_WEBSPEECH
#  include "mozilla/dom/PSpeechSynthesisChild.h"
#endif

#include "ClearOnShutdown.h"
#include "DomainPolicy.h"
#include "GfxInfoBase.h"
#include "MMPrinter.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "mozilla/ipc/URIUtils.h"
#include "VRManagerChild.h"
#include "gfxPlatform.h"
#include "gfxPlatformFontList.h"
#include "mozilla/RemoteSpellCheckEngineChild.h"
#include "mozilla/dom/TabContext.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/net/NeckoMessageUtils.h"
#include "mozilla/widget/PuppetBidiKeyboard.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsString.h"
#include "nscore.h"  // for NS_FREE_PERMANENT_DATA
#include "private/pprio.h"

#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#  include "nsAppRunner.h"
#  include <gtk/gtk.h>
#endif

#ifdef MOZ_CODE_COVERAGE
#  include "mozilla/CodeCoverageHandler.h"
#endif

extern mozilla::LazyLogModule gSHIPBFCacheLog;

using namespace mozilla;
using namespace mozilla::dom::ipc;
using namespace mozilla::media;
using namespace mozilla::embedding;
using namespace mozilla::gmp;
using namespace mozilla::hal_sandbox;
using namespace mozilla::ipc;
using namespace mozilla::intl;
using namespace mozilla::layers;
using namespace mozilla::layout;
using namespace mozilla::net;
using namespace mozilla::widget;
using mozilla::loader::PScriptCacheChild;

namespace geckoprofiler::markers {
struct ProcessPriorityChange {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("ProcessPriorityChange");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aPreviousPriority,
                                   const ProfilerString8View& aNewPriority) {
    aWriter.StringProperty("Before", aPreviousPriority);
    aWriter.StringProperty("After", aNewPriority);
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("Before", MS::Format::String);
    schema.AddKeyFormat("After", MS::Format::String);
    schema.AddStaticLabelValue("Note",
                               "This is a notification of the priority change "
                               "that was done by the parent process");
    schema.SetAllLabels(
        "priority: {marker.data.Before} -> {marker.data.After}");
    return schema;
  }
};

struct ProcessPriority {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("ProcessPriority");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aPriority,
                                   const ProfilingState& aProfilingState) {
    aWriter.StringProperty("Priority", aPriority);
    aWriter.StringProperty("Marker cause",
                           ProfilerString8View::WrapNullTerminatedString(
                               ProfilingStateToString(aProfilingState)));
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("Priority", MS::Format::String);
    schema.AddKeyFormat("Marker cause", MS::Format::String);
    schema.SetAllLabels("priority: {marker.data.Priority}");
    return schema;
  }
};
}  // namespace geckoprofiler::markers

namespace mozilla {
namespace dom {

// IPC sender for remote GC/CC logging.
class CycleCollectWithLogsChild final : public PCycleCollectWithLogsChild {
 public:
  NS_INLINE_DECL_REFCOUNTING(CycleCollectWithLogsChild)

  class Sink final : public nsICycleCollectorLogSink {
    NS_DECL_ISUPPORTS

    Sink(CycleCollectWithLogsChild* aActor, const FileDescriptor& aGCLog,
         const FileDescriptor& aCCLog) {
      mActor = aActor;
      mGCLog = FileDescriptorToFILE(aGCLog, "w");
      mCCLog = FileDescriptorToFILE(aCCLog, "w");
    }

    NS_IMETHOD Open(FILE** aGCLog, FILE** aCCLog) override {
      if (NS_WARN_IF(!mGCLog) || NS_WARN_IF(!mCCLog)) {
        return NS_ERROR_FAILURE;
      }
      *aGCLog = mGCLog;
      *aCCLog = mCCLog;
      return NS_OK;
    }

    NS_IMETHOD CloseGCLog() override {
      MOZ_ASSERT(mGCLog);
      fclose(mGCLog);
      mGCLog = nullptr;
      mActor->SendCloseGCLog();
      return NS_OK;
    }

    NS_IMETHOD CloseCCLog() override {
      MOZ_ASSERT(mCCLog);
      fclose(mCCLog);
      mCCLog = nullptr;
      mActor->SendCloseCCLog();
      return NS_OK;
    }

    NS_IMETHOD GetFilenameIdentifier(nsAString& aIdentifier) override {
      return UnimplementedProperty();
    }

    NS_IMETHOD SetFilenameIdentifier(const nsAString& aIdentifier) override {
      return UnimplementedProperty();
    }

    NS_IMETHOD GetProcessIdentifier(int32_t* aIdentifier) override {
      return UnimplementedProperty();
    }

    NS_IMETHOD SetProcessIdentifier(int32_t aIdentifier) override {
      return UnimplementedProperty();
    }

    NS_IMETHOD GetGcLog(nsIFile** aPath) override {
      return UnimplementedProperty();
    }

    NS_IMETHOD GetCcLog(nsIFile** aPath) override {
      return UnimplementedProperty();
    }

   private:
    ~Sink() {
      if (mGCLog) {
        fclose(mGCLog);
        mGCLog = nullptr;
      }
      if (mCCLog) {
        fclose(mCCLog);
        mCCLog = nullptr;
      }
      // The XPCOM refcount drives the IPC lifecycle;
      Unused << mActor->Send__delete__(mActor);
    }

    nsresult UnimplementedProperty() {
      MOZ_ASSERT(false,
                 "This object is a remote GC/CC logger;"
                 " this property isn't meaningful.");
      return NS_ERROR_UNEXPECTED;
    }

    RefPtr<CycleCollectWithLogsChild> mActor;
    FILE* mGCLog;
    FILE* mCCLog;
  };

 private:
  ~CycleCollectWithLogsChild() = default;
};

NS_IMPL_ISUPPORTS(CycleCollectWithLogsChild::Sink, nsICycleCollectorLogSink);

class ConsoleListener final : public nsIConsoleListener {
 public:
  explicit ConsoleListener(ContentChild* aChild) : mChild(aChild) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSICONSOLELISTENER

 private:
  ~ConsoleListener() = default;

  ContentChild* mChild;
  friend class ContentChild;
};

NS_IMPL_ISUPPORTS(ConsoleListener, nsIConsoleListener)

// Before we send the error to the parent process (which
// involves copying the memory), truncate any long lines.  CSS
// errors in particular share the memory for long lines with
// repeated errors, but the IPC communication we're about to do
// will break that sharing, so we better truncate now.
template <typename CharT>
static void TruncateString(nsTSubstring<CharT>& aString) {
  if (aString.Length() > 1000) {
    aString.Truncate(1000);
  }
}

NS_IMETHODIMP
ConsoleListener::Observe(nsIConsoleMessage* aMessage) {
  if (!mChild) {
    return NS_OK;
  }

  nsCOMPtr<nsIScriptError> scriptError = do_QueryInterface(aMessage);
  if (scriptError) {
    nsAutoString msg;
    nsAutoCString sourceName;
    nsCString category;
    uint32_t lineNum, colNum, flags;
    bool fromPrivateWindow, fromChromeContext;

    nsresult rv = scriptError->GetErrorMessage(msg);
    NS_ENSURE_SUCCESS(rv, rv);
    TruncateString(msg);
    rv = scriptError->GetSourceName(sourceName);
    NS_ENSURE_SUCCESS(rv, rv);
    TruncateString(sourceName);

    rv = scriptError->GetCategory(getter_Copies(category));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = scriptError->GetLineNumber(&lineNum);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = scriptError->GetColumnNumber(&colNum);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = scriptError->GetFlags(&flags);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = scriptError->GetIsFromPrivateWindow(&fromPrivateWindow);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = scriptError->GetIsFromChromeContext(&fromChromeContext);
    NS_ENSURE_SUCCESS(rv, rv);

    {
      AutoJSAPI jsapi;
      jsapi.Init();
      JSContext* cx = jsapi.cx();

      JS::Rooted<JS::Value> stack(cx);
      rv = scriptError->GetStack(&stack);
      NS_ENSURE_SUCCESS(rv, rv);

      if (stack.isObject()) {
        // Because |stack| might be a cross-compartment wrapper, we can't use it
        // with JSAutoRealm. Use the stackGlobal for that.
        JS::Rooted<JS::Value> stackGlobal(cx);
        rv = scriptError->GetStackGlobal(&stackGlobal);
        NS_ENSURE_SUCCESS(rv, rv);

        JSAutoRealm ar(cx, &stackGlobal.toObject());

        StructuredCloneData data;
        ErrorResult err;
        data.Write(cx, stack, err);
        if (err.Failed()) {
          return err.StealNSResult();
        }

        ClonedMessageData cloned;
        if (!data.BuildClonedMessageData(cloned)) {
          return NS_ERROR_FAILURE;
        }

        mChild->SendScriptErrorWithStack(msg, sourceName, lineNum, colNum,
                                         flags, category, fromPrivateWindow,
                                         fromChromeContext, cloned);
        return NS_OK;
      }
    }

    mChild->SendScriptError(msg, sourceName, lineNum, colNum, flags, category,
                            fromPrivateWindow, 0, fromChromeContext);
    return NS_OK;
  }

  nsString msg;
  nsresult rv = aMessage->GetMessageMoz(msg);
  NS_ENSURE_SUCCESS(rv, rv);
  mChild->SendConsoleMessage(msg);
  return NS_OK;
}

#ifdef NIGHTLY_BUILD
/**
 * The singleton of this class is registered with the BackgroundHangMonitor as
 * an annotator, so that the hang monitor can record whether or not there were
 * pending input events when the thread hung.
 */
class PendingInputEventHangAnnotator final : public BackgroundHangAnnotator {
 public:
  virtual void AnnotateHang(BackgroundHangAnnotations& aAnnotations) override {
    int32_t pending = ContentChild::GetSingleton()->GetPendingInputEvents();
    if (pending > 0) {
      aAnnotations.AddAnnotation(u"PendingInput"_ns, pending);
    }
  }

  static PendingInputEventHangAnnotator sSingleton;
};
PendingInputEventHangAnnotator PendingInputEventHangAnnotator::sSingleton;
#endif

class ContentChild::ShutdownCanary final {};

ContentChild* ContentChild::sSingleton;
StaticAutoPtr<ContentChild::ShutdownCanary> ContentChild::sShutdownCanary;

ContentChild::ContentChild()
    : mIsForBrowser(false), mIsAlive(true), mShuttingDown(false) {
  // This process is a content process, so it's clearly running in
  // multiprocess mode!
  nsDebugImpl::SetMultiprocessMode("Child");

  // Our static analysis doesn't allow capturing ref-counted pointers in
  // lambdas, so we need to hide it in a uintptr_t. This is safe because this
  // lambda will be destroyed in ~ContentChild().
  uintptr_t self = reinterpret_cast<uintptr_t>(this);
  profiler_add_state_change_callback(
      AllProfilingStates(),
      [self](ProfilingState aProfilingState) {
        const ContentChild* selfPtr =
            reinterpret_cast<const ContentChild*>(self);
        PROFILER_MARKER("Process Priority", OTHER,
                        mozilla::MarkerThreadId::MainThread(), ProcessPriority,
                        ProfilerString8View::WrapNullTerminatedString(
                            ProcessPriorityToString(selfPtr->mProcessPriority)),
                        aProfilingState);
      },
      self);

  // When ContentChild is created, the observer service does not even exist.
  // When ContentChild::RecvSetXPCOMProcessAttributes is called (the first
  // IPDL call made on this object), shutdown may have already happened. Thus
  // we create a canary here that relies upon getting cleared if shutdown
  // happens without requiring the observer service at this time.
  if (!sShutdownCanary) {
    sShutdownCanary = new ShutdownCanary();
    ClearOnShutdown(&sShutdownCanary, ShutdownPhase::XPCOMShutdown);
  }
}

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(                                                  \
      disable : 4722) /* Silence "destructor never returns" warning \
                       */
#endif

ContentChild::~ContentChild() {
  profiler_remove_state_change_callback(reinterpret_cast<uintptr_t>(this));

#ifndef NS_FREE_PERMANENT_DATA
  MOZ_CRASH("Content Child shouldn't be destroyed.");
#endif
}

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

NS_INTERFACE_MAP_BEGIN(ContentChild)
  NS_INTERFACE_MAP_ENTRY(nsIDOMProcessChild)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMProcessChild)
NS_INTERFACE_MAP_END

mozilla::ipc::IPCResult ContentChild::RecvSetXPCOMProcessAttributes(
    XPCOMInitData&& aXPCOMInit, const StructuredCloneData& aInitialData,
    FullLookAndFeel&& aLookAndFeelData, dom::SystemFontList&& aFontList,
    Maybe<mozilla::ipc::ReadOnlySharedMemoryHandle>&& aSharedUASheetHandle,
    const uintptr_t& aSharedUASheetAddress,
    nsTArray<mozilla::ipc::ReadOnlySharedMemoryHandle>&& aSharedFontListBlocks,
    const bool& aIsReadyForBackgroundProcessing) {
  if (!sShutdownCanary) {
    return IPC_OK();
  }

  mLookAndFeelData = std::move(aLookAndFeelData);
  mFontList = std::move(aFontList);
  mSharedFontListBlocks = std::move(aSharedFontListBlocks);

  gfx::gfxVars::SetValuesForInitialize(aXPCOMInit.gfxNonDefaultVarUpdates());
  PerfStats::SetCollectionMask(aXPCOMInit.perfStatsMask());
  LookAndFeel::EnsureInit();
  InitSharedUASheets(std::move(aSharedUASheetHandle), aSharedUASheetAddress);
  InitXPCOM(std::move(aXPCOMInit), aInitialData,
            aIsReadyForBackgroundProcessing);
  InitGraphicsDeviceData(aXPCOMInit.contentDeviceData());
  RefPtr<net::ChildDNSService> dnsServiceChild =
      dont_AddRef(net::ChildDNSService::GetSingleton());
  if (dnsServiceChild) {
    dnsServiceChild->SetTRRDomain(aXPCOMInit.trrDomain());
    dnsServiceChild->SetTRRModeInChild(aXPCOMInit.trrMode(),
                                       aXPCOMInit.trrModeFromPref());
  }
  return IPC_OK();
}

void ContentChild::Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
                        const char* aParentBuildID, bool aIsForBrowser) {
#ifdef MOZ_WIDGET_GTK
  // When running X11 only build we need to pass a display down
  // to gtk_init because it's not going to use the one from the environment
  // on its own when deciding which backend to use, and when starting under
  // XWayland, it may choose to start with the wayland backend
  // instead of the x11 backend.
  // The DISPLAY environment variable is normally set by the parent process.
  // The MOZ_GDK_DISPLAY environment variable is set from nsAppRunner.cpp
  // when --display is set by the command line.
  if (!gfxPlatform::IsHeadless()) {
    const char* display_name = PR_GetEnv("MOZ_GDK_DISPLAY");
    if (!display_name) {
      bool waylandEnabled = false;
#  ifdef MOZ_WAYLAND
      waylandEnabled = IsWaylandEnabled();
#  endif
      if (!waylandEnabled) {
        display_name = PR_GetEnv("DISPLAY");
      }
    }
    if (display_name) {
      int argc = 3;
      char option_name[] = "--display";
      char* argv[] = {
          // argv0 is unused because g_set_prgname() was called in
          // XRE_InitChildProcess().
          nullptr, option_name, const_cast<char*>(display_name), nullptr};
      char** argvp = argv;
      gtk_init(&argc, &argvp);
    } else {
      gtk_init(nullptr, nullptr);
    }
  }
#endif

#ifdef MOZ_X11
  if (!gfxPlatform::IsHeadless()) {
    // Do this after initializing GDK, or GDK will install its own handler.
    XRE_InstallX11ErrorHandler();
  }
#endif

  MOZ_ASSERT(!sSingleton, "only one ContentChild per child");

  // Once we start sending IPC messages, we need the thread manager to be
  // initialized so we can deal with the responses. Do that here before we
  // try to construct the crash reporter.
  nsresult rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_CRASH("Failed to initialize the thread manager in ContentChild::Init");
  }

  if (!aEndpoint.Bind(this)) {
    MOZ_CRASH("Bind failed in ContentChild::Init");
  }
  sSingleton = this;

  // If communications with the parent have broken down, take the process
  // down so it's not hanging around.
  GetIPCChannel()->SetAbortOnError(true);

  // This must be checked before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID)) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ProcessChild::QuickExit();
  }

#if defined(__OpenBSD__) && defined(MOZ_SANDBOX)
  StartOpenBSDSandbox(GeckoProcessType_Content);
#endif

#ifdef MOZ_X11
#  ifdef MOZ_WIDGET_GTK
  if (GdkIsX11Display() && !gfxPlatform::IsHeadless()) {
    // Send the parent our X socket to act as a proxy reference for our X
    // resources.
    int xSocketFd = ConnectionNumber(DefaultXDisplay());
    SendBackUpXResources(FileDescriptor(xSocketFd));
  }
#  endif
#endif

  CrashReporterClient::InitSingleton(this);

  mIsForBrowser = aIsForBrowser;

  SetProcessName("Web Content"_ns);

#ifdef NIGHTLY_BUILD
  // NOTE: We have to register the annotator on the main thread, as annotators
  // only affect a single thread.
  SchedulerGroup::Dispatch(
      NS_NewRunnableFunction("RegisterPendingInputEventHangAnnotator", [] {
        BackgroundHangMonitor::RegisterAnnotator(
            PendingInputEventHangAnnotator::sSingleton);
      }));
#endif

#if defined(MOZ_MEMORY) && defined(DEBUG) && !defined(MOZ_UBSAN)
  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  MOZ_ASSERT(!stats.opt_randomize_small,
             "Content process should not randomize small allocations");
#endif
}

void ContentChild::AddProfileToProcessName(const nsACString& aProfile) {
  nsCOMPtr<nsIPrincipal> isolationPrincipal =
      ContentParent::CreateRemoteTypeIsolationPrincipal(mRemoteType);
  if (isolationPrincipal) {
    if (isolationPrincipal->OriginAttributesRef().IsPrivateBrowsing()) {
      return;
    }
  }

  mProcessName = aProfile + ":"_ns + mProcessName;  //<profile_name>:example.com
}

void ContentChild::SetProcessName(const nsACString& aName,
                                  const nsACString* aSite,
                                  const nsACString* aCurrentProfile) {
  char* name;
  if ((name = PR_GetEnv("MOZ_DEBUG_APP_PROCESS")) && aName.EqualsASCII(name)) {
#ifdef XP_UNIX
    printf_stderr("\n\nCHILDCHILDCHILDCHILD\n  [%s] debug me @%d\n\n", name,
                  getpid());
    sleep(30);
#elif defined(XP_WIN)
    // Windows has a decent JIT debugging story, so NS_DebugBreak does the
    // right thing.
    NS_DebugBreak(NS_DEBUG_BREAK,
                  "Invoking NS_DebugBreak() to debug child process", nullptr,
                  __FILE__, __LINE__);
#endif
  }

  if (aSite) {
    profiler_set_process_name(aName, aSite);
  } else {
    profiler_set_process_name(aName);
  }

  mProcessName = aName;

  // Requires pref flip
  if (aSite && StaticPrefs::fission_processSiteNames()) {
    nsCOMPtr<nsIPrincipal> isolationPrincipal =
        ContentParent::CreateRemoteTypeIsolationPrincipal(mRemoteType);
    if (isolationPrincipal) {
      // DEFAULT_PRIVATE_BROWSING_ID is the value when it's not private
      MOZ_LOG(ContentParent::GetLog(), LogLevel::Debug,
              ("private = %d, pref = %d",
               isolationPrincipal->OriginAttributesRef().IsPrivateBrowsing(),
               StaticPrefs::fission_processPrivateWindowSiteNames()));
      if (!isolationPrincipal->OriginAttributesRef().IsPrivateBrowsing()
#ifdef NIGHTLY_BUILD
          // Nightly can show site names for private windows, with a second pref
          || StaticPrefs::fission_processPrivateWindowSiteNames()
#endif
      ) {
#if !defined(XP_MACOSX)
        // Mac doesn't have the 15-character limit Linux does
        // Sets profiler process name
        if (isolationPrincipal->SchemeIs("https")) {
          nsAutoCString schemeless;
          isolationPrincipal->GetHostPort(schemeless);
          nsAutoCString originSuffix;
          isolationPrincipal->GetOriginSuffix(originSuffix);
          schemeless.Append(originSuffix);
          mProcessName = schemeless;
        } else
#endif
        {
          mProcessName = *aSite;
        }
      }
    }
  }

  if (StaticPrefs::fission_processProfileName() && aCurrentProfile &&
      !aCurrentProfile->IsEmpty()) {
    AddProfileToProcessName(*aCurrentProfile);
  }

  // else private window, don't change process name, or the pref isn't set
  // mProcessName is always flat (mProcessName == aName)

  mozilla::ipc::SetThisProcessName(mProcessName.get());

  MOZ_LOG(ContentParent::GetLog(), LogLevel::Debug,
          ("Changed name of process %d to %s", getpid(),
           PromiseFlatCString(mProcessName).get()));
}

static nsresult GetCreateWindowParams(nsIOpenWindowInfo* aOpenWindowInfo,
                                      nsDocShellLoadState* aLoadState,
                                      bool aForceNoReferrer,
                                      nsIReferrerInfo** aReferrerInfo,
                                      nsIPrincipal** aTriggeringPrincipal,
                                      nsIContentSecurityPolicy** aCsp) {
  if (!aTriggeringPrincipal || !aCsp) {
    NS_ERROR("aTriggeringPrincipal || aCsp is null");
    return NS_ERROR_FAILURE;
  }

  if (!aReferrerInfo) {
    NS_ERROR("aReferrerInfo is null");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  if (aForceNoReferrer) {
    referrerInfo = new ReferrerInfo(nullptr, ReferrerPolicy::_empty, false);
  }
  if (aLoadState && !referrerInfo) {
    referrerInfo = aLoadState->GetReferrerInfo();
  }

  RefPtr<BrowsingContext> parent = aOpenWindowInfo->GetParent();
  nsCOMPtr<nsPIDOMWindowOuter> opener =
      parent ? parent->GetDOMWindow() : nullptr;
  if (!opener) {
    nsCOMPtr<nsIPrincipal> nullPrincipal =
        NullPrincipal::Create(aOpenWindowInfo->GetOriginAttributes());
    if (!referrerInfo) {
      referrerInfo = new ReferrerInfo(nullptr, ReferrerPolicy::_empty);
    }

    referrerInfo.swap(*aReferrerInfo);
    NS_ADDREF(*aTriggeringPrincipal = nullPrincipal);
    return NS_OK;
  }

  nsCOMPtr<Document> doc = opener->GetDoc();
  NS_ADDREF(*aTriggeringPrincipal = doc->NodePrincipal());

  nsCOMPtr<nsIContentSecurityPolicy> csp = doc->GetCsp();
  if (csp) {
    csp.forget(aCsp);
  }

  nsCOMPtr<nsIURI> baseURI = doc->GetDocBaseURI();
  if (!baseURI) {
    NS_ERROR("Document didn't return a base URI");
    return NS_ERROR_FAILURE;
  }

  if (!referrerInfo) {
    referrerInfo = new ReferrerInfo(*doc);
  }

  referrerInfo.swap(*aReferrerInfo);
  return NS_OK;
}

nsresult ContentChild::ProvideWindowCommon(
    NotNull<BrowserChild*> aTabOpener, nsIOpenWindowInfo* aOpenWindowInfo,
    uint32_t aChromeFlags, bool aCalledFromJS, nsIURI* aURI,
    const nsAString& aName, const nsACString& aFeatures,
    const UserActivation::Modifiers& aModifiers, bool aForceNoOpener,
    bool aForceNoReferrer, bool aIsPopupRequested,
    nsDocShellLoadState* aLoadState, bool* aWindowIsNew,
    BrowsingContext** aReturn) {
  *aReturn = nullptr;

  nsAutoCString features(aFeatures);
  nsAutoString name(aName);

  nsresult rv;

  RefPtr<BrowsingContext> parent = aOpenWindowInfo->GetParent();
  MOZ_DIAGNOSTIC_ASSERT(parent, "We must have a parent BC");

  // Block the attempt to open a new window if the opening BrowsingContext is
  // not marked to use remote tabs. This ensures that the newly opened window is
  // correctly remote.
  if (NS_WARN_IF(!parent->UseRemoteTabs())) {
    return NS_ERROR_ABORT;
  }

  bool useRemoteSubframes =
      aChromeFlags & nsIWebBrowserChrome::CHROME_FISSION_WINDOW;

  uint32_t parentSandboxFlags = parent->SandboxFlags();
  Document* doc = parent->GetDocument();
  if (doc) {
    parentSandboxFlags = doc->GetSandboxFlags();
  }

  const bool isForPrinting = aOpenWindowInfo->GetIsForPrinting();
  // Certain conditions complicate the process of creating the new
  // BrowsingContext, and prevent us from using the
  // "CreateWindowInDifferentProcess" codepath.
  //  * With Fission enabled, process selection will happen during the load, so
  //    switching processes eagerly will not provide a benefit.
  //  * Windows created for printing must be created within the current process
  //    so that a static clone of the source document can be created.
  //  * Sandboxed popups require the full window creation codepath.
  //  * Loads with form or POST data require the full window creation codepath.
  const bool cannotLoadInDifferentProcess =
      useRemoteSubframes || isForPrinting ||
      (parentSandboxFlags &
       SANDBOX_PROPAGATES_TO_AUXILIARY_BROWSING_CONTEXTS) ||
      (aLoadState &&
       (aLoadState->IsFormSubmission() || aLoadState->PostDataStream()));
  if (!cannotLoadInDifferentProcess) {
    // If we're in a content process and we have noopener set, there's no reason
    // to load in our process, so let's load it elsewhere!
    bool loadInDifferentProcess =
        aForceNoOpener && StaticPrefs::dom_noopener_newprocess_enabled();
    if (loadInDifferentProcess) {
      nsCOMPtr<nsIPrincipal> triggeringPrincipal;
      nsCOMPtr<nsIContentSecurityPolicy> csp;
      nsCOMPtr<nsIReferrerInfo> referrerInfo;
      rv = GetCreateWindowParams(aOpenWindowInfo, aLoadState, aForceNoReferrer,
                                 getter_AddRefs(referrerInfo),
                                 getter_AddRefs(triggeringPrincipal),
                                 getter_AddRefs(csp));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      if (name.LowerCaseEqualsLiteral("_blank")) {
        name.Truncate();
      }

      MOZ_DIAGNOSTIC_ASSERT(!nsContentUtils::IsSpecialName(name));

      const bool hasValidUserGestureActivation = [aLoadState, doc] {
        if (aLoadState) {
          return aLoadState->HasValidUserGestureActivation();
        }
        if (doc) {
          return doc->HasValidTransientUserGestureActivation();
        }
        return false;
      }();

      const bool textDirectiveUserActivation = [aLoadState, doc] {
        if (doc && doc->ConsumeTextDirectiveUserActivation()) {
          return true;
        }
        if (aLoadState) {
          return aLoadState->GetTextDirectiveUserActivation();
        }
        return false;
      }() || hasValidUserGestureActivation;

      Unused << SendCreateWindowInDifferentProcess(
          aTabOpener, parent, aChromeFlags, aCalledFromJS,
          aOpenWindowInfo->GetIsTopLevelCreatedByWebContent(), aURI, features,
          aModifiers, name, triggeringPrincipal, csp, referrerInfo,
          aOpenWindowInfo->GetOriginAttributes(), hasValidUserGestureActivation,
          textDirectiveUserActivation);

      // We return NS_ERROR_ABORT, so that the caller knows that we've abandoned
      // the window open as far as it is concerned.
      return NS_ERROR_ABORT;
    }
  }

  TabId tabId(nsContentUtils::GenerateTabId());

  // We need to assign a TabGroup to the PBrowser actor before we send it to the
  // parent. Otherwise, the parent could send messages to us before we have a
  // proper TabGroup for that actor.
  RefPtr<BrowsingContext> openerBC;
  if (!aForceNoOpener) {
    openerBC = parent;
  }

  RefPtr<BrowsingContext> browsingContext = BrowsingContext::CreateDetached(
      nullptr, openerBC, nullptr, aName, BrowsingContext::Type::Content,
      BrowsingContext::CreateDetachedOptions{
          .isPopupRequested = aIsPopupRequested,
          .topLevelCreatedByWebContent = true,
          .isForPrinting = isForPrinting,
      });
  MOZ_ALWAYS_SUCCEEDS(browsingContext->SetRemoteTabs(true));
  MOZ_ALWAYS_SUCCEEDS(browsingContext->SetRemoteSubframes(useRemoteSubframes));
  MOZ_ALWAYS_SUCCEEDS(browsingContext->SetOriginAttributes(
      aOpenWindowInfo->GetOriginAttributes()));

  browsingContext->InitPendingInitialization(true);
  auto unsetPending = MakeScopeExit([browsingContext]() {
    Unused << browsingContext->SetPendingInitialization(false);
  });

  browsingContext->EnsureAttached();

  // The initial about:blank document we generate within the nsDocShell will
  // almost certainly be replaced at some point. Unfortunately, getting the
  // principal right here causes bugs due to frame scripts not getting events
  // they expect, due to the real initial about:blank not being created yet.
  //
  // For this reason, we intentionally mispredict the initial principal here, so
  // that we can act the same as we did before when not predicting a result
  // principal. This `PWindowGlobal` will almost immediately be destroyed.
  nsCOMPtr<nsIPrincipal> initialPrincipal =
      NullPrincipal::Create(browsingContext->OriginAttributesRef());
  WindowGlobalInit windowInit = WindowGlobalActor::AboutBlankInitializer(
      browsingContext, initialPrincipal);

  RefPtr<WindowGlobalChild> windowChild =
      WindowGlobalChild::CreateDisconnected(windowInit);
  if (NS_WARN_IF(!windowChild)) {
    return NS_ERROR_ABORT;
  }

  auto newChild = MakeNotNull<RefPtr<BrowserChild>>(
      this, tabId, *aTabOpener, browsingContext, aChromeFlags,
      /* aIsTopLevel */ true);

  if (IsShuttingDown()) {
    return NS_ERROR_ABORT;
  }

  // Open a remote endpoint for our PBrowser actor.
  ManagedEndpoint<PBrowserParent> parentEp = OpenPBrowserEndpoint(newChild);
  if (NS_WARN_IF(!parentEp.IsValid())) {
    return NS_ERROR_ABORT;
  }

  // Open a remote endpoint for our PWindowGlobal actor.
  ManagedEndpoint<PWindowGlobalParent> windowParentEp =
      newChild->OpenPWindowGlobalEndpoint(windowChild);
  if (NS_WARN_IF(!windowParentEp.IsValid())) {
    return NS_ERROR_ABORT;
  }

  // Tell the parent process to set up its PBrowserParent.
  PopupIPCTabContext ipcContext(aTabOpener, 0);
  if (NS_WARN_IF(!SendConstructPopupBrowser(
          std::move(parentEp), std::move(windowParentEp), tabId, ipcContext,
          windowInit, aChromeFlags))) {
    return NS_ERROR_ABORT;
  }

  windowChild->Init();
  auto guardNullWindowGlobal = MakeScopeExit([&] {
    if (!windowChild->GetWindowGlobal()) {
      windowChild->Destroy();
    }
  });

  // Now that |newChild| has had its IPC link established, call |Init| to set it
  // up.
  // XXX: This MOZ_KnownLive is only necessary because the static analysis can't
  // tell that NotNull<RefPtr<BrowserChild>> is a strong pointer.
  RefPtr<nsPIDOMWindowOuter> parentWindow =
      parent ? parent->GetDOMWindow() : nullptr;
  if (NS_FAILED(MOZ_KnownLive(newChild)->Init(parentWindow, windowChild))) {
    return NS_ERROR_ABORT;
  }

  // Set to true when we're ready to return from this function.
  bool ready = false;

  // NOTE: Capturing by reference here is safe, as this function won't return
  // until one of these callbacks is called.
  auto resolve = [&](CreatedWindowInfo&& info) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    rv = info.rv();
    *aWindowIsNew = info.windowOpened();
    nsTArray<FrameScriptInfo> frameScripts(std::move(info.frameScripts()));
    uint32_t maxTouchPoints = info.maxTouchPoints();
    DimensionInfo dimensionInfo = std::move(info.dimensions());

    // Once this function exits, we should try to exit the nested event loop.
    ready = true;

    // NOTE: We have to handle this immediately in the resolve callback in order
    // to make sure that we don't process any more IPC messages before returning
    // from ProvideWindowCommon.

    // Handle the error which we got back from the parent process, if we got
    // one.
    if (NS_FAILED(rv)) {
      return;
    }

    if (!*aWindowIsNew) {
      rv = NS_ERROR_ABORT;
      return;
    }

    // If the BrowserChild has been torn down, we don't need to do this anymore.
    if (NS_WARN_IF(!newChild->IPCOpen() || newChild->IsDestroyed())) {
      rv = NS_ERROR_ABORT;
      return;
    }

    ParentShowInfo showInfo(u""_ns, /* fakeShowInfo = */ true,
                            /* isTransparent = */ false,
                            newChild->WebWidget()->GetDPI(),
                            newChild->WebWidget()->RoundsWidgetCoordinatesTo(),
                            newChild->WebWidget()->GetDefaultScale().scale);

    newChild->SetMaxTouchPoints(maxTouchPoints);

    if (aForceNoOpener || !parent) {
      MOZ_DIAGNOSTIC_ASSERT(!browsingContext->HadOriginalOpener());
      MOZ_DIAGNOSTIC_ASSERT(browsingContext->GetTopLevelCreatedByWebContent());
      MOZ_DIAGNOSTIC_ASSERT(browsingContext->GetOpenerId() == 0);
    } else {
      MOZ_DIAGNOSTIC_ASSERT(browsingContext->HadOriginalOpener());
      MOZ_DIAGNOSTIC_ASSERT(browsingContext->GetTopLevelCreatedByWebContent());
      MOZ_DIAGNOSTIC_ASSERT(browsingContext->GetOpenerId() == parent->Id());
    }

    // Unfortunately we don't get a window unless we've shown the frame.  That's
    // pretty bogus; see bug 763602.
    newChild->DoFakeShow(showInfo);

    newChild->RecvUpdateDimensions(dimensionInfo);

    for (size_t i = 0; i < frameScripts.Length(); i++) {
      FrameScriptInfo& info = frameScripts[i];
      if (!newChild->RecvLoadRemoteScript(info.url(),
                                          info.runInGlobalScope())) {
        MOZ_CRASH();
      }
    }

    if (xpc::IsInAutomation()) {
      if (nsCOMPtr<nsPIDOMWindowOuter> outer =
              do_GetInterface(newChild->WebNavigation())) {
        nsCOMPtr<nsIObserverService> obs(services::GetObserverService());
        obs->NotifyObservers(
            outer, "dangerous:test-only:new-browser-child-ready", nullptr);
      }
    }

    browsingContext.forget(aReturn);
  };

  // NOTE: Capturing by reference here is safe, as this function won't return
  // until one of these callbacks is called.
  auto reject = [&](ResponseRejectReason) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    NS_WARNING("windowCreated promise rejected");
    rv = NS_ERROR_NOT_AVAILABLE;
    ready = true;
  };

  // Send down the request to open the window.
  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  rv = GetCreateWindowParams(aOpenWindowInfo, aLoadState, aForceNoReferrer,
                             getter_AddRefs(referrerInfo),
                             getter_AddRefs(triggeringPrincipal),
                             getter_AddRefs(csp));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  SendCreateWindow(
      aTabOpener, parent, newChild, aChromeFlags, aCalledFromJS,
      aOpenWindowInfo->GetIsForPrinting(),
      aOpenWindowInfo->GetIsForWindowDotPrint(),
      aOpenWindowInfo->GetIsTopLevelCreatedByWebContent(), aURI, features,
      aModifiers, triggeringPrincipal, csp, referrerInfo,
      aOpenWindowInfo->GetOriginAttributes(),
      aLoadState ? aLoadState->HasValidUserGestureActivation() : false,
      aLoadState ? aLoadState->GetTextDirectiveUserActivation() : false,
      std::move(resolve), std::move(reject));

  // =======================
  // Begin Nested Event Loop
  // =======================

  // We have to wait for a response from SendCreateWindow or with information
  // we're going to need to return from this function, So we spin a nested event
  // loop until they get back to us.

  {
    // Suppress event handling for all contexts in our BrowsingContextGroup so
    // that event handlers cannot target our new window while it's still being
    // opened. Note that pending events that were suppressed while our blocker
    // was active will be dispatched asynchronously from a runnable dispatched
    // to the main event loop after this function returns, not immediately when
    // we leave this scope.
    AutoSuppressEventHandlingAndSuspend seh(browsingContext->Group());

    AutoNoJSAPI nojsapi;

    // Spin the event loop until we get a response. Callers of this function
    // already have to guard against an inner event loop spinning in the
    // non-e10s case because of the need to spin one to create a new chrome
    // window.
    SpinEventLoopUntil("ContentChild::ProvideWindowCommon"_ns,
                       [&]() { return ready; });
    MOZ_RELEASE_ASSERT(ready,
                       "We are on the main thread, so we should not exit this "
                       "loop without ready being true.");
  }

  // =====================
  // End Nested Event Loop
  // =====================

  // It's possible for our new BrowsingContext to become discarded during the
  // nested event loop, in which case we shouldn't return it, since our callers
  // will generally not be prepared to deal with that.
  if (*aReturn && (*aReturn)->IsDiscarded()) {
    NS_RELEASE(*aReturn);
    return NS_ERROR_ABORT;
  }

  // We should have the results already set by the callbacks.
  MOZ_ASSERT_IF(NS_SUCCEEDED(rv), *aReturn);
  return rv;
}

bool ContentChild::IsAlive() const { return mIsAlive; }

bool ContentChild::IsShuttingDown() const { return mShuttingDown; }

void ContentChild::GetProcessName(nsACString& aName) const {
  aName = mProcessName;
}

/* static */
void ContentChild::AppendProcessId(nsACString& aName) {
  if (!aName.IsEmpty()) {
    aName.Append(' ');
  }
  unsigned pid = getpid();
  aName.Append(nsPrintfCString("(pid %u)", pid));
}

void ContentChild::InitGraphicsDeviceData(const ContentDeviceData& aData) {
  gfxPlatform::InitChild(aData);
}

void ContentChild::InitSharedUASheets(
    Maybe<mozilla::ipc::ReadOnlySharedMemoryHandle>&& aHandle,
    uintptr_t aAddress) {
  MOZ_ASSERT_IF(!aHandle, !aAddress);

  if (!aAddress) {
    return;
  }

  // Map the shared memory storing the user agent style sheets.  Do this as
  // early as possible to maximize the chance of being able to map at the
  // address we want.
  GlobalStyleSheetCache::SetSharedMemory(std::move(*aHandle), aAddress);
}

void ContentChild::InitXPCOM(
    XPCOMInitData&& aXPCOMInit,
    const mozilla::dom::ipc::StructuredCloneData& aInitialData,
    bool aIsReadyForBackgroundProcessing) {
#if defined(XP_WIN)
  // DLL services untrusted modules processing depends on
  // BackgroundChild::Startup having been called
  RefPtr<DllServices> dllSvc(DllServices::Get());
  dllSvc->StartUntrustedModulesProcessor(aIsReadyForBackgroundProcessing);
#endif  // defined(XP_WIN)

  PBackgroundChild* actorChild = BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    MOZ_ASSERT_UNREACHABLE("PBackground init can't fail at this point");
    return;
  }

  ClientManager::Startup();

  // RemoteWorkerService will be initialized in RecvRemoteType, to avoid to
  // register it to the RemoteWorkerManager while it is still a prealloc
  // remoteType and defer it to the point the child process is assigned a.
  // actual remoteType.

  nsCOMPtr<nsIConsoleService> svc(do_GetService(NS_CONSOLESERVICE_CONTRACTID));
  if (!svc) {
    NS_WARNING("Couldn't acquire console service");
    return;
  }

  mConsoleListener = new ConsoleListener(this);
  if (NS_FAILED(svc->RegisterListener(mConsoleListener)))
    NS_WARNING("Couldn't register console listener for child process");

  mAvailableDictionaries = std::move(aXPCOMInit.dictionaries());

  RecvSetOffline(aXPCOMInit.isOffline());
  RecvSetConnectivity(aXPCOMInit.isConnected());

  LocaleService::GetInstance()->AssignAppLocales(aXPCOMInit.appLocales());
  LocaleService::GetInstance()->AssignRequestedLocales(
      aXPCOMInit.requestedLocales());

  L10nRegistry::RegisterFileSourcesFromParentProcess(
      aXPCOMInit.l10nFileSources());

  RecvSetCaptivePortalState(aXPCOMInit.captivePortalState());
  RecvBidiKeyboardNotify(aXPCOMInit.isLangRTL(),
                         aXPCOMInit.haveBidiKeyboards());

  if (aXPCOMInit.domainPolicy().active()) {
    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    MOZ_ASSERT(ssm);
    ssm->ActivateDomainPolicyInternal(getter_AddRefs(mPolicy));
    if (!mPolicy) {
      MOZ_CRASH("Failed to activate domain policy.");
    }
    mPolicy->ApplyClone(&aXPCOMInit.domainPolicy());
  }

  nsCOMPtr<nsIClipboard> clipboard(
      do_GetService("@mozilla.org/widget/clipboard;1"));
  if (nsCOMPtr<nsIClipboardProxy> clipboardProxy =
          do_QueryInterface(clipboard)) {
    clipboardProxy->SetCapabilities(aXPCOMInit.clipboardCaps());
  }

  {
    AutoJSAPI jsapi;
    if (NS_WARN_IF(!jsapi.Init(xpc::PrivilegedJunkScope()))) {
      MOZ_CRASH();
    }
    ErrorResult rv;
    JS::Rooted<JS::Value> data(jsapi.cx());
    mozilla::dom::ipc::StructuredCloneData id;
    id.Copy(aInitialData);
    id.Read(jsapi.cx(), &data, rv);
    if (NS_WARN_IF(rv.Failed())) {
      MOZ_CRASH();
    }
    auto* global = ContentProcessMessageManager::Get();
    global->SetInitialProcessData(data);
  }

  // The stylesheet cache is not ready yet. Store this URL for future use.
  nsCOMPtr<nsIURI> ucsURL = std::move(aXPCOMInit.userContentSheetURL());
  GlobalStyleSheetCache::SetUserContentCSSURL(ucsURL);

  GfxInfoBase::SetFeatureStatus(std::move(aXPCOMInit.gfxFeatureStatus()));

  // Initialize the RemoteDecoderManager thread and its associated PBackground
  // channel.
  RemoteDecoderManagerChild::Init();

  Preferences::RegisterCallbackAndCall(&OnFissionBlocklistPrefChange,
                                       kFissionEnforceBlockList);
  Preferences::RegisterCallbackAndCall(&OnFissionBlocklistPrefChange,
                                       kFissionOmitBlockListValues);

  // Set the dynamic scalar definitions for this process.
  TelemetryIPC::AddDynamicScalarDefinitions(aXPCOMInit.dynamicScalarDefs());
}

mozilla::ipc::IPCResult ContentChild::RecvRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage,
    const Maybe<mozilla::ipc::FileDescriptor>& aDMDFile,
    const RequestMemoryReportResolver& aResolver) {
  nsCString process;
  if (aAnonymize || mRemoteType.IsEmpty()) {
    GetProcessName(process);
  } else {
    process = mRemoteType;
  }
  AppendProcessId(process);
  MOZ_ASSERT(!process.IsEmpty());

  MemoryReportRequestClient::Start(
      aGeneration, aAnonymize, aMinimizeMemoryUsage, aDMDFile, process,
      [&](const MemoryReport& aReport) {
        Unused << GetSingleton()->SendAddMemoryReport(aReport);
      },
      aResolver);
  return IPC_OK();
}

#if defined(XP_WIN)
mozilla::ipc::IPCResult ContentChild::RecvGetUntrustedModulesData(
    GetUntrustedModulesDataResolver&& aResolver) {
  RefPtr<DllServices> dllSvc(DllServices::Get());
  dllSvc->GetUntrustedModulesData()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [aResolver](Maybe<UntrustedModulesData>&& aData) {
        aResolver(std::move(aData));
      },
      [aResolver](nsresult aReason) { aResolver(Nothing()); });
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnblockUntrustedModulesThread() {
  if (nsCOMPtr<nsIObserverService> obs =
          mozilla::services::GetObserverService()) {
    obs->NotifyObservers(nullptr, "unblock-untrusted-modules-thread", nullptr);
  }
  return IPC_OK();
}
#endif  // defined(XP_WIN)

PCycleCollectWithLogsChild* ContentChild::AllocPCycleCollectWithLogsChild(
    const bool& aDumpAllTraces, const FileDescriptor& aGCLog,
    const FileDescriptor& aCCLog) {
  return do_AddRef(new CycleCollectWithLogsChild()).take();
}

mozilla::ipc::IPCResult ContentChild::RecvPCycleCollectWithLogsConstructor(
    PCycleCollectWithLogsChild* aActor, const bool& aDumpAllTraces,
    const FileDescriptor& aGCLog, const FileDescriptor& aCCLog) {
  // The sink's destructor is called when the last reference goes away, which
  // will cause the actor to be closed down.
  auto* actor = static_cast<CycleCollectWithLogsChild*>(aActor);
  RefPtr<CycleCollectWithLogsChild::Sink> sink =
      new CycleCollectWithLogsChild::Sink(actor, aGCLog, aCCLog);

  // Invoke the dumper, which will take a reference to the sink.
  nsCOMPtr<nsIMemoryInfoDumper> dumper =
      do_GetService("@mozilla.org/memory-info-dumper;1");
  dumper->DumpGCAndCCLogsToSink(aDumpAllTraces, sink);
  return IPC_OK();
}

bool ContentChild::DeallocPCycleCollectWithLogsChild(
    PCycleCollectWithLogsChild* aActor) {
  RefPtr<CycleCollectWithLogsChild> actor =
      dont_AddRef(static_cast<CycleCollectWithLogsChild*>(aActor));
  return true;
}

mozilla::ipc::IPCResult ContentChild::RecvInitGMPService(
    Endpoint<PGMPServiceChild>&& aGMPService) {
  if (!GMPServiceChild::Create(std::move(aGMPService))) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
  mProfilerController = ChildProfilerController::Create(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGMPsChanged(
    nsTArray<GMPCapabilityData>&& capabilities) {
  GeckoMediaPluginServiceChild::UpdateGMPCapabilities(std::move(capabilities));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInitProcessHangMonitor(
    Endpoint<PProcessHangMonitorChild>&& aHangMonitor) {
  CreateHangMonitorChild(std::move(aHangMonitor));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::GetResultForRenderingInitFailure(
    GeckoChildID aOtherChildID) {
  if (aOtherChildID == XRE_GetChildID() || aOtherChildID == OtherChildID()) {
    // If we are talking to ourselves, or the UI process, then that is a fatal
    // protocol error.
    return IPC_FAIL_NO_REASON(this);
  }

  // If we are talking to the GPU process, then we should recover from this on
  // the next ContentChild::RecvReinitRendering call.
  gfxCriticalNote << "Could not initialize rendering with GPU process";
  return IPC_OK();
}

#if defined(XP_MACOSX)
extern "C" {
void CGSShutdownServerConnections();
};
#endif

mozilla::ipc::IPCResult ContentChild::RecvInitRendering(
    Endpoint<PCompositorManagerChild>&& aCompositor,
    Endpoint<PImageBridgeChild>&& aImageBridge,
    Endpoint<PVRManagerChild>&& aVRBridge,
    Endpoint<PRemoteDecoderManagerChild>&& aVideoManager,
    nsTArray<uint32_t>&& namespaces) {
  MOZ_ASSERT(namespaces.Length() == 3);

  // Note that for all of the methods below, if it can fail, it should only
  // return false if the failure is an IPDL error. In such situations,
  // ContentChild can reason about whether or not to wait for
  // RecvReinitRendering (because we surmised the GPU process crashed), or if it
  // should crash itself (because we are actually talking to the UI process). If
  // there are localized failures (e.g. failed to spawn a thread), then it
  // should MOZ_RELEASE_ASSERT or MOZ_CRASH as necessary instead.
  if (!CompositorManagerChild::Init(std::move(aCompositor), namespaces[0])) {
    return GetResultForRenderingInitFailure(aCompositor.OtherChildID());
  }
  if (!CompositorManagerChild::CreateContentCompositorBridge(namespaces[1])) {
    return GetResultForRenderingInitFailure(aCompositor.OtherChildID());
  }
  if (!ImageBridgeChild::InitForContent(std::move(aImageBridge),
                                        namespaces[2])) {
    return GetResultForRenderingInitFailure(aImageBridge.OtherChildID());
  }
  if (!gfx::VRManagerChild::InitForContent(std::move(aVRBridge))) {
    return GetResultForRenderingInitFailure(aVRBridge.OtherChildID());
  }
  RemoteDecoderManagerChild::InitForGPUProcess(std::move(aVideoManager));

#if defined(XP_MACOSX) && !defined(MOZ_SANDBOX)
  // Close all current connections to the WindowServer. This ensures that the
  // Activity Monitor will not label the content process as "Not responding"
  // because it's not running a native event loop. See bug 1384336. When the
  // build is configured with sandbox support, this is called during sandbox
  // setup.
  CGSShutdownServerConnections();
#endif

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReinitRendering(
    Endpoint<PCompositorManagerChild>&& aCompositor,
    Endpoint<PImageBridgeChild>&& aImageBridge,
    Endpoint<PVRManagerChild>&& aVRBridge,
    Endpoint<PRemoteDecoderManagerChild>&& aVideoManager,
    nsTArray<uint32_t>&& namespaces) {
  MOZ_ASSERT(namespaces.Length() == 3);
  nsTArray<RefPtr<BrowserChild>> tabs = BrowserChild::GetAll();

  // Re-establish singleton bridges to the compositor.
  if (!CompositorManagerChild::Init(std::move(aCompositor), namespaces[0])) {
    return GetResultForRenderingInitFailure(aCompositor.OtherChildID());
  }
  if (!CompositorManagerChild::CreateContentCompositorBridge(namespaces[1])) {
    return GetResultForRenderingInitFailure(aCompositor.OtherChildID());
  }
  if (!ImageBridgeChild::ReinitForContent(std::move(aImageBridge),
                                          namespaces[2])) {
    return GetResultForRenderingInitFailure(aImageBridge.OtherChildID());
  }
  if (!gfx::VRManagerChild::InitForContent(std::move(aVRBridge))) {
    return GetResultForRenderingInitFailure(aVRBridge.OtherChildID());
  }
  gfxPlatform::GetPlatform()->CompositorUpdated();

  // Establish new PLayerTransactions.
  for (const auto& browserChild : tabs) {
    if (browserChild->GetLayersId().IsValid()) {
      browserChild->ReinitRendering();
    }
  }

  // Notify any observers that the compositor has been reinitialized,
  // eg the ZoomConstraintsClients for documents in this process.
  // This must occur after the ReinitRendering call above so that the
  // APZCTreeManagers have been connected.
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(nullptr, "compositor-reinitialized",
                                     nullptr);
  }

  RemoteDecoderManagerChild::InitForGPUProcess(std::move(aVideoManager));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReinitRenderingForDeviceReset() {
  gfxPlatform::GetPlatform()->CompositorUpdated();

  nsTArray<RefPtr<BrowserChild>> tabs = BrowserChild::GetAll();
  for (const auto& browserChild : tabs) {
    if (browserChild->GetLayersId().IsValid()) {
      browserChild->ReinitRenderingForDeviceReset();
    }
  }
  return IPC_OK();
}

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
extern "C" {
CGError CGSSetDenyWindowServerConnections(bool);
};

static void DisconnectWindowServer(bool aIsSandboxEnabled) {
  // Close all current connections to the WindowServer. This ensures that the
  // Activity Monitor will not label the content process as "Not responding"
  // because it's not running a native event loop. See bug 1384336.
  // This is required with or without the sandbox enabled. Until the
  // window server is blocked as the policy level, this should be called
  // just before CGSSetDenyWindowServerConnections() so there are no
  // windowserver connections active when CGSSetDenyWindowServerConnections()
  // is called.
  CGSShutdownServerConnections();

  // Actual security benefits are only achieved when we additionally deny
  // future connections using the sandbox policy. WebGL must be remoted if
  // the windowserver connections are blocked. WebGL remoting is disabled
  // for some tests.
  if (aIsSandboxEnabled &&
      Preferences::GetBool(
          "security.sandbox.content.mac.disconnect-windowserver") &&
      Preferences::GetBool("webgl.out-of-process")) {
    CGError result = CGSSetDenyWindowServerConnections(true);
    MOZ_DIAGNOSTIC_ASSERT(result == kCGErrorSuccess);
#  if !MOZ_DIAGNOSTIC_ASSERT_ENABLED
    Unused << result;
#  endif
  }
}
#endif

mozilla::ipc::IPCResult ContentChild::RecvSetProcessSandbox(
    const Maybe<mozilla::ipc::FileDescriptor>& aBroker) {
  // We may want to move the sandbox initialization somewhere else
  // at some point; see bug 880808.
#if defined(MOZ_SANDBOX)

  bool sandboxEnabled = true;
#  if defined(XP_LINUX)
  // On Linux, we have to support systems that can't use any sandboxing.
  sandboxEnabled = SandboxInfo::Get().CanSandboxContent();

  if (sandboxEnabled && !StaticPrefs::media_cubeb_sandbox()) {
    // Pre-start audio before sandboxing; see bug 1443612.
    Unused << CubebUtils::GetCubeb();
  }

  if (sandboxEnabled) {
    RegisterProfilerObserversForSandboxProfiler();
    sandboxEnabled = SetContentProcessSandbox(
        ContentProcessSandboxParams::ForThisProcess(aBroker));
  }
#  elif defined(XP_WIN)
  if (GetEffectiveContentSandboxLevel() > 7) {
    // Libraries required by Network Security Services (NSS).
    ::LoadLibraryW(L"freebl3.dll");
    ::LoadLibraryW(L"softokn3.dll");
    // Cache value that is retrieved from a registry entry.
    Unused << GetCpuFrequencyMHz();
  }
  mozilla::SandboxTarget::Instance()->StartSandbox();
#  elif defined(XP_MACOSX)
  sandboxEnabled = (GetEffectiveContentSandboxLevel() >= 1);
  DisconnectWindowServer(sandboxEnabled);
#  endif

  CrashReporter::RecordAnnotationBool(
      CrashReporter::Annotation::ContentSandboxEnabled, sandboxEnabled);
#  if defined(XP_LINUX) && !defined(ANDROID)
  CrashReporter::RecordAnnotationU32(
      CrashReporter::Annotation::ContentSandboxCapabilities,
      SandboxInfo::Get().AsInteger());
#  endif /* XP_LINUX && !ANDROID */
#endif   /* MOZ_SANDBOX */

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvBidiKeyboardNotify(
    const bool& aIsLangRTL, const bool& aHaveBidiKeyboards) {
  // bidi is always of type PuppetBidiKeyboard* (because in the child, the only
  // possible implementation of nsIBidiKeyboard is PuppetBidiKeyboard).
  PuppetBidiKeyboard* bidi =
      static_cast<PuppetBidiKeyboard*>(nsContentUtils::GetBidiKeyboard());
  if (bidi) {
    bidi->SetBidiKeyboardInfo(aIsLangRTL, aHaveBidiKeyboards);
  }
  return IPC_OK();
}

static StaticRefPtr<CancelableRunnable> gFirstIdleTask;

static void FirstIdle(void) {
  MOZ_ASSERT(gFirstIdleTask);
  gFirstIdleTask = nullptr;

  ContentChild::GetSingleton()->SendFirstIdle();
}

mozilla::ipc::IPCResult ContentChild::RecvConstructBrowser(
    ManagedEndpoint<PBrowserChild>&& aBrowserEp,
    ManagedEndpoint<PWindowGlobalChild>&& aWindowEp, const TabId& aTabId,
    const IPCTabContext& aContext, const WindowGlobalInit& aWindowInit,
    const uint32_t& aChromeFlags, const ContentParentId& aCpID,
    const bool& aIsForBrowser, const bool& aIsTopLevel) {
  MOZ_DIAGNOSTIC_ASSERT(!IsShuttingDown());

  static bool hasRunOnce = false;
  if (!hasRunOnce) {
    hasRunOnce = true;
    MOZ_ASSERT(!gFirstIdleTask);
    RefPtr<CancelableRunnable> firstIdleTask =
        NewCancelableRunnableFunction("FirstIdleRunnable", FirstIdle);
    gFirstIdleTask = firstIdleTask;
    if (NS_FAILED(NS_DispatchToCurrentThreadQueue(firstIdleTask.forget(),
                                                  EventQueuePriority::Idle))) {
      gFirstIdleTask = nullptr;
      hasRunOnce = false;
    }
  }

  RefPtr<BrowsingContext> browsingContext =
      BrowsingContext::Get(aWindowInit.context().mBrowsingContextId);
  if (!browsingContext || browsingContext->IsDiscarded()) {
    nsPrintfCString reason("%s initial %s BrowsingContext",
                           browsingContext ? "discarded" : "missing",
                           aIsTopLevel ? "top" : "frame");
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Warning, ("%s", reason.get()));
    if (!aIsTopLevel) {
      // Recover if the BrowsingContext is missing for a new subframe. The
      // `ManagedEndpoint` instances will be automatically destroyed.
      NS_WARNING(reason.get());
      return IPC_OK();
    }

    // (these are the only possible values of `reason` at this point)
    return browsingContext
               ? IPC_FAIL(this, "discarded initial top BrowsingContext")
               : IPC_FAIL(this, "missing initial top BrowsingContext");
  }

  if (xpc::IsInAutomation() &&
      StaticPrefs::
          browser_tabs_remote_testOnly_failPBrowserCreation_enabled()) {
    nsAutoCString idString;
    if (NS_SUCCEEDED(Preferences::GetCString(
            "browser.tabs.remote.testOnly.failPBrowserCreation.browsingContext",
            idString))) {
      nsresult rv = NS_OK;
      uint64_t bcid = idString.ToInteger64(&rv);
      if (NS_SUCCEEDED(rv) && bcid == browsingContext->Id()) {
        NS_WARNING("Injecting artificial PBrowser creation failure");
        return IPC_OK();
      }
    }
  }

  if (!aWindowInit.isInitialDocument() ||
      !NS_IsAboutBlank(aWindowInit.documentURI())) {
    return IPC_FAIL(this,
                    "Logic in CreateDocumentViewerForActor currently requires "
                    "actors to be initial about:blank documents");
  }

  // We'll happily accept any kind of IPCTabContext here; we don't need to
  // check that it's of a certain type for security purposes, because we
  // believe whatever the parent process tells us.
  MaybeInvalidTabContext tc(aContext);
  if (!tc.IsValid()) {
    NS_ERROR(nsPrintfCString("Received an invalid TabContext from "
                             "the parent process. (%s)  Crashing...",
                             tc.GetInvalidReason())
                 .get());
    MOZ_CRASH("Invalid TabContext received from the parent process.");
  }

  RefPtr<WindowGlobalChild> windowChild =
      WindowGlobalChild::CreateDisconnected(aWindowInit);
  if (!windowChild) {
    return IPC_FAIL(this, "Failed to create initial WindowGlobalChild");
  }

  RefPtr<BrowserChild> browserChild =
      BrowserChild::Create(this, aTabId, tc.GetTabContext(), browsingContext,
                           aChromeFlags, aIsTopLevel);

  // Bind the created BrowserChild to IPC to actually link the actor.
  if (NS_WARN_IF(!BindPBrowserEndpoint(std::move(aBrowserEp), browserChild))) {
    return IPC_FAIL(this, "BindPBrowserEndpoint failed");
  }

  if (NS_WARN_IF(!browserChild->BindPWindowGlobalEndpoint(std::move(aWindowEp),
                                                          windowChild))) {
    return IPC_FAIL(this, "BindPWindowGlobalEndpoint failed");
  }
  windowChild->Init();
  auto guardNullWindowGlobal = MakeScopeExit([&] {
    if (!windowChild->GetWindowGlobal()) {
      windowChild->Destroy();
    }
  });

  // Ensure that a BrowsingContext is set for our BrowserChild before
  // running `Init`.
  MOZ_RELEASE_ASSERT(browserChild->mBrowsingContext->Id() ==
                     aWindowInit.context().mBrowsingContextId);

  if (NS_WARN_IF(
          NS_FAILED(browserChild->Init(/* aOpener */ nullptr, windowChild)))) {
    return IPC_FAIL(browserChild, "BrowserChild::Init failed");
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->NotifyObservers(static_cast<nsIBrowserChild*>(browserChild),
                        "tab-child-created", nullptr);
  }
  // Notify parent that we are ready to handle input events.
  browserChild->SendRemoteIsReadyToHandleInputEvents();
  return IPC_OK();
}

void ContentChild::GetAvailableDictionaries(
    nsTArray<nsCString>& aDictionaries) {
  aDictionaries = mAvailableDictionaries.Clone();
}

mozilla::PRemoteSpellcheckEngineChild*
ContentChild::AllocPRemoteSpellcheckEngineChild() {
  MOZ_CRASH(
      "Default Constructor for PRemoteSpellcheckEngineChild should never be "
      "called");
  return nullptr;
}

bool ContentChild::DeallocPRemoteSpellcheckEngineChild(
    PRemoteSpellcheckEngineChild* child) {
  delete child;
  return true;
}

mozilla::ipc::IPCResult ContentChild::RecvNotifyEmptyHTTPCache() {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  obs->NotifyObservers(nullptr, "cacheservice:empty-cache", nullptr);
  return IPC_OK();
}

PHalChild* ContentChild::AllocPHalChild() { return CreateHalChild(); }

bool ContentChild::DeallocPHalChild(PHalChild* aHal) {
  delete aHal;
  return true;
}

devtools::PHeapSnapshotTempFileHelperChild*
ContentChild::AllocPHeapSnapshotTempFileHelperChild() {
  return devtools::HeapSnapshotTempFileHelperChild::Create();
}

bool ContentChild::DeallocPHeapSnapshotTempFileHelperChild(
    devtools::PHeapSnapshotTempFileHelperChild* aHeapSnapshotHelper) {
  delete aHeapSnapshotHelper;
  return true;
}

already_AddRefed<PTestShellChild> ContentChild::AllocPTestShellChild() {
  return MakeAndAddRef<TestShellChild>();
}

mozilla::ipc::IPCResult ContentChild::RecvPTestShellConstructor(
    PTestShellChild* actor) {
  return IPC_OK();
}

RefPtr<GenericPromise> ContentChild::UpdateCookieStatus(nsIChannel* aChannel) {
  RefPtr<CookieServiceChild> csChild = CookieServiceChild::GetSingleton();
  NS_ASSERTION(csChild, "Couldn't get CookieServiceChild");

  return csChild->TrackCookieLoad(aChannel);
}

PScriptCacheChild* ContentChild::AllocPScriptCacheChild(
    const FileDescOrError& cacheFile, const bool& wantCacheData) {
  return new loader::ScriptCacheChild();
}

bool ContentChild::DeallocPScriptCacheChild(PScriptCacheChild* cache) {
  delete static_cast<loader::ScriptCacheChild*>(cache);
  return true;
}

mozilla::ipc::IPCResult ContentChild::RecvPScriptCacheConstructor(
    PScriptCacheChild* actor, const FileDescOrError& cacheFile,
    const bool& wantCacheData) {
  Maybe<FileDescriptor> fd;
  if (cacheFile.type() == cacheFile.TFileDescriptor) {
    fd.emplace(cacheFile.get_FileDescriptor());
  }

  static_cast<loader::ScriptCacheChild*>(actor)->Init(fd, wantCacheData);

  // Some scripts listen for "app-startup" to start. However, in the content
  // process, this category runs before the ScriptPreloader is initialized so
  // these scripts wouldn't be added to the cache. Instead, if a script needs to
  // run on start up in the content process, it should listen for this category.
  NS_CreateServicesFromCategory("content-process-ready-for-script", nullptr,
                                "content-process-ready-for-script", nullptr);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvNetworkLinkTypeChange(
    const uint32_t& aType) {
  mNetworkLinkType = aType;
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "contentchild:network-link-type-changed",
                         nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSocketProcessCrashed() {
  nsIOService::IncreaseSocketProcessCrashCount();
  return IPC_OK();
}

PRemotePrintJobChild* ContentChild::AllocPRemotePrintJobChild() {
#ifdef NS_PRINTING
  return new RemotePrintJobChild();
#else
  return nullptr;
#endif
}

media::PMediaChild* ContentChild::AllocPMediaChild() {
  return media::AllocPMediaChild();
}

bool ContentChild::DeallocPMediaChild(media::PMediaChild* aActor) {
  return media::DeallocPMediaChild(aActor);
}

#ifdef MOZ_WEBRTC
PWebrtcGlobalChild* ContentChild::AllocPWebrtcGlobalChild() {
  auto* child = new WebrtcGlobalChild();
  return child;
}

bool ContentChild::DeallocPWebrtcGlobalChild(PWebrtcGlobalChild* aActor) {
  delete static_cast<WebrtcGlobalChild*>(aActor);
  return true;
}
#endif

mozilla::ipc::IPCResult ContentChild::RecvRegisterChrome(
    nsTArray<ChromePackage>&& packages,
    nsTArray<SubstitutionMapping>&& resources,
    nsTArray<OverrideMapping>&& overrides, const nsCString& locale,
    const bool& reset) {
  nsCOMPtr<nsIChromeRegistry> registrySvc = nsChromeRegistry::GetService();
  nsChromeRegistryContent* chromeRegistry =
      static_cast<nsChromeRegistryContent*>(registrySvc.get());
  if (!chromeRegistry) {
    return IPC_FAIL(this, "ChromeRegistryContent is null!");
  }
  chromeRegistry->RegisterRemoteChrome(packages, resources, overrides, locale,
                                       reset);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRegisterChromeItem(
    const ChromeRegistryItem& item) {
  nsCOMPtr<nsIChromeRegistry> registrySvc = nsChromeRegistry::GetService();
  nsChromeRegistryContent* chromeRegistry =
      static_cast<nsChromeRegistryContent*>(registrySvc.get());
  if (!chromeRegistry) {
    return IPC_FAIL(this, "ChromeRegistryContent is null!");
  }
  switch (item.type()) {
    case ChromeRegistryItem::TChromePackage:
      chromeRegistry->RegisterPackage(item.get_ChromePackage());
      break;

    case ChromeRegistryItem::TOverrideMapping:
      chromeRegistry->RegisterOverride(item.get_OverrideMapping());
      break;

    case ChromeRegistryItem::TSubstitutionMapping:
      chromeRegistry->RegisterSubstitution(item.get_SubstitutionMapping());
      break;

    default:
      MOZ_ASSERT(false, "bad chrome item");
      return IPC_FAIL_NO_REASON(this);
  }

  return IPC_OK();
}
mozilla::ipc::IPCResult ContentChild::RecvClearStyleSheetCache(
    const Maybe<bool>& aChrome, const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
    const Maybe<nsCString>& aSchemelessSite,
    const Maybe<OriginAttributesPattern>& aPattern,
    const Maybe<nsCString>& aURL) {
  SharedStyleSheetCache::Clear(aChrome, aPrincipal, aSchemelessSite, aPattern,
                               aURL);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvClearScriptCache(
    const Maybe<bool>& aChrome, const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
    const Maybe<nsCString>& aSchemelessSite,
    const Maybe<OriginAttributesPattern>& aPattern,
    const Maybe<nsCString>& aURL) {
  SharedScriptCache::Clear(aChrome, aPrincipal, aSchemelessSite, aPattern,
                           aURL);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvClearImageCache(
    const Maybe<bool>& aPrivateLoader, const Maybe<bool>& aChrome,
    const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
    const Maybe<nsCString>& aSchemelessSite,
    const Maybe<OriginAttributesPattern>& aPattern,
    const Maybe<nsCString>& aURL) {
  imgLoader::ClearCache(aPrincipal, aChrome, aPrincipal, aSchemelessSite,
                        aPattern, aURL);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetOffline(const bool& offline) {
  nsCOMPtr<nsIIOService> io(do_GetIOService());
  NS_ASSERTION(io, "IO Service can not be null");

  io->SetOffline(offline);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetConnectivity(
    const bool& connectivity) {
  nsCOMPtr<nsIIOService> io(do_GetIOService());
  nsCOMPtr<nsIIOServiceInternal> ioInternal(do_QueryInterface(io));
  NS_ASSERTION(ioInternal, "IO Service can not be null");

  ioInternal->SetConnectivity(connectivity);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetCaptivePortalState(
    const int32_t& aState) {
  nsCOMPtr<nsICaptivePortalService> cps = do_GetService(NS_CAPTIVEPORTAL_CID);
  if (!cps) {
    return IPC_OK();
  }

  mozilla::net::CaptivePortalService* portal =
      static_cast<mozilla::net::CaptivePortalService*>(cps.get());
  portal->SetStateInChild(aState);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetTRRMode(
    const nsIDNSService::ResolverMode& mode,
    const nsIDNSService::ResolverMode& modeFromPref) {
  RefPtr<net::ChildDNSService> dnsServiceChild =
      dont_AddRef(net::ChildDNSService::GetSingleton());
  if (dnsServiceChild) {
    dnsServiceChild->SetTRRModeInChild(mode, modeFromPref);
  }
  return IPC_OK();
}

void ContentChild::ActorDestroy(ActorDestroyReason why) {
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  DestroySandboxProfiler();
#endif

  if (mForceKillTimer) {
    mForceKillTimer->Cancel();
    mForceKillTimer = nullptr;
  }

  if (AbnormalShutdown == why) {
    NS_WARNING("shutting down early because of crash!");
    ProcessChild::QuickExit();
  }

#ifndef NS_FREE_PERMANENT_DATA
  // In release builds, there's no point in the content process
  // going through the full XPCOM shutdown path, because it doesn't
  // keep persistent state.
  ProcessChild::QuickExit();
#else
  // Destroy our JSProcessActors, and reject any pending queries.
  JSActorDidDestroy();

#  if defined(XP_WIN)
  RefPtr<DllServices> dllSvc(DllServices::Get());
  dllSvc->DisableFull();
#  endif  // defined(XP_WIN)

  if (gFirstIdleTask) {
    gFirstIdleTask->Cancel();
    gFirstIdleTask = nullptr;
  }

  BlobURLProtocolHandler::RemoveDataEntries();

  mSharedData = nullptr;

  mIdleObservers.Clear();

  if (mConsoleListener) {
    nsCOMPtr<nsIConsoleService> svc(
        do_GetService(NS_CONSOLESERVICE_CONTRACTID));
    if (svc) {
      svc->UnregisterListener(mConsoleListener);
      mConsoleListener->mChild = nullptr;
    }
  }
  mIsAlive = false;

  CrashReporterClient::DestroySingleton();

  XRE_ShutdownChildProcess();
#endif    // NS_FREE_PERMANENT_DATA
}

void ContentChild::ProcessingError(Result aCode, const char* aReason) {
  switch (aCode) {
    case MsgDropped:
      NS_WARNING("MsgDropped in ContentChild");
      return;

    case MsgNotKnown:
    case MsgNotAllowed:
    case MsgPayloadError:
    case MsgProcessingError:
    case MsgValueError:
      break;

    default:
      MOZ_CRASH("not reached");
  }

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::ipc_channel_error, aReason);

  MOZ_CRASH("Content child abort due to IPC error");
}

mozilla::ipc::IPCResult ContentChild::RecvPreferenceUpdate(const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvVarUpdate(const GfxVarUpdate& aVar) {
  gfx::gfxVars::ApplyUpdate(aVar);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdatePerfStatsCollectionMask(
    const uint64_t& aMask) {
  PerfStats::SetCollectionMask(static_cast<PerfStats::MetricMask>(aMask));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvCollectPerfStatsJSON(
    CollectPerfStatsJSONResolver&& aResolver) {
  aResolver(PerfStats::CollectLocalPerfStatsJSON());
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvCollectScrollingMetrics(
    CollectScrollingMetricsResolver&& aResolver) {
  auto metrics = ScrollingMetrics::CollectLocalScrollingMetrics();
  using ResolverArgs = std::tuple<const uint32_t&, const uint32_t&>;
  aResolver(ResolverArgs(std::get<0>(metrics), std::get<1>(metrics)));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvNotifyVisited(
    nsTArray<VisitedQueryResult>&& aURIs) {
  nsCOMPtr<IHistory> history = components::History::Service();
  if (!history) {
    return IPC_OK();
  }
  for (const VisitedQueryResult& result : aURIs) {
    nsCOMPtr<nsIURI> newURI = result.uri();
    if (!newURI) {
      return IPC_FAIL_NO_REASON(this);
    }
    auto status = result.visited() ? IHistory::VisitedStatus::Visited
                                   : IHistory::VisitedStatus::Unvisited;
    history->NotifyVisited(newURI, status);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvThemeChanged(
    FullLookAndFeel&& aLookAndFeelData, widget::ThemeChangeKind aKind) {
  LookAndFeel::SetData(std::move(aLookAndFeelData));
  LookAndFeel::NotifyChangedAllWindows(aKind);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvLoadProcessScript(
    const nsString& aURL) {
  auto* global = ContentProcessMessageManager::Get();
  if (global && global->LoadScript(aURL)) {
    return IPC_OK();
  }
  return IPC_FAIL(this, "ContentProcessMessageManager::LoadScript failed");
}

mozilla::ipc::IPCResult ContentChild::RecvAsyncMessage(
    const nsString& aMsg, const ClonedMessageData& aData) {
  AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING("ContentChild::RecvAsyncMessage",
                                             OTHER, aMsg);
  MMPrinter::Print("ContentChild::RecvAsyncMessage", aMsg, aData);

  RefPtr<nsFrameMessageManager> cpm =
      nsFrameMessageManager::GetChildProcessManager();
  if (cpm) {
    StructuredCloneData data;
    ipc::UnpackClonedMessageData(aData, data);
    cpm->ReceiveMessage(cpm, nullptr, aMsg, false, &data, nullptr,
                        IgnoreErrors());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRegisterStringBundles(
    nsTArray<mozilla::dom::StringBundleDescriptor>&& aDescriptors) {
  nsCOMPtr<nsIStringBundleService> stringBundleService =
      components::StringBundle::Service();

  for (auto& descriptor : aDescriptors) {
    stringBundleService->RegisterContentBundle(
        descriptor.bundleURL(), std::move(descriptor.mapHandle()));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSimpleURIUnknownRemoteSchemes(
    nsTArray<nsCString>&& aRemoteSchemes) {
  RefPtr<nsIOService> io = nsIOService::GetInstance();
  io->SetSimpleURIUnknownRemoteSchemes(aRemoteSchemes);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateL10nFileSources(
    nsTArray<mozilla::dom::L10nFileSourceDescriptor>&& aDescriptors) {
  L10nRegistry::RegisterFileSourcesFromParentProcess(aDescriptors);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateSharedData(
    mozilla::ipc::ReadOnlySharedMemoryHandle&& aMapHandle,
    nsTArray<IPCBlob>&& aBlobs, nsTArray<nsCString>&& aChangedKeys) {
  nsTArray<RefPtr<BlobImpl>> blobImpls(aBlobs.Length());
  for (auto& ipcBlob : aBlobs) {
    blobImpls.AppendElement(IPCBlobUtils::Deserialize(ipcBlob));
  }

  if (mSharedData) {
    mSharedData->Update(std::move(aMapHandle), std::move(blobImpls),
                        std::move(aChangedKeys));
  } else {
    mSharedData =
        new SharedMap(ContentProcessMessageManager::Get()->GetParentObject(),
                      std::move(aMapHandle), std::move(blobImpls));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvForceGlobalReflow(
    const gfxPlatform::GlobalReflowFlags& aFlags) {
  gfxPlatform::ForceGlobalReflow(aFlags);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGeolocationUpdate(
    nsIDOMGeoPosition* aPosition) {
  RefPtr<nsGeolocationService> gs =
      nsGeolocationService::GetGeolocationService();
  if (!gs) {
    return IPC_OK();
  }
  gs->Update(aPosition);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGeolocationError(
    const uint16_t& errorCode) {
  RefPtr<nsGeolocationService> gs =
      nsGeolocationService::GetGeolocationService();
  if (!gs) {
    return IPC_OK();
  }
  gs->NotifyError(errorCode);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateDictionaryList(
    nsTArray<nsCString>&& aDictionaries) {
  mAvailableDictionaries = std::move(aDictionaries);
  mozInlineSpellChecker::UpdateCanEnableInlineSpellChecking();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateFontList(
    dom::SystemFontList&& aFontList) {
  mFontList = std::move(aFontList);
  if (gfxPlatform::Initialized()) {
    gfxPlatform::GetPlatform()->UpdateFontList(true);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRebuildFontList(
    const bool& aFullRebuild) {
  if (gfxPlatform::Initialized()) {
    gfxPlatform::GetPlatform()->UpdateFontList(aFullRebuild);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvFontListShmBlockAdded(
    const uint32_t& aGeneration, const uint32_t& aIndex,
    mozilla::ipc::ReadOnlySharedMemoryHandle&& aHandle) {
  if (gfxPlatform::Initialized()) {
    gfxPlatformFontList::PlatformFontList()->ShmBlockAdded(aGeneration, aIndex,
                                                           std::move(aHandle));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateAppLocales(
    nsTArray<nsCString>&& aAppLocales) {
  LocaleService::GetInstance()->AssignAppLocales(aAppLocales);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateRequestedLocales(
    nsTArray<nsCString>&& aRequestedLocales) {
  LocaleService::GetInstance()->AssignRequestedLocales(aRequestedLocales);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSystemTimezoneChanged() {
  nsJSUtils::ResetTimeZone();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvAddPermission(
    const IPC::Permission& permission) {
  RefPtr<PermissionManager> permissionManager =
      PermissionManager::GetInstance();
  MOZ_ASSERT(permissionManager,
             "We have no permissionManager in the Content process !");

  // note we do not need to force mUserContextId to the default here because
  // the permission manager does that internally.
  nsAutoCString originNoSuffix;
  OriginAttributes attrs;
  bool success = attrs.PopulateFromOrigin(permission.origin, originNoSuffix);
  NS_ENSURE_TRUE(success, IPC_FAIL_NO_REASON(this));

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), originNoSuffix);
  NS_ENSURE_SUCCESS(rv, IPC_OK());

  nsCOMPtr<nsIPrincipal> principal =
      mozilla::BasePrincipal::CreateContentPrincipal(uri, attrs);

  // child processes don't care about modification time.
  int64_t modificationTime = 0;

  permissionManager->Add(
      principal, nsCString(permission.type), permission.capability, 0,
      permission.expireType, permission.expireTime, modificationTime,
      PermissionManager::eNotify, PermissionManager::eNoDBOperation);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRemoveAllPermissions() {
  RefPtr<PermissionManager> permissionManager =
      PermissionManager::GetInstance();
  MOZ_ASSERT(permissionManager,
             "We have no permissionManager in the Content process !");

  permissionManager->RemoveAllFromIPC();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvFlushMemory(const nsString& reason) {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!mShuttingDown && os) {
    os->NotifyObservers(nullptr, "memory-pressure", reason.get());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvActivateA11y(uint64_t aCacheDomains) {
#ifdef ACCESSIBILITY
  // Start accessibility in content process if it's running in chrome
  // process.
  GetOrCreateAccService(nsAccessibilityService::eMainProcess, aCacheDomains);
#endif  // ACCESSIBILITY
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvShutdownA11y() {
#ifdef ACCESSIBILITY
  // Try to shutdown accessibility in content process if it's shutting down in
  // chrome process.
  MaybeShutdownAccService(nsAccessibilityService::eMainProcess);
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetCacheDomains(
    uint64_t aCacheDomains) {
#ifdef ACCESSIBILITY
  nsAccessibilityService* accService = GetAccService();
  if (!accService) {
    return IPC_FAIL(this, "Accessibility service should exist");
  }
  accService->SetCacheDomains(aCacheDomains);
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvApplicationForeground() {
  // Rebroadcast the "application-foreground"
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "application-foreground", nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvApplicationBackground() {
  // Rebroadcast the "application-background"
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "application-background", nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGarbageCollect() {
  // Rebroadcast the "child-gc-request" so that workers will GC.
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "child-gc-request", nullptr);
  }
  nsJSContext::GarbageCollectNow(JS::GCReason::DOM_IPC);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvCycleCollect() {
  // Rebroadcast the "child-cc-request" so that workers will CC.
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "child-cc-request", nullptr);
  }
  nsJSContext::CycleCollectNow(CCReason::IPC_MESSAGE);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnlinkGhosts() {
#ifdef DEBUG
  nsWindowMemoryReporter::UnlinkGhostWindows();
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvAppInfo(
    const nsCString& version, const nsCString& buildID, const nsCString& name,
    const nsCString& UAName, const nsCString& ID, const nsCString& vendor,
    const nsCString& sourceURL, const nsCString& updateURL) {
  mAppInfo.version.Assign(version);
  mAppInfo.buildID.Assign(buildID);
  mAppInfo.name.Assign(name);
  mAppInfo.UAName.Assign(UAName);
  mAppInfo.ID.Assign(ID);
  mAppInfo.vendor.Assign(vendor);
  mAppInfo.sourceURL.Assign(sourceURL);
  mAppInfo.updateURL.Assign(updateURL);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRemoteType(
    const nsCString& aRemoteType, const nsCString& aProfile) {
  if (aRemoteType == mRemoteType) {
    // Allocation of preallocated processes that are still launching can
    // cause this
    return IPC_OK();
  }

  if (!mRemoteType.IsVoid()) {
    // Preallocated processes are type PREALLOC_REMOTE_TYPE; they may not
    // become a File: process, or Privileged About Content Process
    MOZ_LOG(ContentParent::GetLog(), LogLevel::Debug,
            ("Changing remoteType of process %d from %s to %s", getpid(),
             mRemoteType.get(), aRemoteType.get()));
    // prealloc->anything (but file) or web->web allowed, and no-change
    MOZ_RELEASE_ASSERT(mRemoteType == PREALLOC_REMOTE_TYPE &&
                       aRemoteType != FILE_REMOTE_TYPE &&
                       aRemoteType != PRIVILEGEDABOUT_REMOTE_TYPE);
  } else {
    // Initial setting of remote type.  Either to 'prealloc' or the actual
    // final type (if we didn't use a preallocated process)
    MOZ_LOG(ContentParent::GetLog(), LogLevel::Debug,
            ("Setting remoteType of process %d to %s", getpid(),
             aRemoteType.get()));

    if (aRemoteType == PREALLOC_REMOTE_TYPE) {
      PreallocInit();
    }
  }

  auto remoteTypePrefix = RemoteTypePrefix(aRemoteType);

  // Must do before SetProcessName
  mRemoteType.Assign(aRemoteType);

  // Update the process name so about:memory's process names are more obvious.
  if (aRemoteType == FILE_REMOTE_TYPE) {
    SetProcessName("file:// Content"_ns, nullptr, &aProfile);
  } else if (aRemoteType == EXTENSION_REMOTE_TYPE) {
    SetProcessName("WebExtensions"_ns, nullptr, &aProfile);
  } else if (aRemoteType == PRIVILEGEDABOUT_REMOTE_TYPE) {
    SetProcessName("Privileged Content"_ns, nullptr, &aProfile);
  } else if (aRemoteType == PRIVILEGEDMOZILLA_REMOTE_TYPE) {
    SetProcessName("Privileged Mozilla"_ns, nullptr, &aProfile);
  } else if (aRemoteType == INFERENCE_REMOTE_TYPE) {
    SetProcessName("Inference"_ns, nullptr, &aProfile);
  } else if (remoteTypePrefix == WITH_COOP_COEP_REMOTE_TYPE) {
    // The profiler can sanitize out the eTLD+1
    nsDependentCSubstring etld =
        Substring(aRemoteType, WITH_COOP_COEP_REMOTE_TYPE.Length() + 1);
#ifdef NIGHTLY_BUILD
    SetProcessName("WebCOOP+COEP Content"_ns, &etld, &aProfile);
#else
    SetProcessName("Isolated Web Content"_ns, &etld,
                   &aProfile);  // to avoid confusing people
#endif
  } else if (remoteTypePrefix == FISSION_WEB_REMOTE_TYPE) {
    // The profiler can sanitize out the eTLD+1
    nsDependentCSubstring etld =
        Substring(aRemoteType, FISSION_WEB_REMOTE_TYPE.Length() + 1);
    SetProcessName("Isolated Web Content"_ns, &etld, &aProfile);
  } else if (remoteTypePrefix == SERVICEWORKER_REMOTE_TYPE) {
    // The profiler can sanitize out the eTLD+1
    nsDependentCSubstring etld =
        Substring(aRemoteType, SERVICEWORKER_REMOTE_TYPE.Length() + 1);
    SetProcessName("Isolated Service Worker"_ns, &etld, &aProfile);
  } else {
    // else "prealloc" or "web" type -> "Web Content"
    SetProcessName("Web Content"_ns, nullptr, &aProfile);
  }

  // Turn off Spectre mitigations in isolated web content processes.
  if (StaticPrefs::javascript_options_spectre_disable_for_isolated_content() &&
      StaticPrefs::browser_opaqueResponseBlocking() &&
      (remoteTypePrefix == FISSION_WEB_REMOTE_TYPE ||
       remoteTypePrefix == SERVICEWORKER_REMOTE_TYPE ||
       remoteTypePrefix == WITH_COOP_COEP_REMOTE_TYPE ||
       aRemoteType == PRIVILEGEDABOUT_REMOTE_TYPE ||
       aRemoteType == PRIVILEGEDMOZILLA_REMOTE_TYPE)) {
    JS::DisableSpectreMitigationsAfterInit();
  }

  // Use the prefix to avoid URIs from Fission isolated processes.
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::RemoteType, remoteTypePrefix);

  return IPC_OK();
}

// A method to initialize anything we need during the preallocation phase
void ContentChild::PreallocInit() {
  EnsureNSSInitializedChromeOrContent();

  // SetAcceptLanguages() needs to read localized strings (file access),
  // which is slow, so do this in prealloc
  nsHttpHandler::PresetAcceptLanguages();
}

// Call RemoteTypePrefix() on the result to remove URIs if you want to use this
// for telemetry.
const nsACString& ContentChild::GetRemoteType() const { return mRemoteType; }

mozilla::ipc::IPCResult ContentChild::RecvInitRemoteWorkerService(
    Endpoint<PRemoteWorkerServiceChild>&& aEndpoint,
    Endpoint<PRemoteWorkerDebuggerManagerChild>&& aDebuggerChiledEp) {
  RemoteWorkerService::InitializeChild(std::move(aEndpoint),
                                       std::move(aDebuggerChiledEp));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInitBlobURLs(
    nsTArray<BlobURLRegistrationData>&& aRegistrations) {
  for (uint32_t i = 0; i < aRegistrations.Length(); ++i) {
    BlobURLRegistrationData& registration = aRegistrations[i];
    RefPtr<BlobImpl> blobImpl = IPCBlobUtils::Deserialize(registration.blob());
    MOZ_ASSERT(blobImpl);

    BlobURLProtocolHandler::AddDataEntry(registration.url(),
                                         registration.principal(),
                                         registration.partitionKey(), blobImpl);
    // If we have received an already-revoked blobURL, we have to keep it alive
    // for a while (see BlobURLProtocolHandler) in order to support pending
    // operations such as navigation, download and so on.
    if (registration.revoked()) {
      BlobURLProtocolHandler::RemoveDataEntries(nsTArray{registration.url()},
                                                false);
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInitJSActorInfos(
    nsTArray<JSProcessActorInfo>&& aContentInfos,
    nsTArray<JSWindowActorInfo>&& aWindowInfos) {
  RefPtr<JSActorService> actSvc = JSActorService::GetSingleton();
  actSvc->LoadJSActorInfos(aContentInfos, aWindowInfos);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnregisterJSWindowActor(
    const nsCString& aName) {
  RefPtr<JSActorService> actSvc = JSActorService::GetSingleton();
  actSvc->UnregisterWindowActor(aName);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnregisterJSProcessActor(
    const nsCString& aName) {
  RefPtr<JSActorService> actSvc = JSActorService::GetSingleton();
  actSvc->UnregisterProcessActor(aName);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvLastPrivateDocShellDestroyed() {
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  obs->NotifyObservers(nullptr, "last-pb-context-exited", nullptr);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvNotifyProcessPriorityChanged(
    const hal::ProcessPriority& aPriority) {
  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  NS_ENSURE_TRUE(os, IPC_OK());

  RefPtr<nsHashPropertyBag> props = new nsHashPropertyBag();
  props->SetPropertyAsInt32(u"priority"_ns, static_cast<int32_t>(aPriority));

  PROFILER_MARKER("Process Priority", OTHER,
                  mozilla::MarkerThreadId::MainThread(), ProcessPriorityChange,
                  ProfilerString8View::WrapNullTerminatedString(
                      ProcessPriorityToString(mProcessPriority)),
                  ProfilerString8View::WrapNullTerminatedString(
                      ProcessPriorityToString(aPriority)));

  // Record FOG data before the priority change.
  // Ignore the change if it's the first time we set the process priority.
  if (mProcessPriority != hal::PROCESS_PRIORITY_UNKNOWN) {
    glean::RecordPowerMetrics();
  }

  ConfigureThreadPerformanceHints(aPriority);

  mProcessPriority = aPriority;

  os->NotifyObservers(static_cast<nsIPropertyBag2*>(props),
                      "ipc:process-priority-changed", nullptr);
  if (StaticPrefs::
          dom_memory_foreground_content_processes_have_larger_page_cache()) {
#ifdef MOZ_MEMORY
    if (mProcessPriority >= hal::PROCESS_PRIORITY_FOREGROUND) {
      // Note: keep this in sync with the JS shell (js/src/shell/js.cpp).
      moz_set_max_dirty_page_modifier(4);
    }
#endif
    if (mProcessPriority == hal::PROCESS_PRIORITY_BACKGROUND) {
#ifdef MOZ_MEMORY
      moz_set_max_dirty_page_modifier(-2);

      if (StaticPrefs::dom_memory_memory_pressure_on_background() == 1) {
        jemalloc_free_dirty_pages();
      }
#endif
      if (StaticPrefs::dom_memory_memory_pressure_on_background() == 2) {
        nsCOMPtr<nsIObserverService> obsServ = services::GetObserverService();
        obsServ->NotifyObservers(nullptr, "memory-pressure", u"heap-minimize");
      } else if (StaticPrefs::dom_memory_memory_pressure_on_background() == 3) {
        nsCOMPtr<nsIObserverService> obsServ = services::GetObserverService();
        obsServ->NotifyObservers(nullptr, "memory-pressure", u"low-memory");
      }
#ifdef MOZ_MEMORY
    } else {
      moz_set_max_dirty_page_modifier(0);
#endif
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvMinimizeMemoryUsage() {
  nsCOMPtr<nsIMemoryReporterManager> mgr =
      do_GetService("@mozilla.org/memory-reporter-manager;1");
  NS_ENSURE_TRUE(mgr, IPC_OK());

  Unused << mgr->MinimizeMemoryUsage(/* callback = */ nullptr);
  return IPC_OK();
}

void ContentChild::AddIdleObserver(nsIObserver* aObserver,
                                   uint32_t aIdleTimeInS) {
  MOZ_ASSERT(aObserver, "null idle observer");
  // Make sure aObserver isn't released while we wait for the parent
  aObserver->AddRef();
  SendAddIdleObserver(reinterpret_cast<uint64_t>(aObserver), aIdleTimeInS);
  mIdleObservers.Insert(aObserver);
}

void ContentChild::RemoveIdleObserver(nsIObserver* aObserver,
                                      uint32_t aIdleTimeInS) {
  MOZ_ASSERT(aObserver, "null idle observer");
  SendRemoveIdleObserver(reinterpret_cast<uint64_t>(aObserver), aIdleTimeInS);
  aObserver->Release();
  mIdleObservers.Remove(aObserver);
}

mozilla::ipc::IPCResult ContentChild::RecvNotifyIdleObserver(
    const uint64_t& aObserver, const nsCString& aTopic,
    const nsString& aTimeStr) {
  nsIObserver* observer = reinterpret_cast<nsIObserver*>(aObserver);
  if (mIdleObservers.Contains(observer)) {
    observer->Observe(nullptr, aTopic.get(), aTimeStr.get());
  } else {
    NS_WARNING("Received notification for an idle observer that was removed.");
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvLoadAndRegisterSheet(
    nsIURI* aURI, const uint32_t& aType) {
  if (!aURI) {
    return IPC_OK();
  }

  nsStyleSheetService* sheetService = nsStyleSheetService::GetInstance();
  if (sheetService) {
    sheetService->LoadAndRegisterSheet(aURI, aType);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnregisterSheet(
    nsIURI* aURI, const uint32_t& aType) {
  if (!aURI) {
    return IPC_OK();
  }

  nsStyleSheetService* sheetService = nsStyleSheetService::GetInstance();
  if (sheetService) {
    sheetService->UnregisterSheet(aURI, aType);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDomainSetChanged(
    const uint32_t& aSetType, const uint32_t& aChangeType, nsIURI* aDomain) {
  if (aChangeType == ACTIVATE_POLICY) {
    if (mPolicy) {
      return IPC_OK();
    }
    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    MOZ_ASSERT(ssm);
    ssm->ActivateDomainPolicyInternal(getter_AddRefs(mPolicy));
    if (!mPolicy) {
      return IPC_FAIL_NO_REASON(this);
    }
    return IPC_OK();
  }
  if (!mPolicy) {
    MOZ_ASSERT_UNREACHABLE(
        "If the domain policy is not active yet,"
        " the first message should be ACTIVATE_POLICY");
    return IPC_FAIL_NO_REASON(this);
  }

  NS_ENSURE_TRUE(mPolicy, IPC_FAIL_NO_REASON(this));

  if (aChangeType == DEACTIVATE_POLICY) {
    mPolicy->Deactivate();
    mPolicy = nullptr;
    return IPC_OK();
  }

  nsCOMPtr<nsIDomainSet> set;
  switch (aSetType) {
    case BLOCKLIST:
      mPolicy->GetBlocklist(getter_AddRefs(set));
      break;
    case SUPER_BLOCKLIST:
      mPolicy->GetSuperBlocklist(getter_AddRefs(set));
      break;
    case ALLOWLIST:
      mPolicy->GetAllowlist(getter_AddRefs(set));
      break;
    case SUPER_ALLOWLIST:
      mPolicy->GetSuperAllowlist(getter_AddRefs(set));
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected setType");
      return IPC_FAIL_NO_REASON(this);
  }

  MOZ_ASSERT(set);

  switch (aChangeType) {
    case ADD_DOMAIN:
      NS_ENSURE_TRUE(aDomain, IPC_FAIL_NO_REASON(this));
      set->Add(aDomain);
      break;
    case REMOVE_DOMAIN:
      NS_ENSURE_TRUE(aDomain, IPC_FAIL_NO_REASON(this));
      set->Remove(aDomain);
      break;
    case CLEAR_DOMAINS:
      set->Clear();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected changeType");
      return IPC_FAIL_NO_REASON(this);
  }

  return IPC_OK();
}

void ContentChild::StartForceKillTimer() {
  if (mForceKillTimer) {
    return;
  }

  int32_t timeoutSecs = StaticPrefs::dom_ipc_tabs_shutdownTimeoutSecs();
  if (timeoutSecs > 0) {
    NS_NewTimerWithFuncCallback(getter_AddRefs(mForceKillTimer),
                                ContentChild::ForceKillTimerCallback, this,
                                timeoutSecs * 1000, nsITimer::TYPE_ONE_SHOT,
                                "dom::ContentChild::StartForceKillTimer");
    MOZ_ASSERT(mForceKillTimer);
  }
}

/* static */
void ContentChild::ForceKillTimerCallback(nsITimer* aTimer, void* aClosure) {
  ProcessChild::QuickExit();
}

mozilla::ipc::IPCResult ContentChild::RecvShutdownConfirmedHP() {
  ProcessChild::AppendToIPCShutdownStateAnnotation(
      "RecvShutdownConfirmedHP entry"_ns);

  // Bug 1755376: If we see "RecvShutdownConfirmedHP entry" often in
  // bug IPCError_ShutDownKill we might want to anticipate
  // ShutdownPhase::AppShutdownConfirmed to start here.

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvShutdown() {
  ProcessChild::AppendToIPCShutdownStateAnnotation("RecvShutdown entry"_ns);

  // Signal the ongoing shutdown to AppShutdown, this
  // will make abort nested SpinEventLoopUntilOrQuit loops
  AppShutdown::AdvanceShutdownPhaseWithoutNotify(
      ShutdownPhase::AppShutdownConfirmed);

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    ProcessChild::AppendToIPCShutdownStateAnnotation(
        "content-child-will-shutdown started"_ns);

    os->NotifyObservers(ToSupports(this), "content-child-will-shutdown",
                        nullptr);
  }

  ShutdownInternal();
  return IPC_OK();
}

void ContentChild::ShutdownInternal() {
  ProcessChild::AppendToIPCShutdownStateAnnotation("ShutdownInternal entry"_ns);

  // If we receive the shutdown message from within a nested event loop, we want
  // to wait for that event loop to finish. Otherwise we could prematurely
  // terminate an "unload" or "pagehide" event handler (which might be doing a
  // sync XHR, for example).

  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<nsThread> mainThread = nsThreadManager::get().GetCurrentThread();
  // Note that we only have to check the recursion count for the current
  // cooperative thread. Since the Shutdown message is not labeled with a
  // SchedulerGroup, there can be no other cooperative threads doing work while
  // we're running.
  if (mainThread && mainThread->RecursionDepth() > 1) {
    // We're in a nested event loop. Let's delay for an arbitrary period of
    // time (100ms) in the hopes that the event loop will have finished by
    // then.
    GetCurrentSerialEventTarget()->DelayedDispatch(
        NewRunnableMethod("dom::ContentChild::RecvShutdown", this,
                          &ContentChild::ShutdownInternal),
        100);
    return;
  }

  mShuttingDown = true;

#ifdef NIGHTLY_BUILD
  BackgroundHangMonitor::UnregisterAnnotator(
      PendingInputEventHangAnnotator::sSingleton);
#endif

  if (mPolicy) {
    mPolicy->Deactivate();
    mPolicy = nullptr;
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    ProcessChild::AppendToIPCShutdownStateAnnotation(
        "content-child-shutdown started"_ns);
    os->NotifyObservers(ToSupports(this), "content-child-shutdown", nullptr);
  }

  GetIPCChannel()->SetAbortOnError(false);

  if (mProfilerController) {
    const bool isProfiling = profiler_is_active();
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::ProfilerChildShutdownPhase,
        isProfiling ? "Profiling - GrabShutdownProfileAndShutdown"
                    : "Not profiling - GrabShutdownProfileAndShutdown");
    ProfileAndAdditionalInformation shutdownProfileAndAdditionalInformation =
        mProfilerController->GrabShutdownProfileAndShutdown();
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::ProfilerChildShutdownPhase,
        isProfiling ? "Profiling - Destroying ChildProfilerController"
                    : "Not profiling - Destroying ChildProfilerController");
    mProfilerController = nullptr;
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::ProfilerChildShutdownPhase,
        isProfiling ? "Profiling - SendShutdownProfile (sending)"
                    : "Not profiling - SendShutdownProfile (sending)");
    if (const size_t len = shutdownProfileAndAdditionalInformation.SizeOf();
        len >= size_t(IPC::Channel::kMaximumMessageSize)) {
      shutdownProfileAndAdditionalInformation.mProfile = nsPrintfCString(
          "*Profile from pid %u bigger (%zu) than IPC max (%zu)",
          unsigned(profiler_current_process_id().ToNumber()), len,
          size_t(IPC::Channel::kMaximumMessageSize));
    }
    // Send the shutdown profile to the parent process through our own
    // message channel, which we know will survive for long enough.
    bool sent =
        SendShutdownProfile(shutdownProfileAndAdditionalInformation.mProfile);
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::ProfilerChildShutdownPhase,
        sent ? (isProfiling ? "Profiling - SendShutdownProfile (sent)"
                            : "Not profiling - SendShutdownProfile (sent)")
             : (isProfiling ? "Profiling - SendShutdownProfile (failed)"
                            : "Not profiling - SendShutdownProfile (failed)"));
  }

  if (PerfStats::GetCollectionMask() != 0) {
    SendShutdownPerfStats(PerfStats::CollectLocalPerfStatsJSON());
  }

  // Start a timer that will ensure we quickly exit after a reasonable period
  // of time. Prevents shutdown hangs after our connection to the parent
  // closes or when the parent is too busy to ever kill us.
  ProcessChild::AppendToIPCShutdownStateAnnotation("StartForceKillTimer"_ns);
  StartForceKillTimer();

  ProcessChild::AppendToIPCShutdownStateAnnotation(
      "SendFinishShutdown (sending)"_ns);

  // Notify the parent that we are done with shutdown. This is sent with high
  // priority and will just flag we are done.
  Unused << SendNotifyShutdownSuccess();

  // Now tell the parent to actually destroy our channel which will make end
  // our process. This is expected to be the last event the parent will
  // ever process for this ContentChild.
  bool sent = SendFinishShutdown();

  ProcessChild::AppendToIPCShutdownStateAnnotation(
      sent ? "SendFinishShutdown (sent)"_ns : "SendFinishShutdown (failed)"_ns);
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateWindow(
    const uintptr_t& aChildId) {
  MOZ_ASSERT(
      false,
      "ContentChild::RecvUpdateWindow calls unexpected on this platform.");
  return IPC_FAIL_NO_REASON(this);
}

PContentPermissionRequestChild*
ContentChild::AllocPContentPermissionRequestChild(
    Span<const PermissionRequest> aRequests, nsIPrincipal* aPrincipal,
    nsIPrincipal* aTopLevelPrincipal, const bool& aIsHandlingUserInput,
    const bool& aMaybeUnsafePermissionDelegate, const TabId& aTabId) {
  MOZ_CRASH("unused");
  return nullptr;
}

bool ContentChild::DeallocPContentPermissionRequestChild(
    PContentPermissionRequestChild* actor) {
  nsContentPermissionUtils::NotifyRemoveContentPermissionRequestChild(actor);
  auto child = static_cast<RemotePermissionRequest*>(actor);
  child->IPDLRelease();
  return true;
}

already_AddRefed<PWebBrowserPersistDocumentChild>
ContentChild::AllocPWebBrowserPersistDocumentChild(
    PBrowserChild* aBrowser, const MaybeDiscarded<BrowsingContext>& aContext) {
  return MakeAndAddRef<WebBrowserPersistDocumentChild>();
}

mozilla::ipc::IPCResult ContentChild::RecvPWebBrowserPersistDocumentConstructor(
    PWebBrowserPersistDocumentChild* aActor, PBrowserChild* aBrowser,
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (NS_WARN_IF(!aBrowser)) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (aContext.IsNullOrDiscarded()) {
    aActor->SendInitFailure(NS_ERROR_NO_CONTENT);
    return IPC_OK();
  }

  nsCOMPtr<Document> foundDoc = aContext.get()->GetDocument();

  if (!foundDoc) {
    aActor->SendInitFailure(NS_ERROR_NO_CONTENT);
  } else {
    static_cast<WebBrowserPersistDocumentChild*>(aActor)->Start(foundDoc);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvPush(const nsCString& aScope,
                                               nsIPrincipal* aPrincipal,
                                               const nsString& aMessageId) {
  PushMessageDispatcher dispatcher(aScope, aPrincipal, aMessageId, Nothing());
  Unused << NS_WARN_IF(NS_FAILED(dispatcher.NotifyObserversAndWorkers()));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvPushWithData(
    const nsCString& aScope, nsIPrincipal* aPrincipal,
    const nsString& aMessageId, nsTArray<uint8_t>&& aData) {
  PushMessageDispatcher dispatcher(aScope, aPrincipal, aMessageId,
                                   Some(std::move(aData)));
  Unused << NS_WARN_IF(NS_FAILED(dispatcher.NotifyObserversAndWorkers()));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvPushError(const nsCString& aScope,
                                                    nsIPrincipal* aPrincipal,
                                                    const nsString& aMessage,
                                                    const uint32_t& aFlags) {
  PushErrorDispatcher dispatcher(aScope, aPrincipal, aMessage, aFlags);
  Unused << NS_WARN_IF(NS_FAILED(dispatcher.NotifyObserversAndWorkers()));
  return IPC_OK();
}

mozilla::ipc::IPCResult
ContentChild::RecvNotifyPushSubscriptionModifiedObservers(
    const nsCString& aScope, nsIPrincipal* aPrincipal) {
  PushSubscriptionModifiedDispatcher dispatcher(aScope, aPrincipal);
  Unused << NS_WARN_IF(NS_FAILED(dispatcher.NotifyObservers()));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvBlobURLRegistration(
    const nsCString& aURI, const IPCBlob& aBlob, nsIPrincipal* aPrincipal,
    const nsCString& aPartitionKey) {
  RefPtr<BlobImpl> blobImpl = IPCBlobUtils::Deserialize(aBlob);
  MOZ_ASSERT(blobImpl);

  BlobURLProtocolHandler::AddDataEntry(aURI, aPrincipal, aPartitionKey,
                                       blobImpl);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvBlobURLUnregistration(
    const nsTArray<nsCString>& aURIs) {
  BlobURLProtocolHandler::RemoveDataEntries(
      aURIs,
      /* aBroadcastToOtherProcesses = */ false);
  return IPC_OK();
}

void ContentChild::CreateGetFilesRequest(nsTArray<nsString>&& aDirectoryPaths,
                                         bool aRecursiveFlag, nsID& aUUID,
                                         GetFilesHelperChild* aChild) {
  MOZ_ASSERT(aChild);
  MOZ_ASSERT(!mGetFilesPendingRequests.Contains(aUUID));

  Unused << SendGetFilesRequest(aUUID, aDirectoryPaths, aRecursiveFlag);
  mGetFilesPendingRequests.InsertOrUpdate(aUUID, RefPtr{aChild});
}

void ContentChild::DeleteGetFilesRequest(nsID& aUUID,
                                         GetFilesHelperChild* aChild) {
  MOZ_ASSERT(aChild);
  MOZ_ASSERT(mGetFilesPendingRequests.Contains(aUUID));

  Unused << SendDeleteGetFilesRequest(aUUID);
  mGetFilesPendingRequests.Remove(aUUID);
}

mozilla::ipc::IPCResult ContentChild::RecvGetFilesResponse(
    const nsID& aUUID, const GetFilesResponseResult& aResult) {
  RefPtr<GetFilesHelperChild> child;

  // This object can already been deleted in case DeleteGetFilesRequest has
  // been called when the response was sending by the parent.
  if (!mGetFilesPendingRequests.Remove(aUUID, getter_AddRefs(child))) {
    return IPC_OK();
  }

  if (aResult.type() == GetFilesResponseResult::TGetFilesResponseFailure) {
    child->Finished(aResult.get_GetFilesResponseFailure().errorCode());
  } else {
    MOZ_ASSERT(aResult.type() ==
               GetFilesResponseResult::TGetFilesResponseSuccess);

    const nsTArray<IPCBlob>& ipcBlobs =
        aResult.get_GetFilesResponseSuccess().blobs();

    bool succeeded = true;
    for (uint32_t i = 0; succeeded && i < ipcBlobs.Length(); ++i) {
      RefPtr<BlobImpl> impl = IPCBlobUtils::Deserialize(ipcBlobs[i]);
      succeeded = child->AppendBlobImpl(impl);
    }

    child->Finished(succeeded ? NS_OK : NS_ERROR_OUT_OF_MEMORY);
  }
  return IPC_OK();
}

/* static */
void ContentChild::FatalErrorIfNotUsingGPUProcess(const char* const aErrorMsg,
                                                  GeckoChildID aChildID) {
  // If we're communicating with the same process or the UI process then we
  // want to crash normally. Otherwise we want to just warn as the other end
  // must be the GPU process and it crashing shouldn't be fatal for us.
  if (aChildID == XRE_GetChildID() || aChildID == 0) {
    mozilla::ipc::FatalError(aErrorMsg, false);
  } else {
    nsAutoCString formattedMessage("IPDL error: \"");
    formattedMessage.AppendASCII(aErrorMsg);
    formattedMessage.AppendLiteral(R"(".)");
    NS_WARNING(formattedMessage.get());
  }
}

PURLClassifierChild* ContentChild::AllocPURLClassifierChild(
    nsIPrincipal* aPrincipal, bool* aSuccess) {
  *aSuccess = true;
  return new URLClassifierChild();
}

bool ContentChild::DeallocPURLClassifierChild(PURLClassifierChild* aActor) {
  MOZ_ASSERT(aActor);
  delete aActor;
  return true;
}

PURLClassifierLocalChild* ContentChild::AllocPURLClassifierLocalChild(
    nsIURI* aUri, Span<const IPCURLClassifierFeature> aFeatures) {
  return new URLClassifierLocalChild();
}

bool ContentChild::DeallocPURLClassifierLocalChild(
    PURLClassifierLocalChild* aActor) {
  MOZ_ASSERT(aActor);
  delete aActor;
  return true;
}

PURLClassifierLocalByNameChild*
ContentChild::AllocPURLClassifierLocalByNameChild(
    nsIURI* aUri, Span<const nsCString> aFeatures,
    const nsIUrlClassifierFeature::listType& aListType) {
  return new URLClassifierLocalByNameChild();
}

bool ContentChild::DeallocPURLClassifierLocalByNameChild(
    PURLClassifierLocalByNameChild* aActor) {
  MOZ_ASSERT(aActor);
  delete aActor;
  return true;
}

PSessionStorageObserverChild*
ContentChild::AllocPSessionStorageObserverChild() {
  MOZ_CRASH(
      "PSessionStorageObserverChild actors should be manually constructed!");
}

bool ContentChild::DeallocPSessionStorageObserverChild(
    PSessionStorageObserverChild* aActor) {
  MOZ_ASSERT(aActor);

  delete aActor;
  return true;
}

mozilla::ipc::IPCResult ContentChild::RecvProvideAnonymousTemporaryFile(
    const uint64_t& aID, const FileDescOrError& aFDOrError) {
  mozilla::UniquePtr<AnonymousTemporaryFileCallback> callback;
  mPendingAnonymousTemporaryFiles.Remove(aID, &callback);
  MOZ_ASSERT(callback);

  PRFileDesc* prfile = nullptr;
  if (aFDOrError.type() == FileDescOrError::Tnsresult) {
    DebugOnly<nsresult> rv = aFDOrError.get_nsresult();
    MOZ_ASSERT(NS_FAILED(rv));
  } else {
    auto rawFD = aFDOrError.get_FileDescriptor().ClonePlatformHandle();
    prfile = PR_ImportFile(PROsfd(rawFD.release()));
  }
  (*callback)(prfile);
  return IPC_OK();
}

nsresult ContentChild::AsyncOpenAnonymousTemporaryFile(
    const AnonymousTemporaryFileCallback& aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  static uint64_t id = 0;
  auto newID = id++;
  if (!SendRequestAnonymousTemporaryFile(newID)) {
    return NS_ERROR_FAILURE;
  }

  // Remember the association with the callback.
  MOZ_ASSERT(!mPendingAnonymousTemporaryFiles.Get(newID));
  mPendingAnonymousTemporaryFiles.GetOrInsertNew(newID, aCallback);
  return NS_OK;
}

mozilla::ipc::IPCResult ContentChild::RecvSetPermissionsWithKey(
    const nsCString& aPermissionKey, nsTArray<IPC::Permission>&& aPerms) {
  RefPtr<PermissionManager> permManager = PermissionManager::GetInstance();
  if (permManager) {
    permManager->SetPermissionsWithKey(aPermissionKey, aPerms);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRefreshScreens(
    nsTArray<ScreenDetails>&& aScreens) {
  ScreenManager& screenManager = ScreenManager::GetSingleton();
  screenManager.Refresh(std::move(aScreens));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvShareCodeCoverageMutex(
    CrossProcessMutexHandle aHandle) {
#ifdef MOZ_CODE_COVERAGE
  CodeCoverageHandler::Init(std::move(aHandle));
  return IPC_OK();
#else
  MOZ_CRASH("Shouldn't receive this message in non-code coverage builds!");
#endif
}

mozilla::ipc::IPCResult ContentChild::RecvFlushCodeCoverageCounters(
    FlushCodeCoverageCountersResolver&& aResolver) {
#ifdef MOZ_CODE_COVERAGE
  CodeCoverageHandler::FlushCounters();
  aResolver(/* unused */ true);
  return IPC_OK();
#else
  MOZ_CRASH("Shouldn't receive this message in non-code coverage builds!");
#endif
}

mozilla::ipc::IPCResult ContentChild::RecvSetInputEventQueueEnabled() {
  nsThreadManager::get().EnableMainThreadEventPrioritization();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvFlushInputEventQueue() {
  nsThreadManager::get().FlushInputEventPrioritization();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSuspendInputEventQueue() {
  nsThreadManager::get().SuspendInputEventPrioritization();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvResumeInputEventQueue() {
  nsThreadManager::get().ResumeInputEventPrioritization();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvAddDynamicScalars(
    nsTArray<DynamicScalarDefinition>&& aDefs) {
  TelemetryIPC::AddDynamicScalarDefinitions(aDefs);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvCrossProcessRedirect(
    RedirectToRealChannelArgs&& aArgs,
    nsTArray<Endpoint<extensions::PStreamFilterParent>>&& aEndpoints,
    CrossProcessRedirectResolver&& aResolve) {
  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = mozilla::ipc::LoadInfoArgsToLoadInfo(
      aArgs.loadInfo(), NOT_REMOTE_TYPE, getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    MOZ_DIAGNOSTIC_CRASH("LoadInfoArgsToLoadInfo failed");
    return IPC_OK();
  }

  nsCOMPtr<nsIChannel> newChannel;
  MOZ_ASSERT((aArgs.loadStateInternalLoadFlags() &
              nsDocShell::InternalLoad::INTERNAL_LOAD_FLAGS_IS_SRCDOC) ||
             aArgs.srcdocData().IsVoid());
  rv = nsDocShell::CreateRealChannelForDocument(
      getter_AddRefs(newChannel), aArgs.uri(), loadInfo, nullptr,
      aArgs.newLoadFlags(), aArgs.srcdocData(), aArgs.baseUri());

  if (RefPtr<HttpBaseChannel> httpChannel = do_QueryObject(newChannel)) {
    httpChannel->SetEarlyHints(std::move(aArgs.earlyHints()));
    httpChannel->SetEarlyHintLinkType(aArgs.earlyHintLinkType());
  }

  // This is used to report any errors back to the parent by calling
  // CrossProcessRedirectFinished.
  RefPtr<HttpChannelChild> httpChild = do_QueryObject(newChannel);
  auto resolve = [=](const nsresult& aRv) {
    nsresult rv = aRv;
    if (httpChild) {
      rv = httpChild->CrossProcessRedirectFinished(rv);
    }
    aResolve(rv);
  };
  auto scopeExit = MakeScopeExit([&]() { resolve(rv); });

  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(newChannel)) {
    rv = httpChannel->SetChannelId(aArgs.channelId());
  }
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  rv = newChannel->SetOriginalURI(aArgs.originalURI());
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  if (nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
          do_QueryInterface(newChannel)) {
    rv = httpChannelInternal->SetRedirectMode(aArgs.redirectMode());
  }
  if (NS_FAILED(rv)) {
    return IPC_OK();
  }

  if (aArgs.init()) {
    HttpBaseChannel::ReplacementChannelConfig config(std::move(*aArgs.init()));
    HttpBaseChannel::ConfigureReplacementChannel(
        newChannel, config,
        HttpBaseChannel::ReplacementReason::DocumentChannel);
  }

  if (aArgs.contentDisposition()) {
    newChannel->SetContentDisposition(*aArgs.contentDisposition());
  }

  if (aArgs.contentDispositionFilename()) {
    newChannel->SetContentDispositionFilename(
        *aArgs.contentDispositionFilename());
  }

  if (nsCOMPtr<nsIChildChannel> childChannel = do_QueryInterface(newChannel)) {
    // Connect to the parent if this is a remote channel. If it's entirely
    // handled locally, then we'll call AsyncOpen from the docshell when
    // we complete the setup
    rv = childChannel->ConnectParent(
        aArgs.registrarId());  // creates parent channel
    if (NS_FAILED(rv)) {
      return IPC_OK();
    }
  }

  // We need to copy the property bag before signaling that the channel
  // is ready so that the nsDocShell can retrieve the history data when called.
  if (nsCOMPtr<nsIWritablePropertyBag> bag = do_QueryInterface(newChannel)) {
    nsHashPropertyBag::CopyFrom(bag, aArgs.properties());
  }

  RefPtr<nsDocShellLoadState> loadState;
  rv = nsDocShellLoadState::CreateFromPendingChannel(
      newChannel, aArgs.loadIdentifier(), aArgs.registrarId(),
      getter_AddRefs(loadState));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IPC_OK();
  }
  loadState->SetLoadFlags(aArgs.loadStateExternalLoadFlags());
  loadState->SetInternalLoadFlags(aArgs.loadStateInternalLoadFlags());
  if (IsValidLoadType(aArgs.loadStateLoadType())) {
    loadState->SetLoadType(aArgs.loadStateLoadType());
  }

  if (aArgs.loadingSessionHistoryInfo().isSome()) {
    loadState->SetLoadingSessionHistoryInfo(
        aArgs.loadingSessionHistoryInfo().ref());
  }
  if (aArgs.originalUriString().isSome()) {
    loadState->SetOriginalURIString(aArgs.originalUriString().ref());
  }

  RefPtr<ChildProcessChannelListener> processListener =
      ChildProcessChannelListener::GetSingleton();
  // The listener will call completeRedirectSetup or asyncOpen on the channel.
  processListener->OnChannelReady(loadState, aArgs.loadIdentifier(),
                                  std::move(aEndpoints), aArgs.timing(),
                                  std::move(resolve));
  scopeExit.release();

  // scopeExit will call CrossProcessRedirectFinished(rv) here
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvStartDelayedAutoplayMediaComponents(
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (NS_WARN_IF(aContext.IsNullOrDiscarded())) {
    return IPC_OK();
  }

  aContext.get()->StartDelayedAutoplayMediaComponents();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUpdateMediaControlAction(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const MediaControlAction& aAction) {
  if (NS_WARN_IF(aContext.IsNullOrDiscarded())) {
    return IPC_OK();
  }

  ContentMediaControlKeyHandler::HandleMediaControlAction(aContext.get(),
                                                          aAction);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvOnAllowAccessFor(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const nsCString& aTrackingOrigin, uint32_t aCookieBehavior,
    const ContentBlockingNotifier::StorageAccessPermissionGrantedReason&
        aReason) {
  MOZ_ASSERT(!aContext.IsNull(), "Browsing context cannot be null");

  StorageAccessAPIHelper::OnAllowAccessFor(
      aContext.GetMaybeDiscarded(), aTrackingOrigin, aCookieBehavior, aReason);

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvOnContentBlockingDecision(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const ContentBlockingNotifier::BlockingDecision& aDecision,
    uint32_t aRejectedReason) {
  MOZ_ASSERT(!aContext.IsNull(), "Browsing context cannot be null");

  nsCOMPtr<nsPIDOMWindowOuter> outer = aContext.get()->GetDOMWindow();
  if (!outer) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context without a outer "
             "window"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowInner> inner = outer->GetCurrentInnerWindow();
  if (!inner) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context without a inner "
             "window"));
    return IPC_OK();
  }

  ContentBlockingNotifier::OnDecision(inner, aDecision, aRejectedReason);
  return IPC_OK();
}

#ifdef NIGHTLY_BUILD
void ContentChild::OnChannelReceivedMessage(const Message& aMsg) {
  if (nsContentUtils::IsMessageInputEvent(aMsg)) {
    mPendingInputEvents++;
  }
}

PContentChild::Result ContentChild::OnMessageReceived(const Message& aMsg) {
  if (nsContentUtils::IsMessageInputEvent(aMsg)) {
    DebugOnly<uint32_t> prevEvts = mPendingInputEvents--;
    MOZ_ASSERT(prevEvts > 0);
  }

  return PContentChild::OnMessageReceived(aMsg);
}

PContentChild::Result ContentChild::OnMessageReceived(
    const Message& aMsg, UniquePtr<Message>& aReply) {
  return PContentChild::OnMessageReceived(aMsg, aReply);
}
#endif

mozilla::ipc::IPCResult ContentChild::RecvCreateBrowsingContext(
    uint64_t aGroupId, BrowsingContext::IPCInitializer&& aInit) {
  // We can't already have a BrowsingContext with this ID.
  if (RefPtr<BrowsingContext> existing = BrowsingContext::Get(aInit.mId)) {
    return IPC_FAIL(this, "Browsing context already exists");
  }

  RefPtr<WindowContext> parent = WindowContext::GetById(aInit.mParentId);
  if (!parent && aInit.mParentId != 0) {
    // Handle this case by ignoring the request, as parent must be in the
    // process of being discarded.
    // In the future it would be nice to avoid sending this message to the child
    // at all.
    NS_WARNING("Attempt to attach BrowsingContext to discarded parent");
    return IPC_OK();
  }

  RefPtr<BrowsingContextGroup> group =
      BrowsingContextGroup::GetOrCreate(aGroupId);
  return BrowsingContext::CreateFromIPC(std::move(aInit), group, nullptr);
}

mozilla::ipc::IPCResult ContentChild::RecvDiscardBrowsingContext(
    const MaybeDiscarded<BrowsingContext>& aContext, bool aDoDiscard,
    DiscardBrowsingContextResolver&& aResolve) {
  if (BrowsingContext* context = aContext.GetMaybeDiscarded()) {
    if (aDoDiscard && !context->IsDiscarded()) {
      context->Detach(/* aFromIPC */ true);
    }
    context->AddDiscardListener(aResolve);
    return IPC_OK();
  }

  // Immediately resolve the promise, as we've received the message. This will
  // allow the parent process to discard references to this BC.
  aResolve(true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRegisterBrowsingContextGroup(
    uint64_t aGroupId, nsTArray<SyncedContextInitializer>&& aInits,
    nsTArray<OriginAgentClusterInitializer>&& aUseOriginAgentCluster) {
  RefPtr<BrowsingContextGroup> group =
      BrowsingContextGroup::GetOrCreate(aGroupId);

  for (auto& entry : aUseOriginAgentCluster) {
    group->SetUseOriginAgentClusterFromIPC(entry.principal(),
                                           entry.useOriginAgentCluster());
  }

  // Each of the initializers in aInits is sorted in pre-order, so our parent
  // should always be available before the element itself.
  for (auto& initUnion : aInits) {
    switch (initUnion.type()) {
      case SyncedContextInitializer::TBrowsingContextInitializer: {
        auto& init = initUnion.get_BrowsingContextInitializer();
#ifdef DEBUG
        RefPtr<BrowsingContext> existing = BrowsingContext::Get(init.mId);
        MOZ_ASSERT(!existing, "BrowsingContext must not exist yet!");

        RefPtr<WindowContext> parent = init.GetParent();
        MOZ_ASSERT_IF(parent, parent->Group() == group);
#endif

        BrowsingContext::CreateFromIPC(std::move(init), group, nullptr);
        break;
      }
      case SyncedContextInitializer::TWindowContextInitializer: {
        auto& init = initUnion.get_WindowContextInitializer();
#ifdef DEBUG
        RefPtr<WindowContext> existing =
            WindowContext::GetById(init.mInnerWindowId);
        MOZ_ASSERT(!existing, "WindowContext must not exist yet!");
        RefPtr<BrowsingContext> parent =
            BrowsingContext::Get(init.mBrowsingContextId);
        MOZ_ASSERT(parent && parent->Group() == group);
#endif

        WindowContext::CreateFromIPC(std::move(init));
        break;
      };
      default:
        MOZ_ASSERT_UNREACHABLE();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDestroyBrowsingContextGroup(
    uint64_t aGroupId) {
  if (RefPtr<BrowsingContextGroup> group =
          BrowsingContextGroup::GetExisting(aGroupId)) {
    group->ChildDestroy();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetUseOriginAgentCluster(
    uint64_t aGroupId, nsIPrincipal* aPrincipal, bool aUseOriginAgentCluster) {
  RefPtr<BrowsingContextGroup> group =
      BrowsingContextGroup::GetOrCreate(aGroupId);
  group->SetUseOriginAgentClusterFromIPC(aPrincipal, aUseOriginAgentCluster);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvWindowClose(
    const MaybeDiscarded<BrowsingContext>& aContext, bool aTrustedCaller) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  // Call `GetDocument()` to force the document and its inner window to be
  // created, as it would be forced to be created if this call was being
  // performed in-process.
  if (NS_WARN_IF(!aContext.get()->GetDocument())) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context but document "
             "creation failed"));
    return IPC_OK();
  }

  nsGlobalWindowOuter::Cast(window)->CloseOuter(aTrustedCaller);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvWindowFocus(
    const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
    uint64_t aActionId) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  // Call `GetDocument()` to force the document and its inner window to be
  // created, as it would be forced to be created if this call was being
  // performed in-process.
  if (NS_WARN_IF(!aContext.get()->GetDocument())) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context but document "
             "creation failed"));
    return IPC_OK();
  }

  nsGlobalWindowOuter::Cast(window)->FocusOuter(
      aCallerType, /* aFromOtherProcess */ true, aActionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvWindowBlur(
    const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  // Call `GetDocument()` to force the document and its inner window to be
  // created, as it would be forced to be created if this call was being
  // performed in-process.
  if (NS_WARN_IF(!aContext.get()->GetDocument())) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context but document "
             "creation failed"));
    return IPC_OK();
  }

  nsGlobalWindowOuter::Cast(window)->BlurOuter(aCallerType);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvRaiseWindow(
    const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
    uint64_t aActionId) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    fm->RaiseWindow(window, aCallerType, aActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvAdjustWindowFocus(
    const MaybeDiscarded<BrowsingContext>& aContext, bool aIsVisible,
    uint64_t aActionId, bool aShouldClearAncestorFocus,
    const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    RefPtr<BrowsingContext> bc = aContext.get();
    RefPtr<BrowsingContext> ancestor =
        aAncestorBrowsingContextToFocus.IsNullOrDiscarded()
            ? nullptr
            : aAncestorBrowsingContextToFocus.get();
    fm->AdjustInProcessWindowFocus(bc, false, aIsVisible, aActionId,
                                   aShouldClearAncestorFocus, ancestor);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvClearFocus(
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    fm->ClearFocus(window);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetFocusedBrowsingContext(
    const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    fm->SetFocusedBrowsingContextFromOtherProcess(aContext.get(), aActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetActiveBrowsingContext(
    const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    fm->SetActiveBrowsingContextFromOtherProcess(aContext.get(), aActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvAbortOrientationPendingPromises(
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  dom::ScreenOrientation::AbortInProcessOrientationPromises(aContext.get());
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvUnsetActiveBrowsingContext(
    const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    fm->UnsetActiveBrowsingContextFromOtherProcess(aContext.get(), aActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetFocusedElement(
    const MaybeDiscarded<BrowsingContext>& aContext, bool aNeedsFocus) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = aContext.get()->GetDOMWindow();
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  window->SetFocusedElement(nullptr, 0, aNeedsFocus);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvFinalizeFocusOuter(
    const MaybeDiscarded<BrowsingContext>& aContext, bool aCanFocus,
    CallerType aCallerType) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  if (Element* frame = aContext.get()->GetEmbedderElement()) {
    nsContentUtils::RequestFrameFocus(*frame, aCanFocus, aCallerType);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvBlurToChild(
    const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
    const MaybeDiscarded<BrowsingContext>& aBrowsingContextToClear,
    const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus,
    bool aIsLeavingDocument, bool aAdjustWidget, uint64_t aActionId) {
  if (aFocusedBrowsingContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (MOZ_UNLIKELY(!fm)) {
    return IPC_OK();
  }

  RefPtr<BrowsingContext> toClear = aBrowsingContextToClear.IsDiscarded()
                                        ? nullptr
                                        : aBrowsingContextToClear.get();
  RefPtr<BrowsingContext> toFocus =
      aAncestorBrowsingContextToFocus.IsDiscarded()
          ? nullptr
          : aAncestorBrowsingContextToFocus.get();

  RefPtr<BrowsingContext> focusedBrowsingContext =
      aFocusedBrowsingContext.get();

  fm->BlurFromOtherProcess(focusedBrowsingContext, toClear, toFocus,
                           aIsLeavingDocument, aAdjustWidget, aActionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvSetupFocusedAndActive(
    const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
    uint64_t aActionIdForFocused,
    const MaybeDiscarded<BrowsingContext>& aActiveBrowsingContext,
    uint64_t aActionIdForActive) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    if (!aActiveBrowsingContext.IsNullOrDiscarded()) {
      fm->SetActiveBrowsingContextFromOtherProcess(aActiveBrowsingContext.get(),
                                                   aActionIdForActive);
    }
    if (!aFocusedBrowsingContext.IsNullOrDiscarded()) {
      fm->SetFocusedBrowsingContextFromOtherProcess(
          aFocusedBrowsingContext.get(), aActionIdForFocused);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReviseActiveBrowsingContext(
    uint64_t aOldActionId,
    const MaybeDiscarded<BrowsingContext>& aActiveBrowsingContext,
    uint64_t aNewActionId) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm && !aActiveBrowsingContext.IsNullOrDiscarded()) {
    fm->ReviseActiveBrowsingContext(aOldActionId, aActiveBrowsingContext.get(),
                                    aNewActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReviseFocusedBrowsingContext(
    uint64_t aOldActionId,
    const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
    uint64_t aNewActionId) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm && !aFocusedBrowsingContext.IsNullOrDiscarded()) {
    fm->ReviseFocusedBrowsingContext(
        aOldActionId, aFocusedBrowsingContext.get(), aNewActionId);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvMaybeExitFullscreen(
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  nsIDocShell* shell = aContext.get()->GetDocShell();
  if (!shell) {
    return IPC_OK();
  }

  Document* doc = shell->GetDocument();
  if (doc && doc->GetFullscreenElement()) {
    Document::AsyncExitFullscreen(doc);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvWindowPostMessage(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const ClonedOrErrorMessageData& aMessage, const PostMessageData& aData) {
  if (aContext.IsNullOrDiscarded()) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to dead or detached context"));
    return IPC_OK();
  }

  RefPtr<nsGlobalWindowOuter> window =
      nsGlobalWindowOuter::Cast(aContext.get()->GetDOMWindow());
  if (!window) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ChildIPC: Trying to send a message to a context without a window"));
    return IPC_OK();
  }

  nsCOMPtr<nsIPrincipal> providedPrincipal;
  if (!window->GetPrincipalForPostMessage(
          aData.targetOrigin(), aData.targetOriginURI(),
          aData.callerPrincipal(), *aData.subjectPrincipal(),
          getter_AddRefs(providedPrincipal))) {
    return IPC_OK();
  }

  // Call `GetDocument()` to force the document and its inner window to be
  // created, as it would be forced to be created if this call was being
  // performed in-process.
  if (NS_WARN_IF(!aContext.get()->GetDocument())) {
    MOZ_LOG(BrowsingContext::GetLog(), LogLevel::Debug,
            ("ChildIPC: Trying to send a message to a context but document "
             "creation failed"));
    return IPC_OK();
  }

  // It's OK if `sourceBc` has already been discarded, so long as we can
  // continue to wrap it.
  RefPtr<BrowsingContext> sourceBc = aData.source().GetMaybeDiscarded();

  // Create and asynchronously dispatch a runnable which will handle actual DOM
  // event creation and dispatch.
  RefPtr<PostMessageEvent> event =
      new PostMessageEvent(sourceBc, aData.origin(), window, providedPrincipal,
                           aData.innerWindowId(), aData.callerURI(),
                           aData.scriptLocation(), aData.isFromPrivateWindow());
  event->UnpackFrom(aMessage);

  event->DispatchToTargetThread(IgnoredErrorResult());
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvCommitBrowsingContextTransaction(
    const MaybeDiscarded<BrowsingContext>& aContext,
    BrowsingContext::BaseTransaction&& aTransaction, uint64_t aEpoch) {
  return aTransaction.CommitFromIPC(aContext, aEpoch, this);
}

mozilla::ipc::IPCResult ContentChild::RecvCommitWindowContextTransaction(
    const MaybeDiscarded<WindowContext>& aContext,
    WindowContext::BaseTransaction&& aTransaction, uint64_t aEpoch) {
  return aTransaction.CommitFromIPC(aContext, aEpoch, this);
}

mozilla::ipc::IPCResult ContentChild::RecvCreateWindowContext(
    WindowContext::IPCInitializer&& aInit) {
  RefPtr<BrowsingContext> bc = BrowsingContext::Get(aInit.mBrowsingContextId);
  if (!bc) {
    // Handle this case by ignoring the request, as bc must be in the process of
    // being discarded.
    // In the future it would be nice to avoid sending this message to the child
    // at all.
    NS_WARNING("Attempt to attach WindowContext to discarded parent");
    return IPC_OK();
  }

  WindowContext::CreateFromIPC(std::move(aInit));
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDiscardWindowContext(
    uint64_t aContextId, DiscardWindowContextResolver&& aResolve) {
  // Resolve immediately to acknowledge call
  aResolve(true);

  RefPtr<WindowContext> window = WindowContext::GetById(aContextId);
  if (NS_WARN_IF(!window) || NS_WARN_IF(window->IsDiscarded())) {
    return IPC_OK();
  }

  window->Discard();
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvScriptError(
    const nsString& aMessage, const nsCString& aSourceName,
    const uint32_t& aLineNumber, const uint32_t& aColNumber,
    const uint32_t& aFlags, const nsCString& aCategory,
    const bool& aFromPrivateWindow, const uint64_t& aInnerWindowId,
    const bool& aFromChromeContext) {
  nsresult rv = NS_OK;
  nsCOMPtr<nsIConsoleService> consoleService =
      do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, IPC_FAIL(this, "Failed to get console service"));

  nsCOMPtr<nsIScriptError> scriptError(
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));
  NS_ENSURE_TRUE(scriptError,
                 IPC_FAIL(this, "Failed to construct nsIScriptError"));

  scriptError->InitWithWindowID(aMessage, aSourceName, aLineNumber, aColNumber,
                                aFlags, aCategory, aInnerWindowId,
                                aFromChromeContext);
  rv = consoleService->LogMessage(scriptError);
  NS_ENSURE_SUCCESS(rv, IPC_FAIL(this, "Failed to log script error"));

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReportFrameTimingData(
    const LoadInfoArgs& loadInfoArgs, const nsString& entryName,
    const nsString& initiatorType, UniquePtr<PerformanceTimingData>&& aData) {
  if (!aData) {
    return IPC_FAIL(this, "aData should not be null");
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = mozilla::ipc::LoadInfoArgsToLoadInfo(
      loadInfoArgs, NOT_REMOTE_TYPE, getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    MOZ_DIAGNOSTIC_CRASH("LoadInfoArgsToLoadInfo failed");
    return IPC_OK();
  }

  // It is important to call LoadInfo::GetPerformanceStorage instead of simply
  // getting the performance object via the innerWindowID in order to perform
  // necessary cross origin checks.
  if (PerformanceStorage* storage = loadInfo->GetPerformanceStorage()) {
    storage->AddEntry(entryName, initiatorType, std::move(aData));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvLoadURI(
    const MaybeDiscarded<BrowsingContext>& aContext,
    nsDocShellLoadState* aLoadState, bool aSetNavigating,
    LoadURIResolver&& aResolve) {
  auto resolveOnExit = MakeScopeExit([&] { aResolve(true); });

  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  RefPtr<BrowsingContext> context = aContext.get();
  if (!context->IsInProcess()) {
    // The DocShell has been torn down or the BrowsingContext has changed
    // process in the middle of the load request. There's not much we can do at
    // this point, so just give up.
    return IPC_OK();
  }

  context->LoadURI(aLoadState, aSetNavigating);

  nsCOMPtr<nsPIDOMWindowOuter> window = context->GetDOMWindow();
  BrowserChild* bc = BrowserChild::GetFrom(window);
  if (bc) {
    bc->NotifyNavigationFinished();
  }

#ifdef MOZ_CRASHREPORTER
  if (CrashReporter::GetEnabled()) {
    nsCOMPtr<nsIURI> annotationURI;

    nsresult rv = NS_MutateURI(aLoadState->URI())
                      .SetUserPass(""_ns)
                      .Finalize(annotationURI);

    if (NS_FAILED(rv)) {
      // Ignore failures on about: URIs.
      annotationURI = aLoadState->URI();
    }

    CrashReporter::RecordAnnotationNSCString(CrashReporter::Annotation::URL,
                                             annotationURI->GetSpecOrDefault());
  }
#endif

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInternalLoad(
    nsDocShellLoadState* aLoadState) {
  if (!aLoadState->Target().IsEmpty() ||
      aLoadState->TargetBrowsingContext().IsNull()) {
    return IPC_FAIL(this, "must already be retargeted");
  }
  if (aLoadState->TargetBrowsingContext().IsDiscarded()) {
    return IPC_OK();
  }
  RefPtr<BrowsingContext> context = aLoadState->TargetBrowsingContext().get();

  context->InternalLoad(aLoadState);

#ifdef MOZ_CRASHREPORTER
  if (CrashReporter::GetEnabled()) {
    nsCOMPtr<nsIURI> annotationURI;

    nsresult rv = NS_MutateURI(aLoadState->URI())
                      .SetUserPass(""_ns)
                      .Finalize(annotationURI);

    if (NS_FAILED(rv)) {
      // Ignore failures on about: URIs.
      annotationURI = aLoadState->URI();
    }

    CrashReporter::RecordAnnotationNSCString(CrashReporter::Annotation::URL,
                                             annotationURI->GetSpecOrDefault());
  }
#endif

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDisplayLoadError(
    const MaybeDiscarded<BrowsingContext>& aContext, const nsAString& aURI) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  RefPtr<BrowsingContext> context = aContext.get();

  context->DisplayLoadError(aURI);

  nsCOMPtr<nsPIDOMWindowOuter> window = context->GetDOMWindow();
  BrowserChild* bc = BrowserChild::GetFrom(window);
  if (bc) {
    bc->NotifyNavigationFinished();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvHistoryCommitIndexAndLength(
    const MaybeDiscarded<BrowsingContext>& aContext, const uint32_t& aIndex,
    const uint32_t& aLength, const nsID& aChangeID) {
  if (!aContext.IsNullOrDiscarded()) {
    ChildSHistory* shistory = aContext.get()->GetChildSessionHistory();
    if (shistory) {
      shistory->SetIndexAndLength(aIndex, aLength, aChangeID);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGetLayoutHistoryState(
    const MaybeDiscarded<BrowsingContext>& aContext,
    GetLayoutHistoryStateResolver&& aResolver) {
  nsCOMPtr<nsILayoutHistoryState> state;
  nsIDocShell* docShell;
  mozilla::Maybe<mozilla::dom::Wireframe> wireframe;
  if (!aContext.IsNullOrDiscarded() &&
      (docShell = aContext.get()->GetDocShell())) {
    docShell->PersistLayoutHistoryState();
    docShell->GetLayoutHistoryState(getter_AddRefs(state));
    wireframe = static_cast<nsDocShell*>(docShell)->GetWireframe();
  }
  aResolver(
      std::tuple<nsILayoutHistoryState*, const mozilla::Maybe<Wireframe>&>(
          state, wireframe));

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDispatchLocationChangeEvent(
    const MaybeDiscarded<BrowsingContext>& aContext) {
  if (!aContext.IsNullOrDiscarded() && aContext.get()->GetDocShell()) {
    aContext.get()->GetDocShell()->DispatchLocationChangeEvent();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvDispatchBeforeUnloadToSubtree(
    const MaybeDiscarded<BrowsingContext>& aStartingAt,
    DispatchBeforeUnloadToSubtreeResolver&& aResolver) {
  if (aStartingAt.IsNullOrDiscarded()) {
    aResolver(nsIDocumentViewer::eAllowNavigation);
  } else {
    DispatchBeforeUnloadToSubtree(aStartingAt.get(), std::move(aResolver));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvInitNextGenLocalStorageEnabled(
    const bool& aEnabled) {
  mozilla::dom::RecvInitNextGenLocalStorageEnabled(aEnabled);

  return IPC_OK();
}

/* static */ void ContentChild::DispatchBeforeUnloadToSubtree(
    BrowsingContext* aStartingAt,
    const DispatchBeforeUnloadToSubtreeResolver& aResolver) {
  bool resolved = false;

  aStartingAt->PreOrderWalk([&](dom::BrowsingContext* aBC) {
    if (aBC->GetDocShell()) {
      nsCOMPtr<nsIDocumentViewer> viewer;
      aBC->GetDocShell()->GetDocViewer(getter_AddRefs(viewer));
      if (viewer &&
          viewer->DispatchBeforeUnload() ==
              nsIDocumentViewer::eRequestBlockNavigation &&
          !resolved) {
        // Send our response as soon as we find any blocker, so that we can show
        // the permit unload prompt as soon as possible, without giving
        // subsequent handlers a chance to delay it.
        aResolver(nsIDocumentViewer::eRequestBlockNavigation);
        resolved = true;
      }
    }
  });

  if (!resolved) {
    aResolver(nsIDocumentViewer::eAllowNavigation);
  }
}

mozilla::ipc::IPCResult ContentChild::RecvGoBack(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const Maybe<int32_t>& aCancelContentJSEpoch, bool aRequireUserInteraction,
    bool aUserActivation) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  BrowsingContext* bc = aContext.get();

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(bc->GetDocShell())) {
    if (aCancelContentJSEpoch) {
      docShell->SetCancelContentJSEpoch(*aCancelContentJSEpoch);
    }
    docShell->GoBack(aRequireUserInteraction, aUserActivation);

    if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
      browserChild->NotifyNavigationFinished();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGoForward(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const Maybe<int32_t>& aCancelContentJSEpoch, bool aRequireUserInteraction,
    bool aUserActivation) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  BrowsingContext* bc = aContext.get();

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(bc->GetDocShell())) {
    if (aCancelContentJSEpoch) {
      docShell->SetCancelContentJSEpoch(*aCancelContentJSEpoch);
    }
    docShell->GoForward(aRequireUserInteraction, aUserActivation);

    if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
      browserChild->NotifyNavigationFinished();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvGoToIndex(
    const MaybeDiscarded<BrowsingContext>& aContext, const int32_t& aIndex,
    const Maybe<int32_t>& aCancelContentJSEpoch, bool aUserActivation) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  BrowsingContext* bc = aContext.get();

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(bc->GetDocShell())) {
    if (aCancelContentJSEpoch) {
      docShell->SetCancelContentJSEpoch(*aCancelContentJSEpoch);
    }
    docShell->GotoIndex(aIndex, aUserActivation);

    if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
      browserChild->NotifyNavigationFinished();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvReload(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const uint32_t aReloadFlags) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  BrowsingContext* bc = aContext.get();

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(bc->GetDocShell())) {
    docShell->Reload(aReloadFlags);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ContentChild::RecvStopLoad(
    const MaybeDiscarded<BrowsingContext>& aContext,
    const uint32_t aStopFlags) {
  if (aContext.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  BrowsingContext* bc = aContext.get();

  if (auto* docShell = nsDocShell::Cast(bc->GetDocShell())) {
    docShell->Stop(aStopFlags);
  }

  return IPC_OK();
}

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
mozilla::ipc::IPCResult ContentChild::RecvInitSandboxTesting(
    Endpoint<PSandboxTestingChild>&& aEndpoint) {
  if (!SandboxTestingChild::Initialize(std::move(aEndpoint))) {
    return IPC_FAIL(
        this, "InitSandboxTesting failed to initialise the child process.");
  }
  return IPC_OK();
}
#endif

NS_IMETHODIMP ContentChild::GetChildID(uint64_t* aOut) {
  *aOut = XRE_GetChildID();
  return NS_OK;
}

NS_IMETHODIMP ContentChild::GetActor(const nsACString& aName, JSContext* aCx,
                                     JSProcessActorChild** retval) {
  ErrorResult error;
  RefPtr<JSProcessActorChild> actor =
      JSActorManager::GetActor(aCx, aName, error)
          .downcast<JSProcessActorChild>();
  if (error.MaybeSetPendingException(aCx)) {
    return NS_ERROR_FAILURE;
  }
  actor.forget(retval);
  return NS_OK;
}

NS_IMETHODIMP ContentChild::GetExistingActor(const nsACString& aName,
                                             JSProcessActorChild** retval) {
  RefPtr<JSProcessActorChild> actor =
      JSActorManager::GetExistingActor(aName).downcast<JSProcessActorChild>();
  actor.forget(retval);
  return NS_OK;
}

already_AddRefed<JSActor> ContentChild::InitJSActor(
    JS::Handle<JSObject*> aMaybeActor, const nsACString& aName,
    ErrorResult& aRv) {
  RefPtr<JSProcessActorChild> actor;
  if (aMaybeActor.get()) {
    aRv = UNWRAP_OBJECT(JSProcessActorChild, aMaybeActor.get(), actor);
    if (aRv.Failed()) {
      return nullptr;
    }
  } else {
    actor = new JSProcessActorChild();
  }

  MOZ_RELEASE_ASSERT(!actor->Manager(),
                     "mManager was already initialized once!");
  actor->Init(aName, this);
  return actor.forget();
}

IPCResult ContentChild::RecvRawMessage(const JSActorMessageMeta& aMeta,
                                       const Maybe<ClonedMessageData>& aData,
                                       const Maybe<ClonedMessageData>& aStack) {
  Maybe<StructuredCloneData> data;
  if (aData) {
    data.emplace();
    data->BorrowFromClonedMessageData(*aData);
  }
  Maybe<StructuredCloneData> stack;
  if (aStack) {
    stack.emplace();
    stack->BorrowFromClonedMessageData(*aStack);
  }
  ReceiveRawMessage(aMeta, std::move(data), std::move(stack));
  return IPC_OK();
}

NS_IMETHODIMP ContentChild::GetCanSend(bool* aCanSend) {
  *aCanSend = CanSend();
  return NS_OK;
}

ContentChild* ContentChild::AsContentChild() { return this; }

JSActorManager* ContentChild::AsJSActorManager() { return this; }

IPCResult ContentChild::RecvFlushFOGData(FlushFOGDataResolver&& aResolver) {
  glean::FlushFOGData(std::move(aResolver));
  return IPC_OK();
}

IPCResult ContentChild::RecvUpdateMediaCodecsSupported(
    RemoteDecodeIn aLocation, const media::MediaCodecsSupported& aSupported) {
  RemoteDecoderManagerChild::SetSupported(aLocation, aSupported);

  return IPC_OK();
}

void ContentChild::ConfigureThreadPerformanceHints(
    const hal::ProcessPriority& aPriority) {
  if (aPriority >= hal::PROCESS_PRIORITY_FOREGROUND) {
    static bool canUsePerformanceHintSession = true;
    if (!mPerformanceHintSession && canUsePerformanceHintSession) {
      nsTArray<PlatformThreadHandle> threads;
      Servo_ThreadPool_GetThreadHandles(&threads);
#ifdef XP_WIN
      threads.AppendElement(GetCurrentThread());
#else
      threads.AppendElement(pthread_self());
#endif

      mPerformanceHintSession = hal::CreatePerformanceHintSession(
          threads, GetPerformanceHintTarget(TimeDuration::FromMilliseconds(
                       nsRefreshDriver::DefaultInterval())));

      // Avoid repeatedly attempting to create a session if it is not
      // supported.
      canUsePerformanceHintSession = mPerformanceHintSession != nullptr;
    }

#ifdef MOZ_WIDGET_ANDROID
    // On Android if we are unable to use PerformanceHintManager then fall back
    // to setting the stylo threads' affinities to the performant cores. Android
    // automatically sets each thread's affinity to all cores when a process is
    // foregrounded, and to a subset of cores when the process is backgrounded.
    // We must therefore override this each time the process is foregrounded,
    // but we don't have to do anything when backgrounded.
    if (!mPerformanceHintSession) {
      if (const auto& cpuInfo = hal::GetHeterogeneousCpuInfo()) {
        // If CPUs are homogeneous there is no point setting affinity.
        if (cpuInfo->mBigCpus.Count() != cpuInfo->mTotalNumCpus) {
          BitSet<hal::HeterogeneousCpuInfo::MAX_CPUS> cpus = cpuInfo->mBigCpus;
          if (cpus.Count() < 2) {
            // From testing on a variety of devices it appears using only the
            // big cores gives best performance when there are 2 or more big
            // cores. If there are fewer than 2 big cores then additionally
            // using the medium cores performs better.
            cpus |= cpuInfo->mMediumCpus;
          }

          static_assert(
              hal::HeterogeneousCpuInfo::MAX_CPUS <= CPU_SETSIZE,
              "HeterogeneousCpuInfo::MAX_CPUS is too large for CPU_SETSIZE");

          cpu_set_t cpuset;
          CPU_ZERO(&cpuset);
          for (size_t i = 0; i < cpuInfo->mTotalNumCpus; i++) {
            if (cpus.test(i)) {
              CPU_SET(i, &cpuset);
            }
          }

          // Only set the affinity for the stylo threads, not the main thread.
          // Testing showed no difference in performance between the two
          // options, and as newly spawned threads automatically inherit their
          // parent's affinity mask there may be unintended consequences to
          // setting the main thread affinity.
          nsTArray<PlatformThreadHandle> threads;
          Servo_ThreadPool_GetThreadHandles(&threads);
          for (pthread_t thread : threads) {
            int ret = sched_setaffinity(pthread_gettid_np(thread),
                                        sizeof(cpu_set_t), &cpuset);
            // Occasionally sched_setaffinity fails, presumably due to a race
            // between us receiving the process foreground signal and whatever
            // is responsible for restricting which processes can use certain
            // cores. Trying again in a runnable seems to do the trick, but if
            // that still fails it's not the end of the world.
            if (ret < 0) {
              NS_DispatchToCurrentThread(NS_NewRunnableFunction(
                  "ContentChild::ConfigureThreadPerformanceHints",
                  [threads = std::move(threads), cpuset] {
                    for (pthread_t thread : threads) {
                      sched_setaffinity(pthread_gettid_np(thread),
                                        sizeof(cpu_set_t), &cpuset);
                    }
                  }));
              break;
            }
          }
        }
      }
    }
#endif
  } else {
    mPerformanceHintSession = nullptr;
  }
}

}  // namespace dom

#if defined(__OpenBSD__) && defined(MOZ_SANDBOX)

static LazyLogModule sPledgeLog("OpenBSDSandbox");

NS_IMETHODIMP
OpenBSDFindPledgeUnveilFilePath(const char* file, nsACString& result) {
  struct stat st;

  // Allow overriding files in /etc/$MOZ_APP_NAME
  result.Assign(nsPrintfCString("/etc/%s/%s", MOZ_APP_NAME, file));
  if (stat(PromiseFlatCString(result).get(), &st) == 0) {
    return NS_OK;
  }

  // Or look in the system default directory
  result.Assign(nsPrintfCString(
      "/usr/local/lib/%s/browser/defaults/preferences/%s", MOZ_APP_NAME, file));
  if (stat(PromiseFlatCString(result).get(), &st) == 0) {
    return NS_OK;
  }

  errx(1, "can't locate %s", file);
}

NS_IMETHODIMP
OpenBSDPledgePromises(const nsACString& aPath) {
  // Using NS_LOCAL_FILE_CONTRACTID/NS_LOCALFILEINPUTSTREAM_CONTRACTID requires
  // a lot of setup before they are supported and we want to pledge early on
  // before all of that, so read the file directly
  std::ifstream input(PromiseFlatCString(aPath).get());

  // Build up one line of pledge promises without comments
  nsAutoCString promises;
  bool disabled = false;
  int linenum = 0;
  for (std::string tLine; std::getline(input, tLine);) {
    nsAutoCString line(tLine.c_str());
    linenum++;

    // Cut off any comments at the end of the line, also catches lines
    // that are entirely a comment
    int32_t hash = line.FindChar('#');
    if (hash >= 0) {
      line = Substring(line, 0, hash);
    }
    line.CompressWhitespace(true, true);
    if (line.IsEmpty()) {
      continue;
    }

    if (linenum == 1 && line.EqualsLiteral("disable")) {
      disabled = true;
      break;
    }

    if (!promises.IsEmpty()) {
      promises.Append(" ");
    }
    promises.Append(line);
  }
  input.close();

  if (disabled) {
    warnx("%s: disabled", PromiseFlatCString(aPath).get());
  } else {
    MOZ_LOG(
        sPledgeLog, LogLevel::Debug,
        ("%s: pledge(%s)\n", PromiseFlatCString(aPath).get(), promises.get()));
    if (pledge(promises.get(), nullptr) != 0) {
      err(1, "%s: pledge(%s) failed", PromiseFlatCString(aPath).get(),
          promises.get());
    }
  }

  return NS_OK;
}

void ExpandUnveilPath(nsAutoCString& path) {
  // Expand $XDG_RUNTIME_DIR to the environment variable, or ~/.cache
  nsCString xdgRuntimeDir(PR_GetEnv("XDG_RUNTIME_DIR"));
  if (xdgRuntimeDir.IsEmpty()) {
    xdgRuntimeDir = "~/.cache";
  }
  path.ReplaceSubstring("$XDG_RUNTIME_DIR", xdgRuntimeDir.get());

  // Expand $XDG_CONFIG_HOME to the environment variable, or ~/.config
  nsCString xdgConfigHome(PR_GetEnv("XDG_CONFIG_HOME"));
  if (xdgConfigHome.IsEmpty()) {
    xdgConfigHome = "~/.config";
  }
  path.ReplaceSubstring("$XDG_CONFIG_HOME", xdgConfigHome.get());

  // Expand $XDG_CACHE_HOME to the environment variable, or ~/.cache
  nsCString xdgCacheHome(PR_GetEnv("XDG_CACHE_HOME"));
  if (xdgCacheHome.IsEmpty()) {
    xdgCacheHome = "~/.cache";
  }
  path.ReplaceSubstring("$XDG_CACHE_HOME", xdgCacheHome.get());

  // Expand $XDG_DATA_HOME to the environment variable, or ~/.local/share
  nsCString xdgDataHome(PR_GetEnv("XDG_DATA_HOME"));
  if (xdgDataHome.IsEmpty()) {
    xdgDataHome = "~/.local/share";
  }
  path.ReplaceSubstring("$XDG_DATA_HOME", xdgDataHome.get());

  // Expand leading ~ to the user's home directory
  nsCOMPtr<nsIFile> homeDir;
  nsresult rv =
      GetSpecialSystemDirectory(Unix_HomeDirectory, getter_AddRefs(homeDir));
  if (NS_FAILED(rv)) {
    errx(1, "failed getting home directory");
  }
  if (path.FindChar('~') == 0) {
    nsCString tHome(homeDir->NativePath());
    tHome.Append(Substring(path, 1, path.Length() - 1));
    path = tHome.get();
  }
}

void MkdirP(nsAutoCString& path) {
  // nsLocalFile::CreateAllAncestors would be nice to use

  nsAutoCString tPath("");
  for (const nsACString& dir : path.Split('/')) {
    struct stat st;

    if (dir.IsEmpty()) {
      continue;
    }

    tPath.Append("/");
    tPath.Append(dir);

    if (stat(tPath.get(), &st) == -1) {
      if (mkdir(tPath.get(), 0700) == -1) {
        err(1, "failed mkdir(%s) while MkdirP(%s)",
            PromiseFlatCString(tPath).get(), PromiseFlatCString(path).get());
      }
    }
  }
}

NS_IMETHODIMP
OpenBSDUnveilPaths(const nsACString& uPath, const nsACString& pledgePath) {
  // Using NS_LOCAL_FILE_CONTRACTID/NS_LOCALFILEINPUTSTREAM_CONTRACTID requires
  // a lot of setup before they are allowed/supported and we want to pledge and
  // unveil early on before all of that is setup
  std::ifstream input(PromiseFlatCString(uPath).get());

  bool disabled = false;
  int linenum = 0;
  for (std::string tLine; std::getline(input, tLine);) {
    nsAutoCString line(tLine.c_str());
    linenum++;

    // Cut off any comments at the end of the line, also catches lines
    // that are entirely a comment
    int32_t hash = line.FindChar('#');
    if (hash >= 0) {
      line = Substring(line, 0, hash);
    }
    line.CompressWhitespace(true, true);
    if (line.IsEmpty()) {
      continue;
    }

    if (linenum == 1 && line.EqualsLiteral("disable")) {
      disabled = true;
      break;
    }

    int32_t space = line.FindChar(' ');
    if (space <= 0) {
      errx(1, "%s: line %d: invalid format", PromiseFlatCString(uPath).get(),
           linenum);
    }

    nsAutoCString uPath(Substring(line, 0, space));
    ExpandUnveilPath(uPath);

    nsAutoCString perms(Substring(line, space + 1, line.Length() - space - 1));

    MOZ_LOG(sPledgeLog, LogLevel::Debug,
            ("%s: unveil(%s, %s)\n", PromiseFlatCString(uPath).get(),
             uPath.get(), perms.get()));
    if (unveil(uPath.get(), perms.get()) == -1 && errno != ENOENT) {
      err(1, "%s: unveil(%s, %s) failed", PromiseFlatCString(uPath).get(),
          uPath.get(), perms.get());
    }
  }
  input.close();

  if (disabled) {
    warnx("%s: disabled", PromiseFlatCString(uPath).get());
  } else {
    struct stat st;

    // Only unveil the pledgePath file if it's not already unveiled, otherwise
    // some containing directory will lose visibility.
    if (stat(PromiseFlatCString(pledgePath).get(), &st) == -1) {
      if (errno == ENOENT) {
        if (unveil(PromiseFlatCString(pledgePath).get(), "r") == -1) {
          err(1, "unveil(%s, r) failed", PromiseFlatCString(pledgePath).get());
        }
      } else {
        err(1, "stat(%s)", PromiseFlatCString(pledgePath).get());
      }
    }
  }

  return NS_OK;
}

bool StartOpenBSDSandbox(GeckoProcessType type, ipc::SandboxingKind kind) {
  nsAutoCString pledgeFile;
  nsAutoCString unveilFile;
  char binaryPath[MAXPATHLEN];

  switch (type) {
    case GeckoProcessType_Default: {
      OpenBSDFindPledgeUnveilFilePath("pledge.main", pledgeFile);
      OpenBSDFindPledgeUnveilFilePath("unveil.main", unveilFile);

      // Ensure dconf dir exists before we veil the filesystem
      nsAutoCString dConf("$XDG_RUNTIME_DIR/dconf");
      ExpandUnveilPath(dConf);
      MkdirP(dConf);
      break;
    }

    case GeckoProcessType_Content:
      OpenBSDFindPledgeUnveilFilePath("pledge.content", pledgeFile);
      OpenBSDFindPledgeUnveilFilePath("unveil.content", unveilFile);
      break;

    case GeckoProcessType_GPU:
      OpenBSDFindPledgeUnveilFilePath("pledge.gpu", pledgeFile);
      OpenBSDFindPledgeUnveilFilePath("unveil.gpu", unveilFile);
      break;

    case GeckoProcessType_Socket:
      OpenBSDFindPledgeUnveilFilePath("pledge.socket", pledgeFile);
      OpenBSDFindPledgeUnveilFilePath("unveil.socket", unveilFile);
      break;

    case GeckoProcessType_RDD:
      OpenBSDFindPledgeUnveilFilePath("pledge.rdd", pledgeFile);
      OpenBSDFindPledgeUnveilFilePath("unveil.rdd", unveilFile);
      break;

    case GeckoProcessType_Utility: {
      MOZ_RELEASE_ASSERT(kind <= SandboxingKind::COUNT,
                         "Should define a sandbox");
      switch (kind) {
        case ipc::SandboxingKind::GENERIC_UTILITY:
        default:
          OpenBSDFindPledgeUnveilFilePath("pledge.utility", pledgeFile);
          OpenBSDFindPledgeUnveilFilePath("unveil.utility", unveilFile);
          break;
      }
    } break;

    default:
      MOZ_ASSERT(false, "unknown process type");
      return false;
  }

  nsresult rv = mozilla::BinaryPath::Get(binaryPath);
  if (NS_FAILED(rv)) {
    errx(1, "failed to cache binary path ?");
  }

  if (NS_WARN_IF(NS_FAILED(OpenBSDUnveilPaths(unveilFile, pledgeFile)))) {
    errx(1, "failed reading/parsing %s", unveilFile.get());
  }

  if (NS_WARN_IF(NS_FAILED(OpenBSDPledgePromises(pledgeFile)))) {
    errx(1, "failed reading/parsing %s", pledgeFile.get());
  }

  // Don't overwrite an existing session dbus address, but ensure it is set
  if (!PR_GetEnv("DBUS_SESSION_BUS_ADDRESS")) {
    PR_SetEnv("DBUS_SESSION_BUS_ADDRESS=");
  }

  return true;
}
#endif

}  // namespace mozilla

/* static */
nsIDOMProcessChild* nsIDOMProcessChild::GetSingleton() {
  if (XRE_IsContentProcess()) {
    return mozilla::dom::ContentChild::GetSingleton();
  }
  return mozilla::dom::InProcessChild::Singleton();
}
