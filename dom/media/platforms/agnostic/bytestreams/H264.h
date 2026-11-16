/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MP4_DEMUXER_H264_H_
#define MP4_DEMUXER_H264_H_

#include <stdint.h>

#include "ErrorList.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/Span.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsTArray.h"

namespace mozilla {
class BitReader;
class MediaByteBuffer;
class MediaRawData;

enum H264_PROFILE {
  H264_PROFILE_UNKNOWN = 0,
  H264_PROFILE_BASE = 0x42,
  H264_PROFILE_MAIN = 0x4D,
  H264_PROFILE_EXTENDED = 0x58,
  H264_PROFILE_HIGH = 0x64,
  // Keep in sync with H264ProfileValidator.
};

enum class H264_LEVEL {
  H264_LEVEL_1 = 10,
  H264_LEVEL_1_b = 11,
  H264_LEVEL_1_1 = 11,
  H264_LEVEL_1_2 = 12,
  H264_LEVEL_1_3 = 13,
  H264_LEVEL_2 = 20,
  H264_LEVEL_2_1 = 21,
  H264_LEVEL_2_2 = 22,
  H264_LEVEL_3 = 30,
  H264_LEVEL_3_1 = 31,
  H264_LEVEL_3_2 = 32,
  H264_LEVEL_4 = 40,
  H264_LEVEL_4_1 = 41,
  H264_LEVEL_4_2 = 42,
  H264_LEVEL_5 = 50,
  H264_LEVEL_5_1 = 51,
  H264_LEVEL_5_2 = 52,
  H264_LEVEL_6 = 60,
  H264_LEVEL_6_1 = 61,
  H264_LEVEL_6_2 = 62
  // Keep in sync with H264LevelValidator.
};

// Spec 7.4.2.1
#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256

// NAL unit types
enum NAL_TYPES {
  H264_NAL_SLICE = 1,
  H264_NAL_DPA = 2,
  H264_NAL_DPB = 3,
  H264_NAL_DPC = 4,
  H264_NAL_IDR_SLICE = 5,
  H264_NAL_SEI = 6,
  H264_NAL_SPS = 7,
  H264_NAL_PPS = 8,
  H264_NAL_AUD = 9,
  H264_NAL_END_SEQUENCE = 10,
  H264_NAL_END_STREAM = 11,
  H264_NAL_FILLER_DATA = 12,
  H264_NAL_SPS_EXT = 13,
  H264_NAL_PREFIX = 14,
  H264_NAL_AUXILIARY_SLICE = 19,
  H264_NAL_SLICE_EXT = 20,
  H264_NAL_SLICE_EXT_DVC = 21,
};

// ITU-T H.264 (V15) (08/2024), 7.3.1 NAL unit syntax.
struct MOZ_STACK_CLASS H264NALU final {
  H264NALU(const uint8_t* aData MOZ_LIFETIME_BOUND, uint32_t aByteCount);
  H264NALU() = default;

  uint8_t mNalUnitType;
  // This points to the full content of the NALU, which can be used to extract
  // the RBSP.
  const Span<const uint8_t> mNALU;
};

// According to ITU-T Rec H.264 (2017/04) Table 7.6.
enum SLICE_TYPES {
  P_SLICE = 0,
  B_SLICE = 1,
  I_SLICE = 2,
  SP_SLICE = 3,
  SI_SLICE = 4,
};

struct SPSData {
  bool operator==(const SPSData& aOther) const;
  bool operator!=(const SPSData& aOther) const;

  gfx::YUVColorSpace ColorSpace() const;
  gfx::ColorDepth ColorDepth() const;

  bool valid = {};

  /* Decoded Members */
  /*
    pic_width is the decoded width according to:
    pic_width = ((pic_width_in_mbs_minus1 + 1) * 16)
                - (frame_crop_left_offset + frame_crop_right_offset) * 2
   */
  uint32_t pic_width = {};
  /*
    pic_height is the decoded height according to:
    pic_height = (2 - frame_mbs_only_flag) * ((pic_height_in_map_units_minus1 +
    1) * 16)
                 - (frame_crop_top_offset + frame_crop_bottom_offset) * 2
   */
  uint32_t pic_height = {};

