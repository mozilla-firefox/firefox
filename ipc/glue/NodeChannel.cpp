/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/NodeChannel.h"
#include "chrome/common/ipc_message.h"
#include "chrome/common/ipc_message_utils.h"
#include "mojo/core/ports/name.h"
#include "mozilla/ipc/IOThread.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#ifdef FUZZING_SNAPSHOT
#  include "mozilla/fuzzing/IPCFuzzController.h"
#endif

template <>
struct IPC::ParamTraits<mozilla::ipc::NodeChannel::Introduction> {
  using paramType = mozilla::ipc::NodeChannel::Introduction;
  static void Write(MessageWriter* aWriter, paramType&& aParam) {
    WriteParam(aWriter, aParam.mName);
    WriteParam(aWriter, std::move(aParam.mHandle));
    WriteParam(aWriter, aParam.mMyPid);
    WriteParam(aWriter, aParam.mOtherPid);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mName) &&
           ReadParam(aReader, &aResult->mHandle) &&
           ReadParam(aReader, &aResult->mMyPid) &&
           ReadParam(aReader, &aResult->mOtherPid);
  }
};

namespace mozilla::ipc {

NodeChannel::NodeChannel(const NodeName& aName, IPC::Channel* aChannel,
                         Listener* aListener, base::ProcessId aPid,
                         GeckoChildProcessHost* aChildProcessHost)
    : mListener(aListener),
      mName(aName),
      mOtherPid(aPid),
      mChannel(std::move(aChannel)),
      mChildProcessHost(aChildProcessHost) {}

NodeChannel::~NodeChannel() { Close(); }

// Called when the NodeChannel's refcount drops to `0`.
void NodeChannel::Destroy() {
  // We want to dispatch the `delete` operation to the IO thread. We need to do
  // this even if we're already on the IO thread, as we could be in an
  // `IPC::Channel` callback which unfortunately will not hold a strong
  // reference to keep `this` alive.
  nsISerialEventTarget* ioThread = XRE_GetAsyncIOEventTarget();

  // Synchronously invoke `FinalDestroy` if we're already shutting the IO thread
  // down to ensure we're cleaned up before the thread dies. This is safe as we
  // can't be in a non-owning IPC::Channel callback at this point.
  if (ioThread->IsOnCurrentThread() && MessageLoop::current() &&
      !MessageLoop::current()->IsAcceptingTasks()) {
    FinalDestroy();
    return;
  }

  MOZ_ALWAYS_SUCCEEDS(ioThread->Dispatch(NewNonOwningRunnableMethod(
      "NodeChannel::Destroy", this, &NodeChannel::FinalDestroy)));
}

void NodeChannel::FinalDestroy() {
  AssertIOThread();
  delete this;
}

void NodeChannel::Start() {
  AssertIOThread();

  if (!mChannel->Connect(this)) {
    OnChannelError();
  }
}

void NodeChannel::Close() {
  AssertIOThread();

  if (mState.exchange(State::Closed) != State::Closed) {
    mChannel->Close();
  }
}

void NodeChannel::SetOtherPid(base::ProcessId aNewPid) {
  AssertIOThread();
  MOZ_ASSERT(aNewPid != base::kInvalidProcessId);

  base::ProcessId previousPid = base::kInvalidProcessId;
  if (!mOtherPid.compare_exchange_strong(previousPid, aNewPid)) {
    // The PID was already set before this call, double-check that it's correct.
    MOZ_RELEASE_ASSERT(previousPid == aNewPid,
                       "Different sources disagree on the correct pid?");
  }

  mChannel->SetOtherPid(aNewPid);
}

#ifdef XP_DARWIN
void NodeChannel::SetMachTaskPort(task_t aTask) {
  AssertIOThread();

  if (mState != State::Closed) {
    mChannel->SetOtherMachTask(aTask);
  }
}
#endif

void NodeChannel::SendEventMessage(UniquePtr<IPC::Message> aMessage) {
  // Make sure we're not sending a message with one of our special internal
  // types ,as those should only be sent using the corresponding methods on
  // NodeChannel.
  MOZ_DIAGNOSTIC_ASSERT(aMessage->type() != DEAD_PROXY_MESSAGE_TYPE &&
                        aMessage->type() != INTRODUCE_MESSAGE_TYPE &&
                        aMessage->type() != REQUEST_INTRODUCTION_MESSAGE_TYPE &&
                        aMessage->type() != ACCEPT_INVITE_MESSAGE_TYPE);
  SendMessage(std::move(aMessage));
}

void NodeChannel::RequestIntroduction(const NodeName& aPeerName) {
  MOZ_ASSERT(aPeerName != mojo::core::ports::kInvalidNodeName);
  auto message = std::make_unique<IPC::Message>(
      MSG_ROUTING_CONTROL, REQUEST_INTRODUCTION_MESSAGE_TYPE);
  IPC::MessageWriter writer(*message);
  WriteParam(&writer, aPeerName);
  SendMessage(std::move(message));
}

void NodeChannel::Introduce(Introduction aIntroduction) {
  auto message = std::make_unique<IPC::Message>(MSG_ROUTING_CONTROL,
                                                INTRODUCE_MESSAGE_TYPE);
  IPC::MessageWriter writer(*message);
  WriteParam(&writer, std::move(aIntroduction));
  SendMessage(std::move(message));
}

void NodeChannel::DeadProxy(const NodeName& aProxyNode,
                            const PortName& aProxyPort) {
  MOZ_ASSERT(aProxyNode != mojo::core::ports::kInvalidNodeName);
  MOZ_ASSERT(aProxyPort != mojo::core::ports::kInvalidPortName);
  auto message = std::make_unique<IPC::Message>(MSG_ROUTING_CONTROL,
                                                DEAD_PROXY_MESSAGE_TYPE);
  IPC::MessageWriter writer(*message);
  WriteParam(&writer, aProxyNode);
  WriteParam(&writer, aProxyPort);
  SendMessage(std::move(message));
}

void NodeChannel::AcceptInvite(const NodeName& aRealName,
                               const PortName& aInitialPort) {
  MOZ_ASSERT(aRealName != mojo::core::ports::kInvalidNodeName);
  MOZ_ASSERT(aInitialPort != mojo::core::ports::kInvalidPortName);
  auto message = std::make_unique<IPC::Message>(MSG_ROUTING_CONTROL,
                                                ACCEPT_INVITE_MESSAGE_TYPE);
  IPC::MessageWriter writer(*message);
  WriteParam(&writer, aRealName);
  WriteParam(&writer, aInitialPort);
  SendMessage(std::move(message));
}

void NodeChannel::SendMessage(UniquePtr<IPC::Message> aMessage) {
  if (aMessage->size() > IPC::Channel::kMaximumMessageSize) {
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::IPCMessageName, aMessage->name());
    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::IPCMessageSize, aMessage->size());
    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::IPCMessageLargeBufferShmemFailureSize,
        aMessage->LargeBufferShmemFailureSize());
    MOZ_CRASH("IPC message size is too large");
  }
  aMessage->AssertAsLargeAsHeader();

