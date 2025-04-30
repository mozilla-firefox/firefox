/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IPCFuzzController.h"
#include "mozilla/Fuzzing.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/SyncRunnable.h"

#include "nsIThread.h"
#include "nsThreadUtils.h"

#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/ipc/MessageLink.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/NodeChannel.h"
#include "mozilla/ipc/NodeController.h"

#include "mozilla/ipc/PIdleScheduler.h"
#include "mozilla/ipc/PBackground.h"
#include "mozilla/dom/PContent.h"

#include <fstream>
#include <set>
#include <sstream>
#include <algorithm>

using namespace mojo::core::ports;
using namespace mozilla::ipc;

// Sync inject means that the actual fuzzing takes place on the I/O thread
// and hence it injects directly into the target NodeChannel. In async mode,
// we run the fuzzing on a separate thread and dispatch the runnable that
// injects the message back to the I/O thread. Both approaches seem to work
// and have advantages and disadvantages. Blocking the I/O thread means no
// IPC between other processes will interfere with our fuzzing in the meantime
// but blocking could also cause hangs when such IPC is required during the
// fuzzing runtime for some reason.
// #define MOZ_FUZZ_IPC_SYNC_INJECT 1

// Synchronize after each message rather than just after every constructor
// or at the end of the iteration. Doing so costs us some performance because
// we have to wait for each packet and process events on the main thread,
// but it is necessary when using `OnMessageError` to release on early errors.
#define MOZ_FUZZ_IPC_SYNC_AFTER_EACH_MSG 1

namespace mozilla {
namespace fuzzing {

const uint32_t ipcDefaultTriggerMsg = dom::PContent::Msg_SignalFuzzingReady__ID;

IPCFuzzController::IPCFuzzController()
    : useLastPortName(false),
      useLastPortNameAlways(false),
      protoFilterTargetExcludeToplevel(false),
      useLastActor(0),
      mMutex("IPCFuzzController"),
      mIPCTriggerMsg(ipcDefaultTriggerMsg) {
  InitializeIPCTypes();

  // We use 6 bits for port index selection without wrapping, so we just
  // create 64 empty rows in our port matrix. Not all of these rows will
  // be used though.
  portNames.resize(64);

  // This is our port / toplevel actor ordering. Add new toplevel actors
  // here to support them in the fuzzer. Do *NOT* change the order of
  // these, as it will invalidate our fuzzing corpus.
  portNameToIndex["PContent"] = 0;
  portNameToIndex["PBackground"] = 1;
  portNameToIndex["PBackgroundStarter"] = 2;
  portNameToIndex["PCompositorManager"] = 3;
  portNameToIndex["PImageBridge"] = 4;
  portNameToIndex["PProcessHangMonitor"] = 5;
  portNameToIndex["PProfiler"] = 6;
  portNameToIndex["PVRManager"] = 7;
  portNameToIndex["PCanvasManager"] = 8;
  portNameToIndex["PRemoteLazyInputStream"] = 9;
  portNameToIndex["PRemoteWorkerService"] = 10;
  portNameToIndex["PBackgroundLSDatabase"] = 11;
  portNameToIndex["PRemoteWorkerNonLifeCycleOpController"] = 12;
  portNameToIndex["PNotification"] = 13;
  portNameToIndex["PRemoteWorkerDebuggerManager"] = 14;
  portNameToIndex["PRemoteWorkerDebugger"] = 15;

  // Used to select the n-th trigger message as a starting point for fuzzing
  // in single message mode. A value of 1 will skip the first matching message
  // and start fuzzing on the second message, and so on.
  if (!!getenv("MOZ_FUZZ_IPC_TRIGGER_SINGLEMSG_WAIT")) {
    mIPCTriggerSingleMsgWait =
        atoi(getenv("MOZ_FUZZ_IPC_TRIGGER_SINGLEMSG_WAIT"));
  }

  // When set, dump all IPC message at or above the specified size to files.
  // Useful to collect samples of different types in one run.
  if (!!getenv("MOZ_FUZZ_IPC_DUMP_ALL_MSGS_SIZE")) {
    mIPCDumpAllMsgsSize.emplace(
        atoi(getenv("MOZ_FUZZ_IPC_DUMP_ALL_MSGS_SIZE")));
  }
}

// static
IPCFuzzController& IPCFuzzController::instance() {
  static IPCFuzzController ifc;
  return ifc;
}

void IPCFuzzController::InitializeIPCTypes() {
  const char* cons = "Constructor";
  size_t cons_len = strlen(cons);

  const char* targetNameTrigger = getenv("MOZ_FUZZ_IPC_TRIGGER");
  const char* targetNameDump = getenv("MOZ_FUZZ_IPC_DUMPMSG");

  for (uint32_t start = 0; start < LastMsgIndex; ++start) {
    uint32_t i;
    for (i = (start << 16) + 1; i < ((start + 1) << 16); ++i) {
      const char* name = IPC::StringFromIPCMessageType(i);

      if (name[0] == '<') break;

      if (targetNameTrigger && !strcmp(name, targetNameTrigger)) {
        MOZ_FUZZING_NYX_PRINTF(
            "INFO: [InitializeIPCTypes] Located trigger message (%s, %d)\n",
            targetNameTrigger, i);
        mIPCTriggerMsg = i;
      }

      if (targetNameDump && !strcmp(name, targetNameDump)) {
        MOZ_FUZZING_NYX_PRINTF(
            "INFO: [InitializeIPCTypes] Located dump message (%s, %d)\n",
            targetNameDump, i);
        mIPCDumpMsg.emplace(i);
      }

      size_t len = strlen(name);
      if (len > cons_len && !memcmp(cons, name + len - cons_len, cons_len)) {
        constructorTypes.insert(i);
      }
    }

    uint32_t msgCount = i - ((start << 16) + 1);
    if (msgCount) {
      validMsgTypes[(ProtocolId)start] = msgCount;
    }
  }

  // Resolve potentially disallowed messages now that we have initialized IPC
  // types.
  InitDisallowedIPCTypes();
}

bool IPCFuzzController::GetRandomIPCMessageType(ProtocolId pId,
                                                uint16_t typeOffset,
                                                uint32_t* type) {
  if (actorAllowedMessages.size() > 0) {
    // We are fixed to a single actor with a particular message set allowed.
    *type = actorAllowedMessages[typeOffset % actorAllowedMessages.size()];
    return true;
  }

  auto pIdEntry = validMsgTypes.find(pId);
  if (pIdEntry == validMsgTypes.end()) {
    return false;
  }

  *type =
      ((uint32_t)pIdEntry->first << 16) + 1 + (typeOffset % pIdEntry->second);

  if (strstr(IPC::StringFromIPCMessageType(*type), "::Reply_")) {
    *type = *type - 1;
  }

  // Check if we are allowed to send this message type.
  if (actorDisallowedMessages.find(*type) != actorDisallowedMessages.end()) {
    return false;
  }

  return true;
}

void IPCFuzzController::InitDisallowedIPCTypes() {
  const char* targetMsgName = getenv("MOZ_FUZZ_IPC_MSGFILTER_DISALLOW");
  if (!targetMsgName) {
    // Nothing to do.
    return;
  }

  std::vector<std::string> targetMsgNames;
  std::istringstream targetMsgNameStream(targetMsgName);
  for (std::string msg; getline(targetMsgNameStream, msg, ';');) {
    targetMsgNames.push_back(msg);
  }

  for (auto pIdEntry : validMsgTypes) {
    for (uint16_t typeOffset = 0; typeOffset < pIdEntry.second; ++typeOffset) {
      uint32_t type = ((uint32_t)pIdEntry.first << 16) + 1 + typeOffset;
      const char* msgName = IPC::StringFromIPCMessageType(type);
      if (strstr(msgName, "::Reply_")) {
        continue;
      }

      for (std::string msg : targetMsgNames) {
        if (strstr(msgName, msg.c_str())) {
          actorDisallowedMessages.insert(type);
          break;
        }
      }
    }
  }
}

void IPCFuzzController::InitAllowedIPCTypes() {
  const char* targetMsgName = getenv("MOZ_FUZZ_IPC_MSGFILTER_ALLOW");
  if (!targetMsgName) {
    // Nothing to do.
    return;
  }

  std::vector<std::string> targetMsgNames;
  std::istringstream targetMsgNameStream(targetMsgName);
  for (std::string msg; getline(targetMsgNameStream, msg, ';');) {
    targetMsgNames.push_back(msg);
  }

  if (!maybeLastActorId) {
    // We only want to call this if we are actually pinning to an actor.
    // This also means that calling this is only valid with a PROTOID_FILTER
    // set.
    MOZ_FUZZING_NYX_ABORT("InitAllowedIPCTypes called without actor pinned?!");
  }

  auto result = actorIds.find(lastActorPortName);
  if (result == actorIds.end()) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Couldn't find port in actors map?!\n");
  }
  auto actors = result->second;

