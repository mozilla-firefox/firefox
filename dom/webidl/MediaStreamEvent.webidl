/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The addstream event defined herein is part of the legacy
 * interface extensions described by
 *   https://w3c.github.io/webrtc-pc/#legacy-interface-extensions
 * The event itself was removed. See
 *   https://blog.mozilla.org/webrtc/the-evolution-of-webrtc/
 * for background information.
 */

dictionary MediaStreamEventInit : EventInit {
    MediaStream? stream = null;
};

[Pref="media.peerconnection.enabled",
 Exposed=Window]
interface MediaStreamEvent : Event {
  constructor(DOMString type, optional MediaStreamEventInit eventInitDict = {});

  readonly attribute MediaStream? stream;
};
