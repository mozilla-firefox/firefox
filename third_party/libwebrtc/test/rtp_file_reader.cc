/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/rtp_file_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

namespace webrtc {
namespace test {

namespace {
constexpr size_t kRtpDumpFirstLineLength = 80;
constexpr uint16_t kRtpDumpPacketHeaderSize = 8;

enum {
  kResultFail = -1,
  kResultSuccess = 0,
  kResultSkip = 1,
};

enum {
  kPcapVersionMajor = 2,
  kPcapVersionMinor = 4,
  kLinktypeNull = 0,
  kLinktypeEthernet = 1,
  kBsdNullLoopback1 = 0x00000002,
  kBsdNullLoopback2 = 0x02000000,
  kEthernetIIHeaderMacSkip = 12,
  kEthertypeIp = 0x0800,
  kIpVersion4 = 4,
  kMinIpHeaderLength = 20,
  kFragmentOffsetClear = 0x0000,
  kFragmentOffsetDoNotFragment = 0x4000,
  kProtocolTcp = 0x06,
  kProtocolUdp = 0x11,
  kUdpHeaderLength = 8,
};

constexpr size_t kMaxReadBufferSize = 4096;
constexpr uint32_t kPcapBOMSwapOrder = 0xd4c3b2a1UL;
constexpr uint32_t kPcapBOMNoSwapOrder = 0xa1b2c3d4UL;
constexpr uint32_t kPcapNgBOMLittleEndian = 0x4d3c2b1aUL;

constexpr uint32_t kPcapNgSectionHeaderBlock = 0x0a0d0d0aUL;
constexpr uint32_t kPcapNgInterfaceDescriptionBlock = 0x00000001LU;
constexpr uint32_t kPcapNgPacketBlock = 0x00000006LU;

#define TRY(expr)                           \
  do {                                      \
    if (!(expr)) {                          \
      RTC_LOG(LS_INFO) << "Failed to read"; \
      return false;                         \
    }                                       \
  } while (0)

#define TRY_PCAP(expr)                                               \
  do {                                                               \
    int r = (expr);                                                  \
    if (r == kResultFail) {                                          \
      RTC_LOG(LS_INFO) << "FAIL at " << __FILE__ << ":" << __LINE__; \
      return kResultFail;                                            \
    } else if (r == kResultSkip) {                                   \
      return kResultSkip;                                            \
    }                                                                \
  } while (0)

bool ReadUint32(uint32_t* out, FILE* file) {
  *out = 0;
  for (size_t i = 0; i < 4; ++i) {
    *out <<= 8;
    uint8_t tmp;
    if (fread(&tmp, 1, sizeof(uint8_t), file) != sizeof(uint8_t))
      return false;
    *out |= tmp;
  }
  return true;
}

bool ReadUint16(uint16_t* out, FILE* file) {
  *out = 0;
  for (size_t i = 0; i < 2; ++i) {
    *out <<= 8;
    uint8_t tmp;
    if (fread(&tmp, 1, sizeof(uint8_t), file) != sizeof(uint8_t))
      return false;
    *out |= tmp;
  }
  return true;
}

}  // namespace

class RtpFileReaderImpl : public RtpFileReader {
 public:
  virtual bool Init(FILE* file, const std::set<uint32_t>& ssrc_filter) = 0;
};

class InterleavedRtpFileReader : public RtpFileReaderImpl {
 public:
  ~InterleavedRtpFileReader() override {
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
    }
  }

  bool Init(FILE* file, const std::set<uint32_t>& ssrc_filter) override {
    file_ = file;
    return true;
  }

  bool NextPacket(RtpPacket* packet) override {
    RTC_DCHECK(file_);
    uint32_t len = 0;
    TRY(ReadUint32(&len, file_));
    if (len > RtpPacket::kMaxPacketBufferSize) {
      RTC_FATAL() << "Packet is too large to fit: " << len << " bytes vs "
                  << packet->length()
                  << ". Consider increasing the buffer size";
    }
    packet->buffer.SetSize(len);
    if (fread(packet->buffer.AsMutableSpan().data(), 1, len, file_) != len)
      return false;

    packet->original_length = len;
    packet->time_ms = time_ms_;
    time_ms_ += 5;
    return true;
  }

