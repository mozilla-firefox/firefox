/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExtensionProtocolHandler.h"

#include "FileDescriptorFile.h"
#include "LoadInfo.h"
#include "SimpleChannel.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Omnijar.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/Try.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/NeckoChild.h"
#include "nsContentUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIFile.h"
#include "nsIFileChannel.h"
#include "nsIFileStreams.h"
#include "nsIFileURL.h"
#include "nsIInputStream.h"
#include "nsIInputStreamPump.h"
#include "nsIJARChannel.h"
#include "nsIJARURI.h"
#include "nsIMIMEService.h"
#include "nsIStreamConverterService.h"
#include "nsIStreamListener.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "prio.h"

#if defined(XP_WIN)
#  include "WinUtils.h"
#  include "nsILocalFileWin.h"
#endif

#if defined(XP_MACOSX)
#  include "nsMacUtilsImpl.h"
#endif

#define EXTENSION_SCHEME "moz-extension"
using mozilla::dom::Promise;
using mozilla::ipc::FileDescriptor;

// A list of file extensions containing purely static data, which can be loaded
// from an extension before the extension is fully ready. The main purpose of
// this is to allow image resources from theme XPIs to load quickly during
// browser startup.
//
// The layout of this array is chosen in order to prevent the need for runtime
// relocation, which an array of char* pointers would require. It also has the
// benefit of being more compact when the difference in length between the
// longest and average string is less than 8 bytes. The length of the
// char[] array must match the size of the longest entry in the list.
//
// This list must be kept sorted.
static const char sStaticFileExtensions[][5] = {
    // clang-format off
  "bmp",
  "gif",
  "ico",
  "jpeg",
  "jpg",
  "png",
  "svg",
    // clang-format on
};

namespace mozilla {

namespace net {

using extensions::URLInfo;

LazyLogModule gExtProtocolLog("ExtProtocol");
#undef LOG
#define LOG(...) MOZ_LOG(gExtProtocolLog, LogLevel::Debug, (__VA_ARGS__))

StaticRefPtr<ExtensionProtocolHandler> ExtensionProtocolHandler::sSingleton;

/**
 * Helper class used with SimpleChannel to asynchronously obtain an input
 * stream or file descriptor from the parent for a remote moz-extension load
 * from the child.
 */
class ExtensionStreamGetter final : public nsICancelable {
  NS_DECL_ISUPPORTS
  NS_DECL_NSICANCELABLE

 public:
  // To use when getting a remote input stream for a resource
  // in an unpacked extension.
  ExtensionStreamGetter(nsIURI* aURI, nsILoadInfo* aLoadInfo)
      : mURI(aURI), mLoadInfo(aLoadInfo), mIsJarChannel(false) {
    MOZ_ASSERT(aURI);
    MOZ_ASSERT(aLoadInfo);

    SetupEventTarget();
  }

  // To use when getting an FD for a packed extension JAR file
  // in order to load a resource.
  ExtensionStreamGetter(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                        already_AddRefed<nsIJARChannel> aJarChannel,
                        nsIFile* aJarFile)
      : mURI(aURI),
        mLoadInfo(aLoadInfo),
        mJarChannel(std::move(aJarChannel)),
        mJarFile(aJarFile),
        mIsJarChannel(true) {
    MOZ_ASSERT(aURI);
    MOZ_ASSERT(aLoadInfo);
    MOZ_ASSERT(mJarChannel);
    MOZ_ASSERT(aJarFile);

    SetupEventTarget();
  }

  void SetupEventTarget() {
    mMainThreadEventTarget = GetMainThreadSerialEventTarget();
  }

  // Get an input stream or file descriptor from the parent asynchronously.
  RequestOrReason GetAsync(nsIStreamListener* aListener, nsIChannel* aChannel);

  // Handle an input stream being returned from the parent
  void OnStream(already_AddRefed<nsIInputStream> aStream);

  // Handle file descriptor being returned from the parent
  void OnFD(const FileDescriptor& aFD);

  static void CancelRequest(nsIStreamListener* aListener, nsIChannel* aChannel,
                            nsresult aResult);

 private:
  ~ExtensionStreamGetter() = default;

  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<nsILoadInfo> mLoadInfo;
  nsCOMPtr<nsIJARChannel> mJarChannel;
  nsCOMPtr<nsIInputStreamPump> mPump;
  nsCOMPtr<nsIFile> mJarFile;
  nsCOMPtr<nsIStreamListener> mListener;
  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsISerialEventTarget> mMainThreadEventTarget;
  bool mIsJarChannel;
  bool mCanceled{false};
  nsresult mStatus{NS_OK};
};

NS_IMPL_ISUPPORTS(ExtensionStreamGetter, nsICancelable)

class ExtensionJARFileOpener final : public nsISupports {
 public:
  ExtensionJARFileOpener(nsIFile* aFile,
                         NeckoParent::GetExtensionFDResolver& aResolve)
      : mFile(aFile), mResolve(aResolve) {
    MOZ_ASSERT(aFile);
    MOZ_ASSERT(aResolve);
  }

