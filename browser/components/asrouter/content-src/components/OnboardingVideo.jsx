/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";

export const OnboardingVideo = props => {
  const vidUrl = props.content.video_url;
  const autoplay = props.content.autoPlay;

  const handleVideoAction = event => {
    props.handleAction({
      currentTarget: {
        value: event,
      },
    });
  };

  return (
    <div>
      <video // eslint-disable-line jsx-a11y/media-has-caption
        controls={true}
        autoPlay={autoplay}
        src={vidUrl}
        width="604px"
        height="340px"
        onPlay={() => handleVideoAction("video_start")}
        onEnded={() => handleVideoAction("video_end")}
      >
        <source src={vidUrl}></source>
      </video>
    </div>
  );
};
