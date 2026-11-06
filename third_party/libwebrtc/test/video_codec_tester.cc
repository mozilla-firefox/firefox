/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/video_codec_tester.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "api/array_view.h"
#include "api/environment/environment.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/simulcast_stream.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "media/base/media_constants.h"
#include "modules/video_coding/codecs/av1/av1_svc_config.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp9/svc_config.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "modules/video_coding/utility/ivf_file_writer.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/sleep.h"
#include "test/scoped_key_value_config.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"
#include "test/testsupport/video_frame_writer.h"
#include "third_party/libyuv/include/libyuv/compare.h"
#include "video/config/simulcast.h"

namespace webrtc {
namespace test {

namespace {
using CodedVideoSource = VideoCodecTester::CodedVideoSource;
using VideoSourceSettings = VideoCodecTester::VideoSourceSettings;
using EncodingSettings = VideoCodecTester::EncodingSettings;
using LayerSettings = EncodingSettings::LayerSettings;
using LayerId = VideoCodecTester::LayerId;
using EncoderSettings = VideoCodecTester::EncoderSettings;
using DecoderSettings = VideoCodecTester::DecoderSettings;
using PacingSettings = VideoCodecTester::PacingSettings;
using PacingMode = PacingSettings::PacingMode;
using VideoCodecStats = VideoCodecTester::VideoCodecStats;
using DecodeCallback =
    absl::AnyInvocable<void(const VideoFrame& decoded_frame)>;
using webrtc::test::ImprovementDirection;

constexpr Frequency k90kHz = Frequency::Hertz(90000);

const std::set<ScalabilityMode> kFullSvcScalabilityModes{
    ScalabilityMode::kL2T1,  ScalabilityMode::kL2T1h, ScalabilityMode::kL2T2,
    ScalabilityMode::kL2T2h, ScalabilityMode::kL2T3,  ScalabilityMode::kL2T3h,
    ScalabilityMode::kL3T1,  ScalabilityMode::kL3T1h, ScalabilityMode::kL3T2,
    ScalabilityMode::kL3T2h, ScalabilityMode::kL3T3,  ScalabilityMode::kL3T3h};

const std::set<ScalabilityMode> kKeySvcScalabilityModes{
    ScalabilityMode::kL2T1_KEY,       ScalabilityMode::kL2T2_KEY,
    ScalabilityMode::kL2T2_KEY_SHIFT, ScalabilityMode::kL2T3_KEY,
    ScalabilityMode::kL3T1_KEY,       ScalabilityMode::kL3T2_KEY,
    ScalabilityMode::kL3T3_KEY};

// A thread-safe raw video frame reader.
class VideoSource {
 public:
  explicit VideoSource(VideoSourceSettings source_settings)
      : source_settings_(source_settings) {
    MutexLock lock(&mutex_);
    frame_reader_ = CreateYuvFrameReader(
        source_settings_.file_path, source_settings_.resolution,
        YuvFrameReaderImpl::RepeatMode::kPingPong);
    RTC_CHECK(frame_reader_);
  }

  // Pulls next frame.
  VideoFrame PullFrame(uint32_t timestamp_rtp,
                       Resolution resolution,
                       Frequency framerate) {
    MutexLock lock(&mutex_);
    int frame_num;
    auto buffer = frame_reader_->PullFrame(
        &frame_num, resolution,
        {.num = framerate.millihertz<int>(),
         .den = source_settings_.framerate.millihertz<int>()});
    RTC_CHECK(buffer) << "Can not pull frame. RTP timestamp " << timestamp_rtp;
    frame_num_[timestamp_rtp] = frame_num;
    return VideoFrame::Builder()
        .set_video_frame_buffer(buffer)
        .set_rtp_timestamp(timestamp_rtp)
        .set_timestamp_us((timestamp_rtp / k90kHz).us())
        .build();
  }

  // Reads frame specified by `timestamp_rtp`, scales it to `resolution` and
  // returns. Frame with the given `timestamp_rtp` is expected to be pulled
  // before.
  VideoFrame ReadFrame(uint32_t timestamp_rtp, Resolution resolution) {
    MutexLock lock(&mutex_);
    RTC_CHECK(frame_num_.find(timestamp_rtp) != frame_num_.end())
        << "Frame with RTP timestamp " << timestamp_rtp
        << " was not pulled before";
    auto buffer =
        frame_reader_->ReadFrame(frame_num_.at(timestamp_rtp), resolution);
    return VideoFrame::Builder()
        .set_video_frame_buffer(buffer)
        .set_rtp_timestamp(timestamp_rtp)
        .build();
  }

 private:
  VideoSourceSettings source_settings_;
  std::unique_ptr<FrameReader> frame_reader_ RTC_GUARDED_BY(mutex_);
  std::map<uint32_t, int> frame_num_ RTC_GUARDED_BY(mutex_);
  Mutex mutex_;
};

// Pacer calculates delay necessary to keep frame encode or decode call spaced
// from the previous calls by the pacing time. `Schedule` is expected to be
// called as close as possible to posting frame encode or decode task. This
// class is not thread safe.
class Pacer {
 public:
  explicit Pacer(PacingSettings settings)
      : settings_(settings), delay_(TimeDelta::Zero()) {}

  Timestamp Schedule(Timestamp timestamp) {
    Timestamp now = Timestamp::Micros(rtc::TimeMicros());
    if (settings_.mode == PacingMode::kNoPacing) {
      return now;
    }

    Timestamp scheduled = now;
    if (prev_scheduled_) {
      scheduled = *prev_scheduled_ + PacingTime(timestamp);
      if (scheduled < now) {
        scheduled = now;
      }
    }

    prev_timestamp_ = timestamp;
    prev_scheduled_ = scheduled;
    return scheduled;
  }

 private:
  TimeDelta PacingTime(Timestamp timestamp) {
    if (settings_.mode == PacingMode::kRealTime) {
      return timestamp - *prev_timestamp_;
    }
    RTC_CHECK_EQ(PacingMode::kConstantRate, settings_.mode);
    return 1 / settings_.constant_rate;
  }

  PacingSettings settings_;
  absl::optional<Timestamp> prev_timestamp_;
  absl::optional<Timestamp> prev_scheduled_;
  TimeDelta delay_;
};

class LimitedTaskQueue {
 public:
  // The codec tester reads frames from video source in the main thread.
  // Encoding and decoding are done in separate threads. If encoding or
  // decoding is slow, the reading may go far ahead and may buffer too many
  // frames in memory. To prevent this we limit the encoding/decoding queue
  // size. When the queue is full, the main thread and, hence, reading frames
  // from video source is blocked until a previously posted encoding/decoding
  // task starts.
  static constexpr int kMaxTaskQueueSize = 3;

  LimitedTaskQueue() : queue_size_(0) {}

  void PostScheduledTask(absl::AnyInvocable<void() &&> task, Timestamp start) {
    ++queue_size_;
    task_queue_.PostTask([this, task = std::move(task), start]() mutable {
      // `TaskQueue` doesn't guarantee FIFO order of execution for delayed
      // tasks.
      int64_t wait_ms = (start - Timestamp::Millis(rtc::TimeMillis())).ms();
      if (wait_ms > 0) {
        RTC_CHECK_LT(wait_ms, 10000) << "Too high wait_ms " << wait_ms;
        SleepMs(wait_ms);
      }
      std::move(task)();
      --queue_size_;
      task_executed_.Set();
    });

    task_executed_.Reset();
    if (queue_size_ > kMaxTaskQueueSize) {
      task_executed_.Wait(rtc::Event::kForever);
      RTC_CHECK(queue_size_ <= kMaxTaskQueueSize);
    }
  }

  void PostTaskAndWait(absl::AnyInvocable<void() &&> task) {
    PostScheduledTask(std::move(task), Timestamp::Millis(rtc::TimeMillis()));
    task_queue_.WaitForPreviouslyPostedTasks();
  }

 private:
  TaskQueueForTest task_queue_;
  std::atomic_int queue_size_;
  rtc::Event task_executed_;
};

class TesterY4mWriter {
 public:
  explicit TesterY4mWriter(absl::string_view base_path)
      : base_path_(base_path) {}

  ~TesterY4mWriter() {
    task_queue_.SendTask([] {});
  }

