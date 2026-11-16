/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Changing a media.rdd-* pref after startup must not break media in content
// processes launched afterwards, see bug 2088719.

async function withNewProcessTab(aTask) {
  await SpecialPowers.pushPrefEnv({
    set: [["media.rdd-vpx.enabled", false]],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "https://example.com/browser/dom/media/test/browser/file_empty_page.html",
      forceNewProcess: true,
    },
    browser => SpecialPowers.spawn(browser, [], aTask)
  );

  await SpecialPowers.popPrefEnv();
}

add_task(async function test_decodingInfo_after_rdd_pref_change() {
  await withNewProcessTab(async () => {
    const info = await content.navigator.mediaCapabilities.decodingInfo({
      type: "file",
      video: {
        contentType: 'video/webm; codecs="vp9"',
        width: 320,
        height: 240,
        bitrate: 100000,
        framerate: 30,
      },
    });
    ok(info.supported, "VP9 decoding is reported as supported");
  });
});

add_task(async function test_video_load_after_rdd_pref_change() {
  await withNewProcessTab(async () => {
    const video = content.document.createElement("video");
    content.document.body.appendChild(video);
    video.src = "gizmo.webm";
    await new Promise((resolve, reject) => {
      video.onloadeddata = resolve;
      video.onerror = () => reject(video.error);
    });
    ok(true, "Video data loaded");
  });
});
