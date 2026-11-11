/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.store

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

@RunWith(AndroidJUnit4::class)
class TabsTrayStoreTest {
    @Test
    fun `WHEN the store is initialized THEN the attached middleware are notified`() = runTest {
        val captureMiddleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        TabsTrayStore(middlewares = listOf(captureMiddleware))
        captureMiddleware.assertLastAction(TabsTrayAction.InitAction::class)
    }
}