  void Write(const VideoFrame& frame, int spatial_idx) {
    task_queue_.PostTask([this, frame, spatial_idx] {
      if (y4m_writers_.find(spatial_idx) == y4m_writers_.end()) {
        std::string file_path =
            base_path_ + "-s" + std::to_string(spatial_idx) + ".y4m";
        Y4mVideoFrameWriterImpl* y4m_writer = new Y4mVideoFrameWriterImpl(
            file_path, frame.width(), frame.height(), /*fps=*/30);
        RTC_CHECK(y4m_writer);

        y4m_writers_[spatial_idx] =
            std::unique_ptr<VideoFrameWriter>(y4m_writer);
      }

      y4m_writers_.at(spatial_idx)->WriteFrame(frame);
    });
  }

 private:
  std::string base_path_;
  std::map<int, std::unique_ptr<VideoFrameWriter>> y4m_writers_;
  TaskQueueForTest task_queue_;
};

class TesterIvfWriter {
 public:
  explicit TesterIvfWriter(absl::string_view base_path)
      : base_path_(base_path) {}

  ~TesterIvfWriter() {
    task_queue_.SendTask([] {});
  }

  void Write(const EncodedImage& encoded_frame, VideoCodecType codec_type) {
    task_queue_.PostTask([this, encoded_frame, codec_type] {
      int spatial_idx = encoded_frame.SpatialIndex().value_or(
          encoded_frame.SimulcastIndex().value_or(0));
      if (ivf_file_writers_.find(spatial_idx) == ivf_file_writers_.end()) {
        std::string ivf_path =
            base_path_ + "-s" + std::to_string(spatial_idx) + ".ivf";
        FileWrapper ivf_file = FileWrapper::OpenWriteOnly(ivf_path);
        RTC_CHECK(ivf_file.is_open());

        std::unique_ptr<IvfFileWriter> ivf_writer =
            IvfFileWriter::Wrap(std::move(ivf_file), /*byte_limit=*/0);
        RTC_CHECK(ivf_writer);

        ivf_file_writers_[spatial_idx] = std::move(ivf_writer);
      }

      // To play: ffplay -vcodec vp8|vp9|av1|hevc|h264 filename
      ivf_file_writers_.at(spatial_idx)->WriteFrame(encoded_frame, codec_type);
    });
  }

 private:
  std::string base_path_;
  std::map<int, std::unique_ptr<IvfFileWriter>> ivf_file_writers_;
  TaskQueueForTest task_queue_;
};

class LeakyBucket {
 public:
  LeakyBucket() : level_bits_(0) {}

  // Updates bucket level and returns its current level in bits. Data is remove
  // from bucket with rate equal to target bitrate of previous frame. Bucket
  // level is tracked with floating point precision. Returned value of bucket
  // level is rounded up.
  int Update(const VideoCodecStats::Frame& frame) {
    RTC_CHECK(frame.target_bitrate) << "Bitrate must be specified.";
    if (prev_frame_) {
      RTC_CHECK_GT(frame.timestamp_rtp, prev_frame_->timestamp_rtp)
          << "Timestamp must increase.";
      TimeDelta passed =
          (frame.timestamp_rtp - prev_frame_->timestamp_rtp) / k90kHz;
      level_bits_ -=
          prev_frame_->target_bitrate->bps<double>() * passed.seconds<double>();
      level_bits_ = std::max(level_bits_, 0.0);
    }
    prev_frame_ = frame;
    level_bits_ += frame.frame_size.bytes() * 8;
    return static_cast<int>(std::ceil(level_bits_));
  }

 private:
  absl::optional<VideoCodecStats::Frame> prev_frame_;
  double level_bits_;
};

class VideoCodecAnalyzer : public VideoCodecTester::VideoCodecStats {
 public:
  explicit VideoCodecAnalyzer(VideoSource* video_source)
      : video_source_(video_source) {}

  void StartEncode(const VideoFrame& video_frame,
                   const EncodingSettings& encoding_settings) {
    int64_t encode_start_us = rtc::TimeMicros();
    task_queue_.PostTask([this, timestamp_rtp = video_frame.rtp_timestamp(),
                          encoding_settings, encode_start_us]() {
      RTC_CHECK(frames_.find(timestamp_rtp) == frames_.end())
          << "Duplicate frame. Frame with timestamp " << timestamp_rtp
          << " was seen before";

      Frame frame;
      frame.timestamp_rtp = timestamp_rtp;
      frame.encode_start = Timestamp::Micros(encode_start_us),
      frames_.emplace(timestamp_rtp,
                      std::map<int, Frame>{{/*spatial_idx=*/0, frame}});
      encoding_settings_.emplace(timestamp_rtp, encoding_settings);
    });
  }

  void FinishEncode(const EncodedImage& encoded_frame) {
    int64_t encode_finished_us = rtc::TimeMicros();
    task_queue_.PostTask(
        [this, timestamp_rtp = encoded_frame.RtpTimestamp(),
         spatial_idx = encoded_frame.SpatialIndex().value_or(
             encoded_frame.SimulcastIndex().value_or(0)),
         temporal_idx = encoded_frame.TemporalIndex().value_or(0),
         width = encoded_frame._encodedWidth,
         height = encoded_frame._encodedHeight,
         frame_type = encoded_frame._frameType,
         frame_size_bytes = encoded_frame.size(), qp = encoded_frame.qp_,
         encode_finished_us]() {
          if (spatial_idx > 0) {
            RTC_CHECK(frames_.find(timestamp_rtp) != frames_.end())
                << "Spatial layer 0 frame with timestamp " << timestamp_rtp
                << " was not seen before";
            const Frame& base_frame =
                frames_.at(timestamp_rtp).at(/*spatial_idx=*/0);
            frames_.at(timestamp_rtp).emplace(spatial_idx, base_frame);
          }

          Frame& frame = frames_.at(timestamp_rtp).at(spatial_idx);
          frame.layer_id = {.spatial_idx = spatial_idx,
                            .temporal_idx = temporal_idx};
          frame.width = width;
          frame.height = height;
          frame.frame_size = DataSize::Bytes(frame_size_bytes);
          frame.qp = qp;
          frame.keyframe = frame_type == VideoFrameType::kVideoFrameKey;
          frame.encode_time =
              Timestamp::Micros(encode_finished_us) - frame.encode_start;
          frame.encoded = true;
        });
  }

  void StartDecode(const EncodedImage& encoded_frame) {
    int64_t decode_start_us = rtc::TimeMicros();
    task_queue_.PostTask(
        [this, timestamp_rtp = encoded_frame.RtpTimestamp(),
         spatial_idx = encoded_frame.SpatialIndex().value_or(
             encoded_frame.SimulcastIndex().value_or(0)),
         temporal_idx = encoded_frame.TemporalIndex().value_or(0),
         width = encoded_frame._encodedWidth,
         height = encoded_frame._encodedHeight,
         frame_type = encoded_frame._frameType, qp = encoded_frame.qp_,
         frame_size_bytes = encoded_frame.size(), decode_start_us]() {
          bool decode_only = frames_.find(timestamp_rtp) == frames_.end();
          if (decode_only || frames_.at(timestamp_rtp).find(spatial_idx) ==
                                 frames_.at(timestamp_rtp).end()) {
            Frame frame;
            frame.timestamp_rtp = timestamp_rtp;
            frame.layer_id = {.spatial_idx = spatial_idx,
                              .temporal_idx = temporal_idx};
            frame.width = width;
            frame.height = height;
            frame.keyframe = frame_type == VideoFrameType::kVideoFrameKey;
            frame.qp = qp;
            if (decode_only) {
              frame.frame_size = DataSize::Bytes(frame_size_bytes);
              frames_[timestamp_rtp] = {{spatial_idx, frame}};
            } else {
              frames_[timestamp_rtp][spatial_idx] = frame;
            }
          }

          Frame& frame = frames_.at(timestamp_rtp).at(spatial_idx);
          frame.decode_start = Timestamp::Micros(decode_start_us);
        });
  }

  void FinishDecode(const VideoFrame& decoded_frame, int spatial_idx) {
    int64_t decode_finished_us = rtc::TimeMicros();
    task_queue_.PostTask([this, timestamp_rtp = decoded_frame.rtp_timestamp(),
                          spatial_idx, width = decoded_frame.width(),
                          height = decoded_frame.height(),
                          decode_finished_us]() {
      Frame& frame = frames_.at(timestamp_rtp).at(spatial_idx);
      frame.decode_time =
          Timestamp::Micros(decode_finished_us) - frame.decode_start;
      if (!frame.encoded) {
        frame.width = width;
        frame.height = height;
      }
      frame.decoded = true;
    });

    if (video_source_ != nullptr) {
      // Copy hardware-backed frame into main memory to release output buffers
      // which number may be limited in hardware decoders.
      rtc::scoped_refptr<I420BufferInterface> decoded_buffer =
          decoded_frame.video_frame_buffer()->ToI420();

      task_queue_.PostTask([this, decoded_buffer,
                            timestamp_rtp = decoded_frame.rtp_timestamp(),
                            spatial_idx]() {
        VideoFrame ref_frame = video_source_->ReadFrame(
            timestamp_rtp, {.width = decoded_buffer->width(),
                            .height = decoded_buffer->height()});
        rtc::scoped_refptr<I420BufferInterface> ref_buffer =
            ref_frame.video_frame_buffer()->ToI420();
        Frame& frame = frames_.at(timestamp_rtp).at(spatial_idx);
        frame.psnr = CalcPsnr(*decoded_buffer, *ref_buffer);
      });
    }
  }

