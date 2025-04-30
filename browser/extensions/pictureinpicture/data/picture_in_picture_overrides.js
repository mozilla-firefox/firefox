/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals browser */

let AVAILABLE_PIP_OVERRIDES;

{
  // See PictureInPictureControls.sys.mjs for these values.
  // eslint-disable-next-line no-unused-vars
  const TOGGLE_POLICIES = browser.pictureInPictureChild.getPolicies();
  const KEYBOARD_CONTROLS = browser.pictureInPictureChild.getKeyboardControls();

  AVAILABLE_PIP_OVERRIDES = {
    // The keys of this object are match patterns for URLs, as documented in
    // https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns
    //
    // Example:
    //  const KEYBOARD_CONTROLS = browser.pictureInPictureChild.getKeyboardControls();
    //
    //
    // "https://*.youtube.com/*": {
    //   policy: TOGGLE_POLICIES.THREE_QUARTERS,
    //   disabledKeyboardControls: KEYBOARD_CONTROLS.PLAY_PAUSE | KEYBOARD_CONTROLS.VOLUME,
    // },
    // "https://*.twitch.tv/mikeconley_dot_ca/*": {
    //   policy: TOGGLE_POLICIES.TOP,
    //   disabledKeyboardControls: KEYBOARD_CONTROLS.ALL,
    // },

    tests: {
      // FOR TESTS ONLY!
      "https://mochitest.youtube.com/*browser/browser/extensions/pictureinpicture/tests/browser/test-mock-wrapper.html":
        {
          videoWrapperScriptPath: "video-wrappers/mock-wrapper.js",
        },
      "https://mochitest.youtube.com/*browser/browser/extensions/pictureinpicture/tests/browser/test-toggle-visibility.html":
        {
          videoWrapperScriptPath: "video-wrappers/mock-wrapper.js",
        },
    },

    abcnews: {
      "https://*.abcnews.go.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    airmozilla: {
      "https://*.mozilla.hosted.panopto.com/*": {
        videoWrapperScriptPath: "video-wrappers/airmozilla.js",
      },
    },

    aol: {
      "https://*.aol.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    arte: {
      "https://*.arte.tv/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    bbc: {
      "https://*.bbc.com/*": {
        videoWrapperScriptPath: "video-wrappers/bbc.js",
      },
      "https://*.bbc.co.uk/*": {
        videoWrapperScriptPath: "video-wrappers/bbc.js",
      },
    },

    brightcove: {
      "https://*.brightcove.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    canalplus: {
      "https://*.canalplus.com/live/*": {
        videoWrapperScriptPath: "video-wrappers/canalplus.js",
        disabledKeyboardControls: KEYBOARD_CONTROLS.LIVE_SEEK,
      },
      "https://*.canalplus.com/*": {
        videoWrapperScriptPath: "video-wrappers/canalplus.js",
      },
    },

    cbc: {
      "https://*.cbc.ca/*": {
        videoWrapperScriptPath: "video-wrappers/cbc.js",
      },
    },

    cnbc: {
      "https://*.cnbc.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    cpac: {
      "https://*.cpac.ca/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    cspan: {
      "https://*.c-span.org/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    dailymotion: {
      "https://*.dailymotion.com/*": {
        videoWrapperScriptPath: "video-wrappers/dailymotion.js",
      },
    },

    disneyplus: {
      "https://*.disneyplus.com/*": {
        videoWrapperScriptPath: "video-wrappers/disneyplus.js",
      },
    },

    edx: {
      "https://*.edx.org/*": {
        videoWrapperScriptPath: "video-wrappers/edx.js",
      },
    },

    fandom: {
      "https://*.fandom.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    fastcompany: {
      "https://*.fastcompany.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    frontendMasters: {
      "https://*.frontendmasters.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    funimation: {
      "https://*.funimation.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    fuse: {
      "https://*.fuse.tv/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    hbomax: {
      "https://play.hbomax.com/page/*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://play.hbomax.com/player/*": {
        videoWrapperScriptPath: "video-wrappers/hbomax.js",
      },
    },

    hotstar: {
      "https://*.hotstar.com/*": {
        videoWrapperScriptPath: "video-wrappers/hotstar.js",
      },
    },

    hulu: {
      "https://www.hulu.com/watch/*": {
        policy: TOGGLE_POLICIES.ONE_QUARTER,
        videoWrapperScriptPath: "video-wrappers/hulu.js",
      },
    },

    imdb: {
      "https://*.imdb.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    indpendentuk: {
      "https://*.independent.co.uk/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    indy100: {
      "https://*.indy100.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    instagram: {
      "https://www.instagram.com/*": { policy: TOGGLE_POLICIES.ONE_QUARTER },
    },

    internetArchive: {
      "https://*.archive.org/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    iq: {
      "https://*.iq.com/play/*": {
        videoWrapperScriptPath: "video-wrappers/iq.js",
      },
    },

    kick: {
      "https://*.kick.com/*": {
        visibilityThreshold: 0.7,
      },
    },

    joyn: {
      "https://www.joyn.de/play/*": {
        videoWrapperScriptPath: "video-wrappers/joyn.js",
      },
      "https://www.joyn.at/play/*": {
        videoWrapperScriptPath: "video-wrappers/joyn.js",
      },
      "https://www.joyn.ch/play/*": {
        videoWrapperScriptPath: "video-wrappers/joyn.js",
      },
    },

    laracasts: {
      "https://*.laracasts.com/*": { policy: TOGGLE_POLICIES.ONE_QUARTER },
    },

    msn: {
      "https://*.msn.com/*": {
        visibilityThreshold: 0.7,
      },
    },

    msnbc: {
      "https://*.msnbc.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    mxplayer: {
      "https://*.mxplayer.in/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    nbcnews: {
      "https://*.nbcnews.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    nbcUniversal: {
      "https://*.nbcuni.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    nebula: {
      "https://*.nebula.app/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    netflix: {
      "https://*.netflix.com/*": {
        videoWrapperScriptPath: "video-wrappers/netflix.js",
      },
      "https://*.netflix.com/browse*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://*.netflix.com/latest*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://*.netflix.com/Kids*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://*.netflix.com/title*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://*.netflix.com/notification*": { policy: TOGGLE_POLICIES.HIDDEN },
      "https://*.netflix.com/search*": { policy: TOGGLE_POLICIES.HIDDEN },
    },

    nytimes: {
      "https://*.nytimes.com/*": {
        videoWrapperScriptPath: "video-wrappers/nytimes.js",
      },
    },

    pbs: {
      "https://*.pbs.org/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
      "https://*.pbskids.org/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    piped: {
      "https://*.piped.kavin.rocks/*": {
        videoWrapperScriptPath: "video-wrappers/piped.js",
      },
      "https://*.piped.silkky.cloud/*": {
        videoWrapperScriptPath: "video-wrappers/piped.js",
      },
    },

    primeVideo: {
      "https://*.primevideo.com/*": {
        visibilityThreshold: 0.9,
        videoWrapperScriptPath: "video-wrappers/primeVideo.js",
      },
      "https://*.amazon.com/*": {
        visibilityThreshold: 0.9,
        videoWrapperScriptPath: "video-wrappers/primeVideo.js",
      },
    },

    radiocanada: {
      "https://*.ici.radio-canada.ca/*": {
        videoWrapperScriptPath: "video-wrappers/radiocanada.js",
      },
    },

    reddit: {
      "https://*.reddit.com/*": { policy: TOGGLE_POLICIES.ONE_QUARTER },
    },

    reuters: {
      "https://*.reuters.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    rte: {
      "https://www.rte.ie/player/*": {
        videoWrapperScriptPath: "video-wrappers/hotstar.js",
      },
    },

    sonyliv: {
      "https://*.sonyliv.com/*": {
        videoWrapperScriptPath: "video-wrappers/sonyliv.js",
      },
    },

    syfy: {
      "https://*.syfy.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    ted: {
      "https://*.ted.com/*": {
        showHiddenTextTracks: true,
      },
    },

    time: {
      "https://*.time.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    timvision: {
      "https://*.timvision.it/TV/*": {
        videoWrapperScriptPath: "video-wrappers/canalplus.js",
        disabledKeyboardControls: KEYBOARD_CONTROLS.LIVE_SEEK,
      },
      "https://*.timvision.it/*": {
        videoWrapperScriptPath: "video-wrappers/canalplus.js",
      },
    },

    tubi: {
      "https://*.tubitv.com/live*": {
        videoWrapperScriptPath: "video-wrappers/tubilive.js",
      },
      "https://*.tubitv.com/movies*": {
        videoWrapperScriptPath: "video-wrappers/tubi.js",
      },
      "https://*.tubitv.com/tv-shows*": {
        videoWrapperScriptPath: "video-wrappers/tubi.js",
      },
    },

    twitch: {
      "https://*.twitch.tv/*": {
        videoWrapperScriptPath: "video-wrappers/twitch.js",
        policy: TOGGLE_POLICIES.ONE_QUARTER,
        disabledKeyboardControls: KEYBOARD_CONTROLS.LIVE_SEEK,
      },
      "https://*.twitch.tech/*": {
        videoWrapperScriptPath: "video-wrappers/twitch.js",
        policy: TOGGLE_POLICIES.ONE_QUARTER,
        disabledKeyboardControls: KEYBOARD_CONTROLS.LIVE_SEEK,
      },
      "https://*.twitch.a2z.com/*": {
        videoWrapperScriptPath: "video-wrappers/twitch.js",
        policy: TOGGLE_POLICIES.ONE_QUARTER,
        disabledKeyboardControls: KEYBOARD_CONTROLS.LIVE_SEEK,
      },
    },

    udemy: {
      "https://*.udemy.com/*": {
        videoWrapperScriptPath: "video-wrappers/udemy.js",
        policy: TOGGLE_POLICIES.ONE_QUARTER,
      },
    },

    univision: {
      "https://*.univision.com/*": {
        videoWrapperScriptPath: "video-wrappers/jwplayerWrapper.js",
      },
    },

    viki: {
      "https://*.viki.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    vimeo: {
      "https://*.vimeo.com/*": {
        showHiddenTextTracks: true,
      },
    },

    voot: {
      "https://*.voot.com/*": {
        videoWrapperScriptPath: "video-wrappers/voot.js",
      },
    },

    wired: {
      "https://*.wired.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    yahoo: {
      "https://*.s.yimg.com/*": {
        videoWrapperScriptPath: "video-wrappers/videojsWrapper.js",
      },
    },

    youtube: {
      /**
       * The threshold of 0.7 is so that users can click on the "Skip Ads"
       * button on the YouTube site player without accidentally triggering
       * PiP.
       */
      "https://*.youtube.com/*": {
        visibilityThreshold: 0.7,
        videoWrapperScriptPath: "video-wrappers/youtube.js",
      },
      "https://*.youtube-nocookie.com/*": {
        visibilityThreshold: 0.9,
        videoWrapperScriptPath: "video-wrappers/youtube.js",
      },
    },

    washingtonpost: {
      "https://*.washingtonpost.com/*": {
        videoWrapperScriptPath: "video-wrappers/washingtonpost.js",
      },
    },

    zdf: {
      "https://*.zdf.de/*": {
        videoWrapperScriptPath: "video-wrappers/zdf.js",
      },
      "https://*.3sat.de/*": {
        videoWrapperScriptPath: "video-wrappers/zdf.js",
      },
      "https://*.phoenix.de/*": {
        videoWrapperScriptPath: "video-wrappers/zdf.js",
      },
    },
  };
}