  bool found = false;
  size_t actorIndex;
  for (actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
    if (actors[actorIndex].first == maybeLastActorId ||
        (maybeLastActorId == MSG_ROUTING_CONTROL &&
         !actors[actorIndex].first)) {
      found = true;
      break;
    }
  }

  if (!found) {
    MOZ_FUZZING_NYX_ABORT(
        "ERROR: Pinned to actor that's not in actors map!?\n");
  }

  ActorIdPair ids = actors[actorIndex];

  ProtocolId pId = ids.second;

  auto pIdEntry = validMsgTypes.find(pId);
  if (pIdEntry == validMsgTypes.end()) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Pinned actor has no valid message types!?\n");
  }

  for (uint16_t typeOffset = 0; typeOffset < pIdEntry->second; ++typeOffset) {
    uint32_t type = ((uint32_t)pIdEntry->first << 16) + 1 + typeOffset;
    const char* msgName = IPC::StringFromIPCMessageType(type);
    if (strstr(msgName, "::Reply_")) {
      continue;
    }

    for (std::string msg : targetMsgNames) {
      if (strstr(msgName, msg.c_str())) {
        actorAllowedMessages.push_back(type);
        break;
      }
    }
  }

  if (!actorAllowedMessages.size()) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Empty actorAllowedMessages!?\n");
  }
}

static bool IsManagedByTargetActor(IProtocol* protocol,
                                   std::string& protoIdFilter) {
  while (protocol) {
    if (!strcmp(protocol->GetProtocolName(), protoIdFilter.c_str())) {
      return true;
    }

    protocol = protocol->Manager();
  }
  return false;
}

void IPCFuzzController::OnActorConnected(IProtocol* protocol) {
  if (!XRE_IsParentProcess()) {
    return;
  }

  MOZ_FUZZING_NYX_DEBUG(
      "DEBUG: IPCFuzzController::OnActorConnected() Mutex try\n");

  // Called on background threads and modifies `actorIds`.
  MutexAutoLock lock(mMutex);

  MOZ_FUZZING_NYX_DEBUG(
      "DEBUG: IPCFuzzController::OnActorConnected() Mutex locked\n");

  static bool protoIdFilterInitialized = false;
  static bool allowSubActors =
      !!getenv("MOZ_FUZZ_PROTOID_FILTER_ALLOW_SUBACTORS");
  static std::string protoIdFilter;
  if (!protoIdFilterInitialized) {
    const char* protoIdFilterStr = getenv("MOZ_FUZZ_PROTOID_FILTER");
    if (protoIdFilterStr) {
      protoIdFilter = std::string(protoIdFilterStr);
    }
    protoIdFilterInitialized = true;
  }

  MessageChannel* channel = protocol->ToplevelProtocol()->GetIPCChannel();

  Maybe<PortName> portName = channel->GetPortName();

  if (!portName) {
    MOZ_FUZZING_NYX_PRINTF("INFO: [OnActorConnected] ActorID %d Protocol: %s\n",
                           protocol->Id(), protocol->GetProtocolName());
  }

  if (portName) {
    if (!protoIdFilter.empty()) {
      if (!strcmp(protocol->GetProtocolName(), protoIdFilter.c_str())) {
        MOZ_FUZZING_NYX_PRINTF(
            "INFO: [OnActorConnected] ActorID %d Protocol: %s matches "
            "target.\n",
            protocol->Id(), protocol->GetProtocolName());

        // If our matching protocol is not a toplevel actor, then we need to
        // exclude the toplevel protocol later in `MakeTargetDecision` because
        // the actor will always be added to the map.
        protoFilterTargetExcludeToplevel = protocol->Manager() != nullptr;
      } else if (actorIds[*portName].empty()) {
        MOZ_FUZZING_NYX_PRINTF(
            "INFO: [OnActorConnected] ActorID %d Protocol: %s is toplevel "
            "actor.\n",
            protocol->Id(), protocol->GetProtocolName());
      } else if (allowSubActors &&
                 IsManagedByTargetActor(protocol, protoIdFilter)) {
        MOZ_FUZZING_NYX_PRINTF(
            "INFO: [OnActorConnected] ActorID %d Protocol: %s is managed by "
            "target actor.\n",
            protocol->Id(), protocol->GetProtocolName());
      } else {
        // Not a toplevel actor, not matching the filter and also either not a
        // sub actor of our target or we are focusing only on the target. Ignore
        // this actor.
        if (!!getenv("MOZ_FUZZ_DEBUG")) {
          MOZ_FUZZING_NYX_PRINTF(
              "INFO: [OnActorConnected] ActorID %d Protocol: %s ignored due to "
              "filter.\n",
              protocol->Id(), protocol->GetProtocolName());
        }
        return;
      }
    }

    if (!!getenv("MOZ_FUZZ_DEBUG")) {
      MOZ_FUZZING_NYX_PRINTF(
          "INFO: [OnActorConnected] ActorID %d Protocol: %s Port: %lu %lu\n",
          protocol->Id(), protocol->GetProtocolName(), portName->v1,
          portName->v2);
    }

    actorIds[*portName].emplace_back(protocol->Id(), protocol->GetProtocolId());

    if (Nyx::instance().started()) {
      if (!useLastPortNameAlways) {
        // Fix the port we will be using for at least the next 5 messages
        useLastPortName = true;
        lastActorPortName = *portName;
      }

      // Use this actor for the next 5 messages
      useLastActor = 5;
    }
  } else {
    MOZ_FUZZING_NYX_DEBUG("WARNING: No port name on actor?!\n");
  }
}

void IPCFuzzController::OnActorDestroyed(IProtocol* protocol) {
  if (!XRE_IsParentProcess()) {
    return;
  }

#ifdef FUZZ_DEBUG
  MOZ_FUZZING_NYX_PRINTF("INFO: [OnActorDestroyed] ActorID %d Protocol: %s\n",
                         protocol->Id(), protocol->GetProtocolName());
#endif

  MessageChannel* channel = protocol->ToplevelProtocol()->GetIPCChannel();

  Maybe<PortName> portName = channel->GetPortName();
  if (portName) {
    MOZ_FUZZING_NYX_DEBUG(
        "DEBUG: IPCFuzzController::OnActorDestroyed() Mutex try\n");
    // Called on background threads and modifies `actorIds`.
    MutexAutoLock lock(mMutex);
    MOZ_FUZZING_NYX_DEBUG(
        "DEBUG: IPCFuzzController::OnActorDestroyed() Mutex locked\n");

    if (maybeLastActorId &&
        (maybeLastActorId == protocol->Id() ||
         (maybeLastActorId == MSG_ROUTING_CONTROL && !protocol->Id())) &&
        lastActorPortName == *portName) {
      MOZ_FUZZING_NYX_DEBUG("INFO: Actor pinning released.\n");
      // We destroyed the actor we were focusing on, unpin.
      maybeLastActorId = 0;
      useLastActor = 0;
    }

    for (auto iter = actorIds[*portName].begin();
         iter != actorIds[*portName].end();) {
      if (iter->first == protocol->Id() &&
          iter->second == protocol->GetProtocolId()) {
        iter = actorIds[*portName].erase(iter);
      } else {
        ++iter;
      }
    }
  } else {
    MOZ_FUZZING_NYX_DEBUG("WARNING: No port name on destroyed actor?!\n");
  }
}