  NS_IMETHOD OpenFile() {
    MOZ_ASSERT(!NS_IsMainThread());
    AutoFDClose prFileDesc;

#if defined(XP_WIN)
    nsresult rv;
    nsCOMPtr<nsILocalFileWin> winFile = do_QueryInterface(mFile, &rv);
    MOZ_ASSERT(winFile);
    if (NS_SUCCEEDED(rv)) {
      rv = winFile->OpenNSPRFileDescShareDelete(PR_RDONLY, 0,
                                                getter_Transfers(prFileDesc));
    }
#else
    nsresult rv =
        mFile->OpenNSPRFileDesc(PR_RDONLY, 0, getter_Transfers(prFileDesc));
#endif /* XP_WIN */

    if (NS_SUCCEEDED(rv)) {
      mFD = FileDescriptor(FileDescriptor::PlatformHandleType(
          PR_FileDesc2NativeHandle(prFileDesc.get())));
    }

    nsCOMPtr<nsIRunnable> event =
        mozilla::NewRunnableMethod("ExtensionJarFileFDResolver", this,
                                   &ExtensionJARFileOpener::SendBackFD);

    rv = NS_DispatchToMainThread(event, nsIEventTarget::DISPATCH_NORMAL);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "NS_DispatchToMainThread");
    return NS_OK;
  }

  NS_IMETHOD SendBackFD() {
    MOZ_ASSERT(NS_IsMainThread());
    mResolve(mFD);
    mResolve = nullptr;
    return NS_OK;
  }

  NS_DECL_THREADSAFE_ISUPPORTS

 private:
  virtual ~ExtensionJARFileOpener() = default;

  nsCOMPtr<nsIFile> mFile;
  NeckoParent::GetExtensionFDResolver mResolve;
  FileDescriptor mFD;
};

NS_IMPL_ISUPPORTS(ExtensionJARFileOpener, nsISupports)

// The amount of time, in milliseconds, that the file opener thread will remain
// allocated after it is used. This value chosen because to match other uses
// of LazyIdleThread.
#define DEFAULT_THREAD_TIMEOUT_MS 30000

// Request an FD or input stream from the parent.
RequestOrReason ExtensionStreamGetter::GetAsync(nsIStreamListener* aListener,
                                                nsIChannel* aChannel) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_ASSERT(mMainThreadEventTarget);

  mListener = aListener;
  mChannel = aChannel;

  nsCOMPtr<nsICancelable> cancelableRequest(this);

  RefPtr<ExtensionStreamGetter> self = this;
  if (mIsJarChannel) {
    // Request an FD for this moz-extension URI
    gNeckoChild->SendGetExtensionFD(mURI)->Then(
        mMainThreadEventTarget, __func__,
        [self](const FileDescriptor& fd) { self->OnFD(fd); },
        [self](const mozilla::ipc::ResponseRejectReason) {
          self->OnFD(FileDescriptor());
        });
    return RequestOrCancelable(WrapNotNull(cancelableRequest));
  }

  // Request an input stream for this moz-extension URI
  gNeckoChild->SendGetExtensionStream(mURI)->Then(
      mMainThreadEventTarget, __func__,
      [self](const RefPtr<nsIInputStream>& stream) {
        self->OnStream(do_AddRef(stream));
      },
      [self](const mozilla::ipc::ResponseRejectReason) {
        self->OnStream(nullptr);
      });
  return RequestOrCancelable(WrapNotNull(cancelableRequest));
}

// Called to cancel the ongoing async request.
NS_IMETHODIMP
ExtensionStreamGetter::Cancel(nsresult aStatus) {
  if (mCanceled) {
    return NS_OK;
  }

  mCanceled = true;
  mStatus = aStatus;

  if (mPump) {
    mPump->CancelWithReason(aStatus, "ExtensionStreamGetter::Cancel"_ns);
    mPump = nullptr;
  }

  if (mIsJarChannel && mJarChannel) {
    mJarChannel->CancelWithReason(aStatus, "ExtensionStreamGetter::Cancel"_ns);
  }

  return NS_OK;
}

// static
void ExtensionStreamGetter::CancelRequest(nsIStreamListener* aListener,
                                          nsIChannel* aChannel,
                                          nsresult aResult) {
  MOZ_ASSERT(aListener);
  MOZ_ASSERT(aChannel);

  aListener->OnStartRequest(aChannel);
  aListener->OnStopRequest(aChannel, aResult);
  aChannel->CancelWithReason(NS_BINDING_ABORTED,
                             "ExtensionStreamGetter::CancelRequest"_ns);
}

