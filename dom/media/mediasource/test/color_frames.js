// Helpers for tests whose media files have one solid color per frame: red,
// green and blue in turn, in display order.

const kRed = [255, 0, 0];
const kGreen = [0, 255, 0];
const kBlue = [0, 0, 255];
// The color of the frame at display index N is kFrameColors[N % 3].
const kFrameColors = [kRed, kGreen, kBlue];

// The media files with frames colored as above, at 15360 ticks per second.
const kColorFiles = {
  // Two B-frames per group and three frames per fragment, so a fragment splits
  // a group.
  splitGop: "split_gop_across_fragments.mp4",
  // Five frames; the red frame at 2048 lasts until 17920, in fragment 2.
  longFrame: "long_frame_across_fragments.mp4",
  // 60 frames with two B-frames and three frames per fragment.
  colorCycle: "color_cycle_across_fragments.mp4",
  // Two frames; the duration of the first one runs past the second one.
  overlap: "overlap_across_fragments.mp4",
  // Two key frames with a display gap of 1 s.
  gapBetweenKeyframes: "gap_between_keyframes.mp4",
  // 12 frames with two B-frames and one frame per fragment, so the B-frames
  // come after the P-frame that follows them in display order.
  oneFramePerFragment: "one_frame_per_fragment.mp4",
};

// Seeks the paused video to time and returns the RGBA of its center pixel.
async function colorAt(video, time) {
  video.currentTime = time;
  await once(video, "seeked");
  const canvas = document.createElement("canvas");
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  const context = canvas.getContext("2d");
  context.drawImage(video, 0, 0);
  return context.getImageData(canvas.width / 2, canvas.height / 2, 1, 1).data;
}

// Lossy coding and the YUV to RGB conversion move each channel a little, so
// compare with a fuzz.
function isColor(actual, expected, name) {
  const fuzz = 16;
  for (let i = 0; i < 3; i++) {
    isfuzzy(actual[i], expected[i], fuzz, `${name}, channel ${i}`);
  }
}
