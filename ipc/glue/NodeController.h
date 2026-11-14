/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_NodeController_h
#define mozilla_ipc_NodeController_h

#include "mojo/core/ports/event.h"
#include "mojo/core/ports/name.h"
#include "mojo/core/ports/node.h"
#include "mojo/core/ports/node_delegate.h"
#include "chrome/common/ipc_message.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsTHashMap.h"
#include "mozilla/Queue.h"
#include "mozilla/DataMutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/NodeChannel.h"

namespace mozilla::ipc {

class GeckoChildProcessHost;

class NodeController final : public mojo::core::ports::NodeDelegate,
                             public NodeChannel::Listener {
  using NodeName = mojo::core::ports::NodeName;
  using PortName = mojo::core::ports::PortName;
  using PortRef = mojo::core::ports::PortRef;
  using Event = mojo::core::ports::Event;
  using Node = mojo::core::ports::Node;
  using UserData = mojo::core::ports::UserData;
  using PortStatus = mojo::core::ports::PortStatus;
  using UserMessageEvent = mojo::core::ports::UserMessageEvent;
  using UserMessage = mojo::core::ports::UserMessage;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NodeController, override)

  // Return the global singleton instance. The returned value is only valid
  // while the IO thread is alive.
  static NodeController* GetSingleton();

  class PortObserver : public UserData {
   public:
    virtual void OnPortStatusChanged() = 0;

   protected:
    ~PortObserver() override = default;
  };

  // NOTE: For now there will always be a single broker process, and all
  // processes in the graph need to be able to talk to it (the parent process).
  // Give it a fixed node name for now to simplify things.
  //
  // If we ever decide to have multiple node networks intercommunicating (e.g.
  // multiple instances or background services), we may need to change this.
  static constexpr NodeName kBrokerNodeName{0x1, 0x1};

  bool IsBroker() const { return mName == kBrokerNodeName; }

  // Mint a new connected pair of ports within the current process.
  std::pair<ScopedPort, ScopedPort> CreatePortPair();

  // Get a reference to the port with the given name. Returns an invalid
  // `PortRef` if the name wasn't found.
  PortRef GetPort(const PortName& aName);

  // Set the observer for the given port. This observer will be notified when
  // the status of the port changes.
  void SetPortObserver(const PortRef& aPort, PortObserver* aObserver);

  // See `mojo::core::ports::Node::GetStatus`
  Maybe<PortStatus> GetStatus(const PortRef& aPort);

  // See `mojo::core::ports::Node::ClosePort`
  void ClosePort(const PortRef& aPort);

  // Send a message to the the port's connected peer.
  bool SendUserMessage(const PortRef& aPort, UniquePtr<IPC::Message> aMessage);

  // Get the next message from the port's message queue.
  // Will set `*aMessage` to the found message, or `nullptr`.
  // Returns `false` and sets `*aMessage` to `nullptr` if no further messages
  // will be delivered to this port as its peer has been closed.
  bool GetMessage(const PortRef& aPort, UniquePtr<IPC::Message>* aMessage);

  // Called in the broker process from GeckoChildProcessHost to introduce a new
  // child process into the network. On success, `aInitialPort` will be a
  // `PortRef` which can be used to communicate with the `PortRef` returned from
  // `InitChildProcess`, `aNodeChannel` will be a reference to the `NodeChannel`
  // created for the new process, and `aClientHandle` will be the
  // `ChannelHandle` to pass to the child process on the command line.
  // The port can immediately have messages sent to it.
  bool InviteChildProcess(GeckoChildProcessHost* aChildProcessHost,
                          IPC::Channel::ChannelHandle* aClientHandle,
                          ScopedPort* aInitialPort, NodeChannel** aNodeChannel);

  // Called as the IO thread is started in the parent process.
  static void InitBrokerProcess(const IPC::Channel::ChannelKind* aChannelKind);

  // Called as the IO thread is started in a child process.
  static ScopedPort InitChildProcess(
      IPC::Channel::ChannelHandle&& aChannelHandle, base::ProcessId aParentPid);

  // Called when the IO thread is torn down.
  static void CleanUp();

 private:
  NodeController(const NodeName& aName,
                 const IPC::Channel::ChannelKind* aChannelKind);
  ~NodeController();

  UniquePtr<IPC::Message> SerializeEventMessage(
      UniquePtr<Event> aEvent, const NodeName* aRelayTarget = nullptr);
  UniquePtr<Event> DeserializeEventMessage(UniquePtr<IPC::Message> aMessage,
                                           NodeName* aRelayTarget = nullptr);

  // Get the `NodeChannel` for the named node.
  already_AddRefed<NodeChannel> GetNodeChannel(const NodeName& aName);

  // Stop communicating with this peer. Must be called on the IO thread.
  void DropPeer(NodeName aNodeName);

  // Ensure that there is a direct connection to a remote node, requesting an
  // introduction if there is not.
  // If provided, will optionally send an event to the remote node.
  void ContactRemotePeer(const NodeName& aNode, UniquePtr<Event> aEvent);

  // Send a DeadProxy notification to all peers. May be called on any thread.
  void BroadcastDeadProxyInternal(const NodeName& aProxyNode,
                                  const PortName& aProxyPort);

  // Message Handlers
  void OnEventMessage(const NodeName& aFromNode,
                      UniquePtr<IPC::Message> aMessage) override;
  void OnDeadProxy(const NodeName& aFromNode, const NodeName& aProxyNode,
                   const PortName& aProxyPort) override;
  void OnIntroduce(const NodeName& aFromNode,
                   NodeChannel::Introduction aIntroduction) override;
  void OnRequestIntroduction(const NodeName& aFromNode,
                             const NodeName& aName) override;
  void OnAcceptInvite(const NodeName& aFromNode, const NodeName& aRealName,
                      const PortName& aInitialPort) override;
  void OnChannelError(const NodeName& aFromNode) override;

  // NodeDelegate Implementation
  void ForwardEvent(const NodeName& aNode, UniquePtr<Event> aEvent) override;
  void BroadcastDeadProxy(const PortName& aPort) override;
  void PortStatusChanged(const PortRef& aPortRef) override;
  void ObserveRemoteNode(const NodeName& aNode) override;

  const NodeName mName;
  const UniquePtr<Node> mNode;
  const IPC::Channel::ChannelKind* const mChannelKind;

  template <class T>
  using NodeMap = nsTHashMap<NodeNameHashKey, T>;

  struct Invite {
    // The channel which is being invited. This will have a temporary name until
    // the invite is completed.
    RefPtr<NodeChannel> mChannel;
    // The port which will be merged with the port information from the new
    // child process when recieved.
    PortRef mToMerge;
  };

  struct State {
    // Channels for connecting to all known peers.
    NodeMap<RefPtr<NodeChannel>> mPeers;

    // Messages which are queued for peers which we been introduced to yet.
    NodeMap<Queue<UniquePtr<IPC::Message>, 64>> mPendingMessages;

    // Connections for peers being invited to the network.
    NodeMap<Invite> mInvites;

    // Ports which are waiting to be merged by a particular peer node.
    NodeMap<nsTArray<PortRef>> mPendingMerges;
  };

  DataMutex<State> mState{"NodeController::mState"};
};

}  // namespace mozilla::ipc

#endif
