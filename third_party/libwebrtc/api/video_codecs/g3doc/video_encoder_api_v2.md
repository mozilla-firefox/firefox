<!-- go/cmark -->

<!--* freshness: {
  owner: 'philipel@webrtc.org'
  owner: 'sprang@webrtc.org'
  reviewed: '2026-05-20'
} *-->

# RTC Video Encoder API v2

This file describes the next generation (or "v2") of video encoder API, which
represents a large departure from the legacy `VideoEncoder` interface.

## Video Encoder Model

The video encoder model is intended to be as low level as possible, while still
being codec agnostic. The model is that an encoder is "just a set of buffers".
Every time a frame is encoded, the user decides which buffers may be used for
prediction, and which buffer to update. From this, the user can construct
different encoding structures and scalability modes, dynamically change the
encoding structure, implement encoding strategies such as LTR, selectively drop
or re-encode frames, etc.

At a high level, the API consists of two parts: the
`VideoEncoderFactoryInterface` and the `VideoEncoderInterface`.

- The `VideoEncoderFactoryInterface` maps one-to-one to an underlying
  implementation. Its main purpose is to describe what the implementation is
  capable of, and to create encoder instances of that implementation.
- The `VideoEncoderInterface` is an instance of an encoder, and its main purpose
  is to encode video.

______________________________________________________________________

## VideoEncoderFactoryInterface

The general outline of the `VideoEncoderFactoryInterface` is described here:

```cpp
class VideoEncoderFactoryInterface {
 public:
  enum class RateControlMode { kCqp, kCbr };
  enum class CbrSetting { kBufferSizes, kMaxIntraBitrateFactor };

  struct Capabilities {
    struct PredictionConstraints {
      enum class BufferSpaceType {
        kMultiInstance,  // multiple independent sets of buffers
        kMultiKeyframe,  // single set of buffers, but can store multiple
                         // keyframes simultaneously.
        kSingleKeyframe  // single set of buffers, can only store one keyframe
                         // at a time.
      };

      int num_buffers;
      int max_references;
      int max_temporal_layers;

      BufferSpaceType buffer_space_type;
      int max_spatial_layers;
      std::vector<Rational> scaling_factors;

      std::set<FrameType> supported_frame_types;
    } prediction_constraints;

    struct InputConstraints {
      Resolution min;
      Resolution max;
      int pixel_alignment;
      std::vector<VideoFrameBuffer::Type> input_formats;
    } input_constraints;

    std::vector<EncodingFormat> encoding_formats;

    struct BitrateControl {
      std::pair<int, int> qp_range;
      std::set<RateControlMode> rc_modes;
      std::set<CbrSetting> supported_cbr_settings;
    } rate_control;

    struct Performance {
      bool encode_on_calling_thread;
      std::pair<int, int> min_max_effort_level;
    } performance;
  };

  struct StaticEncoderSettings {
    struct Cqp {};
    struct Cbr {
      TimeDelta max_buffer_size;
      TimeDelta target_buffer_size;
      double max_intra_bitrate_factor;
    };

    Resolution max_encode_dimensions;
    EncodingFormat encoding_format;
    std::variant<Cqp, Cbr> rc_mode;
    int max_number_of_threads;
  };

  virtual ~VideoEncoderFactoryInterface() = default;

  virtual std::string CodecName() const = 0;
  virtual std::string ImplementationName() const = 0;
  virtual std::map<std::string, std::string> CodecSpecifics() const = 0;

  virtual Capabilities GetEncoderCapabilities() const = 0;
  virtual std::unique_ptr<VideoEncoderInterface> CreateEncoder(
      const StaticEncoderSettings& settings,
      const std::map<std::string, std::string>& encoder_specific_settings) = 0;
};
```

### CodecName, ImplementationName, CodecSpecifics

- **`CodecName()`**: Returns the name of the codec (e.g., "VP8", "VP9", "AV1").
- **`ImplementationName()`**: Returns the specific implementation used (e.g.,
  "libaom", "vaapi/Intel gen10").
- **`CodecSpecifics()`**: Returns a string-to-string map containing
  codec-specific information such as profile/tier/level, and encoder-specific
  information like driver version or software version.

