/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/stream_params.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "media/base/rid_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/unique_id_generator.h"

namespace webrtc {
namespace {

void AppendSsrcs(std::span<const uint32_t> ssrcs, StringBuilder* sb) {
  *sb << "ssrcs:[";
  const char* delimiter = "";
  for (uint32_t ssrc : ssrcs) {
    *sb << delimiter << ssrc;
    delimiter = ",";
  }
  *sb << "]";
}

void AppendSsrcGroups(std::span<const SsrcGroup> ssrc_groups,
                      StringBuilder* sb) {
  *sb << "ssrc_groups:";
  const char* delimiter = "";
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    *sb << delimiter << ssrc_group.ToString();
    delimiter = ",";
  }
}

void AppendStreamIds(std::span<const std::string> stream_ids,
                     StringBuilder* sb) {
  *sb << "stream_ids:";
  const char* delimiter = "";
  for (const std::string& stream_id : stream_ids) {
    *sb << delimiter << stream_id;
    delimiter = ",";
  }
}

void AppendRids(std::span<const RidDescription> rids, StringBuilder* sb) {
  *sb << "rids:[";
  const char* delimiter = "";
  for (const RidDescription& rid : rids) {
    *sb << delimiter << rid.rid;
    delimiter = ",";
  }
  *sb << "]";
}

}  // namespace

const char kFecSsrcGroupSemantics[] = "FEC";
const char kFecFrSsrcGroupSemantics[] = "FEC-FR";
const char kFidSsrcGroupSemantics[] = "FID";
const char kSimSsrcGroupSemantics[] = "SIM";

bool GetStream(const StreamParamsVec& streams,
               const StreamSelector& selector,
               StreamParams* stream_out) {
  const StreamParams* found = GetStream(streams, selector);
  if (found && stream_out)
    *stream_out = *found;
  return found != nullptr;
}

SsrcGroup::SsrcGroup(absl::string_view usage,
                     const std::vector<uint32_t>& ssrcs)
    : semantics(usage), ssrcs(ssrcs) {}
SsrcGroup::SsrcGroup(const SsrcGroup&) = default;
SsrcGroup::SsrcGroup(SsrcGroup&&) = default;
SsrcGroup::~SsrcGroup() = default;

SsrcGroup& SsrcGroup::operator=(const SsrcGroup&) = default;
SsrcGroup& SsrcGroup::operator=(SsrcGroup&&) = default;

bool SsrcGroup::has_semantics(absl::string_view semantics_in) const {
  return (semantics == semantics_in && !ssrcs.empty());
}

std::string SsrcGroup::ToString() const {
  StringBuilder sb;
  sb << "{";
  sb << "semantics:" << semantics << ";";
  AppendSsrcs(ssrcs, &sb);
  sb << "}";
  return sb.Release();
}

StreamParams::StreamParams() = default;
StreamParams::StreamParams(const StreamParams&) = default;
StreamParams::StreamParams(StreamParams&&) = default;
StreamParams::~StreamParams() = default;
StreamParams& StreamParams::operator=(const StreamParams&) = default;
StreamParams& StreamParams::operator=(StreamParams&&) = default;

bool StreamParams::operator==(const StreamParams& other) const {
  return (id == other.id && ssrcs == other.ssrcs &&
          ssrc_groups == other.ssrc_groups && cname == other.cname &&
          stream_ids_ == other.stream_ids_ &&
          // RIDs are not required to be in the same order for equality.
          absl::c_is_permutation(rids_, other.rids_));
}

std::string StreamParams::ToString() const {
  StringBuilder sb;
  sb << "{";
  if (!id.empty()) {
    sb << "id:" << id << ";";
  }
  AppendSsrcs(ssrcs, &sb);
  sb << ";";
  AppendSsrcGroups(ssrc_groups, &sb);
  sb << ";";
  if (!cname.empty()) {
    sb << "cname:" << cname << ";";
  }
  AppendStreamIds(stream_ids_, &sb);
  sb << ";";
  if (!rids_.empty()) {
    AppendRids(rids_, &sb);
    sb << ";";
  }
  sb << "}";
  return sb.Release();
}