  std::vector<Frame> Slice(Filter filter, bool merge) const {
    std::vector<Frame> slice;
    for (const auto& [timestamp_rtp, temporal_unit_frames] : frames_) {
      if (temporal_unit_frames.empty()) {
        continue;
      }

      bool is_svc = false;
      if (!encoding_settings_.empty()) {
        ScalabilityMode scalability_mode =
            encoding_settings_.at(timestamp_rtp).scalability_mode;
        if (kFullSvcScalabilityModes.count(scalability_mode) > 0 ||
            (kKeySvcScalabilityModes.count(scalability_mode) > 0 &&
             temporal_unit_frames.at(0).keyframe)) {
          is_svc = true;
        }
      }

      std::vector<Frame> subframes;
      for (const auto& [spatial_idx, frame] : temporal_unit_frames) {
        if (frame.timestamp_rtp < filter.min_timestamp_rtp ||
            frame.timestamp_rtp > filter.max_timestamp_rtp) {
          continue;
        }
        if (filter.layer_id) {
          if (is_svc &&
              frame.layer_id.spatial_idx > filter.layer_id->spatial_idx) {
            continue;
          }
          if (!is_svc &&
              frame.layer_id.spatial_idx != filter.layer_id->spatial_idx) {
            continue;
          }
          if (frame.layer_id.temporal_idx > filter.layer_id->temporal_idx) {
            continue;
          }
        }
        subframes.push_back(frame);
      }

      if (subframes.empty()) {
        continue;
      }

      if (!merge) {
        std::copy(subframes.begin(), subframes.end(),
                  std::back_inserter(slice));
        continue;
      }

      Frame superframe = subframes.back();
      for (const Frame& frame :
           rtc::ArrayView<Frame>(subframes).subview(0, subframes.size() - 1)) {
        superframe.decoded |= frame.decoded;
        superframe.encoded |= frame.encoded;
        superframe.frame_size += frame.frame_size;
        superframe.keyframe |= frame.keyframe;
        superframe.encode_time =
            std::max(superframe.encode_time, frame.encode_time);
        superframe.decode_time =
            std::max(superframe.decode_time, frame.decode_time);
      }

      if (!encoding_settings_.empty()) {
        RTC_CHECK(encoding_settings_.find(superframe.timestamp_rtp) !=
                  encoding_settings_.end())
            << "No encoding settings for frame " << superframe.timestamp_rtp;
        const EncodingSettings& es =
            encoding_settings_.at(superframe.timestamp_rtp);
        superframe.target_bitrate = GetTargetBitrate(es, filter.layer_id);
        superframe.target_framerate = GetTargetFramerate(es, filter.layer_id);
      }

      slice.push_back(superframe);
    }
    return slice;
  }

  Stream Aggregate(Filter filter) const {
    std::vector<Frame> frames = Slice(filter, /*merge=*/true);
    Stream stream;
    LeakyBucket leaky_bucket;
    for (const Frame& frame : frames) {
      Timestamp time = Timestamp::Micros((frame.timestamp_rtp / k90kHz).us());
      if (!frame.frame_size.IsZero()) {
        stream.width.AddSample(StatsSample(frame.width, time));
        stream.height.AddSample(StatsSample(frame.height, time));
        stream.frame_size_bytes.AddSample(
            StatsSample(frame.frame_size.bytes(), time));
        stream.keyframe.AddSample(StatsSample(frame.keyframe, time));
        if (frame.qp) {
          stream.qp.AddSample(StatsSample(*frame.qp, time));
        }
      }
      if (frame.encoded) {
        stream.encode_time_ms.AddSample(
            StatsSample(frame.encode_time.ms(), time));
      }
      if (frame.decoded) {
        stream.decode_time_ms.AddSample(
            StatsSample(frame.decode_time.ms(), time));
      }
      if (frame.psnr) {
        stream.psnr.y.AddSample(StatsSample(frame.psnr->y, time));
        stream.psnr.u.AddSample(StatsSample(frame.psnr->u, time));
        stream.psnr.v.AddSample(StatsSample(frame.psnr->v, time));
      }
      if (frame.target_framerate) {
        stream.target_framerate_fps.AddSample(
            StatsSample(frame.target_framerate->hertz<double>(), time));
      }
      if (frame.target_bitrate) {
        stream.target_bitrate_kbps.AddSample(
            StatsSample(frame.target_bitrate->kbps<double>(), time));
        int buffer_level_bits = leaky_bucket.Update(frame);
        stream.transmission_time_ms.AddSample(StatsSample(
            1000 * buffer_level_bits / frame.target_bitrate->bps<double>(),
            time));
      }
    }

    int num_encoded_frames = stream.frame_size_bytes.NumSamples();
    const Frame& first_frame = frames.front();

    Filter filter_all_layers{.min_timestamp_rtp = filter.min_timestamp_rtp,
                             .max_timestamp_rtp = filter.max_timestamp_rtp};
    std::vector<Frame> frames_all_layers =
        Slice(filter_all_layers, /*merge=*/true);
    const Frame& last_frame = frames_all_layers.back();
    TimeDelta duration =
        (last_frame.timestamp_rtp - first_frame.timestamp_rtp) / k90kHz;
    if (last_frame.target_framerate) {
      duration += 1 / *last_frame.target_framerate;
    }

    DataRate encoded_bitrate =
        DataSize::Bytes(stream.frame_size_bytes.GetSum()) / duration;
    Frequency encoded_framerate = num_encoded_frames / duration;

    double bitrate_mismatch_pct = 0.0;
    if (const auto& target_bitrate = first_frame.target_bitrate;
        target_bitrate) {
      bitrate_mismatch_pct = 100 * (encoded_bitrate / *target_bitrate - 1);
    }
    double framerate_mismatch_pct = 0.0;
    if (const auto& target_framerate = first_frame.target_framerate;
        target_framerate) {
      framerate_mismatch_pct =
          100 * (encoded_framerate / *target_framerate - 1);
    }

    for (Frame& frame : frames) {
      Timestamp time = Timestamp::Micros((frame.timestamp_rtp / k90kHz).us());
      stream.encoded_bitrate_kbps.AddSample(
          StatsSample(encoded_bitrate.kbps<double>(), time));
      stream.encoded_framerate_fps.AddSample(
          StatsSample(encoded_framerate.hertz<double>(), time));
      stream.bitrate_mismatch_pct.AddSample(
          StatsSample(bitrate_mismatch_pct, time));
      stream.framerate_mismatch_pct.AddSample(
          StatsSample(framerate_mismatch_pct, time));
    }

    return stream;
  }

  void LogMetrics(absl::string_view csv_path,
                  std::vector<Frame> frames,
                  std::map<std::string, std::string> metadata) const {
    RTC_LOG(LS_INFO) << "Write metrics to " << csv_path;
    FILE* csv_file = fopen(csv_path.data(), "w");
    const std::string delimiter = ";";
    rtc::StringBuilder header;
    header
        << "timestamp_rtp;spatial_idx;temporal_idx;width;height;frame_size_"
           "bytes;keyframe;qp;encode_time_us;decode_time_us;psnr_y_db;psnr_u_"
           "db;psnr_v_db;target_bitrate_kbps;target_framerate_fps";
    for (const auto& data : metadata) {
      header << ";" << data.first;
    }
    fwrite(header.str().c_str(), 1, header.size(), csv_file);

    for (const Frame& f : frames) {
      rtc::StringBuilder row;
      row << "\n" << f.timestamp_rtp;
      row << ";" << f.layer_id.spatial_idx;
      row << ";" << f.layer_id.temporal_idx;
      row << ";" << f.width;
      row << ";" << f.height;
      row << ";" << f.frame_size.bytes();
      row << ";" << f.keyframe;
      row << ";";
      if (f.qp) {
        row << *f.qp;
      }
      row << ";" << f.encode_time.us();
      row << ";" << f.decode_time.us();
      if (f.psnr) {
        row << ";" << f.psnr->y;
        row << ";" << f.psnr->u;
        row << ";" << f.psnr->v;
      } else {
        row << ";;;";
      }

      const auto& es = encoding_settings_.at(f.timestamp_rtp);
      row << ";"
          << f.target_bitrate.value_or(GetTargetBitrate(es, f.layer_id)).kbps();
      row << ";"
          << f.target_framerate.value_or(GetTargetFramerate(es, f.layer_id))
                 .hertz<double>();

      for (const auto& data : metadata) {
        row << ";" << data.second;
      }
      fwrite(row.str().c_str(), 1, row.size(), csv_file);
    }

    fclose(csv_file);
  }