 private:
  FILE* file_ = nullptr;
  int64_t time_ms_ = 0;
};

// Read RTP packets from file in rtpdump format, as documented at:
// http://www.cs.columbia.edu/irt/software/rtptools/
class RtpDumpReader : public RtpFileReaderImpl {
 public:
  RtpDumpReader() : file_(nullptr) {}
  ~RtpDumpReader() override {
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
    }
  }

  RtpDumpReader(const RtpDumpReader&) = delete;
  RtpDumpReader& operator=(const RtpDumpReader&) = delete;

  bool Init(FILE* file, const std::set<uint32_t>& ssrc_filter) override {
    file_ = file;

    char firstline[kRtpDumpFirstLineLength + 1] = {0};
    if (fgets(firstline, kRtpDumpFirstLineLength, file_) == nullptr) {
      RTC_LOG(LS_INFO) << "Can't read from file";
      return false;
    }
    if (strncmp(firstline, "#!rtpplay", 9) == 0) {
      if (strncmp(firstline, "#!rtpplay1.0", 12) != 0) {
        RTC_LOG(LS_INFO) << "Wrong rtpplay version, must be 1.0";
        return false;
      }
    } else if (strncmp(firstline, "#!RTPencode", 11) == 0) {
      if (strncmp(firstline, "#!RTPencode1.0", 14) != 0) {
        RTC_LOG(LS_INFO) << "Wrong RTPencode version, must be 1.0";
        return false;
      }
    } else {
      RTC_LOG(LS_INFO)
          << "Input file is neither in rtpplay nor RTPencode format";
      return false;
    }

    uint32_t start_sec;
    uint32_t start_usec;
    uint32_t source;
    uint16_t port;
    uint16_t padding;
    TRY(ReadUint32(&start_sec, file_));
    TRY(ReadUint32(&start_usec, file_));
    TRY(ReadUint32(&source, file_));
    TRY(ReadUint16(&port, file_));
    TRY(ReadUint16(&padding, file_));

    return true;
  }

  bool NextPacket(RtpPacket* packet) override {
    uint16_t len;
    uint16_t plen;
    uint32_t offset;
    TRY(ReadUint16(&len, file_));
    TRY(ReadUint16(&plen, file_));
    TRY(ReadUint32(&offset, file_));

    // Use 'len' here because a 'plen' of 0 specifies rtcp.
    len -= kRtpDumpPacketHeaderSize;
    if (len > RtpPacket::kMaxPacketBufferSize) {
      RTC_LOG(LS_ERROR) << "Packet is too large to fit: " << len << " bytes vs "
                        << RtpPacket::kMaxPacketBufferSize
                        << ". Consider increasing the buffer size";
      return false;
    }
    packet->buffer.SetSize(len);
    if (fread(packet->buffer.AsMutableSpan().data(), 1, len, file_) != len) {
      return false;
    }

    packet->original_length = plen;
    packet->time_ms = offset;
    return true;
  }

 private:
  FILE* file_;
};

// Read RTP packets from file in tcpdump/libpcap format, as documented at:
// http://wiki.wireshark.org/Development/LibpcapFileFormat
// Transparently supports PCAPNG as described at
// https://pcapng.com/
class PcapReader : public RtpFileReaderImpl {
 public:
  PcapReader()
      : file_(nullptr),
        swap_pcap_byte_order_(false),
#ifdef WEBRTC_ARCH_BIG_ENDIAN
        swap_network_byte_order_(false),
#else
        swap_network_byte_order_(true),
#endif
        pcapng_(false),
        read_buffer_(),
        packets_by_ssrc_(),
        packets_(),
        next_packet_it_() {
  }