// Handle an input stream sent from the parent.
void ExtensionStreamGetter::OnStream(already_AddRefed<nsIInputStream> aStream) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_ASSERT(mChannel);
  MOZ_ASSERT(mListener);
  MOZ_ASSERT(mMainThreadEventTarget);

  nsCOMPtr<nsIInputStream> stream = std::move(aStream);
  nsCOMPtr<nsIChannel> channel = std::move(mChannel);

  // We must keep an owning reference to the listener
  // until we pass it on to AsyncRead.
  nsCOMPtr<nsIStreamListener> listener = std::move(mListener);

  if (mCanceled) {
    // The channel that has created this stream getter has been canceled.
    CancelRequest(listener, channel, mStatus);
    return;
  }

  if (!stream) {
    // The parent didn't send us back a stream.
    CancelRequest(listener, channel, NS_ERROR_FILE_ACCESS_DENIED);
    return;
  }

  nsCOMPtr<nsIInputStreamPump> pump;
  nsresult rv = NS_NewInputStreamPump(getter_AddRefs(pump), stream.forget(), 0,
                                      0, false, mMainThreadEventTarget);
  if (NS_FAILED(rv)) {
    CancelRequest(listener, channel, rv);
    return;
  }

  rv = pump->AsyncRead(listener);
  if (NS_FAILED(rv)) {
    CancelRequest(listener, channel, rv);
    return;
  }

  mPump = std::move(pump);
}

// Handle an FD sent from the parent.
void ExtensionStreamGetter::OnFD(const FileDescriptor& aFD) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_ASSERT(mChannel);
  MOZ_ASSERT(mListener);

  nsCOMPtr<nsIChannel> channel = std::move(mChannel);

  // We must keep an owning reference to the listener
  // until we pass it on to AsyncOpen.
  nsCOMPtr<nsIStreamListener> listener = std::move(mListener);

  if (mCanceled) {
    // The channel that has created this stream getter has been canceled.
    CancelRequest(listener, channel, mStatus);
    return;
  }

  if (!aFD.IsValid()) {
    // The parent didn't send us back a valid file descriptor.
    CancelRequest(listener, channel, NS_ERROR_FILE_ACCESS_DENIED);
    return;
  }

  RefPtr<FileDescriptorFile> fdFile = new FileDescriptorFile(aFD, mJarFile);
  mJarChannel->SetJarFile(fdFile);
  nsresult rv = mJarChannel->AsyncOpen(listener);
  if (NS_FAILED(rv)) {
    CancelRequest(listener, channel, rv);
  }
}

NS_IMPL_QUERY_INTERFACE(ExtensionProtocolHandler,
                        nsISubstitutingProtocolHandler, nsIProtocolHandler,
                        nsISupportsWeakReference)
NS_IMPL_ADDREF_INHERITED(ExtensionProtocolHandler, SubstitutingProtocolHandler)
NS_IMPL_RELEASE_INHERITED(ExtensionProtocolHandler, SubstitutingProtocolHandler)

already_AddRefed<ExtensionProtocolHandler>
ExtensionProtocolHandler::GetSingleton() {
  static StaticMutex sMutex;
  StaticMutexAutoLock lock(sMutex);
  if (!sSingleton) {
    if (NS_IsMainThread()) {
      sSingleton = new ExtensionProtocolHandler();
      ClearOnShutdown(&sSingleton);
    } else {
      StaticMutexAutoUnlock unlock(sMutex);
      RefPtr<nsIRunnable> r = NS_NewRunnableFunction(
          "ExtensionProtocolHandler::GetSingleton", []() {
            StaticMutexAutoLock lock(sMutex);
            if (!sSingleton) {
              sSingleton = new ExtensionProtocolHandler();
              ClearOnShutdown(&sSingleton);
            }
          });
      SyncRunnable::DispatchToThread(GetMainThreadSerialEventTarget(), r);
    }
  }
  return do_AddRef(sSingleton);
}

ExtensionProtocolHandler::ExtensionProtocolHandler()
    : SubstitutingProtocolHandler(EXTENSION_SCHEME) {
  // Note, extensions.webextensions.protocol.remote=false is for
  // debugging purposes only. With process-level sandboxing, child
  // processes (specifically content and extension processes), will
  // not be able to load most moz-extension URI's when the pref is
  // set to false.
  mUseRemoteFileChannels =
      IsNeckoChild() &&
      Preferences::GetBool("extensions.webextensions.protocol.remote");
}

static inline ExtensionPolicyService& EPS() {
  return ExtensionPolicyService::GetSingleton();
}

bool ExtensionProtocolHandler::ResolveSpecialCases(const nsACString& aHost,
                                                   const nsACString& aPath,
                                                   const nsACString& aPathname,
                                                   nsACString& aResult) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread(),
                        "The ExtensionPolicyService is not thread safe");
  // Create special moz-extension://foo/_generated_background_page.html page
  // for all registered extensions. We can't just do this as a substitution
  // because substitutions can only match on host.
  if (!SubstitutingProtocolHandler::HasSubstitution(aHost)) {
    return false;
  }

  if (aPathname.EqualsLiteral("/_generated_background_page.html")) {
    (void)EPS().GetGeneratedBackgroundPageUrl(aHost, aResult);
    return !aResult.IsEmpty();
  }

  return false;
}

