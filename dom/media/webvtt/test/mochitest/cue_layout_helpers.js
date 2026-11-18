/* import-globals-from manifest.js */

// Helpers for tests that check the layout of displayed cues. They need
// manifest.js.

const VIDEO_WIDTH = 320;
const VIDEO_HEIGHT = 180;

function createVideo() {
  const video = document.createElement("video");
  video.src = "seek.webm";
  video.width = VIDEO_WIDTH;
  video.height = VIDEO_HEIGHT;
  document.body.appendChild(video);
  return video;
}

// Shows the cues on the video and returns their cue box elements once all of
// them are displayed.
async function showCues(video, cues) {
  const track = video.addTextTrack("subtitles", "cues", "en");
  for (const cue of cues) {
    track.addCue(cue);
  }
  track.mode = "showing";
  const entered = Promise.all(cues.map(c => once(c, "enter")));
  await video.play();
  await entered;
  video.pause();

  const chromeCues = cues.map(c => SpecialPowers.wrap(c));
  await SimpleTest.promiseWaitForCondition(
    () => chromeCues.every(c => c.displayState?.isConnected),
    "Wait for all cues to be displayed"
  );
  return chromeCues.map(c => c.displayState);
}

// Returns the computed style of a cue element, which only chrome can reach.
function computedStyle(element) {
  return SpecialPowers.wrap(window).getComputedStyle(element);
}

// Returns the cue background box, the inline box that holds the cue nodes.
function backgroundBoxOf(cueBox) {
  return cueBox.firstElementChild;
}

// Returns the inline box around the text of the cue box.
function textBoxOf(cueBox) {
  let node = cueBox;
  while (node.nodeType != Node.TEXT_NODE) {
    node = node.firstChild;
  }
  return node.parentNode;
}
