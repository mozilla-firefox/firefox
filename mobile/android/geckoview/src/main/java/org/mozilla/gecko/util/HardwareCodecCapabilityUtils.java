/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.util;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecCapabilities;
import android.media.MediaCodecInfo.VideoCapabilities;
import android.media.MediaCodecList;
import android.os.Build;
import android.util.Log;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import org.mozilla.gecko.annotation.WrapForJNI;

public final class HardwareCodecCapabilityUtils {
  private static final String LOGTAG = "HardwareCodecCapability";

  // List of supported HW VP8 encoders.
  private static final String[] supportedVp8HwEncCodecPrefixes = {"OMX.qcom.", "OMX.Intel."};
  // List of supported HW VP8 decoders.
  private static final String[] supportedVp8HwDecCodecPrefixes = {
    "OMX.qcom.", "OMX.Nvidia.", "OMX.Exynos.", "c2.exynos", "OMX.Intel."
  };
  private static final String VP8_MIME_TYPE = "video/x-vnd.on2.vp8";
  // List of supported HW VP9 codecs.
  private static final String[] supportedVp9HwCodecPrefixes = {
    "OMX.qcom.",
    "OMX.Exynos.",
    "c2.exynos",
    "OMX.allwinner.",
    "OMX.amlogic.",
    "OMX.MTK.",
    "OMX.Nvidia.",
    "OMX.rk."
  };
  private static final String VP9_MIME_TYPE = "video/x-vnd.on2.vp9";
  // List of supported HW AV1/HEVC/H.264 codecs.
  private static final String[] supportedGenericHwCodecPrefixes = {
    "OMX.qcom.",
    "OMX.Intel.",
    "OMX.Exynos.",
    "c2.exynos",
    "OMX.Nvidia",
    "OMX.SEC.",
    "OMX.IMG.",
    "OMX.k3.",
    "OMX.hisi.",
    "OMX.TI.",
    "OMX.MTK.",
    "OMX.allwinner.",
    "OMX.amlogic.",
    "OMX.rk."
  };
  private static final String H264_MIME_TYPE = "video/avc";
  private static final String HEVC_MIME_TYPE = "video/hevc";
  private static final String AV1_MIME_TYPE = "video/av01";
  // NV12 color format supported by QCOM codec, but not declared in MediaCodec -
  // see /hardware/qcom/media/mm-core/inc/OMX_QCOMExtns.h
  private static final int COLOR_QCOM_FORMATYUV420PackedSemiPlanar32m = 0x7FA30C04;
  // Allowable color formats supported by codec - in order of preference.
  private static final int[] supportedColorList = {
    CodecCapabilities.COLOR_FormatYUV420Planar,
    CodecCapabilities.COLOR_FormatYUV420SemiPlanar,
    CodecCapabilities.COLOR_QCOM_FormatYUV420SemiPlanar,
    COLOR_QCOM_FORMATYUV420PackedSemiPlanar32m
  };
  private static final int COLOR_FORMAT_NOT_SUPPORTED = -1;
  private static final String[] adaptivePlaybackBlocklist = {
    "GT-I9300", // S3 (I9300 / I9300I)
    "SCH-I535", // S3
    "SGH-T999", // S3 (T-Mobile)
    "SAMSUNG-SGH-T999", // S3 (T-Mobile)
    "SGH-M919", // S4
    "GT-I9505", // S4
    "GT-I9515", // S4
    "SCH-R970", // S4
    "SGH-I337", // S4
    "SPH-L720", // S4 (Sprint)
    "SAMSUNG-SGH-I337", // S4
    "GT-I9195", // S4 Mini
    "300E5EV/300E4EV/270E5EV/270E4EV/2470EV/2470EE",
    "LG-D605" // LG Optimus L9 II
  };

