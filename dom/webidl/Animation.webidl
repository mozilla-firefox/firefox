/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://drafts.csswg.org/web-animations/#animation
 *
 * Copyright © 2015 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

enum AnimationPlayState { "idle", "running", "paused", "finished" };

enum AnimationReplaceState { "active", "removed", "persisted" };

[Exposed=Window]
interface Animation : EventTarget {
  [Throws]
  constructor(optional AnimationEffect? effect = null,
              optional AnimationTimeline? timeline);

  attribute DOMString id;
  [Pure]
  attribute AnimationEffect? effect;
  // Bug 1676794. Drop BinaryName once we support ScrollTimeline interface.
  [BinaryName="timelineFromJS"]
  attribute AnimationTimeline? timeline;

  [BinaryName="startTimeAsDouble"]
  attribute double? startTime;
  [SetterThrows, BinaryName="currentTimeAsDouble"]
  attribute double? currentTime;

           attribute double             playbackRate;
  [BinaryName="playStateFromJS"]
  readonly attribute AnimationPlayState playState;
  [BinaryName="pendingFromJS"]
  readonly attribute boolean            pending;
  readonly attribute AnimationReplaceState replaceState;
  [Throws]
  readonly attribute Promise<Animation> ready;
  [Throws]
  readonly attribute Promise<Animation> finished;
           attribute EventHandler       onfinish;
           attribute EventHandler       oncancel;
           attribute EventHandler       onremove;
  undefined cancel();
  [Throws]
  undefined finish();
  [Throws, BinaryName="playFromJS"]
  undefined play();
  [Throws, BinaryName="pauseFromJS"]
  undefined pause();
  undefined updatePlaybackRate (double playbackRate);
  [Throws]
  undefined reverse();
  undefined persist();
  [CEReactions, Throws, UseCounter]
  undefined commitStyles();
};

// Non-standard extensions
partial interface Animation {
  [ChromeOnly] readonly attribute boolean isRunningOnCompositor;
};
