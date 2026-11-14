/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state

import mozilla.components.concept.engine.EngineSession

/**
 * Represents the state of the window showing a video in picture-in-picture.
 *
 * @property session The session cloning a video.
 * @property requestedTabId ID of the tab whose video the window is waiting on.
 */
data class PictureInPictureWindowState(
    val session: PictureInPictureSession? = null,
    val requestedTabId: String? = null,
)

/**
 * A session cloning the video of a tab into a picture-in-picture window.
 *
 * @property tabId ID of the tab the video comes from.
 * @property engineSession The session cloning the video.
 */
data class PictureInPictureSession(
    val tabId: String,
    val engineSession: EngineSession,
)