  bool interlaced = {};

  /*
   Displayed size.
   display_width and display_height are adjusted according to the display
   sample aspect ratio.
   */
  uint32_t display_width = {};
  uint32_t display_height = {};

  float sample_ratio = {};

  uint32_t crop_left = {};
  uint32_t crop_right = {};
  uint32_t crop_top = {};
  uint32_t crop_bottom = {};

  /*
    H264 decoding parameters according to ITU-T H.264 (T-REC-H.264-201402-I/en)
   http://www.itu.int/rec/T-REC-H.264-201402-I/en
   */

  bool constraint_set0_flag = {};
  bool constraint_set1_flag = {};
  bool constraint_set2_flag = {};
  bool constraint_set3_flag = {};
  bool constraint_set4_flag = {};
  bool constraint_set5_flag = {};

  /*
    profile_idc and level_idc indicate the profile and level to which the coded
    video sequence conforms when the SVC sequence parameter set is the active
    SVC sequence parameter set.
   */
  uint8_t profile_idc = {};
  uint8_t level_idc = {};

  /*
    seq_parameter_set_id identifies the sequence parameter set that is referred
    to by the picture parameter set. The value of seq_parameter_set_id shall be
    in the range of 0 to 31, inclusive.
   */
  uint8_t seq_parameter_set_id = {};

  /*
    chroma_format_idc specifies the chroma sampling relative to the luma
    sampling as specified in clause 6.2. The value of chroma_format_idc shall be
    in the range of 0 to 3, inclusive. When chroma_format_idc is not present,
    it shall be inferred to be equal to 1 (4:2:0 chroma format).
    When profile_idc is equal to 183, chroma_format_idc shall be equal to 0
    (4:0:0 chroma format).
   */
  uint8_t chroma_format_idc = {};

  /*
    bit_depth_luma_minus8 specifies the bit depth of the samples of the luma
    array and the value of the luma quantisation parameter range offset
    QpBdOffset Y , as specified by
      BitDepth Y = 8 + bit_depth_luma_minus8 (7-3)
      QpBdOffset Y = 6 * bit_depth_luma_minus8 (7-4)
    When bit_depth_luma_minus8 is not present, it shall be inferred to be equal
    to 0. bit_depth_luma_minus8 shall be in the range of 0 to 6, inclusive.
  */
  uint8_t bit_depth_luma_minus8 = {};

  /*
    bit_depth_chroma_minus8 specifies the bit depth of the samples of the chroma
    arrays and the value of the chroma quantisation parameter range offset
    QpBdOffset C , as specified by
      BitDepth C = 8 + bit_depth_chroma_minus8 (7-5)
      QpBdOffset C = 6 * bit_depth_chroma_minus8 (7-6)
    When bit_depth_chroma_minus8 is not present, it shall be inferred to be
    equal to 0. bit_depth_chroma_minus8 shall be in the range of 0 to 6,
    inclusive.
  */
  uint8_t bit_depth_chroma_minus8 = {};

  /*
    separate_colour_plane_flag equal to 1 specifies that the three colour
    components of the 4:4:4 chroma format are coded separately.
    separate_colour_plane_flag equal to 0 specifies that the colour components
    are not coded separately. When separate_colour_plane_flag is not present,
    it shall be inferred to be equal to 0. When separate_colour_plane_flag is
    equal to 1, the primary coded picture consists of three separate components,
    each of which consists of coded samples of one colour plane (Y, Cb or Cr)
    that each use the monochrome coding syntax. In this case, each colour plane
    is associated with a specific colour_plane_id value.
   */
  bool separate_colour_plane_flag = {};

