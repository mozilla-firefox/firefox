/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import io.mockk.mockk
import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class IPProtectionPromptPreferencesMiddlewareTest {

    private lateinit var settings: Settings

    private lateinit var repository: DefaultIPProtectionPromptRepository

    private lateinit var middleware: IPProtectionPromptPreferencesMiddleware

    @Before
    fun setup() {
        settings = Settings(testContext)
        repository = DefaultIPProtectionPromptRepository(
            settings = settings,
            installedTimeMillis = { 0L },
        )
        middleware = IPProtectionPromptPreferencesMiddleware(repository)
    }

    @Test
    fun `WHEN the OnPromptCreated action is received THEN the repository knows the prompt is showing`() {
        assertFalse(repository.isShowingPrompt)

        middleware.invoke(
            store = mockk(),
            next = {},
            action = IPProtectionPromptAction.OnPromptCreated,
        )

        assertTrue(repository.isShowingPrompt)
    }

    @Test
    fun `WHEN the OnPromptDismissed action is received THEN the repository knows the prompt is no longer showing`() {
        repository.isShowingPrompt = true

        middleware.invoke(
            store = mockk(),
            next = {},
            action = IPProtectionPromptAction.OnPromptDismissed,
        )

        assertFalse(repository.isShowingPrompt)
    }

    @Test
    fun `WHEN action is noop THEN the repository state is not updated`() {
        assertNoOpAction(IPProtectionPromptAction.OnImpression(Surface.HOMEPAGE))
        assertNoOpAction(IPProtectionPromptAction.OnGetStartedClicked(Surface.HOMEPAGE))
        assertNoOpAction(IPProtectionPromptAction.OnNotNowClicked(Surface.HOMEPAGE))
        assertNoOpAction(IPProtectionPromptAction.OnBrowseWithExtraProtectionClicked(Surface.HOMEPAGE))
        assertNoOpAction(IPProtectionPromptAction.OnPromptManuallyDismissed(Surface.HOMEPAGE))
    }

    private fun assertNoOpAction(action: IPProtectionPromptAction) {
        repository.isShowingPrompt = false
        middleware.invoke(store = mockk(), next = {}, action = action)
        assertFalse(repository.isShowingPrompt)

        repository.isShowingPrompt = true
        middleware.invoke(store = mockk(), next = {}, action = action)
        assertTrue(repository.isShowingPrompt)
    }
}
