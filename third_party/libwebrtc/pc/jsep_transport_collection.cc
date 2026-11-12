/*
 *  Copyright 2021 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/jsep_transport_collection.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "api/function_view.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/sequence_checker.h"
#include "p2p/base/p2p_constants.h"
#include "pc/jsep_transport.h"
#include "pc/session_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

void BundleManager::Update(const SessionDescription* description,
                           SdpType type) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // Rollbacks should call Rollback, not Update.
  RTC_DCHECK(type != SdpType::kRollback);
  bool bundle_groups_changed = false;
  // TODO: bugs.webrtc.org/42228228 - Evaluate whether a PR-Answer can establish
  // a bundle and a final Answer can remove it again. If this happens, PRAnswer
  // should not destroy the unused transport.
  if (bundle_policy_ == PeerConnectionInterface::kBundlePolicyMaxBundle ||
      type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
    // If our policy is "max-bundle" or this is an answer, update all bundle
    // groups.
    bundle_groups_changed = true;
    bundle_groups_.clear();
    for (const ContentGroup* new_bundle_group :
         description->GetGroupsByName(GROUP_TYPE_BUNDLE)) {
      bundle_groups_.push_back(
          std::make_unique<ContentGroup>(*new_bundle_group));
      RTC_DLOG(LS_VERBOSE) << "Establishing bundle group "
                           << new_bundle_group->ToString();
    }
  } else if (type == SdpType::kOffer) {
    // If this is an offer, update existing bundle groups.
    // We do this because as per RFC 8843, section 7.3.2, the answerer cannot
    // remove an m= section from an existing BUNDLE group without rejecting it.
    // Thus any m= sections added to a BUNDLE group in this offer can
    // preemptively start using the bundled transport, as there is no possible
    // non-bundled fallback.
    for (const ContentGroup* new_bundle_group :
         description->GetGroupsByName(GROUP_TYPE_BUNDLE)) {
      // Attempt to find a matching existing group.
      for (const std::string& mid : new_bundle_group->content_names()) {
        auto it = established_bundle_groups_by_mid_.find(mid);
        if (it != established_bundle_groups_by_mid_.end()) {
          *it->second = *new_bundle_group;
          bundle_groups_changed = true;
          RTC_DLOG(LS_VERBOSE)
              << "Establishing bundle group " << new_bundle_group->ToString();
          break;
        }
      }
    }
  }
  if (bundle_groups_changed) {
    RefreshEstablishedBundleGroupsByMid();
  }
}

const ContentGroup* BundleManager::LookupGroupByMid(
    absl::string_view mid) const {
  auto it = established_bundle_groups_by_mid_.find(mid);
  return it != established_bundle_groups_by_mid_.end() ? it->second : nullptr;
}
bool BundleManager::IsFirstMidInGroup(absl::string_view mid) const {
  auto group = LookupGroupByMid(mid);
  if (!group) {
    return true;  // Unbundled MIDs are considered group leaders
  }
  return mid == *(group->FirstContentName());
}

ContentGroup* BundleManager::LookupGroupByMid(absl::string_view mid) {
  auto it = established_bundle_groups_by_mid_.find(mid);
  return it != established_bundle_groups_by_mid_.end() ? it->second : nullptr;
}

void BundleManager::DeleteMid(const ContentGroup* bundle_group,
                              absl::string_view mid) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_LOG(LS_VERBOSE) << "Deleting mid " << mid << " from bundle group "
                      << bundle_group->ToString();
  // Remove the rejected content from the `bundle_group`.
  // The const pointer arg is used to identify the group, we verify
  // it before we use it to make a modification.
  auto bundle_group_it =
      std::find_if(bundle_groups_.begin(), bundle_groups_.end(),
                   [bundle_group](std::unique_ptr<ContentGroup>& group) {
                     return bundle_group == group.get();
                   });
  RTC_DCHECK(bundle_group_it != bundle_groups_.end());
  (*bundle_group_it)->RemoveContentName(mid);
  established_bundle_groups_by_mid_.erase(
      established_bundle_groups_by_mid_.find(mid));
}

void BundleManager::DeleteGroup(const ContentGroup* bundle_group) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DLOG(LS_VERBOSE) << "Deleting bundle group " << bundle_group->ToString();

  auto bundle_group_it =
      std::find_if(bundle_groups_.begin(), bundle_groups_.end(),
                   [bundle_group](std::unique_ptr<ContentGroup>& group) {
                     return bundle_group == group.get();
                   });
  RTC_DCHECK(bundle_group_it != bundle_groups_.end());
  auto mid_list = (*bundle_group_it)->content_names();
  for (const auto& content_name : mid_list) {
    DeleteMid(bundle_group, content_name);
  }
  bundle_groups_.erase(bundle_group_it);
}

void BundleManager::Rollback() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  bundle_groups_.clear();
  for (const auto& bundle_group : stable_bundle_groups_) {
    bundle_groups_.push_back(std::make_unique<ContentGroup>(*bundle_group));
  }
  RefreshEstablishedBundleGroupsByMid();
}

void BundleManager::Commit() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  stable_bundle_groups_.clear();
  for (const auto& bundle_group : bundle_groups_) {
    stable_bundle_groups_.push_back(
        std::make_unique<ContentGroup>(*bundle_group));
  }
}

void BundleManager::RefreshEstablishedBundleGroupsByMid() {
  established_bundle_groups_by_mid_.clear();
  for (const auto& bundle_group : bundle_groups_) {
    for (const std::string& content_name : bundle_group->content_names()) {
      established_bundle_groups_by_mid_[content_name] = bundle_group.get();
    }
  }
}

void JsepTransportCollection::RegisterTransport(
    std::unique_ptr<JsepTransport> transport) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(!GetTransportByName(transport->name()));
  SetTransportForMid(transport->name(), transport.get());
  transports_.push_back(std::move(transport));
  RTC_DCHECK(IsConsistent());
}

void JsepTransportCollection::ForEachTransport(
    FunctionView<void(JsepTransport&)> callback) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  absl::c_for_each(transports_, [&](auto& t) { callback(*t); });
}

void JsepTransportCollection::ForEachActiveTransport(
    FunctionView<void(JsepTransport&)> callback) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  absl::c_for_each(mid_to_transport_, [&](auto& kv) { callback(*kv.second); });
}

void JsepTransportCollection::ForEachMid(
    FunctionView<void(absl::string_view mid, JsepTransport& transport)>
        callback) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  for (const auto& [mid, transport] : mid_to_transport_) {
    callback(mid, *transport);
  }
}

void JsepTransportCollection::DestroyAllTransports() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  for (const auto& jsep_transport : transports_) {
    map_change_callback_(jsep_transport->name(), nullptr);
  }
  transports_.clear();
  RTC_DCHECK(IsConsistent());
}

const JsepTransport* JsepTransportCollection::GetTransportByName(
    absl::string_view transport_name) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  auto it = absl::c_find_if(
      transports_, [&](const auto& t) { return t->name() == transport_name; });
  return (it == transports_.end()) ? nullptr : it->get();
}

JsepTransport* JsepTransportCollection::GetTransportByName(
    absl::string_view transport_name) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  auto it = absl::c_find_if(
      transports_, [&](const auto& t) { return t->name() == transport_name; });
  return (it == transports_.end()) ? nullptr : it->get();
}

JsepTransport* JsepTransportCollection::GetTransportForMid(
    absl::string_view mid) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  auto it = mid_to_transport_.find(mid);
  return it == mid_to_transport_.end() ? nullptr : it->second;
}

const JsepTransport* JsepTransportCollection::GetTransportForMid(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  auto it = mid_to_transport_.find(mid);
  return it == mid_to_transport_.end() ? nullptr : it->second;
}

bool JsepTransportCollection::SetTransportForMid(
    absl::string_view mid,
    JsepTransport* jsep_transport) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(jsep_transport);

  auto it = mid_to_transport_.find(mid);
  if (it != mid_to_transport_.end() && it->second == jsep_transport)
    return true;

  // The map_change_callback must be called before destroying the
  // transport, because it removes references to the transport
  // in the RTP demuxer.
  bool result = map_change_callback_(mid, jsep_transport);

  if (it == mid_to_transport_.end()) {
    mid_to_transport_.insert(std::make_pair(std::string(mid), jsep_transport));
  } else {
    auto old_transport = it->second;
    it->second = jsep_transport;
    MaybeDestroyJsepTransport(old_transport);
  }
  RTC_DCHECK(IsConsistent());
  return result;
}

void JsepTransportCollection::RemoveTransportForMid(absl::string_view mid) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(IsConsistent());
  bool ret = map_change_callback_(mid, nullptr);
  // Calling OnTransportChanged with nullptr should always succeed, since it is
  // only expected to fail when adding media to a transport (not removing).
  RTC_DCHECK(ret);

  auto old_transport = GetTransportForMid(mid);
  if (old_transport) {
    mid_to_transport_.erase(mid);
    MaybeDestroyJsepTransport(old_transport);
  }
  RTC_DCHECK(IsConsistent());
}

bool JsepTransportCollection::RollbackTransports() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  bool ret = true;
  // First, remove any new mid->transport mappings.
  for (const auto& kv : mid_to_transport_) {
    if (stable_mid_to_transport_.count(kv.first) == 0) {
      bool success = map_change_callback_(kv.first, nullptr);
      ret = ret && success;
    }
  }
  // Next, restore old mappings.
  for (const auto& kv : stable_mid_to_transport_) {
    auto it = mid_to_transport_.find(kv.first);
    if (it == mid_to_transport_.end() || it->second != kv.second) {
      bool success = map_change_callback_(kv.first, kv.second);
      ret = ret && success;
    }
  }
  mid_to_transport_ = stable_mid_to_transport_;
  // Moving a transport back to mid_to_transport_ means it's now included in
  // the aggregate state if it wasn't previously.
  state_change_callback_();
  DestroyUnusedTransports();
  RTC_DCHECK(IsConsistent());
  return ret;
}

void JsepTransportCollection::CommitTransports() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  stable_mid_to_transport_ = mid_to_transport_;
  DestroyUnusedTransports();
  RTC_HISTOGRAM_COUNTS_LINEAR("WebRTC.PeerConnection.RtpTransportCount",
                              transports_.size(), 1, 49, 50);
  RTC_DCHECK(IsConsistent());
}

bool JsepTransportCollection::TransportInUse(
    JsepTransport* jsep_transport) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return absl::c_any_of(mid_to_transport_, [&](const auto& kv) {
    return kv.second == jsep_transport;
  });
}

bool JsepTransportCollection::TransportNeededForRollback(
    JsepTransport* jsep_transport) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return absl::c_any_of(stable_mid_to_transport_, [&](const auto& kv) {
    return kv.second == jsep_transport;
  });
}

void JsepTransportCollection::MaybeDestroyJsepTransport(
    JsepTransport* transport) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // Don't destroy the JsepTransport if there are still media sections
  // referring to it, or if it will be needed in case of rollback.
  if (TransportInUse(transport)) {
    return;
  }
  // If this transport is needed for rollback, don't destroy it yet, but make
  // sure the aggregate state is updated since this transport is no longer
  // included in it.
  if (TransportNeededForRollback(transport)) {
    state_change_callback_();
    return;
  }
  auto it = absl::c_find_if(
      transports_, [&](const auto& t) { return t.get() == transport; });
  if (it != transports_.end()) {
    transports_.erase(it);
    state_change_callback_();
  }
  RTC_DCHECK(IsConsistent());
}

void JsepTransportCollection::DestroyUnusedTransports() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  bool need_state_change_callback = false;
  auto it = transports_.begin();
  while (it != transports_.end()) {
    if (TransportInUse(it->get()) || TransportNeededForRollback(it->get())) {
      ++it;
    } else {
      it = transports_.erase(it);
      need_state_change_callback = true;
    }
  }
  if (need_state_change_callback) {
    state_change_callback_();
  }
}

bool JsepTransportCollection::IsConsistent() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  bool consistent = true;
  for (const auto& t : transports_) {
    if (!TransportInUse(t.get()) && !TransportNeededForRollback(t.get())) {
      RTC_LOG(LS_ERROR) << "Transport registered with mid " << t->name()
                        << " is not in use";
      consistent = false;
    }
  }
  return consistent;
}

}  // namespace webrtc
