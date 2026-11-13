/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.pictureinpicture.AbstractPictureInPictureActivity
import mozilla.components.feature.pictureinpicture.PictureInPictureUseCases
import org.mozilla.fenix.ext.components

/** Shows a video in a picture-in-picture window of its own, and on expanding returns to the tab it came from. */
class PictureInPictureActivity : AbstractPictureInPictureActivity() {
    override val store: BrowserStore by lazy { components.core.store }
    override val engine: Engine by lazy { components.core.engine }
    override val pictureInPictureUseCases: PictureInPictureUseCases by lazy {
        components.useCases.pictureInPictureUseCases
    }
}