  void Flush() { task_queue_.WaitForPreviouslyPostedTasks(); }

 private:
  struct FrameId {
    uint32_t timestamp_rtp;
    int spatial_idx;

    bool operator==(const FrameId& o) const {
      return timestamp_rtp == o.timestamp_rtp && spatial_idx == o.spatial_idx;
    }
    bool operator<(const FrameId& o) const {
      return timestamp_rtp < o.timestamp_rtp ||
             (timestamp_rtp == o.timestamp_rtp && spatial_idx < o.spatial_idx);
    }
  };

  Frame::Psnr CalcPsnr(const I420BufferInterface& ref_buffer,
                       const I420BufferInterface& dec_buffer) {
    RTC_CHECK_EQ(ref_buffer.width(), dec_buffer.width());
    RTC_CHECK_EQ(ref_buffer.height(), dec_buffer.height());

    uint64_t sse_y = libyuv::ComputeSumSquareErrorPlane(
        dec_buffer.DataY(), dec_buffer.StrideY(), ref_buffer.DataY(),
        ref_buffer.StrideY(), dec_buffer.width(), dec_buffer.height());

    uint64_t sse_u = libyuv::ComputeSumSquareErrorPlane(
        dec_buffer.DataU(), dec_buffer.StrideU(), ref_buffer.DataU(),
        ref_buffer.StrideU(), dec_buffer.width() / 2, dec_buffer.height() / 2);

    uint64_t sse_v = libyuv::ComputeSumSquareErrorPlane(
        dec_buffer.DataV(), dec_buffer.StrideV(), ref_buffer.DataV(),
        ref_buffer.StrideV(), dec_buffer.width() / 2, dec_buffer.height() / 2);

    int num_y_samples = dec_buffer.width() * dec_buffer.height();
    Frame::Psnr psnr;
    psnr.y = libyuv::SumSquareErrorToPsnr(sse_y, num_y_samples);
    psnr.u = libyuv::SumSquareErrorToPsnr(sse_u, num_y_samples / 4);
    psnr.v = libyuv::SumSquareErrorToPsnr(sse_v, num_y_samples / 4);
    return psnr;
  }

  DataRate GetTargetBitrate(const EncodingSettings& encoding_settings,
                            absl::optional<LayerId> layer_id) const {
    int base_spatial_idx;
    if (layer_id.has_value()) {
      bool is_svc =
          kFullSvcScalabilityModes.count(encoding_settings.scalability_mode);
      base_spatial_idx = is_svc ? 0 : layer_id->spatial_idx;
    } else {
      int num_spatial_layers =
          ScalabilityModeToNumSpatialLayers(encoding_settings.scalability_mode);
      int num_temporal_layers = ScalabilityModeToNumTemporalLayers(
          encoding_settings.scalability_mode);
      layer_id = LayerId({.spatial_idx = num_spatial_layers - 1,
                          .temporal_idx = num_temporal_layers - 1});
      base_spatial_idx = 0;
    }

    DataRate bitrate = DataRate::Zero();
    for (int sidx = base_spatial_idx; sidx <= layer_id->spatial_idx; ++sidx) {
      for (int tidx = 0; tidx <= layer_id->temporal_idx; ++tidx) {
        auto layer_settings = encoding_settings.layers_settings.find(
            {.spatial_idx = sidx, .temporal_idx = tidx});
        RTC_CHECK(layer_settings != encoding_settings.layers_settings.end())
            << "bitrate is not specified for layer sidx=" << sidx
            << " tidx=" << tidx;
        bitrate += layer_settings->second.bitrate;
      }
    }
    return bitrate;
  }

  Frequency GetTargetFramerate(const EncodingSettings& encoding_settings,
                               absl::optional<LayerId> layer_id) const {
    if (layer_id.has_value()) {
      auto layer_settings = encoding_settings.layers_settings.find(
          {.spatial_idx = layer_id->spatial_idx,
           .temporal_idx = layer_id->temporal_idx});
      RTC_CHECK(layer_settings != encoding_settings.layers_settings.end())
          << "framerate is not specified for layer sidx="
          << layer_id->spatial_idx << " tidx=" << layer_id->temporal_idx;
      return layer_settings->second.framerate;
    }
    return encoding_settings.layers_settings.rbegin()->second.framerate;
  }

  SamplesStatsCounter::StatsSample StatsSample(double value,
                                               Timestamp time) const {
    return SamplesStatsCounter::StatsSample{value, time};
  }

  VideoSource* const video_source_;
  TaskQueueForTest task_queue_;
  // RTP timestamp -> spatial layer -> Frame
  std::map<uint32_t, std::map<int, Frame>> frames_;
  std::map<uint32_t, EncodingSettings> encoding_settings_;
};

class Decoder : public DecodedImageCallback {
 public:
  Decoder(const Environment& env,
          VideoDecoderFactory* decoder_factory,
          const DecoderSettings& decoder_settings,
          VideoCodecAnalyzer* analyzer)
      : env_(env),
        decoder_factory_(decoder_factory),
        analyzer_(analyzer),
        pacer_(decoder_settings.pacing_settings) {
    RTC_CHECK(analyzer_) << "Analyzer must be provided";

    if (decoder_settings.decoder_input_base_path) {
      ivf_writer_ = std::make_unique<TesterIvfWriter>(
          *decoder_settings.decoder_input_base_path);
    }

    if (decoder_settings.decoder_output_base_path) {
      y4m_writer_ = std::make_unique<TesterY4mWriter>(
          *decoder_settings.decoder_output_base_path);
    }
  }

  void Initialize(const SdpVideoFormat& sdp_video_format) {
    decoder_ = decoder_factory_->Create(env_, sdp_video_format);
    RTC_CHECK(decoder_) << "Could not create decoder for video format "
                        << sdp_video_format.ToString();

    codec_type_ = PayloadStringToCodecType(sdp_video_format.name);

    task_queue_.PostTaskAndWait([this] {
      decoder_->RegisterDecodeCompleteCallback(this);

      VideoDecoder::Settings ds;
      ds.set_codec_type(*codec_type_);
      ds.set_number_of_cores(1);
      ds.set_max_render_resolution({1280, 720});
      bool result = decoder_->Configure(ds);
      RTC_CHECK(result) << "Failed to configure decoder";
    });
  }

  void Decode(const EncodedImage& encoded_frame) {
    int spatial_idx = encoded_frame.SpatialIndex().value_or(
        encoded_frame.SimulcastIndex().value_or(0));
    {
      MutexLock lock(&mutex_);
      RTC_CHECK_EQ(spatial_idx_.value_or(spatial_idx), spatial_idx)
          << "Spatial index changed from " << *spatial_idx_ << " to "
          << spatial_idx;
      spatial_idx_ = spatial_idx;
    }

    Timestamp pts =
        Timestamp::Micros((encoded_frame.RtpTimestamp() / k90kHz).us());

    task_queue_.PostScheduledTask(
        [this, encoded_frame] {
          analyzer_->StartDecode(encoded_frame);
          int error = decoder_->Decode(encoded_frame, /*render_time_ms*/ 0);
          if (error != 0) {
            RTC_LOG(LS_WARNING)
                << "Decode failed with error code " << error
                << " RTP timestamp " << encoded_frame.RtpTimestamp();
          }
        },
        pacer_.Schedule(pts));

    if (ivf_writer_) {
      ivf_writer_->Write(encoded_frame, *codec_type_);
    }
  }

  void Flush() {
    // TODO(webrtc:14852): Add Flush() to VideoDecoder API.
    task_queue_.PostTaskAndWait([this] { decoder_->Release(); });
  }