  private static MediaCodecInfo[] getCodecListWithOldAPI() {
    int numCodecs = 0;
    try {
      numCodecs = MediaCodecList.getCodecCount();
    } catch (final RuntimeException e) {
      Log.e(LOGTAG, "Failed to retrieve media codec count", e);
      return new MediaCodecInfo[numCodecs];
    }

    final MediaCodecInfo[] codecList = new MediaCodecInfo[numCodecs];

    for (int i = 0; i < numCodecs; ++i) {
      final MediaCodecInfo info = MediaCodecList.getCodecInfoAt(i);
      codecList[i] = info;
    }

    return codecList;
  }

  // Return list of all codecs (decode + encode).
  private static MediaCodecInfo[] getCodecList() {
    final MediaCodecInfo[] codecList;
    try {
      final MediaCodecList list = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
      codecList = list.getCodecInfos();
    } catch (final RuntimeException e) {
      Log.e(LOGTAG, "Failed to retrieve media codec support list", e);
      return new MediaCodecInfo[0];
    }
    return codecList;
  }

  // Return list of all decoders.
  private static MediaCodecInfo[] getDecoderInfos() {
    final ArrayList<MediaCodecInfo> decoderList = new ArrayList<MediaCodecInfo>();
    for (final MediaCodecInfo info : getCodecList()) {
      if (!info.isEncoder()) {
        decoderList.add(info);
      }
    }
    return decoderList.toArray(new MediaCodecInfo[0]);
  }

  // Return list of all encoders.
  private static MediaCodecInfo[] getEncoderInfos() {
    final ArrayList<MediaCodecInfo> encoderList = new ArrayList<MediaCodecInfo>();
    for (final MediaCodecInfo info : getCodecList()) {
      if (info.isEncoder()) {
        encoderList.add(info);
      }
    }
    return encoderList.toArray(new MediaCodecInfo[0]);
  }

  // Return list of all decoder-supported MIME types without distinguishing
  // between SW/HW support.
  @WrapForJNI
  public static String[] getDecoderSupportedMimeTypes() {
    final Set<String> mimeTypes = new HashSet<>();
    for (final MediaCodecInfo info : getDecoderInfos()) {
      mimeTypes.addAll(Arrays.asList(info.getSupportedTypes()));
    }
    return mimeTypes.toArray(new String[0]);
  }

  // Return list of all decoder-supported and encoder-supported MIME types,
  // each prefixed with SW or HW indicating software or hardware support for
  // decoders, and SWE or HWE for encoders.
  // Decoders which support a 10-bit profile are additionally listed with
  // SW10B or HW10B. Hardware video decoders additionally report their largest
  // supported width and height with HWMAX<width>,<height>, and are listed with
  // HWADAPT if they are the default decoder for the MIME type and support
  // adaptive playback.
  @WrapForJNI
  public static String[] getSupportedMimeTypesWithAccelInfo() {
    final Set<String> mimeTypes = new HashSet<>();
    addSupportedMimeTypesWithAccelInfo(mimeTypes, getDecoderInfos(), false);
    addSupportedMimeTypesWithAccelInfo(mimeTypes, getEncoderInfos(), true);
    for (final String typeit : mimeTypes) {
      Log.d(LOGTAG, "MIME support: " + typeit);
    }
    return mimeTypes.toArray(new String[0]);
  }