  /*
     seq_scaling_matrix_present_flag equal to 1 specifies that the flags
     seq_scaling_list_present_flag[ i ] for i = 0..7 or
     i = 0..11 are present. seq_scaling_matrix_present_flag equal to 0 specifies
     that these flags are not present and the sequence-level scaling list
     specified by Flat_4x4_16 shall be inferred for i = 0..5 and the
     sequence-level scaling list specified by Flat_8x8_16 shall be inferred for
     i = 6..11. When seq_scaling_matrix_present_flag is not present, it shall be
     inferred to be equal to 0.
     */
  bool seq_scaling_matrix_present_flag = {};

  /*
    log2_max_frame_num_minus4 specifies the value of the variable
    MaxFrameNum that is used in frame_num related derivations as
    follows:

     MaxFrameNum = 2( log2_max_frame_num_minus4 + 4 ). The value of
    log2_max_frame_num_minus4 shall be in the range of 0 to 12, inclusive.
   */
  uint8_t log2_max_frame_num = {};

  /*
    pic_order_cnt_type specifies the method to decode picture order
    count (as specified in subclause 8.2.1). The value of
    pic_order_cnt_type shall be in the range of 0 to 2, inclusive.
   */
  uint8_t pic_order_cnt_type = {};

  /*
    log2_max_pic_order_cnt_lsb_minus4 specifies the value of the
    variable MaxPicOrderCntLsb that is used in the decoding
    process for picture order count as specified in subclause
    8.2.1 as follows:

    MaxPicOrderCntLsb = 2( log2_max_pic_order_cnt_lsb_minus4 + 4 )

    The value of log2_max_pic_order_cnt_lsb_minus4 shall be in
    the range of 0 to 12, inclusive.
   */
  uint8_t log2_max_pic_order_cnt_lsb = {};

  /*
    delta_pic_order_always_zero_flag equal to 1 specifies that
    delta_pic_order_cnt[ 0 ] and delta_pic_order_cnt[ 1 ] are
    not present in the slice headers of the sequence and shall
    be inferred to be equal to 0.
   */
  bool delta_pic_order_always_zero_flag = {};

  /*
    offset_for_non_ref_pic is used to calculate the picture
    order count of a non-reference picture as specified in
    8.2.1. The value of offset_for_non_ref_pic shall be in the
    range of -231 to 231 - 1, inclusive.
   */
  int8_t offset_for_non_ref_pic = {};

  /*
    offset_for_top_to_bottom_field is used to calculate the
    picture order count of a bottom field as specified in
    subclause 8.2.1. The value of offset_for_top_to_bottom_field
    shall be in the range of -231 to 231 - 1, inclusive.
   */
  int8_t offset_for_top_to_bottom_field = {};

  /*
    max_num_ref_frames specifies the maximum number of short-term and
    long-term reference frames, complementary reference field pairs,
    and non-paired reference fields that may be used by the decoding
    process for inter prediction of any picture in the
    sequence. max_num_ref_frames also determines the size of the sliding
    window operation as specified in subclause 8.2.5.3. The value of
    max_num_ref_frames shall be in the range of 0 to MaxDpbFrames (as
    specified in subclause A.3.1 or A.3.2), inclusive.
   */
  uint32_t max_num_ref_frames = {};

  /*
    max_num_reorder_frames from the VUI bitstream-restriction parameters
    (Annex E.1.2) specifies the maximum number of frames, complementary
    field pairs, or non-paired fields that precede any frame, complementary
    field pair, or non-paired field in decoding order and follow it in
    output order. A non-zero value means decode order can differ from
    presentation order (B-frames). Inferred as 0 for intra-only profiles
    (or MaxDpbFrames otherwise) when the restriction parameters are absent.
   */
  uint32_t max_num_reorder_frames = {};

  /*
    max_dec_frame_buffering from the VUI bitstream-restriction parameters
    (Annex E.1.2) specifies the required size of the decoded picture buffer.
    max_num_reorder_frames shall be in the range of 0 to
    max_dec_frame_buffering, inclusive. Stays 0 when the restriction
    parameters are absent.
   */
  uint32_t max_dec_frame_buffering = {};

  /*
    gaps_in_frame_num_value_allowed_flag specifies the allowed
    values of frame_num as specified in subclause 7.4.3 and the
    decoding process in case of an inferred gap between values of
    frame_num as specified in subclause 8.2.5.2.
   */
  bool gaps_in_frame_num_allowed_flag = {};