// For file or JAR URI's, substitute in a remote channel.
Result<Ok, nsresult> ExtensionProtocolHandler::SubstituteRemoteChannel(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIChannel** aRetVal) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_TRY(aURI ? NS_OK : NS_ERROR_INVALID_ARG);
  MOZ_TRY(aLoadInfo ? NS_OK : NS_ERROR_INVALID_ARG);

  nsAutoCString unResolvedSpec;
  MOZ_TRY(aURI->GetSpec(unResolvedSpec));

  nsAutoCString resolvedSpec;
  MOZ_TRY(ResolveURI(aURI, resolvedSpec));

  // Use the target URI scheme to determine if this is a packed or unpacked
  // extension URI. For unpacked extensions, we'll request an input stream
  // from the parent. For a packed extension, we'll request a file descriptor
  // for the JAR file.
  nsAutoCString scheme;
  MOZ_TRY(net_ExtractURLScheme(resolvedSpec, scheme));

  if (scheme.EqualsLiteral("file")) {
    // Unpacked extension
    SubstituteRemoteFileChannel(aURI, aLoadInfo, resolvedSpec, aRetVal);
    return Ok();
  }

  if (scheme.EqualsLiteral("jar")) {
    // Packed extension
    return SubstituteRemoteJarChannel(aURI, aLoadInfo, resolvedSpec, aRetVal);
  }

  // Only unpacked resource files and JAR files are remoted.
  // No other moz-extension loads should be reading from the filesystem.
  return Ok();
}

void OpenWhenReady(
    Promise* aPromise, nsIStreamListener* aListener, nsIChannel* aChannel,
    const std::function<nsresult(nsIStreamListener*, nsIChannel*)>& aCallback) {
  nsCOMPtr<nsIStreamListener> listener(aListener);
  nsCOMPtr<nsIChannel> channel(aChannel);

  (void)aPromise->ThenWithCycleCollectedArgs(
      [channel, aCallback](
          JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult& aRv,
          nsIStreamListener* aListener) -> already_AddRefed<Promise> {
        nsresult rv = aCallback(aListener, channel);
        if (NS_FAILED(rv)) {
          ExtensionStreamGetter::CancelRequest(aListener, channel, rv);
        }
        return nullptr;
      },
      listener);
}

nsresult ExtensionProtocolHandler::SubstituteChannel(nsIURI* aURI,
                                                     nsILoadInfo* aLoadInfo,
                                                     nsIChannel** result) {
  if (mUseRemoteFileChannels) {
    MOZ_TRY(SubstituteRemoteChannel(aURI, aLoadInfo, result));
  }

  auto* policy = EPS().GetByURL(aURI);
  NS_ENSURE_TRUE(policy, NS_ERROR_UNEXPECTED);

  RefPtr<dom::Promise> readyPromise(policy->ReadyPromise());

  nsresult rv;
  nsCOMPtr<nsIURL> url = do_QueryInterface(aURI, &rv);
  MOZ_TRY(rv);

  nsAutoCString ext;
  MOZ_TRY(url->GetFileExtension(ext));
  ToLowerCase(ext);

  nsCOMPtr<nsIChannel> channel;
  if (ext.EqualsLiteral("css")) {
    // Filter CSS files to replace locale message tokens with localized strings.
    static const auto convert = [](nsIStreamListener* listener,
                                   nsIChannel* channel,
                                   nsIChannel* origChannel) -> nsresult {
      nsresult rv;
      nsCOMPtr<nsIStreamConverterService> convService;
      convService = mozilla::components::StreamConverter::Service(&rv);
      MOZ_TRY(rv);

      nsCOMPtr<nsIURI> uri;
      MOZ_TRY(channel->GetURI(getter_AddRefs(uri)));

      const char* kFromType = "application/vnd.mozilla.webext.unlocalized";
      const char* kToType = "text/css";

      nsCOMPtr<nsIStreamListener> converter;
      MOZ_TRY(convService->AsyncConvertData(kFromType, kToType, listener, uri,
                                            getter_AddRefs(converter)));

      return origChannel->AsyncOpen(converter);
    };

    channel = NS_NewSimpleChannel(
        aURI, aLoadInfo, *result,
        [readyPromise](nsIStreamListener* listener, nsIChannel* channel,
                       nsIChannel* origChannel) -> RequestOrReason {
          if (readyPromise) {
            nsCOMPtr<nsIChannel> chan(channel);
            OpenWhenReady(
                readyPromise, listener, origChannel,
                [chan](nsIStreamListener* aListener, nsIChannel* aChannel) {
                  return convert(aListener, chan, aChannel);
                });
          } else {
            MOZ_TRY(convert(listener, channel, origChannel));
          }
          nsCOMPtr<nsIRequest> request(origChannel);
          return RequestOrCancelable(WrapNotNull(request));
        });
    channel->SetContentType("text/css"_ns);
  } else if (readyPromise) {
    size_t matchIdx;
    if (BinarySearchIf(
            sStaticFileExtensions, 0, std::size(sStaticFileExtensions),
            [&ext](const char* aOther) {
              return Compare(ext, nsDependentCString(aOther));
            },
            &matchIdx)) {
      // This is a static resource that shouldn't depend on the extension being
      // ready. Don't bother waiting for it.
      return NS_OK;
    }

    channel = NS_NewSimpleChannel(
        aURI, aLoadInfo, *result,
        [readyPromise](nsIStreamListener* listener, nsIChannel* channel,
                       nsIChannel* origChannel) -> RequestOrReason {
          OpenWhenReady(readyPromise, listener, origChannel,
                        [](nsIStreamListener* aListener, nsIChannel* aChannel) {
                          return aChannel->AsyncOpen(aListener);
                        });

          nsCOMPtr<nsIRequest> request(origChannel);
          return RequestOrCancelable(WrapNotNull(request));
        });
    SetContentType(aURI, channel);
  } else {
    return NS_OK;
  }

  NS_ENSURE_TRUE(channel, NS_ERROR_OUT_OF_MEMORY);
  if (aLoadInfo) {
    nsCOMPtr<nsILoadInfo> loadInfo =
        static_cast<LoadInfo*>(aLoadInfo)->CloneForNewRequest();
    (*result)->SetLoadInfo(loadInfo);
  }

  channel.swap(*result);
  return NS_OK;
}

