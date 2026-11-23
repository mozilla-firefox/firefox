const { AppConstants } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// In each list of tests below, test file types that are not supported should
// be ignored. To make sure tests respect that, we include a file of type
// "bogus/duh" in each list.

// Make sure to not touch navigator in here, since we want to push prefs that
// will affect the APIs it exposes, but the set of exposed APIs is determined
// when Navigator.prototype is created.  So if we touch navigator before pushing
// the prefs, the APIs it exposes will not take those prefs into account.  We
// work around this by using a navigator object from a different global for our
// UA string testing.
var gManifestNavigatorSource = document.documentElement.appendChild(
  document.createElement("iframe")
);
gManifestNavigatorSource.style.display = "none";
function manifestNavigator() {
  return gManifestNavigatorSource.contentWindow.navigator;
}

// Similarly, use a <video> element from a different global for canPlayType or
// other feature testing.  If we used one from our global and did so before our
// prefs are pushed, then we'd instantiate HTMLMediaElement.prototype before the
// prefs are pushed and APIs we expect to be on that object would not be there.
function manifestVideo() {
  return gManifestNavigatorSource.contentDocument.createElement("video");
}

// Need to get the server url composed with ip:port instead of mochi.test.
// Since we will provide the url to Exoplayer which cannot recognize the domain
// name "mochi.test".
let serverUrl = SpecialPowers.Services.prefs.getCharPref(
  "media.hls.server.url"
);
var gHLSTests = [
  {
    name: serverUrl + "/bipbop_4x3_variant.m3u8",
    type: "audio/x-mpegurl",
    duration: 20.0,
  },
];

// These are small test files, good for just seeing if something loads. We
// really only need one test file per backend here.
var gSmallTests = [
  { name: "small-shot.ogg", type: "audio/ogg", duration: 0.276 },
  { name: "small-shot.m4a", type: "audio/mp4", duration: 0.29 },
  { name: "small-shot.mp3", type: "audio/mpeg", duration: 0.27 },
  { name: "small-shot-mp3.mp4", type: "audio/mp4; codecs=mp3", duration: 0.34 },
  { name: "small-shot.flac", type: "audio/flac", duration: 0.197 },
  { name: "r11025_s16_c1-short.wav", type: "audio/x-wav", duration: 0.37 },
  {
    name: "seek-short.webm",
    type: "video/webm",
    width: 320,
    height: 240,
    duration: 0.23,
  },
  {
    name: "vp9-short.webm",
    type: "video/webm",
    width: 320,
    height: 240,
    duration: 0.2,
  },
  {
    name: "detodos-short.opus",
    type: "audio/ogg; codecs=opus",
    duration: 0.22,
  },
  {
    name: "gizmo-short.mp4",
    type: "video/mp4",
    width: 560,
    height: 320,
    duration: 0.27,
  },
  { name: "flac-s24.flac", type: "audio/flac", duration: 4.04 },
  { name: "bogus.duh", type: "bogus/duh" },
  {
    name: "avc-2bytes-nalu.mp4",
    type: "video/mp4",
    width: 560,
    height: 320,
    duration: 0.27,
  },
  {
    name: "1967133.mp4",
    type: "video/mp4",
    width: 1080,
    height: 1080,
    duration: 0.512,
  },
];

var gFrameCountTests = [
  { name: "bipbop.mp4", type: "video/mp4", totalFrameCount: 297 },
  { name: "gizmo.mp4", type: "video/mp4", totalFrameCount: 166 },
  { name: "seek-short.webm", type: "video/webm", totalFrameCount: 8 },
  { name: "seek.webm", type: "video/webm", totalFrameCount: 120 },
  { name: "av1.mp4", type: "video/mp4", totalFrameCount: 24 },
];

gSmallTests = gSmallTests.concat([
  { name: "sample.3gp", type: "video/3gpp", duration: 4.933 },
  { name: "sample.3g2", type: "video/3gpp2", duration: 4.933 },
]);