  /*
    pic_width_in_mbs_minus1 plus 1 specifies the width of each
    decoded picture in units of macroblocks.  16 macroblocks in a row
   */
  uint32_t pic_width_in_mbs = {};

  /*
    pic_height_in_map_units_minus1 plus 1 specifies the height in
    slice group map units of a decoded frame or field.  16
    macroblocks in each column.
   */
  uint32_t pic_height_in_map_units = {};

  /*
    frame_mbs_only_flag equal to 0 specifies that coded pictures of
    the coded video sequence may either be coded fields or coded
    frames. frame_mbs_only_flag equal to 1 specifies that every
    coded picture of the coded video sequence is a coded frame
    containing only frame macroblocks.
   */
  bool frame_mbs_only_flag = {};

  /*
    mb_adaptive_frame_field_flag equal to 0 specifies no
    switching between frame and field macroblocks within a
    picture. mb_adaptive_frame_field_flag equal to 1 specifies
    the possible use of switching between frame and field
    macroblocks within frames. When mb_adaptive_frame_field_flag
    is not present, it shall be inferred to be equal to 0.
   */
  bool mb_adaptive_frame_field_flag = {};

  /*
    direct_8x8_inference_flag specifies the method used in the derivation
    process for luma motion vectors for B_Skip, B_Direct_16x16 and B_Direct_8x8
    as specified in clause 8.4.1.2. When frame_mbs_only_flag is equal to 0,
    direct_8x8_inference_flag shall be equal to 1.
  */
  bool direct_8x8_inference_flag = {};

  /*
    frame_cropping_flag equal to 1 specifies that the frame cropping
    offset parameters follow next in the sequence parameter
    set. frame_cropping_flag equal to 0 specifies that the frame
    cropping offset parameters are not present.
   */
  bool frame_cropping_flag = {};
  uint32_t frame_crop_left_offset = {};
  uint32_t frame_crop_right_offset = {};
  uint32_t frame_crop_top_offset = {};
  uint32_t frame_crop_bottom_offset = {};

  // VUI Parameters

  /*
    vui_parameters_present_flag equal to 1 specifies that the
    vui_parameters( ) syntax structure as specified in Annex E is
    present. vui_parameters_present_flag equal to 0 specifies that
    the vui_parameters( ) syntax structure as specified in Annex E
    is not present.
   */
  bool vui_parameters_present_flag = {};

  /*
   aspect_ratio_info_present_flag equal to 1 specifies that
   aspect_ratio_idc is present. aspect_ratio_info_present_flag
   equal to 0 specifies that aspect_ratio_idc is not present.
   */
  bool aspect_ratio_info_present_flag = {};

  /*
    aspect_ratio_idc specifies the value of the sample aspect
    ratio of the luma samples. Table E-1 shows the meaning of
    the code. When aspect_ratio_idc indicates Extended_SAR, the
    sample aspect ratio is represented by sar_width and
    sar_height. When the aspect_ratio_idc syntax element is not
    present, aspect_ratio_idc value shall be inferred to be
    equal to 0.
   */
  uint8_t aspect_ratio_idc = {};
  uint32_t sar_width = {};
  uint32_t sar_height = {};

  /*
    video_signal_type_present_flag equal to 1 specifies that video_format,
    video_full_range_flag and colour_description_present_flag are present.
    video_signal_type_present_flag equal to 0, specify that video_format,
    video_full_range_flag and colour_description_present_flag are not present.
   */
  bool video_signal_type_present_flag = {};

  /*
    overscan_info_present_flag equal to1 specifies that the
    overscan_appropriate_flag is present. When overscan_info_present_flag is
    equal to 0 or is not present, the preferred display method for the video
    signal is unspecified (Unspecified).
   */
  bool overscan_info_present_flag = {};
  /*
    overscan_appropriate_flag equal to 1 indicates that the cropped decoded
    pictures output are suitable for display using overscan.
    overscan_appropriate_flag equal to 0 indicates that the cropped decoded
    pictures output contain visually important information in the entire region
    out to the edges of the cropping rectangle of the picture
   */
  bool overscan_appropriate_flag = {};