void StreamParams::GenerateSsrcs(int num_layers,
                                 bool generate_fid,
                                 bool generate_fec_fr,
                                 UniqueRandomIdGenerator* ssrc_generator) {
  RTC_DCHECK_GE(num_layers, 0);
  RTC_DCHECK(ssrc_generator);
  std::vector<uint32_t> primary_ssrcs;
  for (int i = 0; i < num_layers; ++i) {
    uint32_t ssrc = ssrc_generator->GenerateId();
    primary_ssrcs.push_back(ssrc);
    add_ssrc(ssrc);
  }

  if (num_layers > 1) {
    SsrcGroup simulcast(kSimSsrcGroupSemantics, primary_ssrcs);
    ssrc_groups.push_back(simulcast);
  }

  if (generate_fid) {
    for (uint32_t ssrc : primary_ssrcs) {
      AddFidSsrc(ssrc, ssrc_generator->GenerateId());
    }
  }

  if (generate_fec_fr) {
    for (uint32_t ssrc : primary_ssrcs) {
      AddFecFrSsrc(ssrc, ssrc_generator->GenerateId());
    }
  }
}

void StreamParams::GetPrimarySsrcs(std::vector<uint32_t>* primary_ssrcs) const {
  const SsrcGroup* sim_group = get_ssrc_group(kSimSsrcGroupSemantics);
  if (sim_group == nullptr) {
    primary_ssrcs->push_back(first_ssrc());
  } else {
    primary_ssrcs->insert(primary_ssrcs->end(), sim_group->ssrcs.begin(),
                          sim_group->ssrcs.end());
  }
}

void StreamParams::GetSecondarySsrcs(
    absl::string_view semantics,
    const std::vector<uint32_t>& primary_ssrcs,
    std::vector<uint32_t>* secondary_ssrcs) const {
  for (uint32_t primary_ssrc : primary_ssrcs) {
    uint32_t secondary_ssrc;
    if (GetSecondarySsrc(semantics, primary_ssrc, &secondary_ssrc)) {
      secondary_ssrcs->push_back(secondary_ssrc);
    }
  }
}

void StreamParams::GetFidSsrcs(const std::vector<uint32_t>& primary_ssrcs,
                               std::vector<uint32_t>* fid_ssrcs) const {
  return GetSecondarySsrcs(kFidSsrcGroupSemantics, primary_ssrcs, fid_ssrcs);
}

bool StreamParams::AddSecondarySsrc(absl::string_view semantics,
                                    uint32_t primary_ssrc,
                                    uint32_t secondary_ssrc) {
  if (!has_ssrc(primary_ssrc)) {
    return false;
  }

  ssrcs.push_back(secondary_ssrc);
  ssrc_groups.push_back(SsrcGroup(semantics, {primary_ssrc, secondary_ssrc}));
  return true;
}

bool StreamParams::GetSecondarySsrc(absl::string_view semantics,
                                    uint32_t primary_ssrc,
                                    uint32_t* secondary_ssrc) const {
  for (const SsrcGroup& ssrc_group : ssrc_groups) {
    if (ssrc_group.has_semantics(semantics) && ssrc_group.ssrcs.size() >= 2 &&
        ssrc_group.ssrcs[0] == primary_ssrc) {
      *secondary_ssrc = ssrc_group.ssrcs[1];
      return true;
    }
  }
  return false;
}

void StreamParams::set_stream_ids(const std::vector<std::string>& stream_ids) {
  stream_ids_ = stream_ids;
}

absl::string_view StreamParams::first_stream_id() const {
  return stream_ids_.empty() ? absl::string_view("") : stream_ids_[0];
}

}  // namespace webrtc