void IPCFuzzController::AddToplevelActor(PortName name, ProtocolId protocolId) {
  const char* protocolName = ProtocolIdToName(protocolId);
  auto result = portNameToIndex.find(protocolName);
  if (result == portNameToIndex.end()) {
    MOZ_FUZZING_NYX_PRINTF(
        "ERROR: [OnActorConnected] Unknown Top-Level Protocol: %s\n",
        protocolName);
    MOZ_FUZZING_NYX_ABORT("Unknown Top-Level Protocol\n");
  }
  uint8_t portIndex = result->second;
  portNames[portIndex].push_back(name);
  portNameToProtocolName[name] = std::string(protocolName);
}

bool IPCFuzzController::ObserveIPCMessage(mozilla::ipc::NodeChannel* channel,
                                          IPC::Message& aMessage) {
  if (!mozilla::fuzzing::Nyx::instance().is_enabled("IPC_Generic")) {
    // Fuzzer is not enabled.
    return true;
  }

  if (!XRE_IsParentProcess()) {
    // For now we only care about things in the parent process.
    return true;
  }

  if (aMessage.IsFuzzMsg()) {
    // Don't observe our own messages. If this is the first fuzzing message,
    // we also block further non-fuzzing communication on that node.
    if (!channel->mBlockSendRecv) {
      MOZ_FUZZING_NYX_PRINTF(
          "INFO: [NodeChannel::OnMessageReceived] Blocking further "
          "communication on node %lu %lu (seen fuzz msg)\n",
          channel->GetName().v1, channel->GetName().v2);
      channel->mBlockSendRecv = true;
    }
    return true;
  } else if (aMessage.type() == mIPCTriggerMsg && !Nyx::instance().started()) {
    MOZ_FUZZING_NYX_PRINTF("DEBUG: Ready message detected on actor %d.\n",
                           aMessage.routing_id());

    if (!haveTargetNodeName && !!getenv("MOZ_FUZZ_PROTOID_FILTER")) {
      // With a protocol filter set, we want to pin to the actor that
      // received the ready message and stay there. We should do this here
      // because OnActorConnected can be called even after the ready message
      // has been received and potentially override the correct actor.

      // Get the port name associated with this message
      Vector<char, 256, InfallibleAllocPolicy> footer;
      if (!footer.initLengthUninitialized(aMessage.event_footer_size()) ||
          !aMessage.ReadFooter(footer.begin(), footer.length(), false)) {
        MOZ_FUZZING_NYX_ABORT("ERROR: Failed to read message footer.\n");
      }

      UniquePtr<Event> event =
          Event::Deserialize(footer.begin(), footer.length());

      if (!event || event->type() != Event::kUserMessage) {
        MOZ_FUZZING_NYX_ABORT("ERROR: Trigger message is not kUserMessage?!\n");
      }

      lastActorPortName = event->port_name();
      useLastPortNameAlways = true;

      if (!getenv("MOZ_FUZZ_PROTOID_FILTER_ALLOW_SUBACTORS")) {
        // In this mode, we really want to focus on a single actor.
        useLastActor = 1024;
        maybeLastActorId = aMessage.routing_id();
        MOZ_FUZZING_NYX_PRINTF("DEBUG: Pinned to actor %d forever.\n",
                               aMessage.routing_id());
      } else {
        // In this mode, we want to focus on a particular actor and all of its
        // sub actors. This means we have to pin the port at least. Undesired
        // other actors are filtered out already in OnActorConnected *except*
        // for the toplevel actor belonging to this port. This exception is
        // handled separately in MakeTargetDecision.
        MOZ_FUZZING_NYX_PRINTF("DEBUG: Pinned to port %lu %lu forever.\n",
                               lastActorPortName.v1, lastActorPortName.v2);
      }

      InitAllowedIPCTypes();
    }

    // TODO: This is specific to PContent fuzzing. If we later want to fuzz
    // a different process pair, we need additional signals here.
    OnChildReady();

    // The ready message indicates the right node name for us to work with
    // and we should only ever receive it once.
    if (!haveTargetNodeName) {
      targetNodeName = channel->GetName();
      haveTargetNodeName = true;

      // We can also use this message as the base template for other messages
      if (!this->sampleHeader.initLengthUninitialized(
              sizeof(IPC::Message::Header))) {
        MOZ_FUZZING_NYX_ABORT("sampleHeader.initLengthUninitialized failed\n");
      }

      memcpy(sampleHeader.begin(), aMessage.header(),
             sizeof(IPC::Message::Header));
    }
  } else if (haveTargetNodeName && targetNodeName != channel->GetName()) {
    // Not our node, no need to observe
    return true;
  } else if (Nyx::instance().started()) {
    // When fuzzing is already started, we shouldn't observe messages anymore.
    if (!channel->mBlockSendRecv) {
      MOZ_FUZZING_NYX_PRINTF(
          "INFO: [NodeChannel::OnMessageReceived] Blocking further "
          "communication on node %lu %lu (fuzzing started)\n",
          channel->GetName().v1, channel->GetName().v2);
      channel->mBlockSendRecv = true;
    }
    return false;
  }

  Vector<char, 256, InfallibleAllocPolicy> footer;

  if (!footer.initLengthUninitialized(aMessage.event_footer_size())) {
    MOZ_FUZZING_NYX_ABORT("footer.initLengthUninitialized failed\n");
  }

  if (!aMessage.ReadFooter(footer.begin(), footer.length(), false)) {
    MOZ_FUZZING_NYX_ABORT("ERROR: ReadFooter() failed?!\n");
  }

  UniquePtr<Event> event = Event::Deserialize(footer.begin(), footer.length());

  if (!event) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Failed to deserialize observed message?!\n");
  }

  if (event->type() == Event::kUserMessage) {
    if (haveTargetNodeName && !fuzzingStartPending) {
      bool missingActor = false;

      // Check if we have any entries in our port map that we haven't seen yet
      // though `OnActorConnected`. That method is called on a background
      // thread and this call will race with the I/O thread.
      //
      // However, with a custom MOZ_FUZZ_IPC_TRIGGER we assume we want to keep
      // the port pinned so we don't have to wait at all.
      if (mIPCTriggerMsg == ipcDefaultTriggerMsg) {
        MOZ_FUZZING_NYX_DEBUG(
            "DEBUG: IPCFuzzController::ObserveIPCMessage() Mutex try\n");
        // Called on the I/O thread and reads `portSeqNos`.
        //
        // IMPORTANT: We must give up any locks before entering `StartFuzzing`,
        // as we will never return. This would cause a deadlock with new actors
        // being created and `OnActorConnected` being called.
        MutexAutoLock lock(mMutex);

        MOZ_FUZZING_NYX_DEBUG(
            "DEBUG: IPCFuzzController::ObserveIPCMessage() Mutex locked\n");

        for (auto iter = portSeqNos.begin(); iter != portSeqNos.end(); ++iter) {
          auto result = actorIds.find(iter->first);
          if (result == actorIds.end()) {
            // Make sure we only wait for actors that belong to us.
            auto result = portNodeName.find(iter->first);
            if (result->second == targetNodeName) {
              missingActor = true;
              break;
            }
          }
        }
      }

      if (missingActor) {
        MOZ_FUZZING_NYX_PRINT(
            "INFO: Delaying fuzzing start, missing actors...\n");
      } else if (!childReady) {
        MOZ_FUZZING_NYX_PRINT(
            "INFO: Delaying fuzzing start, waiting for child...\n");
      } else {
        fuzzingStartPending = true;
        StartFuzzing(channel, aMessage);

        // In the async case, we return and can already block the relevant
        // communication.
        if (targetNodeName == channel->GetName()) {
          if (!channel->mBlockSendRecv) {
            MOZ_FUZZING_NYX_PRINTF(
                "INFO: [NodeChannel::OnMessageReceived] Blocking further "
                "communication on node %lu %lu (fuzzing start pending)\n",
                channel->GetName().v1, channel->GetName().v2);
            channel->mBlockSendRecv = true;
          }

          return false;
        }
        MOZ_FUZZING_NYX_ABORT("Unreachable");
        return true;
      }
    }

    // Add/update sequence numbers. We need to make sure to do this after our
    // call to `StartFuzzing` because once we start fuzzing, the message will
    // never actually be processed, so we run into a sequence number desync.
    {
      // Get the port name associated with this message
      UserMessageEvent* userMsgEv = static_cast<UserMessageEvent*>(event.get());
      PortName name = event->port_name();

      // Called on the I/O thread and modifies `portSeqNos`.
      MutexAutoLock lock(mMutex);
      portSeqNos.insert_or_assign(
          name, std::pair<int32_t, uint64_t>(aMessage.seqno(),
                                             userMsgEv->sequence_num()));
#ifdef FUZZ_DEBUG
      MOZ_FUZZING_NYX_PRINTF(
          "DEBUG: Port %lu %lu updated sequence number to %lu\n", name.v1,
          name.v2, userMsgEv->sequence_num());
#endif

      portNodeName.insert_or_assign(name, channel->GetName());
    }
  }

  return true;
}