  /*
    video_format indicates the representation of the pictures as specified in
    Table E-2, before being coded in accordance with this
    Recommendation | International Standard. When the video_format syntax
    element is not present, video_format value shall be inferred to be equal
    to 5. (Unspecified video format)
   */
  uint8_t video_format = {};

  /*
    video_full_range_flag indicates the black level and range of the luma and
    chroma signals as derived from E′Y, E′PB, and E′PR or E′R, E′G, and E′B
    real-valued component signals.
    When the video_full_range_flag syntax element is not present, the value of
    video_full_range_flag shall be inferred to be equal to 0.
   */
  bool video_full_range_flag = {};

  /*
    colour_description_present_flag equal to1 specifies that colour_primaries,
    transfer_characteristics and matrix_coefficients are present.
    colour_description_present_flag equal to 0 specifies that colour_primaries,
    transfer_characteristics and matrix_coefficients are not present.
   */
  bool colour_description_present_flag = {};

  /*
    colour_primaries indicates the chromaticity coordinates of the source
    primaries as specified in Table E-3 in terms of the CIE 1931 definition of
    x and y as specified by ISO 11664-1.
    When the colour_primaries syntax element is not present, the value of
    colour_primaries shall be inferred to be equal to 2 (the chromaticity is
    unspecified or is determined by the application).
   */
  uint8_t colour_primaries = {};

  /*
    transfer_characteristics indicates the opto-electronic transfer
    characteristic of the source picture as specified in Table E-4 as a function
    of a linear optical intensity input Lc with a nominal real-valued range of 0
    to 1.
    When the transfer_characteristics syntax element is not present, the value
    of transfer_characteristics shall be inferred to be equal to 2
    (the transfer characteristics are unspecified or are determined by the
    application).
   */
  uint8_t transfer_characteristics = {};

  uint8_t matrix_coefficients = {};
  bool chroma_loc_info_present_flag = {};
  /*
    The value of chroma_sample_loc_type_top_field and
    chroma_sample_loc_type_bottom_field shall be in the range of 0 to 5,
    inclusive
  */
  uint8_t chroma_sample_loc_type_top_field = {};
  uint8_t chroma_sample_loc_type_bottom_field = {};

  bool scaling_matrix_present = {};
  uint8_t scaling_matrix4x4[6][16] = {};
  uint8_t scaling_matrix8x8[6][64] = {};

  SPSData();
};

struct SEIRecoveryData {
  /*
    recovery_frame_cnt specifies the recovery point of output pictures in output
    order. All decoded pictures in output order are indicated to be correct or
    approximately correct in content starting at the output order position of
    the reference picture having the frame_num equal to the frame_num of the VCL
    NAL units for the current access unit incremented by recovery_frame_cnt in
    modulo MaxFrameNum arithmetic. recovery_frame_cnt shall be in the range of 0
    to MaxFrameNum − 1, inclusive.
  */
  uint32_t recovery_frame_cnt = 0;
  /*
    exact_match_flag indicates whether decoded pictures at and subsequent to the
    specified recovery point in output order derived by starting the decoding
    process at the access unit associated with the recovery point SEI message
    shall be an exact match to the pictures that would be produced by starting
    the decoding process at the location of a previous IDR access unit in the
    NAL unit stream. The value 0 indicates that the match need not be exact and
    the value 1 indicates that the match shall be exact.
  */
  bool exact_match_flag = false;
  /*
    broken_link_flag indicates the presence or absence of a broken link in the
    NAL unit stream at the location of the recovery point SEI message */
  bool broken_link_flag = false;
  /*
    changing_slice_group_idc equal to 0 indicates that decoded pictures are
    correct or approximately correct in content at and subsequent to the
    recovery point in output order when all macroblocks of the primary coded
    pictures are decoded within the changing slice group period
  */
  uint8_t changing_slice_group_idc = 0;
};

class H264 {
 public:
  /* Check if out of band extradata contains a SPS NAL */
  static bool HasSPS(const mozilla::MediaByteBuffer* aExtraData);
  // Extract SPS and PPS NALs from aSample by looking into each NALs.
  // aSample must be in AVCC format.
  static already_AddRefed<mozilla::MediaByteBuffer> ExtractExtraData(
      const mozilla::MediaRawData* aSample);
  // Return true if both extradata are equal.
  static bool CompareExtraData(const mozilla::MediaByteBuffer* aExtraData1,
                               const mozilla::MediaByteBuffer* aExtraData2);