  ~PcapReader() override {
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
    }
  }

  PcapReader(const PcapReader&) = delete;
  PcapReader& operator=(const PcapReader&) = delete;

  bool Init(FILE* file, const std::set<uint32_t>& ssrc_filter) override {
    return Initialize(file, ssrc_filter) == kResultSuccess;
  }

  int Initialize(FILE* file, const std::set<uint32_t>& ssrc_filter) {
    file_ = file;

    size_t total_packet_count = 0;
    if (ReadGlobalHeader() < 0) {
      return kResultFail;
    }
    int result;
    if (!pcapng_) {
      result = ReadPcap(ssrc_filter, total_packet_count);
    } else {
      result = ReadPcapNg(ssrc_filter, total_packet_count);
    }
    if (result == kResultFail) {
      return kResultFail;
    }

    RTC_LOG(LS_INFO) << "Total packets in file: " << total_packet_count;
    RTC_LOG(LS_INFO) << "Total RTP/RTCP packets: " << packets_.size();

    for (SsrcMapIterator mit = packets_by_ssrc_.begin();
         mit != packets_by_ssrc_.end(); ++mit) {
      uint32_t ssrc = mit->first;
      const std::vector<uint32_t>& packet_indices = mit->second;
      int pt = packets_[packet_indices[0]].payload_type;
      RTC_LOG(LS_INFO) << "SSRC: " << ssrc << ", " << packet_indices.size()
                       << " packets, pt=" << pt << ".";
    }

    // TODO(solenberg): Better validation of identified SSRC streams.
    //
    // Since we're dealing with raw network data here, we will wrongly identify
    // some packets as RTP. When these packets are consumed by RtpPlayer, they
    // are unlikely to cause issues as they will ultimately be filtered out by
    // the RtpRtcp module. However, we should really do better filtering here,
    // which we can accomplish in a number of ways, e.g.:
    //
    // - Verify that the time stamps and sequence numbers for RTP packets are
    //   both increasing/decreasing. If they move in different directions, the
    //   SSRC is likely bogus and can be dropped. (Normally they should be inc-
    //   reasing but we must allow packet reordering).
    // - If RTP sequence number is not changing, drop the stream.
    // - Can also use srcip:port->dstip:port pairs, assuming few SSRC collisions
    //   for up/down streams.

    next_packet_it_ = packets_.begin();
    return kResultSuccess;
  }

  int ReadPcap(const std::set<uint32_t>& ssrc_filter,
               size_t& total_packet_count) {
    uint32_t stream_start_ms = 0;
    int32_t next_packet_pos = ftell(file_);
    for (;;) {
      TRY_PCAP(fseek(file_, next_packet_pos, SEEK_SET));
      int result = ReadPacket(&next_packet_pos, stream_start_ms, ssrc_filter);
      if (result == kResultFail) {
        break;
      } else if (result == kResultSuccess && packets_.size() == 1) {
        RTC_DCHECK_EQ(stream_start_ms, 0);
        PacketIterator it = packets_.begin();
        stream_start_ms = it->time_offset_ms;
        it->time_offset_ms = 0;
      }
      total_packet_count++;
    }

    if (feof(file_) == 0) {
      RTC_LOG(LS_ERROR) << "Failed reading file!";
      return kResultFail;
    }
    return kResultSuccess;
  }

  int ReadPcapNg(const std::set<uint32_t>& ssrc_filter,
                 size_t& total_packet_count) {
    uint32_t stream_start_ms = 0;
    int next_packet_pos = 0;
    for (;;) {
      TRY_PCAP(fseek(file_, next_packet_pos, SEEK_SET));
      int result = ReadPacketNg(&next_packet_pos, stream_start_ms, ssrc_filter);

      if (result == kResultFail) {
        break;
      } else if (result == kResultSuccess && packets_.size() == 1) {
        RTC_DCHECK_EQ(stream_start_ms, 0);
        PacketIterator it = packets_.begin();
        stream_start_ms = it->time_offset_ms;
        it->time_offset_ms = 0;
      }
      total_packet_count++;
    }
    if (feof(file_) == 0) {
      RTC_LOG(LS_ERROR) << "Failed reading file!";
      return kResultFail;
    }
    return kResultSuccess;
  }

  bool NextPacket(RtpPacket* packet) override {
    if (next_packet_it_ == packets_.end()) {
      return false;
    }
    if (next_packet_it_->payload_length > RtpPacket::kMaxPacketBufferSize) {
      return false;
    }
    if (fseek(file_, next_packet_it_->pos_in_file, SEEK_SET) != 0) {
      return false;
    }
    packet->buffer.SetSize(next_packet_it_->payload_length);
    if (Read(packet->buffer.AsMutableSpan().data(), packet->buffer.size()) ==
        kResultFail) {
      return false;
    }
    packet->time_ms = next_packet_it_->time_offset_ms;
    packet->original_length = packet->length();
    next_packet_it_++;
    return true;
  }

 private:
  // A marker of an RTP packet within the file.
  struct RtpPacketMarker {
    uint32_t time_offset_ms;
    uint32_t source_ip;
    uint32_t dest_ip;
    uint16_t source_port;
    uint16_t dest_port;
    // Payload type of the RTP packet,
    // or RTCP packet type of the first RTCP packet in a compound RTCP packet.
    int payload_type;
    int32_t pos_in_file;  // Byte offset of payload from start of file.
    uint32_t payload_length;
  };

  typedef std::vector<RtpPacketMarker>::iterator PacketIterator;
  typedef std::map<uint32_t, std::vector<uint32_t> > SsrcMap;
  typedef std::map<uint32_t, std::vector<uint32_t> >::iterator SsrcMapIterator;

  int ReadGlobalHeader() {
    uint32_t magic;
    TRY_PCAP(Read(&magic, false));
    if (magic == kPcapBOMSwapOrder) {
      swap_pcap_byte_order_ = true;
    } else if (magic == kPcapBOMNoSwapOrder) {
      swap_pcap_byte_order_ = false;
    } else if (magic == kPcapNgSectionHeaderBlock) {
      pcapng_ = true;
      RTC_LOG(LS_INFO) << "PCAPNG detected, support is experimental";
      return kResultSuccess;
    } else {
      return kResultFail;
    }

    uint16_t version_major;
    uint16_t version_minor;
    TRY_PCAP(Read(&version_major, false));
    TRY_PCAP(Read(&version_minor, false));
    if (version_major != kPcapVersionMajor ||
        version_minor != kPcapVersionMinor) {
      return kResultFail;
    }

    int32_t this_zone;  // GMT to local correction.
    uint32_t sigfigs;   // Accuracy of timestamps.
    uint32_t snaplen;   // Max length of captured packets, in octets.
    uint32_t network;   // Data link type.
    TRY_PCAP(Read(&this_zone, false));
    TRY_PCAP(Read(&sigfigs, false));
    TRY_PCAP(Read(&snaplen, false));
    TRY_PCAP(Read(&network, false));

    // Accept only LINKTYPE_NULL and LINKTYPE_ETHERNET.
    // See: http://www.tcpdump.org/linktypes.html
    if (network != kLinktypeNull && network != kLinktypeEthernet) {
      return kResultFail;
    }

    return kResultSuccess;
  }

  int ProcessPacket(RtpPacketMarker& marker,
                    const std::set<uint32_t>& ssrc_filter,
                    std::span<const uint8_t> packet) {
    if (IsRtcpPacket(packet)) {
      marker.payload_type = packet[1];
      packets_.push_back(marker);
    } else if (IsRtpPacket(packet)) {
      uint32_t ssrc = ParseRtpSsrc(packet);
      marker.payload_type = ParseRtpPayloadType(packet);
      if (ssrc_filter.empty() || ssrc_filter.find(ssrc) != ssrc_filter.end()) {
        packets_by_ssrc_[ssrc].push_back(
            static_cast<uint32_t>(packets_.size()));
        packets_.push_back(marker);
      } else {
        return kResultSkip;
      }
    } else {
      RTC_LOG(LS_INFO) << "Not recognized as RTP/RTCP";
      return kResultSkip;
    }

    return kResultSuccess;
  }

  int ReadPacket(int32_t* next_packet_pos,
                 uint32_t stream_start_ms,
                 const std::set<uint32_t>& ssrc_filter) {
    RTC_DCHECK(next_packet_pos);

    uint32_t ts_sec;    // Timestamp seconds.
    uint32_t ts_usec;   // Timestamp microseconds.
    uint32_t incl_len;  // Number of octets of packet saved in file.
    uint32_t orig_len;  // Actual length of packet.
    TRY_PCAP(Read(&ts_sec, false));
    TRY_PCAP(Read(&ts_usec, false));
    TRY_PCAP(Read(&incl_len, false));
    TRY_PCAP(Read(&orig_len, false));

    *next_packet_pos = ftell(file_) + incl_len;

    RtpPacketMarker marker = {};
    marker.time_offset_ms = CalcTimeDelta(ts_sec, ts_usec, stream_start_ms);
    TRY_PCAP(ReadPacketHeader(&marker));
    marker.pos_in_file = ftell(file_);

    if (marker.payload_length > sizeof(read_buffer_)) {
      RTC_LOG(LS_ERROR) << "Packet too large!";
      return kResultFail;
    }
    TRY_PCAP(Read(read_buffer_, marker.payload_length));
    return ProcessPacket(marker, ssrc_filter,
                         {read_buffer_, marker.payload_length});
  }

  int ReadPacketNg(int32_t* next_packet_pos,
                   uint32_t stream_start_ms,
                   const std::set<uint32_t>& ssrc_filter) {
    uint32_t block_type;
    uint32_t block_length;
    TRY_PCAP(Read(&block_type, false));
    TRY_PCAP(Read(&block_length, false));
    if (block_length == 0) {
      RTC_LOG(LS_ERROR) << "Empty PCAPNG block";
      return kResultFail;
    }

    *next_packet_pos += block_length;
    switch (block_type) {
      case kPcapNgSectionHeaderBlock: {
        // TODO: https://issues.webrtc.org/issues/351327754 - interpret more of
        // this block, in particular the if_tsresol option.
        uint32_t byte_order_magic;
        TRY_PCAP(Read(&byte_order_magic, false));
        swap_pcap_byte_order_ = (byte_order_magic == kPcapNgBOMLittleEndian);
      } break;
      case kPcapNgInterfaceDescriptionBlock:
        break;
      case kPcapNgPacketBlock: {
        uint32_t interface;  // Interface ID. Unused.
        uint32_t ts_upper;   // Upper 32 bits of timestamp.
        uint32_t ts_lower;   // Lower 32 bits of timestamp.
        uint32_t incl_len;   // Number of octets of packet saved in file.
        uint32_t orig_len;   // Actual length of packet.
        TRY_PCAP(Read(&interface, false));
        TRY_PCAP(Read(&ts_upper, false));
        TRY_PCAP(Read(&ts_lower, false));
        TRY_PCAP(Read(&incl_len, false));
        TRY_PCAP(Read(&orig_len, false));

        RtpPacketMarker marker = {};
        // Note: Wireshark writes nanoseconds most of the time, see comments in
        // it's pcapio.c. We are only interesting in the time difference so
        // truncating to uint32_t is ok.
        uint64_t timestamp_ms =
            ((static_cast<uint64_t>(ts_upper) << 32) | ts_lower) /
            kNumMicrosecsPerSec;
        marker.time_offset_ms =
            static_cast<uint32_t>(timestamp_ms) - stream_start_ms;
        TRY_PCAP(ReadPacketHeader(&marker));
        marker.pos_in_file = ftell(file_);
        if (marker.payload_length > sizeof(read_buffer_)) {
          RTC_LOG(LS_ERROR) << "Packet too large!";
          return kResultFail;
        }
        TRY_PCAP(Read(read_buffer_, marker.payload_length));
        if (ProcessPacket(marker, ssrc_filter,
                          {read_buffer_, marker.payload_length}) !=
            kResultSuccess) {
          return kResultFail;
        }
        return kResultSuccess;
      }
    }
    return kResultSkip;
  }

  int ReadPacketHeader(RtpPacketMarker* marker) {
    int32_t file_pos = ftell(file_);

    // Check for BSD null/loopback frame header. The header is just 4 bytes in
    // native byte order, so we check for both versions as we don't care about
    // the header as such and will likely fail reading the IP header if this is
    // something else than null/loopback.
    uint32_t protocol;
    TRY_PCAP(Read(&protocol, true));
    if (protocol == kBsdNullLoopback1 || protocol == kBsdNullLoopback2) {
      int result = ReadXxpIpHeader(marker);
      if (result != kResultSkip) {
        return result;
      }
    }

    TRY_PCAP(fseek(file_, file_pos, SEEK_SET));

    // Check for Ethernet II, IP frame header.
    uint16_t type;
    TRY_PCAP(Skip(kEthernetIIHeaderMacSkip));  // Source+destination MAC.
    TRY_PCAP(Read(&type, true));
    if (type == kEthertypeIp) {
      int result = ReadXxpIpHeader(marker);
      if (result != kResultSkip) {
        return result;
      }
    }

    return kResultSkip;
  }

  uint32_t CalcTimeDelta(uint32_t ts_sec, uint32_t ts_usec, uint32_t start_ms) {
    // Round to nearest ms.
    uint64_t t2_ms =
        ((static_cast<uint64_t>(ts_sec) * 1000000) + ts_usec + 500) / 1000;
    uint64_t t1_ms = static_cast<uint64_t>(start_ms);
    if (t2_ms < t1_ms) {
      return 0;
    } else {
      return t2_ms - t1_ms;
    }
  }

  int ReadXxpIpHeader(RtpPacketMarker* marker) {
    RTC_DCHECK(marker);

    uint16_t version;
    uint16_t length;
    uint16_t id;
    uint16_t fragment;
    uint16_t protocol;
    uint16_t checksum;
    TRY_PCAP(Read(&version, true));
    TRY_PCAP(Read(&length, true));
    TRY_PCAP(Read(&id, true));
    TRY_PCAP(Read(&fragment, true));
    TRY_PCAP(Read(&protocol, true));
    TRY_PCAP(Read(&checksum, true));
    TRY_PCAP(Read(&marker->source_ip, true));
    TRY_PCAP(Read(&marker->dest_ip, true));

    if (((version >> 12) & 0x000f) != kIpVersion4) {
      RTC_LOG(LS_INFO) << "IP header is not IPv4";
      return kResultSkip;
    }

    if (fragment != kFragmentOffsetClear &&
        fragment != kFragmentOffsetDoNotFragment) {
      RTC_LOG(LS_INFO) << "IP fragments cannot be handled";
      return kResultSkip;
    }

    // Skip remaining fields of IP header.
    uint16_t header_length = (version & 0x0f00) >> (8 - 2);
    RTC_DCHECK_GE(header_length, kMinIpHeaderLength);
    TRY_PCAP(Skip(header_length - kMinIpHeaderLength));

    protocol = protocol & 0x00ff;
    if (protocol == kProtocolTcp) {
      RTC_LOG(LS_INFO) << "TCP packets are not handled";
      return kResultSkip;
    } else if (protocol == kProtocolUdp) {
      uint16_t payload_length;
      uint16_t payload_checksum;
      TRY_PCAP(Read(&marker->source_port, true));
      TRY_PCAP(Read(&marker->dest_port, true));
      TRY_PCAP(Read(&payload_length, true));
      TRY_PCAP(Read(&payload_checksum, true));
      marker->payload_length = payload_length - kUdpHeaderLength;
    } else {
      RTC_LOG(LS_INFO) << "Unknown transport (expected UDP or TCP)";
      return kResultSkip;
    }

    return kResultSuccess;
  }

  int Read(uint32_t* out, bool expect_network_order) {
    uint32_t tmp = 0;
    if (fread(&tmp, 1, sizeof(uint32_t), file_) != sizeof(uint32_t)) {
      return kResultFail;
    }
    if ((!expect_network_order && swap_pcap_byte_order_) ||
        (expect_network_order && swap_network_byte_order_)) {
      tmp = ((tmp >> 24) & 0x000000ff) | (tmp << 24) |
            ((tmp >> 8) & 0x0000ff00) | ((tmp << 8) & 0x00ff0000);
    }
    *out = tmp;
    return kResultSuccess;
  }

  int Read(uint16_t* out, bool expect_network_order) {
    uint16_t tmp = 0;
    if (fread(&tmp, 1, sizeof(uint16_t), file_) != sizeof(uint16_t)) {
      return kResultFail;
    }
    if ((!expect_network_order && swap_pcap_byte_order_) ||
        (expect_network_order && swap_network_byte_order_)) {
      tmp = ((tmp >> 8) & 0x00ff) | (tmp << 8);
    }
    *out = tmp;
    return kResultSuccess;
  }

  int Read(uint8_t* out, uint32_t count) {
    if (fread(out, 1, count, file_) != count) {
      return kResultFail;
    }
    return kResultSuccess;
  }

  int Read(int32_t* out, bool expect_network_order) {
    int32_t tmp = 0;
    if (fread(&tmp, 1, sizeof(uint32_t), file_) != sizeof(uint32_t)) {
      return kResultFail;
    }
    if ((!expect_network_order && swap_pcap_byte_order_) ||
        (expect_network_order && swap_network_byte_order_)) {
      tmp = ((tmp >> 24) & 0x000000ff) | (tmp << 24) |
            ((tmp >> 8) & 0x0000ff00) | ((tmp << 8) & 0x00ff0000);
    }
    *out = tmp;
    return kResultSuccess;
  }

  int Skip(uint32_t length) {
    if (fseek(file_, length, SEEK_CUR) != 0) {
      return kResultFail;
    }
    return kResultSuccess;
  }

  FILE* file_;
  bool swap_pcap_byte_order_;
  const bool swap_network_byte_order_;
  bool pcapng_;
  uint8_t read_buffer_[kMaxReadBufferSize];

  SsrcMap packets_by_ssrc_;
  std::vector<RtpPacketMarker> packets_;
  PacketIterator next_packet_it_;
};