 private:
  int Decoded(VideoFrame& decoded_frame) override {
    int spatial_idx;
    {
      MutexLock lock(&mutex_);
      spatial_idx = *spatial_idx_;
    }

    analyzer_->FinishDecode(decoded_frame, spatial_idx);

    if (y4m_writer_) {
      y4m_writer_->Write(decoded_frame, spatial_idx);
    }

    return WEBRTC_VIDEO_CODEC_OK;
  }

  const Environment env_;
  VideoDecoderFactory* decoder_factory_;
  std::unique_ptr<VideoDecoder> decoder_;
  VideoCodecAnalyzer* const analyzer_;
  Pacer pacer_;
  LimitedTaskQueue task_queue_;
  std::unique_ptr<TesterIvfWriter> ivf_writer_;
  std::unique_ptr<TesterY4mWriter> y4m_writer_;
  absl::optional<VideoCodecType> codec_type_;
  absl::optional<int> spatial_idx_ RTC_GUARDED_BY(mutex_);
  Mutex mutex_;
};

class Encoder : public EncodedImageCallback {
 public:
  using EncodeCallback =
      absl::AnyInvocable<void(const EncodedImage& encoded_frame)>;

  Encoder(const Environment& env,
          VideoEncoderFactory* encoder_factory,
          const EncoderSettings& encoder_settings,
          VideoCodecAnalyzer* analyzer)
      : env_(env),
        encoder_factory_(encoder_factory),
        analyzer_(analyzer),
        pacer_(encoder_settings.pacing_settings) {
    RTC_CHECK(analyzer_) << "Analyzer must be provided";

    if (encoder_settings.encoder_input_base_path) {
      y4m_writer_ = std::make_unique<TesterY4mWriter>(
          *encoder_settings.encoder_input_base_path);
    }

    if (encoder_settings.encoder_output_base_path) {
      ivf_writer_ = std::make_unique<TesterIvfWriter>(
          *encoder_settings.encoder_output_base_path);
    }
  }

  void Initialize(const EncodingSettings& encoding_settings) {
    encoder_ =
        encoder_factory_->Create(env_, encoding_settings.sdp_video_format);
    RTC_CHECK(encoder_) << "Could not create encoder for video format "
                        << encoding_settings.sdp_video_format.ToString();

    codec_type_ =
        PayloadStringToCodecType(encoding_settings.sdp_video_format.name);

    task_queue_.PostTaskAndWait([this, encoding_settings] {
      encoder_->RegisterEncodeCompleteCallback(this);
      Configure(encoding_settings);
      SetRates(encoding_settings);
    });
  }

  void Encode(const VideoFrame& input_frame,
              const EncodingSettings& encoding_settings,
              EncodeCallback callback) {
    {
      MutexLock lock(&mutex_);
      callbacks_[input_frame.rtp_timestamp()] = std::move(callback);
    }

    Timestamp pts =
        Timestamp::Micros((input_frame.rtp_timestamp() / k90kHz).us());

    task_queue_.PostScheduledTask(
        [this, input_frame, encoding_settings] {
          analyzer_->StartEncode(input_frame, encoding_settings);

          if (!last_encoding_settings_ ||
              !IsSameRate(encoding_settings, *last_encoding_settings_)) {
            SetRates(encoding_settings);
          }
          last_encoding_settings_ = encoding_settings;

          int error = encoder_->Encode(input_frame, /*frame_types=*/nullptr);
          if (error != 0) {
            RTC_LOG(LS_WARNING)
                << "Encode failed with error code " << error
                << " RTP timestamp " << input_frame.rtp_timestamp();
          }
        },
        pacer_.Schedule(pts));

    if (y4m_writer_) {
      y4m_writer_->Write(input_frame, /*spatial_idx=*/0);
    }
  }

  void Flush() {
    task_queue_.PostTaskAndWait([this] { encoder_->Release(); });
    if (last_superframe_) {
      int num_spatial_layers =
          ScalabilityModeToNumSpatialLayers(last_superframe_->scalability_mode);
      for (int sidx = *last_superframe_->encoded_frame.SpatialIndex() + 1;
           sidx < num_spatial_layers; ++sidx) {
        last_superframe_->encoded_frame.SetSpatialIndex(sidx);
        DeliverEncodedFrame(last_superframe_->encoded_frame);
      }
      last_superframe_.reset();
    }
  }

 private:
  struct Superframe {
    EncodedImage encoded_frame;
    rtc::scoped_refptr<EncodedImageBuffer> encoded_data;
    ScalabilityMode scalability_mode;
  };

  Result OnEncodedImage(const EncodedImage& encoded_frame,
                        const CodecSpecificInfo* codec_specific_info) override {
    analyzer_->FinishEncode(encoded_frame);

    if (last_superframe_ && last_superframe_->encoded_frame.RtpTimestamp() !=
                                encoded_frame.RtpTimestamp()) {
      // New temporal unit. We have frame of previous temporal unit (TU) stored
      // which means that the previous TU used spatial prediction. If encoder
      // dropped a frame of layer X in the previous TU, mark the stored frame
      // as a frame belonging to layer >X and deliver it such that decoders of
      // layer >X receive encoded lower layers.
      int num_spatial_layers =
          ScalabilityModeToNumSpatialLayers(last_superframe_->scalability_mode);
      for (int sidx =
               last_superframe_->encoded_frame.SpatialIndex().value_or(0) + 1;
           sidx < num_spatial_layers; ++sidx) {
        last_superframe_->encoded_frame.SetSpatialIndex(sidx);
        DeliverEncodedFrame(last_superframe_->encoded_frame);
      }
      last_superframe_.reset();
    }

    const EncodedImage& superframe =
        MakeSuperFrame(encoded_frame, codec_specific_info);
    DeliverEncodedFrame(superframe);

    return Result(Result::Error::OK);
  }

  void DeliverEncodedFrame(const EncodedImage& encoded_frame) {
    {
      MutexLock lock(&mutex_);
      auto it = callbacks_.find(encoded_frame.RtpTimestamp());
      RTC_CHECK(it != callbacks_.end());
      it->second(encoded_frame);
      callbacks_.erase(callbacks_.begin(), it);
    }

    if (ivf_writer_ != nullptr) {
      ivf_writer_->Write(encoded_frame, codec_type_);
    }
  }

  void Configure(const EncodingSettings& es) {
    const LayerSettings& top_layer_settings =
        es.layers_settings.rbegin()->second;
    const int num_spatial_layers =
        ScalabilityModeToNumSpatialLayers(es.scalability_mode);
    const int num_temporal_layers =
        ScalabilityModeToNumTemporalLayers(es.scalability_mode);
    DataRate total_bitrate = std::accumulate(
        es.layers_settings.begin(), es.layers_settings.end(), DataRate::Zero(),
        [](DataRate acc, const std::pair<const LayerId, LayerSettings> layer) {
          return acc + layer.second.bitrate;
        });

    VideoCodec vc;
    vc.width = top_layer_settings.resolution.width;
    vc.height = top_layer_settings.resolution.height;
    vc.startBitrate = total_bitrate.kbps();
    vc.maxBitrate = total_bitrate.kbps();
    vc.minBitrate = 0;
    vc.maxFramerate = top_layer_settings.framerate.hertz<uint32_t>();
    vc.active = true;
    vc.numberOfSimulcastStreams = 0;
    vc.mode = webrtc::VideoCodecMode::kRealtimeVideo;
    vc.SetFrameDropEnabled(true);
    vc.SetScalabilityMode(es.scalability_mode);
    vc.SetVideoEncoderComplexity(VideoCodecComplexity::kComplexityNormal);

    vc.codecType = PayloadStringToCodecType(es.sdp_video_format.name);
    switch (vc.codecType) {
      case kVideoCodecVP8:
        *(vc.VP8()) = VideoEncoder::GetDefaultVp8Settings();
        vc.VP8()->SetNumberOfTemporalLayers(num_temporal_layers);
        vc.SetScalabilityMode(std::vector<ScalabilityMode>{
            ScalabilityMode::kL1T1, ScalabilityMode::kL1T2,
            ScalabilityMode::kL1T3}[num_temporal_layers - 1]);
        vc.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;
      case kVideoCodecVP9:
        *(vc.VP9()) = VideoEncoder::GetDefaultVp9Settings();
        // See LibvpxVp9Encoder::ExplicitlyConfiguredSpatialLayers.
        vc.spatialLayers[0].targetBitrate = vc.maxBitrate;
        vc.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;
      case kVideoCodecAV1:
        vc.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;
      case kVideoCodecH264:
        *(vc.H264()) = VideoEncoder::GetDefaultH264Settings();
        vc.H264()->SetNumberOfTemporalLayers(num_temporal_layers);
        vc.qpMax = cricket::kDefaultVideoMaxQpH26x;
        break;
      case kVideoCodecH265:
        vc.qpMax = cricket::kDefaultVideoMaxQpH26x;
        break;
      case kVideoCodecGeneric:
        RTC_CHECK_NOTREACHED();
        break;
    }

    bool is_simulcast =
        num_spatial_layers > 1 &&
        (vc.codecType == kVideoCodecVP8 || vc.codecType == kVideoCodecH264 ||
         vc.codecType == kVideoCodecH265);
    if (is_simulcast) {
      vc.numberOfSimulcastStreams = num_spatial_layers;
      for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
        auto tl0_settings = es.layers_settings.find(
            LayerId{.spatial_idx = sidx, .temporal_idx = 0});
        auto tlx_settings = es.layers_settings.find(LayerId{
            .spatial_idx = sidx, .temporal_idx = num_temporal_layers - 1});
        DataRate total_bitrate = std::accumulate(
            tl0_settings, tlx_settings, DataRate::Zero(),
            [](DataRate acc,
               const std::pair<const LayerId, LayerSettings> layer) {
              return acc + layer.second.bitrate;
            });
        SimulcastStream& ss = vc.simulcastStream[sidx];
        ss.width = tl0_settings->second.resolution.width;
        ss.height = tl0_settings->second.resolution.height;
        ss.numberOfTemporalLayers = num_temporal_layers;
        ss.maxBitrate = total_bitrate.kbps();
        ss.targetBitrate = total_bitrate.kbps();
        ss.minBitrate = 0;
        ss.maxFramerate = vc.maxFramerate;
        ss.qpMax = vc.qpMax;
        ss.active = true;
      }
    }