  // Ensure that SPS data makes sense, Return true if SPS data was, and false
  // otherwise. If false, then content will be adjusted accordingly.
  static bool EnsureSPSIsSane(SPSData& aSPS);

  static bool DecodeSPSFromExtraData(const mozilla::MediaByteBuffer* aExtraData,
                                     SPSData& aDest);
  /* Decode SPS NAL RBSP and fill SPSData structure */
  static bool DecodeSPS(const mozilla::MediaByteBuffer* aSPS, SPSData& aDest);

  // If the given aExtraData is valid, return the aExtraData.max_num_ref_frames
  // clamped to be in the range of [4, 16]; otherwise return 4.
  static uint32_t ComputeMaxRefFrames(
      const mozilla::MediaByteBuffer* aExtraData);

  // Return max_num_reorder_frames from a valid SPS in the extradata,
  // otherwise return 0. A non-zero value means decode order can differ from
  // presentation order (B-frames).
  static uint32_t ComputeMaxNumReorderFrames(
      const mozilla::MediaByteBuffer* aExtraData);

  enum class FrameType {
    // IDR is a special iframe, according to the spec T-REC-H.264-202408, 3.69 :
    // "An IDR picture causes the decoding process to mark all reference
    // pictures as "unused for reference" immediately after the decoding of the
    // IDR picture. All coded pictures that follow an IDR picture in decoding
    // order can be decoded without inter prediction from any picture that
    // precedes the IDR picture in decoding order."
    I_FRAME_IDR,
    I_FRAME_OTHER,
    OTHER,
    INVALID,
  };

  // Returns the frame type. Returns I_FRAME if the sample is an IDR
  // (Instantaneous Decoding Refresh) Picture.
  static FrameType GetFrameType(const mozilla::MediaRawData* aSample);

  /* From a NAL, extract the SVC temporal id, per H264 spec Annex G, 7.3.1.1 */
  static Result<int, nsresult> ExtractSVCTemporalId(const uint8_t* aData,
                                                    size_t aLength);

  // Create a dummy extradata, useful to create a decoder and test the
  // capabilities of the decoder.
  static already_AddRefed<mozilla::MediaByteBuffer> CreateExtraData(
      uint8_t aProfile, uint8_t aConstraints, H264_LEVEL aLevel,
      const gfx::IntSize& aSize);
  // aSPS and aPPS include their NAL unit headers.
  static void WriteExtraData(mozilla::MediaByteBuffer* aDestExtraData,
                             const uint8_t aProfile, const uint8_t aConstraints,
                             const uint8_t aLevel,
                             const Span<const uint8_t> aSPS,
                             const Span<const uint8_t> aPPS);

 private:
  friend class SPSNAL;