  private static void addSupportedMimeTypesWithAccelInfo(
      final Set<String> aMimeTypes, final MediaCodecInfo[] aInfos, final boolean aIsEncoder) {
    final String swTag = aIsEncoder ? "SWE " : "SW ";
    final String hwTag = aIsEncoder ? "HWE " : "HW ";
    // MediaCodec.createDecoderByType picks the first decoder in list order.
    final Set<String> defaultDecoderMimeTypes = new HashSet<>();

    for (final MediaCodecInfo info : aInfos) {
      for (final String mimeType : info.getSupportedTypes()) {
        final boolean isDefaultDecoder = !aIsEncoder && defaultDecoderMimeTypes.add(mimeType);
        final CodecCapabilities caps = info.getCapabilitiesForType(mimeType);
        final boolean supports10Bit = !aIsEncoder && supports10BitProfile(mimeType, caps);
        if (!isHardwareAccelerated(info, getSupportedHWCodecPrefixes(mimeType, aIsEncoder))) {
          aMimeTypes.add(swTag + mimeType);
          if (supports10Bit) {
            aMimeTypes.add("SW10B " + mimeType);
          }
          continue;
        }
        if (getSupportsYUV420orNV12(caps) != COLOR_FORMAT_NOT_SUPPORTED) {
          aMimeTypes.add(hwTag + mimeType);
          if (supports10Bit) {
            aMimeTypes.add("HW10B " + mimeType);
          }
          if (!aIsEncoder) {
            final MediaCodecInfo.VideoCapabilities video = caps.getVideoCapabilities();
            if (video != null) {
              aMimeTypes.add(
                  "HWMAX"
                      + video.getSupportedWidths().getUpper()
                      + ","
                      + video.getSupportedHeights().getUpper()
                      + " "
                      + mimeType);
            }
            if (isDefaultDecoder && supportsAdaptivePlayback(mimeType, caps)) {
              aMimeTypes.add("HWADAPT " + mimeType);
            }
          }
        }
      }
    }
  }