#ifdef FUZZING_SNAPSHOT
  if (mBlockSendRecv) {
    return;
  }
#endif

  if (mState != State::Active) {
    NS_WARNING("Dropping message as channel has been closed");
    return;
  }

  // NOTE: As this is not guaranteed to be running on the I/O thread, the
  // channel may have become closed since we checked above. IPC::Channel will
  // handle that and return `false` here, so we can re-check `mState`.
  if (!mChannel->Send(std::move(aMessage))) {
    NS_WARNING("Call to Send() failed");

    // If we're still active, update `mState` to `State::Closing`, and dispatch
    // a runnable to actually close our channel.
    State expected = State::Active;
    if (mState.compare_exchange_strong(expected, State::Closing)) {
      XRE_GetAsyncIOEventTarget()->Dispatch(
          NewRunnableMethod("NodeChannel::CloseForSendError", this,
                            &NodeChannel::OnChannelError));
    }
  }
}

void NodeChannel::OnMessageReceived(UniquePtr<IPC::Message> aMessage) {
  AssertIOThread();

#ifdef FUZZING_SNAPSHOT
  if (mBlockSendRecv && !aMessage->IsFuzzMsg()) {
    return;
  }
#endif

  IPC::MessageReader reader(*aMessage);
  switch (aMessage->type()) {
    case REQUEST_INTRODUCTION_MESSAGE_TYPE: {
      NodeName name;
      if (IPC::ReadParam(&reader, &name)) {
        mListener->OnRequestIntroduction(mName, name);
        return;
      }
      break;
    }
    case INTRODUCE_MESSAGE_TYPE: {
      Introduction introduction;
      if (IPC::ReadParam(&reader, &introduction)) {
        mListener->OnIntroduce(mName, std::move(introduction));
        return;
      }
      break;
    }
    case DEAD_PROXY_MESSAGE_TYPE: {
      NodeName proxyNode;
      PortName proxyPort;
      if (IPC::ReadParam(&reader, &proxyNode) &&
          IPC::ReadParam(&reader, &proxyPort)) {
        mListener->OnDeadProxy(mName, proxyNode, proxyPort);
        return;
      }
      break;
    }
    case ACCEPT_INVITE_MESSAGE_TYPE: {
      NodeName realName;
      PortName initialPort;
      if (IPC::ReadParam(&reader, &realName) &&
          IPC::ReadParam(&reader, &initialPort)) {
        mListener->OnAcceptInvite(mName, realName, initialPort);
        return;
      }
      break;
    }
    // Assume all unrecognized types are intended as user event messages, and
    // deliver them to our listener as such. This allows us to use the same type
    // field for both internal messages and protocol messages.
    //
    // FIXME: Consider doing something cleaner in the future?
    case EVENT_MESSAGE_TYPE:
    default: {
#ifdef FUZZING_SNAPSHOT
      if (!fuzzing::IPCFuzzController::instance().ObserveIPCMessage(
              this, *aMessage)) {
        return;
      }
#endif

      mListener->OnEventMessage(mName, std::move(aMessage));
      return;
    }
  }

  // If we got to this point without early returning the message was malformed
  // in some way. Report an error.

  NS_WARNING("NodeChannel received a malformed message");
  OnChannelError();
}

void NodeChannel::OnChannelConnected(base::ProcessId aPeerPid) {
  AssertIOThread();

  SetOtherPid(aPeerPid);

  // We may need to tell the GeckoChildProcessHost which we were created by that
  // the channel has been connected to unblock completing the process launch.
  RefPtr<GeckoChildProcessHost> host(mChildProcessHost);
  if (host) {
    host->OnChannelConnected(aPeerPid);
  }
}

void NodeChannel::OnChannelError() {
  AssertIOThread();

  State prev = mState.exchange(State::Closed);
  if (prev == State::Closed) {
    return;
  }

  // Clean up the channel.
  mChannel->Close();

  // Tell our listener about the error.
  mListener->OnChannelError(mName);
}

}  // namespace mozilla::ipc
