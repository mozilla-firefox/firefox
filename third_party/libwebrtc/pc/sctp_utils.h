/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_SCTP_UTILS_H_
#define PC_SCTP_UTILS_H_

#include <string>

#include "api/data_channel_interface.h"
#include "api/transport/data_channel_transport_interface.h"
#include "media/base/media_channel.h"
#include "media/sctp/sctp_transport_internal.h"
#include "net/dcsctp/public/types.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/ssl_stream_adapter.h"  // For SSLRole

namespace rtc {
class CopyOnWriteBuffer;
}  // namespace rtc

namespace webrtc {
struct DataChannelInit;

// Wraps the `uint16_t` sctp data channel stream id value and does range
// checking. The class interface is `int` based to ease with DataChannelInit
// compatibility and types used in `DataChannelController`'s interface. Going
// forward, `int` compatibility won't be needed and we can either just use
// this class or the internal dcsctp::StreamID type.
class StreamId {
 public:
  StreamId() = default;
  explicit StreamId(uint16_t id) : id_(id) {}
  StreamId(const StreamId& sid) = default;
  StreamId& operator=(const StreamId& sid) = default;
  // Provided for compatibility with existing code that hasn't been updated
  // to use `StreamId` directly. New code should not use 'int' for the stream
  // id but rather `StreamId` directly.
  int stream_id_int() const { return static_cast<int>(id_.value()); }

  bool operator==(const StreamId& sid) const { return id_ == sid.id_; }
  bool operator<(const StreamId& sid) const { return id_ < sid.id_; }
  bool operator!=(const StreamId& sid) const { return !(operator==(sid)); }

 private:
  dcsctp::StreamID id_;
};

// Read the message type and return true if it's an OPEN message.
bool IsOpenMessage(const rtc::CopyOnWriteBuffer& payload);

bool ParseDataChannelOpenMessage(const rtc::CopyOnWriteBuffer& payload,
                                 std::string* label,
                                 DataChannelInit* config);

bool ParseDataChannelOpenAckMessage(const rtc::CopyOnWriteBuffer& payload);

bool WriteDataChannelOpenMessage(const std::string& label,
                                 const std::string& protocol,
                                 absl::optional<Priority> priority,
                                 bool ordered,
                                 absl::optional<int> max_retransmits,
                                 absl::optional<int> max_retransmit_time,
                                 rtc::CopyOnWriteBuffer* payload);
bool WriteDataChannelOpenMessage(const std::string& label,
                                 const DataChannelInit& config,
                                 rtc::CopyOnWriteBuffer* payload);
void WriteDataChannelOpenAckMessage(rtc::CopyOnWriteBuffer* payload);

}  // namespace webrtc

#endif  // PC_SCTP_UTILS_H_