// Used by test_bug654550.html, for videoStats preference
var gVideoTests = [
  {
    name: "seek-short.webm",
    type: "video/webm",
    width: 320,
    height: 240,
    duration: 0.23,
  },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Temp hack for trackIDs and captureStream() -- bug 1215769
var gLongerTests = [
  {
    name: "seek.webm",
    type: "video/webm",
    width: 320,
    height: 240,
    duration: 3.966,
  },
  {
    name: "gizmo.mp4",
    type: "video/mp4",
    width: 560,
    height: 320,
    duration: 5.56,
  },
];

// Used by test_progress to ensure we get the correct progress information
// during resource download.
var gProgressTests = [
  { name: "r11025_u8_c1.wav", type: "audio/x-wav", duration: 1.0, size: 11069 },
  { name: "big-short.wav", type: "audio/x-wav", duration: 1.11, size: 12366 },
  { name: "seek-short.webm", type: "video/webm", duration: 0.23, size: 19267 },
  { name: "gizmo-short.mp4", type: "video/mp4", duration: 0.27, size: 29905 },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Used by test_played.html
var gPlayedTests = [
  { name: "big-short.wav", type: "audio/x-wav", duration: 1.11 },
  { name: "seek-short.webm", type: "video/webm", duration: 0.23 },
  { name: "gizmo-short.mp4", type: "video/mp4", duration: 0.27 },
  { name: "owl-short.mp3", type: "audio/mpeg", duration: 0.52 },
  { name: "very-short.mp3", type: "audio/mpeg", duration: 0.07 },
  // Disable vbr.mp3 to see if it reduces the error of AUDCLNT_E_CPUUSAGE_EXCEEDED.
  // See bug 1110922 comment 26.
  //{ name:"vbr.mp3", type:"audio/mpeg", duration:10.0 },
  { name: "bug495794.ogg", type: "audio/ogg", duration: 0.3 },
];

if (
  manifestNavigator().userAgent.includes("Windows") &&
  manifestVideo().canPlayType('video/mp4; codecs="avc1.64000c"')
) {
  gPlayedTests = gPlayedTests.concat(
    { name: "red-46x48.mp4", type: "video/mp4", duration: 1.0 },
    { name: "red-48x46.mp4", type: "video/mp4", duration: 1.0 }
  );
}

// Used by test_mozLoadFrom.  Need one test file per decoder backend, plus
// anything for testing clone-specific bugs.
var cloneKey = Math.floor(Math.random() * 100000000);
var gCloneTests = [
  // vp9.webm is more like 4s, so if you load this twice you'll get an unexpected duration
  {
    name:
      "dynamic_resource.sjs?key=" +
      cloneKey +
      "&res1=seek-short.webm&res2=vp9.webm",
    type: "video/webm",
    duration: 0.23,
  },
];

// Used by test_play_twice.  Need one test file per decoder backend, plus
// anything for testing bugs that occur when replying a played file.
var gReplayTests = gSmallTests.concat([
  { name: "bug533822.ogg", type: "audio/ogg" },
]);

// Used by test_paused_after_ended. Need one test file per decoder backend, plus
// anything for testing bugs that occur when replying a played file.
var gPausedAfterEndedTests = gSmallTests.concat([
  { name: "r11025_u8_c1.wav", type: "audio/x-wav", duration: 1.0 },
  { name: "small-shot.ogg", type: "video/ogg", duration: 0.276 },
]);

// Test the mozHasAudio property, and APIs that detect different kinds of
// tracks
var gTrackTests = [
  {
    name: "big-short.wav",
    type: "audio/x-wav",
    duration: 1.11,
    size: 12366,
    hasAudio: true,
    hasVideo: false,
  },
  {
    name: "seek-short.webm",
    type: "video/webm",
    duration: 0.23,
    size: 19267,
    hasAudio: false,
    hasVideo: true,
  },
  {
    name: "flac-s24.flac",
    type: "audio/flac",
    duration: 4.04,
    hasAudio: true,
    hasVideo: false,
  },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Used by any media recorder test. Need one test file per decoder backend
// currently supported by the media encoder.
var gMediaRecorderTests = [
  // Duration should be greater than 500ms because we will record 2
  // time slices (250ms per slice)
  {
    name: "detodos-recorder-test.opus",
    type: "audio/ogg; codecs=opus",
    duration: 0.62,
  },
];

// Used by video media recorder tests
var gMediaRecorderVideoTests = [
  {
    name: "seek-short.webm",
    type: "video/webm",
    width: 320,
    height: 240,
    duration: 0.23,
  },
];

// These are files that we want to make sure we can play through.  We can
// also check metadata.  Put files of the same type together in this list so if
// something crashes we have some idea of which backend is responsible.
// Used by test_playback, which expects no error event and one ended event.
var gPlayTests = [
  // Test playback of a WebM file with vp9 video
  { name: "vp9cake-short.webm", type: "video/webm", duration: 1.0 },
  // 8-bit samples
  { name: "r11025_u8_c1.wav", type: "audio/x-wav", duration: 1.0 },
  // 8-bit samples, file is truncated
  { name: "r11025_u8_c1_trunc.wav", type: "audio/x-wav", duration: 1.8 },
  // file has trailing non-PCM data
  { name: "r11025_s16_c1_trailing.wav", type: "audio/x-wav", duration: 1.0 },
  // file with list chunk
  { name: "r16000_u8_c1_list.wav", type: "audio/x-wav", duration: 4.2 },
  // file with 2 extra bytes of metadata
  {
    name: "16bit_wave_extrametadata.wav",
    type: "audio/x-wav",
    duration: 1.108,
  },
  // IEEE float wave file
  { name: "wavedata_float.wav", type: "audio/x-wav", duration: 1.0 },
  // 24-bit samples
  { name: "wavedata_s24.wav", type: "audio/x-wav", duration: 1.0 },
  // aLaw compressed wave file
  { name: "wavedata_alaw.wav", type: "audio/x-wav", duration: 1.0 },
  // uLaw compressed wave file
  { name: "wavedata_ulaw.wav", type: "audio/x-wav", duration: 1.0 },
  // Data length 0xFFFFFFFF
  { name: "bug1301226.wav", type: "audio/x-wav", duration: 0.003673 },
  // Data length 0xFFFFFFFF and odd chunk lengths.
  { name: "bug1301226-odd.wav", type: "audio/x-wav", duration: 0.003673 },

  // Encoded with vorbis beta1, includes unusually sized codebooks
  { name: "beta-phrasebook.ogg", type: "audio/ogg", duration: 4.01 },
  // Small file, only 1 frame with audio only.
  { name: "bug520493.ogg", type: "audio/ogg", duration: 0.458 },
  // Small file with vorbis comments with 0 length values and names.
  { name: "bug520500.ogg", type: "audio/ogg", duration: 0.123 },

  { name: "bug495794.ogg", type: "audio/ogg", duration: 0.3 },
  { name: "audio-overhang.ogg", type: "video/ogg", duration: 2.3 },
  { name: "video-overhang.ogg", type: "video/ogg", duration: 3.966 },

  // bug461281.ogg with the middle second chopped out.
  { name: "audio-gaps.ogg", type: "audio/ogg", duration: 2.208 },

  // Test playback/metadata work after a redirect
  {
    name: "redirect.sjs?domain=mochi.test:8888&file=vp9.webm",
    type: "video/webm",
  },

  // Test playback of a webm file
  { name: "seek-short.webm", type: "video/webm", duration: 0.23 },

  // Test playback of a webm file with 'matroska' doctype
  // TODO : re-enable this in bug 1985419 after MKV is supported.
  // { name: "bug1377278.webm", type: "video/webm", duration: 4.0 },

  // Test playback of a WebM file with non-zero start time.
  { name: "split.webm", type: "video/webm", duration: 1.967 },

  // Test playback of a WebM file with resolution changes.
  { name: "resolution-change.webm", type: "video/webm", duration: 6.533 },

  // The following webm files test cases where the webm metadata dimensions do
  // not match those in the stream. See bug 1695033 for more info.

  // Reference file with correct dimensions (webm metadata matches stream
  // resolution).
  { name: "bipbop_short_vp8.webm", type: "video/webm", duration: 1.011 },

  // The webm resolution is greater in both dimensions than the in stream
  // resolution.
  {
    name: "bipbop_short_pixel_metadata_bigger_than_in_stream_vp8.webm",
    type: "video/webm",
    duration: 1.011,
  },

  // The webm resolution is correct for height, but is narrower than the stream
  // resolution.
  {
    name: "bipbop_short_pixel_metadata_narrower_than_in_stream_vp8.webm",
    type: "video/webm",
    duration: 1.011,
  },

  // The webm resolution is smaller in both dimensions than the in stream
  // resolution.
  {
    name: "bipbop_short_pixel_metadata_smaller_than_in_stream_vp8.webm",
    type: "video/webm",
    duration: 1.011,
  },

  // End of webm dimension clashing files.

  // A really short, low sample rate, single channel file. This tests whether
  // we can handle playing files when only push very little audio data to the
  // hardware.
  { name: "spacestorm-1000Hz-100ms.ogg", type: "audio/ogg", duration: 0.099 },

  // Opus data in an ogg container
  {
    name: "detodos-short.opus",
    type: "audio/ogg; codecs=opus",
    duration: 0.22,
    contentDuration: 0.2135,
  },
  // Opus data in a webm container
  {
    name: "detodos-short.webm",
    type: "audio/webm; codecs=opus",
    duration: 0.26,
    contentDuration: 0.2535,
  },
  // Opus in webm channel mapping=2 sample file
  {
    name: "opus-mapping2.webm",
    type: "audio/webm; codecs=opus",
    duration: 10.01,
    contentDuration: 9.99,
  },
  { name: "bug1066943.webm", type: "audio/webm; codecs=opus", duration: 1.383 },

  // Multichannel Opus in an ogg container
  { name: "test-1-mono.opus", type: "audio/ogg; codecs=opus", duration: 1.044 },
  {
    name: "test-2-stereo.opus",
    type: "audio/ogg; codecs=opus",
    duration: 2.925,
  },
  { name: "test-3-LCR.opus", type: "audio/ogg; codecs=opus", duration: 4.214 },
  { name: "test-4-quad.opus", type: "audio/ogg; codecs=opus", duration: 6.234 },
  { name: "test-5-5.0.opus", type: "audio/ogg; codecs=opus", duration: 7.558 },
  { name: "test-6-5.1.opus", type: "audio/ogg; codecs=opus", duration: 10.333 },
  { name: "test-7-6.1.opus", type: "audio/ogg; codecs=opus", duration: 11.69 },
  { name: "test-8-7.1.opus", type: "audio/ogg; codecs=opus", duration: 13.478 },

  {
    name: "gizmo-short.mp4",
    type: "video/mp4",
    duration: 0.27,
    contentDuration: 0.267,
  },
  // Two adjacent colr boxes in the video sample entry (bug 2044320).
  { name: "two-colr.mp4", type: "video/mp4", duration: 0.512 },
  // Test playback of a MP4 file with a non-zero start time (and audio starting
  // a second later).
  { name: "bipbop-lateaudio.mp4", type: "video/mp4" },
  // Opus in MP4 channel mapping=0 sample file (content shorter due to preskip)
  {
    name: "opus-sample.mp4",
    type: "audio/mp4; codecs=opus",
    duration: 10.92,
    contentDuration: 10.09,
  },
  // Opus in MP4 channel mapping=2 sample file
  { name: "opus-mapping2.mp4", type: "audio/mp4; codecs=opus", duration: 10.0 },

  { name: "small-shot.m4a", type: "audio/mp4", duration: 0.29 },
  { name: "small-shot.mp3", type: "audio/mpeg", duration: 0.27 },
  { name: "owl.mp3", type: "audio/mpeg", duration: 3.343 },
  // owl.mp3 as above, but with something funny going on in the ID3v2 tag
  // that caused DirectShow to fail.
  { name: "owl-funny-id3.mp3", type: "audio/mpeg", duration: 3.343 },
  // owl.mp3 as above, but with something even funnier going on in the ID3v2 tag
  // that caused DirectShow to fail.
  { name: "owl-funnier-id3.mp3", type: "audio/mpeg", duration: 3.343 },
  // One second of silence with ~140KB of ID3 tags. Usually when the first MP3
  // frame is at such a high offset into the file, MP3FrameParser will give up
  // and report that the stream is not MP3. However, it does not count ID3 tags
  // in that offset. This test case makes sure that ID3 exclusion holds.
  { name: "huge-id3.mp3", type: "audio/mpeg", duration: 1.0 },
  // Half a second file of a sine with a large ID3v2 tag, followed by an ID3v1
  // tag. The ID3v1 tags should be at the end of the file, but software usually
  // play it anyway.
  { name: "id3v1afterlongid3v2.mp3", type: "audio/mpeg", duration: 0.5 },
  // An VBR file with a padding value that is greater than an mp3 packet, and
  // also subsequent packets after the theoretical EOF computed from metadata,
  // to test padding trimming edge cases.
  {
    name: "padding-spanning-multiple-packets.mp3",
    type: "audio/mpeg",
  },
  // A truncated VBR MP3 with just enough frames to keep most decoders happy.
  // The Xing header reports the length of the file to be around 10 seconds, but
  // there is really only one second worth of data. We want MP3FrameParser to
  // trust the header, so this should be reported as 10 seconds.
  {
    name: "vbr-head.mp3",
    type: "audio/mpeg",
    duration: 10.0,
    contentDuration: 1.019,
  },

  // A flac file where the STREAMINFO block was removed.
  // It is necessary to parse the file to find an audio frame instead.
  { name: "flac-noheader-s16.flac", type: "audio/flac", duration: 4.0 },
  { name: "flac-s24.flac", type: "audio/flac", duration: 4.04 },
  {
    name: "flac-sample.mp4",
    type: "audio/mp4; codecs=flac",
    duration: 4.95,
    contentDuration: 5.03,
  },
  // A file that has no codec delay at the container level, but has a delay at
  // the codec level.
  {
    name: "no-container-codec-delay.webm",
    type: "video/webm",
  },
  // A file that has a codec delay at a container level of 0, but as a delay at
  // the codec level that is non-zero.
  { name: "invalid-preskip.webm", type: "audio/webm; codecs=opus" },

  // Invalid file
  { name: "bogus.duh", type: "bogus/duh", duration: Number.NaN },
];

const win32 =
  SpecialPowers.Services.appinfo.OS == "WINNT" &&
  !SpecialPowers.Services.appinfo.is64Bit;
if (!win32) {
  gPlayTests.push({ name: "av1.mp4", type: "video/mp4", duration: 1.0 });
}

var gMKVtests = [
  // ffmpeg -f lavfi -i "sine=frequency=440:duration=1" \
  // -c:a aac -b:a 128k output-aac.mkv
  {
    name: "output_aac.mkv",
    type: 'audio/matroska; codecs="mp4a.40.2"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i "smptebars=size=1280x720:rate=30:duration=1" \
  // -c:v libx264 -pix_fmt yuv420p output-avc.mkv
  {
    name: "output_avc.mkv",
    type: 'video/matroska; codecs="avc1.42C01F"',
    duration: 1.0,
  },
  // ffmpeg \
  // -f lavfi -i "testsrc2=size=1920x1080:rate=30:duration=1" \
  // -f lavfi -i "sine=frequency=440:duration=1" \
  // -c:v libx264 -profile:v high -level:v 4.1 -pix_fmt yuv420p -g 60 \
  // -c:a aac -ar 44100 -b:a 128k output_avc_hl41_1080p_aaclc_44100.mkv
  {
    name: "output_avc_hl41_1080p_aaclc_44100.mkv",
    type: 'video/matroska; codecs="avc1.640029, mp4a.40.2"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a libvorbis output_vorbis.mkv
  {
    name: "output_vorbis.mkv",
    type: 'audio/matroska; codecs="vorbis"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a libopus output_opus.mkv
  {
    name: "output_opus.mkv",
    type: 'audio/matroska; codecs="opus"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a libopus -ac 6 -mapping_family 1 output_opus_surround.mkv
  {
    name: "output_opus_surround.mkv",
    type: 'audio/matroska; codecs="opus"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -t 1 -c:v libvpx output_vp8.mkv
  {
    name: "output_vp8.mkv",
    type: 'video/matroska; codecs="vp8"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -t 1 -c:v libvpx-vp9 -pix_fmt yuv420p output_vp9.mkv
  {
    name: "output_vp9.mkv",
    type: 'video/matroska; codecs="vp9"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -t 1 -c:v libaom-av1 -pix_fmt yuv420p output_av1.mkv
  {
    name: "output_av1.mkv",
    type: 'video/matroska; codecs="av1"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a libmp3lame output_mp3.mkv
  {
    name: "output_mp3.mkv",
    type: 'audio/matroska; codecs="mp3"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a flac output_flac.mkv
  {
    name: "output_flac.mkv",
    type: 'audio/matroska; codecs="flac"',
    duration: 1.0,
  },
  // ffmpeg -f lavfi -i sine=frequency=1000:duration=1 -c:a pcm_s16le output_pcm.mkv
  {
    name: "output_pcm.mkv",
    type: 'audio/matroska; codecs="pcm"',
    duration: 1.0,
  },
];

// HEVC file can only be tested on the gpu worker which has hardware decoder
// support. This pref will only be set on the `media-gpu` task.
if (
  SpecialPowers.Services.prefs.getBoolPref(
    "media.hardware-video-decoding.force-enabled",
    0
  )
) {
  // ffmpeg -f lavfi -i testsrc=size=320x240:rate=30 -t 1 -c:v libx265 -pix_fmt yuv420p output_hevc.mkv
  gMKVtests.push(
    {
      name: "output_hevc.mkv",
      type: 'video/matroska; codecs="hvc1.1.6.L63.B0"',
      duration: 1.0,
    }
    // TODO : enable this, this will currently fail on MacOS worker on CI.
    // {
    //   name: "partial_support_hevc_ac3.mkv",
    //   type: 'video/matroska; codecs="hvc1.1.6.L63.B0"',
    //   duration: 0.1,
    // }
  );
}

// Parital support test cases, these files only have one supported track, another
// unsupported track should be ignored. All files are generated by ffmpeg.
gMKVtests.push(
  // valid video + invalid audio
  {
    name: "partial_support_av1_ac3.mkv",
    type: 'video/matroska; codecs="av1"',
    duration: 0.1,
  },
  {
    name: "partial_support_avc_ac3.mkv",
    type: 'video/matroska; codecs="avc1.640029, mp4a.40.2"',
    duration: 0.1,
  },
  {
    name: "partial_support_vp8_ac3.mkv",
    type: 'video/matroska; codecs="vp8"',
    duration: 0.1,
  },
  {
    name: "partial_support_vp9_ac3.mkv",
    type: 'video/matroska; codecs="vp9"',
    duration: 0.1,
  },
  // valid audio + invalid video
  {
    name: "partial_support_theora_aac.mkv",
    type: 'audio/matroska; codecs="mp4a.40.2"',
    duration: 0.1,
  },
  {
    name: "partial_support_theora_opus.mkv",
    type: 'audio/matroska; codecs="opus"',
    duration: 0.1,
  },
  {
    name: "partial_support_theora_vorbis.mkv",
    type: 'audio/matroska; codecs="vorbis"',
    duration: 0.1,
  }
);

// AAC files with different sample rates. We add these here as some are added
// conditionally.
gPlayTests.push(
  {
    name: "bipbop_audio_aac_8k.mp4",
    type: "audio/mp4",
    duration: 1.06,
  },
  {
    name: "bipbop_audio_aac_22.05k.mp4",
    type: "audio/mp4",
    duration: 1.06,
  },
  {
    name: "bipbop_audio_aac_44.1k.mp4",
    type: "audio/mp4",
    duration: 1.06,
  },
  {
    name: "bipbop_audio_aac_48k.mp4",
    type: "audio/mp4",
    duration: 1.06,
  }
);

// Disabled for now, will be reenabled in 1982387
// if (
//   AppConstants.isPlatformAndVersionAtLeast("win", "11") ||
//   AppConstants.isPlatformAndVersionAtLeast("macosx", "10.15") ||
//   // Android 9
//   AppConstants.isPlatformAndVersionAtLeast("android", "28")
// ) {
//   gPlayTests.push({
//     name: "sine-3s-xhe-aac-44khz-mono.mp4",
//     type: 'audio/mp4; codecs="mp4a.40.42"',
//     duration: 3.0,
//   });
// }

if (AppConstants.platform != "win") {
  // Windows WMF decoder doesn't do >48K everywhere. See bug 1698639.
  gPlayTests.push(
    {
      name: "bipbop_audio_aac_88.2k.mp4",
      type: "audio/mp4",
      duration: 1.06,
    },
    {
      name: "bipbop_audio_aac_96k.mp4",
      type: "audio/mp4",
      duration: 1.06,
    }
  );
}

// ambisonics.mp4 causes intermittents, so we conditionally add it until we fix
// the root cause.
const skipAmbisonics =
  // Bug 1484451 - skip on mac debug
  (AppConstants.platform == "macosx" && AppConstants.DEBUG) ||
  // Bug 1483259 - skip on linux64 opt
  (AppConstants.platform == "linux" &&
    !AppConstants.DEBUG &&
    SpecialPowers.Services.appinfo.is64Bit);
if (!skipAmbisonics) {
  // Ambisonics AAC, requires AAC extradata to be set when creating decoder (see bug 1431169)
  // Also test 4.0 decoding.
  gPlayTests.push({
    name: "ambisonics.mp4",
    type: "audio/mp4",
    duration: 16.48,
  });
}

var gSeekToNextFrameTests = [
  // Test playback of a WebM file with vp9 video
  { name: "vp9-short.webm", type: "video/webm", duration: 0.2 },
  { name: "vp9cake-short.webm", type: "video/webm", duration: 1.0 },

  // Test playback/metadata work after a redirect
  {
    name: "redirect.sjs?domain=mochi.test:8888&file=vp9.webm",
    type: "video/webm",
  },
  // Test playback of a webm file
  { name: "seek-short.webm", type: "video/webm", duration: 0.23 },
  // Test playback of a WebM file with non-zero start time.
  { name: "split.webm", type: "video/webm", duration: 1.967 },

  { name: "gizmo-short.mp4", type: "video/mp4", duration: 0.27 },

  // Test playback of a MP4 file with a non-zero start time (and audio starting
  // a second later).
  { name: "bipbop-lateaudio.mp4", type: "video/mp4" },
];

// A file for each type we can support.
var gSnifferTests = [
  { name: "big.wav", type: "audio/x-wav", duration: 9.278982, size: 102444 },
  { name: "seek.webm", type: "video/webm", duration: 3.966, size: 215529 },
  { name: "gizmo.mp4", type: "video/mp4", duration: 5.56, size: 383631 },
  // A mp3 file with id3 tags.
  { name: "id3tags.mp3", type: "audio/mpeg", duration: 0.28, size: 3530 },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Files that contain resolution changes
var gResolutionChangeTests = [
  { name: "resolution-change.webm", type: "video/webm", duration: 6.533 },
];

// Files we must reject as invalid.
var gInvalidTests = [
  { name: "invalid-m0c0.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-m0c3.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-m1c0.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-m1c9.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-m2c0.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-m2c1.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-cmap-short.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-cmap-s0c0.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-cmap-s0c2.opus", type: "audio/ogg; codecs=opus" },
  { name: "invalid-cmap-s1c2.opus", type: "audio/ogg; codecs=opus" },
];

var gInvalidPlayTests = [
  { name: "invalid-excess_discard.webm", type: "audio/webm; codecs=opus" },
  { name: "invalid-excess_neg_discard.webm", type: "audio/webm; codecs=opus" },
  { name: "invalid-neg_discard.webm", type: "audio/webm; codecs=opus" },
  {
    name: "invalid-discard_on_multi_blocks.webm",
    type: "audio/webm; codecs=opus",
  },
];

// Files to check different cases of ogg skeleton information.
// multiple-bos-more-header-fields.ogg
// - Skeleton v3, w/ Content-Type,Role,Name,Language,Title for vorbis
// audio-gaps-short.ogg
// - No skeleton, but vorbis
var gMultitrackInfoOggPlayList = [
  {
    name: "multiple-bos-more-header-fileds.ogg",
    type: "video/ogg",
    duration: 0.431,
  },
  { name: "audio-gaps-short.ogg", type: "audio/ogg", duration: 0.5 },
];
// Pre-parsed results of gMultitrackInfoOggPlayList.
var gOggTrackInfoResults = {
  "multiple-bos-more-header-fileds.ogg": {
    audio_id: "1",
    audio_kind: "main",
    audio_language: "",
    audio_label: "",
    video_id: "2",
    video_kind: "main",
    video_language: "",
    video_label: "",
  },
  "audio-gaps-short.ogg": {
    audio_id: "1",
    audio_kind: "main",
    audio_language: "",
    audio_label: "",
  },
};

// Returns a promise that resolves to a function that converts
// relative paths to absolute, to test loading files from file: URIs.
// Optionally checks whether the file actually exists on disk at the location
// we've specified.
function makeAbsolutePathConverter() {
  const url = SimpleTest.getTestFileURL("chromeHelper.js");
  const script = SpecialPowers.loadChromeScript(url);
  return new Promise(resolve => {
    script.addMessageListener("media-test:cwd", cwd => {
      if (!cwd) {
        ok(false, "Failed to find path to test files");
      }

      resolve((path, mustExist) => {
        // android mochitest doesn't support file://
        if (manifestNavigator().appVersion.includes("Android")) {
          return path;
        }

        const { Ci, Cc } = SpecialPowers;
        var f = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
        f.initWithPath(cwd);
        var split = path.split("/");
        for (var i = 0; i < split.length; ++i) {
          f.append(split[i]);
        }
        if (mustExist && !f.exists()) {
          ok(false, "We expected '" + path + "' to exist, but it doesn't!");
        }
        return f.path;
      });
    });
    script.sendAsyncMessage("media-test:getcwd");
  });
}

// Returns true if two TimeRanges are equal, false otherwise
function range_equals(r1, r2) {
  if (r1.length != r2.length) {
    return false;
  }
  for (var i = 0; i < r1.length; i++) {
    if (r1.start(i) != r2.start(i) || r1.end(i) != r2.end(i)) {
      return false;
    }
  }
  return true;
}

// These are URIs to files that we use to check that we don't leak any state
// or other information such that script can determine stuff about a user's
// environment. Used by test_info_leak.
function makeInfoLeakTests() {
  return makeAbsolutePathConverter().then(fileUriToSrc => [
    {
      type: "audio/x-wav",
      src: fileUriToSrc("tests/dom/media/test/r11025_s16_c1.wav", true),
    },
    {
      type: "audio/x-wav",
      src: fileUriToSrc("tests/dom/media/test/404.wav", false),
    },
    {
      type: "audio/ogg",
      src: fileUriToSrc("tests/dom/media/test/404.ogg", false),
    },
    {
      type: "video/webm",
      src: fileUriToSrc("tests/dom/media/test/seek.webm", true),
    },
    {
      type: "video/webm",
      src: fileUriToSrc("tests/dom/media/test/404.webm", false),
    },
    {
      type: "audio/x-wav",
      src: "http://localhost/404.wav",
    },
    {
      type: "video/webm",
      src: "http://localhost/404.webm",
    },
    {
      type: "video/ogg",
      src: "http://example.com/tests/dom/media/test/test_info_leak.html",
    },
    {
      type: "audio/ogg",
      src: "http://example.com/tests/dom/media/test/test_info_leak.html",
    },
  ]);
}

// These are files that must fire an error during load or playback, and do not
// cause a crash. Used by test_playback_errors, which expects one error event
// and no ended event. Put files of the same type together in this list so if
// something crashes we have some idea of which backend is responsible.
var gErrorTests = [
  { name: "bogus.wav", type: "audio/x-wav" },
  { name: "bug501279.ogg", type: "audio/ogg" },
  { name: "bug604067.webm", type: "video/webm" },
  { name: "bug1535980.webm", type: "video/webm" },
  { name: "bug1799787.webm", type: "video/webm" },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Playing this file errors out after receiving "loadedmetadata", we still want
// to check the duration in "onerror" and make sure it is still available.
var gDurationTests = [{ name: "bug604067.webm", duration: 6.076 }];

// These are files that have nontrivial duration and are useful for seeking within.
var gSeekTests = [
  { name: "r11025_s16_c1.wav", type: "audio/x-wav", duration: 1.0 },
  { name: "audio.wav", type: "audio/x-wav", duration: 0.031247 },
  { name: "seek.webm", type: "video/webm", duration: 3.966 },
  { name: "sine.webm", type: "audio/webm", duration: 4.001 },
  { name: "split.webm", type: "video/webm", duration: 1.967 },
  { name: "detodos.opus", type: "audio/ogg; codecs=opus", duration: 2.9135 },
  { name: "gizmo.mp4", type: "video/mp4", duration: 5.56 },
  {
    name: "/tests/dom/media/webaudio/test/half-a-second-1ch-44100-aac-afconvert.mp4",
    type: 'audio/mp4; codecs="mp4a.40.2"',
    duration: 0.5,
  },
  { name: "owl.mp3", type: "audio/mpeg", duration: 3.343 },
  { name: "bogus.duh", type: "bogus/duh", duration: 123 },
];

var gFastSeekTests = [
  {
    name: "gizmo.mp4",
    type: "video/mp4",
    keyframes: [0, 1.0, 2.0, 3.0, 4.0, 5.0],
  },
  // Note: Not all keyframes in the file are actually referenced in the Cues in this file.
  { name: "seek.webm", type: "video/webm", keyframes: [0, 0.8, 1.6, 2.4, 3.2] },
];

// These files are WebMs without cues. They're seekable within their buffered
// ranges. If work renders WebMs fully seekable these files should be moved
// into gSeekTests
var gCuelessWebMTests = [
  { name: "no-cues.webm", type: "video/webm", duration: 3.967 },
];

// These are files that are non seekable, due to problems with the media,
// for example broken or missing indexes.
var gUnseekableTests = [{ name: "bogus.duh", type: "bogus/duh" }];

var androidVersion = -1; // non-Android platforms
if (
  manifestNavigator().userAgent.includes("Mobile") ||
  manifestNavigator().userAgent.includes("Tablet")
) {
  androidVersion = SpecialPowers.Services.sysinfo.getProperty("version");
}

function getAndroidVersion() {
  return androidVersion;
}

// These are files suitable for using with a "new Audio" constructor.
var gAudioTests = [
  { name: "adts.aac", type: "audio/mp4", duration: 1.37 },
  { name: "r11025_s16_c1.wav", type: "audio/x-wav", duration: 1.0 },
  { name: "sound.ogg", type: "audio/ogg" },
  { name: "owl.mp3", type: "audio/mpeg", duration: 3.343 },
  { name: "small-shot.m4a", type: "audio/mp4", duration: 0.29 },
  { name: "bogus.duh", type: "bogus/duh", duration: 123 },
  { name: "empty_size.mp3", type: "audio/mpeg", duration: 2.235 },
];

// These files ensure our handling of 404 errors is consistent across the
// various backends.
var g404Tests = [
  { name: "404.wav", type: "audio/x-wav" },
  { name: "404.oga", type: "audio/ogg" },
  { name: "404.webm", type: "video/webm" },
  { name: "bogus.duh", type: "bogus/duh" },
];

// These are files suitable for testing various decoder failures that are
// expected to fire MEDIA_ERR_DECODE.  Used by test_decode_error, which expects
// an error and emptied event, and no loadedmetadata or ended event.
var gDecodeErrorTests = [
  // Valid files with unsupported codecs
  { name: "r11025_msadpcm_c1.wav", type: "audio/x-wav" },
  { name: "dirac.ogg", type: "video/ogg" },
  // Invalid files
  { name: "bogus.wav", type: "audio/x-wav" },
  { name: "invalid-crop-wrap.webm", type: "video/webm" },

  { name: "bogus.duh", type: "bogus/duh" },
];

// These are files that are used for media fragments tests
var gFragmentTests = [
  { name: "big.wav", type: "audio/x-wav", duration: 9.278982, size: 102444 },
];

// Used by test_chaining.html. The |links| attributes is the number of links in
// this file that we should be able to play.
var gChainingTests = [
  // Vorbis and Opus chained file. They have user comments |index=n| where `n`
  // is the index of this segment in the file, 0 indexed.
  { name: "chain.ogg", type: "audio/ogg", links: 4 },
  { name: "chain.opus", type: "audio/ogg; codec=opus", links: 4 },
  // Those files are chained files with a different number of channels in each
  // part. This is not supported and should stop playing after the first part.
  { name: "variable-channel.ogg", type: "audio/ogg", links: 1 },
  { name: "variable-channel.opus", type: "audio/ogg; codec=opus", links: 1 },
  // Those files are chained files with a different sample rate in each
  // part. This is not supported and should stop playing after the first part.
  { name: "variable-samplerate.ogg", type: "audio/ogg", links: 1 },
  // Opus decoding in Firefox outputs 48 kHz PCM despite having a different
  // original sample rate, so we can safely play Opus chained media that have
  // different samplerate accross links.
  { name: "variable-samplerate.opus", type: "audio/ogg; codec=opus", links: 2 },
  // An opus file that has two links, with a different preskip value for each
  // link. We should be able to play both links.
  { name: "variable-preskip.opus", type: "audio/ogg; codec=opus", links: 2 },
  { name: "bogus.duh", type: "bogus/duh" },
];

// Videos with an aspect ratio. Used for testing that displaying frames
// on a canvas works correctly in the case of non-standard aspect ratios.
// See bug 874897 for an example.
var gAspectRatioTests = [
  { name: "VID_0001.ogg", type: "video/ogg", duration: 19.966 },
];

// These are files with non-trivial tag sets.
// Used by test_metadata.html.
var gMetadataTests = [
  // Ogg Vorbis files
  {
    name: "detodos.opus",
    tags: {
      title: "De todos. Para todos.",
      artist: "Mozilla.org",
    },
  },
  { name: "sound.ogg", tags: {} },
  {
    name: "small-shot.ogg",
    tags: {
      title: "Pew SFX",
    },
  },
  {
    name: "badtags.ogg",
    tags: {
      // We list only the valid tags here, and verify
      // the invalid ones are filtered out.
      title: "Invalid comments test file",
      empty: "",
      "": "empty",
      "{- [(`!@\"#$%^&')] -}": "valid tag name, surprisingly",
      // The file also includes the following invalid tags.
      // "A description with no separator is a common problem.",
      // "雨":"Likely, but an invalid key (non-ascii).",
      // "not\nval\x1fid":"invalid tag name",
      // "not~valid":"this isn't a valid name either",
      // "not-utf-8":"invalid sequences: \xff\xfe\xfa\xfb\0eol"
    },
  },
  {
    name: "wave_metadata.wav",
    tags: {
      name: "Track Title",
      artist: "Artist Name",
      comments: "Comments",
    },
  },
  {
    name: "wave_metadata_utf8.wav",
    tags: {
      name: "歌曲名稱",
      artist: "作曲者",
      comments: "註解",
    },
  },
  {
    name: "wave_metadata_unknown_tag.wav",
    tags: {
      name: "Track Title",
      comments: "Comments",
    },
  },
  {
    name: "wave_metadata_bad_len.wav",
    tags: {
      name: "Track Title",
      artist: "Artist Name",
      comments: "Comments",
    },
  },
  {
    name: "wave_metadata_bad_no_null.wav",
    tags: {
      name: "Track Title",
      artist: "Artist Name",
      comments: "Comments!!",
    },
  },
  {
    name: "wave_metadata_bad_utf8.wav",
    tags: {
      name: "歌曲名稱",
      comments: "註解",
    },
  },
  { name: "wavedata_u8.wav", tags: {} },
];

// Now Fennec doesn't support flac, so only test it on non-android platforms.
if (getAndroidVersion() < 0) {
  gMetadataTests = gMetadataTests.concat([
    {
      name: "flac-s24.flac",
      tags: {
        ALBUM: "Seascapes",
        TITLE: "(La Mer) - II. Jeux de vagues. Allegro",
        COMPOSER: "Debussy, Claude",
        TRACKNUMBER: "2/9",
        DISCNUMBER: "1/1",
        encoder: "Lavf57.41.100",
      },
    },
  ]);
}

// Test files for Encrypted Media Extensions
var gEMETests = [
  {
    name: "vp9 in mp4",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="vp9.0"',
        fragments: ["short-vp9-encrypted-video.mp4"],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: ["short-aac-encrypted-audio.mp4"],
      },
    ],
    keys: {
      "2cdb0ed6119853e7850671c3e9906c3c": "808B9ADAC384DE1E4F56140F4AD76194",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 0.47,
  },
  {
    name: "video-only with 2 keys",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop-cenc-videoinit.mp4",
          "bipbop-cenc-video1.m4s",
          "bipbop-cenc-video2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d03": "7e5733337e5733337e5733337e573333",
      "7e571d047e571d047e571d047e571d04": "7e5744447e5744447e5744447e574444",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 1.6,
  },
  {
    name: "video-only with 2 keys, CORS",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop-cenc-videoinit.mp4",
          "bipbop-cenc-video1.m4s",
          "bipbop-cenc-video2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d03": "7e5733337e5733337e5733337e573333",
      "7e571d047e571d047e571d047e571d04": "7e5744447e5744447e5744447e574444",
    },
    sessionType: "temporary",
    sessionCount: 1,
    crossOrigin: true,
    duration: 1.6,
  },
  {
    name: "audio&video tracks, both with all keys",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop-cenc-audioinit.mp4",
          "bipbop-cenc-audio1.m4s",
          "bipbop-cenc-audio2.m4s",
          "bipbop-cenc-audio3.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop-cenc-videoinit.mp4",
          "bipbop-cenc-video1.m4s",
          "bipbop-cenc-video2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d03": "7e5733337e5733337e5733337e573333",
      "7e571d047e571d047e571d047e571d04": "7e5744447e5744447e5744447e574444",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "audio&video tracks, both with all keys, CORS",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop-cenc-audioinit.mp4",
          "bipbop-cenc-audio1.m4s",
          "bipbop-cenc-audio2.m4s",
          "bipbop-cenc-audio3.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop-cenc-videoinit.mp4",
          "bipbop-cenc-video1.m4s",
          "bipbop-cenc-video2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d03": "7e5733337e5733337e5733337e573333",
      "7e571d047e571d047e571d047e571d04": "7e5744447e5744447e5744447e574444",
    },
    sessionType: "temporary",
    sessionCount: 2,
    crossOrigin: true,
    duration: 1.6,
  },
  {
    name: "400x300 audio&video tracks, each with its key",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_300_215kbps-cenc-audio-key1-init.mp4",
          "bipbop_300_215kbps-cenc-audio-key1-1.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-2.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-3.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_300_215kbps-cenc-video-key1-init.mp4",
          "bipbop_300_215kbps-cenc-video-key1-1.m4s",
          "bipbop_300_215kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "640x480@624kbps audio&video tracks, each with its key",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_624kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_624kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e"',
        fragments: [
          "bipbop_480_624kbps-cenc-video-key1-init.mp4",
          "bipbop_480_624kbps-cenc-video-key1-1.m4s",
          "bipbop_480_624kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "640x480@959kbps audio&video tracks, each with its key",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_959kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_959kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e"',
        fragments: [
          "bipbop_480_959kbps-cenc-video-key1-init.mp4",
          "bipbop_480_959kbps-cenc-video-key1-1.m4s",
          "bipbop_480_959kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "640x480 then 400x300, same key (1st) per track",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_624kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_624kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e,avc1.4d4015"',
        fragments: [
          "bipbop_480_624kbps-cenc-video-key1-init.mp4",
          "bipbop_480_624kbps-cenc-video-key1-1.m4s",
          "bipbop_300_215kbps-cenc-video-key1-init.mp4",
          "bipbop_300_215kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "640x480 then 400x300, same key (2nd) per track",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_624kbps-cenc-audio-key2-init.mp4",
          "bipbop_480_624kbps-cenc-audio-key2-1.m4s",
          "bipbop_480_624kbps-cenc-audio-key2-2.m4s",
          "bipbop_480_624kbps-cenc-audio-key2-3.m4s",
          "bipbop_480_624kbps-cenc-audio-key2-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e,avc1.4d4015"',
        fragments: [
          "bipbop_480_624kbps-cenc-video-key2-init.mp4",
          "bipbop_480_624kbps-cenc-video-key2-1.m4s",
          "bipbop_300_215kbps-cenc-video-key2-init.mp4",
          "bipbop_300_215kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
      "7e571d047e571d047e571d047e571d22": "7e5744447e5744447e5744447e574422",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "640x480 with 1st keys then 400x300 with 2nd keys",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_624kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_624kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e,avc1.4d4015"',
        fragments: [
          "bipbop_480_624kbps-cenc-video-key1-init.mp4",
          "bipbop_480_624kbps-cenc-video-key1-1.m4s",
          "bipbop_300_215kbps-cenc-video-key2-init.mp4",
          "bipbop_300_215kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "400x300 with 1st keys then 640x480 with 2nd keys",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_300_215kbps-cenc-audio-key1-init.mp4",
          "bipbop_300_215kbps-cenc-audio-key1-1.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-2.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-3.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015,avc1.4d401e"',
        fragments: [
          "bipbop_300_215kbps-cenc-video-key1-init.mp4",
          "bipbop_300_215kbps-cenc-video-key1-1.m4s",
          "bipbop_480_624kbps-cenc-video-key2-init.mp4",
          "bipbop_480_624kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "640x480@959kbps with 1st keys then 640x480@624kbps with 2nd keys",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_959kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_959kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_959kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e"',
        fragments: [
          "bipbop_480_959kbps-cenc-video-key1-init.mp4",
          "bipbop_480_959kbps-cenc-video-key1-1.m4s",
          "bipbop_480_624kbps-cenc-video-key2-init.mp4",
          "bipbop_480_624kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "640x480@624kbps with 1st keys then 640x480@959kbps with 2nd keys",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_480_624kbps-cenc-audio-key1-init.mp4",
          "bipbop_480_624kbps-cenc-audio-key1-1.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-2.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-3.m4s",
          "bipbop_480_624kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d401e"',
        fragments: [
          "bipbop_480_624kbps-cenc-video-key1-init.mp4",
          "bipbop_480_624kbps-cenc-video-key1-1.m4s",
          "bipbop_480_959kbps-cenc-video-key2-init.mp4",
          "bipbop_480_959kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "400x300 with presentation size 533x300",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_300wp_227kbps-cenc-audio-key1-init.mp4",
          "bipbop_300wp_227kbps-cenc-audio-key1-1.m4s",
          "bipbop_300wp_227kbps-cenc-audio-key1-2.m4s",
          "bipbop_300wp_227kbps-cenc-audio-key1-3.m4s",
          "bipbop_300wp_227kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_300wp_227kbps-cenc-video-key1-init.mp4",
          "bipbop_300wp_227kbps-cenc-video-key1-1.m4s",
          "bipbop_300wp_227kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "400x300 as-is then 400x300 presented as 533x300",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_300_215kbps-cenc-audio-key1-init.mp4",
          "bipbop_300_215kbps-cenc-audio-key1-1.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-2.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-3.m4s",
          "bipbop_300_215kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_300_215kbps-cenc-video-key1-init.mp4",
          "bipbop_300_215kbps-cenc-video-key1-1.m4s",
          "bipbop_300wp_227kbps-cenc-video-key1-init.mp4",
          "bipbop_300wp_227kbps-cenc-video-key1-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "400x225",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_225w_175kbps-cenc-audio-key1-init.mp4",
          "bipbop_225w_175kbps-cenc-audio-key1-1.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-2.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-3.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.64000d"',
        fragments: [
          "bipbop_225w_175kbps-cenc-video-key1-init.mp4",
          "bipbop_225w_175kbps-cenc-video-key1-1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "640x360",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_360w_253kbps-cenc-audio-key1-init.mp4",
          "bipbop_360w_253kbps-cenc-audio-key1-1.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-2.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-3.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.64001e"',
        fragments: [
          "bipbop_360w_253kbps-cenc-video-key1-init.mp4",
          "bipbop_360w_253kbps-cenc-video-key1-1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "400x225 then 640x360",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_225w_175kbps-cenc-audio-key1-init.mp4",
          "bipbop_225w_175kbps-cenc-audio-key1-1.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-2.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-3.m4s",
          "bipbop_225w_175kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.64000d,avc1.64001e"',
        fragments: [
          "bipbop_225w_175kbps-cenc-video-key1-init.mp4",
          "bipbop_225w_175kbps-cenc-video-key1-1.m4s",
          "bipbop_360w_253kbps-cenc-video-key2-init.mp4",
          "bipbop_360w_253kbps-cenc-video-key2-1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    name: "640x360 then 640x480",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_360w_253kbps-cenc-audio-key1-init.mp4",
          "bipbop_360w_253kbps-cenc-audio-key1-1.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-2.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-3.m4s",
          "bipbop_360w_253kbps-cenc-audio-key1-4.m4s",
        ],
      },
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.64001e,avc1.4d401e"',
        fragments: [
          "bipbop_360w_253kbps-cenc-video-key1-init.mp4",
          "bipbop_360w_253kbps-cenc-video-key1-1.m4s",
          "bipbop_480_624kbps-cenc-video-key2-init.mp4",
          "bipbop_480_624kbps-cenc-video-key2-2.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d037e571d037e571d037e571d11": "7e5733337e5733337e5733337e573311",
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
      "7e571d037e571d037e571d037e571d12": "7e5733337e5733337e5733337e573312",
    },
    sessionType: "temporary",
    sessionCount: 3,
    duration: 1.6,
  },
  {
    // File generated with shaka packager:
    // packager-osx --enable_raw_key_encryption --keys label=:key_id=7e571d047e571d047e571d047e571d21:key=7e5744447e5744447e5744447e574421 --segment_duration 1 --clear_lead 0 in=test-flac.mp4,stream=audio,output=flac-sample-cenc.mp4
    name: "flac in mp4 clearkey",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="flac"',
        fragments: ["flac-sample-cenc.mp4"],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 2.05,
  },
  {
    // File generated with shaka packager:
    // packager-osx --enable_raw_key_encryption --keys label=:key_id=7e571d047e571d047e571d047e571d21:key=7e5744447e5744447e5744447e574421 --segment_duration 1 --clear_lead 0 in=test-opus.mp4,stream=audio,output=opus-sample-cenc.mp4
    name: "opus in mp4 clearkey",
    tracks: [
      {
        name: "audio",
        type: 'audio/mp4; codecs="opus"',
        fragments: ["opus-sample-cenc.mp4"],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 1.98,
  },
  // ffmpeg -i bipbop.mp4 -t 00:00:02 -c:v libaom-av1 bipbop_av1.mp4
  // packager-linux-x64 in=bipbop_av1.mp4,stream=video,out=bipbop-clearkey-video-av1.mp4 --enable_raw_key_encryption --keys label=:key_id=8b5df745ad84145b5617c33116e35a67:key=bddfd35dd9be033ee73bc18bc1885056 --clear_lead 0
  {
    name: "MP4 av1 video clearkey",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="av1"',
        fragments: ["bipbop-clearkey-video-av1.mp4"],
      },
    ],
    keys: {
      // "keyid" : "key"
      "8b5df745ad84145b5617c33116e35a67": "bddfd35dd9be033ee73bc18bc1885056",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 2.0,
    skipTests: ["waitingforkey"],
  },
  // ffmpeg -i bipbop.mp4 -t 00:00:02 -c:v libaom-av1 bipbop_av1.webm
  // packager-linux-x64 in=bipbop_av1.webm,stream=video,out=bipbop-clearkey-video-av1.webm --enable_raw_key_encryption --keys label=:key_id=8b5df745ad84145b5617c33116e35a67:key=bddfd35dd9be033ee73bc18bc1885056 --clear_lead 0
  {
    name: "WebM av1 video clearkey",
    tracks: [
      {
        name: "video",
        type: 'video/webm; codecs="av1"',
        fragments: ["bipbop-clearkey-video-av1.webm"],
      },
    ],
    keys: {
      // "keyid" : "key"
      "8b5df745ad84145b5617c33116e35a67": "bddfd35dd9be033ee73bc18bc1885056",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 2.0,
    skipTests: ["waitingforkey"],
  },
  {
    name: "WebM vorbis audio & vp8 video clearkey",
    tracks: [
      {
        name: "audio",
        type: 'audio/webm; codecs="vorbis"',
        fragments: ["bipbop_360w_253kbps-clearkey-audio.webm"],
      },
      {
        name: "video",
        type: 'video/webm; codecs="vp8"',
        fragments: ["bipbop_360w_253kbps-clearkey-video-vp8.webm"],
      },
    ],
    keys: {
      // "keyid" : "key"
      f1f3ee1790527e9de47217d43835f76a: "97b9ddc459c8d5ff23c1f2754c95abe8",
      "8b5df745ad84145b5617c33116e35a67": "bddfd35dd9be033ee73bc18bc1885056",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "WebM vorbis audio & vp9 video clearkey",
    tracks: [
      {
        name: "audio",
        type: 'audio/webm; codecs="vorbis"',
        fragments: ["bipbop_360w_253kbps-clearkey-audio.webm"],
      },
      {
        name: "video",
        type: 'video/webm; codecs="vp9"',
        fragments: ["bipbop_360w_253kbps-clearkey-video-vp9.webm"],
      },
    ],
    keys: {
      // "keyid" : "key"
      f1f3ee1790527e9de47217d43835f76a: "97b9ddc459c8d5ff23c1f2754c95abe8",
      eedf63a94fa7c398ee094f123a4ee709: "973b679a746c82f3acdb856b30e9378e",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 1.6,
  },
  {
    name: "WebM vorbis audio & vp9 video clearkey with subsample encryption",
    tracks: [
      {
        name: "audio",
        type: 'audio/webm; codecs="vorbis"',
        fragments: ["sintel-short-clearkey-subsample-encrypted-audio.webm"],
      },
      {
        name: "video",
        type: 'video/webm; codecs="vp9"',
        fragments: ["sintel-short-clearkey-subsample-encrypted-video.webm"],
      },
    ],
    keys: {
      // "keyid" : "key"
      "2cdb0ed6119853e7850671c3e9906c3c": "808B9ADAC384DE1E4F56140F4AD76194",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.0,
  },
  {
    // Files adapted from testcase for bug 1560092. See bug 1630381 for a
    // detailed explanation on how they were adapted.
    name: "avc3 h264 video in mp4 using clearkey cenc encryption",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc3.640015"',
        fragments: [
          "big-buck-bunny-cenc-avc3-init.mp4",
          "big-buck-bunny-cenc-avc3-1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "10000000100010001000100000000001": "3A2A1B68DD2BD9B2EEB25E84C4776668",
    },
    sessionType: "temporary",
    sessionCount: 1,
    duration: 2.08,
  },
  // The following cbcs files are created using shaka-packager using commands like
  // ./packager-win.exe 'in=bipbop_2s.mp4,stream=audio,init_segment=bipbop_cbcs_1_9_audio_init.mp4,segment_template=bipbop_cbcs_1_9_audio_$Number$.m4s' \
  // 'in=bipbop_2s.mp4,stream=video,init_segment=bipbop_cbcs_1_9_video_init.mp4,segment_template=bipbop_cbcs_1_9_video_$Number$.m4s' --protection_scheme cbcs \
  // --enable_raw_key_encryption --keys label=:key_id=7e571d047e571d047e571d047e571d21:key=7e5744447e5744447e5744447e574421 --iv 11223344556677889900112233445566 \
  // --clear_lead 0 --crypt_byte_block 1 --skip_byte_block 9
  // See bug 1726202 for more details on their creation.
  {
    name: "mp4 h264 + aac clearkey cbcs 1:9 pattern",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_cbcs_1_9_video_init.mp4",
          "bipbop_cbcs_1_9_video_1.m4s",
        ],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_cbcs_1_9_audio_init.mp4",
          "bipbop_cbcs_1_9_audio_1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.04,
  },
  {
    name: "mp4 h264 + aac clearkey cbcs 5:5 pattern",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_cbcs_5_5_video_init.mp4",
          "bipbop_cbcs_5_5_video_1.m4s",
        ],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_cbcs_5_5_audio_init.mp4",
          "bipbop_cbcs_5_5_audio_1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.04,
  },
  {
    name: "mp4 h264 + aac clearkey cbcs 10:0 pattern",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_cbcs_10_0_video_init.mp4",
          "bipbop_cbcs_10_0_video_1.m4s",
        ],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_cbcs_10_0_audio_init.mp4",
          "bipbop_cbcs_10_0_audio_1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.04,
  },
  {
    name: "mp4 h264 + aac clearkey cbcs 7:7 pattern",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_cbcs_7_7_video_init.mp4",
          "bipbop_cbcs_7_7_video_1.m4s",
        ],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_cbcs_7_7_audio_init.mp4",
          "bipbop_cbcs_7_7_audio_1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.04,
  },
  {
    name: "mp4 h264 + aac clearkey cbcs 9:8 pattern",
    tracks: [
      {
        name: "video",
        type: 'video/mp4; codecs="avc1.4d4015"',
        fragments: [
          "bipbop_cbcs_9_8_video_init.mp4",
          "bipbop_cbcs_9_8_video_1.m4s",
        ],
      },
      {
        name: "audio",
        type: 'audio/mp4; codecs="mp4a.40.2"',
        fragments: [
          "bipbop_cbcs_9_8_audio_init.mp4",
          "bipbop_cbcs_9_8_audio_1.m4s",
        ],
      },
    ],
    keys: {
      // "keyid" : "key"
      "7e571d047e571d047e571d047e571d21": "7e5744447e5744447e5744447e574421",
    },
    sessionType: "temporary",
    sessionCount: 2,
    duration: 2.04,
  },
];

var gEMENonMSEFailTests = [
  {
    name: "short-cenc.mp4",
    audioType: 'audio/mp4; codecs="mp4a.40.2"',
    videoType: 'video/mp4; codecs="avc1.64000d"',
    duration: 0.47,
  },
];

// Test files that are supposed to loop seamlessly when played back.
var gSeamlessLoopingTests = [
  // MP4 files dont't loop seamlessly yet, the seeking logic seeks to 0, not the
  // actual first packet, resulting in incorrect decoding.
  // See bug 1817989
  // { name: "sin-441-1s-44100-fdk_aac.mp4", type: "audio/mp4" },
  // { name: "sin-441-1s-44100-afconvert.mp4", type: "audio/mp4" },
  // { name: "sin-441-1s-44100.ogg", type: "audio/vorbis" },
  // { name: "sin-441-1s-44100.opus", type: "audio/opus" },
  { name: "sin-441-1s-44100-lame.mp3", type: "audio/mpeg" },
  { name: "sin-441-1s-44100.flac", type: "audio/flac" },
];

// These are files that are used for video decode suspend in
// background tabs tests.
var gDecodeSuspendTests = [
  { name: "gizmo.mp4", type: "video/mp4", duration: 5.56 },
  { name: "gizmo-noaudio.mp4", type: "video/mp4", duration: 5.56 },
  { name: "gizmo.webm", type: 'video/webm; codecs="vp9,opus"', duration: 5.56 },
  {
    name: "gizmo-noaudio.webm",
    type: 'video/webm; codecs="vp9"',
    duration: 5.56,
  },
];

// These are video files with hardware-decodable formats and longer
// durations that are looped while we check telemetry for macOS video
// low power mode.
var gVideoLowPowerTests = [
  { name: "gizmo.mp4", type: "video/mp4", duration: 5.56 },
];

function checkMetadata(msg, e, test) {
  if (test.width) {
    is(e.videoWidth, test.width, msg + " video width");
  }
  if (test.height) {
    is(e.videoHeight, test.height, msg + " video height");
  }
  if (test.duration) {
    ok(
      Math.abs(e.duration - test.duration) < 0.1,
      msg + " duration (" + e.duration + ") should be around " + test.duration
    );
  }
  is(
    !!test.keys,
    SpecialPowers.do_lookupGetter(e, "isEncrypted").apply(e),
    msg + " isEncrypted should be true if we have decryption keys"
  );
}

// Returns the first test from candidates array which we can play with the
// installed video backends.
function getPlayableVideo(candidates) {
  var resources = getPlayableVideos(candidates);
  if (resources.length) {
    return resources[0];
  }
  return null;
}

function getPlayableVideos(candidates) {
  var v = manifestVideo();
  return candidates.filter(function (x) {
    return /^video/.test(x.type) && v.canPlayType(x.type);
  });
}

function getPlayableAudio(candidates) {
  var v = manifestVideo();
  var resources = candidates.filter(function (x) {
    return /^audio/.test(x.type) && v.canPlayType(x.type);
  });
  if (resources.length) {
    return resources[0];
  }
  return null;
}

// Returns the type of element that should be created for the given mimetype.
function getMajorMimeType(mimetype) {
  if (/^video/.test(mimetype)) {
    return "video";
  }
  return "audio";
}

// removeNodeAndSource() below intentionally aborts any in-flight media
// resource fetch. If a play() promise is still pending at that point, it
// rejects with AbortError; callers aren't expected to catch that themselves,
// so silence just this specific rejection instead of letting it show up as a
// JS error in logs.
window.addEventListener("unhandledrejection", function (event) {
  if (
    event.reason instanceof DOMException &&
    event.reason.name == "AbortError" &&
    event.reason.message.startsWith(
      "The fetching process for the media resource was aborted"
    )
  ) {
    event.preventDefault();
  }
});

// Force releasing decoder to avoid timeout in waiting for decoding resource.
function removeNodeAndSource(n) {
  n.remove();
  // reset |srcObject| first since it takes precedence over |src|.
  n.srcObject = null;
  n.removeAttribute("src");
  n.load();
  while (n.firstChild) {
    n.firstChild.remove();
  }
}

function once(target, name, cb) {
  var p = new Promise(function (resolve) {
    target.addEventListener(
      name,
      function () {
        resolve();
      },
      { once: true }
    );
  });
  if (cb) {
    p.then(cb);
  }
  return p;
}

/**
 * @param {HTMLMediaElement} video target of interest.
 * @param {string} eventName the event to wait on.
 * @returns {Promise} A promise that is resolved when event happens.
 */
function nextEvent(video, eventName) {
  return new Promise(function (resolve) {
    let f = function (event) {
      video.removeEventListener(eventName, f);
      resolve(event);
    };
    video.addEventListener(eventName, f);
  });
}

function TimeStamp(token) {
  function pad(x) {
    return x < 10 ? "0" + x : x;
  }
  var now = new Date();
  var ms = now.getMilliseconds();
  var time =
    "[" +
    pad(now.getHours()) +
    ":" +
    pad(now.getMinutes()) +
    ":" +
    pad(now.getSeconds()) +
    "." +
    ms +
    "]" +
    // eslint-disable-next-line no-nested-ternary
    (ms < 10 ? "  " : ms < 100 ? " " : "");
  return token ? time + " " + token : time;
}

function Log(token, msg) {
  info(TimeStamp(token) + " " + msg);
}

// Number of tests to run in parallel.
var PARALLEL_TESTS = 2;

// Disable parallel test for media engine tests, see more info in bug 1840914.
if (
  SpecialPowers.Services.prefs.getIntPref("media.wmf.media-engine.enabled", 0)
) {
  PARALLEL_TESTS = 1;
}

// Prefs to set before running tests.  Use this to improve coverage of
// conditions that might not otherwise be encountered on the test data.
var gTestPrefs = [
  ["media.recorder.max_memory", 1024],
  ["media.audio-max-decode-error", 0],
  ["media.video-max-decode-error", 0],
];

// When true, we'll loop forever on whatever test we run. Use this to debug
// intermittent test failures.
const DEBUG_TEST_LOOP_FOREVER = false;

// Manages a run of media tests. Runs them in chunks in order to limit
// the number of media elements/threads running in parallel. This limits peak
// memory use, particularly on Linux x86 where thread stacks use 10MB of
// virtual address space.
// Usage:
//   1. Create a new MediaTestManager object.
//   2. Create a test startTest function. This takes a test object and a token,
//      and performs anything necessary to start the test. The test object is an
//      element in one of the g*Tests above. Your startTest function must call
//      MediaTestManager.start(token) if it starts a test. The test object is
//      guaranteed to be playable by our supported decoders; you don't need to
//      check canPlayType.
//   3. When your tests finishes, call MediaTestManager.finished(), passing
//      the token back to the manager. The manager may either start the next run
//      or end the mochitest if all the tests are done.
function MediaTestManager() {
  // Set a very large timeout to prevent Mochitest timeout.
  // Instead MediaTestManager will manage timeout of each test.
  SimpleTest.requestLongerTimeout(1000);

  // Return how many seconds elapsed since |begin|.
  function elapsedTime(begin) {
    var end = new Date();
    return (end.getTime() - begin.getTime()) / 1000;
  }
  // Sets up a MediaTestManager to runs through the 'tests' array, which needs
  // to be one of, or have the same fields as, the g*Test arrays of tests. Uses
  // the user supplied 'startTest' function to initialize the test. This
  // function must accept two arguments, the test entry from the 'tests' array,
  // and a token. Call MediaTestManager.started(token) if you start the test,
  // and MediaTestManager.finished(token) when the test finishes. You don't have
  // to start every test, but if you call started() you *must* call finish()
  // else you'll timeout.
  this.runTests = function (tests, startTest) {
    this.startTime = new Date();
    SimpleTest.info(
      "Started " +
        this.startTime +
        " (" +
        this.startTime.getTime() / 1000 +
        "s)"
    );
    this.testNum = 0;
    this.tests = tests;
    this.startTest = startTest;
    this.tokens = [];
    this.isShutdown = false;
    this.numTestsRunning = 0;
    this.handlers = {};
    this.timers = {};

    // Always wait for explicit finish.
    SimpleTest.waitForExplicitFinish();
    SpecialPowers.pushPrefEnv({ set: gTestPrefs }, () => {
      this.nextTest();
    });

    SimpleTest.registerCleanupFunction(() => {
      if (this.tokens.length) {
        info("Test timed out. Remaining tests=" + this.tokens);
      }
      for (var token of this.tokens) {
        var handler = this.handlers[token];
        if (handler && handler.ontimeout) {
          handler.ontimeout();
        }
      }
    });
  };

  // Registers that the test corresponding to 'token' has been started.
  // Don't call more than once per token.
  this.started = function (token, handler) {
    this.tokens.push(token);
    this.numTestsRunning++;
    this.handlers[token] = handler;

    var onTimeout = async () => {
      ok(false, "Test timed out!");
      info(`${token} timed out!`);
      await dumpDebugInfoForToken(token);
      this.finished(token);
    };
    // Default timeout to 180s for each test.
    // Call SimpleTest._originalSetTimeout() to bypass the flaky timeout checker.
    this.timers[token] = SimpleTest._originalSetTimeout.call(
      window,
      onTimeout,
      180000
    );

    is(
      this.numTestsRunning,
      this.tokens.length,
      "[started " +
        token +
        " t=" +
        elapsedTime(this.startTime) +
        "] Length of array should match number of running tests"
    );
  };

  // Registers that the test corresponding to 'token' has finished. Call when
  // you've finished your test. If all tests are complete this will finish the
  // run, otherwise it may start up the next run. It's ok to call multiple times
  // per token.
  this.finished = function (token) {
    var i = this.tokens.indexOf(token);
    if (i != -1) {
      // Remove the element from the list of running tests.
      this.tokens.splice(i, 1);
    }

    if (this.timers[token]) {
      // Cancel the timer when the test finishes.
      clearTimeout(this.timers[token]);
      this.timers[token] = null;
    }

    info("[finished " + token + "] remaining= " + this.tokens);
    this.numTestsRunning--;
    is(
      this.numTestsRunning,
      this.tokens.length,
      "[finished " +
        token +
        " t=" +
        elapsedTime(this.startTime) +
        "] Length of array should match number of running tests"
    );
    if (this.tokens.length < PARALLEL_TESTS) {
      this.nextTest();
    }
  };

  // Starts the next batch of tests, or finishes if they're all done.
  // Don't call this directly, call finished(token) when you're done.
  this.nextTest = function () {
    while (
      this.testNum < this.tests.length &&
      this.tokens.length < PARALLEL_TESTS
    ) {
      var test = this.tests[this.testNum];
      var token = (test.name ? test.name + "-" : "") + this.testNum;
      this.testNum++;

      if (DEBUG_TEST_LOOP_FOREVER && this.testNum == this.tests.length) {
        this.testNum = 0;
      }

      // Ensure we can play the resource type.
      if (
        test.type &&
        !document.createElement("video").canPlayType(test.type)
      ) {
        SimpleTest.info(`Skipping ${token} type=${test.type}`);
        continue;
      }

      // Do the init. This should start the test.
      this.startTest(test, token);
    }

    if (
      this.testNum == this.tests.length &&
      !DEBUG_TEST_LOOP_FOREVER &&
      !this.tokens.length &&
      !this.isShutdown
    ) {
      this.isShutdown = true;
      if (this.onFinished) {
        this.onFinished();
      }
      var onCleanup = () => {
        var end = new Date();
        SimpleTest.info(
          "Finished at " + end + " (" + end.getTime() / 1000 + "s)"
        );
        SimpleTest.info("Running time: " + elapsedTime(this.startTime) + "s");
        SimpleTest.finish();
      };
      mediaTestCleanup(onCleanup);
    }
  };
}

// Ensures we've got no active video or audio elements in the document, and
// forces a GC to release the address space reserved by the decoders' threads'
// stacks.
function mediaTestCleanup(callback) {
  var V = document.getElementsByTagName("video");
  for (let i = 0; i < V.length; i++) {
    removeNodeAndSource(V[i]);
    V[i] = null;
  }
  var A = document.getElementsByTagName("audio");
  for (let i = 0; i < A.length; i++) {
    removeNodeAndSource(A[i]);
    A[i] = null;
  }
  SpecialPowers.exactGC(callback);
}

async function dumpDebugInfoForToken(token) {
  for (let v of document.getElementsByTagName("video")) {
    if (token === v.token) {
      info(JSON.stringify(await SpecialPowers.wrap(v).mozRequestDebugInfo()));
      return;
    }
  }
  for (let a of document.getElementsByTagName("audio")) {
    if (token === a.token) {
      info(JSON.stringify(await SpecialPowers.wrap(a).mozRequestDebugInfo()));
      return;
    }
  }
}

// Could be undefined in a page opened by the parent test page
// like file_access_controls.html.
if ("SimpleTest" in window) {
  SimpleTest.requestFlakyTimeout("untriaged");

  // Register timeout function to dump debugging logs.
  SimpleTest.registerTimeoutFunction(async function () {
    for (const v of document.getElementsByTagName("video")) {
      SimpleTest.info(
        JSON.stringify(await SpecialPowers.wrap(v).mozRequestDebugInfo())
      );
    }
    for (const a of document.getElementsByTagName("audio")) {
      SimpleTest.info(
        JSON.stringify(await SpecialPowers.wrap(a).mozRequestDebugInfo())
      );
    }
  });
}
