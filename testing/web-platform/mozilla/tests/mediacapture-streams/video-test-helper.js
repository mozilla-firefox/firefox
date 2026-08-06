// Helper functions checking video flow using HTMLVideoElement.

async function test_resolution_equals(t, track, width, height) {
  const video = document.createElement("video");
  const timeout = new Promise(r => t.step_timeout(r, 1000));
  const waitForResize = () => Promise.race([
        new Promise(r => video.onresize = r),
        timeout.then(() => { throw new Error("Timeout waiting for resize"); }),
      ]);
  video.srcObject = new MediaStream([track]);
  video.play();
  // Wait for the first frame.
  await waitForResize();
  // There's a potential race with applyConstraints, where a frame of the old
  // resolution is in flight and renders after applyConstraints has resolved.
  // In that case, wait for another frame.
  if (video.videoWidth != width || video.videoHeight != height) {
    await waitForResize();
  }

  assert_equals(video.videoWidth, width, "videoWidth");
  assert_equals(video.videoHeight, height, "videoHeight");
}

async function test_framerate_between_exclusive(t, track, lower, upper) {
  const video = document.createElement("video");
  document.body.appendChild(video);
  video.style = "width:320px;height:240px;"
  t.add_cleanup(async () => document.body.removeChild(video));

  video.srcObject = new MediaStream([track]);
  await video.play();

  const numSeconds = 2;
  const startTime = performance.now();
  await new Promise(r => setTimeout(r, numSeconds * 1000));
  const totalVideoFrames = video.mozPaintedFrames;
  const elapsedSeconds = (performance.now() - startTime) / 1000;
  assert_between_exclusive(totalVideoFrames / elapsedSeconds, lower, upper,
                           "totalVideoFrames");
}