Result<bool, nsresult> ExtensionProtocolHandler::AllowExternalResource(
    nsIFile* aExtensionDir, nsIFile* aRequestedFile) {
  MOZ_ASSERT(!IsNeckoChild());

#if defined(XP_WIN)
  // On Windows, non-package builds don't use symlinks so we never need to
  // allow a resource from outside of the extension dir.
  return false;
#else
  if (mozilla::IsPackagedBuild()) {
    return false;
  }

  // On Mac and Linux unpackaged dev builds, system extensions use
  // symlinks to point to resources in the repo dir which we have to
  // allow loading. Before we allow an unpacked extension to load a
  // resource outside of the extension dir, we make sure the extension
  // dir is within the app directory.
  bool result = MOZ_TRY(AppDirContains(aExtensionDir));
  if (!result) {
    return false;
  }

#  if defined(XP_MACOSX)
  // Additionally, on Mac dev builds, we make sure that the requested
  // resource is within the repo dir. We don't perform this check on Linux
  // because we don't have a reliable path to the repo dir on Linux.
  return DevRepoContains(aRequestedFile);
#  else /* XP_MACOSX */
  return true;
#  endif
#endif /* defined(XP_WIN) */
}

#if defined(XP_MACOSX)
// The |aRequestedFile| argument must already be Normalize()'d
Result<bool, nsresult> ExtensionProtocolHandler::DevRepoContains(
    nsIFile* aRequestedFile) {
  MOZ_ASSERT(!mozilla::IsPackagedBuild());
  MOZ_ASSERT(!IsNeckoChild());

  // On the first invocation, set mDevRepo and mDevObjDir
  if (!mAlreadyCheckedDevRepo) {
    mAlreadyCheckedDevRepo = true;
    MOZ_TRY(nsMacUtilsImpl::GetRepoDir(getter_AddRefs(mDevRepo)));
    nsresult rv = nsMacUtilsImpl::GetObjDir(getter_AddRefs(mDevObjDir));
    if (NS_FAILED(rv)) {
      // GetObjDir is never expected to fail, but if it does, don't let it
      // trip the handling of mDevRepo.
      LOG("GetObjDir() failed: %x", static_cast<uint32_t>(rv));
    }
    if (MOZ_LOG_TEST(gExtProtocolLog, LogLevel::Debug)) {
      nsAutoCString repoPath;
      (void)mDevRepo->GetNativePath(repoPath);
      LOG("Repo path: %s", repoPath.get());
      nsAutoCString objdirPath;
      (void)mDevObjDir->GetNativePath(objdirPath);
      LOG("objdir path: %s", objdirPath.get());
    }
  }

  bool result = false;
  if (mDevRepo) {
    MOZ_TRY(mDevRepo->Contains(aRequestedFile, &result));
  }
  if (!result && mDevObjDir) {
    MOZ_TRY(mDevObjDir->Contains(aRequestedFile, &result));
  }
  return result;
}
#endif /* XP_MACOSX */

#if !defined(XP_WIN)
Result<bool, nsresult> ExtensionProtocolHandler::AppDirContains(
    nsIFile* aExtensionDir) {
  MOZ_ASSERT(!mozilla::IsPackagedBuild());
  MOZ_ASSERT(!IsNeckoChild());

  // On the first invocation, set mAppDir
  if (!mAlreadyCheckedAppDir) {
    mAlreadyCheckedAppDir = true;
    MOZ_TRY(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(mAppDir)));
    nsresult rv = mAppDir->Normalize();
    if (NS_FAILED(rv)) {
      LOG("Failed to normalize AppDir: %x", static_cast<uint32_t>(rv));
    }
    if (MOZ_LOG_TEST(gExtProtocolLog, LogLevel::Debug)) {
      nsAutoCString appDirPath;
      (void)mAppDir->GetNativePath(appDirPath);
      LOG("AppDir path: %s", appDirPath.get());
    }
  }

  bool result = false;
  if (mAppDir) {
    MOZ_TRY(mAppDir->Contains(aExtensionDir, &result));
  }
  return result;
}
#endif /* !defined(XP_WIN) */