    VideoEncoder::Settings ves(
        VideoEncoder::Capabilities(/*loss_notification=*/false),
        /*number_of_cores=*/1,
        /*max_payload_size=*/1440);

    int result = encoder_->InitEncode(&vc, ves);
    RTC_CHECK(result == WEBRTC_VIDEO_CODEC_OK);
  }

  void SetRates(const EncodingSettings& es) {
    VideoEncoder::RateControlParameters rc;
    int num_spatial_layers =
        ScalabilityModeToNumSpatialLayers(es.scalability_mode);
    int num_temporal_layers =
        ScalabilityModeToNumTemporalLayers(es.scalability_mode);
    for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
      for (int tidx = 0; tidx < num_temporal_layers; ++tidx) {
        auto layers_settings = es.layers_settings.find(
            {.spatial_idx = sidx, .temporal_idx = tidx});
        RTC_CHECK(layers_settings != es.layers_settings.end())
            << "Bitrate for layer S=" << sidx << " T=" << tidx << " is not set";
        rc.bitrate.SetBitrate(sidx, tidx,
                              layers_settings->second.bitrate.bps());
      }
    }
    rc.framerate_fps =
        es.layers_settings.rbegin()->second.framerate.hertz<double>();
    encoder_->SetRates(rc);
  }

  bool IsSameRate(const EncodingSettings& a, const EncodingSettings& b) const {
    for (auto [layer_id, layer] : a.layers_settings) {
      const auto& other_layer = b.layers_settings.at(layer_id);
      if (layer.bitrate != other_layer.bitrate ||
          layer.framerate != other_layer.framerate) {
        return false;
      }
    }

    return true;
  }

  static bool IsSvc(const EncodedImage& encoded_frame,
                    const CodecSpecificInfo& codec_specific_info) {
    if (!codec_specific_info.scalability_mode) {
      return false;
    }
    ScalabilityMode scalability_mode = *codec_specific_info.scalability_mode;
    return (kFullSvcScalabilityModes.count(scalability_mode) ||
            (kKeySvcScalabilityModes.count(scalability_mode) &&
             encoded_frame.FrameType() == VideoFrameType::kVideoFrameKey));
  }

  const EncodedImage& MakeSuperFrame(
      const EncodedImage& encoded_frame,
      const CodecSpecificInfo* codec_specific_info) {
    if (last_superframe_) {
      // Append to base spatial layer frame(s).
      RTC_CHECK_EQ(*encoded_frame.SpatialIndex(),
                   *last_superframe_->encoded_frame.SpatialIndex() + 1)
          << "Inter-layer frame drops are not supported.";
      size_t current_size = last_superframe_->encoded_data->size();
      last_superframe_->encoded_data->Realloc(current_size +
                                              encoded_frame.size());
      memcpy(last_superframe_->encoded_data->data() + current_size,
             encoded_frame.data(), encoded_frame.size());
      last_superframe_->encoded_frame.SetEncodedData(
          last_superframe_->encoded_data);
      last_superframe_->encoded_frame.SetSpatialIndex(
          encoded_frame.SpatialIndex());
      return last_superframe_->encoded_frame;
    }

    RTC_CHECK(codec_specific_info != nullptr);
    if (IsSvc(encoded_frame, *codec_specific_info)) {
      last_superframe_ = Superframe{
          .encoded_frame = EncodedImage(encoded_frame),
          .encoded_data = EncodedImageBuffer::Create(encoded_frame.data(),
                                                     encoded_frame.size()),
          .scalability_mode = *codec_specific_info->scalability_mode};
      last_superframe_->encoded_frame.SetEncodedData(
          last_superframe_->encoded_data);
      return last_superframe_->encoded_frame;
    }

    return encoded_frame;
  }