void IPCFuzzController::OnMessageError(
    mozilla::ipc::HasResultCodes::Result code, const IPC::Message& aMsg) {
  if (!mozilla::fuzzing::Nyx::instance().is_enabled("IPC_Generic")) {
    // Fuzzer is not enabled.
    return;
  }

  if (!XRE_IsParentProcess()) {
    // For now we only care about things in the parent process.
    return;
  }

  if (!aMsg.IsFuzzMsg()) {
    // We should only act upon fuzzing messages.
    return;
  }

  switch (code) {
    case ipc::HasResultCodes::MsgDropped:
      Nyx::instance().handle_event("MOZ_IPC_DROPPED", nullptr, 0, nullptr);
      break;
    case ipc::HasResultCodes::MsgNotKnown:
      // Seeing this error should be rare - one potential reason is if a sync
      // message is sent as async and vice versa. Other than that, we shouldn't
      // be generating this error at all.
      Nyx::instance().handle_event("MOZ_IPC_UNKNOWN_TYPE", nullptr, 0, nullptr);
#ifdef FUZZ_DEBUG
      MOZ_FUZZING_NYX_PRINTF(
          "WARNING: MOZ_IPC_UNKNOWN_TYPE for message type %s (%u) routed to "
          "actor %d (sync %d)\n",
          IPC::StringFromIPCMessageType(aMsg.type()), aMsg.type(),
          aMsg.routing_id(), aMsg.is_sync());
#endif
      break;
    case ipc::HasResultCodes::MsgNotAllowed:
      Nyx::instance().handle_event("MOZ_IPC_NOTALLOWED_ERROR", nullptr, 0,
                                   nullptr);
      break;
    case ipc::HasResultCodes::MsgPayloadError:
    case ipc::HasResultCodes::MsgValueError:
      Nyx::instance().handle_event("MOZ_IPC_DESERIALIZE_ERROR", nullptr, 0,
                                   nullptr);
      break;
    case ipc::HasResultCodes::MsgProcessingError:
      Nyx::instance().handle_event("MOZ_IPC_PROCESS_ERROR", nullptr, 0,
                                   nullptr);
      break;
    default:
      MOZ_FUZZING_NYX_ABORT("unknown Result code");
  }

  // Count this message as one iteration as well.
  Nyx::instance().release(IPCFuzzController::instance().getMessageStopCount() +
                          1);
}

bool IPCFuzzController::MakeTargetDecision(
    uint8_t portIndex, uint8_t portInstanceIndex, uint8_t actorIndex,
    uint8_t actorProtocolIndex, uint16_t typeOffset, PortName* name,
    int32_t* seqno, uint64_t* fseqno, int32_t* actorId, uint32_t* type,
    bool* is_cons, bool update) {
  if (useLastActor) {
    useLastActor--;
    *name = lastActorPortName;

    MOZ_FUZZING_NYX_PRINT("DEBUG: MakeTargetDecision: Pinned to last actor.\n");

    // Once we stop pinning to the last actor, we need to decide if we
    // want to keep the pinning on the port itself. We use one of the
    // unused upper bits of portIndex for this purpose.
    if (!useLastActor && !useLastPortNameAlways && (portIndex & (1 << 7))) {
      MOZ_FUZZING_NYX_PRINT(
          "DEBUG: MakeTargetDecision: Released pinning on last port.\n");
      useLastPortName = false;
    }
  } else if (useLastPortName || useLastPortNameAlways) {
    *name = lastActorPortName;
    MOZ_FUZZING_NYX_PRINT("DEBUG: MakeTargetDecision: Pinned to last port.\n");
  } else {
    // Every possible toplevel actor type has a fixed number that
    // we assign to it in the constructor of this class. Here, we
    // use the lower 6 bits to select this toplevel actor type.
    // This approach has the advantage that the tests will always
    // select the same toplevel actor type deterministically,
    // independent of the order they appeared and independent
    // of the type of fuzzing we are doing.
    auto portInstances = portNames[portIndex & 0x3f];
    if (!portInstances.size()) {
      return false;
    }
    *name = portInstances[portInstanceIndex % portInstances.size()];
  }

  // We should always have at least one actor per port
  auto result = actorIds.find(*name);
  if (result == actorIds.end()) {
    MOZ_FUZZING_NYX_PRINT("ERROR: Couldn't find port in actors map?!\n");
    return false;
  }

  // Find a random actor on this port
  auto actors = result->second;
  if (actors.empty()) {
    MOZ_FUZZING_NYX_PRINT(
        "ERROR: Couldn't find an actor for selected port?!\n");
    return false;
  }

  auto seqNos = portSeqNos[*name];

  // Hand out the correct sequence numbers
  *seqno = seqNos.first - 1;
  *fseqno = seqNos.second + 1;

  // If a type is already specified, we must be in preserveHeaderMode.
  bool isPreserveHeader = *type;

  if (useLastActor) {
    if (maybeLastActorId) {
      bool found = false;
      for (actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
        // Toplevel actors have a discrepancy here: Routing ID is -1 but the
        // actor id provided through protocol->Id() is 0.
        if (actors[actorIndex].first == maybeLastActorId ||
            (maybeLastActorId == MSG_ROUTING_CONTROL &&
             !actors[actorIndex].first)) {
          found = true;
          break;
        }
      }

      if (!found) {
        MOZ_FUZZING_NYX_ABORT(
            "ERROR: Pinned to actor that's not in actors map!?\n");
      }
    } else {
      actorIndex = actors.size() - 1;
    }
  } else if (isPreserveHeader) {
    // In preserveHeaderMode, we need to find an actor that matches the
    // requested message type instead of any random actor.
    uint16_t maybeProtocolId = *type >> 16;
    if (maybeProtocolId >= IPCMessageStart::LastMsgIndex) {
      // Not a valid protocol.
      return false;
    }
    ProtocolId wantedProtocolId = static_cast<ProtocolId>(maybeProtocolId);
    std::vector<uint32_t> allowedIndices;
    for (uint32_t i = 0; i < actors.size(); ++i) {
      if (protoFilterTargetExcludeToplevel && !i) {
        // Filter out the toplevel protocol at index 0
        continue;
      }

      if (actors[i].second == wantedProtocolId) {
        allowedIndices.push_back(i);
      }
    }

    if (allowedIndices.empty()) {
      return false;
    }

    actorIndex = allowedIndices[actorIndex % allowedIndices.size()];
  } else {
    std::set<ProtocolId> seenProtocol;
    std::vector<ProtocolId> availableProtocols;

    if (protoFilterTargetExcludeToplevel && actors.size() < 2) {
      // We likely destroyed all other actors
      return false;
    }

    for (auto actor : actors) {
      if (protoFilterTargetExcludeToplevel && seenProtocol.empty()) {
        // Skip the toplevel protocol.
        seenProtocol.insert(actor.second);
        continue;
      }

      if (seenProtocol.find(actor.second) == seenProtocol.end()) {
        seenProtocol.insert(actor.second);
        availableProtocols.push_back(actor.second);
      }
    }

    // Instead of directly selecting a random actor, we select the protocol
    // first and then out of all available actors matching this protocol,
    // we select the destination actor. This makes sure that we are uniformly
    // fuzzing protocols and not biasing towards protocols with lots of actor
    // instances.
    ProtocolId wantedProtocolId =
        availableProtocols[actorProtocolIndex % availableProtocols.size()];

    std::vector<uint32_t> allowedIndices;
    for (uint32_t i = 0; i < actors.size(); ++i) {
      if (actors[i].second == wantedProtocolId) {
        allowedIndices.push_back(i);
      }
    }

    actorIndex = allowedIndices[actorIndex % allowedIndices.size()];
  }

  ActorIdPair ids = actors[actorIndex];
  *actorId = ids.first;

  // If the actor ID is 0, then we are talking to the toplevel actor
  // of this port. Hence we must set the ID to MSG_ROUTING_CONTROL.
  if (!*actorId) {
    *actorId = MSG_ROUTING_CONTROL;
  }

  if (!isPreserveHeader || actorAllowedMessages.size() > 0) {
    // If msgType is already set, then we are in preserveHeaderMode
    if (!this->GetRandomIPCMessageType(ids.second, typeOffset, type)) {
      MOZ_FUZZING_NYX_PRINT("ERROR: GetRandomIPCMessageType failed?!\n");
      return false;
    }

    *is_cons = false;
    if (constructorTypes.find(*type) != constructorTypes.end()) {
      *is_cons = true;
    }
  }

  if (isPreserveHeader &&
      actorDisallowedMessages.find(*type) != actorDisallowedMessages.end()) {
    // If we have messages that aren't allowed to be sent, we need to
    // confirm that the type set in the header is still allowed.
    return false;
  }

  MOZ_FUZZING_NYX_PRINTF(
      "DEBUG: MakeTargetDecision: Top-Level Protocol: %s Protocol: %s msgType: "
      "%s (%u), Actor Instance %u of %zu, actor ID: %d, PreservedHeader: %d\n",
      portNameToProtocolName[*name].c_str(), ProtocolIdToName(ids.second),
      IPC::StringFromIPCMessageType(*type), *type, actorIndex, actors.size(),
      *actorId, isPreserveHeader);

  if (update) {
    portSeqNos.insert_or_assign(*name,
                                std::pair<int32_t, uint64_t>(*seqno, *fseqno));
  }

  return true;
}

