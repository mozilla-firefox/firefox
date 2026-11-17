/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/widget/filedialog/WinFileDialogParent.h"

#include "mozilla/Logging.h"
#include "mozilla/Result.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticString.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsISupports.h"

namespace mozilla::widget::filedialog {

// Count of currently-open file dialogs (not just open-file dialogs).
static size_t sOpenDialogActors = 0;

WinFileDialogParent::WinFileDialogParent() {
  MOZ_LOG(sLogFileDialog, LogLevel::Debug,
          ("%s %p", __PRETTY_FUNCTION__, this));
}

WinFileDialogParent::~WinFileDialogParent() {
  MOZ_LOG(sLogFileDialog, LogLevel::Debug,
          ("%s %p", __PRETTY_FUNCTION__, this));
}

PWinFileDialogParent::nsresult WinFileDialogParent::BindToUtilityProcess(
    mozilla::ipc::UtilityProcessParent* aUtilityParent) {
  if (!aUtilityParent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  Endpoint<PWinFileDialogParent> parentEnd;
  Endpoint<PWinFileDialogChild> childEnd;
  nsresult rv = PWinFileDialog::CreateEndpoints(
      mozilla::ipc::EndpointProcInfo::Current(),
      aUtilityParent->OtherEndpointProcInfo(), &parentEnd, &childEnd);

  if (NS_FAILED(rv)) {
    MOZ_ASSERT(false, "Protocol endpoints failure");
    return NS_ERROR_FAILURE;
  }

  if (!aUtilityParent->SendStartWinFileDialogService(std::move(childEnd))) {
    MOZ_ASSERT(false, "SendStartWinFileDialogService failed");
    return NS_ERROR_FAILURE;
  }

  if (!parentEnd.Bind(this)) {
    MOZ_ASSERT(false, "parentEnd.Bind failed");
    return NS_ERROR_FAILURE;
  }

  sOpenDialogActors++;
  return NS_OK;
}

// Convert the raw IPC promise-type to a filedialog::Promise.
template <typename T, typename Ex>
static auto ConvertToFDPromise(
    StaticString aMethod,  // __func__
    Ex&& extractor,
    RefPtr<MozPromise<T, mozilla::ipc::ResponseRejectReason, true>>
        aSrcPromise) {
  // The extractor must produce a `mozilla::Result<..., Error>` from `T`.
  using SrcResultInfo = detail::DestructureResult<std::invoke_result_t<Ex, T>>;
  using ResolveT = typename SrcResultInfo::OkT;
  static_assert(std::is_same_v<typename SrcResultInfo::ErrorT, Error>,
                "expected T to be a Result<..., Error>");

  using SrcPromiseT = MozPromise<T, mozilla::ipc::ResponseRejectReason, true>;
  using DstPromiseT = MozPromise<ResolveT, Error, true>;

  RefPtr<DstPromiseT> ret = aSrcPromise->Then(
      mozilla::GetCurrentSerialEventTarget(), aMethod,

      [extractor, aMethod](T&& val) {
        mozilla::Result<ResolveT, Error> result = extractor(std::move(val));
        if (result.isOk()) {
          return DstPromiseT::CreateAndResolve(result.unwrap(), aMethod);
        }
        return DstPromiseT::CreateAndReject(result.unwrapErr(), aMethod);
      },
      [aMethod](typename mozilla::ipc::ResponseRejectReason&& val) {
        return DstPromiseT::CreateAndReject(
            MOZ_FD_ERROR(IPCError, "IPC", (uint32_t)val), aMethod);
      });

  return ret;
}

template <typename Input, typename Output>
struct Extractor {
  template <typename Input::Type tag_, Output const& (Input::*getter_)() const>
  static auto get() {
    return [](Input&& res) -> Result<Output, Error> {
      if (res.type() == tag_) {
        return (res.*getter_)();
      }
      if (res.type() == Input::TRemoteError) {
        RemoteError err = res.get_RemoteError();
        return Err(Error{.kind = Error::RemoteError,
                         .where = Error::Location::Deserialize(err.where()),
                         .why = err.why()});
      }
      MOZ_ASSERT_UNREACHABLE("internal IPC failure?");
      return Err(MOZ_FD_ERROR(IPCError, "internal IPC failure?", E_FAIL));
    };
  }
};

[[nodiscard]] RefPtr<WinFileDialogParent::ShowFileDialogPromise>
WinFileDialogParent::ShowFileDialogImpl(HWND parent, const FileDialogType& type,
                                        mozilla::Span<Command const> commands,
                                        bool aNeedsInputProtection) {
  auto inner_promise = PWinFileDialogParent::SendShowFileDialog(
      reinterpret_cast<WindowsHandle>(parent), type, std::move(commands),
      aNeedsInputProtection);

  return ConvertToFDPromise(
      __func__,
      Extractor<FileResult, Maybe<Results>>::get<
          FileResult::TMaybeResults, &FileResult::get_MaybeResults>(),
      std::move(inner_promise));
}

[[nodiscard]] RefPtr<WinFileDialogParent::ShowFolderDialogPromise>
WinFileDialogParent::ShowFolderDialogImpl(HWND parent,
                                          mozilla::Span<Command const> commands,
                                          bool aNeedsInputProtection) {
  auto inner_promise = PWinFileDialogParent::SendShowFolderDialog(
      reinterpret_cast<WindowsHandle>(parent), std::move(commands),
      aNeedsInputProtection);

  return ConvertToFDPromise(
      __func__,
      Extractor<FolderResult, Maybe<nsString>>::get<
          FolderResult::TMaybensString, &FolderResult::get_MaybensString>(),
      std::move(inner_promise));
}

void WinFileDialogParent::ProcessingError(Result aCode, const char* aReason) {
  detail::LogProcessingError(sLogFileDialog, this, aCode, aReason);
}

ProcessProxy::ProcessProxy(RefPtr<WFDP>&& obj)
    : data(MakeRefPtr<Contents>(std::move(obj))) {}

ProcessProxy::Contents::Contents(RefPtr<WFDP>&& obj) : ptr(std::move(obj)) {}

ProcessProxy::Contents::~Contents() {
  AssertIsOnMainThread();

  // destroy the actor...
  ptr->Close();

  // ... and possibly the process
  if (!--sOpenDialogActors) {
    StopProcess();
  }
}

void ProcessProxy::Contents::StopProcess() {
  auto const upm = ipc::UtilityProcessManager::GetSingleton();
  if (!upm) {
    // This is only possible when the UtilityProcessManager has shut down -- in
    // which case the file-dialog process has also already been directed to shut
    // down, and there's nothing we need to do here.
    return;
  }

  MOZ_LOG(sLogFileDialog, LogLevel::Debug,
          ("%s: killing the WINDOWS_FILE_DIALOG process (no more live "
           "actors)",
           __PRETTY_FUNCTION__));
  upm->CleanShutdown(ipc::SandboxingKind::WINDOWS_FILE_DIALOG);
}

}  // namespace mozilla::widget::filedialog
