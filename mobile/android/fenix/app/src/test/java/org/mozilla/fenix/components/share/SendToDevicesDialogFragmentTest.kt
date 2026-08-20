/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.share

import android.os.Bundle
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.TabData
import mozilla.components.concept.sync.TabPrivacy
import mozilla.components.service.fxa.manager.FxaAccountManager
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class SendToDevicesDialogFragmentTest {

    private val mockAccountManager = mockk<FxaAccountManager>(relaxed = true)
    private lateinit var fragment: SendToDevicesDialogFragment

    @Before
    fun setUp() {
        fragment =
            spyk(
                SendToDevicesDialogFragment.newInstance(
                    urls = listOf("https://example.com"),
                    titles = listOf("Title"),
                    isPrivate = false,
                )
            )
        every { fragment.navigateToSignIn() } just runs
        every { fragment.onAuthenticated() } just runs
    }

    // region loadTabData

    @Test
    fun `GIVEN bundle with PRIVATE privacy WHEN loadTabData is called THEN tabs use Private privacy`() {
        val bundle =
            Bundle().apply {
                putStringArrayList("urls", arrayListOf("https://example.com"))
                putString("privacy", "PRIVATE")
            }

        fragment.loadTabData(bundle)

        assertEquals(listOf(TabPrivacy.Private), fragment.tabsForTest.map { it.privacy })
    }

    @Test
    fun `GIVEN bundle without privacy extra WHEN loadTabData is called THEN tabs default to Normal privacy`() {
        val bundle = Bundle().apply { putStringArrayList("urls", arrayListOf("https://example.com")) }

        fragment.loadTabData(bundle)

        assertEquals(listOf(TabPrivacy.Normal), fragment.tabsForTest.map { it.privacy })
    }

    @Test
    fun `GIVEN bundle with urls and titles WHEN loadTabData is called THEN tabs are updated`() {
        val bundle =
            Bundle().apply {
                putStringArrayList("urls", arrayListOf("https://mozilla.org", "https://example.com"))
                putStringArrayList("titles", arrayListOf("Mozilla", "Example"))
            }

        fragment.loadTabData(bundle)

        assertEquals(
            listOf(
                TabData("Mozilla", "https://mozilla.org", TabPrivacy.Normal),
                TabData("Example", "https://example.com", TabPrivacy.Normal),
            ),
            fragment.tabsForTest,
        )
    }

    @Test
    fun `GIVEN a url with a missing title WHEN loadTabData is called THEN the tab title defaults to empty`() {
        val bundle =
            Bundle().apply {
                putStringArrayList("urls", arrayListOf("https://mozilla.org"))
            }

        fragment.loadTabData(bundle)

        assertEquals(listOf(TabData("", "https://mozilla.org", TabPrivacy.Normal)), fragment.tabsForTest)
    }

    @Test
    fun `GIVEN null bundle WHEN loadTabData is called THEN tabs are empty`() {
        fragment.loadTabData(null)

        assertTrue(fragment.tabsForTest.isEmpty())
    }

    // endregion

    // region checkAuthAndNavigate

    @Test
    fun `GIVEN unauthenticated account WHEN checkAuthAndNavigate is called THEN navigateToSignIn is called`() {
        every { mockAccountManager.authenticatedAccount() } returns null

        fragment.checkAuthAndNavigate(mockAccountManager)

        verify { fragment.navigateToSignIn() }
    }

    @Test
    fun `GIVEN authenticated account WHEN checkAuthAndNavigate is called THEN navigateToSignIn is not called`() {
        every { mockAccountManager.authenticatedAccount() } returns mockk<OAuthAccount>()

        fragment.checkAuthAndNavigate(mockAccountManager)

        verify(exactly = 0) { fragment.navigateToSignIn() }
    }

    @Test
    fun `GIVEN unauthenticated account WHEN checkAuthAndNavigate is called twice THEN navigateToSignIn is called only once`() {
        every { mockAccountManager.authenticatedAccount() } returns null

        fragment.checkAuthAndNavigate(mockAccountManager)
        fragment.checkAuthAndNavigate(mockAccountManager)

        verify(exactly = 1) { fragment.navigateToSignIn() }
    }

    // endregion

    // region showSendResult

    @Test
    fun `GIVEN a single tab WHEN showSendResult is called with success THEN the single-tab message is shown`() =
        runTest(UnconfinedTestDispatcher()) {
            fragment.loadTabData(Bundle().apply { putStringArrayList("urls", arrayListOf("https://example.com")) })
            var shownText: Int? = null

            fragment.showSendResult(
                retryScope = backgroundScope,
                onSuccess = { text -> shownText = text },
                onFailure = {},
                send = { true },
            )

            assertEquals(R.string.sync_sent_tab_snackbar_2, shownText)
        }

    @Test
    fun `GIVEN multiple tabs WHEN showSendResult is called with success THEN the multi-tab message is shown`() =
        runTest(UnconfinedTestDispatcher()) {
            fragment.loadTabData(
                Bundle().apply {
                    putStringArrayList("urls", arrayListOf("https://mozilla.org", "https://example.com"))
                }
            )
            var shownText: Int? = null

            fragment.showSendResult(
                retryScope = backgroundScope,
                onSuccess = { text -> shownText = text },
                onFailure = {},
                send = { true },
            )

            assertEquals(R.string.sync_sent_tabs_snackbar_2, shownText)
        }

    @Test
    fun `WHEN showSendResult is called with failure THEN a retry action is offered`() =
        runTest(UnconfinedTestDispatcher()) {
            var retry: (() -> Unit)? = null

            fragment.showSendResult(
                retryScope = backgroundScope,
                onSuccess = {},
                onFailure = { onRetry -> retry = onRetry },
                send = { false },
            )

            kotlin.test.assertNotNull(retry)
        }

    @Test
    fun `GIVEN a failed send WHEN the retry action is invoked and succeeds THEN the success message is shown`() =
        runTest(UnconfinedTestDispatcher()) {
            fragment.loadTabData(Bundle().apply { putStringArrayList("urls", arrayListOf("https://example.com")) })
            var retry: (() -> Unit)? = null
            var shownText: Int? = null
            var callCount = 0

            fragment.showSendResult(
                retryScope = backgroundScope,
                onSuccess = { text -> shownText = text },
                onFailure = { onRetry -> retry = onRetry },
                send = {
                    callCount++
                    callCount > 1
                },
            )
            retry?.invoke()

            assertEquals(R.string.sync_sent_tab_snackbar_2, shownText)
        }

    @Test
    fun `GIVEN a failed send WHEN the retry action is invoked and fails again THEN a new retry action is offered`() =
        runTest(UnconfinedTestDispatcher()) {
            var failureCount = 0
            var retry: (() -> Unit)? = null

            fragment.showSendResult(
                retryScope = backgroundScope,
                onSuccess = {},
                onFailure = { onRetry ->
                    failureCount++
                    retry = onRetry
                },
                send = { false },
            )
            retry?.invoke()

            assertEquals(2, failureCount)
        }

    // endregion
}

@Suppress("UNCHECKED_CAST")
private val SendToDevicesDialogFragment.tabsForTest: List<TabData>
    get() = javaClass.getDeclaredField("tabs").apply { isAccessible = true }.get(this) as List<TabData>