  const Environment env_;
  VideoEncoderFactory* const encoder_factory_;
  std::unique_ptr<VideoEncoder> encoder_;
  VideoCodecAnalyzer* const analyzer_;
  Pacer pacer_;
  absl::optional<EncodingSettings> last_encoding_settings_;
  std::unique_ptr<VideoBitrateAllocator> bitrate_allocator_;
  LimitedTaskQueue task_queue_;
  std::unique_ptr<TesterY4mWriter> y4m_writer_;
  std::unique_ptr<TesterIvfWriter> ivf_writer_;
  std::map<uint32_t, int> sidx_ RTC_GUARDED_BY(mutex_);
  std::map<uint32_t, EncodeCallback> callbacks_ RTC_GUARDED_BY(mutex_);
  VideoCodecType codec_type_;
  absl::optional<Superframe> last_superframe_;
  Mutex mutex_;
};

void ConfigureSimulcast(VideoCodec* vc) {
  int num_spatial_layers =
      ScalabilityModeToNumSpatialLayers(*vc->GetScalabilityMode());
  int num_temporal_layers =
      ScalabilityModeToNumTemporalLayers(*vc->GetScalabilityMode());

  if (num_spatial_layers == 1) {
    SimulcastStream* ss = &vc->simulcastStream[0];
    ss->width = vc->width;
    ss->height = vc->height;
    ss->numberOfTemporalLayers = num_temporal_layers;
    ss->maxBitrate = vc->maxBitrate;
    ss->targetBitrate = vc->maxBitrate;
    ss->minBitrate = vc->minBitrate;
    ss->qpMax = vc->qpMax;
    ss->active = true;
    return;
  }

  ScopedKeyValueConfig field_trials((rtc::StringBuilder()
                                     << "WebRTC-VP8ConferenceTemporalLayers/"
                                     << num_temporal_layers << "/")
                                        .str());

  const std::vector<webrtc::VideoStream> streams = cricket::GetSimulcastConfig(
      /*min_layer=*/1, num_spatial_layers, vc->width, vc->height,
      /*bitrate_priority=*/1.0, cricket::kDefaultVideoMaxQpVpx,
      /*is_screenshare=*/false, /*temporal_layers_supported=*/true,
      field_trials, webrtc::kVideoCodecVP8);

  vc->numberOfSimulcastStreams = streams.size();
  RTC_CHECK_LE(vc->numberOfSimulcastStreams, num_spatial_layers);
  if (vc->numberOfSimulcastStreams < num_spatial_layers) {
    vc->SetScalabilityMode(LimitNumSpatialLayers(*vc->GetScalabilityMode(),
                                                 vc->numberOfSimulcastStreams));
  }

  for (int i = 0; i < vc->numberOfSimulcastStreams; ++i) {
    SimulcastStream* ss = &vc->simulcastStream[i];
    ss->width = streams[i].width;
    ss->height = streams[i].height;
    RTC_CHECK_EQ(*streams[i].num_temporal_layers, num_temporal_layers);
    ss->numberOfTemporalLayers = *streams[i].num_temporal_layers;
    ss->maxBitrate = streams[i].max_bitrate_bps / 1000;
    ss->targetBitrate = streams[i].target_bitrate_bps / 1000;
    ss->minBitrate = streams[i].min_bitrate_bps / 1000;
    ss->qpMax = streams[i].max_qp;
    ss->active = true;
  }
}

void SetDefaultCodecSpecificSettings(VideoCodec* vc, int num_temporal_layers) {
  switch (vc->codecType) {
    case kVideoCodecVP8:
      *(vc->VP8()) = VideoEncoder::GetDefaultVp8Settings();
      vc->VP8()->SetNumberOfTemporalLayers(num_temporal_layers);
      break;
    case kVideoCodecVP9: {
      *(vc->VP9()) = VideoEncoder::GetDefaultVp9Settings();
      vc->VP9()->SetNumberOfTemporalLayers(num_temporal_layers);
    } break;
    case kVideoCodecH264: {
      *(vc->H264()) = VideoEncoder::GetDefaultH264Settings();
      vc->H264()->SetNumberOfTemporalLayers(num_temporal_layers);
    } break;
    case kVideoCodecAV1:
    case kVideoCodecH265:
      break;
    case kVideoCodecGeneric:
      RTC_CHECK_NOTREACHED();
  }
}

std::tuple<std::vector<DataRate>, ScalabilityMode>
SplitBitrateAndUpdateScalabilityMode(std::string codec_type,
                                     ScalabilityMode scalability_mode,
                                     int width,
                                     int height,
                                     std::vector<int> bitrates_kbps,
                                     double framerate_fps) {
  int num_spatial_layers = ScalabilityModeToNumSpatialLayers(scalability_mode);
  int num_temporal_layers =
      ScalabilityModeToNumTemporalLayers(scalability_mode);

  int num_bitrates = static_cast<int>(bitrates_kbps.size());
  RTC_CHECK(num_bitrates == 1 || num_bitrates == num_spatial_layers ||
            num_bitrates == num_spatial_layers * num_temporal_layers);

  if (num_bitrates == num_spatial_layers * num_temporal_layers) {
    std::vector<DataRate> bitrates;
    for (const auto& bitrate_kbps : bitrates_kbps) {
      bitrates.push_back(DataRate::KilobitsPerSec(bitrate_kbps));
    }
    return std::make_tuple(bitrates, scalability_mode);
  }

  int total_bitrate_kbps =
      std::accumulate(bitrates_kbps.begin(), bitrates_kbps.end(), 0);

  VideoCodec vc;
  vc.codecType = PayloadStringToCodecType(codec_type);
  vc.width = width;
  vc.height = height;
  vc.startBitrate = total_bitrate_kbps;
  vc.maxBitrate = total_bitrate_kbps;
  vc.minBitrate = 0;
  vc.maxFramerate = static_cast<uint32_t>(framerate_fps);
  vc.numberOfSimulcastStreams = 0;
  vc.mode = webrtc::VideoCodecMode::kRealtimeVideo;
  vc.SetScalabilityMode(scalability_mode);
  SetDefaultCodecSpecificSettings(&vc, num_temporal_layers);

  if (num_bitrates == num_spatial_layers) {
    switch (vc.codecType) {
      case kVideoCodecVP8:
      case kVideoCodecH264:
      case kVideoCodecH265:
        vc.numberOfSimulcastStreams = num_spatial_layers;
        for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
          SimulcastStream* ss = &vc.simulcastStream[sidx];
          ss->width = width >> (num_spatial_layers - sidx - 1);
          ss->height = height >> (num_spatial_layers - sidx - 1);
          ss->maxFramerate = vc.maxFramerate;
          ss->numberOfTemporalLayers = num_temporal_layers;
          ss->maxBitrate = bitrates_kbps[sidx];
          ss->targetBitrate = bitrates_kbps[sidx];
          ss->minBitrate = 0;
          ss->qpMax = 0;
          ss->active = true;
        }
        break;
      case kVideoCodecVP9:
      case kVideoCodecAV1:
        for (int sidx = num_spatial_layers - 1; sidx >= 0; --sidx) {
          SpatialLayer* ss = &vc.spatialLayers[sidx];
          ss->width = width >> (num_spatial_layers - sidx - 1);
          ss->height = height >> (num_spatial_layers - sidx - 1);
          ss->maxFramerate = vc.maxFramerate;
          ss->numberOfTemporalLayers = num_temporal_layers;
          ss->maxBitrate = bitrates_kbps[sidx];
          ss->targetBitrate = bitrates_kbps[sidx];
          ss->minBitrate = 0;
          ss->qpMax = 0;
          ss->active = true;
        }
        break;
      case kVideoCodecGeneric:
        RTC_CHECK_NOTREACHED();
    }
  } else {
    switch (vc.codecType) {
      case kVideoCodecVP8:
      case kVideoCodecH264:
      case kVideoCodecH265:
        ConfigureSimulcast(&vc);
        break;
      case kVideoCodecVP9: {
        const std::vector<SpatialLayer> spatialLayers = GetVp9SvcConfig(vc);
        for (size_t i = 0; i < spatialLayers.size(); ++i) {
          vc.spatialLayers[i] = spatialLayers[i];
          vc.spatialLayers[i].active = true;
        }
      } break;
      case kVideoCodecAV1: {
        bool result =
            SetAv1SvcConfig(vc, num_spatial_layers, num_temporal_layers);
        RTC_CHECK(result) << "SetAv1SvcConfig failed";
      } break;
      case kVideoCodecGeneric:
        RTC_CHECK_NOTREACHED();
    }

    if (*vc.GetScalabilityMode() != scalability_mode) {
      RTC_LOG(LS_WARNING) << "Scalability mode changed from "
                          << ScalabilityModeToString(scalability_mode) << " to "
                          << ScalabilityModeToString(*vc.GetScalabilityMode());
      num_spatial_layers =
          ScalabilityModeToNumSpatialLayers(*vc.GetScalabilityMode());
      num_temporal_layers =
          ScalabilityModeToNumTemporalLayers(*vc.GetScalabilityMode());
    }
  }

  std::unique_ptr<VideoBitrateAllocator> bitrate_allocator =
      CreateBuiltinVideoBitrateAllocatorFactory()->CreateVideoBitrateAllocator(
          vc);
  VideoBitrateAllocation bitrate_allocation =
      bitrate_allocator->Allocate(VideoBitrateAllocationParameters(
          1000 * total_bitrate_kbps, framerate_fps));

  std::vector<DataRate> bitrates;
  for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
    for (int tidx = 0; tidx < num_temporal_layers; ++tidx) {
      int bitrate_bps = bitrate_allocation.GetBitrate(sidx, tidx);
      bitrates.push_back(DataRate::BitsPerSec(bitrate_bps));
    }
  }

  return std::make_tuple(bitrates, *vc.GetScalabilityMode());
}

}  // namespace

