/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_hwinference_TextGenerationParent_h
#define mozilla_hwinference_TextGenerationParent_h

#include <functional>

#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/hwinference/PTextGenerationParent.h"
#include "mozilla/ipc/FileDescriptor.h"

namespace mozilla::ipc {
class UtilityProcessKeepAlive;
}  // namespace mozilla::ipc

namespace mozilla::hwinference {

// Main-process side of one generator. Main thread only.
class TextGenerationParent final : public PTextGenerationParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TextGenerationParent, override);

  using DeltaHandler = std::function<void(const nsCString&)>;

  // Why the model never became usable: the backend refused it, or the
  // process went away before reporting.
  struct LoadFailure {
    enum class Cause { Backend, ActorGone };
    Cause cause;
    nsCString message;
  };
  using ReadyPromise = MozPromise<double, LoadFailure, true>;

  // A generator on the browser HWInference process, launched if needed: the
  // parent side is bound here, the child side handed to the process with
  // aModel, the model file the parent opened. Null if the process cannot be
  // launched. The caller ends it with Close().
  static already_AddRefed<TextGenerationParent> Create(
      const ipc::FileDescriptor& aModel, const TextGenerationOptions& aOptions);

  // Whether Create() found the process already up.
  bool ProcessReused() const { return mProcessReused; }

  // Resolves with the model load time in milliseconds.
  RefPtr<ReadyPromise> WhenReady();

  // The handler runs before the Generate reply.
  void SetDeltaHandler(DeltaHandler aHandler) {
    mDeltaHandler = std::move(aHandler);
  }

  mozilla::ipc::IPCResult RecvReady(const LoadResult& aResult);
  mozilla::ipc::IPCResult RecvDelta(const nsCString& aText);

  void ActorDestroy(ActorDestroyReason aReason) override;

 private:
  friend PTextGenerationParent;
  TextGenerationParent(RefPtr<ipc::UtilityProcessKeepAlive> aKeepAlive,
                       bool aProcessReused);
  ~TextGenerationParent();

  // Released from ActorDestroy, so the process outlives the generator.
  RefPtr<ipc::UtilityProcessKeepAlive> mKeepAlive;
  const bool mProcessReused;
  DeltaHandler mDeltaHandler;
  MozPromiseHolder<ReadyPromise> mReadyPromise;
  Maybe<double> mLoadMs;
  Maybe<LoadFailure> mLoadFailure;
};

}  // namespace mozilla::hwinference

#endif  // mozilla_hwinference_TextGenerationParent_h