void IPCFuzzController::OnMessageTaskStart() { messageStartCount++; }

void IPCFuzzController::OnMessageTaskStop() { messageStopCount++; }

void IPCFuzzController::OnPreFuzzMessageTaskRun() { messageTaskCount++; }
void IPCFuzzController::OnPreFuzzMessageTaskStop() { messageTaskCount--; }

void IPCFuzzController::OnDropPeer(const char* reason = nullptr,
                                   const char* file = nullptr, int line = 0) {
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (!Nyx::instance().started()) {
    // It's possible to close a connection to some peer before we have even
    // started fuzzing. We ignore these events until we are actually fuzzing.
    return;
  }

  MOZ_FUZZING_NYX_PRINT(
      "ERROR: ======== END OF ITERATION (DROP_PEER) ========\n");
#ifdef FUZZ_DEBUG
  MOZ_FUZZING_NYX_PRINTF("DEBUG: ======== %s:%d ========\n", file, line);
#endif
  Nyx::instance().handle_event("MOZ_IPC_DROP_PEER", file, line, reason);

  if (Nyx::instance().is_replay()) {
    // In replay mode, let's ignore drop peer to avoid races with it.
    return;
  }

  Nyx::instance().release(IPCFuzzController::instance().getMessageStopCount());
}

void IPCFuzzController::StartFuzzing(mozilla::ipc::NodeChannel* channel,
                                     IPC::Message& aMessage) {
  nodeChannel = channel;

  RefPtr<IPCFuzzLoop> runnable = new IPCFuzzLoop();

#if MOZ_FUZZ_IPC_SYNC_INJECT
  runnable->Run();
#else
  nsCOMPtr<nsIThread> newThread;
  nsresult rv =
      NS_NewNamedThread("IPCFuzzLoop", getter_AddRefs(newThread), runnable);

  if (NS_FAILED(rv)) {
    MOZ_FUZZING_NYX_ABORT("ERROR: [StartFuzzing] NS_NewNamedThread failed?!\n");
  }
#endif
}

IPCFuzzController::IPCFuzzLoop::IPCFuzzLoop()
    : mozilla::Runnable("IPCFuzzLoop") {}