  /* Extract RAW BYTE SEQUENCE PAYLOAD from NAL content.
     Returns nullptr if invalid content.
     This is compliant to ITU H.264 7.3.1 Syntax in tabular form NAL unit syntax
   */
  static already_AddRefed<mozilla::MediaByteBuffer> DecodeNALUnit(
      const uint8_t* aNAL, size_t aLength);
  static already_AddRefed<mozilla::MediaByteBuffer> EncodeNALUnit(
      const uint8_t* aNAL, size_t aLength);
  static bool vui_parameters(mozilla::BitReader& aBr, SPSData& aDest);
  // Read HRD parameters, all data is ignored.
  static bool hrd_parameters(mozilla::BitReader& aBr);
  static uint8_t NumSPS(const mozilla::MediaByteBuffer* aExtraData);
  // Decode SEI payload and return true if the SEI NAL indicates a recovery
  // point.
  static bool DecodeRecoverySEI(const mozilla::MediaByteBuffer* aSEI,
                                SEIRecoveryData& aDest);
  // Decode NAL Slice payload and return true if its slice type is I slice or SI
  // slice.
  static bool DecodeISlice(const mozilla::MediaByteBuffer* aSlice);
};

/*
 * ISO/IEC 14496-15 : avcC.
 * aligned(8) class AVCDecoderConfigurationRecord {
 *   unsigned int(8) configurationVersion = 1;
 *   unsigned int(8) AVCProfileIndication;
 *   unsigned int(8) profile_compatibility;
 *   unsigned int(8) AVCLevelIndication;
 *   bit(6) reserved = '111111'b;
 *   unsigned int(2) lengthSizeMinusOne;
 *   bit(3) reserved = '111'b;
 *   unsigned int(5) numOfSequenceParameterSets;
 *   for (i = 0; i < numOfSequenceParameterSets; i++) {
 *     unsigned int(16) sequenceParameterSetLength;
 *     bit(8 * sequenceParameterSetLength) sequenceParameterSetNALUnit;
 *   }
 *   unsigned int(8) numOfPictureParameterSets;
 *   for (i = 0; i < numOfPictureParameterSets; i++) {
 *     unsigned int(16) pictureParameterSetLength;
 *     bit(8 * pictureParameterSetLength) pictureParameterSetNALUnit;
 *   }
 *   if (AVCProfileIndication != 66 && AVCProfileIndication != 77 &&
 *       AVCProfileIndication != 88) {
 *     bit(6) reserved = '111111'b;
 *     unsigned int(2) chroma_format;
 *     bit(5) reserved = '11111'b;
 *     unsigned int(3) bit_depth_luma_minus8;
 *     bit(5) reserved = '11111'b;
 *     unsigned int(3) bit_depth_chroma_minus8;
 *     unsigned int(8) numOfSequenceParameterSetExt;
 *     for (i = 0; i < numOfSequenceParameterSetExt; i++) {
 *       unsigned int(16) sequenceParameterSetExtLength;
 *       bit(8 * sequenceParameterSetExtLength) sequenceParameterSetExtNALUnit;
 *     }
 *   }
 * };
 */
struct AVCCConfig final {
 public:
  // The extradata (from sample or directly given) should have a lifetime equal
  // to or longer than AVCCConfig.
  static Result<AVCCConfig, nsresult> Parse(
      const mozilla::MediaRawData* aSample);
  static Result<AVCCConfig, nsresult> Parse(
      const mozilla::MediaByteBuffer* aExtraData);

  uint8_t NALUSize() const { return mLengthSizeMinusOne + 1; }

  uint8_t mConfigurationVersion;
  uint8_t mAVCProfileIndication;
  uint8_t mProfileCompatibility;
  uint8_t mAVCLevelIndication;
  uint8_t mLengthSizeMinusOne;
  nsTArray<H264NALU> mSPSs;
  nsTArray<H264NALU> mPPSs;
  // Following members are optional.
  Maybe<uint8_t> mChromaFormat;
  Maybe<uint8_t> mBitDepthLumaMinus8;
  Maybe<uint8_t> mBitDepthChromaMinus8;
  nsTArray<H264NALU> mSPSExts;

  uint32_t NumSPS() const { return mSPSs.Length(); }
  uint32_t NumPPS() const { return mPPSs.Length(); }
  uint32_t NumSPSExt() const { return mSPSExts.Length(); }

  // This method is used when the attributes of AVCCConfig are modified, and
  // then you want to create a new byte buffer based on the updated attributes.
  // If the updated attributes make the config invalid, this method will return
  // nullptr.
  already_AddRefed<mozilla::MediaByteBuffer> CreateNewExtraData() const;

 private:
  AVCCConfig() = default;
};

}  // namespace mozilla

#endif  // MP4_DEMUXER_H264_H_