### CreateEncoder

Creates an encoder instance (`VideoEncoderInterface`).

#### StaticEncoderSettings

Represents settings that are fixed for the entire lifetime of the encoder.

- **`max_encode_dimensions`**: The maximum resolution (`Resolution`) that will
  be requested from the encoder. This allows the encoder to pre-allocate buffers
  and know that only sizes equal to or lower than those will be used.
- **`encoding_format`**: The encoded output format (`EncodingFormat`).
- **`rc_mode`**: The rate control mode to be used when encoding.
  - **`Cqp`**: Constant QP mode.
  - **`Cbr`**: Constant bitrate mode. The encoder picks the QP needed to meet
    the per frame bit budget implied by the target bitrate and frame duration
    given in `FrameEncodeSettings`. Individual frames will be larger or smaller
    than that budget, and these settings say how much of that variation the
    caller can absorb, expressed as the transmission delay it adds when the
    stream is sent over a channel with a capacity equal to the target bitrate.
    Note that an encoder is not required to honor all of these settings, see
    [Bitrate Control](#bitrate-control), and that settings which are out of
    range are rejected, i.e. `CreateEncoder` returns nullptr.
    - **`max_buffer_size`**: The largest transmission delay the encoder may
      cause. The encoder is expected to stay within this bound even at the cost
      of quality. Must be finite and greater than zero.
    - **`target_buffer_size`**: The transmission delay the encoder should
      operate at, leaving the difference up to `max_buffer_size` as headroom for
      bursts, such as intra frames or sudden changes in the content. Must be
      greater than zero and at most `max_buffer_size`.
    - **`max_intra_bitrate_factor`**: How much data an intra frame may use,
      expressed as a multiple of the bit budget of a frame on the same temporal
      layer. This controls the trade off between the quality of intra frames and
      the delay they add. Must be finite and at least 1.0.
- **`max_number_of_threads`**: The maximum number of threads that the encoder
  instance may spawn.

______________________________________________________________________

## Querying Encoder Capabilities

One of the most important goals for the new API is to be able to reason about
the available encoders. The factory can return an instance of a `Capabilities`
struct via `GetEncoderCapabilities()`.

### Prediction Constraints

To facilitate implementations of different scalability structures, such as
temporal & spatial layers, we need to know how many reference buffers are
available to store states from previous frames, and how those reference buffers
may be used when encoding a frame. The `PredictionConstraints` describe these
constraints:

```cpp
struct PredictionConstraints {
  enum class BufferSpaceType {
    kMultiInstance,  // multiple independent sets of buffers
    kMultiKeyframe,  // single set of buffers, but can store multiple
                     // keyframes simultaneously.
    kSingleKeyframe  // single set of buffers, can only store one keyframe
                     // at a time.
  };

  int num_buffers;
  int max_references;
  int max_temporal_layers;

  BufferSpaceType buffer_space_type;
  int max_spatial_layers;
  std::vector<Rational> scaling_factors;

  std::vector<FrameType> supported_frame_types;
};
```

- **`num_buffers`**: How many reference buffers are available. For a fully
  implemented VP8 encoder this would be 3, for VP9/AV1 it would be 8, and for
  H264/H265 it would be 16. Specific encoder implementations may limit this
  number to something lower than the spec allows. If the encoder implementation
  supports reference frame control, it must indicate at least a value of 1.
  Implementations that don't support reference frame control must indicate a
  value of 0.
- **`max_references`**: This field indicates how many buffers can be used as
  reference for any given frame. For both VP8 and VP9 this would then be up to
  3\. Again, implementations may limit it further.
- **`max_temporal_layers`**: The maximum number of temporal layers the encoder
  supports. This is mainly used to determine how many temporal layers can be
  used in CBR-mode, as many encoders lack per-layer CBR rate control.
- **`max_spatial_layers`**: Maximum number of spatial/quality layers.
- **`scaling_factors`**: Indicates the resolution scale factors supported when
  referencing other frames. Examples:
  - `{1, 1/2}` needed in a spatial scalability scenario using a higher layer
    referencing a buffer from a lower spatial layer where the width and height
    are ½ of the layer currently encoded.
  - `{1}` A single 1/1 factor indicates the encoder can do inter-layer
    prediction, but only when using the same resolution.
  - `{}` An empty list indicates inter-layer prediction is not supported (i.e.,
    only simulcast modes are supported).
- **`buffer_space_type`**: This enum is relevant when multiple independent
  encodings are produced, i.e., simulcast or spatial scalability with
  inter-layer prediction. The enum indicates if the encodings have independent
  sets of reference buffers, or if they share a common buffer space, and if
  multiple keyframes can be stored at the same time in the same buffer space.
  - **`kMultiInstance`**: Spatial layers are encoded in separate buffer spaces.
    Inter-layer prediction is not supported. An example is the libvpx VP8
    multires encoder.
  - **`kMultiKeyframe`**: Spatial layers share the same buffer space, and
    multiple keyframes can be stored in the encoder at the same time. An example
    is the libvpx VP9 encoder.
  - **`kSingleKeyframe`**: Spatial layers share the same buffer space, and only
    one keyframe can be stored in the buffer at any time. Note that encoders
    that support `kStartFrame` can still encode independent spatial layers. This
    is the typical encoder buffer space type, and an example that supports both
    keyframes and startframes is the libaom AV1 encoder.
- **`supported_frame_types`**: Indicates which types of frames the encoder can
  produce:
  - **`FrameType::kKeyframe`**: An all-intra coded frame which depends on no
    previous state and refreshes all current decoder state (recreates all
    reference buffers, coding contexts, entropy tables, etc.).
  - **`FrameType::kStartFrame`**: An independently decodable frame that only
    updates specified decoder state, but allows previous reference frames to be
    kept. A new decoder must be able to start decoding from this frame.
  - **`FrameType::kDeltaFrame`**: A frame that references existing state as part
    of the coding.

### Input Constraints

Input constraints define the constraints for the source frames to be encoded.

```cpp
struct InputConstraints {
  Resolution min;
  Resolution max;
  int pixel_alignment;
  std::vector<VideoFrameBuffer::Type> input_formats;
};
```

- **`min` / `max`**: The lower and upper bound on the resolutions the encoder
  supports (`Resolution`).
- **`pixel_alignment`**: Requested output resolution must be a multiple of the
  pixel alignment.
- **`input_formats`**: The set of formats the encoder supports for input frames
  (e.g., `VideoFrameBuffer::Type::kI420`, `VideoFrameBuffer::Type::kNV12`).

### Encoding Formats

The supported encoded image formats, in terms of chroma subsampling and the
sample bit depth. Note that the input format does not necessarily match the
encoded format. E.g., the input may be in RGB but the encoded images are in YUV
4:2:0 format.

```cpp
struct EncodingFormat {
  enum SubSampling { k420, k422, k444 };
  SubSampling sub_sampling;
  int bit_depth;
};
```

- **`sub_sampling`**: Supported chroma subsampling (4:2:0, 4:2:2, 4:4:4).
- **`bit_depth`**: Supported sample bit depth (typically 8, 10, or 12).

### Bitrate Control

```cpp
struct BitrateControl {
  std::pair<int, int> qp_range;
  std::set<RateControlMode> rc_modes;
  std::set<CbrSetting> supported_cbr_settings;
};
```

- **`qp_range`**: Min and max QP supported by the encoder.
- **`rc_modes`**: The set of supported rate control modes (`RateControlMode`):
  - **`RateControlMode::kCbr` (Constant Bitrate)**: If the encoder indicates it
    can handle scalability modes (see Prediction Constraints), it MUST allow
    setting per-layer bitrate targets to advertise CBR support.
  - **`RateControlMode::kCqp` (Constant QP)**: The encoder MUST allow setting
    the QP on a per-frame basis in this mode. By setting per-frame QP we can
    implement external rate control.
- **`supported_cbr_settings`**: The settings in `StaticEncoderSettings::Cbr`
  that the encoder honors. `kBufferSizes` covers `max_buffer_size` and
  `target_buffer_size`, which are only meaningful together. Settings not listed
  here are ignored by the encoder, which uses an implementation defined behavior
  instead. Only meaningful if `rc_modes` contains `RateControlMode::kCbr`.

### Performance

Performance-related options:

```cpp
struct Performance {
  bool encode_on_calling_thread;
  std::pair<int, int> min_max_effort_level;
};
```

- **`encode_on_calling_thread`**: Whether the thread calling `Encode` will be
  used for encoding.
- **`min_max_effort_level`**: Indicates min and max available effort level. The
  min effort level can be a negative value and indicates faster encoding but at
  a lower quality or compression. The max effort level can be a positive value
  and indicates slower encoding but at a higher quality or compression. The
  default (0) should always be part of the range.

______________________________________________________________________

## VideoEncoderInterface

```cpp
class VideoEncoderInterface {
 public:
  virtual ~VideoEncoderInterface() = default;
  enum class FrameType { kKeyframe, kStartFrame, kDeltaFrame };

  struct EncodingError {};
  struct EncodedData {
    FrameType frame_type;
    int encoded_qp;
  };
  using EncodeResult = std::variant<EncodingError, EncodedData>;

  struct FrameOutput {
    virtual ~FrameOutput() = default;
    virtual std::span<uint8_t> GetBitstreamOutputBuffer(DataSize size) = 0;
    virtual void EncodeComplete(const EncodeResult& encode_result) = 0;
  };

  struct TemporalUnitSettings {
    VideoTrackInterface::ContentHint content_hint =
        VideoTrackInterface::ContentHint::kNone;
    Timestamp presentation_timestamp;
  };

  struct FrameEncodeSettings {
    struct Cbr {
      TimeDelta duration;
      DataRate target_bitrate;
    };

    struct Cqp {
      int target_qp;
    };

    std::variant<Cqp, Cbr> rate_options;

    FrameType frame_type = FrameType::kDeltaFrame;
    int temporal_id = 0;
    int spatial_id = 0;
    Resolution resolution;
    std::vector<int> reference_buffers;
    std::optional<int> update_buffer;
    int effort_level = 0;

    std::unique_ptr<FrameOutput> frame_output;
  };

  virtual void Encode(scoped_refptr<VideoFrameBuffer> frame_buffer,
                      const TemporalUnitSettings& settings,
                      std::vector<FrameEncodeSettings> frame_settings) = 0;
};
```

______________________________________________________________________

## Encoding Parameters

### TemporalUnitSettings

These settings are provided for each temporal unit to be encoded.

```cpp
struct TemporalUnitSettings {
  VideoTrackInterface::ContentHint content_hint =
      VideoTrackInterface::ContentHint::kNone;
  Timestamp presentation_timestamp;
};
```

- **`content_hint`**: Indicates the content type of the video track
  (`VideoTrackInterface::ContentHint` from `api/media_stream_interface.h`):
  - `ContentHint::kNone` / `ContentHint::kFluid`: Camera/motion video (default
    tuning).
  - `ContentHint::kDetailed` / `ContentHint::kText`: Detailed or text screen
    content (screen tuning with palette mode enabled).
- **`presentation_timestamp`**: When the frame is meant to be presented. Only
  used in CBR mode, where it determines how much buffer levels should be
  adjusted between encoded frames.

### FrameEncodeSettings

These settings are provided for each frame. Note that there may be multiple
encodings for a single input frame (e.g. when spatial layering is used). This is
represented by a vector of `FrameEncodeSettings`.

```cpp
struct FrameEncodeSettings {
  struct Cbr {
    TimeDelta duration;
    DataRate target_bitrate;
  };

  struct Cqp {
    int target_qp;
  };

  std::variant<Cqp, Cbr> rate_options;

  FrameType frame_type = FrameType::kDeltaFrame;
  int temporal_id = 0;
  int spatial_id = 0;
  Resolution resolution;
  std::vector<int> reference_buffers;
  std::optional<int> update_buffer;
  int effort_level = 0;

  std::unique_ptr<FrameOutput> frame_output;
};
```

- **`rate_options`**
  - **`Cbr`**
    - **`duration`**: The duration of the frame (`TimeDelta`).
    - **`target_bitrate`**: The target rate for the duration of the frame
      (`DataRate`).
  - **`Cqp`**
    - **`target_qp`**: The frame global QP to encode with.
- **`frame_type`**: Whether the frame should be a keyframe, startframe, or delta
  frame (`FrameType`).
- **`temporal_id`**: The temporal ID for this frame. Must be set to 0 for
  keyframes. Provided as input for the CBR rate controller.
- **`spatial_id`**: The spatial ID for this frame. The `FrameEncodeSettings` of
  a temporal unit must be ordered by strictly increasing spatial ID, i.e. there
  is at most one frame per spatial layer and lower layers come first.
- **`resolution`**: The resolution to encode the frame at. Note that any
  reference buffer used for prediction must have a resolution that is a multiple
  of the supported scaling factors.
- **`reference_buffers`**: List of reference buffer slots the encoder is allowed
  to reference when encoding this frame. It may choose to use only a subset. The
  list is in order of preference, most useful (typically the most recently
  updated) first, and encoders that favor some references over others should
  follow that order.
- **`update_buffer`**: Which buffer that the encoded frame should be placed in.
  For keyframes and startframes this must be set.
- **`effort_level`**: Indicates the amount of effort the encoder should spend
  encoding the frame.
- **`frame_output`**: A `unique_ptr<FrameOutput>` that provides an allocator for
  the bitstream buffer (`GetBitstreamOutputBuffer`) and receives the callback
  when the encode completes (`EncodeComplete`).

Note that an encoder MUST update exactly the buffer specified in the encode call
(e.g., no internal repeated keyframe interval) if it supports spatial
scalability. Otherwise, it breaks assumptions needed to construct some
dependency structures.

### FrameOutput

The encoder MUST provide feedback for each frame we attempt to encode. We must
therefore have a mechanism to map the encoding requests to the feedback. By
providing a callback and output buffer allocator interface (`FrameOutput`) via
`std::unique_ptr<FrameOutput>` in `FrameEncodeSettings`, each frame will have
its own unique feedback and output allocation path.

```cpp
struct FrameOutput {
  virtual ~FrameOutput() = default;
  virtual std::span<uint8_t> GetBitstreamOutputBuffer(DataSize size) = 0;
  virtual void EncodeComplete(const EncodeResult& encode_result) = 0;
};
```

- **`GetBitstreamOutputBuffer(DataSize size)`**: The encoder calls this to
  request a target buffer of specified `size` for the resulting bitstream data.
  The caller allocates or provides the buffer (as a `std::span<uint8_t>`),
  allowing the encoder to write directly to it and eliminate extra copies.
- **`EncodeComplete(const EncodeResult& encode_result)`**: Called when the frame
  has completed encoding.

```cpp
struct EncodingError {};
struct EncodedData {
  FrameType frame_type;
  int encoded_qp;
};
using EncodeResult = std::variant<EncodingError, EncodedData>;
```

The feedback contains either:

- **`EncodingError`**: Some error occurred, the encoder is no longer usable.
- **`EncodedData`**:
  - **`frame_type`**: Certain encoders have a fixed keyframe interval, and to
    support them the encoded frame type is signaled back to the application.
  - **`encoded_qp`**: The approximate average QP for the encoded frame. It is
    recommended that a pixel-weighted average QP is reported, but e.g. average
    pre-macroblock average QP is acceptable in most cases. If CQP mode is used,
    this value should match the target QP.

______________________________________________________________________

## Encode

```cpp
virtual void Encode(scoped_refptr<VideoFrameBuffer> frame_buffer,
                    const TemporalUnitSettings& settings,
                    std::vector<FrameEncodeSettings> frame_settings) = 0;
```

A `VideoEncoderInterface` instance has just a single method for interacting with
it: `Encode()`. It takes three arguments:

- **`frame_buffer`**: The input frame to be encoded
  (`scoped_refptr<VideoFrameBuffer>`).
- **`settings`**: Settings common to the temporal units
  (`TemporalUnitSettings`).
- **`frame_settings`**: One settings struct per output frame expected from the
  encoder (`FrameEncodeSettings`).

______________________________________________________________________

## A note on references

A frame for which all referenced frames have been decoded MUST itself be
decodable. References do not only express dependencies on other frames used for
inter-frame prediction, they express any form of dependency on prior state. What
this means is that frames that have no references MUST be decodable without
relying on any prior state.