NS_IMETHODIMP IPCFuzzController::IPCFuzzLoop::Run() {
  MOZ_FUZZING_NYX_DEBUG("DEBUG: BEGIN IPCFuzzLoop::Run()\n");

  const size_t maxMsgSize = 2048;
  const size_t controlLen = 16;

  Vector<char, 256, InfallibleAllocPolicy> buffer;

  RefPtr<NodeController> controller = NodeController::GetSingleton();

  // TODO: The following code is full of data races. We need synchronization
  // on the `IPCFuzzController` instance, because the I/O thread can call into
  // this class via ObserveIPCMessages. The problem is that any such call
  // must either be observed to update the sequence numbers, or the packet
  // must be dropped already.
  if (!IPCFuzzController::instance().haveTargetNodeName) {
    MOZ_FUZZING_NYX_ABORT("ERROR: I don't have the target NodeName?!\n");
  }

  {
    MOZ_FUZZING_NYX_DEBUG("DEBUG: IPCFuzzLoop::Run() Mutex try\n");
    // Called on the I/O thread and modifies `portSeqNos` and `actorIds`.
    MutexAutoLock lock(IPCFuzzController::instance().mMutex);
    MOZ_FUZZING_NYX_DEBUG("DEBUG: IPCFuzzLoop::Run() Mutex locked\n");

    // The wait/delay logic in ObserveIPCMessage should ensure that we haven't
    // seen any packets on ports for which we haven't received actor information
    // yet, if those ports belong to our channel. However, we might also have
    // seen ports not belonging to our channel, which we have to remove now.
    for (auto iter = IPCFuzzController::instance().portSeqNos.begin();
         iter != IPCFuzzController::instance().portSeqNos.end();) {
      auto result = IPCFuzzController::instance().actorIds.find(iter->first);
      if (result == IPCFuzzController::instance().actorIds.end()) {
        auto portNameResult =
            IPCFuzzController::instance().portNodeName.find(iter->first);
        if (portNameResult->second ==
                IPCFuzzController::instance().targetNodeName &&
            IPCFuzzController::instance().mIPCTriggerMsg ==
                ipcDefaultTriggerMsg) {
          MOZ_FUZZING_NYX_PRINTF(
              "ERROR: We should not have port map entries without a "
              "corresponding "
              "entry in our actors map (Port %lu %lu)\n",
              iter->first.v1, iter->first.v2);
          MOZ_REALLY_CRASH(__LINE__);
        } else {
          iter = IPCFuzzController::instance().portSeqNos.erase(iter);
        }
      } else {
        ++iter;
      }
    }

    // TODO: Technically, at this point we only know that PContent (or whatever
    // toplevel protocol we decided to synchronize on), is present. It might
    // be possible that others aren't created yet and we are racing on this.
    //
    // Note: The delay logic mentioned above makes this less likely. Only actors
    // which are created on-demand and which have not been referenced yet at all
    // would be affected by such a race.
    for (auto iter = IPCFuzzController::instance().actorIds.begin();
         iter != IPCFuzzController::instance().actorIds.end(); ++iter) {
      bool isValidTarget = false;
      Maybe<PortStatus> status;
      PortRef ref = controller->GetPort(iter->first);
      if (ref.is_valid()) {
        status = controller->GetStatus(ref);
        if (status) {
          isValidTarget = status->peer_node_name ==
                          IPCFuzzController::instance().targetNodeName;
        }
      }

      auto result = IPCFuzzController::instance().portSeqNos.find(iter->first);
      if (result == IPCFuzzController::instance().portSeqNos.end()) {
        if (isValidTarget) {
          MOZ_FUZZING_NYX_PRINTF(
              "INFO: Using Port %lu %lu for protocol %s (*)\n", iter->first.v1,
              iter->first.v2, ProtocolIdToName(iter->second[0].second));

          // Normally the start sequence numbers would be -1 and 1, but our map
          // does not record the next numbers, but the "last seen" state. So we
          // have to adjust these so the next calculated sequence number pair
          // matches the start sequence numbers.
          IPCFuzzController::instance().portSeqNos.insert_or_assign(
              iter->first, std::pair<int32_t, uint64_t>(0, 0));

          IPCFuzzController::instance().AddToplevelActor(
              iter->first, iter->second[0].second);

        } else {
          MOZ_FUZZING_NYX_PRINTF(
              "INFO: Removing Port %lu %lu for protocol %s (*)\n",
              iter->first.v1, iter->first.v2,
              ProtocolIdToName(iter->second[0].second));

          // This toplevel actor does not belong to us, but we haven't added
          // it to `portSeqNos`, so we don't have to remove it.
        }
      } else {
        if (isValidTarget) {
          MOZ_FUZZING_NYX_PRINTF("INFO: Using Port %lu %lu for protocol %s\n",
                                 iter->first.v1, iter->first.v2,
                                 ProtocolIdToName(iter->second[0].second));

          IPCFuzzController::instance().AddToplevelActor(
              iter->first, iter->second[0].second);
        } else {
          MOZ_FUZZING_NYX_PRINTF(
              "INFO: Removing Port %lu %lu for protocol %s\n", iter->first.v1,
              iter->first.v2, ProtocolIdToName(iter->second[0].second));

          // This toplevel actor does not belong to us, so remove it.
          IPCFuzzController::instance().portSeqNos.erase(result);
        }
      }
    }
  }

  IPCFuzzController::instance().runnableDone = false;

  SyncRunnable::DispatchToThread(
      GetMainThreadSerialEventTarget(),
      NS_NewRunnableFunction("IPCFuzzController::StartFuzzing", [&]() -> void {
        MOZ_FUZZING_NYX_PRINT("INFO: Main thread runnable start.\n");
        NS_ProcessPendingEvents(NS_GetCurrentThread());
        MOZ_FUZZING_NYX_PRINT("INFO: Main thread runnable done.\n");
      }));

  MOZ_FUZZING_NYX_PRINT("INFO: Performing snapshot...\n");
  Nyx::instance().start();

  uint32_t expected_messages = 0;

  if (!buffer.initLengthUninitialized(maxMsgSize)) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Failed to initialize buffer!\n");
  }

  for (int i = 0; i < 3; ++i) {
    // Grab enough data to potentially fill our everything except the footer.
    uint32_t bufsize =
        Nyx::instance().get_data((uint8_t*)buffer.begin(), buffer.length());

    if (bufsize == 0xFFFFFFFF) {
      // Done constructing
      MOZ_FUZZING_NYX_DEBUG("Iteration complete: Out of data.\n");
      break;
    }

    // Payload must be int aligned
    bufsize -= bufsize % 4;

    // Need at least a header and the control bytes.
    if (bufsize < sizeof(IPC::Message::Header) + controlLen) {
      MOZ_FUZZING_NYX_DEBUG("INFO: Not enough data to craft IPC message.\n");
      continue;
    }

    const uint8_t* controlData = (uint8_t*)buffer.begin();

    char* ipcMsgData = buffer.begin() + controlLen;
    size_t ipcMsgLen = bufsize - controlLen;

    bool preserveHeader = controlData[15] == 0xFF;

    if (!preserveHeader) {
      // Copy the header of the original message
      memcpy(ipcMsgData, IPCFuzzController::instance().sampleHeader.begin(),
             sizeof(IPC::Message::Header));
    }

    IPC::Message::Header* ipchdr = (IPC::Message::Header*)ipcMsgData;

    ipchdr->payload_size = ipcMsgLen - sizeof(IPC::Message::Header);

    PortName new_port_name;
    int32_t new_seqno;
    uint64_t new_fseqno;

    int32_t actorId;
    uint32_t msgType = 0;
    bool isConstructor = false;
    // Control Data Layout (16 byte)
    // Byte  0 - Port Index (selects out of the valid ports seen)
    // Byte  1 - Actor Index (selects one of the actors for that port)
    // Byte  2 - Type Offset (select valid type for the specified actor)
    // Byte  3 -  ^- continued
    // Byte  4 - Actor Protocol Index (selects the protocol on that port)
    // Byte  5 - Optionally select a particular instance of the selected
    //           port type. Some toplevel protocols can have multiple
    //           instances running at the same time.
    //
    // Byte 15 - If set to 0xFF, skip overwriting the header, leave fields
    //           like message type intact and only set target actor and
    //           other fields that are dynamic.

    uint8_t portIndex = controlData[0];
    uint8_t actorIndex = controlData[1];
    uint16_t typeOffset = *(uint16_t*)(&controlData[2]);
    uint8_t actorProtocolIndex = controlData[4];
    uint8_t portInstanceIndex = controlData[5];

    UniquePtr<IPC::Message> msg(new IPC::Message(ipcMsgData, ipcMsgLen));

    if (preserveHeader) {
      isConstructor = msg->is_constructor();
      msgType = msg->header()->type;

      if (!msgType) {
        // msgType == 0 is used to indicate to MakeTargetDecision that we are
        // not in preserve header mode. It's not a valid message type in any
        // case and we can error out early.
        Nyx::instance().release(
            IPCFuzzController::instance().getMessageStopCount());
      }
    }

    if (!IPCFuzzController::instance().MakeTargetDecision(
            portIndex, portInstanceIndex, actorIndex, actorProtocolIndex,
            typeOffset, &new_port_name, &new_seqno, &new_fseqno, &actorId,
            &msgType, &isConstructor)) {
      MOZ_FUZZING_NYX_DEBUG("DEBUG: MakeTargetDecision returned false.\n");
      continue;
    }

    if (Nyx::instance().is_replay()) {
      MOZ_FUZZING_NYX_PRINT("INFO: Replaying IPC packet with payload:\n");
      for (uint32_t i = 0; i < ipcMsgLen - sizeof(IPC::Message::Header); ++i) {
        if (i % 16 == 0) {
          MOZ_FUZZING_NYX_PRINT("\n  ");
        }

        MOZ_FUZZING_NYX_PRINTF(
            "0x%02X ",
            (unsigned char)(ipcMsgData[sizeof(IPC::Message::Header) + i]));
      }
      MOZ_FUZZING_NYX_PRINT("\n");
    }

    if (isConstructor) {
      MOZ_FUZZING_NYX_DEBUG("DEBUG: Sending constructor message...\n");
      msg->header()->flags.SetConstructor();
    }

    if (IPC::IPCMessageTypeIsSync(msgType)) {
      MOZ_FUZZING_NYX_DEBUG("INFO: Sending sync message...\n");
      msg->header()->flags.SetSync();
    }

    msg->set_seqno(new_seqno);
    msg->set_routing_id(actorId);

    if (!preserveHeader) {
      // TODO: There is no setter for this.
      msg->header()->type = msgType;
    }

    // Make sure we're not sending with LAZY_SEND
    msg->header()->flags.mFlags &= ~IPC::Message::HeaderFlags::LAZY_SEND_BIT;

    // Create the footer
    auto messageEvent = MakeUnique<UserMessageEvent>(0);
    messageEvent->set_port_name(new_port_name);
    messageEvent->set_sequence_num(new_fseqno);

    Vector<char, 256, InfallibleAllocPolicy> footerBuffer;
    (void)footerBuffer.initLengthUninitialized(
        messageEvent->GetSerializedSize());
    messageEvent->Serialize(footerBuffer.begin());

    msg->WriteFooter(footerBuffer.begin(), footerBuffer.length());
    msg->set_event_footer_size(footerBuffer.length());

    // This marks the message as a fuzzing message. Without this, it will
    // be ignored by MessageTask and also not even scheduled by NodeChannel
    // in asynchronous mode. We use this to ignore any IPC activity that
    // happens just while we are fuzzing.
    msg->SetFuzzMsg();

#ifdef FUZZ_DEBUG
    MOZ_FUZZING_NYX_PRINTF(
        "DEBUG: OnEventMessage iteration %d, EVS: %u Payload: %u.\n", i,
        ipchdr->event_footer_size, ipchdr->payload_size);
#endif

#ifdef FUZZ_DEBUG
    MOZ_FUZZING_NYX_PRINTF("DEBUG: OnEventMessage: Port %lu %lu. Actor %d\n",
                           new_port_name.v1, new_port_name.v2, actorId);
    MOZ_FUZZING_NYX_PRINTF(
        "DEBUG: OnEventMessage: Flags: %u TxID: %d Handles: %u\n",
        msg->header()->flags, msg->header()->txid, msg->header()->num_handles);
#endif

    if (!!getenv("MOZ_FUZZ_DEBUG")) {
      MOZ_FUZZING_NYX_PRINTF(
          "INFO: Flags: IsSync: %d IsReply: %d IsReplyError: %d IsConstructor: "
          "%d IsRelay: %d IsLazySend: %d\n",
          msg->is_sync(), msg->is_reply(), msg->is_reply_error(),
          msg->is_constructor(), msg->is_relay(), msg->is_lazy_send());
    }

    // The number of messages we expect to see stopped.
    expected_messages++;

#if MOZ_FUZZ_IPC_SYNC_INJECT
    // For synchronous injection, we just call OnMessageReceived directly.
    IPCFuzzController::instance().nodeChannel->OnMessageReceived(
        std::move(msg));
#else
    // For asynchronous injection, we have to post to the I/O thread instead.
    XRE_GetAsyncIOEventTarget()->Dispatch(NS_NewRunnableFunction(
        "NodeChannel::OnMessageReceived",
        [msg = std::move(msg),
         nodeChannel =
             RefPtr{IPCFuzzController::instance().nodeChannel}]() mutable {
          int32_t msgType = msg->header()->type;

          // By default, we sync on the target thread of the receiving actor.
          bool syncOnIOThread = false;

          switch (msgType) {
            case DATA_PIPE_CLOSED_MESSAGE_TYPE:
            case DATA_PIPE_BYTES_CONSUMED_MESSAGE_TYPE:
            case ACCEPT_INVITE_MESSAGE_TYPE:
            case REQUEST_INTRODUCTION_MESSAGE_TYPE:
            case INTRODUCE_MESSAGE_TYPE:
            case BROADCAST_MESSAGE_TYPE:
              // This set of special messages will not be routed to actors and
              // therefore we won't see these as stopped messages later. These
              // messages are either used by NodeChannel, DataPipe or
              // MessageChannel without creating MessageTasks. As such, the best
              // we can do is synchronize on this thread. We do this by
              // emulating the MessageTaskStart/Stop behavior that normal event
              // messages have.
              syncOnIOThread = true;
              break;
            default:
              // Synchronization will happen in MessageChannel. Note that this
              // also applies to certain special message types, as long as they
              // are received by actors and not intercepted earlier.
              break;
          }

          if (syncOnIOThread) {
            mozilla::fuzzing::IPCFuzzController::instance()
                .OnMessageTaskStart();
          }

          nodeChannel->OnMessageReceived(std::move(msg));

          if (syncOnIOThread) {
            mozilla::fuzzing::IPCFuzzController::instance().OnMessageTaskStop();

            // Don't continue for now after sending such a special message.
            // It can cause ports to go away and further messages can time out.
            Nyx::instance().release(
                IPCFuzzController::instance().getMessageStopCount());
          }
        }));
#endif

#ifdef MOZ_FUZZ_IPC_SYNC_AFTER_EACH_MSG
    MOZ_FUZZING_NYX_DEBUG("DEBUG: Synchronizing after message...\n");
    IPCFuzzController::instance().SynchronizeOnMessageExecution(
        expected_messages);

    SyncRunnable::DispatchToThread(
        GetMainThreadSerialEventTarget(),
        NS_NewRunnableFunction(
            "IPCFuzzController::StartFuzzing", [&]() -> void {
              MOZ_FUZZING_NYX_DEBUG("DEBUG: Main thread runnable start.\n");
              NS_ProcessPendingEvents(NS_GetCurrentThread());
              MOZ_FUZZING_NYX_DEBUG("DEBUG: Main thread runnable done.\n");
            }));
#else

    if (isConstructor) {
      MOZ_FUZZING_NYX_DEBUG(
          "DEBUG: Synchronizing due to constructor message...\n");
      IPCFuzzController::instance().SynchronizeOnMessageExecution(
          expected_messages);
    }
#endif
  }