void VideoCodecStats::Stream::LogMetrics(
    MetricsLogger* logger,
    std::string test_case_name,
    std::string prefix,
    std::map<std::string, std::string> metadata) const {
  logger->LogMetric(prefix + "width", test_case_name, width, Unit::kCount,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  logger->LogMetric(prefix + "height", test_case_name, height, Unit::kCount,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  logger->LogMetric(prefix + "frame_size_bytes", test_case_name,
                    frame_size_bytes, Unit::kBytes,
                    ImprovementDirection::kNeitherIsBetter, metadata);
  logger->LogMetric(prefix + "keyframe", test_case_name, keyframe, Unit::kCount,
                    ImprovementDirection::kSmallerIsBetter, metadata);
  logger->LogMetric(prefix + "qp", test_case_name, qp, Unit::kUnitless,
                    ImprovementDirection::kSmallerIsBetter, metadata);
  // TODO(webrtc:14852): Change to us or even ns.
  logger->LogMetric(prefix + "encode_time_ms", test_case_name, encode_time_ms,
                    Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter,
                    metadata);
  logger->LogMetric(prefix + "decode_time_ms", test_case_name, decode_time_ms,
                    Unit::kMilliseconds, ImprovementDirection::kSmallerIsBetter,
                    metadata);
  // TODO(webrtc:14852): Change to kUnitLess. kKilobitsPerSecond are converted
  // to bytes per second in Chromeperf dash.
  logger->LogMetric(prefix + "target_bitrate_kbps", test_case_name,
                    target_bitrate_kbps, Unit::kKilobitsPerSecond,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  logger->LogMetric(prefix + "target_framerate_fps", test_case_name,
                    target_framerate_fps, Unit::kHertz,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  // TODO(webrtc:14852): Change to kUnitLess. kKilobitsPerSecond are converted
  // to bytes per second in Chromeperf dash.
  logger->LogMetric(prefix + "encoded_bitrate_kbps", test_case_name,
                    encoded_bitrate_kbps, Unit::kKilobitsPerSecond,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  logger->LogMetric(prefix + "encoded_framerate_fps", test_case_name,
                    encoded_framerate_fps, Unit::kHertz,
                    ImprovementDirection::kBiggerIsBetter, metadata);
  logger->LogMetric(prefix + "bitrate_mismatch_pct", test_case_name,
                    bitrate_mismatch_pct, Unit::kPercent,
                    ImprovementDirection::kNeitherIsBetter, metadata);
  logger->LogMetric(prefix + "framerate_mismatch_pct", test_case_name,
                    framerate_mismatch_pct, Unit::kPercent,
                    ImprovementDirection::kNeitherIsBetter, metadata);
  logger->LogMetric(prefix + "transmission_time_ms", test_case_name,
                    transmission_time_ms, Unit::kMilliseconds,
                    ImprovementDirection::kSmallerIsBetter, metadata);
  logger->LogMetric(prefix + "psnr_y_db", test_case_name, psnr.y,
                    Unit::kUnitless, ImprovementDirection::kBiggerIsBetter,
                    metadata);
  logger->LogMetric(prefix + "psnr_u_db", test_case_name, psnr.u,
                    Unit::kUnitless, ImprovementDirection::kBiggerIsBetter,
                    metadata);
  logger->LogMetric(prefix + "psnr_v_db", test_case_name, psnr.v,
                    Unit::kUnitless, ImprovementDirection::kBiggerIsBetter,
                    metadata);
}

// TODO(ssilkin): use Frequency and DataRate for framerate and bitrate.
std::map<uint32_t, EncodingSettings> VideoCodecTester::CreateEncodingSettings(
    std::string codec_type,
    std::string scalability_name,
    int width,
    int height,
    std::vector<int> layer_bitrates_kbps,
    double framerate_fps,
    int num_frames,
    uint32_t first_timestamp_rtp) {
  auto [layer_bitrates, scalability_mode] =
      SplitBitrateAndUpdateScalabilityMode(
          codec_type, *ScalabilityModeFromString(scalability_name), width,
          height, layer_bitrates_kbps, framerate_fps);

  int num_spatial_layers = ScalabilityModeToNumSpatialLayers(scalability_mode);
  int num_temporal_layers =
      ScalabilityModeToNumTemporalLayers(scalability_mode);

  std::map<LayerId, LayerSettings> layers_settings;
  for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
    int layer_width = width >> (num_spatial_layers - sidx - 1);
    int layer_height = height >> (num_spatial_layers - sidx - 1);
    for (int tidx = 0; tidx < num_temporal_layers; ++tidx) {
      double layer_framerate_fps =
          framerate_fps / (1 << (num_temporal_layers - tidx - 1));
      layers_settings.emplace(
          LayerId{.spatial_idx = sidx, .temporal_idx = tidx},
          LayerSettings{
              .resolution = {.width = layer_width, .height = layer_height},
              .framerate = Frequency::MilliHertz(1000 * layer_framerate_fps),
              .bitrate = layer_bitrates[sidx * num_temporal_layers + tidx]});
    }
  }

  SdpVideoFormat sdp_video_format = SdpVideoFormat(codec_type);
  if (codec_type == "H264") {
    const std::string packetization_mode =
        "1";  // H264PacketizationMode::SingleNalUnit
    sdp_video_format.parameters =
        CreateH264Format(H264Profile::kProfileConstrainedBaseline,
                         H264Level::kLevel3_1, packetization_mode,
                         /*add_scalability_modes=*/false)
            .parameters;
  }

  std::map<uint32_t, EncodingSettings> frames_settings;
  uint32_t timestamp_rtp = first_timestamp_rtp;
  for (int frame_num = 0; frame_num < num_frames; ++frame_num) {
    frames_settings.emplace(
        timestamp_rtp, EncodingSettings{.sdp_video_format = sdp_video_format,
                                        .scalability_mode = scalability_mode,
                                        .layers_settings = layers_settings});

    timestamp_rtp += k90kHz / Frequency::MilliHertz(1000 * framerate_fps);
  }

  return frames_settings;
}

std::unique_ptr<VideoCodecTester::VideoCodecStats>
VideoCodecTester::RunDecodeTest(const Environment& env,
                                CodedVideoSource* video_source,
                                VideoDecoderFactory* decoder_factory,
                                const DecoderSettings& decoder_settings,
                                const SdpVideoFormat& sdp_video_format) {
  std::unique_ptr<VideoCodecAnalyzer> analyzer =
      std::make_unique<VideoCodecAnalyzer>(/*video_source=*/nullptr);
  Decoder decoder(env, decoder_factory, decoder_settings, analyzer.get());
  decoder.Initialize(sdp_video_format);

  while (auto frame = video_source->PullFrame()) {
    decoder.Decode(*frame);
  }

  decoder.Flush();
  analyzer->Flush();
  return std::move(analyzer);
}

std::unique_ptr<VideoCodecTester::VideoCodecStats>
VideoCodecTester::RunEncodeTest(
    const Environment& env,
    const VideoSourceSettings& source_settings,
    VideoEncoderFactory* encoder_factory,
    const EncoderSettings& encoder_settings,
    const std::map<uint32_t, EncodingSettings>& encoding_settings) {
  VideoSource video_source(source_settings);
  std::unique_ptr<VideoCodecAnalyzer> analyzer =
      std::make_unique<VideoCodecAnalyzer>(/*video_source=*/nullptr);
  Encoder encoder(env, encoder_factory, encoder_settings, analyzer.get());
  encoder.Initialize(encoding_settings.begin()->second);

  for (const auto& [timestamp_rtp, frame_settings] : encoding_settings) {
    const EncodingSettings::LayerSettings& top_layer =
        frame_settings.layers_settings.rbegin()->second;
    VideoFrame source_frame = video_source.PullFrame(
        timestamp_rtp, top_layer.resolution, top_layer.framerate);
    encoder.Encode(source_frame, frame_settings,
                   [](const EncodedImage& encoded_frame) {});
  }

  encoder.Flush();
  analyzer->Flush();
  return std::move(analyzer);
}

std::unique_ptr<VideoCodecTester::VideoCodecStats>
VideoCodecTester::RunEncodeDecodeTest(
    const Environment& env,
    const VideoSourceSettings& source_settings,
    VideoEncoderFactory* encoder_factory,
    VideoDecoderFactory* decoder_factory,
    const EncoderSettings& encoder_settings,
    const DecoderSettings& decoder_settings,
    const std::map<uint32_t, EncodingSettings>& encoding_settings) {
  VideoSource video_source(source_settings);
  std::unique_ptr<VideoCodecAnalyzer> analyzer =
      std::make_unique<VideoCodecAnalyzer>(&video_source);
  const EncodingSettings& frame_settings = encoding_settings.begin()->second;
  Encoder encoder(env, encoder_factory, encoder_settings, analyzer.get());
  encoder.Initialize(frame_settings);

  int num_spatial_layers =
      ScalabilityModeToNumSpatialLayers(frame_settings.scalability_mode);
  std::vector<std::unique_ptr<Decoder>> decoders;
  for (int sidx = 0; sidx < num_spatial_layers; ++sidx) {
    auto decoder = std::make_unique<Decoder>(env, decoder_factory,
                                             decoder_settings, analyzer.get());
    decoder->Initialize(frame_settings.sdp_video_format);
    decoders.push_back(std::move(decoder));
  }

  for (const auto& [timestamp_rtp, frame_settings] : encoding_settings) {
    const EncodingSettings::LayerSettings& top_layer =
        frame_settings.layers_settings.rbegin()->second;
    VideoFrame source_frame = video_source.PullFrame(
        timestamp_rtp, top_layer.resolution, top_layer.framerate);
    encoder.Encode(source_frame, frame_settings,
                   [&decoders](const EncodedImage& encoded_frame) {
                     int sidx = encoded_frame.SpatialIndex().value_or(
                         encoded_frame.SimulcastIndex().value_or(0));
                     decoders.at(sidx)->Decode(encoded_frame);
                   });
  }

  encoder.Flush();
  for (auto& decoder : decoders) {
    decoder->Flush();
  }
  analyzer->Flush();
  return std::move(analyzer);
}

}  // namespace test
}  // namespace webrtc