  private static boolean isHardwareAccelerated(
      final MediaCodecInfo aCodecInfo, final String[] aHwPrefixes) {
    // By public API.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      return aCodecInfo.isHardwareAccelerated();
    }
    // By name.
    if (aHwPrefixes == null) {
      return false;
    }
    for (final String prefix : aHwPrefixes) {
      if (aCodecInfo.getName().startsWith(prefix)) {
        return true;
      }
    }
    return false;
  }

  public static boolean checkSupportsAdaptivePlayback(
      final MediaCodec aCodec, final String aMimeType) {
    try {
      final MediaCodecInfo info = aCodec.getCodecInfo();
      return supportsAdaptivePlayback(aMimeType, info.getCapabilitiesForType(aMimeType));
    } catch (final IllegalArgumentException e) {
      Log.e(LOGTAG, "Retrieve codec information failed", e);
    }
    return false;
  }

  private static boolean supportsAdaptivePlayback(
      final String aMimeType, final CodecCapabilities aCapabilities) {
    return !isAdaptivePlaybackBlocklisted(aMimeType)
        && aCapabilities != null
        && aCapabilities.isFeatureSupported(CodecCapabilities.FEATURE_AdaptivePlayback);
  }

  // See Bug1360626 and
  // https://codereview.chromium.org/1869103002 for details.
  private static boolean isAdaptivePlaybackBlocklisted(final String aMimeType) {
    Log.d(LOGTAG, "The device ModelID is " + Build.MODEL);
    if (!aMimeType.equals("video/avc") && !aMimeType.equals("video/avc1")) {
      return false;
    }

    if (!Build.MANUFACTURER.toLowerCase(Locale.ROOT).equals("samsung")) {
      return false;
    }

    for (final String model : adaptivePlaybackBlocklist) {
      if (Build.MODEL.startsWith(model)) {
        return true;
      }
    }
    return false;
  }

  // Check if a given MIME Type has HW decode or encode support.
  public static boolean getHWCodecCapability(final String aMimeType, final boolean aIsEncoder) {
    for (final MediaCodecInfo info : getCodecList()) {
      if (info.isEncoder() != aIsEncoder) {
        continue;
      }
      String name = null;
      for (final String mimeType : info.getSupportedTypes()) {
        if (mimeType.equals(aMimeType)) {
          name = info.getName();
          break;
        }
      }
      if (name == null) {
        continue; // No HW support in this codec; try the next one.
      }
      Log.d(LOGTAG, "Found candidate" + (aIsEncoder ? " encoder " : " decoder ") + name);

      if (!isHardwareAccelerated(info, getSupportedHWCodecPrefixes(aMimeType, aIsEncoder))) {
        continue;
      }

      // Check if codec supports either yuv420 or nv12.
      final CodecCapabilities capabilities = info.getCapabilitiesForType(aMimeType);
      for (final int colorFormat : capabilities.colorFormats) {
        Log.v(LOGTAG, "   Color: 0x" + Integer.toHexString(colorFormat));
      }
      for (final MediaCodecInfo.CodecProfileLevel pl : capabilities.profileLevels) {
        Log.v(
            LOGTAG,
            "   Profile: 0x"
                + Integer.toHexString(pl.profile)
                + "/Level=0x"
                + Integer.toHexString(pl.level));
      }
      final int codecColorFormat = getSupportsYUV420orNV12(capabilities);
      if (codecColorFormat != COLOR_FORMAT_NOT_SUPPORTED) {
        Log.d(
            LOGTAG,
            "Found target"
                + (aIsEncoder ? " encoder " : " decoder ")
                + name
                + ". Color: 0x"
                + Integer.toHexString(codecColorFormat));
        return true;
      }
    }
    // No HW codec.
    return false;
  }

  // Check if codec supports YUV420 or NV12
  private static int getSupportsYUV420orNV12(final CodecCapabilities aCodecCaps) {
    for (final int supportedColorFormat : supportedColorList) {
      for (final int codecColorFormat : aCodecCaps.colorFormats) {
        if (codecColorFormat == supportedColorFormat) {
          return codecColorFormat;
        }
      }
    }
    return COLOR_FORMAT_NOT_SUPPORTED;
  }

  // Check if MIME type string has HW prefix (encode or decode, VP8, VP9, and H264)
  private static String[] getSupportedHWCodecPrefixes(
      final String aMimeType, final boolean aIsEncoder) {
    if (aMimeType.equals(H264_MIME_TYPE)
        || aMimeType.equals(HEVC_MIME_TYPE)
        || aMimeType.equals(AV1_MIME_TYPE)) {
      return supportedGenericHwCodecPrefixes;
    }
    if (aMimeType.equals(VP9_MIME_TYPE)) {
      return supportedVp9HwCodecPrefixes;
    }
    if (aMimeType.equals(VP8_MIME_TYPE)) {
      return aIsEncoder ? supportedVp8HwEncCodecPrefixes : supportedVp8HwDecCodecPrefixes;
    }
    return null;
  }

  @WrapForJNI
  public static boolean hasHWVP8(final boolean aIsEncoder) {
    return getHWCodecCapability(VP8_MIME_TYPE, aIsEncoder);
  }

  @WrapForJNI
  public static boolean hasHWVP9(final boolean aIsEncoder) {
    return getHWCodecCapability(VP9_MIME_TYPE, aIsEncoder);
  }

  @WrapForJNI
  public static boolean hasHWAV1(final boolean aIsEncoder) {
    return getHWCodecCapability(AV1_MIME_TYPE, aIsEncoder);
  }

  @WrapForJNI
  public static boolean hasHWHEVC(final boolean aIsEncoder) {
    return getHWCodecCapability(HEVC_MIME_TYPE, aIsEncoder);
  }

  private static final HashMap<String, Integer> sMaxEncoderPixels = new HashMap<>();

  // The largest frame size, in pixels, any encoder for aMimeType accepts, or 0
  // when unknown.
  @WrapForJNI
  public static synchronized int getMaxEncoderPixels(final String aMimeType) {
    final Integer cached = sMaxEncoderPixels.get(aMimeType);
    if (cached != null) {
      return cached;
    }
    int max = 0;
    for (final MediaCodecInfo info : getCodecList()) {
      if (!info.isEncoder()) {
        continue;
      }
      final CodecCapabilities capabilities;
      try {
        capabilities = info.getCapabilitiesForType(aMimeType);
      } catch (final IllegalArgumentException e) {
        continue;
      }
      final VideoCapabilities video = capabilities.getVideoCapabilities();
      if (video == null) {
        continue;
      }
      max = Math.max(max, widestFramePixels(video));
      max = Math.max(max, tallestFramePixels(video));
      Log.d(LOGTAG, "Encoder " + info.getName() + " max pixels " + max);
    }
    sMaxEncoderPixels.put(aMimeType, max);
    return max;
  }

  // The widest frame aVideo accepts, in pixels, or 0 if unknown.
  private static int widestFramePixels(final VideoCapabilities aVideo) {
    try {
      final int width = aVideo.getSupportedWidths().getUpper();
      return supportedAlignedPixels(aVideo, width, aVideo.getSupportedHeightsFor(width).getUpper());
    } catch (final IllegalArgumentException e) {
      return 0;
    }
  }

  // The tallest frame aVideo accepts, in pixels, or 0 if unknown.
  private static int tallestFramePixels(final VideoCapabilities aVideo) {
    try {
      final int height = aVideo.getSupportedHeights().getUpper();
      return supportedAlignedPixels(
          aVideo, aVideo.getSupportedWidthsFor(height).getUpper(), height);
    } catch (final IllegalArgumentException e) {
      return 0;
    }
  }

  // aWidth x aHeight rounded down to aVideo's alignment, in pixels, or 0 if
  // aVideo doesn't accept that size. The width and height ranges alone are only
  // a bounding box, so the pair is verified with isSizeSupported.
  private static int supportedAlignedPixels(
      final VideoCapabilities aVideo, final int aWidth, final int aHeight) {
    final int width = aWidth - aWidth % aVideo.getWidthAlignment();
    final int height = aHeight - aHeight % aVideo.getHeightAlignment();
    if (!aVideo.isSizeSupported(width, height)) {
      return 0;
    }
    return width * height;
  }

  @WrapForJNI
  public static boolean hasHWH264(final boolean aIsEncoder) {
    return getHWCodecCapability(H264_MIME_TYPE, aIsEncoder);
  }

  @WrapForJNI(calledFrom = "gecko")
  public static boolean hasHWH264() {
    return getHWCodecCapability(H264_MIME_TYPE, true)
        && getHWCodecCapability(H264_MIME_TYPE, false);
  }

  private static boolean supports10BitProfile(
      final String aMimeType, final CodecCapabilities aCaps) {
    for (final MediaCodecInfo.CodecProfileLevel pl : aCaps.profileLevels) {
      if ((aMimeType.equals(H264_MIME_TYPE)
              && pl.profile == MediaCodecInfo.CodecProfileLevel.AVCProfileHigh10)
          || (aMimeType.equals(VP9_MIME_TYPE) && is10BitVP9Profile(pl.profile))
          || (aMimeType.equals(HEVC_MIME_TYPE) && is10BitHEVCProfile(pl.profile))
          || (aMimeType.equals(AV1_MIME_TYPE) && is10BitAV1Profile(pl.profile))) {
        return true;
      }
    }
    return false;
  }

  private static boolean is10BitVP9Profile(final int profile) {
    if ((profile == MediaCodecInfo.CodecProfileLevel.VP9Profile2)
        || (profile == MediaCodecInfo.CodecProfileLevel.VP9Profile3)
        || (profile == MediaCodecInfo.CodecProfileLevel.VP9Profile2HDR)
        || (profile == MediaCodecInfo.CodecProfileLevel.VP9Profile3HDR)) {
      return true;
    }

    return Build.VERSION.SDK_INT >= 29
        && ((profile == MediaCodecInfo.CodecProfileLevel.VP9Profile2HDR10Plus)
            || (profile == MediaCodecInfo.CodecProfileLevel.VP9Profile3HDR10Plus));
  }

  private static boolean is10BitHEVCProfile(final int profile) {
    if (profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10) {
      return true;
    }
    // API 24+
    if (Build.VERSION.SDK_INT < 29) {
      return profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10HDR10;
    }
    // API 29+
    return (profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10HDR10)
        || (profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10HDR10Plus);
  }

  private static boolean is10BitAV1Profile(final int profile) {
    if (Build.VERSION.SDK_INT < 29) {
      // Be conservative when we cannot get supported profile.
      return false;
    }
    return (profile == MediaCodecInfo.CodecProfileLevel.AV1ProfileMain10)
        || (profile == MediaCodecInfo.CodecProfileLevel.AV1ProfileMain10HDR10)
        || (profile == MediaCodecInfo.CodecProfileLevel.AV1ProfileMain10HDR10Plus);
  }
}