#ifndef MOZ_FUZZ_IPC_SYNC_AFTER_EACH_MSG
  MOZ_FUZZING_NYX_DEBUG("DEBUG: Synchronizing due to end of iteration...\n");
  IPCFuzzController::instance().SynchronizeOnMessageExecution(
      expected_messages);

  SyncRunnable::DispatchToThread(
      GetMainThreadSerialEventTarget(),
      NS_NewRunnableFunction("IPCFuzzController::StartFuzzing", [&]() -> void {
        MOZ_FUZZING_NYX_DEBUG("DEBUG: Main thread runnable start.\n");
        NS_ProcessPendingEvents(NS_GetCurrentThread());
        MOZ_FUZZING_NYX_DEBUG("DEBUG: Main thread runnable done.\n");
      }));
#endif

  MOZ_FUZZING_NYX_DEBUG(
      "DEBUG: ======== END OF ITERATION (RELEASE) ========\n");

  Nyx::instance().release(IPCFuzzController::instance().getMessageStopCount());

  // Never reached.
  return NS_OK;
}

void IPCFuzzController::SynchronizeOnMessageExecution(
    uint32_t expected_messages) {
  // This synchronization will work in both the sync and async case.
  // For the async case, it is important to wait for the exact stop count
  // because the message task is not even started potentially when we
  // read this loop.
  int hang_timeout = 10 * 1000;
  while (IPCFuzzController::instance().getMessageStopCount() !=
         expected_messages) {
#ifdef FUZZ_DEBUG
    uint32_t count_stopped =
        IPCFuzzController::instance().getMessageStopCount();
    uint32_t count_live = IPCFuzzController::instance().getMessageStartCount();
    MOZ_FUZZING_NYX_PRINTF(
        "DEBUG: Post Constructor: %d stopped messages (%d live, %d "
        "expected)!\n",
        count_stopped, count_live, expected_messages);
#endif
    PR_Sleep(PR_MillisecondsToInterval(50));
    hang_timeout -= 50;

    if (hang_timeout <= 0) {
      Nyx::instance().handle_event("MOZ_TIMEOUT", nullptr, 0, nullptr);
      MOZ_FUZZING_NYX_PRINT(
          "ERROR: ======== END OF ITERATION (TIMEOUT) ========\n");
      if (!!getenv("MOZ_FUZZ_CRASH_ON_TIMEOUT")) {
        MOZ_DIAGNOSTIC_CRASH("IPCFuzzController Timeout");
      }
      Nyx::instance().release(
          IPCFuzzController::instance().getMessageStopCount());
    }
  }
}

