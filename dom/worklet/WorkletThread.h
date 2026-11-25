/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_worklet_WorkletThread_h
#define mozilla_dom_worklet_WorkletThread_h

#include "mozilla/Attributes.h"
#include "mozilla/CondVar.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/WorkletImpl.h"
#include "nsIObserver.h"
#include "nsThread.h"

class nsIRunnable;

namespace JS {
class ContextOptions;
};  // namespace JS

namespace mozilla::dom {

class WorkletThread final : public nsThread, public nsIObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOBSERVER

  static already_AddRefed<WorkletThread> Create(WorkletImpl* aWorkletImpl);

  // Threads that call EnsureCycleCollectedJSContext must call
  // DeleteCycleCollectedJSContext::Get() before terminating.  Clients of
  // Create() do not need to do this as Terminate() will ensure this happens.
  static void EnsureCycleCollectedJSContext(JSRuntime* aParentRuntime,
                                            const JS::ContextOptions& aOptions);
  static void DeleteCycleCollectedJSContext();

  static bool IsOnWorkletThread();

  static void AssertIsOnWorkletThread();

  nsresult DispatchRunnable(already_AddRefed<nsIRunnable> aRunnable);

  void Terminate();

  // Recommended native stack size to use, greater than the stack size
  // internally used by SpiderMonkey.
  static uint32_t StackSize();

 private:
  explicit WorkletThread(WorkletImpl* aWorkletImpl);
  ~WorkletThread();

  void RunEventLoop();
  class PrimaryRunnable;

  void TerminateInternal();
  class TerminateRunnable;

  // This should only be called by consumers that have an
  // nsIEventTarget/nsIThread pointer.
  NS_IMETHOD
  Dispatch(already_AddRefed<nsIRunnable> aRunnable, uint32_t aFlags) override;

  NS_IMETHOD
  DispatchFromScript(nsIRunnable* aRunnable, uint32_t aFlags) override;

  NS_IMETHOD
  DelayedDispatch(already_AddRefed<nsIRunnable>, uint32_t) override;

  const RefPtr<WorkletImpl> mWorkletImpl;

  bool mExitLoop;  // worklet execution thread

  bool mIsTerminating;  // main thread
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_worklet_WorkletThread_h