static void LogExternalResourceError(nsIFile* aExtensionDir,
                                     nsIFile* aRequestedFile) {
  MOZ_ASSERT(aExtensionDir);
  MOZ_ASSERT(aRequestedFile);

  LOG("Rejecting external unpacked extension resource [%s] from "
      "extension directory [%s]",
      aRequestedFile->HumanReadablePath().get(),
      aExtensionDir->HumanReadablePath().get());
}

Result<nsCOMPtr<nsIInputStream>, nsresult> ExtensionProtocolHandler::NewStream(
    nsIURI* aChildURI, bool* aTerminateSender) {
  MOZ_ASSERT(!IsNeckoChild());
  MOZ_TRY(aChildURI ? NS_OK : NS_ERROR_INVALID_ARG);
  MOZ_TRY(aTerminateSender ? NS_OK : NS_ERROR_INVALID_ARG);

  *aTerminateSender = true;
  nsresult rv;

  // We should never receive a URI that isn't for a moz-extension because
  // these requests ordinarily come from the child's ExtensionProtocolHandler.
  // Ensure this request is for a moz-extension URI. A rogue child process
  // could send us any URI.
  if (!aChildURI->SchemeIs(EXTENSION_SCHEME)) {
    return Err(NS_ERROR_UNKNOWN_PROTOCOL);
  }

  // For errors after this point, we want to propagate the error to
  // the child, but we don't force the child to be terminated because
  // the error is likely to be due to a bug in the extension.
  *aTerminateSender = false;

  /*
   * Make sure there is a substitution installed for the host found
   * in the child's request URI and make sure the host resolves to
   * a directory.
   */

  nsAutoCString host;
  MOZ_TRY(aChildURI->GetAsciiHost(host));

  // Lookup the directory this host string resolves to
  nsCOMPtr<nsIURI> baseURI;
  MOZ_TRY(GetSubstitution(host, getter_AddRefs(baseURI)));

  // The result should be a file URL for the extension base dir
  nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(baseURI, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIFile> extensionDir;
  MOZ_TRY(fileURL->GetFile(getter_AddRefs(extensionDir)));

  bool isDirectory = false;
  MOZ_TRY(extensionDir->IsDirectory(&isDirectory));
  if (!isDirectory) {
    // The host should map to a directory for unpacked extensions
    return Err(NS_ERROR_FILE_NOT_DIRECTORY);
  }

  // Make sure the child URI resolves to a file URI then get a file
  // channel for the request. The resultant channel should be a
  // file channel because we only request remote streams for unpacked
  // extension resource loads where the URI resolves to a file.
  nsAutoCString resolvedSpec;
  MOZ_TRY(ResolveURI(aChildURI, resolvedSpec));

  nsAutoCString resolvedScheme;
  MOZ_TRY(net_ExtractURLScheme(resolvedSpec, resolvedScheme));
  if (!resolvedScheme.EqualsLiteral("file")) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIURI> resolvedURI;
  MOZ_TRY(ioService->NewURI(resolvedSpec, nullptr, nullptr,
                            getter_AddRefs(resolvedURI)));

  // We use the system principal to get a file channel for the request,
  // but only after we've checked (above) that the child URI is of
  // moz-extension scheme and that the URI host maps to a directory.
  nsCOMPtr<nsIChannel> channel;
  MOZ_TRY(NS_NewChannel(getter_AddRefs(channel), resolvedURI,
                        nsContentUtils::GetSystemPrincipal(),
                        nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                        nsIContentPolicy::TYPE_OTHER));

  nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(channel, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIFile> requestedFile;
  MOZ_TRY(fileChannel->GetFile(getter_AddRefs(requestedFile)));

  /*
   * Make sure the file we resolved to is within the extension directory.
   */

  // Normalize paths for sane comparisons. nsIFile::Contains depends on
  // it for reliable subpath checks.
  MOZ_TRY(extensionDir->Normalize());
  MOZ_TRY(requestedFile->Normalize());
#if defined(XP_WIN)
  if (!widget::WinUtils::ResolveJunctionPointsAndSymLinks(extensionDir) ||
      !widget::WinUtils::ResolveJunctionPointsAndSymLinks(requestedFile)) {
    return Err(NS_ERROR_FILE_ACCESS_DENIED);
  }
#endif

  bool isResourceFromExtensionDir = false;
  MOZ_TRY(extensionDir->Contains(requestedFile, &isResourceFromExtensionDir));
  if (!isResourceFromExtensionDir) {
    bool isAllowed =
        MOZ_TRY(AllowExternalResource(extensionDir, requestedFile));
    if (!isAllowed) {
      LogExternalResourceError(extensionDir, requestedFile);
      return Err(NS_ERROR_FILE_ACCESS_DENIED);
    }
  }

  nsCOMPtr<nsIInputStream> inputStream = MOZ_TRY(NS_NewLocalFileInputStream(
      requestedFile, PR_RDONLY, -1, nsIFileInputStream::DEFER_OPEN));

  return inputStream;
}

Result<Ok, nsresult> ExtensionProtocolHandler::NewFD(
    nsIURI* aChildURI, bool* aTerminateSender,
    NeckoParent::GetExtensionFDResolver& aResolve) {
  MOZ_ASSERT(!IsNeckoChild());
  MOZ_TRY(aChildURI ? NS_OK : NS_ERROR_INVALID_ARG);
  MOZ_TRY(aTerminateSender ? NS_OK : NS_ERROR_INVALID_ARG);

  *aTerminateSender = true;
  nsresult rv;

  // Ensure this is a moz-extension URI
  if (!aChildURI->SchemeIs(EXTENSION_SCHEME)) {
    return Err(NS_ERROR_UNKNOWN_PROTOCOL);
  }

  // For errors after this point, we want to propagate the error to
  // the child, but we don't force the child to be terminated.
  *aTerminateSender = false;

  nsAutoCString host;
  MOZ_TRY(aChildURI->GetAsciiHost(host));

  // We expect the host string to map to a JAR file because the URI
  // should refer to a web accessible resource for an enabled extension.
  nsCOMPtr<nsIURI> subURI;
  MOZ_TRY(GetSubstitution(host, getter_AddRefs(subURI)));

  nsCOMPtr<nsIJARURI> jarURI = do_QueryInterface(subURI, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIURI> innerFileURI;
  MOZ_TRY(jarURI->GetJARFile(getter_AddRefs(innerFileURI)));

  nsCOMPtr<nsIFileURL> innerFileURL = do_QueryInterface(innerFileURI, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIFile> jarFile;
  MOZ_TRY(innerFileURL->GetFile(getter_AddRefs(jarFile)));

  if (!mFileOpenerThread) {
    mFileOpenerThread = new LazyIdleThread(DEFAULT_THREAD_TIMEOUT_MS,
                                           "ExtensionProtocolHandler");
  }

  RefPtr<ExtensionJARFileOpener> fileOpener =
      new ExtensionJARFileOpener(jarFile, aResolve);

  nsCOMPtr<nsIRunnable> event = mozilla::NewRunnableMethod(
      "ExtensionJarFileOpener", fileOpener, &ExtensionJARFileOpener::OpenFile);

  MOZ_TRY(mFileOpenerThread->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL));

  return Ok();
}

// Set the channel's content type using the provided URI's type

// static
void ExtensionProtocolHandler::SetContentType(nsIURI* aURI,
                                              nsIChannel* aChannel) {
  nsresult rv;
  nsCOMPtr<nsIMIMEService> mime = do_GetService("@mozilla.org/mime;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString contentType;
    rv = mime->GetTypeFromURI(aURI, contentType);
    if (NS_SUCCEEDED(rv)) {
      (void)aChannel->SetContentType(contentType);
    }
  }
}

// Gets a SimpleChannel that wraps the provided ExtensionStreamGetter

// static
void ExtensionProtocolHandler::NewSimpleChannel(
    nsIURI* aURI, nsILoadInfo* aLoadinfo, ExtensionStreamGetter* aStreamGetter,
    nsIChannel** aRetVal) {
  nsCOMPtr<nsIChannel> channel = NS_NewSimpleChannel(
      aURI, aLoadinfo, aStreamGetter,
      [](nsIStreamListener* listener, nsIChannel* simpleChannel,
         ExtensionStreamGetter* getter) -> RequestOrReason {
        return getter->GetAsync(listener, simpleChannel);
      },
      [](bool async, nsIChannel* simpleChannel,
         ExtensionStreamGetter* getter) -> InputStreamOrReason {
        // nsBaseChannel::Open() may call us with async=false. When we return
        // NS_ERROR_NOT_IMPLEMENTED, it falls back to NS_ImplementChannelOpen
        // which spins the event loop, which has side effects (bug 2092510), so
        // log the action for visibility if needed.

        if (!async) {
          LOG("Synchronous open may fall back to spinning event loop.");
        }

        return Err(NS_ERROR_NOT_IMPLEMENTED);
      });

  SetContentType(aURI, channel);
  channel.swap(*aRetVal);
}

// Gets a SimpleChannel that wraps the provided channel

// static
void ExtensionProtocolHandler::NewSimpleChannel(nsIURI* aURI,
                                                nsILoadInfo* aLoadinfo,
                                                nsIChannel* aChannel,
                                                nsIChannel** aRetVal) {
  // NOTE: Despite the generic method name and input, this private method has
  // only one caller: ExtensionProtocolHandler::SubstituteRemoteJarChannel
  // calls this when we encounter a JAR channel whose FD is already cached.

  nsCOMPtr<nsIChannel> channel = NS_NewSimpleChannel(
      aURI, aLoadinfo, aChannel,
      [](nsIStreamListener* listener, nsIChannel* simpleChannel,
         nsIChannel* origChannel) -> RequestOrReason {
        nsresult rv = origChannel->AsyncOpen(listener);
        if (NS_FAILED(rv)) {
          simpleChannel->Cancel(NS_BINDING_ABORTED);
          return Err(rv);
        }
        nsCOMPtr<nsIRequest> request(origChannel);
        return RequestOrCancelable(WrapNotNull(request));
      },
      [](bool async, nsIChannel* simpleChannel,
         nsIChannel* origChannel) -> InputStreamOrReason {
        if (async) {
          return Err(NS_ERROR_NOT_IMPLEMENTED);
        }

        // nsBaseChannel::Open() may call us with async=false. If we returned
        // NS_ERROR_NOT_IMPLEMENTED, it falls back to NS_ImplementChannelOpen
        // which spins the event loop. Nested event loops cause undesired side
        // effects (bug 2092510), so try reading directly where possible.

        // Note: origChannel is the JAR channel (per comment above).
        LOG("[JARChannel %p] Synchronous open of cached JAR.", origChannel);

        nsCOMPtr<nsIInputStream> stream;
        MOZ_TRY(origChannel->Open(getter_AddRefs(stream)));
        return stream;
      });

  SetContentType(aURI, channel);
  channel.swap(*aRetVal);
}

void ExtensionProtocolHandler::SubstituteRemoteFileChannel(
    nsIURI* aURI, nsILoadInfo* aLoadinfo, nsACString& aResolvedFileSpec,
    nsIChannel** aRetVal) {
  MOZ_ASSERT(IsNeckoChild());

  LOG("Will read via parent: %s", PromiseFlatCString(aResolvedFileSpec).get());

  RefPtr<ExtensionStreamGetter> streamGetter =
      new ExtensionStreamGetter(aURI, aLoadinfo);

  NewSimpleChannel(aURI, aLoadinfo, streamGetter, aRetVal);
}

static Result<Ok, nsresult> LogCacheCheck(const nsIJARChannel* aJarChannel,
                                          nsIJARURI* aJarURI, bool aIsCached) {
  nsresult rv;

  nsCOMPtr<nsIURI> innerFileURI;
  MOZ_TRY(aJarURI->GetJARFile(getter_AddRefs(innerFileURI)));

  nsCOMPtr<nsIFileURL> innerFileURL = do_QueryInterface(innerFileURI, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIFile> jarFile;
  MOZ_TRY(innerFileURL->GetFile(getter_AddRefs(jarFile)));

  nsAutoCString uriSpec, jarSpec;
  (void)aJarURI->GetSpec(uriSpec);
  (void)innerFileURI->GetSpec(jarSpec);
  LOG("[JARChannel %p] Cache %s: %s (%s)", aJarChannel,
      aIsCached ? "hit" : "miss", uriSpec.get(), jarSpec.get());

  return Ok();
}

Result<Ok, nsresult> ExtensionProtocolHandler::SubstituteRemoteJarChannel(
    nsIURI* aURI, nsILoadInfo* aLoadinfo, nsACString& aResolvedSpec,
    nsIChannel** aRetVal) {
  MOZ_ASSERT(IsNeckoChild());
  nsresult rv;

  // Build a JAR URI for this jar:file:// URI and use it to extract the
  // inner file URI.
  nsCOMPtr<nsIURI> uri;
  MOZ_TRY(NS_NewURI(getter_AddRefs(uri), aResolvedSpec));

  nsCOMPtr<nsIJARURI> jarURI = do_QueryInterface(uri, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIJARChannel> jarChannel = do_QueryInterface(*aRetVal, &rv);
  MOZ_TRY(rv);

  bool isCached = false;
  MOZ_TRY(jarChannel->EnsureCached(&isCached));
  if (MOZ_LOG_TEST(gExtProtocolLog, LogLevel::Debug)) {
    (void)LogCacheCheck(jarChannel, jarURI, isCached);
  }

  if (isCached) {
    // Using a SimpleChannel with an ExtensionStreamGetter here (like the
    // non-cached JAR case) isn't needed to load the extension resource
    // because we don't need to ask the parent for an FD for the JAR, but
    // wrapping the JARChannel in a SimpleChannel allows HTTP forwarding to
    // moz-extension URI's to work because HTTP forwarding requires the
    // target channel implement nsIChildChannel.
    // This also enables a direct read from the cached FD if needed.
    NewSimpleChannel(aURI, aLoadinfo, jarChannel.get(), aRetVal);
    return Ok();
  }

  nsCOMPtr<nsIURI> innerFileURI;
  MOZ_TRY(jarURI->GetJARFile(getter_AddRefs(innerFileURI)));

  nsCOMPtr<nsIFileURL> innerFileURL = do_QueryInterface(innerFileURI, &rv);
  MOZ_TRY(rv);

  nsCOMPtr<nsIFile> jarFile;
  MOZ_TRY(innerFileURL->GetFile(getter_AddRefs(jarFile)));

  RefPtr<ExtensionStreamGetter> streamGetter =
      new ExtensionStreamGetter(aURI, aLoadinfo, jarChannel.forget(), jarFile);

  NewSimpleChannel(aURI, aLoadinfo, streamGetter, aRetVal);
  return Ok();
}

}  // namespace net
}  // namespace mozilla