static void dumpIPCMessageToFile(const UniquePtr<IPC::Message>& aMsg,
                                 uint32_t aDumpCount, bool aUseNyx = false) {
  if (Nyx::instance().is_replay()) {
    return;
  }

  std::stringstream dumpFilename;
  std::string msgName(IPC::StringFromIPCMessageType(aMsg->type()));
  std::replace(msgName.begin(), msgName.end(), ':', '_');

  if (aUseNyx) {
    dumpFilename << "seeds/";
  }

  dumpFilename << msgName << aDumpCount << ".bin";

  Pickle::BufferList::IterImpl iter(aMsg->Buffers());
  Vector<char, 256, InfallibleAllocPolicy> dumpBuffer;
  if (!dumpBuffer.initLengthUninitialized(sizeof(IPC::Message::Header) +
                                          aMsg->Buffers().Size())) {
    MOZ_FUZZING_NYX_ABORT("dumpBuffer.initLengthUninitialized failed\n");
  }
  if (!aMsg->Buffers().ReadBytes(
          iter,
          reinterpret_cast<char*>(dumpBuffer.begin() +
                                  sizeof(IPC::Message::Header)),
          dumpBuffer.length() - sizeof(IPC::Message::Header))) {
    MOZ_FUZZING_NYX_ABORT("ReadBytes failed\n");
  }
  memcpy(dumpBuffer.begin(), aMsg->header(), sizeof(IPC::Message::Header));

  if (aUseNyx) {
    MOZ_FUZZING_NYX_PRINTF("INFO: Calling dump_file: %s Size: %zu\n",
                           dumpFilename.str().c_str(), dumpBuffer.length());
    Nyx::instance().dump_file(reinterpret_cast<char*>(dumpBuffer.begin()),
                              dumpBuffer.length(), dumpFilename.str().c_str());
  } else {
    std::fstream file;
    file.open(dumpFilename.str(), std::ios::out | std::ios::binary);
    file.write(reinterpret_cast<char*>(dumpBuffer.begin()),
               dumpBuffer.length());
    file.close();
  }
}

UniquePtr<IPC::Message> IPCFuzzController::replaceIPCMessage(
    UniquePtr<IPC::Message> aMsg) {
  if (!mozilla::fuzzing::Nyx::instance().is_enabled("IPC_SingleMessage")) {
    // Fuzzer is not enabled.
    return aMsg;
  }

  if (!XRE_IsParentProcess()) {
    // For now we only care about things in the parent process.
    return aMsg;
  }

  static bool dumpFilterInitialized = false;
  static std::string dumpFilter;
  if (!dumpFilterInitialized) {
    const char* dumpFilterStr = getenv("MOZ_FUZZ_DUMP_FILTER");
    if (dumpFilterStr) {
      dumpFilter = std::string(dumpFilterStr);
    }
    dumpFilterInitialized = true;
  }

  if (aMsg->type() != mIPCTriggerMsg) {
    if ((mIPCDumpMsg && aMsg->type() == mIPCDumpMsg.value()) ||
        (mIPCDumpAllMsgsSize.isSome() &&
         aMsg->Buffers().Size() >= mIPCDumpAllMsgsSize.value())) {
      if (!dumpFilter.empty()) {
        std::string msgName(IPC::StringFromIPCMessageType(aMsg->type()));
        if (msgName.find(dumpFilter) != std::string::npos) {
          dumpIPCMessageToFile(aMsg, mIPCDumpCount);
          mIPCDumpCount++;
        }
      } else {
        dumpIPCMessageToFile(aMsg, mIPCDumpCount);
        mIPCDumpCount++;
      }
    }

    // Not the trigger message. Output additional information here for
    // automation purposes. This shouldn't be an issue as we will only
    // output these messages until we take a snapshot.
    MOZ_FUZZING_NYX_PRINTF("INFO: [OnIPCMessage] Message: %s Size: %u\n",
                           IPC::StringFromIPCMessageType(aMsg->type()),
                           aMsg->header()->payload_size);
    return aMsg;
  } else {
    // Dump the trigger message through Nyx in case we want to use it
    // as a seed to AFL++ outside of the VM.
    dumpIPCMessageToFile(aMsg, mIPCDumpCount, true /* aUseNyx */);
    mIPCDumpCount++;
    if (mIPCTriggerSingleMsgWait > 0) {
      mIPCTriggerSingleMsgWait--;
      return aMsg;
    }
  }

  const size_t maxMsgSize = 4096;

  Vector<char, 256, InfallibleAllocPolicy> buffer;
  if (!buffer.initLengthUninitialized(maxMsgSize)) {
    MOZ_FUZZING_NYX_ABORT("ERROR: Failed to initialize buffer!\n");
  }

  char* ipcMsgData = buffer.begin();

  //                        //
  // *** Snapshot Point *** //
  //                        //
  MOZ_FUZZING_NYX_PRINT("INFO: Performing snapshot...\n");
  Nyx::instance().start();

  IPCFuzzController::instance().useLastActor = 0;
  IPCFuzzController::instance().useLastPortName = false;
  IPCFuzzController::instance().useLastPortNameAlways = false;

  MOZ_FUZZING_NYX_DEBUG("DEBUG: Requesting data...\n");

  // Grab enough data to send at most `maxMsgSize` bytes
  uint32_t bufsize =
      Nyx::instance().get_raw_data((uint8_t*)buffer.begin(), buffer.length());

  if (bufsize == 0xFFFFFFFF) {
    MOZ_FUZZING_NYX_DEBUG("Nyx: Out of data.\n");
    Nyx::instance().release(0);
  }

#ifdef FUZZ_DEBUG
  MOZ_FUZZING_NYX_PRINTF("DEBUG: Got buffer of size %u...\n", bufsize);
#endif

  // Payload must be int aligned
  bufsize -= bufsize % 4;

  // Need at least a header and the control bytes.
  if (bufsize < sizeof(IPC::Message::Header)) {
    MOZ_FUZZING_NYX_DEBUG("INFO: Not enough data to craft IPC message.\n");
    Nyx::instance().release(0);
  }

  buffer.shrinkTo(bufsize);

  // Copy the header of the original message
  memcpy(ipcMsgData, aMsg->header(), sizeof(IPC::Message::Header));
  IPC::Message::Header* ipchdr = (IPC::Message::Header*)ipcMsgData;

  size_t ipcMsgLen = buffer.length();
  ipchdr->payload_size = ipcMsgLen - sizeof(IPC::Message::Header);

  if (Nyx::instance().is_replay()) {
    MOZ_FUZZING_NYX_PRINT("INFO: Replaying single IPC packet with payload:\n");
    for (uint32_t i = 0; i < ipcMsgLen - sizeof(IPC::Message::Header); ++i) {
      if (i % 16 == 0) {
        MOZ_FUZZING_NYX_PRINT("\n  ");
      }

      MOZ_FUZZING_NYX_PRINTF(
          "0x%02X ",
          (unsigned char)(ipcMsgData[sizeof(IPC::Message::Header) + i]));
    }
    MOZ_FUZZING_NYX_PRINT("\n");
  }

  UniquePtr<IPC::Message> msg(new IPC::Message(ipcMsgData, ipcMsgLen));

  if (!!getenv("MOZ_FUZZ_DEBUG")) {
    MOZ_FUZZING_NYX_PRINTF("INFO: Name: %s Target: %d\n", msg->name(),
                           msg->routing_id());
  }

  // This marks the message as a fuzzing message. Without this, it will
  // be ignored by MessageTask and also not even scheduled by NodeChannel
  // in asynchronous mode. We use this to ignore any IPC activity that
  // happens just while we are fuzzing.
  msg->SetFuzzMsg();

  return msg;
}

void IPCFuzzController::syncAfterReplace() {
  if (!mozilla::fuzzing::Nyx::instance().is_enabled("IPC_SingleMessage")) {
    // Fuzzer is not enabled.
    return;
  }

  if (!XRE_IsParentProcess()) {
    // For now we only care about things in the parent process.
    return;
  }

  if (!Nyx::instance().started()) {
    // Not started yet
    return;
  }

  MOZ_FUZZING_NYX_DEBUG(
      "DEBUG: ======== END OF ITERATION (RELEASE) ========\n");

  Nyx::instance().release(1);
}

}  // namespace fuzzing
}  // namespace mozilla