RtpFileReaderImpl* CreateReaderForFormat(RtpFileReader::FileFormat format) {
  RtpFileReaderImpl* reader = nullptr;
  switch (format) {
    case RtpFileReader::kPcap:
      reader = new PcapReader();
      break;
    case RtpFileReader::kRtpDump:
      reader = new RtpDumpReader();
      break;
    case RtpFileReader::kLengthPacketInterleaved:
      reader = new InterleavedRtpFileReader();
      break;
  }
  return reader;
}

RtpFileReader* RtpFileReader::Create(FileFormat format,
                                     const uint8_t* data,
                                     size_t size,
                                     const std::set<uint32_t>& ssrc_filter) {
  std::unique_ptr<RtpFileReaderImpl> reader(CreateReaderForFormat(format));

  FILE* file = tmpfile();
  if (file == nullptr) {
    RTC_LOG(LS_ERROR) << "ERROR: Can't open file from memory buffer.";
    return nullptr;
  }

  if (fwrite(reinterpret_cast<const void*>(data), sizeof(uint8_t), size,
             file) != size) {
    return nullptr;
  }
  rewind(file);

  if (!reader->Init(file, ssrc_filter)) {
    return nullptr;
  }
  return reader.release();
}

RtpFileReader* RtpFileReader::Create(FileFormat format,
                                     absl::string_view filename,
                                     const std::set<uint32_t>& ssrc_filter) {
  RtpFileReaderImpl* reader = CreateReaderForFormat(format);
  std::string filename_str = std::string(filename);
  FILE* file = fopen(filename_str.c_str(), "rb");
  if (file == nullptr) {
    RTC_LOG(LS_ERROR) << "ERROR: Can't open file: '" << filename_str << "'.";
    return nullptr;
  }

  if (!reader->Init(file, ssrc_filter)) {
    delete reader;
    return nullptr;
  }
  return reader;
}

RtpFileReader* RtpFileReader::Create(FileFormat format,
                                     absl::string_view filename) {
  return RtpFileReader::Create(format, filename, std::set<uint32_t>());
}

}  // namespace test
}  // namespace webrtc
